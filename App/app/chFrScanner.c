
#include <stddef.h>

#include "app/app.h"
#include "app/chFrScanner.h"
#include "audio.h"
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
#include "driver/systick.h"
#endif
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/main.h"
//#include "debugging.h"

int8_t            gScanStateDir;
bool              gScanKeepResult;
bool              gScanPauseMode;

#ifdef ENABLE_SCAN_RANGES
uint32_t          gScanRangeStart;
uint32_t          gScanRangeStop;
#endif

typedef enum {
    SCAN_NEXT_CHAN_SCANLIST1 = 0,
    SCAN_NEXT_CHAN_SCANLIST2,
    SCAN_NEXT_CHAN_DUAL_WATCH,
    SCAN_NEXT_CHAN_MR,
    SCAN_NEXT_NUM
} scan_next_chan_t;

scan_next_chan_t    currentScanList;
uint32_t            initialFrqOrChan;
uint8_t             initialCROSS_BAND_RX_TX;

#ifndef ENABLE_FEAT_F4HWN
    uint32_t lastFoundFrqOrChan;
#else
    uint32_t lastFoundFrqOrChan;
    uint32_t lastFoundFrqOrChanOld;
#endif

static void NextFreqChannel(void);
static void NextMemChannel(void);

static void CHFRSCANNER_AbortActiveReception(void)
{
    if (!FUNCTION_IsRx())
        return;

    AUDIO_AudioPathOff();
    gEnableSpeaker   = false;
    gMonitor         = false;
    gRxReceptionMode = RX_MODE_NONE;
    gScanPauseMode   = false;

    FUNCTION_Init();
    FUNCTION_Select(FUNCTION_FOREGROUND);
}

#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
#define SCAN_FAST_PRECHECK_STEPS    6
#define SCAN_FAST_RSSI_MARGIN       16
#define SCAN_FAST_SQUELCH_MARGIN     8
#define SCAN_FAST_WEAK_MARGIN        8
#define SCAN_FAST_RECHECK_DELAY_US 350
#define SCAN_FAST_FINE_STEP_LIMIT   250
#define SCAN_FAST_FINE_REFINE_SPAN 1000
#define SCAN_FAST_FINE_REFINE_MAX    80
#define SCAN_FAST_FINE_RSSI_DROP      8
#define SCAN_FAST_RSSI_MAX          65535u
// Settle loop guard: at most this many 1us waits while the BK4819 glitch
// indicator stays above SCAN_FAST_GLITCH_THRESHOLD. Caps the worst-case
// settling time per step at ~50us before we read the RSSI anyway.
#define SCAN_FAST_GLITCH_GUARD_MAX  50
#define SCAN_FAST_GLITCH_THRESHOLD  200
// HF/VHF boundary in Hz: BK4819_PickRXFilterPathBasedOnFrequency() switches
// the front-end filter path here, so we only re-run that (relatively
// expensive) call when we actually cross the boundary.
#define SCAN_FAST_HF_VHF_BOUNDARY_HZ 28000000u

typedef enum {
    SCAN_FAST_DISABLED,
    SCAN_FAST_QUIET_BATCH,
    SCAN_FAST_CANDIDATE
} scan_fast_result_t;

static uint16_t scanFastReg30;
static uint16_t scanFastNoiseFloor   = SCAN_FAST_RSSI_MAX;
static uint32_t scanFastPrevFrequency;
static bool     scanFastLastFullTuneCandidate;
static VFO_Info_t scanFastDisplayVfo;
static bool       scanFastDisplayVfoValid;

static void ScanFastResetState(void)
{
    // Called on every scan (re)start, after a reception, and on each
    // wraparound to the start of the channel list / range. The noise
    // floor is re-warmed up from current conditions instead of carrying
    // stale calibration into a later sweep.
    scanFastNoiseFloor            = SCAN_FAST_RSSI_MAX;
    scanFastPrevFrequency         = 0;
    scanFastLastFullTuneCandidate = false;
    scanFastDisplayVfoValid       = false;
}

static void ScanFastResetNoiseFloor(void)
{
    scanFastNoiseFloor = SCAN_FAST_RSSI_MAX;
}

