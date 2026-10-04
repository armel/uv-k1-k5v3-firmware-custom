/* Copyright 2026 Armel F4HWN contributors
 * SPDX-License-Identifier: Apache-2.0 */
#include "../aprstx/ui.h"
#include "tx_resident.h"

/* These buffers occupy the overlay workspace, not the resident shared stack. */
static aprs_ui_t ui;
static uint8_t frame[APRS_FRAME_CAP], config[16];
static app_tx_info_t selected;

static void draw(const app_api_t *api, const char *status)
{
    aprs_ui_draw(api, &ui, status, selected.tx_freq_10hz);
    if (!ui.setup && selected.rx_freq_10hz != selected.tx_freq_10hz) {
        /* A rejected offset/reverse channel still shows both snapshot
         * frequencies, so its RSSI cannot be mistaken for TX-channel CCA. */
        char label[14];
        for (uint8_t i = 0; i < 2; ++i) {
            uint32_t frequency = i ? selected.tx_freq_10hz : selected.rx_freq_10hz;
            for (uint8_t x = 0; x < 128; ++x) api->fb[3 + i][x] = 0;
            label[0] = i ? 'T' : 'R'; label[1] = 'X'; label[2] = ' ';
            aprs_number(aprs_divmod(&frequency, 100000), label + 3, 4);
            label[7] = '.'; aprs_number(frequency, label + 8, 5);
            api->print_tiny(label, 0, i ? 32 : 24, false, true);
        }
        api->blit_full();
    }
}

static const char *channel_status(uint8_t denial)
{
    if (selected.modulation != APP_TX_MOD_FM) return "FM ONLY";
    if (!(selected.flags & APP_TX_INFO_SIMPLEX) ||
        selected.tx_freq_10hz != selected.rx_freq_10hz) return "SIMPLEX ONLY";
    return denial == APP_AFSK_OK ? "READY FM" : aprs_resident_result(denial);
}

#ifndef APRS_HOST_TEST
__attribute__((section(".text.entry"), used))
#endif
void app_main(const app_api_t *api)
{
    if (!aprs_resident_available(api)) return;
    const aprs_resident_api_t *resident = (const aprs_resident_api_t *)api;
    resident->afsk_cancel();
    api->cfg_load(config, sizeof(config));
    if (!aprs_cfg_decode16(config, &ui.model)) aprs_defaults(&ui.model);
    aprs_ui_init(&ui);
    selected.size = sizeof(selected);
    const char *status = channel_status(resident->tx_info(&selected));
    uint8_t previous = APP_KEY_INVALID, released = 0;
    uint16_t ticks = 0;
    bool redraw = true;

    for (;;) {
        uint8_t key = api->get_key();
        /* An all-keys-up interval is required after entry, setup and TX. PTT
         * seen in setup consumes the latch and cannot become an automatic TX. */
        if (key == APP_KEY_INVALID && !ui.setup) {
            if (released < 3) ++released;
        } else if (key == APP_KEY_PTT && released == 3 && !ui.setup) {
            released = 0;
            uint16_t length = aprs_frame_build(&ui.model, "", frame);
            if (!length) {
                status = "SET CALL/POSITION";
            } else {
                api->battery_sample();
                uint8_t error = resident->tx_info(&selected);
                if (error == APP_AFSK_OK || error == APP_AFSK_BUSY) {
                    status = "CCA / HOLD PTT";
                    draw(api, status);
                    error = aprs_resident_send(api, frame, length, &selected);
                    status = aprs_resident_result(error);
                } else {
                    resident->afsk_cancel();
                    status = aprs_resident_result(error);
                }
            }
            redraw = true;
        } else if (key != APP_KEY_INVALID) {
            released = 0;
        }
        if (key != previous && key != APP_KEY_INVALID && key != APP_KEY_PTT) {
            if (!aprs_ui_key(api, &ui, key)) break;
            redraw = true;
        }
        previous = key;
        aprs_ui_tick(&ui);
        api->backlight_update();
        if (++ticks == 50) {
            ticks = 0;
            api->battery_sample();
            uint8_t error = resident->tx_info(&selected);
            if (error != APP_AFSK_OK && error != APP_AFSK_BUSY)
                status = channel_status(error);
            redraw = true;
        }
        if (redraw) {
            api->backlight_on();
            draw(api, status);
            redraw = false;
        }
        api->delay_ms(20);
    }
    /* The resident loader also cancels before VFO restore and deferred flash
     * commit; the app owns its cleanup on every controlled exit. */
    resident->afsk_cancel();
    if (ui.dirty && aprs_model_valid(&ui.model)) {
        aprs_cfg_encode16(&ui.model, config);
        api->cfg_save(config, sizeof(config));
    }
}
