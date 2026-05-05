/* Copyright 2023 fagci
 * https://github.com/fagci
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */
#include "app/spectrum.h"
#include "am_fix.h"
#include "audio.h"
#include "misc.h"

#ifdef ENABLE_SCAN_RANGES
#include "chFrScanner.h"
#endif

#include "driver/backlight.h"
#include "frequencies.h"
#include "ui/helper.h"
#include "ui/main.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "screenshot.h"
#endif

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
#include "driver/py25q16.h"
#endif

struct FrequencyBandInfo
{
    uint32_t lower;
    uint32_t upper;
    uint32_t middle;
};

#define F_MIN frequencyBandTable[0].lower
#define F_MAX frequencyBandTable[BAND_N_ELEM - 1].upper

const uint16_t RSSI_MAX_VALUE = 65535;

static uint32_t initialFreq;
static char String[32];

static bool isInitialized = false;
bool isListening = true;
bool monitorMode = false;
bool redrawStatus = true;
bool redrawScreen = false;
bool newScanStart = true;
bool preventKeypress = true;
bool audioState = true;
bool lockAGC = false;

State currentState = SPECTRUM, previousState = SPECTRUM;

PeakInfo peak;
ScanInfo scanInfo;
static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};
static bool menuKeyPendingShort = false;
static bool menuKeyLongHandled = false;

#ifdef ENABLE_SCAN_RANGES
static uint16_t blacklistFreqs[15];
static uint8_t blacklistFreqsIdx;
#endif

const char *bwOptions[] = {"25", "12.5", "6.25"};
const uint8_t modulationTypeTuneSteps[] = {100, 50, 10};
const uint8_t modTypeReg47Values[] = {1, 7, 5};

SpectrumSettings settings = {.stepsCount = STEPS_64,
                             .scanStepIndex = S_STEP_25_0kHz,
                             .frequencyChangeStep = 80000,
                             .scanDelay = 3200,
                             .rssiTriggerLevel = 150,
                             .backlightState = true,
                             .bw = BK4819_FILTER_BW_WIDE,
                             .listenBw = BK4819_FILTER_BW_WIDE,
                             .modulationType = false,
                             .dbMin = -130,
                             .dbMax = -50};

uint32_t fMeasure = 0;
uint32_t currentFreq, tempFreq;
uint16_t rssiHistory[128];

// Peak hold: tracks the highest Y per column with timed decay
static uint8_t  peakHoldY[128];       // Peak Y value per display column (0=top)
static uint8_t  peakHoldAge[64];      // Shared decay timer (1 per 2 columns)
#define PEAK_HOLD_DELAY  15           // Sweeps before decay starts
#define PEAK_HOLD_INIT   0xFF         // "no peak" sentinel (same as SPECTRUM_TOPY_SKIP)

// Cached REG_30 value for scan steps: avoids re-reading it on every SetFScan()
// call (saves 1 SPI read per step = fewer SPI bus events = less SPI-induced audio interference).
static uint16_t scanReg30 = 0;

// Bidirectional sweep: true = left→right (fStart→fEnd), false = right→left.
static bool scanForward = true;
// Alternate sweep start side across full sweep cycles to reduce directional bias.
static bool scanStartFromLeft = true;
// True until the opposite half-sweep is completed.
static bool scanReturnPending = true;

// Optional interlaced progression for large scans (>128 steps).
// 1 = enabled, 0 = disabled.
#ifndef SPECTRUM_INTERLACE_LARGE_SWEEPS
#define SPECTRUM_INTERLACE_LARGE_SWEEPS 1
#endif
#if SPECTRUM_INTERLACE_LARGE_SWEEPS
static uint16_t interlaceStride = 1;
static uint16_t interlacePhase = 0;
#endif

// Incremental display: one framebuffer page sent per tick instead of a full
// BlitFullScreen burst.
static uint8_t renderPage = 0;

// Decoupled render timer: Render() fires every RENDER_PERIOD_TICKS ticks
// regardless of step count, keeping it above the ~9 Hz flutter-fusion
// threshold that would cause an audible "tac" if tied to the sweep rate.
static uint16_t renderTimer = 0;
#define RENDER_PERIOD_TICKS 20

// Disabling automatic DbMax and squelch trigger settings
static bool manualSetFlag = false;

typedef enum AutoSensitivityProfile
{
    AUTO_SENS_WEAK = 0,    // less sensitive (higher margin over noise)
    AUTO_SENS_NORMAL,      // default
    AUTO_SENS_STRONG,      // more sensitive (lower margin over noise)
    AUTO_SENS_N_ELEM
} AutoSensitivityProfile;

// Margin above the measured noise floor used by auto trigger.
// 1 RSSI unit ~= 0.5 dB.
static const uint8_t autoTriggerMarginRssi[AUTO_SENS_N_ELEM] = {
    24, // weak  : +12 dB
    16, // normal:  +8 dB (legacy behavior)
    10, // strong:  +5 dB
};
static const char *autoSensitivityLabel[AUTO_SENS_N_ELEM] = {"WEAK", "NORM", "STRG"};
static AutoSensitivityProfile autoSensitivity = AUTO_SENS_NORMAL;

// Hysteresis and debounce for listen state.
// 1 RSSI unit ~= 0.5 dB.
#define LISTEN_OPEN_HYST_RSSI    4   // +2 dB above trigger to open
#define LISTEN_CLOSE_HYST_RSSI   4   // -2 dB below trigger to keep listening
#define LISTEN_RELEASE_LOW_COUNT 4   // consecutive low reads before release
#define LISTEN_DROP_EXIT_RSSI   20   // 10 dB abrupt drop => leave RX
static uint8_t listenLowCount = 0;
static uint16_t listenPrevRssi = RSSI_MAX_VALUE;
static uint16_t autoNoiseFloor = RSSI_MAX_VALUE;

// EMA-smoothed RSSI for STILL display only (peak.rssi stays raw for trigger)
static uint16_t rssiSmoothed = 0;

// Sweeps remaining before auto-scaling of dbMax resumes (0 = auto)
static uint8_t manualDbMaxTimer = 0;
#define MANUAL_DBMAX_SWEEPS 2
uint8_t vfo;
uint8_t freqInputIndex = 0;
uint8_t freqInputDotIndex = 0;
KEY_Code_t freqInputArr[10];
char freqInputString[11];

uint8_t menuState = 0;
uint16_t listenT = 0;

RegisterSpec registerSpecs[] = {
    {},
    {"LNAs", BK4819_REG_13, 8, 0b11, 1},
    {"LNA", BK4819_REG_13, 5, 0b111, 1},
    {"PGA", BK4819_REG_13, 0, 0b111, 1},
    //{"BPF", BK4819_REG_3D, 0, 0xFFFF, 0x2aaa},
    // {"MIX", 0x13, 3, 0b11, 1}, // TODO: hidden
};

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
const int8_t LNAsOptions[] = {-19, -16, -11, 0};
const int8_t LNAOptions[] = {-24, -19, -14, -9, -6, -4, -2, 0};
const int8_t VGAOptions[] = {-33, -27, -21, -15, -9, -6, -3, 0};
//const char *BPFOptions[] = {"8.46", "7.25", "6.35", "5.64", "5.08", "4.62", "4.23"};

typedef struct {
    const int8_t *options;
    uint8_t count;
} MenuOptions;

static const MenuOptions regOptions[] = {
    {NULL, 0},             // NULL
    {LNAsOptions, 4},      // LNAs
    {LNAOptions, 8},       // LNA
    {VGAOptions, 8}        // VGA
};
#endif

uint16_t statuslineUpdateTimer = 0;

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
static void LoadSettings()
{
    uint8_t Data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    PY25Q16_ReadBuffer(0x00A148, Data, sizeof(Data));

    // Data[0]: scanStepIndex (7:4), stepsCount (3:2), listenBw (1:0)
    settings.scanStepIndex = (Data[0] >> 4) & 0x0F;
    if (settings.scanStepIndex > 14)
        settings.scanStepIndex = S_STEP_25_0kHz;

    settings.stepsCount = (Data[0] >> 2) & 0x03;
    if (settings.stepsCount > 3)
        settings.stepsCount = STEPS_64;

    settings.listenBw = Data[0] & 0x03;
    if (settings.listenBw > 2)
        settings.listenBw = BK4819_FILTER_BW_WIDE;

    // Data[1]: manualSetFlag (0), autoSensitivity (2:1)
    manualSetFlag = Data[1] & 0x01;
    autoSensitivity = (Data[1] >> 1) & 0x03;
    if (autoSensitivity >= AUTO_SENS_N_ELEM)
        autoSensitivity = AUTO_SENS_NORMAL;

    // Data[2]: dbMax encoded as (dbMax + 130) / 5
    if (Data[2] <= 28)
        settings.dbMax = (int)Data[2] * 5 - 130;

    // Data[3]: rssiTriggerLevel as uint8_t (0xFF = auto)
    settings.rssiTriggerLevel = (Data[3] == 0xFF) ? RSSI_MAX_VALUE : Data[3];

    // Data[4] ~ Data[7] are free (for the moment...)
}

static void SaveSettings()
{
    uint8_t Data[8] = {0};
    PY25Q16_ReadBuffer(0x00A148, Data, sizeof(Data));

    // Data[0]: scanStepIndex (7:4), stepsCount (3:2), listenBw (1:0)
    Data[0] = (settings.scanStepIndex << 4) | (settings.stepsCount << 2) | settings.listenBw;

    // Data[1]: manualSetFlag (0), autoSensitivity (2:1)
    Data[1] = (manualSetFlag & 0x01) | ((autoSensitivity & 0x03) << 1);

    // Data[2]: dbMax encoded as (dbMax + 130) / 5
    Data[2] = (uint8_t)((settings.dbMax + 130) / 5);

    // Data[3]: rssiTriggerLevel as uint8_t (0xFF = auto)
    Data[3] = (settings.rssiTriggerLevel == RSSI_MAX_VALUE) ? 0xFF : (uint8_t)settings.rssiTriggerLevel;

    PY25Q16_WriteBuffer(0x00A148, Data, sizeof(Data), false);
}
#endif

