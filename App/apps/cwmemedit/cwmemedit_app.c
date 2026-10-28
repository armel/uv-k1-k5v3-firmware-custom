/* Copyright 2026 Johan Denoyer F4WAT
 * https://github.com/jdenoy
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

/*
 * CW Mem Edit - companion editor for CW Mem's user block (cwmem_user.h):
 * the callsign and the custom messages of keys 7, 8 and 9. The block is read
 * and written through the configuration proxy (descriptor at asset offset
 * zero); the loader commits it after EXIT. Text entry follows APRS Edit:
 * multi-tap on 0 to 9, a long press types the digit, UP / DOWN move the
 * cursor, F erases. Opened from CW Mem (key 0), EXIT goes back to it with
 * the keyer settings it handed over.
 */

#include <stdbool.h>
#include <stdint.h>
#include "../app_api.h"
#include "../cwmem/cwmem_user.h"
#include "cwmemedit_assets.h"

#define LOOP_MS       10u
#define HOLD_TICKS    40u   /* long press: the digit itself */
#define COMMIT_TICKS  80u   /* a multi-tap character settles */
#define ROW_CHARS     16u   /* normal-font characters per edit row */
#define EDIT_X         8u
#define ITEM_COUNT     4u   /* CALL, 7, 8, 9 */

enum { SCREEN_INDEX = 0, SCREEN_EDIT };

static struct {
    const app_api_t *api;
    uint8_t *block, *backup;
    char *text;                  /* the field being edited, inside block */
    uint8_t item, screen, len, max, cursor;
    uint8_t pendingKey, pendingPos, cycle, heldKey;
    uint16_t pendingTicks, heldTicks;
    int16_t handoffLen;
    bool pending, longFired, dirty, changed, running, missing;
} g;

#define A (g.api)

static void copy(void *dst, const void *src, uint8_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
}

static char *field(uint8_t item)
{
    return (char *)g.block + (item ? MEMU_MSG + (item - 1u) * MEMU_MSG_STRIDE
                                   : MEMU_CALL);
}

static uint8_t fieldMax(uint8_t item)
{
    return item ? MEMU_MSG_STRIDE - 1u : MEMU_CALL_MAX;
}

static uint8_t fieldLen(uint8_t item)
{
    const char *t = field(item);
    const uint8_t max = fieldMax(item);
    uint8_t n = 0u;
    while (n < max && t[n])
        n++;
    return n;
}

static void commitPending(void)
{
    g.pending = false;
    g.pendingTicks = 0u;
    g.cycle = 0u;
}

/* Fields are NUL-padded: an insertion shifts the padding right. */
static bool insertChar(char c)
{
    if (g.len >= g.max)
        return false;
    for (uint8_t i = g.len; i > g.cursor; i--)
        g.text[i] = g.text[i - 1u];
    g.text[g.cursor++] = c;
    g.len++;
    return true;
}

static void eraseBeforeCursor(void)
{
    commitPending();
    if (!g.cursor)
        return;
    g.cursor--;
    for (uint8_t i = g.cursor; i + 1u < g.len; i++)
        g.text[i] = g.text[i + 1u];
    g.text[--g.len] = '\0';
}

static void editNumber(uint8_t key, bool longPress)
{
    if (longPress) {
        commitPending();
        insertChar((char)('0' + key));
        return;
    }
    char chars[KEYMAP_STRIDE];
    A->asset_read((uint16_t)(KEYMAP + key * KEYMAP_STRIDE), chars, KEYMAP_STRIDE);
    if (g.pending && g.pendingKey == key) {
        if (++g.cycle >= KEYMAP_STRIDE || !chars[g.cycle])
            g.cycle = 0u;
    } else {
        commitPending();
        if (!insertChar(' '))
            return;
        g.pending = true;
        g.pendingKey = key;
        g.pendingPos = (uint8_t)(g.cursor - 1u);
    }
    g.text[g.pendingPos] = chars[g.cycle];
    g.pendingTicks = 0u;
}

/* The boot message's callsign (LINE 1), at most MEMU_CALL_MAX characters;
 * returns its length, 0 when LINE 1 holds none. */
