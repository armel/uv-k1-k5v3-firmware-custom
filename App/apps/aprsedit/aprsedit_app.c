/* APRS Edit - companion editor for APRS TX's mutable beacon profile.
 *
 * The profile is edited in place: opening a field snapshots the whole block,
 * EXIT restores the snapshot and MENU keeps the edit. The index and the editor
 * therefore draw the same bytes with the same code; the editor only adds the
 * cursor, the bold value and the hints. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "../aprs_profile.h"
#include "aprsedit_assets.h"

#define HOLD_TICKS    40u
#define COMMIT_TICKS  80u
#define LOOP_MS       10u
#define EDIT_CHARS    11u    /* comment: normal-font characters per pane row   */
#define EDIT_ROWS     6u     /* rows above the bottom capsules: no scrolling   */
#define PATH_WIDE2    2u     /* "WIDE1-1,WIDE2-1": its second hop on a second line */
#define PATH_MAX      3u     /* "ARISS", the ISS digipeater alias */
#define LAT_DIGITS    6u
#define INDEX_SEP_X   48u
#define PANE_X        50u
#define PANE_W        (128u - PANE_X)
#define PANE_SYM_X    79u
#define NO_CURSOR     0xFFu
#define MODE_CAPS_X   115u   /* input mode capsule: 3 tiny characters, bottom right */
#define RATIO_LEFT_X  54u    /* "xx/yy" capsule, bottom left of the pane */
#define RATIO_RIGHT_X 107u   /* the same, bottom right */
#define SYM_JUMP      5u     /* a number key selects symbol key * SYM_JUMP */

enum { SCREEN_INDEX = 0, SCREEN_EDIT, SCREEN_PRESET };
enum {
    ITEM_CALL = 0, ITEM_SSID, ITEM_LAT, ITEM_LON,
    ITEM_PATH, ITEM_SYMBOL, ITEM_COMMENT, MENU_COUNT
};

_Static_assert(PANE_X + EDIT_CHARS * 7u <= 128u, "comment editor row must fit the pane");
_Static_assert(COMMENT_LIMIT < EDIT_ROWS * EDIT_CHARS && COMMENT_LIMIT <= APRS_COMMENT_MAX,
               "a full comment and its insertion cursor must fit above the capsules");
_Static_assert(PANE_SYM_X + SYM_W <= 128u, "APRS symbol must fit on screen");
_Static_assert(PRESET_COUNT == 5u, "APRS preset selector shows five rows");
_Static_assert(TEMPLATE_COUNT == 4u, "APRS preset selector has four templates");
_Static_assert(T_MODE_LO == T_MODE_UP + 4u && T_MODE_NUM == T_MODE_UP + 8u,
               "input mode names are indexed by mode * 4");
_Static_assert(9u * SYM_JUMP < SYM_COUNT, "symbol jump must stay in the table");

static struct {
    const app_api_t *A;
    uint8_t *profile, *backup;   /* live block, snapshot taken when a field opens */
    char *custom;
    char *text;                  /* callsign or comment inside the profile */
    uint8_t len, cursor, custom_len, custom_cursor;
    uint8_t mode, screen, item, preset, active_preset;
    uint8_t pending_key, pending_pos, cycle, held_key;
    uint16_t pending_ticks, held_ticks;
    bool pending, long_fired, dirty, changed, running, missing, bad, return_to_tx;
} g;

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)value;
    return dst;
}

/* The resident formatter (API level 3). One local stub keeps every call a
 * plain bl: the service lies past the ldr immediate range of the table. */
__attribute__((noinline))
static char *put_u8(char *out, uint8_t value)
{
    return g.A->append_i32(out, value);
}

/* "3/7" */
static char *put_ratio(char *out, uint8_t value, uint8_t total)
{
    out = put_u8(out, value); *out++ = '/';
    return put_u8(out, total);
}

/* v moved one step in direction d within 0..n-1, wrapping at both ends. */
static uint8_t step(uint8_t v, int8_t d, uint8_t n)
{
    if (d > 0) return (uint8_t)(v + 1u < n ? v + 1u : 0u);
    if (d < 0) return (uint8_t)(v ? v - 1u : n - 1u);
    return v;
}

static bool is_text(void)
{
    return g.item == ITEM_CALL || g.item == ITEM_COMMENT;
}

