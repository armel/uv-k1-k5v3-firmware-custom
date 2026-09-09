/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
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

#include "app/foxhunt.h"

#if defined(ENABLE_FEAT_F4HWN_FOXHUNT) || defined(ENABLE_FEAT_F4HWN_BEACON)

#if defined(ENABLE_UART) || defined(ENABLE_USB)
#include "app/uart.h"
#endif

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
#include "k5viewer.h"
#endif

#include "settings.h"
#include "ui/status.h"

// Signal window mapped onto the RSSI bar, in dBm.
// Roughly S0 (empty) to S9 + 40 dB (full), IARU VHF/UHF scale.
#define FOXHUNT_DBM_FLOOR (-141)
#define FOXHUNT_DBM_CEIL  (-53)

// Gauge geometry inside the main frame buffer (y in 0..55, status line excluded).
// 13 segments on the IARU S-meter scale: S1..S9 are 6 dB apart, the four marks
// past S9 (+10..+40) are 10 dB apart, so the axis compresses at the top.
#define FOXHUNT_BAR_X0     6    // leftmost segment x (centres the 13 segments)
#define FOXHUNT_SEG_COUNT  13   // S1..S9 + (+10..+40)
#define FOXHUNT_SEG_PITCH  9    // px between segment starts
#define FOXHUNT_SEG_W      8    // segment width
#define FOXHUNT_SEG_BOTTOM 37   // base the staircase grows up from

// Refresh period and trend sampling window.
#define FOXHUNT_TICK_MS      50
#define FOXHUNT_TREND_TICKS  20   // compare the level roughly once per second

// Keypad lock: hold F for ~0.5 s to toggle it, mirroring the long-press F gesture of the
// main screen. Tracked as an elapsed-time accumulator (not a tick count) so the same hold
// fires in every loop — the 50 ms hunt/idle ticks and the 10 ms TX slices alike — letting
// the pad be locked or unlocked at any moment, mid-burst included. While locked every key
// is swallowed except this hold (and, in the hunt, the attenuation arrows), so a radio
// carried by a child cannot be knocked out of the mode or its settings changed.
#define FOXHUNT_LOCK_HOLD_MS 500

// Signal-history graph: a full-width scrolling trace of the level over the last
// seconds, on the same 13-level S scale as the staircase (FOXHUNT_FillCount). It is
// an alternate main gauge, toggled with the 2 key. The reading is EMA-smoothed each
// tick; one sample is stored every FOXHUNT_HIST_DECIM ticks, so the visible window
// spans LEN * DECIM * TICK_MS (120 * 3 * 50 ms = 18 s).
#define FOXHUNT_HIST_LEN    120  // samples kept (also the graph width in px)
#define FOXHUNT_HIST_DECIM  3    // ticks between stored samples (3 * 50 ms = 150 ms)
#define FOXHUNT_GRAPH_X0    4    // left column of the graph (frame-buffer x)
#define FOXHUNT_GRAPH_TOP   27   // top row (level 13, S9+40)
#define FOXHUNT_GRAPH_BOT   45   // baseline (axis) row
#define FOXHUNT_GRAPH_FLOOR 2    // gap between the level-0 line and the baseline, so
                                 // the lowest reading never sits on the axis

// Main-gauge mode cycled by the 2 key.
#define FOXHUNT_GRAPH_BAR   0    // S-meter staircase (default)
#define FOXHUNT_GRAPH_HIST  1    // full-width signal history

// Geiger audio: the blip RATE tracks the signal level (faster = closer), the
// pitch rises too as a secondary cue. Below the silence floor, no blips at all.
#define FOXHUNT_AUDIO_SETTLE_MS 60  // audio amplifier warm-up after enabling the speaker
#define FOXHUNT_BLIP_MS         50  // tone duration per blip
#define FOXHUNT_RATE_SLOW_TICKS 20  // ticks between blips at the floor (~1/s)
#define FOXHUNT_RATE_FAST_TICKS 2   // ticks between blips at the ceiling (fast)
#define FOXHUNT_SILENCE_DBM     (-120) // stay silent below this level (noise floor)
#define FOXHUNT_TONE_MIN        400  // Hz at the floor of the window
#define FOXHUNT_TONE_MAX        2400 // Hz at the ceiling of the window

// Audio mode cycled by the 1 key: silent -> Geiger beep -> received station.
#define FOXHUNT_AUDIO_OFF       0
#define FOXHUNT_AUDIO_BEEP      1
#define FOXHUNT_AUDIO_STATION   2

// --- Beacon (fox) app --------------------------------------------------------
// Turns the radio into the hidden transmitter: each cycle keys up on the TX VFO and
// repeats the CW fox identifier (MOE..MO5, or "<call> MOE") in Morse for the TX window,
// then stays silent for the (adjustable) idle gap. Two keying modes (KEY_4):
//   TONE (default): carrier stays up the whole window, only the tone is keyed on/off
//                   (EnterTxMute/ExitTxMute) = MCW on FM (F2A).
//   CARR:           the carrier (PA_ENABLE) is keyed together with the tone, so between
//                   elements the carrier itself is gone (carrier interruption, the ARDF
//                   field pattern) — harder to home in on, at the cost of some key clicks.
#define FOXHUNT_BEACON_TONE_HZ   1000           // CW tone pitch (Hz)
#define FOXHUNT_MORSE_UNIT_MS    100            // one Morse time unit (~12 WPM, ARDF pace)
#define FOXHUNT_BEACON_IDLE_DEF  30             // default silence between IDs (s)
#define FOXHUNT_BEACON_IDLE_MIN  5              // shortest idle gap (s)
#define FOXHUNT_BEACON_IDLE_MAX  240            // longest idle gap (s) — 4 min = the ARDF
                                                // silence of a 5-fox cycle (60 s TX x4)
#define FOXHUNT_BEACON_IDLE_STEP 5              // idle adjust step (s)
// TX window: the ID is repeated for this many seconds each cycle (a real ARDF fox keys
// up for a fixed slot, not a single one-shot). 60 s = the classic ARDF slot.
#define FOXHUNT_BEACON_TX_DEF    30             // default TX window (s)
#define FOXHUNT_BEACON_TX_MIN    5              // shortest TX window (s)
#define FOXHUNT_BEACON_TX_MAX    60             // longest TX window (s)
#define FOXHUNT_BEACON_TX_STEP   5              // TX adjust step (s)
#define FOXHUNT_CALLSIGN_MAX     12             // maximum boot-message characters used by the beacon

// Fox identifier (3 key). MOE..MO5 are the five standard IARU ARDF foxes ("MO" + 1..5
// dits: E I S H 5); MO is the ARDF finish/home beacon (transmitted continuously on a real
// course — here it rides the normal TX/IDLE cycle, so set a short IDLE for a near-constant
// MO); CALL prefixes the operator's callsign (legal ID on the ham bands): "<call> MOE".
// Pure MOE..MO5 / MO (no callsign) suit casual / PMR fox games.
#define FOXHUNT_FOX_MOE    0
#define FOXHUNT_FOX_MOI    1
#define FOXHUNT_FOX_MOS    2
#define FOXHUNT_FOX_MOH    3
#define FOXHUNT_FOX_MO5    4
#define FOXHUNT_FOX_MO     5
#define FOXHUNT_FOX_CALL   6
#define FOXHUNT_FOX_COUNT  7

// Framebuffer lines for the transmitted message (gFontBig spans two lines each):
// CALL id shows the callsign on _CALL and the "MOx" below on _ID; a pure single-word
// fox (MOE..MO5) is centred on _ONE.
#define FOXHUNT_MSG_LINE_CALL  1
#define FOXHUNT_MSG_LINE_ID    3
#define FOXHUNT_MSG_LINE_ONE   2

// Front-end gain ladder (KEY_3), written straight to the BK4829 gain register REG_13.
// Fields (LSB->MSB): pga[2:0], mixer[4:3], lna[7:5], lnaS[9:8]; gain tables live in
// bk4829.c (BK4819_GetRxGain_dB). The RX default is 0x03DF (pga7/mixer3/lna6/lnaS3).
//
// Steps 0..3 attenuate the PGA only (the last stage) — {pga 7,5,3,1} = 0/-6/-15/-27 dB
// on top of the default — same behaviour as before. Steps 4..5 attenuate at the *front*
// of the chain instead: right on top of the fox the LNA saturates, so cutting after it
// (PGA) no longer tracks the field; reducing the LNA itself brings it back into its
// linear region and restores the gradient for the final approach.
//
// What "BYP" / "BYP+" actually are: a label of convenience, NOT a hardware bypass — the
// BK4829 has no LNA-bypass switch we flip here. Both steps simply keep the PGA at minimum
// and pull the front-end gain fields of REG_13 further down:
//   BYP  (step 4): LNA reduced           (lna 6->3)              — front-end starts giving
//   BYP+ (step 5): LNA + LNA-short cut    (lna 3->1, lnaS 3->1)  — deepest front-end cut
//
// Total gain stays monotonic: -2/-8/-17/-29/-36/-70 dB. These values are a sane starting
// point — expect to trim them on the air against a strong close source. NB the shown dBm
// is not gain-compensated (bk4829.c), so the reading steps down when BYP engages; that is
// intended (it re-centres the S-meter).
#define FOXHUNT_ATT_COUNT  6    // 4 PGA steps + 2 front-end (BYP) steps
#define FOXHUNT_ATT_BYP0   4    // first step at which the front-end (LNA) is reduced
#define FOXHUNT_ATT_SETTLE_MS 40  // let the RSSI detector converge to the new gain before
                                  // rebasing the holds/trend/history onto it (tune on air)