static uint8_t DBm2S(int dbm)
{
    uint8_t i = 0;
    dbm *= -1;
    for (i = 0; i < ARRAY_SIZE(U8RssiMap); i++)
    {
        if (dbm >= U8RssiMap[i])
        {
            return i;
        }
    }
    return i;
}

static int Rssi2DBm(uint16_t rssi)
{
    return (rssi / 2) - 160 + dBmCorrTable[gRxVfo->Band];
}

static uint16_t GetRegMenuValue(uint8_t st)
{
    RegisterSpec s = registerSpecs[st];
    return (BK4819_ReadRegister(s.num) >> s.offset) & s.mask;
}

void LockAGC()
{
    //RADIO_SetupAGC(settings.modulationType == MODULATION_AM, lockAGC);
    RADIO_SetupAGC(false, lockAGC);
    //lockAGC = true;
    lockAGC = false;
}

static void SetRegMenuValue(uint8_t st, bool add)
{
    uint16_t v = GetRegMenuValue(st);
    RegisterSpec s = registerSpecs[st];

    if (s.num == BK4819_REG_13)
        LockAGC();

    uint16_t reg = BK4819_ReadRegister(s.num);
    if (add && v <= s.mask - s.inc)
    {
        v += s.inc;
    }
    else if (!add && v >= 0 + s.inc)
    {
        v -= s.inc;
    }
    // TODO: use max value for bits count in max value, or reset by additional
    // mask in spec
    reg &= ~(s.mask << s.offset);
    BK4819_WriteRegister(s.num, reg | (v << s.offset));
    redrawScreen = true;
}

// GUI functions

#ifndef ENABLE_FEAT_F4HWN
static void PutPixel(uint8_t x, uint8_t y, bool fill)
{
    UI_DrawPixelBuffer(gFrameBuffer, x, y, fill);
}
static void PutPixelStatus(uint8_t x, uint8_t y, bool fill)
{
    UI_DrawPixelBuffer(&gStatusLine, x, y, fill);
}
#endif

#ifndef ENABLE_FEAT_F4HWN
static void GUI_DisplaySmallest(const char *pString, uint8_t x, uint8_t y,
                                bool statusbar, bool fill)
{
    uint8_t c;
    uint8_t pixels;
    const uint8_t *p = (const uint8_t *)pString;

    while ((c = *p++) && c != '\0')
    {
        c -= 0x20;
        for (int i = 0; i < 3; ++i)
        {
            pixels = gFont3x5[c][i];
            for (int j = 0; j < 6; ++j)
            {
                if (pixels & 1)
                {
                    if (statusbar)
                        PutPixelStatus(x + i, y + j, fill);
                    else
                        PutPixel(x + i, y + j, fill);
                }
                pixels >>= 1;
            }
        }
        x += 4;
    }
}
#endif

// Utility functions

static int clamp(int v, int min, int max)
{
    return v <= min ? min : (v >= max ? max : v);
}

static uint16_t my_abs(int16_t v) { return v < 0 ? (uint16_t)(-v) : (uint16_t)v; }

void SetState(State state)
{
    previousState = currentState;
    currentState = state;
    redrawScreen = true;
    redrawStatus = true;
}

// Radio functions

static void ToggleAFBit(bool on)
{
    uint16_t reg = BK4819_ReadRegister(BK4819_REG_47);
    reg &= ~(1 << 8);
    if (on)
        reg |= on << 8;
    BK4819_WriteRegister(BK4819_REG_47, reg);
}

static const BK4819_REGISTER_t registers_to_save[] = {
    BK4819_REG_30,
    BK4819_REG_37,
    BK4819_REG_3D,
    BK4819_REG_43,
    BK4819_REG_47,
    BK4819_REG_48,
    BK4819_REG_7E,
};

static uint16_t registers_stack[ARRAY_SIZE(registers_to_save)];

static void BackupRegisters()
{
    for (uint32_t i = 0; i < ARRAY_SIZE(registers_to_save); i++)
    {
        registers_stack[i] = BK4819_ReadRegister(registers_to_save[i]);
    }
}

static void RestoreRegisters()
{

    for (uint32_t i = 0; i < ARRAY_SIZE(registers_to_save); i++)
    {
        BK4819_WriteRegister(registers_to_save[i], registers_stack[i]);
    }

#ifdef ENABLE_FEAT_F4HWN
    gVfoConfigureMode = VFO_CONFIGURE;
#endif
}

static void ToggleAFDAC(bool on)
{
    uint32_t Reg = BK4819_ReadRegister(BK4819_REG_30);
    Reg &= ~(1 << 9);
    if (on)
        Reg |= (1 << 9);
    BK4819_WriteRegister(BK4819_REG_30, Reg);
}

static void SetF(uint32_t f)
{
    fMeasure = f;

    BK4819_SetFrequency(fMeasure);
    BK4819_PickRXFilterPathBasedOnFrequency(fMeasure);
    uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
    BK4819_WriteRegister(BK4819_REG_30, 0);
    BK4819_WriteRegister(BK4819_REG_30, reg);
}

// Lightweight frequency-set used during scanning.
// Skips the band-select GPIO writes (band does not change within a sweep)
// and uses a cached REG_30 value (read once in InitScan) instead of reading
// it on every step.  Reduces per-step SPI transactions from ~7 to 4,
// cutting the SPI bus activity that causes SPI-induced audio interference.
static void SetFScan(uint32_t f)
{
    // Refresh RF path only when crossing the VHF/UHF boundary (280 MHz)
    if ((f < 28000000) != (fMeasure < 28000000))
        BK4819_PickRXFilterPathBasedOnFrequency(f);
    fMeasure = f;
    BK4819_SetFrequency(f);
    BK4819_WriteRegister(BK4819_REG_30, 0);
    BK4819_WriteRegister(BK4819_REG_30, scanReg30);
}

// Spectrum related

static bool IsPeakOverOpenLevel()
{
    uint16_t openLevel = settings.rssiTriggerLevel;
    if (openLevel <= (uint16_t)(RSSI_MAX_VALUE - LISTEN_OPEN_HYST_RSSI))
        openLevel += LISTEN_OPEN_HYST_RSSI;
    else
        openLevel = RSSI_MAX_VALUE;

    return peak.rssi >= openLevel;
}

static bool IsListeningSignalPresent(uint16_t rssi)
{
    uint16_t closeLevel = (settings.rssiTriggerLevel > LISTEN_CLOSE_HYST_RSSI)
                              ? (uint16_t)(settings.rssiTriggerLevel - LISTEN_CLOSE_HYST_RSSI)
                              : 0;
    return rssi >= closeLevel;
}

static void ResetPeak()
{
    peak.t = 0;
    peak.rssi = 0;
    peak.f = 0;
    peak.i = 0;
}

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
    static void setTailFoundInterrupt()
    {
        BK4819_WriteRegister(BK4819_REG_3F, BK4819_REG_3F_CxCSS_TAIL);
    }

    static bool checkIfTailFound()
    {
      uint16_t interrupt_status_bits;
      // if interrupt waiting to be handled
      if(BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
        // reset the interrupt
        BK4819_WriteRegister(BK4819_REG_02, 0);
        // fetch the interrupt status bits
        interrupt_status_bits = BK4819_ReadRegister(BK4819_REG_02);
        // End listen on CSS tail.
        if (interrupt_status_bits & BK4819_REG_02_CxCSS_TAIL)
        {
            listenT = 0;
            // disable interrupts
            BK4819_WriteRegister(BK4819_REG_3F, 0);
            // reset the interrupt
            BK4819_WriteRegister(BK4819_REG_02, 0);
            return true;
        }
      }
      return false;
    }
#endif

bool IsCenterMode() { return settings.scanStepIndex < S_STEP_2_5kHz; }
// scan step in 0.01khz
uint16_t GetScanStep() { return scanStepValues[settings.scanStepIndex]; }

uint16_t GetStepsCount()
{
#ifdef ENABLE_SCAN_RANGES
    if (gScanRangeStart)
    {
        uint32_t range = gScanRangeStop - gScanRangeStart;
        uint16_t step = GetScanStep();
        return (range / step) + 1;  // +1 to include up limit
    }
#endif
    return 128 >> settings.stepsCount;
}

#ifdef ENABLE_SCAN_RANGES
static uint16_t GetStepsCountDisplay()
{
    if (gScanRangeStart)
    {
        return (gScanRangeStop - gScanRangeStart) / GetScanStep();
    }
    return GetStepsCount();
}
#endif

uint32_t GetBW() { return GetStepsCount() * GetScanStep(); }
uint32_t GetFStart()
{
    return IsCenterMode() ? currentFreq - (GetBW() >> 1) : currentFreq;
}

uint32_t GetFEnd()
{
#ifdef ENABLE_SCAN_RANGES
    if (gScanRangeStart)
    {
        return gScanRangeStop;
    }
#endif
    return currentFreq + GetBW();
}

static void TuneToPeak()
{
    scanInfo.f = peak.f;
    scanInfo.rssi = peak.rssi;
    scanInfo.i = peak.i;
    SetF(scanInfo.f);
}

static void DeInitSpectrum()
{
    SetF(initialFreq);
    RestoreRegisters();
    isInitialized = false;
}

uint8_t GetBWRegValueForScan()
{
    return scanStepBWRegValues[settings.scanStepIndex];
}

uint16_t GetRssi()
{
    // Wait for glitch to settle below threshold (not just < 255)
    uint8_t guard = 50;
    while (guard-- && (BK4819_ReadRegister(0x63) & 0xFF) >= 200)
    {
        SYSTICK_DelayUs(1);
    }
    // Discard first read (AGC may still be transitioning), keep second
    BK4819_GetRSSI();
    uint16_t rssi = BK4819_GetRSSI();
#ifdef ENABLE_AM_FIX
    if (settings.modulationType == MODULATION_AM && gSetting_AM_fix)
        rssi += AM_fix_get_gain_diff() * 2;
#endif
    return rssi;
}