static uint16_t ScanFastReadRssi(void)
{
    uint8_t guard = SCAN_FAST_GLITCH_GUARD_MAX;
    while (guard-- && BK4819_GetGlitchIndicator() >= SCAN_FAST_GLITCH_THRESHOLD)
    {
        SYSTICK_DelayUs(1);
    }

    // Discard first read: after fast tuning the RSSI/AGC value may still be stale.
    BK4819_GetRSSI();
    return BK4819_GetRSSI();
}

static void ScanFastTune(uint32_t frequency)
{
    if (scanFastPrevFrequency == 0 ||
        ((frequency < SCAN_FAST_HF_VHF_BOUNDARY_HZ) !=
         (scanFastPrevFrequency < SCAN_FAST_HF_VHF_BOUNDARY_HZ)))
    {
        BK4819_PickRXFilterPathBasedOnFrequency(frequency);
    }

    scanFastPrevFrequency = frequency;
    BK4819_SetFrequency(frequency);
    BK4819_WriteRegister(BK4819_REG_30, 0);
    BK4819_WriteRegister(BK4819_REG_30, scanFastReg30);
}

const VFO_Info_t *CHFRSCANNER_GetScanDisplayVfo(void)
{
    if (!scanFastDisplayVfoValid || gScanStateDir == SCAN_OFF || FUNCTION_IsRx())
        return NULL;

    return &scanFastDisplayVfo;
}

static bool ScanFastUpdateDisplayVfo(uint16_t channel, uint32_t *frequency, ModulationMode_t *modulation)
{
    ChannelScanDisplayInfo_t info;

    if (!SETTINGS_FetchChannelScanDisplayInfo(channel, &info))
    {
        scanFastDisplayVfoValid = false;
        return false;
    }

    scanFastDisplayVfo = gEeprom.VfoInfo[gEeprom.RX_VFO];

    scanFastDisplayVfo.CHANNEL_SAVE = channel;
    scanFastDisplayVfo.freq_config_RX = info.rx;
    scanFastDisplayVfo.freq_config_TX = info.tx;
    scanFastDisplayVfo.TX_OFFSET_FREQUENCY = info.offset;
    scanFastDisplayVfo.StepFrequency = info.stepFrequency;
    scanFastDisplayVfo.STEP_SETTING = info.stepSetting;
    scanFastDisplayVfo.Modulation = info.modulation;
    scanFastDisplayVfo.TX_OFFSET_FREQUENCY_DIRECTION = info.txOffsetFrequencyDirection;
    scanFastDisplayVfo.OUTPUT_POWER = info.outputPower;
    scanFastDisplayVfo.FrequencyReverse = info.frequencyReverse;
    scanFastDisplayVfo.CHANNEL_BANDWIDTH = info.channelBandwidth;
    scanFastDisplayVfo.BUSY_CHANNEL_LOCK = info.busyChannelLock;
    scanFastDisplayVfo.TX_LOCK = info.txLock;
#ifdef ENABLE_DTMF_CALLING
    scanFastDisplayVfo.DTMF_DECODING_ENABLE = info.dtmfDecodingEnable;
#endif
    scanFastDisplayVfo.DTMF_PTT_ID_TX_MODE = info.dtmfPttIdTxMode;

    if (!scanFastDisplayVfo.FrequencyReverse)
    {
        scanFastDisplayVfo.pRX = &scanFastDisplayVfo.freq_config_RX;
        scanFastDisplayVfo.pTX = &scanFastDisplayVfo.freq_config_TX;
    }
    else
    {
        scanFastDisplayVfo.pRX = &scanFastDisplayVfo.freq_config_TX;
        scanFastDisplayVfo.pTX = &scanFastDisplayVfo.freq_config_RX;
    }

    scanFastDisplayVfoValid = true;

    if (frequency)
        *frequency = info.rx.Frequency;
    if (modulation)
        *modulation = info.modulation;

    return true;
}

static uint16_t ScanFastSaturatingAdd(uint16_t value, uint16_t add)
{
    return (value > SCAN_FAST_RSSI_MAX - add) ? SCAN_FAST_RSSI_MAX : (uint16_t)(value + add);
}

static uint16_t ScanFastSaturatingSub(uint16_t value, uint16_t sub)
{
    return (value > sub) ? (uint16_t)(value - sub) : 0;
}

