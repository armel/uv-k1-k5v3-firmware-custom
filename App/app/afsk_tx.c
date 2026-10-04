/* SPDX-License-Identifier: Apache-2.0 */
#include "afsk_tx.h"
#include "afsk_core.h"
#include "afsk_hw.h"
#include <string.h>
#ifndef AFSK_TX_HOST_TEST
#include "py32f0xx.h"
#else
#include "afsk_mock.h"
#endif

static uint8_t frame_copy[APP_AFSK_FRAME_MAX];
static app_afsk_opts_t options;
static app_afsk_status_t status;
static afsk_hdlc_t hdlc;
static uint16_t frame_length;
static uint32_t pa_start, next_deadline;
static volatile bool owns_rf, restore_pending;
static bool space_tone;

bool AFSK_TX_OwnsRF(void) { return owns_rf; }

static void finish(uint8_t state, uint8_t error, uint32_t now)
{
    /* PA is the first hardware operation on every terminal path. */
    AFSK_HW_PA(false);
    AFSK_HW_Stop();
    status.pa_on_us = ((AFSK_HW_Now() - pa_start) * 25u) / 3u;
    (void)now;
    status.state = state;
    status.error = error;
    restore_pending = true; /* lease remains held until foreground restores RX */
}

static bool emit_bit(void)
{
    uint8_t bit;
    if (!AFSK_HDLC_Next(&hdlc, frame_copy, frame_length, options.tail_flags, &bit))
        return false;
    if (!bit) {
        space_tone = !space_tone;
        AFSK_HW_Tone(space_tone);
    }
    ++status.bits_sent;
    return true;
}

void AFSK_TX_Tick(uint32_t now)
{
    if (!owns_rf || status.state != APP_AFSK_ACTIVE)
        return;
    uint32_t late = now - next_deadline;
    if (late * 400u > status.max_lateness_cycles)
        status.max_lateness_cycles = late * 400u;
    if (!AFSK_HW_PTT())
        finish(APP_AFSK_ABORTED, APP_AFSK_PTT_RELEASED, now);
    /* Stop one symbol plus a 1 ms PA-gate margin before the requested
     * ceiling. The hardware WCET and interrupt latency require measurement. */
    else if (now - pa_start + AFSK_SYMBOL_TICKS + 120u >=
             (uint32_t)options.max_on_ms * 120u)
        finish(APP_AFSK_ABORTED, APP_AFSK_TIMEOUT, now);
    else if (late > AFSK_LATENESS_TICKS)
        finish(APP_AFSK_ERROR, APP_AFSK_TIMING, now);
    else if (!emit_bit())
        finish(APP_AFSK_DONE, APP_AFSK_OK, now); /* last bit had its full interval */
    else {
        next_deadline += AFSK_SYMBOL_TICKS;
        /* IRQ lateness and SPI both consume the absolute deadline budget.
         * Keep more than two ticks for the compare-register arm sequence;
         * clearing CC1IF after an already elapsed compare would lose it. */
        uint32_t after_spi = AFSK_HW_Now();
        if ((int32_t)(next_deadline - after_spi) <= (int32_t)AFSK_LATENESS_TICKS)
            finish(APP_AFSK_ERROR, APP_AFSK_TIMING, after_spi);
        else
            AFSK_HW_Arm(next_deadline);
    }
}

static void restore(void)
{
    if (!restore_pending)
        return;
    AFSK_HW_Restore();
    AFSK_HW_ReleaseTimer();
    restore_pending = false;
    owns_rf = false;
}

uint8_t AFSK_TX_Info(app_tx_info_t *out)
{
    if (!out || out->size != sizeof(*out))
        return APP_AFSK_BAD_ARGUMENT;
    uint8_t denial = AFSK_HW_Info(out);
    if (owns_rf) {
        out->flags |= APP_TX_INFO_BUSY;
        denial = APP_AFSK_BUSY;
    }
    out->denial = denial;
    return denial;
}

uint8_t AFSK_TX_Submit(const uint8_t *frame, uint16_t length,
                     const app_afsk_opts_t *opts)
{
    if (owns_rf)
        return APP_AFSK_BUSY;
    if (!AFSK_ValidateOptions(opts) || !AFSK_ValidateFrame(frame, length))
        return APP_AFSK_BAD_ARGUMENT;
    app_tx_info_t info = { .size = sizeof(info) };
    uint8_t result = AFSK_HW_Info(&info);
    if (result != APP_AFSK_OK)
        return result;
    if (!opts->channel_token || opts->channel_token != info.channel_token)
        return APP_AFSK_CHANNEL_CHANGED;
    if (!AFSK_HW_PTT())
        return APP_AFSK_PTT_RELEASED;
    if (__get_PRIMASK() || !AFSK_HW_AcquireTimer())
        return APP_AFSK_INTERNAL;

    owns_rf = true; /* no IRQ writes BK until the explicit arm below */
    memcpy(frame_copy, frame, length);
    options = *opts;
    frame_length = length;
    status = (app_afsk_status_t){ .size = sizeof(status), .state = APP_AFSK_IDLE };
    AFSK_HDLC_Init(&hdlc, &options);
    space_tone = false;
    result = AFSK_HW_Prepare(&info, options.tone_gain);
    if (result != APP_AFSK_OK) {
        /* No PA was enabled. Complete the RX restore before refusing. */
        AFSK_HW_PA(false);
        AFSK_HW_Stop();
        restore_pending = true;
        status.state = APP_AFSK_ERROR;
        status.error = result;
        restore();
        return result;
    }
    app_tx_info_t final = { .size = sizeof(final) };
    result = AFSK_HW_Info(&final);
    if (result == APP_AFSK_OK && final.channel_token != options.channel_token)
        result = APP_AFSK_CHANNEL_CHANGED;
    if (result == APP_AFSK_OK && !AFSK_HW_PTT())
        result = APP_AFSK_PTT_RELEASED;
    if (result != APP_AFSK_OK) {
        AFSK_HW_PA(false);
        AFSK_HW_Stop();
        restore_pending = true;
        status.state = APP_AFSK_ERROR;
        status.error = result;
        restore();
        return result;
    }

    /* First symbol is prepared while quiet. CC1 starts after the PA gate write;
     * the time ceiling includes that write and all subsequent keyed intervals. */
    emit_bit();
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    pa_start = AFSK_HW_Now();
    AFSK_HW_PA(true);
    status.state = APP_AFSK_ACTIVE;
    next_deadline = AFSK_HW_Now() + AFSK_SYMBOL_TICKS;
    AFSK_HW_Arm(next_deadline);
    __set_PRIMASK(mask);
    return APP_AFSK_OK;
}

void AFSK_TX_Poll(app_afsk_status_t *out)
{
    if (!out || out->size != sizeof(*out))
        return;
    restore();
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *out = status;
    out->size = sizeof(*out);
    if (status.state == APP_AFSK_ACTIVE)
        out->pa_on_us = ((AFSK_HW_Now() - pa_start) * 25u) / 3u;
    __set_PRIMASK(mask);
}

void AFSK_TX_Cancel(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (owns_rf && status.state == APP_AFSK_ACTIVE)
        finish(APP_AFSK_ABORTED, APP_AFSK_CANCELED, AFSK_HW_Now());
    __set_PRIMASK(mask);
    restore();
}