static void ToggleAudio(bool on)
{
    if (on == audioState)
    {
        return;
    }
    audioState = on;
    if (on)
    {
        AUDIO_AudioPathOn();
    }
    else
    {
        AUDIO_AudioPathOff();
    }
}

static void ToggleRX(bool on)
{
    #ifdef ENABLE_FEAT_F4HWN_SPECTRUM
    if (isListening == on) {
        return;
    }
    #endif
    isListening = on;

    //RADIO_SetupAGC(settings.modulationType == MODULATION_AM, lockAGC);
    RADIO_SetupAGC(false, lockAGC);

    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on);

    ToggleAudio(on);
    ToggleAFDAC(on);
    ToggleAFBit(on);

    if (on)
    {
        listenLowCount = 0;
        // Seed with the RSSI that opened the squelch so the very first measure
        // can already detect an abrupt drop (quick-PTT case where the operator
        // released before listen was actually engaged).
        // listenPrevRssi = RSSI_MAX_VALUE; // previous behavior
        listenPrevRssi = peak.rssi;
    #ifdef ENABLE_FEAT_F4HWN_SPECTRUM
        listenT = 25;
        BK4819_WriteRegister(0x43, listenBWRegValues[settings.listenBw]);
        setTailFoundInterrupt();
    #else
        listenT = 1000;
        BK4819_WriteRegister(0x43, listenBWRegValues[settings.listenBw]);
    #endif
    }
    else
    {
        listenLowCount = 0;
        listenPrevRssi = RSSI_MAX_VALUE;
        BK4819_WriteRegister(0x43, GetBWRegValueForScan());
    }
}

// Scan info

static void ResetScanStats()
{
    scanInfo.rssi = 0;
    scanInfo.rssiMax = 0;
    scanInfo.rssiMin = RSSI_MAX_VALUE;
    scanInfo.iPeak = 0;
    scanInfo.fPeak = 0;
}

// Resets scan position and stats without touching the radio — safe to call
// on every sweep restart because scanReg30 and the RF filter path remain
// valid as long as the scan range hasn't changed.
static void InitScanPosition()
{
    ResetScanStats();
    scanInfo.scanStep = GetScanStep();
    scanInfo.measurementsCount = GetStepsCount();
    bool startFromLeft = scanStartFromLeft;
#if SPECTRUM_INTERLACE_LARGE_SWEEPS
    interlacePhase = 0;
    interlaceStride = 1;
    if (scanInfo.measurementsCount > ARRAY_SIZE(rssiHistory))
    {
        // Keep interlaced scans deterministic from the left edge.
        startFromLeft = true;
        interlaceStride =
            (scanInfo.measurementsCount + ARRAY_SIZE(rssiHistory) - 1) /
            ARRAY_SIZE(rssiHistory);
    }
#endif
    if (!startFromLeft && scanInfo.measurementsCount > 1)
    {
        scanInfo.i = scanInfo.measurementsCount - 1;
        scanInfo.f = GetFEnd();
        scanForward = false;
    }
    else
    {
        scanInfo.i = 0;
        scanInfo.f = GetFStart();
        scanForward = true;
    }
    scanReturnPending = scanInfo.measurementsCount > 1;
}

static void InitScan()
{
    InitScanPosition();

    // Cache the band-select LNA and REG_30 for the upcoming sweep.
    // SetFScan() will use these cached values, saving 3 SPI ops per step.
    // Mask bit 9 (AF DAC enable) so the cached value is always correct for
    // scanning regardless of whether audio was on when InitScan() was called
    // (RelaunchScan calls InitScan before ToggleRX(false)).
    BK4819_PickRXFilterPathBasedOnFrequency(scanInfo.f);
    scanReg30 = BK4819_ReadRegister(BK4819_REG_30) & ~(1u << 9);
}

static void ResetBlacklist()
{
    for (int i = 0; i < 128; ++i)
    {
        if (rssiHistory[i] == RSSI_MAX_VALUE)
            rssiHistory[i] = 0;
    }
#ifdef ENABLE_SCAN_RANGES
    memset(blacklistFreqs, 0, sizeof(blacklistFreqs));
    blacklistFreqsIdx = 0;
#endif
}

static void RelaunchScan()
{
    InitScan();
    ResetPeak();
    ToggleRX(false);
#ifdef SPECTRUM_AUTOMATIC_SQUELCH
    if (!manualSetFlag)
        settings.rssiTriggerLevel = RSSI_MAX_VALUE;
#endif
    preventKeypress = true;
    scanInfo.rssiMin = RSSI_MAX_VALUE;
    memset(peakHoldY,   PEAK_HOLD_INIT, sizeof(peakHoldY));
    memset(peakHoldAge, 0,              sizeof(peakHoldAge));

}

static void UpdateScanInfo()
{
    if (scanInfo.rssi > scanInfo.rssiMax)
    {
        scanInfo.rssiMax = scanInfo.rssi;
        scanInfo.fPeak = scanInfo.f;
        scanInfo.iPeak = scanInfo.i;
    }

    if (scanInfo.rssi < scanInfo.rssiMin)
    {
        scanInfo.rssiMin = scanInfo.rssi;
        settings.dbMin = Rssi2DBm(scanInfo.rssiMin);
        int dbMax = settings.dbMax - 10;
        if (settings.dbMin > dbMax)
            settings.dbMin = dbMax;
        redrawStatus = true;
    }
}

static void AutoTriggerLevel()
{
    if (manualSetFlag)
        return;

    if (scanInfo.rssiMin == RSSI_MAX_VALUE)
        return; // no valid measurement yet

    // Lightweight floor tracking with tiny memory/code footprint.
    // Uses current sweep min, smoothed across sweeps.
    if (autoNoiseFloor == RSSI_MAX_VALUE || settings.rssiTriggerLevel == RSSI_MAX_VALUE)
    {
        autoNoiseFloor = scanInfo.rssiMin;
    }
    else
    {
        autoNoiseFloor = (uint16_t)((3u * autoNoiseFloor + scanInfo.rssiMin + 2u) >> 2);
    }

    uint16_t target = autoNoiseFloor + autoTriggerMarginRssi[autoSensitivity];

    uint16_t oldTrigger = settings.rssiTriggerLevel;

    if (settings.rssiTriggerLevel == RSSI_MAX_VALUE)
    {
        // Fresh calibration (first sweep, or after step change): jump directly.
        settings.rssiTriggerLevel = target;
        redrawStatus = true;
        return;
    }

    // Adaptive slew: follow noise floor changes with rate limiting.
    // Faster convergence when the gap is large (e.g. after filter BW change).
    int16_t diff  = (int16_t)target - (int16_t)settings.rssiTriggerLevel;
    bool diffSign = diff < 0;
    uint16_t absDiff = my_abs(diff);

    if (absDiff > 4)
    {
        int16_t step = (absDiff > 12) ? 4 : ((absDiff > 6) ? 2 : 1);
        settings.rssiTriggerLevel += diffSign ? -step : step;
    }
    // Dead zone ±4: hold steady to avoid jitter near target

    if (settings.rssiTriggerLevel != oldTrigger)
        redrawStatus = true;
}

static void UpdatePeakInfoForce()
{
    peak.t = 0;
    peak.rssi = scanInfo.rssiMax;
    peak.f = scanInfo.fPeak;
    peak.i = scanInfo.iPeak;
    AutoTriggerLevel();
}

static void UpdatePeakInfo()
{
    if (peak.f == 0 || peak.t >= 1024 || peak.rssi < scanInfo.rssiMax)
        UpdatePeakInfoForce();
}

static uint8_t GetHistorySlot(uint16_t idx)
{
#ifdef ENABLE_SCAN_RANGES
    if (scanInfo.measurementsCount > ARRAY_SIZE(rssiHistory))
    {
        uint32_t slot = (uint32_t)idx * ARRAY_SIZE(rssiHistory) / scanInfo.measurementsCount;
        if (slot >= ARRAY_SIZE(rssiHistory))
            slot = ARRAY_SIZE(rssiHistory) - 1;
        return (uint8_t)slot;
    }
#endif
    return (uint8_t)idx;
}

static void SetRssiHistory(uint16_t idx, uint16_t rssi)
{
    uint8_t slot = GetHistorySlot(idx);

    if (rssi == RSSI_MAX_VALUE)
    {
        rssiHistory[slot] = RSSI_MAX_VALUE;
        return;
    }

    uint16_t prev = rssiHistory[slot];

#ifdef ENABLE_SCAN_RANGES
    if (scanInfo.measurementsCount > ARRAY_SIZE(rssiHistory))
    {
        if (prev == RSSI_MAX_VALUE)
            return;
        // For large ranges: keep fast attack, soften decay to reduce flicker.
        if (rssi >= prev)
            rssiHistory[slot] = rssi;
        else
            rssiHistory[slot] = (uint16_t)((3u * prev + rssi) >> 2);
        return;
    }
#endif
    // Attack/decay: instant rise, fast fall for stable display
    if (rssi >= prev) {
        rssiHistory[slot] = rssi;              // Attack: instant
    } else {
        rssiHistory[slot] = (prev + rssi) >> 1; // Decay: halve the gap each sweep
    }
}

static void Measure()
{
    uint16_t rssi = scanInfo.rssi = GetRssi();
    SetRssiHistory(scanInfo.i, rssi);
}

static void RequestAutoTriggerRecalibration()
{
    if (!manualSetFlag)
    {
        settings.rssiTriggerLevel = RSSI_MAX_VALUE;
        autoNoiseFloor = RSSI_MAX_VALUE;
    }
}