static uint8_t bootCall(char *out)
{
    char call[MEMU_CALL_MAX + 2u];
    A->boot_callsign(call, sizeof(call));
    uint8_t n = 0u;
    while (n < MEMU_CALL_MAX && call[n]) {
        out[n] = call[n];
        n++;
    }
    out[n] = '\0';
    return n;
}

static void openEditor(void)
{
    copy(g.backup, g.block, MEMU_LEN);
    g.text = field(g.item);
    g.max = fieldMax(g.item);
    g.len = fieldLen(g.item);
    /* An empty callsign starts from LINE 1, the one CW Mem sends. */
    if (!g.item && !g.len) {
        char call[MEMU_CALL_MAX + 1u];
        g.len = bootCall(call);
        copy(g.text, call, g.len);
    }
    g.cursor = g.len;
    g.screen = SCREEN_EDIT;
    commitPending();
}

static void indexKey(uint8_t key)
{
    const int8_t d = A->nav_dir(key);
    if (d) {
        g.item = (uint8_t)((g.item + ITEM_COUNT + d) & (ITEM_COUNT - 1u));
    } else if (key == APP_KEY_MENU && !g.missing) {
        openEditor();
    } else if (key == APP_KEY_EXIT) {
        if (g.changed)
            A->cfg_save(g.block, MEMU_LEN);
        g.running = false;
    }
}

static void editKey(uint8_t key)
{
    const int8_t d = A->nav_dir(key);
    if (key <= APP_KEY_9) {
        editNumber(key, false);
    } else if (d) {
        /* The cursor wraps between both ends of the text. */
        commitPending();
        g.cursor = d > 0 ? (g.cursor >= g.len ? 0u : g.cursor + 1u)
                         : (g.cursor ? g.cursor - 1u : g.len);
    } else if (key == APP_KEY_F) {
        eraseBeforeCursor();
    } else if (key == APP_KEY_MENU) {
        commitPending();
        g.changed = true;
        g.screen = SCREEN_INDEX;
    } else if (key == APP_KEY_EXIT) {
        copy(g.block, g.backup, MEMU_LEN);
        g.screen = SCREEN_INDEX;
    }
}

static void shortKey(uint8_t key)
{
    if (g.screen == SCREEN_EDIT)
        editKey(key);
    else
        indexKey(key);
    g.dirty = true;
}

static void drawStatus(void)
{
    char t[TEXT_MAX];
    A->status_clear();
    A->asset_read(T_TITLE, t, TEXT_MAX);
    A->print_inverse(t, 2u, 0u, true, true, (uint8_t)(2u + T_TITLE_CHARS * 4u));
    A->asset_read(T_VERSION, t, TEXT_MAX);
    A->print_tiny(t, 92u, 1u, true, false);
    A->draw_battery();
}

/* "CALL F4WAT" / "7 UR RST 599": the label, then the text or LINE 1. */
static void drawItem(uint8_t item, uint8_t line, bool selected)
{
    char s[TEXT_MAX + MEMU_MSG_STRIDE];
    A->asset_read((uint16_t)(T_ITEM + item * T_ITEM_STRIDE), s, TEXT_MAX);
    char *out = s;
    while (*out)
        out++;
    const uint8_t n = fieldLen(item);
    if (n) {
        copy(out, field(item), n);
        out[n] = '\0';
    } else if (!item) {
        /* No callsign of its own: CW Mem sends LINE 1's, if any. */
        const uint8_t b = bootCall(out);
        A->asset_read(b ? T_LINE1 : T_NOCALL, out + b, TEXT_MAX);
    }
    /* The capsule starts 2 px left of x: x below 2 would wrap. */
    if (selected)
        A->print_inverse(s, 2u, line, false, true, 127u);
    else
        A->print_tiny(s, 2u, (uint8_t)(line * 8u + 1u), false, true);
}

static void hint(uint16_t offset, uint8_t y)
{
    char t[TEXT_MAX];
    A->asset_read(offset, t, TEXT_MAX);
    A->print_tiny(t, 2u, y, false, true);
}