static uint16_t ScanFastGetNoiseTrigger(void)
{
    return ScanFastSaturatingAdd(scanFastNoiseFloor, SCAN_FAST_RSSI_MARGIN);
}

static uint16_t ScanFastGetSquelchTrigger(void)
{
    return ScanFastSaturatingSub(gRxVfo->SquelchOpenRSSIThresh, SCAN_FAST_SQUELCH_MARGIN);
}

static bool ScanFastIsNearCandidate(uint16_t rssi)
{
    const uint16_t rssiWithMargin  = ScanFastSaturatingAdd(rssi, SCAN_FAST_WEAK_MARGIN);
    const uint16_t squelchTrigger  = ScanFastGetSquelchTrigger();

    if (scanFastNoiseFloor == SCAN_FAST_RSSI_MAX)
        return gRxVfo->SquelchOpenRSSIThresh > 0 &&
               rssiWithMargin >= gRxVfo->SquelchOpenRSSIThresh;

    return rssiWithMargin >= ScanFastGetNoiseTrigger() &&
           rssiWithMargin >= squelchTrigger;
}

static uint16_t ScanFastReadCandidateRssi(void)
{
    uint16_t rssi = ScanFastReadRssi();

    if (ScanFastIsNearCandidate(rssi))
    {
        SYSTICK_DelayUs(SCAN_FAST_RECHECK_DELAY_US);

        const uint16_t retryRssi = ScanFastReadRssi();
        if (retryRssi > rssi)
            rssi = retryRssi;
    }

    return rssi;
}

static bool ScanFastIsCandidate(uint16_t rssi)
{
    const uint16_t squelchTrigger = ScanFastGetSquelchTrigger();

    if (scanFastNoiseFloor == SCAN_FAST_RSSI_MAX)
    {
        scanFastNoiseFloor = rssi;
        return gRxVfo->SquelchOpenRSSIThresh > 0 &&
               rssi >= gRxVfo->SquelchOpenRSSIThresh;
    }

    const uint16_t noiseTrigger   = ScanFastGetNoiseTrigger();
    const uint16_t rssiWithMargin = ScanFastSaturatingAdd(rssi, SCAN_FAST_WEAK_MARGIN);

    if ((rssi >= noiseTrigger && rssi >= squelchTrigger) ||
        (rssiWithMargin >= noiseTrigger && rssiWithMargin >= squelchTrigger))
    {
        return true;
    }

    if (rssi < scanFastNoiseFloor)
        scanFastNoiseFloor = rssi;
    else
        scanFastNoiseFloor = (uint16_t)((7u * scanFastNoiseFloor + rssi + 4u) >> 3);

    return false;
}

static void ScanFastApplyChannelShape(ModulationMode_t modulation)
{
    const bool modulationChanged = gRxVfo->Modulation != modulation;
    const bool bandwidthChanged  = gRxVfo->CHANNEL_BANDWIDTH != BANDWIDTH_WIDE;

    if (!modulationChanged && !bandwidthChanged)
        return;

    gRxVfo->Modulation         = modulation;
    gRxVfo->CHANNEL_BANDWIDTH  = BANDWIDTH_WIDE;

    if (modulationChanged)
        RADIO_SetModulation(modulation);

    if (modulation == MODULATION_AM)
    {
        BK4819_SetFilterBandwidth(RADIO_GetAMFilterBandwidth(gRxVfo), true);
    }
    else
    {
#ifdef ENABLE_AM_FIX
        BK4819_SetFilterBandwidth(BK4819_FILTER_BW_WIDE, true);
#else
        BK4819_SetFilterBandwidth(BK4819_FILTER_BW_WIDE, false);
#endif
    }

    if (modulationChanged)
    {
        // AM and FM use different demod/AGC profiles, so their RSSI
        // baselines are not directly comparable. Relearn the floor after
        // crossing that boundary instead of treating the next FM block as
        // a wall of candidates.
        ScanFastResetNoiseFloor();
    }
}
#endif