/* Point the text editor at the selected item's characters (the comment for
 * every item but the callsign) and load their length. */
static void select_text(void)
{
    const bool call = g.item == ITEM_CALL;
    g.text = (char *)g.profile + (call ? APRS_PROFILE_CALL : APRS_PROFILE_COMMENT);
    g.len = g.profile[call ? APRS_PROFILE_CALL_LEN : APRS_PROFILE_COMMENT_LEN];
}

/* While a multi-tap character is pending the cursor stays on it. */
static uint8_t text_cursor(void)
{
    return g.pending ? g.pending_pos : g.cursor;
}

static bool position_ok(void)
{
    const uint8_t *d = g.profile + APRS_PROFILE_POS;
    for (uint8_t i = 0u; i < APRS_POS_DIGITS; i++) if (d[i] > 9u) return false;
    const uint8_t lat = (uint8_t)(d[0] * 10u + d[1]);
    const uint8_t lat_min = (uint8_t)(d[2] * 10u + d[3]);
    const uint16_t lon = (uint16_t)(d[6] * 100u + d[7] * 10u + d[8]);
    const uint8_t lon_min = (uint8_t)(d[9] * 10u + d[10]);
    if (lat > 90u || lat_min > 59u || lon > 180u || lon_min > 59u) return false;
    if (lat == 90u && (lat_min | d[4] | d[5])) return false;
    if (lon == 180u && (lon_min | d[11] | d[12])) return false;
    return true;
}

/* A string centred in the right pane. */
static void pane(const char *s, uint8_t line, bool bold)
{
    (bold ? g.A->print_bold : g.A->print_normal)(s, PANE_X, 127u, line);
}

static void hint(const char *s, uint8_t y)
{
    g.A->print_tiny(s, PANE_X, y, false, true);
}

/* "06/48" capsule at the bottom of the pane: always 5 tiny characters. */
static void draw_ratio(uint8_t value, uint8_t total, uint8_t x)
{
    char text[6], *out = text;
    if (value < 10u) *out++ = '0';
    *put_ratio(out, value, total) = '\0';
    g.A->print_inverse(text, x, 6u, false, true, (uint8_t)(x + 20u));
}

static void draw_symbol(uint8_t symbol, uint8_t x)
{
    const uint16_t offset = (uint16_t)(SYM_BITMAPS + (uint16_t)symbol * SYM_BYTES);
    for (uint8_t page = 0u; page < 3u; page++)
        g.A->asset_read((uint16_t)(offset + page * SYM_W),
                        g.A->fb[2u + page] + x, SYM_W);
}

static void draw_menu(const char *assets)
{
    char count[6];
    const char *menu = assets + T_MENU;
    g.A->print_normal(menu + step(g.item, -1, MENU_COUNT) * T_MENU_STRIDE, 0u, 0u, 1u);
    g.A->print_string(menu + g.item * T_MENU_STRIDE, 0u, 0u, 2u, 8u);
    g.A->print_normal(menu + step(g.item, 1, MENU_COUNT) * T_MENU_STRIDE, 0u, 0u, 4u);
    *put_ratio(count, (uint8_t)(g.item + 1u), MENU_COUNT) = '\0';
    g.A->print_normal(count, 6u, 0u, 6u);
    g.A->draw_line(g.A->fb, INDEX_SEP_X, 0, INDEX_SEP_X, 55, true);
    for (uint8_t x = 0u; x < INDEX_SEP_X; x += 2u) g.A->fb[5][x] = 0x40u;
}

/* The path name, in bold while it is being edited (as the SSID and symbol). */
static void draw_path(const char *assets, uint8_t path, bool bold)
{
    /* The first hop never moves: "DIRECT", "WIDE1-1" or "ARISS" on line 2.
     * The two-hop path shows "WIDE1-1" there and adds its second hop on the
     * line below, read after the comma of its own name "WIDE1-1,WIDE2-1". */
    const char *paths = assets + T_PATHS;
    pane(paths + (path == PATH_WIDE2 ? 1u : path) * T_PATHS_STRIDE, 2u, bold);
    if (path == PATH_WIDE2) pane(paths + PATH_WIDE2 * T_PATHS_STRIDE + 8u, 3u, bold);
}