static void RearmRuntimeState()
{
    settings.dbMin = -128;
    settings.dbMax = -97;
    memset(rssiHistory, 0, sizeof(rssiHistory));
    memset(peakHoldY,   PEAK_HOLD_INIT, sizeof(peakHoldY));
    memset(peakHoldAge, 0,              sizeof(peakHoldAge));
    rssiSmoothed = 0;
    manualDbMaxTimer = 0;
    
    RelaunchScan();

    redrawScreen = true;
    redrawStatus = true;
}

// Reset spectrum runtime/config to defaults while keeping current frequency
// context (center/range). Persist only fields that are normally saved.
static void ResetSpectrumToDefaults()
{
    manualSetFlag = false;
    autoSensitivity = AUTO_SENS_NORMAL;
    monitorMode = false;
    menuState = 0;
    lockAGC = false;

    settings.scanStepIndex = S_STEP_25_0kHz;
    settings.stepsCount = STEPS_64;
    settings.listenBw = BK4819_FILTER_BW_WIDE;
    settings.modulationType = gTxVfo->Modulation;
    settings.rssiTriggerLevel = RSSI_MAX_VALUE;
    autoNoiseFloor = RSSI_MAX_VALUE;

    // Keep frequency/range unchanged; recompute move step from fresh scan params.
    settings.frequencyChangeStep = GetBW() >> 1;

    RADIO_SetModulation(settings.modulationType);
    BK4819_SetFilterBandwidth(settings.listenBw, false);

    listenLowCount = 0;
    listenPrevRssi = RSSI_MAX_VALUE;
    scanStartFromLeft = true;

    RearmRuntimeState();
    ResetBlacklist();

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
    SaveSettings();
#endif
}

// Update things by keypress

static uint16_t dbm2rssi(int dBm)
{
    return (dBm + 160 - dBmCorrTable[gRxVfo->Band]) * 2;
}

static void ClampRssiTriggerLevel()
{
    settings.rssiTriggerLevel =
        clamp(settings.rssiTriggerLevel, dbm2rssi(settings.dbMin),
              dbm2rssi(settings.dbMax));
}

static void UpdateDbMax(bool inc)
{
    settings.dbMax = clamp(settings.dbMax + (inc ? 5 : -5),
                           settings.dbMin + 10, 10);
    ClampRssiTriggerLevel();
    manualDbMaxTimer = MANUAL_DBMAX_SWEEPS;
    redrawScreen = true;
    redrawStatus = true;
}

static void UpdateAutoSensitivity(bool inc)
{
    if (inc)
    {
        if (autoSensitivity < AUTO_SENS_STRONG)
            autoSensitivity++;
    }
    else
    {
        if (autoSensitivity > AUTO_SENS_WEAK)
            autoSensitivity--;
    }

    // Force immediate re-calibration to the new profile margin.
    settings.rssiTriggerLevel = RSSI_MAX_VALUE;
    redrawScreen = true;
    redrawStatus = true;
}

static void UpdateRssiTriggerLevel(bool inc)
{
    if (inc)
        settings.rssiTriggerLevel += 2;
    else
        settings.rssiTriggerLevel -= 2;

    if (settings.rssiTriggerLevel > dbm2rssi(settings.dbMax))
        UpdateDbMax(true);
    else
        ClampRssiTriggerLevel();

    redrawScreen = true;
    redrawStatus = true;
}


static void UpdateScanStep(bool inc)
{
    if (inc)
    {
        settings.scanStepIndex = settings.scanStepIndex != S_STEP_100_0kHz ? settings.scanStepIndex + 1 : 0;
    }
    else
    {
        settings.scanStepIndex = settings.scanStepIndex != 0 ? settings.scanStepIndex - 1 : S_STEP_100_0kHz;
    }

    settings.frequencyChangeStep = GetBW() >> 1;
    RelaunchScan();
    ResetBlacklist();
    redrawScreen = true;
}

static void UpdateCurrentFreq(bool inc)
{
    if (inc && currentFreq < F_MAX)
    {
        currentFreq += settings.frequencyChangeStep;
    }
    else if (!inc && currentFreq > F_MIN)
    {
        currentFreq -= settings.frequencyChangeStep;
    }
    else
    {
        return;
    }
    RelaunchScan();
    ResetBlacklist();
    redrawScreen = true;
}

static void UpdateCurrentFreqStill(bool inc)
{
    uint8_t offset = modulationTypeTuneSteps[settings.modulationType];
    uint32_t f = fMeasure;
    if (inc && f < F_MAX)
    {
        f += offset;
    }
    else if (!inc && f > F_MIN)
    {
        f -= offset;
    }
    SetF(f);
    redrawScreen = true;
}

static void ResumeSweepInDirection(bool forward)
{
    ToggleRX(false);
    ResetScanStats();
    ResetPeak();
    InitScanPosition();

    if (forward || scanInfo.measurementsCount <= 1)
    {
        scanForward = true;
        scanInfo.i = 0;
        scanInfo.f = GetFStart();
    }
    else
    {
        scanForward = false;
        scanInfo.i = scanInfo.measurementsCount - 1;
        scanInfo.f = GetFEnd();
    }

    newScanStart = false;
    preventKeypress = false;
    redrawScreen = true;
    redrawStatus = true;
}

static void UpdateFreqChangeStep(bool inc)
{
    uint16_t diff = GetScanStep() * 4;
    if (inc && settings.frequencyChangeStep < 200000)
    {
        settings.frequencyChangeStep += diff;
    }
    else if (!inc && settings.frequencyChangeStep > 10000)
    {
        settings.frequencyChangeStep -= diff;
    }
    SYSTEM_DelayMs(100);
    redrawScreen = true;
}

static void ToggleModulation()
{
    // Always leave listen mode before changing demod path.
    // This prevents carrying a stale "locked RX" state across modulation
    // changes (notably USB -> FM).
    ToggleRX(false);

    if (settings.modulationType < MODULATION_UKNOWN - 1)
    {
        settings.modulationType++;
    }
    else
    {
        settings.modulationType = MODULATION_FM;
    }
    RADIO_SetModulation(settings.modulationType);

    // Re-arm runtime spectrum state for the new demodulation profile.
    // USB and FM can have very different RSSI/noise floors, so keeping the
    // previous history/levels can draw a persistent horizontal wall.
    if (!manualSetFlag)
        settings.rssiTriggerLevel = RSSI_MAX_VALUE;

    RearmRuntimeState();
    ResetBlacklist();
}

static void ToggleListeningBW()
{
    if (settings.listenBw == BK4819_FILTER_BW_NARROWER)
    {
        settings.listenBw = BK4819_FILTER_BW_WIDE;
    }
    else
    {
        settings.listenBw++;
    }
    redrawScreen = true;
}

static void ToggleBacklight()
{
    settings.backlightState = !settings.backlightState;
    if (settings.backlightState)
    {
        // BACKLIGHT_TurnOn();
        BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MAX);
    }
    else
    {
        // BACKLIGHT_TurnOff();
        BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MIN);
    }
}

static void ToggleStepsCount()
{
    if (settings.stepsCount == STEPS_128)
    {
        settings.stepsCount = STEPS_16;
    }
    else
    {
        settings.stepsCount--;
    }
    settings.frequencyChangeStep = GetBW() >> 1;
    RelaunchScan();
    ResetBlacklist();
    redrawScreen = true;
}

static void ResetFreqInput()
{
    tempFreq = 0;
    for (int i = 0; i < 10; ++i)
    {
        freqInputString[i] = '-';
    }
}

static void FreqInput()
{
    freqInputIndex = 0;
    freqInputDotIndex = 0;
    ResetFreqInput();
    SetState(FREQ_INPUT);
}

static void UpdateFreqInput(KEY_Code_t key)
{
    if (key != KEY_EXIT && freqInputIndex >= 10)
    {
        return;
    }
    if (key == KEY_STAR)
    {
        if (freqInputIndex == 0 || freqInputDotIndex)
        {
            return;
        }
        freqInputDotIndex = freqInputIndex;
    }
    if (key == KEY_EXIT)
    {
        freqInputIndex--;
        if (freqInputDotIndex == freqInputIndex)
            freqInputDotIndex = 0;
    }
    else
    {
        freqInputArr[freqInputIndex++] = key;
    }

    ResetFreqInput();

    uint8_t dotIndex =
        freqInputDotIndex == 0 ? freqInputIndex : freqInputDotIndex;

    KEY_Code_t digitKey;
    for (int i = 0; i < 10; ++i)
    {
        if (i < freqInputIndex)
        {
            digitKey = freqInputArr[i];
            freqInputString[i] = digitKey <= KEY_9 ? '0' + digitKey - KEY_0 : '.';
        }
        else
        {
            freqInputString[i] = '-';
        }
    }

    uint32_t base = 100000; // 1MHz in BK units
    for (int i = dotIndex - 1; i >= 0; --i)
    {
        tempFreq += (freqInputArr[i] - KEY_0) * base;
        base *= 10;
    }

    base = 10000; // 0.1MHz in BK units
    if (dotIndex < freqInputIndex)
    {
        for (int i = dotIndex + 1; i < freqInputIndex; ++i)
        {
            tempFreq += (freqInputArr[i] - KEY_0) * base;
            base /= 10;
        }
    }
    redrawScreen = true;
}

static void Blacklist()
{
#ifdef ENABLE_SCAN_RANGES
    blacklistFreqs[blacklistFreqsIdx++ % ARRAY_SIZE(blacklistFreqs)] = peak.i;
#endif

    SetRssiHistory(peak.i, RSSI_MAX_VALUE);
    ResetPeak();
    ToggleRX(false);
    ResetScanStats();
}

#ifdef ENABLE_SCAN_RANGES
static bool IsBlacklisted(uint16_t idx)
{
    if (blacklistFreqsIdx)
        for (uint8_t i = 0; i < ARRAY_SIZE(blacklistFreqs); i++)
            if (blacklistFreqs[i] == idx)
                return true;
    return false;
}
#endif