static const uint16_t FOXHUNT_ATT_REG13[FOXHUNT_ATT_COUNT] = {
    0x03DF,   // 0    pga7                 ATT   0 dB (RX default)
    0x03DD,   // 1    pga5                 ATT  -6 dB
    0x03DB,   // 2    pga3                 ATT -15 dB
    0x03D9,   // 3    pga1                 ATT -27 dB
    0x0379,   // 4    pga1 lna3            BYP  (front-end starts giving)
    0x0139,   // 5    pga1 lna1 lnaS1      BYP+ (deep front-end cut)
};
static const uint8_t FOXHUNT_ATT_DB[FOXHUNT_ATT_BYP0] = {0, 6, 15, 27};   // PGA-step labels

static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

static bool    foxRunning;
static bool     foxLocked;     // keypad lock (long-press F toggles it, hunt + beacon)
static uint16_t fHoldMs;       // elapsed time the F key has been held (ms)
static bool     fLongDone;     // the long-press lock toggle already fired this hold
static uint8_t foxAudioMode;
static uint8_t foxGraphMode;   // FOXHUNT_GRAPH_BAR / _HIST, cycled by the 2 key
static uint8_t attStep;
static int16_t curDbm;
static int16_t peakDbm;
static int16_t minDbm;         // lowest level held since the last reset (body-scan null)
static int16_t trendRef;
static int16_t trendDelta;
static uint8_t trendTick;
static uint8_t audioTick;

// Signal-history ring: FOXHUNT_FillCount levels (0..13), oldest at histHead.
static uint8_t histBuf[FOXHUNT_HIST_LEN];
static uint8_t histHead;    // index of the oldest sample (next write slot)
static uint8_t histTick;    // decimation counter (0..FOXHUNT_HIST_DECIM-1)
static int16_t histEma;     // EMA-smoothed level feed, dBm in 1/8 units (x8 fixed)

static char str[14];   // widest write is the freq "1300.00000" (10 chars) + NUL, with slack

// Sentinel-prefixed Morse patterns: after the leading 1 sentinel bit, each lower
// bit is one element read MSB-first (0 = dit, 1 = dah). 0 = unsupported char.
static const uint8_t FOXHUNT_MORSE_LETTER[26] = {
    0x05, 0x18, 0x1A, 0x0C, 0x02, 0x12, 0x0E, 0x10, 0x04, 0x17,   // A..J
    0x0D, 0x14, 0x07, 0x06, 0x0F, 0x16, 0x1D, 0x0A, 0x08, 0x03,   // K..T
    0x09, 0x11, 0x0B, 0x19, 0x1B, 0x1C,                           // U..Z
};
static const uint8_t FOXHUNT_MORSE_DIGIT[10] = {
    0x3F, 0x2F, 0x27, 0x23, 0x21, 0x20, 0x30, 0x38, 0x3C, 0x3E,   // 0..9
};

// Trailing character of each fox id: MOE / MOI / MOS / MOH / MO5 (index = FOXHUNT_FOX_*).
static const char FOXHUNT_FOX_TAIL[5] = {'E', 'I', 'S', 'H', '5'};

// Beacon state.
static bool    beaconPhaseTx;     // true = transmit this tick, false = idle gap
static uint8_t beaconIdle;        // configured silence between IDs (s)
static uint8_t beaconIdleLeft;    // seconds left in the current silence
static uint8_t beaconIdleTick;    // tick counter that clocks the idle countdown
static char    beaconMsg[17];     // CW message: 12-char call + " MOE" + NUL, e.g. "F4HWN MOE"
static uint8_t beaconCharsSent;   // characters completed in the current burst
static uint8_t beaconTx;          // TX window length (s): the ID repeats for this long
static uint16_t beaconTxMsLeft;   // ms left in the current TX window (drained as it plays)
static uint8_t beaconTxSecShown;  // whole-second value last painted on the TX line
static uint8_t foxFox;            // selected fox identifier (FOXHUNT_FOX_*)
static bool    beaconCarrierKeyed; // false = TONE (F2A, keyed tone); true = CARR (keyed carrier)
static char    foxCall[FOXHUNT_CALLSIGN_MAX + 1];  // sanitised callsign for the CALL id

static void FOXHUNT_EnterHunt(void);
static void FOXHUNT_EnterBeacon(void);
static void FOXHUNT_BeaconDraw(bool txNow, uint8_t idleLeft);
static void FOXHUNT_UpdateBeaconProgress(uint8_t visibleChars);
static void FOXHUNT_BuildBeaconMsg(void);
static void FOXHUNT_SaveConfig(void);

// Read the calibrated signal level of the current RX VFO, in dBm.
static int16_t FOXHUNT_ReadDbm(void)
{
    return BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band];
}

// Apply the selected step's BK4829 gain preset to REG_13: the PGA for the ATT steps,
// plus the LNA/LNAs for the BYP steps. Read-modify-write masked to the known gain
// fields (bits 0..9), so the undocumented upper bits 10..15 are preserved rather than
// cleared. Every preset fits within 0x03FF, so only the gain fields ever change.
static void FOXHUNT_ApplyAtt(void)
{
    uint16_t reg = BK4819_ReadRegister(BK4819_REG_13);
    reg = (reg & ~0x03FFu) | FOXHUNT_ATT_REG13[attStep];
    BK4819_WriteRegister(BK4819_REG_13, reg);
}

// Set the speaker/AF state for the current audio mode.
static void FOXHUNT_SetAudio(void)
{
    if (foxAudioMode == FOXHUNT_AUDIO_OFF) {
        AUDIO_AudioPathOff();
        return;
    }
    // Speaker on, let the amplifier warm up, RX audio muted for now. BEEP keeps it
    // muted (only the blips are heard); STATION un-mutes per level in the loop.
    AUDIO_AudioPathOn();
    SYSTEM_DelayMs(FOXHUNT_AUDIO_SETTLE_MS);
    BK4819_SetAF(BK4819_AF_MUTE);
}

// Clamped linear map of dbm over the [FLOOR, CEIL] window onto [lo, hi] (hi may sit
// below lo for an inverted mapping). Shared by the Geiger pitch and blip-rate ramps.
static int32_t FOXHUNT_LerpDbm(int16_t dbm, int32_t lo, int32_t hi)
{
    if (dbm <= FOXHUNT_DBM_FLOOR) return lo;
    if (dbm >= FOXHUNT_DBM_CEIL)  return hi;
    return lo + ((int32_t)(dbm - FOXHUNT_DBM_FLOOR) * (hi - lo)) /
                (FOXHUNT_DBM_CEIL - FOXHUNT_DBM_FLOOR);
}

// Map a dBm value to a Geiger tone frequency (higher signal, higher pitch).
static uint16_t FOXHUNT_DbmToTone(int16_t dbm)
{
    return (uint16_t)FOXHUNT_LerpDbm(dbm, FOXHUNT_TONE_MIN, FOXHUNT_TONE_MAX);
}

// Ticks between two blips: fewer ticks = faster clicking as the signal rises.
static uint8_t FOXHUNT_BlipPeriod(int16_t dbm)
{
    return (uint8_t)FOXHUNT_LerpDbm(dbm, FOXHUNT_RATE_SLOW_TICKS, FOXHUNT_RATE_FAST_TICKS);
}

// Emit one short blip at the given pitch, then hand the chip back to RX.
// The speaker path is already on and settled (see the key-1 handler); between
// blips TurnsOffTones_TurnsOnRX mutes the AF, so the RX stays silent.
static void FOXHUNT_Blip(uint16_t freq)
{
    BK4819_PrepareToPlayTone(true);
    BK4819_PlayToneRaw(freq, FOXHUNT_BLIP_MS);
    BK4819_TurnsOffTones_TurnsOnRX();

    // TurnsOffTones_TurnsOnRX restores a normal RX front-end, so re-assert the
    // fixed gain and attenuator the level reading relies on.
    BK4819_SetAGC(false);
    FOXHUNT_ApplyAtt();
}

// Number of lit segments for a level, on the IARU S-meter scale:
// S1..S9 are 6 dB apart, the four marks past S9 (+10..+40) are 10 dB apart.
static uint8_t FOXHUNT_FillCount(int16_t dbm)
{
    int16_t n;
    if (dbm < -141) return 0;
    if (dbm <= -93)
        n = 1 + (dbm + 141) / 6;    // S1 (-141) .. S9 (-93)  -> 1..9
    else
        n = 9 + (dbm + 93) / 10;    // +10 (-83) .. +40 (-53) -> 10..13
    if (n > FOXHUNT_SEG_COUNT)
        n = FOXHUNT_SEG_COUNT;
    return (uint8_t)n;
}