#if defined(ENABLE_FEAT_F4HWN_SCAN_FASTER) && defined(ENABLE_SCAN_RANGES)
static void ScanRangeFastRefineCandidate(uint16_t firstRssi)
{
    const uint16_t step = gRxVfo->StepFrequency;
    if (step == 0 || step >= SCAN_FAST_FINE_STEP_LIMIT)
        return;

    uint16_t maxSteps = SCAN_FAST_FINE_REFINE_SPAN / step;
    if (maxSteps == 0)
        maxSteps = 1;
    if (maxSteps > SCAN_FAST_FINE_REFINE_MAX)
        maxSteps = SCAN_FAST_FINE_REFINE_MAX;

    uint16_t bestRssi = firstRssi;
    uint32_t bestFrequency = gRxVfo->freq_config_RX.Frequency;
    uint8_t fallingSteps = 0;

    for (uint16_t i = 0; i < maxSteps; ++i)
    {
        const uint32_t prevRxFrequency = gRxVfo->pRX->Frequency;

        gRxVfo->freq_config_RX.Frequency =
            APP_SetFreqByStepAndLimits(gRxVfo, gScanStateDir, gScanRangeStart, gScanRangeStop);
        RADIO_ApplyOffset(gRxVfo);

        const uint32_t freq = gRxVfo->pRX->Frequency;
        if ((gScanStateDir > 0 && freq < prevRxFrequency) ||
            (gScanStateDir < 0 && freq > prevRxFrequency))
        {
            break;
        }

        ScanFastTune(freq);

        const uint16_t rssi = ScanFastReadRssi();
        if (rssi > bestRssi)
        {
            bestRssi = rssi;
            bestFrequency = gRxVfo->freq_config_RX.Frequency;
            fallingSteps = 0;
        }
        else if (bestRssi > rssi && bestRssi - rssi >= SCAN_FAST_FINE_RSSI_DROP)
        {
            if (++fallingSteps >= 3)
                break;
        }
    }

    gRxVfo->freq_config_RX.Frequency = bestFrequency;
    RADIO_ApplyOffset(gRxVfo);
    ScanFastTune(gRxVfo->pRX->Frequency);
}

static scan_fast_result_t ScanRangeFastPrecheck(void)
{
    if (!gScanRangeStart)
        return SCAN_FAST_DISABLED;

    if (gRxVfo->SquelchOpenRSSIThresh == 0)
        return SCAN_FAST_DISABLED;

    // Mute AF DAC during the precheck sweep: avoids audio glitches and
    // saves a few uA on each silent step. The bit is restored on the real
    // tune by RADIO_SetupRegisters() in NextFreqChannel().
    scanFastReg30 = BK4819_ReadRegister(BK4819_REG_30) & ~BK4819_REG_30_MASK_ENABLE_AF_DAC;

    for (uint8_t i = 0; i < SCAN_FAST_PRECHECK_STEPS; ++i)
    {
        gRxVfo->freq_config_RX.Frequency =
            APP_SetFreqByStepAndLimits(gRxVfo, gScanStateDir, gScanRangeStart, gScanRangeStop);
        RADIO_ApplyOffset(gRxVfo);

        const uint32_t freq = gRxVfo->pRX->Frequency;

        // Detect wraparound: scanning forward but the new freq is lower
        // than the previous one (or scanning backward but it's higher)
        // means the range has wrapped from stop back to start. Reset the
        // noise floor so the new pass re-warms up from current conditions.
        if (scanFastPrevFrequency != 0 &&
            ((gScanStateDir > 0 && freq < scanFastPrevFrequency) ||
             (gScanStateDir < 0 && freq > scanFastPrevFrequency)))
        {
            ScanFastResetState();
        }

        ScanFastTune(freq);

        const uint16_t rssi = ScanFastReadCandidateRssi();
        if (ScanFastIsCandidate(rssi))
        {
            ScanRangeFastRefineCandidate(rssi);
            return SCAN_FAST_CANDIDATE;
        }
    }

    return SCAN_FAST_QUIET_BATCH;
}
#endif