// Draw things

// Integer square root (for sugar map non-linear compression)
static uint8_t iSqrt(uint16_t n)
{
    if (n == 0) return 0;
    uint16_t x = n;
    uint16_t y = (x + 1) >> 1;
    while (y < x) { x = y; y = (x + n / x) >> 1; }
    return (uint8_t)x;
}

static bool IsRssiHistoryInvalid(uint16_t rssi)
{
    // rssiHistory is cleared to 0 on (re)entry; treat it as "not measured yet"
    // so the renderer does not draw an artificial horizontal baseline.
    return rssi == 0 || rssi == RSSI_MAX_VALUE;
}

// applied x2 to prevent initial rounding.
// A mild square-root compression (sugar map) is applied so that weak signals
// occupy more of the display height while strong peaks are not clipped.
uint8_t Rssi2PX(uint16_t rssi, uint8_t pxMin, uint8_t pxMax)
{
    const int DB_MIN = settings.dbMin << 1;
    const int DB_MAX = settings.dbMax << 1;
    const int DB_RANGE = DB_MAX - DB_MIN;

    const uint8_t PX_RANGE = pxMax - pxMin;

    int dbm = clamp(Rssi2DBm(rssi) << 1, DB_MIN, DB_MAX);

    // Linear 0..PX_RANGE position
    uint8_t linear = (uint8_t)(((dbm - DB_MIN) * PX_RANGE + DB_RANGE / 2) / DB_RANGE);

    // Square-root compression: sqrt(linear * PX_RANGE) rescaled to PX_RANGE
    uint8_t compressed = iSqrt((uint16_t)linear * PX_RANGE);

    // Blend 50/50 between linear and compressed for a subtle effect
    return ((uint16_t)linear + compressed) / 2 + pxMin;
}

uint8_t Rssi2Y(uint16_t rssi)
{
    // Map into [DrawingTopY, DrawingEndY] so peaks never overdraw the
    // frequency display rendered in gFrameBuffer[0] (pixels 0-7).
    return DrawingEndY - Rssi2PX(rssi, 0, DrawingEndY - DrawingTopY);
}

// Resolve the RSSI value at fractional sample index (Q8 fixed-point) using
// linear interpolation. Blacklisted samples (RSSI_MAX_VALUE) are skipped by
// falling back to the other neighbour; if both are blacklisted, returns
// RSSI_MAX_VALUE so the caller can skip the column.
static uint16_t InterpolateRssi(uint8_t bars, uint16_t pos256)
{
    uint8_t i = pos256 >> 8;
    uint8_t frac = pos256 & 0xFF;

    if (i >= bars - 1)
    {
        i = bars - 1;
        frac = 0;
    }

    uint16_t rssiA = rssiHistory[i];
    uint16_t rssiB = rssiHistory[(i + 1 < bars) ? (i + 1) : i];

    if (IsRssiHistoryInvalid(rssiA) && IsRssiHistoryInvalid(rssiB))
        return RSSI_MAX_VALUE;
    if (IsRssiHistoryInvalid(rssiA))
        return rssiB;
    if (IsRssiHistoryInvalid(rssiB))
        return rssiA;

    return ((uint32_t)rssiA * (256 - frac) + (uint32_t)rssiB * frac) >> 8;
}

// Sentinel value in topY[] to mark a column that should not be drawn
// (blacklisted RSSI sample on both neighbours).
#define SPECTRUM_TOPY_SKIP 0xFF

// Half-step bridging helper: compute crestTop/crestBot for column x
// from a topY-like array.
static void CalcCrest(const uint8_t *yArr, uint8_t x,
                      uint8_t *crestTop, uint8_t *crestBot)
{
    uint8_t y0 = yArr[x];
    *crestTop = y0;
    *crestBot = y0;

    bool goBack = true;
    uint8_t n = 0;

    if (x > 0) {
        n = yArr[x - 1];
        goto Start;
    }

Back:
    goBack = false;

    if (x + 1 < 128) {
        n = yArr[x + 1];
        goto Start;
    }

    return;

Start:
    if (n != SPECTRUM_TOPY_SKIP && n <= DrawingEndY) {
        uint8_t mid = (y0 + n + 1) >> 1;
        if (mid < *crestTop) *crestTop = mid;
        if (mid > *crestBot) *crestBot = mid;
    }

    if (goBack)
        goto Back;
}

// Draw the spectrum curve (solid crest + checkerboard body) and the peak hold
// dotted trace.  Both use the same half-step bridging so the peak hold crest
// shape mirrors the live crest exactly, just rendered with a dotted pattern.
static void DrawSpectrumCurve(const uint8_t *topY)
{
    // Pass 1: update peakHoldY[] from topY[] before rendering so that the
    // bridging in Pass 2 already sees fully-updated neighbour values.
    for (uint8_t x = 0; x < 128; x++)
    {
        uint8_t y0 = topY[x];
        if (y0 == SPECTRUM_TOPY_SKIP || y0 > DrawingEndY) {
            peakHoldY[x] = PEAK_HOLD_INIT;
            continue;
        }

        uint8_t ph = peakHoldY[x];
        if (ph == PEAK_HOLD_INIT || y0 <= ph)
        {
            peakHoldY[x]        = y0;
            peakHoldAge[x >> 1] = 0;
        }
        else
        {
            if (peakHoldAge[x >> 1] < PEAK_HOLD_DELAY) {
                if (!(x & 1)) peakHoldAge[x >> 1]++;
            } else {
                ph += 2;
                peakHoldY[x] = (ph <= DrawingEndY) ? ph : PEAK_HOLD_INIT;
            }
        }
    }

    // Pass 2: draw live curve (solid) then peak hold (dotted).
    for (uint8_t x = 0; x < 128; x++)
    {
        // --- Live spectrum crest + body ---
        uint8_t y0 = topY[x];
        if (y0 != SPECTRUM_TOPY_SKIP && y0 <= DrawingEndY)
        {
            uint8_t crestTop, crestBot;
            CalcCrest(topY, x, &crestTop, &crestBot);

            // Solid crest contour.
            for (uint8_t y = crestTop; y <= crestBot; y++)
                PutPixel(x, y, true);

            // Checkerboard body below the crest.
            for (uint8_t y = crestBot + 1; y <= DrawingEndY; y++)
                if (((x + y) & 1) == 0)
                    PutPixel(x, y, true);
        }

        // --- Peak hold dotted crest ---
        uint8_t ph = peakHoldY[x];
        if (ph != PEAK_HOLD_INIT && ph <= DrawingEndY)
        {
            uint8_t phTop, phBot;
            CalcCrest(peakHoldY, x, &phTop, &phBot);

            // Dotted crest: checkerboard pattern over the full crest range.
            for (uint8_t y = phTop; y <= phBot; y++)
                if (((x + y) & 1) == 0)
                    PutPixel(x, y, true);
        }
    }
}

// Spatial smoothing: 3-bin moving average on topY for a cleaner curve.
// Only averages valid (non-SKIP) neighbours.
static void SmoothTopY(uint8_t *topY)
{
    uint8_t prev = topY[0];
    for (uint8_t x = 1; x < 127; x++)
    {
        uint8_t cur = topY[x];
        uint8_t next = topY[x + 1];
        if (cur == SPECTRUM_TOPY_SKIP) {
            prev = cur;
            continue;
        }
        uint16_t sum = cur;
        uint8_t n = 1;
        if (prev != SPECTRUM_TOPY_SKIP) { sum += prev; n++; }
        if (next != SPECTRUM_TOPY_SKIP) { sum += next; n++; }
        prev = cur;                       // save unsmoothed value for next iteration
        topY[x] = (sum + n / 2) / n;     // rounded average
    }
}

// Fill topY[0..127] by linear interpolation of `bars` RSSI samples across the
// 128 display columns. Invalid (blacklisted) samples become SPECTRUM_TOPY_SKIP.
static void BuildSpectrumTopY(uint8_t *topY, uint8_t bars)
{
    if (bars == 0)
    {
        for (uint8_t x = 0; x < 128; x++)
            topY[x] = SPECTRUM_TOPY_SKIP;
        return;
    }

    if (bars == 1)
    {
        uint16_t rssi = rssiHistory[0];
        uint8_t y = IsRssiHistoryInvalid(rssi) ? SPECTRUM_TOPY_SKIP : Rssi2Y(rssi);
        for (uint8_t x = 0; x < 128; x++)
            topY[x] = y;
        return;
    }

    // Q8 fixed-point: step256 / 256 advances one sample, multiplied by x.
    uint16_t step256 = ((uint16_t)(bars - 1) << 8) / 127;

    for (uint8_t x = 0; x < 128; x++)
    {
        uint16_t rssi = InterpolateRssi(bars, (uint16_t)x * step256);
        topY[x] = (rssi == RSSI_MAX_VALUE) ? SPECTRUM_TOPY_SKIP : Rssi2Y(rssi);
    }
}

static void BuildCurrentSpectrumTopY(uint8_t *topY)
{
#ifdef ENABLE_FEAT_F4HWN
    uint16_t steps = GetStepsCount();
    // max bars at 128 to correctly draw larger numbers of samples
    uint8_t bars = (steps > 128) ? 128 : steps;
#else
    uint8_t bars = 128 >> settings.stepsCount;
    if (bars == 0)
        bars = 1;
#endif

    BuildSpectrumTopY(topY, bars);
    SmoothTopY(topY);
}