// Build the IARU S-meter reading string (e.g. "S7" or "S9+30").
static void FOXHUNT_BuildS(char *out, int16_t dbm)
{
    if (dbm >= -93) {
        int16_t over = dbm - (-93);
        if (over > 40) over = 40;
        sprintf(out, "S9+%02d", over);
    } else if (dbm < -141) {
        sprintf(out, "S0");
    } else {
        sprintf(out, "S%d", (dbm + 147) / 6);
    }
}

// Fill a solid rectangle, column by column.
static void FOXHUNT_FillRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool black)
{
    for (int16_t x = x0; x <= x1; x++)
        UI_DrawLineBuffer(gFrameBuffer, x, y0, x, y1, black);
}

static void FOXHUNT_DrawBar(void)
{
    uint8_t nCur = FOXHUNT_FillCount(curDbm);
    uint8_t i;

    // Continuous S-meter staircase (no frame): each segment is 1 px taller than
    // the previous, from S1 (1 px) to S9+40 (13 px), growing up from the base.
    for (i = 0; i < nCur; i++) {
        int16_t sx = FOXHUNT_BAR_X0 + i * FOXHUNT_SEG_PITCH;
        FOXHUNT_FillRect(sx, FOXHUNT_SEG_BOTTOM - i, sx + FOXHUNT_SEG_W - 1, FOXHUNT_SEG_BOTTOM, true);
    }

    // Solid separator line between the staircase and the scale labels below,
    // with a 1 px gap above (y38) and below (y40) it.
    UI_DrawLineBuffer(gFrameBuffer, FOXHUNT_BAR_X0, FOXHUNT_SEG_BOTTOM + 2,
                      FOXHUNT_BAR_X0 + (FOXHUNT_SEG_COUNT - 1) * FOXHUNT_SEG_PITCH + FOXHUNT_SEG_W - 1,
                      FOXHUNT_SEG_BOTTOM + 2, true);
}

// Push one EMA-smoothed reading into the signal history, decimated so the visible
// window spans several seconds. Called once per hunt tick.
static void FOXHUNT_HistSample(void)
{
    // First-order IIR (EMA) on the raw dBm, in x8 fixed point so the /4 step does
    // not stall a few dB short of the input. Tau ~4 ticks (~200 ms): kills the RSSI
    // snow while staying responsive.
    histEma += ((int16_t)curDbm * 8 - histEma) / 4;

    if (++histTick < FOXHUNT_HIST_DECIM)
        return;
    histTick = 0;

    histBuf[histHead] = FOXHUNT_FillCount((int16_t)(histEma / 8));
    histHead = (uint8_t)((histHead + 1) % FOXHUNT_HIST_LEN);
}

// Draw the full-width signal-history graph, spectrum-style: a solid crest contour
// over a checkerboard-filled body, on the 13-level S scale. Oldest sample on the
// left, newest ("now") on the right. Replaces the staircase when the 2 key selects
// it. The level-0 line sits FOXHUNT_GRAPH_FLOOR px above the baseline so the lowest
// reading never touches the axis.
static void FOXHUNT_DrawHist(void)
{
    const uint8_t floorY = FOXHUNT_GRAPH_BOT - FOXHUNT_GRAPH_FLOOR;   // level-0 line
    const uint8_t span   = floorY - FOXHUNT_GRAPH_TOP;                // px for 13 levels

    // Baseline (axis) across the full width, below the level-0 line.
    UI_DrawLineBuffer(gFrameBuffer, FOXHUNT_GRAPH_X0, FOXHUNT_GRAPH_BOT,
                      FOXHUNT_GRAPH_X0 + FOXHUNT_HIST_LEN - 1, FOXHUNT_GRAPH_BOT, true);

    uint8_t prevY = 0;
    uint8_t idx = histHead;   // ring index, wrapped by increment (no per-column modulo)
    for (uint8_t c = 0; c < FOXHUNT_HIST_LEN; c++) {
        uint8_t lvl = histBuf[idx];   // oldest -> newest
        if (++idx >= FOXHUNT_HIST_LEN)
            idx = 0;
        if (lvl > FOXHUNT_SEG_COUNT)
            lvl = FOXHUNT_SEG_COUNT;
        uint8_t x = FOXHUNT_GRAPH_X0 + c;
        uint8_t y = (uint8_t)(floorY - (lvl * span) / FOXHUNT_SEG_COUNT);   // crest top

        // Checkerboard body from just under the crest down to the level-0 line
        // (same pattern as the spectrum: ((x + y) & 1) == 0).
        for (uint8_t yy = y + 1; yy <= floorY; yy++)
            if (((x + yy) & 1) == 0)
                UI_DrawLineBuffer(gFrameBuffer, x, yy, x, yy, true);

        // Solid crest, bridged to the previous sample so it reads as one contour.
        if (c == 0) {
            UI_DrawLineBuffer(gFrameBuffer, x, y, x, y, true);
        } else {
            uint8_t ylo = (y < prevY) ? y : prevY;
            uint8_t yhi = (y < prevY) ? prevY : y;
            UI_DrawLineBuffer(gFrameBuffer, x, ylo, x, yhi, true);
        }
        prevY = y;
    }
}

// Push the status line and the full frame buffer to the LCD, then mirror to K5Viewer.
// Factored out because the same argument-less sequence runs after every full redraw
// (hunt loop, beacon idle, beacon burst, TX-denied notice).
static void FOXHUNT_BlitScreen(void)
{
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    K5VIEWER_Update(false);
#endif
}

// Draw a left-anchored inverse tag in the smallest font (the PK / MN / ATT / beacon
// setting style). The box spans the text width, so the caller only gives x and the line.
static void FOXHUNT_Tag(const char *s, uint8_t x, uint8_t line)
{
    GUI_DisplaySmallestInverse(s, x, line, false, true, (uint8_t)(x + strlen(s) * 4));
}

// Mirror the firmware's status-bar indicator at the same column as the main screens
// (status.c), so it reads identically: the padlock when the keypad is locked, else the
// F glyph when the reverse-step arm is set. Lock takes priority, exactly as on status.c.
static void FOXHUNT_DrawFKey(void)
{
    if (foxLocked)
        memcpy(gStatusLine + 70, gFontKeyLock, sizeof(gFontKeyLock));
    else if (gWasFKeyPressed)
        memcpy(gStatusLine + 70, gFontF, sizeof(gFontF));
}

// Draw a string right-aligned in the small (7 px) font: its right edge lands at rightX.
static void FOXHUNT_DrawRightSmall(const char *s, uint8_t rightX, uint8_t line)
{
    UI_PrintStringSmallNormal(s, (uint8_t)(rightX - strlen(s) * 7), 0, line);
}

// Right-aligned frequency on the bottom line (line 6), shared by the hunt and
// beacon screens.
static void FOXHUNT_DrawFreqBR(uint32_t freq)
{
    sprintf(str, "%u.%05u", freq / 100000, freq % 100000);
    FOXHUNT_DrawRightSmall(str, 126, 6);
}

// Common beacon-screen frame: clear, the BEACON tag, the battery, and the TX
// frequency. Shared by the beacon draw and the TX-denied notice.
static void FOXHUNT_BeaconChrome(void)
{
    UI_DisplayClear();
    UI_StatusClear();
    GUI_DisplaySmallestInverse("BEACON", 2, 0, true, true, 26);
    UI_DrawStatusBattery(gStatusLine, str);
    FOXHUNT_DrawFKey();
    FOXHUNT_DrawFreqBR(gTxVfo->pTX->Frequency);
}