/* The comment in rows of the normal font. */
static void draw_text(void)
{
    char row[EDIT_CHARS + 1u];
    for (uint8_t line = 0u; line < EDIT_ROWS; line++) {
        const uint8_t start = (uint8_t)(line * EDIT_CHARS);
        uint8_t n = 0u;
        while (n < EDIT_CHARS && start + n < g.len) { row[n] = g.text[start + n]; n++; }
        row[n] = '\0';
        g.A->print_normal(row, PANE_X, 0u, line);
    }
}

/* A row of len characters centred on line 2 of the pane. The character under
 * the cursor is redrawn in bold; a cursor at len is an insertion mark below
 * the next cell; NO_CURSOR shows none. */
static void draw_row(const char *row, uint8_t len, uint8_t cursor)
{
    char one[2];
    const uint8_t start = (uint8_t)(PANE_X + (PANE_W - len * 7u) / 2u);
    const uint8_t x = (uint8_t)(start + cursor * 7u);
    g.A->print_normal(row, start, 0u, 2u);
    if (cursor < len) {
        one[0] = row[cursor]; one[1] = '\0';
        g.A->print_bold(one, x, 0u, 2u);
    } else if (cursor == len) {
        g.A->draw_line(g.A->fb, x, 23, (int16_t)(x + 5), 23, true);
    }
}

static void draw_coord(uint8_t first, uint8_t count, char hemisphere, bool edit)
{
    char row[12]; char *out = row; uint8_t bold = NO_CURSOR;
    const uint8_t *digits = g.profile + APRS_PROFILE_POS + first;
    for (uint8_t i = 0u; i < count; i++) {
        if (i == count - 4u) *out++ = ' ';
        if (i == count - 2u) *out++ = '.';
        if (edit && i == g.cursor) bold = (uint8_t)(out - row);
        *out++ = (char)('0' + digits[i]);
    }
    *out++ = ' '; *out++ = hemisphere; *out = '\0';
    draw_row(row, (uint8_t)(out - row), bold);
}

static void draw_comment(bool edit)
{
    draw_text();
    if (!edit) return;
    uint8_t line = 0u, col = text_cursor();
    while (col >= EDIT_CHARS) { col -= EDIT_CHARS; line++; }
    const int16_t x = (int16_t)(PANE_X + col * 7u), y = (int16_t)(line * 8u + 7u);
    g.A->draw_line(g.A->fb, x, y, (int16_t)(x + 5), y, true);
    draw_ratio(g.len, COMMENT_LIMIT, RATIO_LEFT_X);   /* level with the mode capsule */
}

/* The selected field in the right pane: the index shows it as stored, the
 * editor adds its cursor and hints. */
static void draw_field(const char *assets)
{
    char value[12]; char *out = value;
    const uint8_t *p = g.profile;
    const bool edit = g.screen != SCREEN_INDEX;
    draw_menu(assets);
    if (g.item == ITEM_CALL) {
        if (!g.len) pane(assets + T_AUTO, 2u, false);
        else {
            memcpy(value, g.text, g.len); value[g.len] = '\0';
            draw_row(value, g.len, edit ? text_cursor() : NO_CURSOR);
        }
    } else if (g.item == ITEM_LAT) {
        draw_coord(0u, LAT_DIGITS, (p[APRS_PROFILE_HEMI] & 1u) ? 'S' : 'N', edit);
    } else if (g.item == ITEM_LON) {
        draw_coord(LAT_DIGITS, APRS_POS_DIGITS - LAT_DIGITS,
                   (p[APRS_PROFILE_HEMI] & 2u) ? 'W' : 'E', edit);
    } else if (g.item == ITEM_PATH) {
        draw_path(assets, (uint8_t)(p[APRS_PROFILE_ROUTE] >> 4), edit);
    } else if (g.item == ITEM_COMMENT) {
        draw_comment(edit);
    } else {
        uint8_t line = 2u;
        if (g.item == ITEM_SSID) out = put_u8(out, (uint8_t)(p[APRS_PROFILE_ROUTE] & 15u));
        else {
            /* table/code pair on line 1, the icon on lines 2-4 (stored 4 px
             * down in its pages by gen_assets.py: y = 20-39); the editor
             * adds the position in the table as a capsule on line 6 */
            const uint8_t symbol = p[APRS_PROFILE_SYMBOL];
            if (edit) draw_ratio((uint8_t)(symbol + 1u), SYM_COUNT, RATIO_RIGHT_X);
            *out++ = assets[SYM_CODES + symbol * 2u];
            *out++ = assets[SYM_CODES + symbol * 2u + 1u];
            draw_symbol(symbol, PANE_SYM_X);
            line = 1u;
        }
        *out = '\0';
        pane(value, line, edit);
    }
    if (!edit) return;
    if (g.bad) hint(assets + T_INVALID, 48u);
    if (is_text())                           /* ABC / abc / 123 */
        g.A->print_inverse(assets + T_MODE_UP + g.mode * 4u, MODE_CAPS_X, 6u, false, true,
                           MODE_CAPS_X + 12u);
}