static void DrawStatus()
{
    if (manualSetFlag)
    {
        char curStr[6];
        char trigStr[6];

        if (IsRssiHistoryInvalid(scanInfo.rssi))
            sprintf(curStr, "--");
        else
            sprintf(curStr, "%d", Rssi2DBm(scanInfo.rssi));

        if (monitorMode || settings.rssiTriggerLevel == RSSI_MAX_VALUE)
            sprintf(trigStr, "--");
        else
            sprintf(trigStr, "%d", Rssi2DBm(settings.rssiTriggerLevel));

        sprintf(String, "M %s/%s", curStr, trigStr);
    }
    else
    {
        // In AUTO, keep mode/profile display only (no current/trigger pair).
        sprintf(String, "A:%s %c", autoSensitivityLabel[autoSensitivity],
                scanForward ? '>' : '<');
    }
    
    GUI_DisplaySmallest(String, 0, 1, true, true);

    BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryCheckCounter++ % 4],
                             &gBatteryCurrent);

    uint16_t voltage = (gBatteryVoltages[0] + gBatteryVoltages[1] +
                        gBatteryVoltages[2] + gBatteryVoltages[3]) /
                       4 * 760 / gBatteryCalibration[3];

    unsigned perc = BATTERY_VoltsToPercent(voltage);

    // sprintf(String, "%d %d", voltage, perc);
    // GUI_DisplaySmallest(String, 48, 1, true, true);

    gStatusLine[116] = 0b00011100;
    gStatusLine[117] = 0b00111110;
    for (int i = 118; i <= 126; i++)
    {
        gStatusLine[i] = 0b00100010;
    }

    for (unsigned i = 127; i >= 118; i--)
    {
        if (127 - i <= (perc + 5) * 9 / 100)
        {
            gStatusLine[i] = 0b00111110;
        }
    }
}

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
static void ShowChannelName(uint32_t f)
{
    static uint32_t channelF = 0;
    static char channelName[12]; 

    // Channel name starts at x=43 (fixed), leaving room for the dBm
    // string on the left (max ~40 px) and battery indicator at x=116.
    // Clear first so a shorter name doesn't leave stale pixels.
    memset(&gStatusLine[43], 0, 116 - 43);

    if (isListening)
    {
        if (f != channelF) {
            channelF = f;
            unsigned int i;
            memset(channelName, 0, sizeof(channelName));
            for (i = 0; IS_MR_CHANNEL(i); i++)
            {
                if (RADIO_CheckValidChannel(i, false, 0))
                {
                    if (SETTINGS_FetchChannelFrequency(i) == channelF)
                    {
                        SETTINGS_FetchChannelName(channelName, i);
                        break;
                    }
                }
            }
        }
        if (channelName[0] != 0) {
            UI_PrintStringSmallBufferNormal(channelName, gStatusLine + 43);
        }
    }

    ST7565_BlitStatusLine();
}
#endif

static void FormatFrequency(uint32_t freq, char *buffer) {
    sprintf(buffer, "%u.%05u", freq / 100000, freq % 100000);
}

static void DrawF(uint32_t f)
{
    FormatFrequency(f, String);
    // Align frequency with channel name in status bar (both at x=43).
    // Left-aligned (End == Start = 43) so it does not collide with BW at x=108.
    UI_PrintStringSmallNormal(String, 43, 43, 0);

    sprintf(String, "%3s", gModulationStr[settings.modulationType]);
    GUI_DisplaySmallest(String, 116, 1, false, true);
    sprintf(String, "%4sk", bwOptions[settings.listenBw]);
    GUI_DisplaySmallest(String, 108, 7, false, true);

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
    ShowChannelName(f);
#endif
}

static void DrawNums()
{

    if (currentState == SPECTRUM)
    {
#ifdef ENABLE_SCAN_RANGES
        sprintf(String, "%ux", gScanRangeStart ? GetStepsCountDisplay() : GetStepsCount());
#else
        sprintf(String, "%ux", GetStepsCount());
#endif
        GUI_DisplaySmallest(String, 0, 1, false, true);
        sprintf(String, "%u.%02uk", GetScanStep() / 100, GetScanStep() % 100);
        GUI_DisplaySmallest(String, 0, 7, false, true);

    }

    if (IsCenterMode())
    {
        sprintf(String, "%u.%05u \x7F%u.%02uk", currentFreq / 100000,
                currentFreq % 100000, settings.frequencyChangeStep / 100,
                settings.frequencyChangeStep % 100);
        GUI_DisplaySmallest(String, 36, 49, false, true);
    }
    else
    {
        FormatFrequency(GetFStart(), String);
        GUI_DisplaySmallest(String, 0, 49, false, true);

#ifdef ENABLE_SCAN_RANGES
        if (gScanRangeStart)
        {
            // Scan-range mode: UP/DOWN are blocked, frequencyChangeStep is unused.
            // Show the visible bandwidth instead, which is meaningful here.
            uint32_t bw = gScanRangeStop - gScanRangeStart;
            sprintf(String, "%u.%02uk", bw / 100, bw % 100);
        }
        else
#endif
        {
            sprintf(String, "\x7F%u.%02uk", settings.frequencyChangeStep / 100,
                    settings.frequencyChangeStep % 100);
        }
        GUI_DisplaySmallest(String, 48, 49, false, true);

        FormatFrequency(GetFEnd(), String);
        GUI_DisplaySmallest(String, 93, 49, false, true);
    }
}

static bool SpectrumColumnAtOrAboveY(const uint8_t *topY, uint8_t x, uint8_t y)
{
    int8_t start = (x > 0) ? -1 : 0;
    int8_t end = (x < 127) ? 1 : 0;

    for (int8_t dx = start; dx <= end; dx++)
    {
        uint8_t n = x + dx;
        if (topY[n] != SPECTRUM_TOPY_SKIP && topY[n] <= y + 1)
            return true;
        if (peakHoldY[n] != PEAK_HOLD_INIT && peakHoldY[n] <= y + 1)
            return true;
    }

    return false;
}

static uint8_t GetScanStepTextWidth()
{
    uint16_t whole = GetScanStep() / 100;
    uint8_t digits = 1;

    while (whole >= 10)
    {
        whole /= 10;
        digits++;
    }

    return (digits + 4) * 4; // "%u.%02uk", 4 px advance per char
}

static void DrawRssiTriggerLevel(const uint8_t *topY)
{
    if (settings.rssiTriggerLevel == RSSI_MAX_VALUE || monitorMode)
        return;
    uint8_t scanStepTextWidth = GetScanStepTextWidth();
    uint8_t y = Rssi2Y(settings.rssiTriggerLevel);
    for (uint8_t x = 0; x < 128; x += 2)
    {
        if (SpectrumColumnAtOrAboveY(topY, x, y))
            continue;
        if (y <= 12 && (x < scanStepTextWidth + 2 || x >= 114))
            continue;
        if (gFrameBuffer[y / 8][x] & (1 << (y % 8)))
            continue;
        PutPixel(x, y, true);
    }
}

static void DrawTicks()
{
    uint32_t f = GetFStart();
    uint32_t span = GetFEnd() - GetFStart();
    uint32_t step = span / 128;
    for (uint8_t i = 0; i < 128; i += (1 << settings.stepsCount))
    {
        f = GetFStart() + span * i / 128;
        uint8_t barValue = 0b00000001;
        (f % 10000) < step && (barValue |= 0b00000010);
        (f % 50000) < step && (barValue |= 0b00000100);
        (f % 100000) < step && (barValue |= 0b00011000);

        gFrameBuffer[5][i] |= barValue;
    }

    // center
    if (IsCenterMode())
    {
        memset(gFrameBuffer[5] + 62, 0x80, 5);
        gFrameBuffer[5][64] = 0xff;
    }
    else
    {
        memset(gFrameBuffer[5] + 1, 0x80, 3);
        memset(gFrameBuffer[5] + 124, 0x80, 3);

        gFrameBuffer[5][0] = 0xff;
        gFrameBuffer[5][127] = 0xff;
    }
}

static void DrawArrow(uint8_t x)
{
    for (signed i = -2; i <= 2; ++i)
    {
        signed v = x + i;
        if (!(v & 128))
        {
            gFrameBuffer[5][v] |= (0b01111000 << my_abs(i)) & 0b01111000;
        }
    }
}

static bool GetDirection(KEY_Code_t key) {
    return (key == KEY_UP) ? gEeprom.SET_NAV : !gEeprom.SET_NAV;
}

// Returns true if the key was handled (stop state-specific processing).
static bool OnKeyDownCommon(uint8_t key) {
    bool isTrue = (key == KEY_3 || key == KEY_STAR);

    switch (key)
    {
    case KEY_3:
    case KEY_9:
        if (manualSetFlag)
            UpdateDbMax(isTrue);
        else
            UpdateAutoSensitivity(isTrue);
        return true;
    case KEY_STAR:
    case KEY_F:
        UpdateRssiTriggerLevel(isTrue);
        return true;
    case KEY_0:
        ToggleModulation();
        return true;
    case KEY_6:
        ToggleListeningBW();
        return true;
    case KEY_SIDE2:
        ToggleBacklight();
        return true;
    }
    return false;
}

static void OnKeyDown(uint8_t key) {
    bool isTrue = (key == KEY_1 || key == KEY_2);

    switch (key)
    {
    case KEY_1:
    case KEY_7:
        UpdateScanStep(isTrue);
        break;
    case KEY_2:
    case KEY_8:
        UpdateFreqChangeStep(isTrue);
        break;
    case KEY_UP:
    case KEY_DOWN:
        // If the spectrum is currently receiving (green LED on),
        // force-stop RX and restart sweep in the requested direction.
        if (isListening) {
            ResumeSweepInDirection(GetDirection(key));
            break;
        }
#ifdef ENABLE_SCAN_RANGES
        if (!gScanRangeStart) {
#endif
        UpdateCurrentFreq(GetDirection(key));
#ifdef ENABLE_SCAN_RANGES
        }
#endif
        break;
    case KEY_SIDE1:
        Blacklist();
        break;
    case KEY_5:
#ifdef ENABLE_SCAN_RANGES
        if (!gScanRangeStart)
#endif
            FreqInput();
        break;
    case KEY_4:
#ifdef ENABLE_SCAN_RANGES
        if (!gScanRangeStart)
#endif
            ToggleStepsCount();
        break;
    case KEY_PTT:
        SetState(STILL);
        TuneToPeak();
        break;
    case KEY_MENU:
        // Short press toggles manual/auto.
        manualSetFlag = !manualSetFlag;
        if (!manualSetFlag)
            settings.rssiTriggerLevel = RSSI_MAX_VALUE;
        redrawStatus = true;
        break;
    case KEY_EXIT:
        if (menuState)
        {
            menuState = 0;
            break;
        }
#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
        SaveSettings();
#endif
#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.CURRENT_STATE = 0;
        SETTINGS_WriteCurrentState();
#endif
        DeInitSpectrum();
        break;
    default:
        break;
    }
}

