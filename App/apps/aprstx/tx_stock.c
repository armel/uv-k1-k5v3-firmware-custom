/* SPDX-License-Identifier: Apache-2.0
 * Experimental UV-K1 / UV-K5 V3, F4HWN Labs v6.0.0 only.
 * No private resident addresses: the sole MMIO reads are SysTick and PTT.
 */
#include "tx.h"
#define MMIO(a) (*(volatile const uint32_t *)(a))
#define SYSTICK_CTRL MMIO(0xE000E010u)
#define SYSTICK_LOAD MMIO(0xE000E014u)
#define SYSTICK_VAL  MMIO(0xE000E018u)
#define PTT_IDR MMIO(0x50000410u)
#define BIT_CYCLES 40000u
#define PERIOD 480000u
#define LATE_CYCLES 960u /* provisional 20 us; measure on hardware */
#define MAX_CYCLES (1900u * 48000u) /* setup gets a nominal 100 ms reserve */
static bool owned;

bool aprs_stock_ptt(void) { return !(PTT_IDR & (1u << 10)); }
bool aprs_stock_clock_valid(void)
{
    return (SYSTICK_CTRL & 7) == 7 && SYSTICK_LOAD == PERIOD - 1;
}

uint8_t aprs_stock_cca(const app_api_t *a)
{
    uint16_t quiet = 0, backoff = 0;
    for (uint16_t elapsed = 0; elapsed < 5000; elapsed += 20) {
        if (!aprs_stock_ptt()) return APRS_ABORTED;
        int16_t rssi = a->rssi_dbm();
        if (rssi >= -110) {
            quiet = 0;
            backoff = 100 + (SYSTICK_VAL & 15) * 20;
        } else if (backoff) backoff -= 20;
        else { quiet += 20; if (quiet > 200) return APRS_SENT; }
        a->delay_ms(20); a->backlight_update();
    }
    return APRS_BUSY;
}

void aprs_stock_cleanup(const app_api_t *a)
{
    if (!owned) return;
    a->tx_carrier(false); /* PA first; do not restore raw TX registers */
    a->tx_mute(true);
    a->bk_write(0x70, 0);
    a->tx_end();
    owned = false;
}

uint8_t aprs_stock_send(const app_api_t *a, const uint8_t *frame, uint16_t length,
                        uint32_t frequency)
{
    uint8_t result = APRS_DENIED;
    if (!length || length > APRS_FRAME_CAP || !aprs_stock_clock_valid()) return APRS_TIMING;
    if (!aprs_stock_ptt()) return APRS_ABORTED;
    if (a->tx_state() || a->tx_freq() != frequency || a->rx_freq() != frequency)
        return APRS_DENIED;
    if (a->rssi_dbm() >= -110) return APRS_BUSY;
    owned = true; /* set before the callback that can key PA */
    a->tx_set_params();
    a->tx_carrier(false);
    a->tx_mute(true);
    uint32_t actual = a->bk_read(0x38) | ((uint32_t)a->bk_read(0x39) << 16);
    if (actual != frequency) { result = APRS_VFO; goto done; }
    if (!aprs_stock_ptt()) { result = APRS_ABORTED; goto done; }
    a->bk_write(0x31, a->bk_read(0x31) & ~14u); /* scramble, VOX, compander */
    a->bk_write(0x2B, 0); a->bk_write(0x24, 0); a->bk_write(0x51, 0);
    a->tx_tone(1200); /* prime once; blocks 50 ms with PA disabled */
    a->tx_mute(true);
    if (!aprs_stock_ptt()) { result = APRS_ABORTED; goto done; }
    if (a->bk_read(0x30) & (1u << 2)) goto done; /* MIC ADC must be off */
    if (!aprs_stock_clock_valid()) { result = APRS_TIMING; goto done; }
    aprs_hdlc_t bits;
    aprs_hdlc_init(&bits, frame, length, 45, 3);
    /* Blocking setup may span multiple SysTick wraps: initialize only now. */
    bool space = false;
    uint8_t bit;
    a->tx_carrier(true); a->tx_mute(false);
    uint32_t previous = SYSTICK_VAL, now = 0, deadline = 0;
    bool have_bit = aprs_hdlc_next(&bits, &bit);
    for (;;) {
        uint32_t current = SYSTICK_VAL;
        now += previous >= current ? previous - current : previous + PERIOD - current;
        previous = current;
        if (!aprs_stock_ptt()) { result = APRS_ABORTED; goto done; }
        if (now > MAX_CYCLES || now > deadline + LATE_CYCLES) {
            result = APRS_TIMING; goto done;
        }
        if (now < deadline) continue;
        /* A final deadline keeps the last symbol on air for a full period. */
        if (!have_bit) break;
        if (!bit) { space = !space; a->bk_write(0x71, space ? 0x58BA : 0x3065); }
        deadline += BIT_CYCLES;
        have_bit = aprs_hdlc_next(&bits, &bit);
    }
    result = APRS_SENT;
done:
    aprs_stock_cleanup(a);
    return result;
}
