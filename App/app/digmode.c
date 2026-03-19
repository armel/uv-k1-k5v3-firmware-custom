/* Copyright 2025 bg7nzl
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "app/digmode.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#include "driver/systick.h"
#include "driver/system.h"
#include "dsp/vernier.h"
#include "functions.h"
#include "radio.h"
#include "scheduler.h"
#include "settings.h"
#include "misc.h"
#include "ui/ui.h"

#if defined(ENABLE_UART)
#include "driver/uart.h"
#endif

/* ------------------------------------------------------------------ */
/*  Global state                                                       */
/* ------------------------------------------------------------------ */

volatile bool gDigmodeEntered  = false;
volatile bool gDigmodeTxActive = false;

DigmodeDisplayState_t gDigmodeDisplay;

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static uint32_t sBaseFreq;       // PLL base frequency word (10 Hz units)
static uint16_t sBase3B;         // REG_3B baseline
static uint32_t sAlphaMhz;       // Vernier alpha (mHz/LSB)
static uint8_t  sPaBias;

/* Heartbeat watchdog — counts down in 10ms ticks */
static uint16_t sHeartbeatCountdown;

/* EMF recovery state */
static uint32_t sPrevApplyAt;
static uint32_t sLastDelta;
static uint16_t sLastFreqDhz;
static uint8_t  sConsecCrcFails;

/* Scheduling FIFO (streaming SET_FREQ) */
typedef struct {
    uint16_t freq_dhz;
    uint32_t apply_at_us;
} FreqEntry_t;

static FreqEntry_t sFifo[DIGMODE_FIFO_SIZE];
static uint8_t     sFifoHead;
static uint8_t     sFifoTail;
static uint8_t     sFifoCount;

/* Schedule buffer (one-shot SCHED_TX) */
static uint16_t sSchedBuf[DIGMODE_SCHED_MAX];
static uint16_t sSchedCount;
static uint16_t sSchedPos;
static uint32_t sSchedInterval;   /* microseconds between steps */
static uint32_t sSchedNextTime;   /* radio-local µs for next step */
static bool     sSchedActive;

/* Deferred start: schedule loaded, waiting for start_at */
static bool     sSchedWaiting;
static uint32_t sSchedStartAt;    /* radio-local µs when TX should begin */
static uint32_t sSchedBaseFreq;
static uint8_t  sSchedPower;

/* ------------------------------------------------------------------ */
/*  FIFO helpers                                                       */
/* ------------------------------------------------------------------ */

static void FifoClear(void)
{
    sFifoHead  = 0;
    sFifoTail  = 0;
    sFifoCount = 0;
}

static bool FifoPush(uint16_t freq_dhz, uint32_t apply_at_us)
{
    if (sFifoCount >= DIGMODE_FIFO_SIZE)
        return false;
    sFifo[sFifoTail].freq_dhz    = freq_dhz;
    sFifo[sFifoTail].apply_at_us = apply_at_us;
    sFifoTail = (sFifoTail + 1) % DIGMODE_FIFO_SIZE;
    sFifoCount++;
    return true;
}

static bool FifoPeek(FreqEntry_t *out)
{
    if (sFifoCount == 0)
        return false;
    *out = sFifo[sFifoHead];
    return true;
}

static void FifoPop(void)
{
    if (sFifoCount == 0)
        return;
    sFifoHead = (sFifoHead + 1) % DIGMODE_FIFO_SIZE;
    sFifoCount--;
}

/* ------------------------------------------------------------------ */
/*  UART frame helpers                                                 */
/* ------------------------------------------------------------------ */

static void SendFrame(const uint8_t *data, uint8_t len)
{
#if defined(ENABLE_UART)
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++)
        crc ^= data[i];
    UART_Send(data, len);
    UART_Send(&crc, 1);
#endif
}

static void SendAck(uint8_t cmd, uint8_t result)
{
    uint8_t frame[9];
    frame[0] = DIGMODE_SYNC;
    frame[1] = DIGMODE_CMD_ACK;
    frame[2] = 6;       /* payload: cmd(1) + result×5 */
    frame[3] = cmd;
    frame[4] = result;
    frame[5] = result;
    frame[6] = result;
    frame[7] = result;
    frame[8] = result;
    SendFrame(frame, 9);
}