static void FOXHUNT_Draw(void)
{
    const uint8_t *trendIcon;
    char sMeter[8];
    char big[8];

    UI_DisplayClear();
    UI_StatusClear();

    // Title as an inverse label, in the scan-list tag style.
    GUI_DisplaySmallestInverse("FOX HUNT", 2, 0, true, true, 34);

    // Battery (icon + optional percentage/voltage) top-right, as on the main screens.
    UI_DrawStatusBattery(gStatusLine, str);
    FOXHUNT_DrawFKey();

    // Gauge-mode icon (2 key), between the label and the audio icon: ascending
    // bars for the S-meter staircase, a sine wave for the signal history. Each
    // bitmap has its own width, so copy its own size.
    if (foxGraphMode == FOXHUNT_GRAPH_HIST)
        memcpy(gStatusLine + 38, BITMAP_FoxHuntGraph, sizeof(BITMAP_FoxHuntGraph));
    else
        memcpy(gStatusLine + 38, BITMAP_FoxHuntBars, sizeof(BITMAP_FoxHuntBars));

    // Audio state icon between the label and the battery: speaker for beep,
    // headphones for station audio, nothing when silent.
    if (foxAudioMode == FOXHUNT_AUDIO_BEEP)
        memcpy(gStatusLine + 55, BITMAP_FoxHuntSignal, sizeof(BITMAP_FoxHuntSignal));
    else if (foxAudioMode == FOXHUNT_AUDIO_STATION)
        memcpy(gStatusLine + 55, BITMAP_FoxHuntSpeaker, sizeof(BITMAP_FoxHuntSpeaker));

    // Hero level, floating: the big number renders naturally (left-anchored) and
    // the "dBm" unit follows right after it, so the block flows with the width.
    sprintf(big, "%d", curDbm);
    UI_DisplayFrequency(big, 2, 0, false);
    UI_PrintStringSmallNormal("dBm", (uint8_t)(strlen(big) * 13 + 4), 0, 1);

    // Trend as an arrow on line 0 (up = nearer, down = farther, = stable),
    // right-anchored; the signed delta since ~1 s ago sits on line 1.
    trendIcon = (trendDelta > 0) ? BITMAP_FoxHuntUp
              : (trendDelta < 0) ? BITMAP_FoxHuntDown
              : BITMAP_FoxHuntFlat;
    memcpy(gFrameBuffer[0] + (126 - 11), trendIcon, 11);
    // Signed delta on line 1 — only when it actually moved (no "00 dBm" when
    // stable). "dBm" right-anchored to x126, the value 2 px to its left (a real
    // space would be 7 px), so they are drawn as two separate strings.
    if (trendDelta != 0) {
        sprintf(str, "%+03d", trendDelta);   // sign + 2 digits, e.g. "+05" / "-12"
        UI_PrintStringSmallNormal("dBm", 127 - 3 * 7, 0, 1);
        FOXHUNT_DrawRightSmall(str, 127 - 3 * 7 - 2, 1);   // value left of "dBm"
    }

    // Context line: peak hold (left), min hold (centre) and S-meter (right) as small
    // inverse labels (scan-list tag style, 4 px/char). PK and MN bracket a body-scan
    // rotation — the deeper MN sits below PK, the sharper the null.
    sprintf(str, "PK %d", peakDbm);
    FOXHUNT_Tag(str, 4, 2);
    sprintf(str, "MN %d", minDbm);
    FOXHUNT_Tag(str, (uint8_t)((LCD_WIDTH - strlen(str) * 4) / 2), 2);
    FOXHUNT_BuildS(sMeter, curDbm);
    GUI_DisplaySmallestInverse(sMeter, 126 - strlen(sMeter) * 4, 2, false, true, 125);

    // Main gauge, toggled by the 2 key: the S-meter staircase, or a full-width
    // scrolling history of the level over the last seconds (same 13-level S scale).
    if (foxGraphMode == FOXHUNT_GRAPH_HIST) {
        FOXHUNT_DrawHist();
    } else {
        // Segmented S-meter staircase.
        FOXHUNT_DrawBar();

        // Scale under the gauge, aligned with segments 1 / 5 / 9 / 13.
        GUI_DisplaySmallest("S1",    6, 41, false, true);
        GUI_DisplaySmallest("S5",   42, 41, false, true);
        GUI_DisplaySmallest("S9",   78, 41, false, true);
        GUI_DisplaySmallest("+40", 110, 41, false, true);
    }

    // Front-end ladder as an inverse label (left), tuned frequency (right). The PGA
    // steps show "ATT xxdB"; the two front-end steps show "BYP" / "BYP+".
    if (attStep < FOXHUNT_ATT_BYP0)
        sprintf(str, "ATT %ddB", FOXHUNT_ATT_DB[attStep]);
    else
        strcpy(str, (attStep == FOXHUNT_ATT_BYP0) ? "BYP" : "BYP+");
    FOXHUNT_Tag(str, 4, 6);
    FOXHUNT_DrawFreqBR(gRxVfo->pRX->Frequency);
}

// One key = one setting, each press cycling to the next value and wrapping — the same
// scheme for the hunt gauges and the beacon settings, so there are no navigation arrows
// to learn. dir is +1 for a plain press, -1 when the F key armed a reverse step. The
// beacon values are read where they take effect (TX window at the burst start, idle at
// the burst end, fox at the next repeat), so cycling any of them is safe at any time.

// Wrap a 0..count-1 index one step forward (dir > 0) or backward, both ways round.
static __attribute__((noinline)) uint8_t FOXHUNT_WrapStep(uint8_t v, uint8_t count, int8_t dir)
{
    return (uint8_t)((v + (dir > 0 ? 1u : (unsigned)(count - 1u))) % count);
}

// Step a lo..hi value by step, wrapping lo<->hi at the ends.
static uint8_t FOXHUNT_RangeStep(uint8_t v, uint8_t lo, uint8_t hi, uint8_t step, int8_t dir)
{
    if (dir > 0)
        return (v >= hi) ? lo : (uint8_t)(v + step);
    return (v <= lo) ? hi : (uint8_t)(v - step);
}

// Rebase the level-tracking state (peak/min hold, trend reference and the signal
// history) onto a fresh reading. Used at hunt entry and after any front-end gain
// change: the shown dBm is not gain-compensated, so without this a step change would
// leave a stale peak, an artificial min, a one-shot false trend spike and a cliff in
// the sparkline.
static void FOXHUNT_RebaseMeasurements(void)
{
    curDbm     = FOXHUNT_ReadDbm();
    peakDbm    = curDbm;
    minDbm     = curDbm;
    trendRef   = curDbm;
    trendDelta = 0;
    trendTick  = 0;

    // Prime the signal history flat at the current level so the sparkline scrolls in
    // from a sensible baseline instead of showing the gain step as a cliff.
    {
        uint8_t lvl = FOXHUNT_FillCount(curDbm);
        for (uint8_t i = 0; i < FOXHUNT_HIST_LEN; i++)
            histBuf[i] = lvl;
    }
    histHead = 0;
    histTick = 0;
    histEma  = (int16_t)(curDbm * 8);
}

// Front-end ladder: ATT 0 -> -6 -> -15 -> -27 dB, then BYP -> BYP+ (F reverses).
static void FOXHUNT_AttCycle(int8_t dir)
{
    attStep = FOXHUNT_WrapStep(attStep, FOXHUNT_ATT_COUNT, dir);
    FOXHUNT_ApplyAtt();

    // Wait for the RSSI to settle to the new gain, THEN rebase the holds/trend/history.
    // Reading straight away would latch the old-gain level, and as the RSSI converges
    // over the next ticks the very artifacts the rebase avoids (stuck peak, false trend
    // spike, sparkline cliff) would reappear.
    SYSTEM_DelayMs(FOXHUNT_ATT_SETTLE_MS);
    FOXHUNT_RebaseMeasurements();
}

// Convert the two navigation keys into the same semantic value direction used
// by the resident menus and by the overlay FoxHunt app:
//   UV-K5 UP/DOWN    -> +1/-1
//   UV-K1 LEFT/RIGHT -> -1/+1
static int8_t FOXHUNT_NavDirection(KEY_Code_t key)
{
    int8_t direction = (key == KEY_UP) ? 1 : -1;
    return gEeprom.SET_NAV ? direction : -direction;
}

// Fox identifier: MOE -> MOI -> MOS -> MOH -> MO5 -> MO -> CALL (F reverses).
static void FOXHUNT_FoxCycle(int8_t dir)
{
    foxFox = FOXHUNT_WrapStep(foxFox, FOXHUNT_FOX_COUNT, dir);
}

// TX window: 5..60 s (F reverses).
static void FOXHUNT_TxCycle(int8_t dir)
{
    beaconTx = FOXHUNT_RangeStep(beaconTx, FOXHUNT_BEACON_TX_MIN, FOXHUNT_BEACON_TX_MAX,
                                 FOXHUNT_BEACON_TX_STEP, dir);
}

// Idle gap: 5..240 s (F reverses).
static void FOXHUNT_IdleCycle(int8_t dir)
{
    beaconIdle = FOXHUNT_RangeStep(beaconIdle, FOXHUNT_BEACON_IDLE_MIN, FOXHUNT_BEACON_IDLE_MAX,
                                   FOXHUNT_BEACON_IDLE_STEP, dir);
}

// Apply a beacon number key in the given direction; returns true when it changed a
// setting so the caller can refresh. Keys: 1 = TX, 2 = IDLE, 3 = FOX, 4 = keying mode
// (TONE/CARR, a plain toggle so dir is moot). Shared by both beacon phases.
static bool FOXHUNT_BeaconKey(KEY_Code_t key, int8_t dir)
{
    switch (key) {
        case KEY_1: FOXHUNT_TxCycle(dir);   return true;
        case KEY_2: FOXHUNT_IdleCycle(dir); return true;
        case KEY_3: FOXHUNT_FoxCycle(dir);  return true;
        case KEY_4: beaconCarrierKeyed = !beaconCarrierKeyed; return true;
        default:    return false;
    }
}

// Sleep one UI tick, but in 10 ms slices stepping the backlight fade each slice,
// so the fade rate matches the main screen (BACKLIGHT_Update runs every 10 ms in
// APP_TimeSlice10ms; delaying the whole 50 ms tick at once made it ~5x slower).
static void FOXHUNT_TickDelay(void)
{
    for (uint8_t i = 0; i < FOXHUNT_TICK_MS / 10; i++) {
        SYSTEM_DelayMs(10);
        BACKLIGHT_Update();
    }
}