#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
static bool MemChannelFastPrecheck(uint16_t channel)
{
    uint32_t frequency;
    ModulationMode_t modulation;

    if (gRxVfo->SquelchOpenRSSIThresh == 0)
    {
        scanFastLastFullTuneCandidate = false;
        return true;
    }

    if (!ScanFastUpdateDisplayVfo(channel, &frequency, &modulation))
    {
        scanFastLastFullTuneCandidate = false;
        return true;
    }

    ScanFastApplyChannelShape(modulation);

    // Mute AF DAC for the same reason as in ScanRangeFastPrecheck().
    scanFastReg30 = BK4819_ReadRegister(BK4819_REG_30) & ~BK4819_REG_30_MASK_ENABLE_AF_DAC;
    ScanFastTune(frequency);

    if (ScanFastIsCandidate(ScanFastReadCandidateRssi()))
    {
        scanFastLastFullTuneCandidate = true;
        return true;  // signal detected: let the full tune path follow
    }

    // No signal here: still mirror the probed frequency in the VFO so the
    // status line (channel name + frequency) keeps in sync as we skip.
    // RADIO_ConfigureChannel() will overwrite these values cleanly when a
    // candidate is eventually retained.
    scanFastLastFullTuneCandidate = false;
    gRxVfo->freq_config_RX.Frequency = frequency;
    return false;
}

static void AdvanceMemScanList(const bool enabled)
{
    if (enabled)
        if (++currentScanList >= SCAN_NEXT_NUM)
            currentScanList = SCAN_NEXT_CHAN_SCANLIST1;
}

static void SetMemScanProgressChannel(uint16_t channel)
{
    gEeprom.MrChannel[    gEeprom.RX_VFO] = channel;
    gEeprom.ScreenChannel[gEeprom.RX_VFO] = channel;
    gRxVfo->CHANNEL_SAVE = channel;
}
#endif

#if defined(ENABLE_FEAT_F4HWN_RESUME_STATE) || defined(ENABLE_SCAN_RANGES)
    void CHFRSCANNER_ScanRange(void) {
        gScanRangeStart = gScanRangeStart ? 0 : gTxVfo->pRX->Frequency;
        gScanRangeStop = gEeprom.VfoInfo[!gEeprom.TX_VFO].freq_config_RX.Frequency;
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        ScanFastResetState();
#endif
        if(gScanRangeStart > gScanRangeStop)
            SWAP(gScanRangeStart, gScanRangeStop);
    }
#endif

void CHFRSCANNER_Start(const bool storeBackupSettings, const int8_t scan_direction)
{
    if (storeBackupSettings) {
        initialCROSS_BAND_RX_TX = gEeprom.CROSS_BAND_RX_TX;
        gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
        gScanKeepResult = false;
    }
    
    RADIO_SelectVfos();
    CHFRSCANNER_AbortActiveReception();

    gNextMrChannel   = gRxVfo->CHANNEL_SAVE;
    currentScanList = SCAN_NEXT_CHAN_SCANLIST1;
    gScanStateDir    = scan_direction;
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    ScanFastResetState();
#endif

    if (IS_MR_CHANNEL(gNextMrChannel))
    {   

        if(!RADIO_CheckValidList(gEeprom.SCAN_LIST_DEFAULT)) {
            RADIO_NextValidList(1);
            UI_MAIN_NotifyScanProgressDataChanged();
            gUpdateStatus = true;
        }

        // channel mode
        if (storeBackupSettings) {
            initialFrqOrChan = gRxVfo->CHANNEL_SAVE;
            lastFoundFrqOrChan = initialFrqOrChan;
        }
        NextMemChannel();
    }
    else
    {   // frequency mode
        if (storeBackupSettings) {
            initialFrqOrChan = gRxVfo->freq_config_RX.Frequency;
            lastFoundFrqOrChan = initialFrqOrChan;
        }
        NextFreqChannel();
    }

#ifdef ENABLE_FEAT_F4HWN
    lastFoundFrqOrChanOld = lastFoundFrqOrChan;
#endif

    gScanPauseDelayIn_10ms = scan_pause_delay_in_2_10ms;
    gScheduleScanListen    = false;
    gRxReceptionMode       = RX_MODE_NONE;
    gScanPauseMode         = false;
}

void CHFRSCANNER_ManualResume(const int8_t scan_direction)
{
    CHFRSCANNER_Start(false, scan_direction);

    gScanPauseDelayIn_10ms = (gRxVfo->SquelchOpenRSSIThresh == 0)
        ? scan_pause_delay_in_3_10ms
        : 1;
    gScheduleScanListen    = false;
}