static void SendSyncResp(uint32_t pc_time_us)
{
    uint32_t radio_time_us = SCHEDULER_GetMicros();
    uint8_t frame[11];
    frame[0]  = DIGMODE_SYNC;
    frame[1]  = DIGMODE_CMD_SYNC_RESP;
    frame[2]  = 8;  // payload: 4 + 4
    frame[3]  = (pc_time_us >> 24) & 0xFF;
    frame[4]  = (pc_time_us >> 16) & 0xFF;
    frame[5]  = (pc_time_us >>  8) & 0xFF;
    frame[6]  = (pc_time_us >>  0) & 0xFF;
    frame[7]  = (radio_time_us >> 24) & 0xFF;
    frame[8]  = (radio_time_us >> 16) & 0xFF;
    frame[9]  = (radio_time_us >>  8) & 0xFF;
    frame[10] = (radio_time_us >>  0) & 0xFF;
    SendFrame(frame, 11);
}

static void SendStatus(void)
{
    uint8_t frame[6];
    frame[0] = DIGMODE_SYNC;
    frame[1] = DIGMODE_CMD_STATUS;
    frame[2] = 3;
    frame[3] = gDigmodeTxActive ? 1 : 0;
    frame[4] = (sLastFreqDhz >> 8) & 0xFF;
    frame[5] = sLastFreqDhz & 0xFF;
    SendFrame(frame, 6);
}

/* ------------------------------------------------------------------ */
/*  Majority vote for 5 freq copies                                    */
/* ------------------------------------------------------------------ */

