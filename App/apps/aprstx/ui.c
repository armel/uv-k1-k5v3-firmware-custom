/* SPDX-License-Identifier: Apache-2.0 */
#include "ui.h"
static const char *const fields[] = { "Call", "SSID", "Lat DDMMhh",
    "Lon DDDMMhh", "Path", "Symbol" };
static const char *const paths[] = { "DIRECT", "WIDE1-1", "WIDE1-1,WIDE2-1", "ARISS" };
static const char alphabet[10][5] = { "0", "1", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ" };

void aprs_ui_init(aprs_ui_t *u)
{
    u->setup = !aprs_model_valid(&u->model);
    u->editing = u->field = u->length = u->dirty = u->tap_ms = 0;
    u->position_set = u->model.valid ? 3 : 0;
}

void aprs_ui_tick(aprs_ui_t *u)
{
    u->tap_ms = u->tap_ms > 20 ? u->tap_ms - 20 : 0;
}

static void begin(aprs_ui_t *u)
{
    u->editing = 1; u->tap_ms = 0; u->numeric = u->field != 0;
    u->length = 0; u->negative = 0;
    if (u->field == 5) { u->edit[0] = u->model.symbol; u->length = 1; u->negative = u->model.table; }
    u->edit[u->length] = 0;
}

static bool confirm(aprs_ui_t *u)
{
    uint32_t v = 0;
    if (u->field != 0 && u->field != 5)
        for (uint8_t i = 0; i < u->length; ++i) v = v * 10 + u->edit[i] - '0';
    switch (u->field) {
    case 0:
        if (!aprs_call_valid(u->edit)) return false;
        for (uint8_t i = 0; i < 7; ++i) u->model.call[i] = u->edit[i];
        break;
    case 1: if (!u->length || v > 15) return false; u->model.ssid = v; break;
    case 2: case 3: {
        uint32_t max = u->field == 2 ? 90 : 180;
        uint32_t min = v, deg = aprs_divmod(&min, 10000);
        if (u->length != (u->field == 2 ? 6 : 7) || min >= 6000 || deg > max || (deg == max && min)) return false;
        int32_t pos = deg * 6000 + min;
        if (u->negative) pos = -pos;
        if (u->field == 2) u->model.lat = pos; else u->model.lon = pos;
        u->position_set |= u->field == 2 ? 1 : 2;
        u->model.valid = u->position_set == 3;
        break;
    }
    case 4: if (!u->length || v > 3) return false; u->model.path = v; break;
    case 5: u->model.symbol = u->edit[0]; u->model.table = u->negative; break;
    }
    u->dirty = 1; return true;
}

bool aprs_ui_key(const app_api_t *a, aprs_ui_t *u, uint8_t key)
{
    if (!u->setup) {
        if (key == APP_KEY_EXIT) return false;
        if (key == APP_KEY_MENU) u->setup = 1;
        return true;
    }
    if (key == APP_KEY_EXIT) {
        if (u->editing) u->editing = 0;
        else u->setup = 0;
        return true;
    }
    if (key == APP_KEY_MENU) {
        if (!u->editing) begin(u);
        else if (confirm(u)) u->editing = 0;
        return true;
    }
    int8_t d = a->nav_dir(key);
    if (!u->editing) {
        if (d) { int8_t f = u->field + d; u->field = f < 0 ? 5 : (f > 5 ? 0 : f); }
        return true;
    }
    if (key == APP_KEY_F) {
        if (u->field == 0) { u->numeric ^= 1; u->tap_ms = 0; }
        if (u->field == 2 || u->field == 3 || u->field == 5) u->negative ^= 1;
    }
    if (u->field == 5) {
        if (d) { int16_t c = u->edit[0] + d; u->edit[0] = c < '!' ? '~' : (c > '~' ? '!' : c); }
        return true;
    }
    if (key == APP_KEY_STAR) { if (u->length) u->edit[--u->length] = 0; u->tap_ms = 0; }
    if (key <= APP_KEY_9) {
        static const uint8_t caps[] = { 6, 2, 6, 7, 2, 1 };
        uint8_t cap = caps[u->field];
        const char *set = alphabet[key];
        bool repeat = !u->numeric && u->tap_ms && key == u->last_digit && u->length;
        if (repeat) {
            ++u->tap; if (!set[u->tap]) u->tap = 0;
            u->edit[u->length - 1] = set[u->tap];
        } else if (u->length < cap) {
            u->tap = 0;
            u->edit[u->length++] = u->numeric ? '0' + key : set[0];
            u->edit[u->length] = 0;
        }
        u->last_digit = key; u->tap_ms = 700;
    }
    return true;
}

static void line(const app_api_t *a, const char *s, uint8_t y)
{
    a->print_tiny(s, 0, y, false, true);
}

void aprs_ui_draw(const app_api_t *a, const aprs_ui_t *u, const char *status, uint32_t frequency)
{
    char b[16];
    a->display_clear(); a->status_clear();
    a->print_tiny(status, 0, 0, true, true); a->draw_battery();
    line(a, "APRSTX POS/FM", 0);
    if (u->setup) {
        line(a, fields[u->field], 10);
        line(a, u->editing ? u->edit : "MENU", 20);
        if (u->editing && (u->field == 2 || u->field == 3 || u->field == 5)) {
            b[0] = u->field == 5 ? (u->negative ? '\\' : '/') :
                (u->field == 2 ? (u->negative ? 'S' : 'N') : (u->negative ? 'W' : 'E'));
            b[1] = 0; line(a, b, 30);
        }
        line(a, "MENU OK  EXIT", 40);
        line(a, u->editing ? "* del F" : "Save: exit", 48);
    } else {
        uint8_t i = 0; while (u->model.call[i]) { b[i] = u->model.call[i]; ++i; }
        b[i++] = '-'; aprs_number(u->model.ssid, b + i, 2); line(a, b, 8);
        aprs_number(aprs_divmod(&frequency, 100000), b, 4); b[4] = '.';
        aprs_number(frequency, b + 5, 5); line(a, b, 16);
        aprs_coordinate(u->model.lat, false, b); line(a, b, 24);
        aprs_coordinate(u->model.lon, true, b); line(a, b, 32);
        line(a, paths[u->model.path], 40);
        line(a, "MENU / Hold PTT", 48);
    }
    a->blit_full(); a->blit_status();
}