static void draw_presets(const char *assets)
{
    draw_menu(assets);
    for (uint8_t i = 0u; i < PRESET_COUNT; i++)
        pane(assets + T_PRESET_NAMES + i * T_PRESET_NAMES_STRIDE, (uint8_t)(i + 1u),
             i == g.preset);                 /* five rows centred on lines 1-5 */
}

static void draw(void)
{
    char assets[UI_SIZE] __attribute__((aligned(4)));
    const app_api_t *A = g.A;
    A->asset_read(0u, assets, UI_SIZE);
    A->display_clear(); A->status_clear();
    A->print_inverse(assets + T_TITLE, 2u, 0u, true, true,
                     (uint8_t)(2u + T_TITLE_CHARS * 4u));
    A->draw_battery();
    if (g.missing) {
        A->print_bold(assets + T_MISSING1, 0u, 127u, 2u);     /* full width: no menu here */
        A->print_normal(assets + T_MISSING2, 0u, 127u, 3u);
    } else if (g.screen == SCREEN_PRESET) draw_presets(assets);
    else {
        if (g.screen == SCREEN_INDEX) select_text();
        draw_field(assets);
    }
    A->blit_status(); A->blit_full();
}

static void commit_pending(void)
{
    g.pending = false; g.pending_key = APP_KEY_INVALID; g.cycle = 0u; g.pending_ticks = 0u;
}

/* The profile pads both texts with spaces and stores their length apart, so
 * the editor never writes a terminator. */
static bool insert_char(char c)
{
    if (g.len >= (g.item == ITEM_CALL ? APRS_CALL_MAX : COMMENT_LIMIT)) return false;
    for (uint8_t i = g.len; i > g.cursor; i--) g.text[i] = g.text[i - 1u];
    g.text[g.cursor++] = c; g.len++;
    return true;
}

static void sync_custom(void)
{
    if (g.item != ITEM_COMMENT) return;
    g.active_preset = 0u; g.custom_len = g.len; g.custom_cursor = g.cursor;
    memcpy(g.custom, g.text, g.len);
}

static void edit_number(uint8_t key, bool long_press)
{
    const bool digit = long_press || g.mode == 2u;
    if (g.item == ITEM_CALL && !digit && key < 2u) return;
    if (digit) {
        commit_pending(); if (insert_char((char)('0' + key))) sync_custom(); return;
    }
    char chars[KEYMAP_STRIDE];   /* NUL-padded: every entry is shorter than its stride */
    g.A->asset_read((uint16_t)(KEYMAP + key * KEYMAP_STRIDE), chars, KEYMAP_STRIDE);
    if (g.pending && g.pending_key == key) {
        if (!chars[++g.cycle]) g.cycle = 0u;
    } else {
        commit_pending();
        if (!insert_char(' ')) return;
        g.pending = true; g.pending_key = key; g.pending_pos = (uint8_t)(g.cursor - 1u);
    }
    char c = chars[g.cycle];
    if (g.item == ITEM_COMMENT && g.mode == 1u && c >= 'A' && c <= 'Z') c += 'a' - 'A';
    g.text[g.pending_pos] = c; g.pending_ticks = 0u; sync_custom();
}

static void erase_before_cursor(void)
{
    commit_pending(); if (!g.cursor) return; g.cursor--;
    for (uint8_t i = g.cursor; i + 1u < g.len; i++) g.text[i] = g.text[i + 1u];
    g.text[--g.len] = ' '; sync_custom();
}