static void OnKeyDownFreqInput(KEY_Code_t key)
{
    switch (key)
    {
    case KEY_0...KEY_9:
    case KEY_STAR:
    case KEY_EXIT:
        if (freqInputIndex == 0 && key == KEY_EXIT)
        {
            SetState(previousState);
            break;
        }
        UpdateFreqInput(key);
        break;
    case KEY_MENU:
        if (tempFreq < F_MIN || tempFreq > F_MAX)
        {
            break;
        }
        SetState(previousState);
        currentFreq = tempFreq;
        if (currentState == SPECTRUM)
        {
            ResetBlacklist();
            RelaunchScan();
        }
        else
        {
            SetF(currentFreq);
        }
        break;
    default:
        break;
    }
}

static void OnKeyDownStill(KEY_Code_t key) {
    switch (key)
    {
    case KEY_UP:
    case KEY_DOWN:
        if (menuState) {
            SetRegMenuValue(menuState, GetDirection(key));
            break;
        }
        UpdateCurrentFreqStill(GetDirection(key));
        break;
    case KEY_5:
        FreqInput();
        break;
    case KEY_SIDE1:
        monitorMode = !monitorMode;
        break;
    case KEY_MENU:
        menuState = (menuState == ARRAY_SIZE(registerSpecs) - 1) ? 1 : menuState + 1;
        redrawScreen = true;
        break;
    case KEY_EXIT:
        if (!menuState)
        {
            SetState(SPECTRUM);
            lockAGC = false;
            monitorMode = false;
            RelaunchScan();
            break;
        }
        menuState = 0;
        break;
    default:
        break;
    }
}

static void RenderFreqInput() { UI_PrintString(freqInputString, 2, 127, 0, 8); }

static void RenderStatus()
{
    UI_StatusClear();
    DrawStatus();
    ST7565_BlitStatusLine();
}

static void RenderSpectrum()
{
    uint16_t steps = GetStepsCount();
    uint8_t arrowX = (steps > 1) ? (uint8_t)(128u * peak.i / (steps - 1)) : 0;
    uint8_t topY[128];

    BuildCurrentSpectrumTopY(topY);
    DrawTicks();
    DrawArrow(arrowX);
    DrawSpectrumCurve(topY);
    DrawF(peak.f);
    DrawNums();
    DrawRssiTriggerLevel(topY);
}

static void RenderStill()
{
    DrawF(fMeasure);

    const uint8_t METER_PAD_LEFT = 3;

    memset(&gFrameBuffer[2][METER_PAD_LEFT], 0b00010000, 121);

    for (int i = 0; i < 121; i += 5)
    {
        gFrameBuffer[2][i + METER_PAD_LEFT] = 0b00110000;
    }

    for (int i = 0; i < 121; i += 10)
    {
        gFrameBuffer[2][i + METER_PAD_LEFT] = 0b01110000;
    }

    uint8_t x = Rssi2PX(rssiSmoothed, 0, 121);
    for (int i = 0; i < x; ++i)
    {
        if (i % 5)
        {
            gFrameBuffer[2][i + METER_PAD_LEFT] |= 0b00000111;
        }
    }

    int dbm = Rssi2DBm(rssiSmoothed);
    uint8_t s = DBm2S(dbm);
    sprintf(String, "S: %u", s);
    GUI_DisplaySmallest(String, 4, 25, false, true);
    sprintf(String, "%d dBm", dbm);
    GUI_DisplaySmallest(String, 28, 25, false, true);

    if (!monitorMode)
    {
        uint8_t x = Rssi2PX(settings.rssiTriggerLevel, 0, 121);
        gFrameBuffer[2][METER_PAD_LEFT + x] = 0b11111111;
    }

    const uint8_t PAD_LEFT = 4;
    const uint8_t CELL_WIDTH = 30;
    uint8_t offset = PAD_LEFT;
    uint8_t row = 4;

    for (int i = 0, idx = 1; idx <= 3; ++i, ++idx)
    {
        if (idx == 4)
        {
            row += 2;
            i = 0;
        }
        offset = PAD_LEFT + i * CELL_WIDTH;
        if (menuState == idx)
        {
            for (int j = 0; j < CELL_WIDTH; ++j)
            {
                gFrameBuffer[row][j + offset] = 0xFF;
                gFrameBuffer[row + 1][j + offset] = 0xFF;
            }
        }
        sprintf(String, "%s", registerSpecs[idx].name);
        GUI_DisplaySmallest(String, offset + 2, row * 8 + 2, false,
                            menuState != idx);

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
        sprintf(String, "%ddB", regOptions[idx].options[GetRegMenuValue(idx)]);

        /*
        if(idx == 1)
        {
            sprintf(String, "%ddB", LNAsOptions[GetRegMenuValue(idx)]);
        }
        else if(idx == 2)
        {
            sprintf(String, "%ddB", LNAOptions[GetRegMenuValue(idx)]);
        }
        else if(idx == 3)
        {
            sprintf(String, "%ddB", VGAOptions[GetRegMenuValue(idx)]);
        }
        else if(idx == 4)
        {
            sprintf(String, "%skHz", BPFOptions[(GetRegMenuValue(idx) / 0x2aaa)]);
        }
        */
#else
        sprintf(String, "%u", GetRegMenuValue(idx));
#endif
        GUI_DisplaySmallest(String, offset + 2, (row + 1) * 8 + 1, false,
                            menuState != idx);
    }
}

static void Render()
{
    UI_DisplayClear();

    switch (currentState)
    {
    case SPECTRUM:
        RenderSpectrum();
        break;
    case FREQ_INPUT:
        RenderFreqInput();
        break;
    case STILL:
        RenderStill();
        break;
    }

    // Display blit is done incrementally (one page per tick) — see Tick().
}

static bool HandleUserInput()
{
    kbd.prev = kbd.current;
    kbd.current = KEYBOARD_GetKey();

    if (kbd.current != KEY_INVALID && kbd.current == kbd.prev)
    {
        if (kbd.counter < 16)
            kbd.counter++;
        else
            kbd.counter -= 3;
        SYSTEM_DelayMs(20);
    }
    else
    {
        kbd.counter = 0;
    }

    // Spectrum MENU key handling:
    // - short press => action on release
    // - long press  => one-shot at counter==16
    if (currentState == SPECTRUM)
    {
        if (kbd.current == KEY_INVALID && kbd.prev == KEY_MENU)
        {
            if (menuKeyPendingShort && !menuKeyLongHandled)
                OnKeyDown(KEY_MENU);
            menuKeyPendingShort = false;
            menuKeyLongHandled = false;
        }
        else if (kbd.current != KEY_MENU && kbd.prev != KEY_MENU)
        {
            menuKeyPendingShort = false;
            menuKeyLongHandled = false;
        }
    }

    if (kbd.counter == 3 || kbd.counter == 16)
    {
        if (currentState == SPECTRUM && kbd.current == KEY_MENU)
        {
            if (kbd.counter == 3)
            {
                menuKeyPendingShort = true;
                menuKeyLongHandled = false;
            }
            else if (kbd.counter == 16 && !menuKeyLongHandled)
            {
                menuKeyPendingShort = false;
                menuKeyLongHandled = true;
                ResetSpectrumToDefaults();
            }
            return true;
        }

        if (currentState == FREQ_INPUT)
            OnKeyDownFreqInput(kbd.current);

        else if (!OnKeyDownCommon(kbd.current)) {
            if (currentState == SPECTRUM)
                OnKeyDown(kbd.current);
            else if (currentState == STILL)
                OnKeyDownStill(kbd.current);
        }
    }

    return true;
}

static void Scan()
{
    uint8_t slot = GetHistorySlot(scanInfo.i);

    if (rssiHistory[slot] != RSSI_MAX_VALUE
#ifdef ENABLE_SCAN_RANGES
        && !IsBlacklisted(scanInfo.i)
#endif
    )
    {
        SetFScan(scanInfo.f);
        Measure();
        UpdateScanInfo();
    }
}

static void NextScanStep()
{
    ++peak.t;
    if (scanForward) {
        ++scanInfo.i;
        scanInfo.f += scanInfo.scanStep;
    } else {
        --scanInfo.i;
        scanInfo.f -= scanInfo.scanStep;
    }
}

#if SPECTRUM_INTERLACE_LARGE_SWEEPS
static bool UseInterlacedSweep()
{
    return scanInfo.measurementsCount > ARRAY_SIZE(rssiHistory) &&
           interlaceStride > 1;
}

static bool NextScanStepInterlaced()
{
    uint16_t next = scanInfo.i + interlaceStride;

    if (next < scanInfo.measurementsCount)
    {
        scanInfo.i = next;
        scanInfo.f += (uint32_t)interlaceStride * scanInfo.scanStep;
        return false;
    }

    for (uint16_t phase = interlacePhase + 1; phase < interlaceStride; ++phase)
    {
        if (phase < scanInfo.measurementsCount)
        {
            interlacePhase = phase;
            scanInfo.i = phase;
            scanInfo.f = GetFStart() + (uint32_t)phase * scanInfo.scanStep;
            return false;
        }
    }

    interlacePhase = 0;
    return true;
}
#endif