static void draw(void)
{
    A->display_clear();
    drawStatus();
    if (g.missing) {
        char t[TEXT_MAX];
        A->asset_read(T_MISSING, t, TEXT_MAX);
        A->print_normal(t, 0u, 127u, 3u);
    } else if (g.screen == SCREEN_INDEX) {
        for (uint8_t i = 0u; i < ITEM_COUNT; i++)
            drawItem(i, (uint8_t)(1u + i), i == g.item);
        hint(T_HINT_INDEX, 49u);
    } else {
        drawItem(g.item, 1u, true);
        /* The text in two rows of ROW_CHARS, the cursor underlined. */
        char row[ROW_CHARS + 1u];
        for (uint8_t r = 0u; r < 2u; r++) {
            const uint8_t start = (uint8_t)(r * ROW_CHARS);
            uint8_t n = g.len > start ? (uint8_t)(g.len - start) : 0u;
            if (n > ROW_CHARS)
                n = ROW_CHARS;
            copy(row, g.text + start, n);
            row[n] = '\0';
            A->print_normal(row, EDIT_X, 0u, (uint8_t)(3u + r));
        }
        const bool second = g.cursor >= ROW_CHARS;
        const uint8_t x = (uint8_t)(EDIT_X +
            (second ? g.cursor - ROW_CHARS : g.cursor) * 7u);
        const int16_t y = second ? 39 : 31;
        A->draw_line(A->fb, x, y, (int16_t)(x + 5), y, true);
        hint(T_HINT_EDIT1, 41u);
        hint(T_HINT_EDIT2, 49u);
    }
    A->blit_status();
    A->blit_full();
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    uint8_t block[MEMU_LEN], backup[MEMU_LEN];
    uint8_t settings[APP_HANDOFF_MAX];
    uint32_t magic;
    g.api = api;
    g.block = block;
    g.backup = backup;
    g.heldKey = APP_KEY_INVALID;
    g.pendingKey = APP_KEY_INVALID;
    g.running = true;
    g.dirty = true;
    /* From CW Mem: its keyer settings, handed back unchanged on EXIT. */
    g.handoffLen = api->handoff_read(settings, sizeof(settings), 0);
    api->cfg_load(block, MEMU_LEN);
    copy(&magic, block, sizeof(magic));
    g.missing = magic != MEMU_MAGIC;
    for (uint8_t i = 1u; i < ITEM_COUNT; i++)
        field(i)[MEMU_MSG_STRIDE - 1u] = '\0';
    api->backlight_on();

    while (g.running) {
        if (g.dirty) {
            g.dirty = false;
            draw();
        }
        const uint8_t key = api->get_key();
        if (key == APP_KEY_SAVER || key == APP_KEY_WAKE) {
            g.heldKey = APP_KEY_INVALID;
            g.heldTicks = 0u;
            g.longFired = false;
            g.dirty = true;
        } else if (key == APP_KEY_INVALID) {
            /* Keys act on release, so a long press never adds a tap. */
            if (g.heldKey != APP_KEY_INVALID && !g.longFired)
                shortKey(g.heldKey);
            g.heldKey = APP_KEY_INVALID;
            g.heldTicks = 0u;
            g.longFired = false;
        } else if (key != g.heldKey) {
            g.heldKey = key;
            g.heldTicks = 0u;
            g.longFired = false;
            if (api->nav_dir(key)) {
                g.longFired = true;
                shortKey(key);
            }
        } else if (api->nav_dir(key)) {
            if (++g.heldTicks >= 32u && !(g.heldTicks & 7u))
                shortKey(key);
        } else if (!g.longFired && g.screen == SCREEN_EDIT &&
                   key <= APP_KEY_9 && ++g.heldTicks >= HOLD_TICKS) {
            g.longFired = true;
            editNumber(key, true);
            g.dirty = true;
        }
        if (g.pending && ++g.pendingTicks >= COMMIT_TICKS) {
            commitPending();
            g.dirty = true;
        }
        api->battery_sample();
        api->delay_ms(LOOP_MS);
        api->backlight_update();
    }

    if (g.handoffLen > 0)
        api->handoff(CW_MEM_APP_NAME, settings, (uint8_t)g.handoffLen);
}