/*
void CHFRSCANNER_ContinueScanning(void)
{
    if (IS_FREQ_CHANNEL(gNextMrChannel))
    {
        if (gCurrentFunction == FUNCTION_INCOMING)
            APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
        else
            NextFreqChannel();  // switch to next frequency
    }
    else
    {
        if (gCurrentCodeType == CODE_TYPE_OFF && gCurrentFunction == FUNCTION_INCOMING)
            APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
        else
            NextMemChannel();    // switch to next channel
    }
    
    gScanPauseMode      = false;
    gRxReceptionMode    = RX_MODE_NONE;
    gScheduleScanListen = false;
}
*/

void CHFRSCANNER_ContinueScanning(void)
{
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    if (scanFastLastFullTuneCandidate &&
        gCurrentFunction != FUNCTION_INCOMING &&
        !g_SquelchLost)
    {
        // A rejected full-tune candidate is just a false RSSI hit. Keep the
        // learned floor; resetting here can make the next channel blind when
        // it is the real signal, especially while scanning down.
        scanFastLastFullTuneCandidate = false;
    }
#endif

    if (gCurrentFunction == FUNCTION_INCOMING &&
        (IS_FREQ_CHANNEL(gNextMrChannel) || gCurrentCodeType == CODE_TYPE_OFF))
    {
        APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
    }
    else
    {
        IS_FREQ_CHANNEL(gNextMrChannel) ? NextFreqChannel() : NextMemChannel();
    }

    gScanPauseMode      = false;
    gRxReceptionMode    = RX_MODE_NONE;
    gScheduleScanListen = false;
}

void CHFRSCANNER_Found(void)
{
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    // After a real reception the BK4819 AGC has shifted, biasing the next
    // few RSSI readings high. Reset the precheck state so it warms up from
    // the current noise floor instead of carrying stale calibration into
    // the post-reception scan, which otherwise turns nearly every channel
    // into a CANDIDATE and erases the speed gain.
    ScanFastResetState();
#endif

    if (gEeprom.SCAN_RESUME_MODE > 80) {
        if (!gScanPauseMode) {
            gScanPauseDelayIn_10ms = scan_pause_delay_in_5_10ms * (gEeprom.SCAN_RESUME_MODE - 80) * 5;
            gScanPauseMode = true;
        }
    } else {
        gScanPauseDelayIn_10ms = 0;
    }

    // gScheduleScanListen is always false...
    gScheduleScanListen = false;

    /*
    if(gEeprom.SCAN_RESUME_MODE > 1 && gEeprom.SCAN_RESUME_MODE < 26)
    {
        if (!gScanPauseMode)
        {
            gScanPauseDelayIn_10ms = scan_pause_delay_in_5_10ms * (gEeprom.SCAN_RESUME_MODE - 1) * 5;
            gScheduleScanListen    = false;
            gScanPauseMode         = true;
        }
    }
    else
    {
        gScanPauseDelayIn_10ms = 0;
        gScheduleScanListen    = false;
    }
    */

    /*
    switch (gEeprom.SCAN_RESUME_MODE)
    {
        case SCAN_RESUME_TO:
            if (!gScanPauseMode)
            {
                gScanPauseDelayIn_10ms = scan_pause_delay_in_1_10ms;
                gScheduleScanListen    = false;
                gScanPauseMode         = true;
            }
            break;

        case SCAN_RESUME_CO:
        case SCAN_RESUME_SE:
            gScanPauseDelayIn_10ms = 0;
            gScheduleScanListen    = false;
            break;
    }
    */

#ifdef ENABLE_FEAT_F4HWN
    lastFoundFrqOrChanOld = lastFoundFrqOrChan;
#endif

    if (IS_MR_CHANNEL(gRxVfo->CHANNEL_SAVE)) { //memory scan
        lastFoundFrqOrChan = gRxVfo->CHANNEL_SAVE;
    }
    else { // frequency scan
        lastFoundFrqOrChan = gRxVfo->freq_config_RX.Frequency;
    }


    gScanKeepResult = true;
}