static bool MajorityVote(const uint16_t freqs[DIGMODE_FREQ_COPIES],
                         uint16_t *result)
{
    for (uint8_t i = 0; i < DIGMODE_FREQ_COPIES; i++)
    {
        uint8_t count = 0;
        for (uint8_t j = 0; j < DIGMODE_FREQ_COPIES; j++)
        {
            if (freqs[j] == freqs[i])
                count++;
        }
        if (count >= 3)
        {
            *result = freqs[i];
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  Vernier frequency application                                      */
/* ------------------------------------------------------------------ */

static void ApplyFreq(uint16_t freq_dhz)
{
    sLastFreqDhz = freq_dhz;

    uint16_t coarse_steps = freq_dhz / 100;
    uint16_t fine_dhz     = freq_dhz % 100;
    int32_t  fine_mhz     = (int32_t)fine_dhz * 100;

    VernierResult_t v = VERNIER_Solve(fine_mhz, sAlphaMhz);

    uint32_t pll_freq = sBaseFreq + coarse_steps - v.pll_comp;
    BK4819_SetFrequency(pll_freq);

    uint16_t reg3b = sBase3B - v.xtal_trim;
    BK4819_WriteRegister(BK4819_REG_3B, reg3b);

    /* Toggle REG_30 to trigger VCO re-calibration — without this
       the PLL ignores the new frequency in REG_38/39 */
    uint16_t reg30 = BK4819_ReadRegister(BK4819_REG_30);
    BK4819_WriteRegister(BK4819_REG_30, 0);
    BK4819_WriteRegister(BK4819_REG_30, reg30);

    /* Update display state */
    gDigmodeDisplay.cur_audio_dhz = freq_dhz;
    gDigmodeDisplay.cur_rf_freq   = pll_freq;
    gDigmodeDisplay.tx_active     = gDigmodeTxActive;
    gDigmodeDisplay.fifo_depth    = sFifoCount;
    gUpdateDisplay = true;
}

static inline void HeartbeatReset(void)
{
    sHeartbeatCountdown = DIGMODE_HEARTBEAT_MS / 10;
}

/* ------------------------------------------------------------------ */
/*  Circular buffer reader                                             */
/* ------------------------------------------------------------------ */

static inline uint8_t CRead(const uint8_t *buf, uint16_t buf_size,
                             uint16_t start, uint16_t off)
{
    return buf[(start + off) % buf_size];
}

static uint32_t CRead32BE(const uint8_t *buf, uint16_t buf_size,
                           uint16_t start, uint16_t off)
{
    return ((uint32_t)CRead(buf, buf_size, start, off)     << 24) |
           ((uint32_t)CRead(buf, buf_size, start, off + 1) << 16) |
           ((uint32_t)CRead(buf, buf_size, start, off + 2) <<  8) |
           ((uint32_t)CRead(buf, buf_size, start, off + 3));
}

static uint16_t CRead16BE(const uint8_t *buf, uint16_t buf_size,
                           uint16_t start, uint16_t off)
{
    return ((uint16_t)CRead(buf, buf_size, start, off) << 8) |
           CRead(buf, buf_size, start, off + 1);
}

/* ------------------------------------------------------------------ */
/*  TX start / stop                                                    */
/* ------------------------------------------------------------------ */

static void EnterDigmode(void)
{
    if (!gDigmodeEntered)
    {
        gDigmodeEntered = true;

        /* Switch to USB RX with squelch disabled immediately.
           This also wakes BK4819 from power save and lets the
           PLL stabilise well before any TX is requested. */
        gCurrentVfo->Modulation = MODULATION_USB;

        /* Override VFO squelch so RADIO_SetupRegisters (called by
           FUNCTION_Foreground and the main loop) keeps it open. */
        gCurrentVfo->SquelchOpenRSSIThresh    = 0;
        gCurrentVfo->SquelchCloseRSSIThresh   = 0;
        gCurrentVfo->SquelchOpenNoiseThresh   = 127;
        gCurrentVfo->SquelchCloseNoiseThresh  = 127;
        gCurrentVfo->SquelchCloseGlitchThresh = 255;
        gCurrentVfo->SquelchOpenGlitchThresh  = 0;

        RADIO_SetupRegisters(true);
        BK4819_SetupSquelch(0, 0, 127, 127, 255, 0);

        GUI_SelectNextDisplay(DISPLAY_DIGMODE);
    }
}

static void DoStartTx(uint32_t base_freq_10hz, uint8_t power_level)
{
    EnterDigmode();

    if (gDigmodeTxActive)
        return;

    sBase3B   = 22656 + gEeprom.BK4819_XTAL_FREQ_LOW;
    sBaseFreq = base_freq_10hz;
    sAlphaMhz = VERNIER_ComputeAlpha(sBaseFreq * 10);

    /* Apply power level: 0xFF = use current VFO setting */
    if (power_level <= OUTPUT_POWER_HIGH)
    {
        gCurrentVfo->OUTPUT_POWER = power_level;
        gCurrentVfo->pTX->Frequency = sBaseFreq;
        RADIO_ConfigureSquelchAndOutputPower(gCurrentVfo);

        /* The function above also overwrites squelch thresholds from
           EEPROM — force them back to "fully open" so the main loop's
           RADIO_SetupRegisters() won't re-enable squelch on us. */
        gCurrentVfo->SquelchOpenRSSIThresh    = 0;
        gCurrentVfo->SquelchCloseRSSIThresh   = 0;
        gCurrentVfo->SquelchOpenNoiseThresh   = 127;
        gCurrentVfo->SquelchCloseNoiseThresh  = 127;
        gCurrentVfo->SquelchCloseGlitchThresh = 255;
        gCurrentVfo->SquelchOpenGlitchThresh  = 0;
    }
    sPaBias = gCurrentVfo->TXP_CalculatedSetting;

    FifoClear();
    sConsecCrcFails = 0;
    sPrevApplyAt    = 0;
    sLastDelta      = 170000;
    sLastFreqDhz    = 0;

    /*
     * Reuse the exact CW transmit path: set VFO to CW at our frequency,
     * then call FUNCTION_Select(FUNCTION_TRANSMIT) which invokes
     * FUNCTION_Transmit() → RADIO_SetTxParameters() — the proven TX flow.
     */
    gCurrentVfo->pTX->Frequency = sBaseFreq;
    gCurrentVfo->Modulation     = MODULATION_CW;

    FUNCTION_Select(FUNCTION_TRANSMIT);

    /* Disable TX timeout — digmode runs until Python sends STOP */
    gTxTimerCountdown_500ms = 0;
    gTxTimeoutReached       = false;

    /* Kill the CW sidetone — we only want a pure carrier */
    BK4819_WriteRegister(BK4819_REG_70, 0x0000);
    BK4819_WriteRegister(BK4819_REG_71, 0x0000);
    AUDIO_AudioPathOff();
    gEnableSpeaker = false;

    /* Zero MIC gain as extra safety */
    BK4819_WriteRegister(BK4819_REG_7D, 0xE940);

    gDigmodeTxActive = true;

    gDigmodeDisplay.base_freq  = sBaseFreq;
    gDigmodeDisplay.tx_active  = true;
    gUpdateDisplay = true;

    SendAck(DIGMODE_CMD_START_TX, DIGMODE_RESULT_OK);
}

static void DoStopTx(void)
{
    if (!gDigmodeTxActive)
        return;

    gDigmodeTxActive = false;
    FifoClear();

    /* Use the CW end-of-transmission cleanup */
    BK4819_WriteRegister(BK4819_REG_70, 0x0000);
    BK4819_WriteRegister(BK4819_REG_71, 0x0000);
    BK4819_SetAF(BK4819_AF_MUTE);
    BK4819_WriteRegister((BK4819_REGISTER_t)0x40U, 0x3516);

    /* PA off */
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    /* Restore xtal trim */
    BK4819_WriteRegister(BK4819_REG_3B, sBase3B);

    /* Red LED off */
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);

    /* Return to RX via the normal path — must call RADIO_SetupRegisters
       so the PLL frequency is reloaded without the CW -650 Hz offset. */
    gCurrentVfo->Modulation = MODULATION_USB;
    RADIO_SetupRegisters(true);

    /* Force squelch fully open for digital mode RX */
    BK4819_SetupSquelch(0, 0, 127, 127, 255, 0);

    gDigmodeDisplay.tx_active = false;
    gDigmodeDisplay.cur_audio_dhz = 0;
    gUpdateDisplay = true;

    SendAck(DIGMODE_CMD_STOP_TX, DIGMODE_RESULT_OK);
}

/* ------------------------------------------------------------------ */
/*  SET_FREQ with 5x redundancy + EMF recovery                        */
/* ------------------------------------------------------------------ */

static void HandleSetFreq(const uint8_t *buf, uint16_t buf_size,
                          uint16_t start_idx, uint8_t len, bool crc_ok)
{
    /* Expected payload: freq(2B)*5 + apply_at(4B) = 14 bytes */
    if (len < 14)
        return;

    uint16_t freq_dhz;
    uint32_t apply_at;

    if (crc_ok)
    {
        sConsecCrcFails = 0;

        freq_dhz = CRead16BE(buf, buf_size, start_idx, 3);
        apply_at = CRead32BE(buf, buf_size, start_idx, 13);

        if (sPrevApplyAt != 0 && apply_at > sPrevApplyAt)
            sLastDelta = apply_at - sPrevApplyAt;
        sPrevApplyAt = apply_at;
    }
    else
    {
        sConsecCrcFails++;
        gDigmodeDisplay.crc_fail_count = sConsecCrcFails;

        if (sConsecCrcFails >= DIGMODE_MAX_CRC_FAILS)
        {
            DoStopTx();
            return;
        }

        /* Majority vote on 5 freq copies */
        uint16_t freqs[DIGMODE_FREQ_COPIES];
        for (uint8_t i = 0; i < DIGMODE_FREQ_COPIES; i++)
            freqs[i] = CRead16BE(buf, buf_size, start_idx, 3 + i * 2);

        if (!MajorityVote(freqs, &freq_dhz))
            freq_dhz = sLastFreqDhz;

        apply_at = sPrevApplyAt + sLastDelta;
        sPrevApplyAt = apply_at;
    }

    if (!gDigmodeTxActive)
        return;

    FifoPush(freq_dhz, apply_at);
}

/* ------------------------------------------------------------------ */
/*  SCHED_TX / SCHED_APPEND                                            */
/* ------------------------------------------------------------------ */

static void SchedBeginTx(void)
{
    DoStartTx(sSchedBaseFreq, sSchedPower);

    if (sSchedCount > 0)
    {
        ApplyFreq(sSchedBuf[0]);
        sSchedPos = 1;
    }

    sSchedNextTime = SCHEDULER_GetMicros() + sSchedInterval;
    sSchedActive   = true;
    sSchedWaiting  = false;

    gDigmodeDisplay.sched_waiting = false;
}

static void HandleSchedTx(const uint8_t *buf, uint16_t buf_size,
                          uint16_t start_idx, uint8_t len)
{
    /*  Payload: base_freq(4) + interval_us(4) + power(1) + start_at(4) + freq(2×N)
     *  Minimum: 4+4+1+4 = 13 bytes (N may be 0).
     *  N == 0 → no TX, just switch RX to base_freq and clear queue.
     *  start_at = 0 → begin immediately.
     */
    if (len < 13)
        return;

    uint32_t base_freq   = CRead32BE(buf, buf_size, start_idx, 3);
    uint32_t interval_us = CRead32BE(buf, buf_size, start_idx, 7);
    uint8_t  power       = CRead(buf, buf_size, start_idx, 11);
    uint32_t start_at    = CRead32BE(buf, buf_size, start_idx, 12);

    /* Stop any running TX / schedule */
    if (gDigmodeTxActive)
        DoStopTx();
    sSchedActive  = false;
    sSchedWaiting = false;

    EnterDigmode();

    /* Load freq entries starting at payload offset 13 */
    uint8_t n = (len - 13) / 2;
    if (n > DIGMODE_SCHED_MAX)
        n = DIGMODE_SCHED_MAX;

    sSchedCount    = n;
    sSchedPos      = 0;
    sSchedInterval = interval_us;
    sSchedBaseFreq = base_freq;
    sSchedPower    = power;

    for (uint8_t i = 0; i < n; i++)
        sSchedBuf[i] = CRead16BE(buf, buf_size, start_idx, 16 + i * 2);

    gDigmodeDisplay.base_freq = base_freq;

    /* Always update RX frequency to match base_freq */
    gCurrentVfo->pRX->Frequency = base_freq;
    RADIO_SetupRegisters(true);
    BK4819_SetupSquelch(0, 0, 127, 127, 255, 0);

    if (n == 0)
    {
        /* Empty list — RX switched, no TX */
        SendAck(DIGMODE_CMD_SCHED_TX, DIGMODE_RESULT_OK);
        return;
    }

    if (start_at == 0)
    {
        /* Immediate start */
        SchedBeginTx();
    }
    else
    {
        /* Deferred start — wait until start_at */
        sSchedStartAt = start_at;
        sSchedWaiting = true;
        gDigmodeDisplay.sched_waiting = true;
    }

    SendAck(DIGMODE_CMD_SCHED_TX, DIGMODE_RESULT_OK);
}

static void HandleSchedAppend(const uint8_t *buf, uint16_t buf_size,
                              uint16_t start_idx, uint8_t len)
{
    uint8_t n = len / 2;
    uint16_t space = DIGMODE_SCHED_MAX - sSchedCount;
    if (n > space)
        n = space;

    for (uint8_t i = 0; i < n; i++)
        sSchedBuf[sSchedCount + i] = CRead16BE(buf, buf_size, start_idx, 3 + i * 2);

    sSchedCount += n;

    SendAck(DIGMODE_CMD_SCHED_APP, DIGMODE_RESULT_OK);
}

/* ------------------------------------------------------------------ */
/*  Frame parser                                                       */
/* ------------------------------------------------------------------ */

uint16_t DIGMODE_ProcessByte(const uint8_t *buf, uint16_t available,
                             uint16_t buf_size, uint16_t start_idx)
{
    if (available < 4)
        return 0;

    uint8_t sync = CRead(buf, buf_size, start_idx, 0);
    if (sync != DIGMODE_SYNC)
        return 1;

    uint8_t cmd = CRead(buf, buf_size, start_idx, 1);
    if (cmd == 0x00 || cmd > DIGMODE_MAX_CMD)
        return 1;

    uint8_t len = CRead(buf, buf_size, start_idx, 2);
    uint16_t frame_size = 3 + len + 1;

    if (available < frame_size)
        return 0;

    /* CRC check */
    uint8_t crc = 0;
    for (uint16_t i = 0; i < frame_size - 1; i++)
        crc ^= CRead(buf, buf_size, start_idx, i);
    uint8_t rx_crc = CRead(buf, buf_size, start_idx, frame_size - 1);
    bool crc_ok = (crc == rx_crc);

    /* For SET_FREQ we handle CRC failure with EMF recovery */
    if (cmd == DIGMODE_CMD_SET_FREQ)
    {
        HeartbeatReset();
        HandleSetFreq(buf, buf_size, start_idx, len, crc_ok);
        return frame_size;
    }

    /* All other commands require valid CRC */
    if (!crc_ok)
    {
        SendAck(cmd, DIGMODE_RESULT_ERR);
        return frame_size;
    }

    HeartbeatReset();

    switch (cmd)
    {
        case DIGMODE_CMD_START_TX:
        {
            uint32_t base_freq = 0;
            uint8_t  power = 0xFF;
            if (len >= 4)
                base_freq = CRead32BE(buf, buf_size, start_idx, 3);
            else
                base_freq = gCurrentVfo->pTX->Frequency;
            if (len >= 5)
                power = CRead(buf, buf_size, start_idx, 7);
            DoStartTx(base_freq, power);
            break;
        }

        case DIGMODE_CMD_STOP_TX:
            DoStopTx();
            break;

        case DIGMODE_CMD_STATUS:
            SendStatus();
            break;

        case DIGMODE_CMD_SYNC_REQ:
        {
            uint32_t pc_time_us = 0;
            if (len >= 4)
                pc_time_us = CRead32BE(buf, buf_size, start_idx, 3);
            SendSyncResp(pc_time_us);
            EnterDigmode();
            break;
        }

        case DIGMODE_CMD_NOOP:
            break;

        case DIGMODE_CMD_SCHED_TX:
            HandleSchedTx(buf, buf_size, start_idx, len);
            break;

        case DIGMODE_CMD_SCHED_APP:
            HandleSchedAppend(buf, buf_size, start_idx, len);
            break;

        default:
            break;
    }

    return frame_size;
}

/* ------------------------------------------------------------------ */
/*  10ms poll — service scheduling FIFO                                */
/* ------------------------------------------------------------------ */

void DIGMODE_Poll(void)
{
    if (!gDigmodeEntered)
        return;

    /* Heartbeat watchdog: if no valid frame for 1 s, kill TX.
       Paused during schedule wait and playback — both have defined endpoints. */
    if (!sSchedActive && !sSchedWaiting && sHeartbeatCountdown > 0)
    {
        sHeartbeatCountdown--;
        if (sHeartbeatCountdown == 0 && gDigmodeTxActive)
            DoStopTx();
    }

    /* Deferred schedule start — wait for start_at timestamp */
    if (sSchedWaiting)
    {
        uint32_t now = SCHEDULER_GetMicros();
        int32_t diff = (int32_t)(sSchedStartAt - now);
        if (diff <= 0)
        {
            SchedBeginTx();
        }
        else
        {
            gDigmodeDisplay.countdown_ms = (uint32_t)diff / 1000;
            gUpdateDisplay = true;
        }
        return;
    }

    if (!gDigmodeTxActive)
        return;

    /* --- Schedule playback (autonomous, firmware-timed) --- */
    if (sSchedActive)
    {
        uint32_t now = SCHEDULER_GetMicros();
        while (sSchedPos < sSchedCount)
        {
            int32_t diff = (int32_t)(sSchedNextTime - now);
            if (diff > 10000)
                break;          /* next step >10ms away, wait */
            if (diff > 0)
                SYSTICK_DelayUs((uint32_t)diff);

            ApplyFreq(sSchedBuf[sSchedPos]);
            sSchedPos++;
            sSchedNextTime += sSchedInterval;
            now = SCHEDULER_GetMicros();
        }

        if (sSchedPos >= sSchedCount)
        {
            sSchedActive = false;
            DoStopTx();
        }

        gDigmodeDisplay.fifo_depth = sSchedCount - sSchedPos;
        return;
    }

    /* --- Streaming FIFO playback (PC-timed SET_FREQ) --- */
    FreqEntry_t entry;
    while (FifoPeek(&entry))
    {
        uint32_t now = SCHEDULER_GetMicros();
        int32_t  diff = (int32_t)(entry.apply_at_us - now);

        if (diff > 10000)
            break;

        FifoPop();

        if (diff > 0)
            SYSTICK_DelayUs((uint32_t)diff);

        ApplyFreq(entry.freq_dhz);
    }

    gDigmodeDisplay.fifo_depth = sFifoCount;
}