// Idle housekeeping the foreground scheduler normally does but this modal loop
// bypasses: the periodic battery sample, the config save and the backlight
// timeout (BLTime). Cadenced on the 500 ms system tick.
//
// The SetOff auto power-off is deliberately NOT run here. Fox Hunt is an active,
// attended mode that receives a signal continuously; the main loop never sleeps
// while receiving (it re-arms gSleepModeCountdown_500ms on RX), so the old
// hand-back-to-main scheme just ejected the user to the VFO instead of powering
// off. Both hunt and beacon therefore ignore SetOff and run until EXIT.
static void FOXHUNT_IdleHousekeeping(void)
{
    if (!gNextTimeslice_500ms)
        return;
    gNextTimeslice_500ms = false;

    // This modal loop bypasses the foreground scheduler that normally samples the
    // battery, which would otherwise freeze gBatteryDisplayLevel for the whole
    // session. Refresh it here — every caller of this function has the PA off (the
    // hunt loop, and the beacon idle gap, never a burst), so the reading is not
    // pulled down by TX load — keeping the status icon live and letting the
    // beacon's battery gate react to a pack draining under a long run.
    BATTERY_Sample(false);

    // Persist any changed setting within ~0.5 s, so it survives a power-off (not
    // just a clean EXIT). No-op when nothing changed.
    FOXHUNT_SaveConfig();

    // Backlight timeout: at BLTime expiry drop from BLMax to BLMin (both modes).
    if (gBacklightCountdown_500ms > 0
        && gEeprom.BACKLIGHT_TIME < 61
        && --gBacklightCountdown_500ms == 0)
        BACKLIGHT_TurnOff();
}

// Track a held F key and toggle the keypad lock once it crosses the long-press threshold.
// Fed the time elapsed since the last call (a hunt/idle tick, or a TX slice), so it fires
// after the same ~0.5 s hold in every loop and the pad can be (un)locked at any moment. A
// non-F key resets the accumulator; the toggle fires once per physical hold. Returns true
// exactly on the toggle so a caller that does not redraw every tick (the TX loop) can
// refresh the padlock immediately.
static bool FOXHUNT_LockHoldTrack(KEY_Code_t key, uint16_t elapsedMs)
{
    if (key != KEY_F) {
        fHoldMs   = 0;
        fLongDone = false;
        return false;
    }
    if (fLongDone)                          // already toggled once for this hold
        return false;
    fHoldMs += elapsedMs;
    if (fHoldMs < FOXHUNT_LOCK_HOLD_MS)
        return false;

    fLongDone       = true;
    foxLocked       = !foxLocked;
    gWasFKeyPressed = false;                // a lock toggle is not a reverse-step arm
    BACKLIGHT_TurnOn();
    return true;
}

static void FOXHUNT_HandleKeys(void)
{
    kbd.prev    = kbd.current;
    kbd.current = KEYBOARD_GetKey();

    // Long-press F toggles the keypad lock; a short F press falls through to the reverse-
    // step arm on its rising edge below. One hunt tick has elapsed since the last call.
    FOXHUNT_LockHoldTrack(kbd.current, FOXHUNT_TICK_MS);

    // Act only on the rising edge of a new key.
    if (kbd.current == KEY_INVALID || kbd.current == kbd.prev)
        return;

    BACKLIGHT_TurnOn();   // any keypress wakes the screen and re-arms the timers

    // While locked, swallow everything except the attenuation arrows — the lock is
    // released only by the long-press F handled above. Keeping ATT reachable is the whole
    // point: on the final approach the sensitivity still has to be pulled down by hand.
    if (foxLocked) {
        if (kbd.current == KEY_UP || kbd.current == KEY_DOWN)
            FOXHUNT_AttCycle(FOXHUNT_NavDirection(kbd.current));
        return;
    }

    if (kbd.current == KEY_F) {   // arm / disarm a reverse step for the next number key
        gWasFKeyPressed = !gWasFKeyPressed;
        return;
    }
    const int8_t dir = gWasFKeyPressed ? -1 : 1;

    switch (kbd.current) {
        case KEY_EXIT:
            foxRunning = false;
            break;
        case KEY_1:
            // Toggle the main gauge: S-meter staircase <-> full-width history.
            foxGraphMode ^= 1;
            break;
        case KEY_2:
            // Cycle the audio: silent -> Geiger beep -> station audio (F reverses).
            foxAudioMode = FOXHUNT_WrapStep(foxAudioMode, 3, dir);
            FOXHUNT_SetAudio();
            if (foxAudioMode == FOXHUNT_AUDIO_BEEP)
                audioTick = FOXHUNT_RATE_SLOW_TICKS;   // blip promptly
            break;
        case KEY_3:
            // Cycle the front-end ladder (ATT 0/-6/-15/-27 dB, then BYP/BYP+; F reverses).
            FOXHUNT_AttCycle(dir);
            break;
        case KEY_UP:
        case KEY_DOWN:
            // Follow SetNav: UP/DOWN on K5, LEFT/RIGHT on K1.
            FOXHUNT_AttCycle(FOXHUNT_NavDirection(kbd.current));
            break;
        case KEY_MENU:
            // Reset the peak / min hold and the trend reference (before each body scan).
            peakDbm  = curDbm;
            minDbm   = curDbm;
            trendRef = curDbm;
            break;
        default:
            break;
    }

    gWasFKeyPressed = false;   // any non-F key consumes (or cancels) the reverse arm
}

// Enter Fox Hunt with fixed front-end gain, the persisted attenuator/audio mode,
// and freshly rebased level tracking.
static void FOXHUNT_EnterHunt(void)
{
    BK4819_SetAGC(false);
    FOXHUNT_ApplyAtt();
    FOXHUNT_SetAudio();          // (re)apply the current audio mode: off / beep / station

    // Let the RSSI settle to the gain just applied, then reset the peak/min hold, trend
    // reference and signal history onto a fresh reading. Same settle as
    // FOXHUNT_AttCycle: without it a restored
    // BYP/BYP+ step at start-up would rebase onto the old-gain reading.
    // SetAudio already waits when audio is on, but not in the default audio-off case.
    SYSTEM_DelayMs(FOXHUNT_ATT_SETTLE_MS);
    FOXHUNT_RebaseMeasurements();
    audioTick  = (foxAudioMode == FOXHUNT_AUDIO_BEEP) ? FOXHUNT_RATE_SLOW_TICKS : 0;
}

// Evaluate the TX gates for the beacon's TX VFO in the same precedence order as
// RADIO_PrepareTX (F LOCK / per-VFO TX LOCK, then battery, then modulation) and
// return the matching VfoState_t. VFO_STATE_NORMAL means the burst is clear to
// transmit. Reusing the radio's own states keeps the on-screen wording identical
// to the main display (VfoStateStr).
static VfoState_t FOXHUNT_TxState(void)
{
    if (TX_freq_check(gTxVfo->pTX->Frequency) != 0 && gTxVfo->TX_LOCK)
        return VFO_STATE_TX_DISABLE;
    if (gBatteryDisplayLevel == 0)
        return VFO_STATE_BAT_LOW;
    if (gBatteryDisplayLevel > 6)
        return VFO_STATE_VOLTAGE_HIGH;
    if (gTxVfo->Modulation != MODULATION_FM)
        return VFO_STATE_TX_DISABLE;
    return VFO_STATE_NORMAL;
}

// Refuse feedback shown when a burst is barred: reuse the beacon screen layout and
// the radio's own state label (VfoStateStr, e.g. "TX DISABLE" / "BAT LOW" / "VOLT
// HIGH"), same font as the main screen, for a beat; the caller then waits through
// a fresh idle gap before checking again.
static void FOXHUNT_TxDeniedNotice(VfoState_t state)
{
    // Frame (clear + BEACON tag + battery + barred TX frequency).
    FOXHUNT_BeaconChrome();

    // Centred, gFontBig — same wording and font as the main screen's VFO state,
    // on line 1 so it sits exactly where the beacon's "TX" / "IDLE" text appears.
    UI_PrintString(VfoStateStr[state], 0, 127, 1, 8);

    FOXHUNT_BlitScreen();

    // Hold the notice ~1 s while keeping the backlight fade alive.
    for (uint8_t i = 0; i < 20; i++)
        FOXHUNT_TickDelay();
}

// Enter the Beacon app. TX gates are enforced before each burst.
static void FOXHUNT_EnterBeacon(void)
{
    AUDIO_AudioPathOff();
    gCurrentVfo   = gTxVfo;      // the VFO RADIO_SetTxParameters keys up
    beaconPhaseTx = true;
}

// Paint the TX line (line 0): "TX" left, remaining-window seconds right. Kept separate
// so FOXHUNT_TxDelay can refresh just this line each second, live, without redrawing the
// whole screen mid-Morse. Records the value shown so the caller only repaints on change.
static void FOXHUNT_DrawTxSeconds(void)
{
    const uint8_t sec = (uint8_t)((beaconTxMsLeft + 999u) / 1000u);
    memset(gFrameBuffer[0], 0, sizeof(gFrameBuffer[0]));
    UI_PrintStringSmallNormal("TX", 1, 0, 0);
    sprintf(str, "%02us", sec);
    FOXHUNT_DrawRightSmall(str, 127, 0);
    beaconTxSecShown = sec;
}