void CHFRSCANNER_Stop(void)
{
    if(initialCROSS_BAND_RX_TX != CROSS_BAND_OFF) {
        gEeprom.CROSS_BAND_RX_TX = initialCROSS_BAND_RX_TX;
        initialCROSS_BAND_RX_TX = CROSS_BAND_OFF;
    }
    
    gScanStateDir = SCAN_OFF;

    const uint32_t chFr = gScanKeepResult ? lastFoundFrqOrChan : initialFrqOrChan;
    const bool channelChanged = chFr != initialFrqOrChan;
    if (IS_MR_CHANNEL(gNextMrChannel)) {
        gEeprom.MrChannel[gEeprom.RX_VFO]     = chFr;
        gEeprom.ScreenChannel[gEeprom.RX_VFO] = chFr;
        RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);

        if(channelChanged) {
            SETTINGS_SaveVfoIndices();
            gUpdateStatus = true;
        }
    }
    else {
        gRxVfo->freq_config_RX.Frequency = chFr;
        RADIO_ApplyOffset(gRxVfo);
        RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
        if(channelChanged) {
            SETTINGS_SaveChannel(gRxVfo->CHANNEL_SAVE, gEeprom.RX_VFO, gRxVfo, 1);
        }
    }

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.CURRENT_STATE = 0;
        SETTINGS_WriteCurrentState();
    #endif

    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;
}

static void NextFreqChannel(void)
{
#ifdef ENABLE_SCAN_RANGES
    if(gScanRangeStart) {
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        const scan_fast_result_t fastResult = ScanRangeFastPrecheck();

        if (fastResult == SCAN_FAST_QUIET_BATCH)
        {
            scanFastLastFullTuneCandidate = false;
            gScanPauseDelayIn_10ms = 1;
            gUpdateDisplay = true;
            return;
        }

        if (fastResult == SCAN_FAST_DISABLED)
        {
            scanFastLastFullTuneCandidate = false;
            gRxVfo->freq_config_RX.Frequency = APP_SetFreqByStepAndLimits(gRxVfo, gScanStateDir, gScanRangeStart, gScanRangeStop);
        }
        else
        {
            scanFastLastFullTuneCandidate = true;
        }
#else
        gRxVfo->freq_config_RX.Frequency = APP_SetFreqByStepAndLimits(gRxVfo, gScanStateDir, gScanRangeStart, gScanRangeStop);
#endif
    }
    else
#endif
    {
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        scanFastLastFullTuneCandidate = false;
#endif
        gRxVfo->freq_config_RX.Frequency = APP_SetFrequencyByStep(gRxVfo, gScanStateDir);
    }

    RADIO_ApplyOffset(gRxVfo);
    RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
    RADIO_SetupRegisters(true);

#ifdef ENABLE_FASTER_CHANNEL_SCAN
    gScanPauseDelayIn_10ms = 9;   // 90ms
#else
    gScanPauseDelayIn_10ms = scan_pause_delay_in_6_10ms;
#endif

    gUpdateDisplay     = true;
}

static void NextMemChannel(void)
{
    static uint16_t prev_mr_chan = 0;
    const bool      enabled      = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT <= MR_CHANNELS_LIST + 1) ? gEeprom.SCAN_LIST_ENABLED : true;
    const int16_t   chan1        = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT <= MR_CHANNELS_LIST + 1 && gEeprom.SCANLIST_PRIORITY_CH[0] != MR_CHANNELS_MAX) ? gEeprom.SCANLIST_PRIORITY_CH[0] : -1;
    const int16_t   chan2        = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT <= MR_CHANNELS_LIST + 1 && gEeprom.SCANLIST_PRIORITY_CH[1] != MR_CHANNELS_MAX) ? gEeprom.SCANLIST_PRIORITY_CH[1] : -1;
    const uint16_t  prev_chan    = gNextMrChannel;
    uint16_t        chan         = 0;

    //char str[64] = "";

    if (enabled)
    {
        switch (currentScanList)
        {
            case SCAN_NEXT_CHAN_SCANLIST1:
                prev_mr_chan = gNextMrChannel;
    
                //sprintf(str, "-> Chan1 %d\n", chan1 + 1);
                //LogUart(str);

                if (chan1 >= 0)
                {
                    if (RADIO_CheckValidChannel(chan1, false, gEeprom.SCAN_LIST_DEFAULT))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST1;
                        gNextMrChannel   = chan1;
                        break;
                    }
                }

                [[fallthrough]];
            case SCAN_NEXT_CHAN_SCANLIST2:

                //sprintf(str, "-> Chan2 %d\n", chan2 + 1);
                //LogUart(str);

                if (chan2 >= 0)
                {
                    if (RADIO_CheckValidChannel(chan2, false, gEeprom.SCAN_LIST_DEFAULT))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST2;
                        gNextMrChannel   = chan2;
                        break;
                    }
                }

                [[fallthrough]];
            /*
            case SCAN_NEXT_CHAN_SCANLIST3:
                if (chan3 >= 0)
                {
                    if (RADIO_CheckValidChannel(chan3, false, 0))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST3;
                        gNextMrChannel   = chan3;
                        break;
                    }
                }
                [[fallthrough]];
            */
            // this bit doesn't yet work if the other VFO is a frequency
            case SCAN_NEXT_CHAN_DUAL_WATCH:
                // dual watch is enabled - include the other VFO in the scan