static void open_editor(void)
{
    memcpy(g.backup, g.profile, APRS_PROFILE_LEN);
    g.screen = SCREEN_EDIT; g.bad = false; g.mode = 0u; g.cursor = 0u;
    select_text();
    /* a longer comment written by another tool is cut to what the editor shows */
    if (g.item == ITEM_COMMENT && g.len > COMMENT_LIMIT) {
        g.len = COMMENT_LIMIT;
        memset(g.text + COMMENT_LIMIT, ' ', APRS_COMMENT_MAX - COMMENT_LIMIT);
    }
    if (is_text()) g.cursor = g.len;
    sync_custom();
}

static void commit_editor(void)
{
    commit_pending();
    g.bad = (g.item == ITEM_LAT || g.item == ITEM_LON) && !position_ok();
    if (g.bad) return;
    if (is_text())
        g.profile[g.item == ITEM_CALL ? APRS_PROFILE_CALL_LEN : APRS_PROFILE_COMMENT_LEN] = g.len;
    g.changed = true; g.screen = SCREEN_INDEX;
}

static void apply_preset(void)
{
    memset(g.text, ' ', APRS_COMMENT_MAX);
    if (!g.preset) {
        g.len = g.custom_len; memcpy(g.text, g.custom, g.len); g.cursor = g.custom_cursor;
    } else {
        const uint8_t index = (uint8_t)(g.preset - 1u);
        g.A->asset_read((uint16_t)(P_PRESET_LENGTHS + index), &g.len, 1u);
        g.A->asset_read((uint16_t)(P_PRESETS + (uint16_t)index * APRS_COMMENT_MAX), g.text, g.len);
        g.cursor = g.len;
    }
    g.active_preset = g.preset; g.screen = SCREEN_EDIT;
}

static void index_key(uint8_t key)
{
    if (key == APP_KEY_MENU) open_editor();
    else if (key == APP_KEY_EXIT) {
        if (g.changed) g.A->cfg_save(g.profile, APRS_PROFILE_LEN);
        if (g.return_to_tx) g.A->handoff(APRS_TX_APP_NAME, NULL, 0u);
        g.running = false;
    } else g.item = step(g.item, g.A->nav_dir(key), MENU_COUNT);
}

static void text_key(uint8_t key)
{
    if (g.item == ITEM_COMMENT && key == APP_KEY_PTT) {
        commit_pending(); if (!g.active_preset) sync_custom();
        g.preset = g.active_preset; g.screen = SCREEN_PRESET;
    } else if (key == APP_KEY_STAR) {
        commit_pending();
        if (g.item == ITEM_CALL) g.mode = g.mode == 2u ? 0u : 2u;
        else if (++g.mode == 3u) g.mode = 0u;
    } else if (key == APP_KEY_F) erase_before_cursor();
    else if (key <= APP_KEY_9) edit_number(key, false);
    else {
        commit_pending();   /* the cursor wraps between both ends of the text */
        g.cursor = step(g.cursor, g.A->nav_dir(key), (uint8_t)(g.len + 1u));
    }
}

static void value_key(uint8_t key)
{
    int8_t d = g.A->nav_dir(key);
    uint8_t *p = g.profile;
    const bool number = key <= APP_KEY_9;
    if (g.item == ITEM_SSID) {
        const uint8_t route = p[APRS_PROFILE_ROUTE];
        const uint8_t ssid = number ? key : (uint8_t)((route + d) & 15u);
        p[APRS_PROFILE_ROUTE] = (uint8_t)((route & 0xF0u) | ssid);
    } else if (g.item == ITEM_PATH) {
        const uint8_t route = p[APRS_PROFILE_ROUTE];
        p[APRS_PROFILE_ROUTE] = (uint8_t)((step((uint8_t)(route >> 4), d, PATH_MAX + 1u) << 4) |
                                          (route & 15u));
    } else if (g.item == ITEM_SYMBOL) {
        p[APRS_PROFILE_SYMBOL] = number ? (uint8_t)(key * SYM_JUMP)
                                        : step(p[APRS_PROFILE_SYMBOL], d, SYM_COUNT);
    } else {
        const bool lat = g.item == ITEM_LAT;
        const uint8_t first = lat ? 0u : LAT_DIGITS;
        if (number) { p[APRS_PROFILE_POS + first + g.cursor] = key; g.bad = false; d = 1; }
        else if (key == APP_KEY_STAR) p[APRS_PROFILE_HEMI] ^= lat ? 1u : 2u;
        g.cursor = step(g.cursor, d, lat ? LAT_DIGITS : APRS_POS_DIGITS - LAT_DIGITS);
    }
}