// Sleep in short slices while watching the keypad, so a burst stays interactive and the
// TX-window countdown (beaconTxMsLeft) drains in real time. EXIT leaves Beacon. The
// 1/2/3 keys edit the beacon
// settings live, exactly as in the idle phase: the TX window (1) and idle gap (2) take
// effect at the upcoming window / gap, and a FOX change (3) at the next repeat (the
// message is rebuilt there, never under the running loop).
static bool FOXHUNT_TxDelay(uint16_t ms)
{
    while (ms) {
        const uint16_t slice = (ms > 10) ? 10 : ms;   // 10 ms: same fade rate as idle
        SYSTEM_DelayMs(slice);
        ms -= slice;
        beaconTxMsLeft = (beaconTxMsLeft > slice) ? (beaconTxMsLeft - slice) : 0;
        BACKLIGHT_Update();                 // keep any brightness fade moving in TX

        // Tick the remaining-window seconds live: repaint only line 0 when the whole
        // second changes, so the countdown drains smoothly through a long ID instead of
        // jumping once per repeat (a one-line blit barely dents the Morse timing).
        if ((uint8_t)((beaconTxMsLeft + 999u) / 1000u) != beaconTxSecShown) {
            FOXHUNT_DrawTxSeconds();
            ST7565_BlitLine(0);
        }

        kbd.prev    = kbd.current;
        kbd.current = KEYBOARD_GetKey();

        // Track a held F across slices (fed the real slice time, so the ~0.5 s threshold
        // matches hunt/idle). This runs before the rising-edge test below — a held key
        // reads the same every slice and would otherwise be skipped — so the pad can be
        // locked or unlocked mid-burst. On a toggle, repaint at once so the padlock
        // appears without waiting for the next ID repaint.
        if (FOXHUNT_LockHoldTrack(kbd.current, slice)) {
            FOXHUNT_BeaconDraw(true, 0);
            FOXHUNT_BlitScreen();
        }

        if (kbd.current == KEY_INVALID || kbd.current == kbd.prev)
            continue;                       // rising edge only

        BACKLIGHT_TurnOn();                 // keep the screen awake during TX

        // Locked: ignore every key for the rest of the burst; the long-press F unlock is
        // handled above, before this gate, so the reverse-arm F and the setting keys stay
        // inert until the pad is unlocked.
        if (foxLocked)
            continue;

        if (kbd.current == KEY_EXIT) {
            foxRunning = false;             // abort the burst and leave Beacon
            return true;
        }
        if (kbd.current == KEY_MENU) {
            // Cut the transmission short but stay in Beacon: the loop drops straight
            // into a fresh idle gap of the configured IDLE length.
            return true;
        }
        if (kbd.current == KEY_F) {         // arm / disarm the reverse step
            gWasFKeyPressed = !gWasFKeyPressed;
            FOXHUNT_BeaconDraw(true, 0);    // reflect the F indicator in the status line
            FOXHUNT_BlitScreen();
            continue;
        }
        if (FOXHUNT_BeaconKey(kbd.current, gWasFKeyPressed ? -1 : 1)) {
            gWasFKeyPressed = false;        // consumed; also clears the F indicator
            FOXHUNT_BeaconDraw(true, 0);    // show the new value immediately
            FOXHUNT_BlitScreen();
        }
    }
    return false;
}

// Morse pattern of one character (letters, digits, '/'); 0 = space / unsupported.
static uint8_t FOXHUNT_MorseByte(char c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') return FOXHUNT_MORSE_LETTER[c - 'A'];
    if (c >= '0' && c <= '9') return FOXHUNT_MORSE_DIGIT[c - '0'];
    if (c == '/')             return 0x32;      // -..-.
    return 0;
}

// Beacon keying primitive. Both modes key the tone (Enter/ExitTxMute); CARR additionally
// gates the PA in lockstep, so between elements the carrier itself is gone — not just the
// tone. Muting the tone in CARR too is deliberate: gating PA_ENABLE does not perfectly
// kill the carrier, and a tone left riding would bleed through that residual carrier as a
// near-continuous note. The PLL stays locked, so the carrier gate is only a GPIO toggle.
static void FOXHUNT_KeyOn(void)
{
    BK4819_ExitTxMute();
    // Assert the carrier in both modes: a no-op in TONE (already up), but it re-arms the PA
    // if the mode was switched from CARR mid-window while the carrier happened to be down.
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, true);
}
static void FOXHUNT_KeyOff(void)
{
    BK4819_EnterTxMute();
    if (beaconCarrierKeyed)
        BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
}

// Send one character as Morse: key each element on (FOXHUNT_KeyOn) for its dit/dah
// duration, off (FOXHUNT_KeyOff) in the gaps. What "key" means depends on the beacon
// mode (tone vs carrier), see above. Returns true if aborted.
static bool FOXHUNT_MorseChar(char c, uint8_t visibleChars)
{
    uint8_t code = FOXHUNT_MorseByte(c);

    if (code == 0) {                            // space / unknown -> word gap
        if (FOXHUNT_TxDelay(FOXHUNT_MORSE_UNIT_MS * 4))
            return true;
        FOXHUNT_UpdateBeaconProgress(visibleChars);
        return false;
    }

    uint8_t bit = 0x80;
    while (!(code & bit)) bit >>= 1;            // skip to the sentinel bit
    for (bit >>= 1; bit; bit >>= 1) {           // then walk the elements, MSB first
        const uint16_t on = (code & bit) ? (FOXHUNT_MORSE_UNIT_MS * 3)   // dah
                                         :  FOXHUNT_MORSE_UNIT_MS;        // dit
        FOXHUNT_KeyOn();
        if (FOXHUNT_TxDelay(on)) { FOXHUNT_KeyOff(); return true; }
        FOXHUNT_KeyOff();
        if (FOXHUNT_TxDelay(FOXHUNT_MORSE_UNIT_MS)) return true;         // intra gap
    }

    // Reveal the character once all its CW elements have been transmitted. The
    // remaining two units keep the completed character visible during the rest
    // of the standard three-unit inter-character gap.
    FOXHUNT_UpdateBeaconProgress(visibleChars);

    // Inter-character gap is 3 units; one already elapsed after the last element.
    return FOXHUNT_TxDelay(FOXHUNT_MORSE_UNIT_MS * 2);
}

// One beacon cycle: repeat the CW identification for the whole TX window, at the TX
// VFO's configured power. Carrier stays up across the window; the tone is keyed per
// Morse element (MCW), and the ID is re-sent until the window drains — a real ARDF fox
// keys up for a fixed slot rather than sending a single one-shot.
static void FOXHUNT_BeaconTransmit(void)
{
    RADIO_SetTxParameters();                             // key up: carrier + PA + PLL lock

    // Prime the tone generator, then open the window silent before the first element:
    // tone muted in both modes, and in CARR the carrier dropped too (PLL still locked).
    BK4819_TransmitTone(false, FOXHUNT_BEACON_TONE_HZ);
    BK4819_EnterTxMute();
    if (beaconCarrierKeyed)
        BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    beaconTxMsLeft = (uint16_t)beaconTx * 1000u;         // drained inside FOXHUNT_TxDelay

    bool stop = false;
    while (!stop && beaconTxMsLeft > 0) {
        // Rebuild from the current fox at the repeat boundary — this is where a FOX
        // change made live (during the previous repeat or the idle gap) takes effect,
        // never mid-message.
        FOXHUNT_BuildBeaconMsg();

        // Redraw once per repeat (remaining-window countdown + reset progress), in the
        // muted gap so it never stretches a Morse element. Local LCD only here — the
        // K5Viewer frame is pushed after the ID (below), when the text is complete.
        beaconCharsSent = 0;
        FOXHUNT_BeaconDraw(true, 0);
        ST7565_BlitStatusLine();
        ST7565_BlitFullScreen();

        for (uint8_t i = 0; !stop && beaconMsg[i]; i++) {
            stop = FOXHUNT_MorseChar(beaconMsg[i], i + 1u);

#if defined(ENABLE_UART) || defined(ENABLE_USB)
            // Service maintenance commands in the muted inter-character gap.
            UART_ServiceCommands();
#endif
        }

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
        // Service the K5Viewer link in the muted gap (never during a keyed element, so
        // the Morse timing stays intact): ParseInput refills the keep-alive that Update
        // drains — without it the link dies within ~15 repeats over a long TX window —
        // and Update then mirrors the now-complete ID frame plus the live seconds.
        if (!stop) {
            K5VIEWER_ParseInput();
            K5VIEWER_Update(false);
        }
#endif

        // Word gap before the next repeat, only while the window still has room.
        if (!stop && beaconTxMsLeft > 0)
            stop = FOXHUNT_TxDelay(FOXHUNT_MORSE_UNIT_MS * 7);
    }

    // Key down: mute, drop the PA and restore RX for the silent gap. If EXIT
    // interrupted the burst, FOXHUNT_TxDelay has already stopped the app.
    BK4819_EnterTxMute();
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    RADIO_SetupRegisters(true);
}

// Build the CW message from the current fox id: "<call> MOE" for CALL (falls back to a
// bare MOE when no callsign is set), otherwise the pure "MOx" of the chosen fox.
static void FOXHUNT_BuildBeaconMsg(void)
{
    if (foxFox == FOXHUNT_FOX_CALL) {
        if (foxCall[0] != '\0')
            sprintf(beaconMsg, "%s MOE", foxCall);
        else
            strcpy(beaconMsg, "MOE");
    } else if (foxFox == FOXHUNT_FOX_MO) {
        strcpy(beaconMsg, "MO");                // ARDF finish/home beacon
    } else {
        sprintf(beaconMsg, "MO%c", FOXHUNT_FOX_TAIL[foxFox]);
    }
}