//              if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF)
//              {
//                  chan = (gEeprom.RX_VFO + 1) & 1u;
//                  chan = gEeprom.ScreenChannel[chan];
//                  if (IS_MR_CHANNEL(chan))
//                  {
//                      currentScanList = SCAN_NEXT_CHAN_DUAL_WATCH;
//                      gNextMrChannel   = chan;
//                      break;
//                  }
//              }

            default:
            case SCAN_NEXT_CHAN_MR:
                currentScanList = SCAN_NEXT_CHAN_MR;
                gNextMrChannel   = prev_mr_chan;
                chan             = 0xFFFF;
                break;
        }
    }

    if (!enabled || chan == 0xFFFF)
    {
        const uint16_t searchStart = gNextMrChannel;
        chan = RADIO_FindNextChannel(gNextMrChannel + gScanStateDir, gScanStateDir, true, gEeprom.SCAN_LIST_DEFAULT);
        if (chan == 0xFFFF)
        {   // no valid channel found -> wrapping back to the first channel
            chan = MR_CHANNEL_FIRST;
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
            // Wraparound: re-warm the precheck noise floor on the new pass
            // so it tracks current RF conditions instead of an EMA that
            // accumulated drift over the previous full sweep.
            ScanFastResetState();
#endif
        }
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        else if ((gScanStateDir > 0 && chan < searchStart) ||
                 (gScanStateDir < 0 && chan > searchStart))
        {
            // RADIO_FindNextChannel() wraps internally, so 0xFFFF is not
            // returned on a normal full-sweep wrap. Detect that transition
            // here and restart the RSSI floor learning for the new pass.
            ScanFastResetState();
        }
#endif

        gNextMrChannel = chan;

        //sprintf(str, "----> Chan %d\n", chan + 1);
        //LogUart(str);
    }

#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    SetMemScanProgressChannel(gNextMrChannel);

    if (!MemChannelFastPrecheck(gNextMrChannel))
    {
        gScanPauseDelayIn_10ms = 1;
        gUpdateDisplay = true;
        AdvanceMemScanList(enabled);
        return;
    }
#endif

    if (gNextMrChannel != prev_chan
#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        || scanFastLastFullTuneCandidate
#endif
    )
    {
#ifndef ENABLE_FEAT_F4HWN_SCAN_FASTER
        gEeprom.MrChannel[    gEeprom.RX_VFO] = gNextMrChannel;
        gEeprom.ScreenChannel[gEeprom.RX_VFO] = gNextMrChannel;
#endif

        RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);
        RADIO_SetupRegisters(true);

        gUpdateDisplay = true;
    }

#ifdef ENABLE_FASTER_CHANNEL_SCAN
    gScanPauseDelayIn_10ms = 9;  // 90ms .. <= ~60ms it misses signals (squelch response and/or PLL lock time) ?
#else
    gScanPauseDelayIn_10ms = scan_pause_delay_in_3_10ms;
#endif

#ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
    AdvanceMemScanList(enabled);
#else
    if (enabled)
        if (++currentScanList >= SCAN_NEXT_NUM)
            currentScanList = SCAN_NEXT_CHAN_SCANLIST1;  // back round we go
#endif
}
