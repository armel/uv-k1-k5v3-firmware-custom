/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../App/apps/aprstx/ui.h"
#include "../../App/apps/aprstx-resident/tx_resident.h"

void app_main(const app_api_t *api);
static aprs_resident_api_t resident;
static app_api_t *api;
static uint8_t sequence[1024];
static unsigned sequence_length, cursor, sends, cancels, polls, saves;
static uint32_t elapsed, submitted_at;
static uint8_t denial, submit_error, terminal_error;
static bool active, busy, stalled, corrupt_config;
static app_tx_info_t info;
static uint8_t framebuffer[7][128];
static unsigned frequency_labels;

static aprs_model_t model(void)
{
    aprs_model_t m;
    aprs_defaults(&m);
    memcpy(m.call, "N0CALL", 7);
    m.ssid = 9; m.lat = 334500; m.lon = 225700; m.valid = 1;
    return m;
}

static void push(uint8_t key, unsigned count)
{
    while (count--) {
        assert(sequence_length < sizeof(sequence));
        sequence[sequence_length++] = key;
    }
}
static uint8_t key(void)
{
    assert(cursor < 10000); /* a lifecycle regression must not hang the suite */
    uint8_t result = cursor < sequence_length ? sequence[cursor] : APP_KEY_EXIT;
    ++cursor;
    return result;
}
static void delay(uint32_t ms) { elapsed += ms; }
static void nothing(void) { assert(!active); }
static void tiny(const char *s, uint8_t x, uint8_t y, bool status, bool fill)
{
    (void)x; (void)status; assert(fill && !active);
    if ((!strncmp(s, "RX ", 3) && y == 24) ||
        (!strncmp(s, "TX ", 3) && y == 32)) ++frequency_labels;
}
static int8_t nav(uint8_t k)
{ return k == APP_KEY_UP ? 1 : (k == APP_KEY_DOWN ? -1 : 0); }
static int16_t rssi(void) { assert(!active); return busy ? -80 : -120; }
static void load(uint8_t *raw, uint8_t size)
{
    aprs_model_t m = model();
    assert(size == 16);
    aprs_cfg_encode16(&m, raw);
    if (corrupt_config) raw[0] = 0xFF;
}
static void save(const uint8_t *raw, uint8_t size)
{
    aprs_model_t m;
    assert(!active && size == 16 && aprs_cfg_decode16(raw, &m));
    ++saves;
}
static uint8_t tx_info(app_tx_info_t *out)
{
    assert(!active && out->size == sizeof(*out));
    *out = info;
    out->denial = denial;
    return denial;
}
static uint8_t submit(const uint8_t *frame, uint16_t length,
                       const app_afsk_opts_t *opts)
{
    assert(!active && aprs_crc16(frame, length) == 0xF0B8);
    assert(opts->size == 16 && opts->flags == APP_AFSK_REQUIRE_PHYSICAL_PTT);
    assert(opts->channel_token == info.channel_token);
    assert(opts->preamble_flags == 45 && opts->tail_flags == 3);
    assert(opts->tone_gain == 66 && opts->max_on_ms == 2000 && !opts->reserved);
    ++sends; submitted_at = elapsed; polls = 0;
    if (!submit_error) active = true;
    return submit_error;
}
static void poll(app_afsk_status_t *out)
{
    assert(active && out->size == 16);
    ++polls;
    out->state = stalled || polls < 3 ? APP_AFSK_ACTIVE :
                 terminal_error ? APP_AFSK_ABORTED : APP_AFSK_DONE;
    out->error = terminal_error;
    if (out->state != APP_AFSK_ACTIVE) active = false;
}
static void cancel(void) { ++cancels; active = false; }

static void reset(void)
{
    memset(&resident, 0, sizeof(resident));
#if APP_API_LEVEL < 2
    api = &resident.v1;
#else
    api = &resident;
#endif
    *api = (app_api_t){
        .abi_major = 1, .api_level = 2, .api_size = sizeof(resident),
        .fb = framebuffer, .get_key = key, .delay_ms = delay, .display_clear = nothing,
        .status_clear = nothing, .print_tiny = tiny, .draw_battery = nothing,
        .blit_full = nothing, .blit_status = nothing, .backlight_on = nothing,
        .backlight_update = nothing, .battery_sample = nothing,
        .cfg_load = load, .cfg_save = save, .nav_dir = nav, .rssi_dbm = rssi
    };
    resident.tx_info = tx_info; resident.afsk_submit = submit;
    resident.afsk_poll = poll; resident.afsk_cancel = cancel;
    sequence_length = cursor = sends = cancels = polls = saves = 0;
    elapsed = submitted_at = 0;
    frequency_labels = 0;
    active = busy = stalled = corrupt_config = false;
    denial = submit_error = terminal_error = APP_AFSK_OK;
    info = (app_tx_info_t){.size = 20, .flags = APP_TX_INFO_SIMPLEX,
        .tx_freq_10hz = 14439000, .rx_freq_10hz = 14439000,
        .channel_token = 0x12345678, .modulation = APP_TX_MOD_FM};
}

static uint8_t send_frame(void)
{
    aprs_model_t m = model();
    uint8_t frame[APRS_FRAME_CAP];
    uint16_t length = aprs_frame_build(&m, "", frame);
    assert(length && aprs_crc16(frame, length) == 0xF0B8);
    return aprs_resident_send(api, frame, length, &info);
}