static void edit_key(uint8_t key)
{
    if (key == APP_KEY_MENU) commit_editor();
    else if (key == APP_KEY_EXIT) {
        commit_pending(); memcpy(g.profile, g.backup, APRS_PROFILE_LEN);
        g.screen = SCREEN_INDEX;
    } else if (is_text()) text_key(key);
    else value_key(key);
}

static void preset_key(uint8_t key)
{
    if (key == APP_KEY_MENU) apply_preset();
    else if (key == APP_KEY_EXIT) g.screen = SCREEN_EDIT;
    else g.preset = step(g.preset, g.A->nav_dir(key), PRESET_COUNT);
}

static void short_key(uint8_t key)
{
    if (g.missing) { if (key == APP_KEY_EXIT) index_key(key); return; }
    if (g.screen == SCREEN_INDEX) index_key(key);
    else if (g.screen == SCREEN_PRESET) preset_key(key);
    else edit_key(key);
    g.dirty = true;
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    uint8_t block[APRS_PROFILE_LEN], backup[APRS_PROFILE_LEN];
    char custom[APRS_COMMENT_MAX];
    uint8_t request = 0u;
    uint32_t magic;
    g.A = api; g.profile = block; g.backup = backup; g.custom = custom;
    g.held_key = APP_KEY_INVALID; g.pending_key = APP_KEY_INVALID;
    g.running = true; g.dirty = true;
    const int16_t handoff_len = api->handoff_read(&request, sizeof request, NULL);
    g.return_to_tx = handoff_len >= 1 && request == APRS_HANDOFF_EDIT_RETURN;
    api->cfg_load(block, sizeof(block));
    memcpy(&magic, block, sizeof(magic));
    g.missing = magic != APRS_PROFILE_MAGIC ||
                block[APRS_PROFILE_COMMENT_LEN] > APRS_COMMENT_MAX ||
                block[APRS_PROFILE_CALL_LEN] > APRS_CALL_MAX ||
                (block[APRS_PROFILE_ROUTE] >> 4) > PATH_MAX ||
                block[APRS_PROFILE_SYMBOL] >= SYM_COUNT ||
                block[APRS_PROFILE_HEMI] > 3u || !position_ok();
    for (uint8_t i = 0u; !g.missing && i < block[APRS_PROFILE_CALL_LEN]; i++) {
        const uint8_t c = block[APRS_PROFILE_CALL + i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) g.missing = true;
    }
    api->backlight_on();
    while (g.running) {
        if (g.dirty) { g.dirty = false; draw(); }
        const uint8_t key = api->get_key();
        if (key == APP_KEY_SAVER || key == APP_KEY_WAKE) {
            g.held_key = APP_KEY_INVALID; g.held_ticks = 0u; g.long_fired = false; g.dirty = true;
        } else if (key == APP_KEY_INVALID) {
            if (g.held_key != APP_KEY_INVALID && !g.long_fired) short_key(g.held_key);
            g.held_key = APP_KEY_INVALID; g.held_ticks = 0u; g.long_fired = false;
        } else if (key != g.held_key) {
            g.held_key = key; g.held_ticks = 0u; g.long_fired = false;
            if (api->nav_dir(key)) { g.long_fired = true; short_key(key); }
        } else if (api->nav_dir(key)) {
            if (++g.held_ticks >= 32u && !(g.held_ticks & 7u)) short_key(key);
        } else if (!g.long_fired && !g.missing && g.screen == SCREEN_EDIT && is_text() &&
                   key <= APP_KEY_9 && ++g.held_ticks >= HOLD_TICKS) {
            g.long_fired = true; edit_number(key, true); g.dirty = true;
        }
        /* a multi-tap character settles: the cursor moves on past it */
        if (g.pending && ++g.pending_ticks >= COMMIT_TICKS) { commit_pending(); g.dirty = true; }
        api->battery_sample(); api->delay_ms(LOOP_MS); api->backlight_update();
    }
}