static void FinalizeCompletedSweep()
{
    if (! (scanInfo.measurementsCount >> 7)) // if (scanInfo.measurementsCount < 128)
        memset(&rssiHistory[scanInfo.measurementsCount], 0,
               sizeof(rssiHistory) - scanInfo.measurementsCount * sizeof(rssiHistory[0]));

    // Auto-adjust dbMax unless the user has overridden it manually.
    if (manualDbMaxTimer > 0) {
        if (--manualDbMaxTimer == 0)
            redrawStatus = true;
    } else if (!manualSetFlag) {
        int newMax = Rssi2DBm(scanInfo.rssiMax) + 5;
        int dbMin = settings.dbMin + 10;
        if (newMax < dbMin)
            newMax = dbMin;
        if (newMax > 10)
            newMax = 10;
        settings.dbMax = newMax;
    }

    // Next full sweep starts from the opposite side to avoid directional bias.
    scanStartFromLeft = !scanStartFromLeft;
    newScanStart = true;
}

static void UpdateScan()
{
    Scan();

#if SPECTRUM_INTERLACE_LARGE_SWEEPS
    if (UseInterlacedSweep())
    {
        bool atEnd = (scanInfo.i + interlaceStride >= scanInfo.measurementsCount);

        if (!atEnd)
        {
            ++peak.t;
            (void)NextScanStepInterlaced();
            return;
        }

        preventKeypress = false;

        UpdatePeakInfo();
        if (IsPeakOverOpenLevel())
        {
            ToggleRX(true);
            TuneToPeak();
            return;
        }

        ++peak.t;
        if (!NextScanStepInterlaced())
            return;

        FinalizeCompletedSweep();
        return;
    }
#endif

    bool atEnd = scanForward ? (scanInfo.i >= scanInfo.measurementsCount - 1)
                             : (scanInfo.i <= 1);

    if (!atEnd)
    {
        NextScanStep();
        return;
    }

    // End of half-sweep: unlock keypad; Render() fires on its own timer.
    preventKeypress = false;

    UpdatePeakInfo();
    if (IsPeakOverOpenLevel())
    {
        ToggleRX(true);
        TuneToPeak();
        return;
    }

    if (scanReturnPending)
    {
        // Finish the opposite half-sweep before finalizing this cycle.
        scanReturnPending = false;
        scanForward = !scanForward;
        NextScanStep();
        return;
    }

    // Full round trip done.
    FinalizeCompletedSweep();
}

static void UpdateStill()
{
    Measure();
    redrawScreen = true;
    preventKeypress = false;

    peak.rssi = scanInfo.rssi;
    // EMA α=0.25 for display only; seed on first sample
    rssiSmoothed = rssiSmoothed ? (rssiSmoothed * 3 + scanInfo.rssi) >> 2
                                : scanInfo.rssi;
    AutoTriggerLevel();

    if (IsPeakOverOpenLevel() || monitorMode) {
        ToggleRX(true);
    }
}

static void UpdateListening()
{
    preventKeypress = false;

    // listenT counts down with 1ms delay per tick — no SPI during this phase.
    if (listenT)
    {
        listenT--;
        SYSTEM_DelayMs(1);
        return;
    }

    // --- Single SPI burst: all BK4819 accesses happen here, once per
    // listenT expiry (every 320 ms).  SPI repeats at ~3 Hz — below the
    // audible range.  Between bursts the bus is completely silent.

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
    bool tailFound = checkIfTailFound();
    if (tailFound)
    {
        ToggleRX(false);
        ResetScanStats();
        ResetPeak();
        RequestAutoTriggerRecalibration();
        newScanStart = true;
        redrawStatus = true;
        return;
    }
#endif

    if (currentState == SPECTRUM)
    {
        BK4819_WriteRegister(0x43, GetBWRegValueForScan());
        Measure();
        BK4819_WriteRegister(0x43, listenBWRegValues[settings.listenBw]);
    }
    else
    {
#ifndef ENABLE_FEAT_F4HWN_SPECTRUM
        if (currentState == STILL)
        {
            ToggleRX(false);
            ResetScanStats();
            ResetPeak();
            RequestAutoTriggerRecalibration();
            newScanStart = true;
            redrawStatus = true;
            return;
        }
#endif
        Measure();
    }

    peak.rssi = scanInfo.rssi;
    rssiSmoothed = rssiSmoothed ? (rssiSmoothed * 3 + scanInfo.rssi) >> 2
                                : scanInfo.rssi;
    redrawScreen = true;
    redrawStatus = true;

    bool abruptDrop = false;
    if (!monitorMode && listenPrevRssi != RSSI_MAX_VALUE &&
        listenPrevRssi > LISTEN_DROP_EXIT_RSSI)
    {
        // End TX usually appears as a sharp RSSI fall; leave RX quickly and
        // resume sweep instead of waiting for the debounce path.
        abruptDrop = (scanInfo.rssi + LISTEN_DROP_EXIT_RSSI) <= listenPrevRssi;
    }
    listenPrevRssi = scanInfo.rssi;

    bool keepListening = monitorMode;
    if (!keepListening)
    {
        if (abruptDrop)
        {
            listenLowCount = 0;
            keepListening = false;
        }
        else if (IsListeningSignalPresent(scanInfo.rssi))
        {
            listenLowCount = 0;
            keepListening = true;
        }
        else if (++listenLowCount < LISTEN_RELEASE_LOW_COUNT)
        {
            keepListening = true;
        }
        else
        {
            listenLowCount = 0;
            keepListening = false;
        }
    }

    if (keepListening)
    {
        listenT = 320;
        return;
    }

    ToggleRX(false);
    ResetScanStats();
    ResetPeak();
    RequestAutoTriggerRecalibration();
    newScanStart = true;
    redrawStatus = true;
}

static void Tick()
{
#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
    // Parse incoming packets on every tick so serial keys are never missed,
    // regardless of whether the screen needs redrawing.
    SCREENSHOT_ParseInput();
#endif

    if (gNextTimeslice)
    {
        gNextTimeslice = false;
#ifdef ENABLE_AM_FIX
        if (settings.modulationType == MODULATION_AM && !lockAGC)
        {
            AM_fix_10ms(vfo); // allow AM_Fix to apply its AGC action
        }
#endif
        BACKLIGHT_Update();
    }

#ifdef ENABLE_SCAN_RANGES
    if (gNextTimeslice_500ms)
    {
        gNextTimeslice_500ms = false;

        // For large scans (>128 steps), refresh display periodically but
        // wait for the full sweep to complete before triggering listen mode.
        // This avoids showing stale rssiHistory data from a previous sweep.
        if (GetStepsCount() > 128 && !isListening)
        {
            redrawScreen = true;
            preventKeypress = false;
        }
    }
#endif

    if (!preventKeypress)
    {
        HandleUserInput();
    }
    if (newScanStart)
    {
        InitScanPosition();
        newScanStart = false;
    }
    if (isListening && currentState != FREQ_INPUT)
    {
        UpdateListening();
    }
    else
    {
        if (currentState == SPECTRUM)
        {
            UpdateScan();
        }
        else if (currentState == STILL)
        {
            UpdateStill();
        }
    }
    if (redrawStatus || ++statuslineUpdateTimer > 4096)
    {
        RenderStatus();
        redrawStatus = false;
        statuslineUpdateTimer = 0;
    }
    // Render at a fixed rate (RENDER_PERIOD_TICKS) independent of step count,
    // so the CPU burst from Render() never falls below the ~9 Hz flutter-fusion
    // threshold regardless of how many steps the scan uses.  redrawScreen can
    // still force an immediate repaint (key presses, settings changes, etc.).
    if (redrawScreen || ++renderTimer >= RENDER_PERIOD_TICKS)
    {
        Render();
        // For screenshot
        #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            SCREENSHOT_Update(false);
        #endif
        redrawScreen = false;
        renderTimer = 0;
    }

    // Send one framebuffer page to the display per tick (~47 Hz full refresh).
    ST7565_BlitLine(renderPage);
    if (++renderPage >= FRAME_LINES)
        renderPage = 0;
}

void APP_RunSpectrum()
{
    settings.backlightState = gEeprom.BACKLIGHT_TIME == 0 ? false : true;

    // TX here coz it always? set to active VFO
    vfo = gEeprom.TX_VFO;
#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
    LoadSettings();
#endif
    // set the current frequency in the middle of the display
#ifdef ENABLE_SCAN_RANGES
    if (gScanRangeStart)
    {
        currentFreq = initialFreq = gScanRangeStart;
        // Keep saved spectrum step/count in scan-range mode.
        // Previously this branch forced scanStepIndex from VFO step and
        // stepsCount to STEPS_128 on every entry, which made the user think
        // spectrum settings were not persisted.
        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
            gEeprom.CURRENT_STATE = 5;
        #endif
    }
    else {
#endif
        currentFreq = initialFreq = gTxVfo->pRX->Frequency -
                                    ((GetStepsCount() / 2) * GetScanStep());
        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
            gEeprom.CURRENT_STATE = 4;
        #endif
    }

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        SETTINGS_WriteCurrentState();
    #endif

    BackupRegisters();

    isListening = true; // to turn off RX later
    newScanStart = true;
    scanStartFromLeft = true;

    ToggleRX(true), ToggleRX(false); // hack to prevent noise when squelch off
    RADIO_SetModulation(settings.modulationType = gTxVfo->Modulation);

#ifdef ENABLE_FEAT_F4HWN_SPECTRUM
    BK4819_SetFilterBandwidth(settings.listenBw, false);
#else
    BK4819_SetFilterBandwidth(settings.listenBw = BK4819_FILTER_BW_WIDE, false);
#endif

    // Reset dynamic spectrum state on every entry.
    // Persisted settings are step/count/listenBW only; trigger and dB window
    // are runtime values and should not carry over between sessions.
    // manualSetFlag = false;
    // settings.rssiTriggerLevel = RSSI_MAX_VALUE;

    RearmRuntimeState();

    isInitialized = true;

    while (isInitialized)
    {
        Tick();
    }

    BACKLIGHT_TurnOn();
}