// Short label for the FOX setting: "FOX CALL" or "FOX MOx".
static void FOXHUNT_FoxLabel(char *out)
{
    if (foxFox == FOXHUNT_FOX_CALL)
        strcpy(out, "FOX CALL");
    else if (foxFox == FOXHUNT_FOX_MO)
        strcpy(out, "FOX MO");
    else
        sprintf(out, "FOX MO%c", FOXHUNT_FOX_TAIL[foxFox]);
}

// Draw the transmitted portion of the message. A CALL id splits on the space: callsign
// on the upper line, "MOx" below. A pure fox (no space) is centred on one line. Each
// revealed prefix keeps the anchor of the full string, so characters appear in place
// instead of re-centring as they arrive.
static void FOXHUNT_DrawBeaconMessage(uint8_t visibleChars)
{
    const char *separator = strchr(beaconMsg, ' ');

    if (separator == NULL) {                             // pure fox id (MOE..MO5)
        const size_t idLen  = strlen(beaconMsg);
        const size_t visLen = MIN((size_t)visibleChars, idLen);
        char id[sizeof(beaconMsg)];
        memcpy(id, beaconMsg, visLen);
        id[visLen] = '\0';
        UI_PrintString(id, (uint8_t)((LCD_WIDTH - idLen * 8u) / 2u), 0,
                       FOXHUNT_MSG_LINE_ONE, 8);
        return;
    }

    const size_t callLen = (size_t)(separator - beaconMsg);
    const size_t visibleCallLen = MIN((size_t)visibleChars, callLen);
    char call[FOXHUNT_CALLSIGN_MAX + 1];
    memcpy(call, beaconMsg, visibleCallLen);
    call[visibleCallLen] = '\0';
    UI_PrintString(call, (uint8_t)((LCD_WIDTH - callLen * 8u) / 2u), 0,
                   FOXHUNT_MSG_LINE_CALL, 8);

    if ((size_t)visibleChars > callLen + 1u) {
        const char *id = separator + 1;
        const size_t idLen = strlen(id);
        const size_t visibleIdLen = MIN((size_t)visibleChars - callLen - 1u, idLen);
        char idbuf[8];
        memcpy(idbuf, id, visibleIdLen);
        idbuf[visibleIdLen] = '\0';
        UI_PrintString(idbuf, (uint8_t)((LCD_WIDTH - idLen * 8u) / 2u), 0,
                       FOXHUNT_MSG_LINE_ID, 8);
    }
}

// Update only the line pair whose latest character changed so screen refreshes do not
// lengthen the Morse timing more than necessary.
static void FOXHUNT_UpdateBeaconProgress(uint8_t visibleChars)
{
    const char *separator = strchr(beaconMsg, ' ');
    uint8_t topLine;

    beaconCharsSent = visibleChars;

    if (separator == NULL) {                             // pure fox id
        topLine = FOXHUNT_MSG_LINE_ONE;
    } else {
        const size_t callLen = (size_t)(separator - beaconMsg);
        if ((size_t)visibleChars == callLen + 1u)        // the space: nothing new to show
            return;
        topLine = ((size_t)visibleChars <= callLen)
                    ? FOXHUNT_MSG_LINE_CALL
                    : FOXHUNT_MSG_LINE_ID;
    }

    memset(gFrameBuffer[topLine], 0, sizeof(gFrameBuffer[topLine]));
    memset(gFrameBuffer[topLine + 1], 0, sizeof(gFrameBuffer[topLine + 1]));
    FOXHUNT_DrawBeaconMessage(beaconCharsSent);
    ST7565_BlitLine(topLine);
    ST7565_BlitLine(topLine + 1);
}

static void FOXHUNT_BeaconDraw(bool txNow, uint8_t idleLeft)
{
    // Frame (clear + BEACON tag + battery + TX frequency on the bottom-right).
    FOXHUNT_BeaconChrome();

    if (txNow) {
        // On-air icon in the status bar (only while the carrier is up), then the top
        // line "TX" + remaining window seconds (refreshed live in FOXHUNT_TxDelay).
        memcpy(gStatusLine + 48, BITMAP_FoxHuntTx, sizeof(BITMAP_FoxHuntTx));
        FOXHUNT_DrawTxSeconds();
        FOXHUNT_DrawBeaconMessage(beaconCharsSent);
    } else {
        // Silence phase: big "IDLE" with the countdown to the next burst below it.
        UI_PrintString("IDLE", 0, 127, 1, 10);
        sprintf(str, "%02us", idleLeft);
        UI_PrintString(str, 0, 127, 3, 10);
    }

    // Settings tags: TX (line 5, left) over IDLE (line 6, left). On line 5, right of TX:
    // FOX shifted left with the keying mode (TONE/CARR) right-aligned beside it, so the pair
    // spans roughly the width of the TX frequency drawn just below on line 6
    // (FOXHUNT_BeaconChrome). Keys: 1 TX, 2 IDLE, 3 FOX, 4 mode; editable in either phase.
    sprintf(str, "TX %us", beaconTx);
    FOXHUNT_Tag(str, 4, 5);
    sprintf(str, "IDLE %us", beaconIdle);
    FOXHUNT_Tag(str, 4, 6);
    FOXHUNT_FoxLabel(str);
    FOXHUNT_Tag(str, 66, 5);

    const char *mode = beaconCarrierKeyed ? "CARR" : "TONE";
    FOXHUNT_Tag(mode, (uint8_t)(125 - strlen(mode) * 4), 5);
}

static void FOXHUNT_BeaconKeys(void)
{
    kbd.prev    = kbd.current;
    kbd.current = KEYBOARD_GetKey();

    // Long-press F toggles the keypad lock (same gesture as the hunt screen). One idle
    // tick has elapsed since the last call.
    FOXHUNT_LockHoldTrack(kbd.current, FOXHUNT_TICK_MS);

    if (kbd.current == KEY_INVALID || kbd.current == kbd.prev)
        return;

    BACKLIGHT_TurnOn();   // any keypress wakes the screen and re-arms the timers

    // Locked: swallow every key (the beacon has no per-key control to keep live, unlike
    // the hunt's attenuation arrows); release with the long-press F handled above. A burst
    // in progress enforces the same lock in FOXHUNT_TxDelay.
    if (foxLocked)
        return;

    if (kbd.current == KEY_F) {   // arm / disarm a reverse step for the next number key
        gWasFKeyPressed = !gWasFKeyPressed;
        return;
    }

    switch (kbd.current) {
        case KEY_EXIT:
            foxRunning = false;
            break;
        case KEY_MENU:
            // Restart a full idle interval now (the TX-phase M drops here too).
            beaconIdleLeft = beaconIdle;
            beaconIdleTick = 0;
            break;
        default:
            // 1 = TX window, 2 = idle gap, 3 = fox (F reverses); same keys as during TX.
            FOXHUNT_BeaconKey(kbd.current, gWasFKeyPressed ? -1 : 1);
            break;
    }

    gWasFKeyPressed = false;   // any non-F key consumes (or cancels) the reverse arm
}

// One iteration of the beacon loop: either transmit a burst, or count down the
// editable silent gap. Kept off the hot RX path so hunt stays lightweight.
static void FOXHUNT_BeaconTick(void)
{
    if (beaconPhaseTx) {
        // Re-assert the TX gates before every burst (RADIO_PrepareTX parity). F
        // LOCK / TX LOCK and modulation are fixed for the session, but the battery
        // is re-sampled over the idle gaps (FOXHUNT_IdleHousekeeping), so a beacon
        // left running stops keying up once the pack falls low or over-voltage
        // instead of transmitting blind. Refuse, notify, then retry after an idle gap.
        VfoState_t state = FOXHUNT_TxState();
        if (state != VFO_STATE_NORMAL) {
            FOXHUNT_TxDeniedNotice(state);
            beaconPhaseTx = false;
            beaconIdleLeft = beaconIdle;
            beaconIdleTick = 0;
            return;
        }

        // The burst repeats the ID for the whole TX window, redrawing the screen
        // itself each repeat (remaining-window countdown); EXIT may clear
        // foxRunning and abort it.
        FOXHUNT_BeaconTransmit();

        beaconPhaseTx  = false;
        beaconIdleLeft = beaconIdle;
        beaconIdleTick = 0;

        return;
    }

    // Idle (silent) phase: responsive, editable countdown.
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    K5VIEWER_ParseInput();
#endif
    FOXHUNT_BeaconKeys();

    if (!foxRunning)
        return;

    FOXHUNT_BeaconDraw(false, beaconIdleLeft);
    FOXHUNT_BlitScreen();

    if (++beaconIdleTick >= (1000 / FOXHUNT_TICK_MS)) { // one second elapsed
        beaconIdleTick = 0;
        if (beaconIdleLeft > 0)
            beaconIdleLeft--;
        if (beaconIdleLeft == 0)
            beaconPhaseTx = true;                        // next tick transmits
    }

    FOXHUNT_IdleHousekeeping();   // battery sample + save + backlight timeout
    FOXHUNT_TickDelay();          // tick delay + smooth backlight fade
}

