/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include "ui.h"
#include "tx.h"
static aprs_ui_t ui;
static uint8_t frame[APRS_FRAME_CAP];
static const char results[] = "SENT\0ABORTED\0BUSY\0TX DENIED\0TIMING ERROR\0VFO MISMATCH";
static const uint8_t result_offsets[] = { 0, 5, 13, 18, 28, 41 };

#ifdef __arm__
__attribute__((section(".text.entry"), used))
#endif
void app_main(const app_api_t *a)
{
    /* The stock callback prefix through nav_dir is sufficient. No BEAM,
     * triple-VFO service or resident internals are used. */
    if (a->abi_major != 1 || a->api_level < 1 ||
        a->api_size < offsetof(app_api_t, nav_dir) + sizeof(a->nav_dir)) return;
    aprs_defaults(&ui.model);
    uint8_t cfg[16];
    a->cfg_load(cfg, 16);
    if (!aprs_cfg_decode16(cfg, &ui.model)) aprs_defaults(&ui.model);
    aprs_ui_init(&ui);
    a->backlight_on();
    const char *status = ui.setup ? "SETUP" : "READY";
    uint8_t previous = APP_KEY_INVALID, released = 0;
    bool armed = false, redraw = true;
    uint16_t battery = 0;
    for (;;) {
        uint8_t key = a->get_key();
        bool ptt = aprs_stock_ptt();
        if (!ptt && key == APP_KEY_INVALID) {
            if (released < 3) ++released;
            if (released == 3) armed = true;
        } else released = 0;
        if (redraw) { aprs_ui_draw(a, &ui, status, a->tx_freq()); redraw = false; }
        if (key != previous && key != APP_KEY_INVALID && key != APP_KEY_PTT) {
            bool setup = ui.setup;
            if (!aprs_ui_key(a, &ui, key)) break;
            if (setup != ui.setup) { armed = false; released = 0; }
            status = aprs_model_valid(&ui.model) ? "READY" : "SETUP";
            redraw = true; a->backlight_on();
        }
        previous = key;
        if (ptt && armed && !ui.setup) {
            armed = false; released = 0;
            uint16_t length = aprs_frame_build(&ui.model, "", frame);
            uint8_t result = APRS_DENIED;
            uint32_t frequency = a->tx_freq();
            a->battery_sample();
            if (length && !a->tx_state() && a->rx_freq() == frequency) {
                if (aprs_stock_clock_valid()) {
                    aprs_ui_draw(a, &ui, "CCA", frequency);
                    result = aprs_stock_cca(a);
                    if (result == APRS_SENT) {
                        aprs_ui_draw(a, &ui, "TX", frequency);
                        result = aprs_stock_send(a, frame, length, frequency);
                    }
                } else result = APRS_TIMING;
            }
            status = results + result_offsets[result]; redraw = true;
        }
        aprs_ui_tick(&ui);
        if (++battery == 50) { battery = 0; a->battery_sample(); }
        a->delay_ms(20); a->backlight_update();
    }
    aprs_stock_cleanup(a);
    if (ui.dirty && aprs_model_valid(&ui.model)) {
        aprs_cfg_encode16(&ui.model, cfg); a->cfg_save(cfg, 16);
    }
}