static void backend_tests(void)
{
    reset(); assert(aprs_resident_available(api));
    assert(!aprs_resident_available(NULL));
    api->abi_major = 2; assert(!aprs_resident_available(api)); api->abi_major = 1;
    api->api_level = 1; assert(!aprs_resident_available(api));
    app_main(api); assert(!cancels && !sends);
    api->api_level = 2; api->api_size = sizeof(resident) - 1;
    assert(!aprs_resident_available(api));
    api->api_size = sizeof(resident); resident.afsk_cancel = 0;
    assert(!aprs_resident_available(api));

    reset(); push(APP_KEY_PTT, 20);
    assert(send_frame() == APP_AFSK_OK);
    assert(sends == 1 && cancels == 1 && submitted_at >= 200 && !active);

    reset(); busy = true; push(APP_KEY_PTT, 300);
    assert(send_frame() == APP_AFSK_BUSY);
    assert(!sends && cancels == 1 && elapsed == 5000);

    reset(); push(APP_KEY_PTT, 3); push(APP_KEY_INVALID, 1);
    assert(send_frame() == APP_AFSK_PTT_RELEASED && !sends && cancels == 1);

    reset(); push(APP_KEY_PTT, 11); push(APP_KEY_INVALID, 1);
    assert(send_frame() == APP_AFSK_PTT_RELEASED && sends == 1 && !active);

    reset(); info.rx_freq_10hz += 60000; push(APP_KEY_PTT, 20);
    assert(send_frame() == APP_AFSK_TX_DENIED && !sends && cancels == 1);
    reset(); info.modulation = APP_TX_MOD_AM; push(APP_KEY_PTT, 20);
    assert(send_frame() == APP_AFSK_TX_DENIED && !sends && cancels == 1);

    reset(); submit_error = APP_AFSK_CHANNEL_CHANGED; push(APP_KEY_PTT, 20);
    assert(send_frame() == APP_AFSK_CHANNEL_CHANGED && sends == 1 && cancels == 1);
    reset(); terminal_error = APP_AFSK_TIMING; push(APP_KEY_PTT, 20);
    assert(send_frame() == APP_AFSK_TIMING && cancels == 1 && !active);
    reset(); stalled = true; push(APP_KEY_PTT, 300);
    assert(send_frame() == APP_AFSK_TIMEOUT && cancels == 1 && !active);
}

static void lifecycle_tests(void)
{
    reset(); push(APP_KEY_PTT, 20); push(APP_KEY_EXIT, 1);
    app_main(api); assert(!sends && cancels == 2 && !saves);

    reset(); push(APP_KEY_INVALID, 4); push(APP_KEY_PTT, 80); push(APP_KEY_EXIT, 1);
    app_main(api); assert(sends == 1 && !active && !saves);

    reset(); push(APP_KEY_INVALID, 4); push(APP_KEY_MENU, 1);
    push(APP_KEY_INVALID, 1); push(APP_KEY_EXIT, 1); /* return from setup */
    push(APP_KEY_PTT, 40); push(APP_KEY_EXIT, 1);
    app_main(api); assert(!sends);

    reset(); push(APP_KEY_INVALID, 4); push(APP_KEY_PTT, 5);
    push(APP_KEY_INVALID, 1); push(APP_KEY_EXIT, 1);
    app_main(api); assert(!sends && cancels == 3);

    reset(); push(APP_KEY_INVALID, 4); push(APP_KEY_PTT, 40);
    push(APP_KEY_INVALID, 4); push(APP_KEY_PTT, 40); push(APP_KEY_EXIT, 1);
    app_main(api); assert(sends == 2 && !active);

    reset(); denial = APP_AFSK_LOW_BATTERY;
    push(APP_KEY_INVALID, 4); push(APP_KEY_PTT, 40); push(APP_KEY_EXIT, 1);
    app_main(api); assert(!sends && !active);

    reset(); info.rx_freq_10hz += 60000;
    push(APP_KEY_INVALID, 4); push(APP_KEY_PTT, 40); push(APP_KEY_EXIT, 1);
    app_main(api); assert(!sends && !active && frequency_labels >= 2);

    reset(); corrupt_config = true;
    push(APP_KEY_INVALID, 4); push(APP_KEY_PTT, 40);
    push(APP_KEY_EXIT, 1); push(APP_KEY_INVALID, 1); push(APP_KEY_EXIT, 1);
    app_main(api); assert(!sends && !saves);

    /* Edit SSID in the real UI, then exit: exactly one valid 16-byte stage. */
    reset(); push(APP_KEY_MENU, 1); push(APP_KEY_INVALID, 1);
    push(APP_KEY_UP, 1); push(APP_KEY_INVALID, 1); push(APP_KEY_MENU, 1);
    push(APP_KEY_INVALID, 1); push(APP_KEY_1, 1); push(APP_KEY_INVALID, 1);
    push(APP_KEY_MENU, 1); push(APP_KEY_INVALID, 1); push(APP_KEY_EXIT, 1);
    push(APP_KEY_INVALID, 1); push(APP_KEY_EXIT, 1);
    app_main(api); assert(!sends && saves == 1 && !active);
}

int main(void)
{
    backend_tests(); lifecycle_tests();
    puts("Resident APRS API guards, CCA, cancel, token, PTT and config tests passed");
}