// --- Persistence (external flash) --------------------------------------------
// Concatenated right after the VFO data in the VFO sector. NB: the 14 VFOs really
// span 0x9000..0x90E0 (224 B); the old 0x90D6 mapping bound was 10 B short and
// overlapped the last VFO (470 MHz VFO1 @ 0x90D0..0x90DF), so the config sits at
// 0x90E0, just past the real end. Normal saves are read-modify-write (Append=false)
// so they preserve these bytes, and the VFO mapping is extended to 0x90E7
// (eeprom_compat.c) so aircopy clones them with the VFOs. A factory reset clears it.
#define FOXHUNT_CFG_ADDR  0x0090E0u
#define FOXHUNT_CFG_MAGIC 0xF4u    // tells a written config from erased flash (0xFF)
#define FOXHUNT_CFG_LEN   8        // magic + att + graph + audio + idle + fox + tx + carrier

// RAM mirror of the bytes last written to flash, so FOXHUNT_SaveConfig only touches
// the flash when a value actually changed.
static uint8_t foxCfgSaved[FOXHUNT_CFG_LEN];

// Pack the live settings into the on-flash layout.
static __attribute__((noinline)) void FOXHUNT_ConfigPack(uint8_t out[FOXHUNT_CFG_LEN])
{
    out[0] = FOXHUNT_CFG_MAGIC;
    out[1] = attStep;
    out[2] = foxGraphMode;
    out[3] = foxAudioMode;
    out[4] = beaconIdle;
    out[5] = foxFox;
    out[6] = beaconTx;
    out[7] = beaconCarrierKeyed;
}

// Restore persisted settings; erased/invalid flash leaves the defaults in place.
static void FOXHUNT_LoadConfig(void)
{
    uint8_t cfg[FOXHUNT_CFG_LEN];
    PY25Q16_ReadBuffer(FOXHUNT_CFG_ADDR, cfg, sizeof(cfg));

    if (cfg[0] == FOXHUNT_CFG_MAGIC) {                    // valid, saved config
        if (cfg[1] < FOXHUNT_ATT_COUNT)      attStep      = cfg[1];
        if (cfg[2] <= FOXHUNT_GRAPH_HIST)    foxGraphMode = cfg[2];
        if (cfg[3] <= FOXHUNT_AUDIO_STATION) foxAudioMode = cfg[3];
        if (cfg[4] >= FOXHUNT_BEACON_IDLE_MIN && cfg[4] <= FOXHUNT_BEACON_IDLE_MAX
            && (cfg[4] % FOXHUNT_BEACON_IDLE_STEP) == 0)
            beaconIdle = cfg[4];
        if (cfg[5] < FOXHUNT_FOX_COUNT)      foxFox       = cfg[5];
        if (cfg[6] >= FOXHUNT_BEACON_TX_MIN && cfg[6] <= FOXHUNT_BEACON_TX_MAX
            && (cfg[6] % FOXHUNT_BEACON_TX_STEP) == 0)
            beaconTx = cfg[6];
        if (cfg[7] <= 1) beaconCarrierKeyed = cfg[7];   // erased (0xFF) legacy config -> keep default
    }

    FOXHUNT_ConfigPack(foxCfgSaved);   // mirror the loaded (or default) state
}

// Write the settings to flash, but only when they changed since the last write.
// Called on the 500 ms tick (so any change persists within ~0.5 s and survives a
// power-off, not just a clean EXIT) and once more on leaving either app. The RAM
// compare avoids re-reading the 4 KB sector on every quiet tick.
static void FOXHUNT_SaveConfig(void)
{
    uint8_t cfg[FOXHUNT_CFG_LEN];
    FOXHUNT_ConfigPack(cfg);

    if (memcmp(cfg, foxCfgSaved, sizeof(cfg)) == 0)
        return;

    PY25Q16_WriteBuffer(FOXHUNT_CFG_ADDR, cfg, sizeof(cfg), false);
    memcpy(foxCfgSaved, cfg, sizeof(cfg));
}

static void FOXHUNT_Begin(void)
{
    BACKLIGHT_UpdateTickless();
    BACKLIGHT_TurnOn();

    // Both apps operate on the user-selected VFO. Dual watch may have left the
    // receiver on the other one, so establish a deterministic starting point.
    gEeprom.RX_VFO = gEeprom.TX_VFO;
    gRxVfo         = gTxVfo;
    gCurrentVfo    = gTxVfo;
    RADIO_SetupRegisters(true);

    // Initialise the full configuration before loading it. The carrier-keying byte was
    // appended (index 7): an older seven-byte config reads 0xFF there and LoadConfig's
    // `<= 1` guard rejects it, so it stays at the default below — no migration needed.
    attStep      = 0;
    foxGraphMode = FOXHUNT_GRAPH_BAR;
    foxAudioMode = FOXHUNT_AUDIO_OFF;
    beaconIdle    = FOXHUNT_BEACON_IDLE_DEF;
    beaconTx      = FOXHUNT_BEACON_TX_DEF;
    foxFox        = FOXHUNT_FOX_CALL;
    beaconCarrierKeyed = false;   // TONE (F2A) by default
    beaconPhaseTx = false;
    FOXHUNT_LoadConfig();

    // Ignore the key that launched the modal app until it has been released.
    kbd.prev = kbd.current = KEYBOARD_GetKey();
    gWasFKeyPressed = false;
    foxLocked = false;
    fHoldMs   = 0;
    fLongDone = false;
    foxRunning = true;
}

static void FOXHUNT_LoadCallsign(void)
{
    char call[FOXHUNT_CALLSIGN_MAX + 1];
    uint8_t n = 0;

    // Read the callsign from the boot message (line 1), sanitised to what Morse can
    // send; empty/erased leaves foxCall empty (the CALL id then falls back to bare MOE).
    PY25Q16_ReadBuffer(SETTINGS_BOOT_MESSAGE_LINE1_ADDR, call, FOXHUNT_CALLSIGN_MAX);
    call[FOXHUNT_CALLSIGN_MAX] = '\0';
    for (uint8_t i = 0; i < FOXHUNT_CALLSIGN_MAX; i++) {
        char c = call[i];
        if (c == '\0' || (uint8_t)c == 0xFF)
            break;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/')
            foxCall[n++] = c;
    }
    foxCall[n] = '\0';
}

static void FOXHUNT_End(void)
{
    FOXHUNT_SaveConfig();
    gWasFKeyPressed = false;
    AUDIO_AudioPathOff();
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);
}

/* Both entry points stay in this translation unit so their small modal services
 * can be shared when both features are enabled. The action table references only
 * the enabled entry point(s); --gc-sections then drops the complete unused RX or
 * Beacon call graph, including its private constants and bitmaps. */
void APP_RunFoxHunt(void)
{
    FOXHUNT_Begin();

    FOXHUNT_EnterHunt();
    while (foxRunning) {
#if defined(ENABLE_UART) || defined(ENABLE_USB)
        UART_ServiceCommands();
#endif
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
        // Keep the K5Viewer link alive and pick up any remote key.
        K5VIEWER_ParseInput();
#endif
        FOXHUNT_HandleKeys();
        if (!foxRunning)
            break;

        curDbm = FOXHUNT_ReadDbm();
        if (curDbm > peakDbm)
            peakDbm = curDbm;
        if (curDbm < minDbm)
            minDbm = curDbm;

        if (++trendTick >= FOXHUNT_TREND_TICKS) {
            trendDelta = curDbm - trendRef;
            trendRef   = curDbm;
            trendTick  = 0;
        }

        FOXHUNT_HistSample();   // feed the smoothed signal-history sparkline

        FOXHUNT_Draw();

        FOXHUNT_BlitScreen();   // status + full frame + K5Viewer mirror

        // Beep mode: Geiger blip whose rate/pitch track the level (silent below floor).
        if (foxAudioMode == FOXHUNT_AUDIO_BEEP && curDbm >= FOXHUNT_SILENCE_DBM) {
            if (++audioTick >= FOXHUNT_BlipPeriod(curDbm)) {
                audioTick = 0;
                FOXHUNT_Blip(FOXHUNT_DbmToTone(curDbm));
            }
        }
        // Station mode: route the received audio, gated by the silence floor.
        else if (foxAudioMode == FOXHUNT_AUDIO_STATION) {
            BK4819_SetAF(curDbm >= FOXHUNT_SILENCE_DBM
                         ? (gRxVfo->Modulation == MODULATION_AM ? BK4819_AF_AM : BK4819_AF_FM)
                         : BK4819_AF_MUTE);
        }

        FOXHUNT_IdleHousekeeping();   // battery sample + save + backlight timeout
        FOXHUNT_TickDelay();          // tick delay + smooth backlight fade
    }

    FOXHUNT_End();
}

void APP_RunBeacon(void)
{
    FOXHUNT_Begin();
    FOXHUNT_LoadCallsign();
    FOXHUNT_EnterBeacon();

    while (foxRunning) {
#if defined(ENABLE_UART) || defined(ENABLE_USB)
        UART_ServiceCommands();
#endif
        FOXHUNT_BeaconTick();
    }

    FOXHUNT_End();
}

#endif // ENABLE_FEAT_F4HWN_FOXHUNT || ENABLE_FEAT_F4HWN_BEACON
