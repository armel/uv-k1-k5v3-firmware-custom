/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
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
 * ChannelBook is a memory browser for the active Multiconfig bank.
 *
 * The app shows all channels, useful scan lists and channels outside a scan
 * list. First-character and channel-range directories plus direct number entry
 * keep channels quick to find. Selecting a memory is staged through API level
 * 4 and applied only after the overlay returns.
 */

#include <stdint.h>
#include <stdbool.h>
#include "../app_api.h"
#include "channelbook_assets.h"

#define GROUP_ALL     0u
#define GROUP_NONE    (APP_MR_LISTS + 1u)
#define GROUP_COUNT   (APP_MR_LISTS + 2u)
#define NO_CHANNEL    0xFFFFu

#define VIEW_GROUPS   0u
#define VIEW_CHANNELS 1u

#define CATALOG_SCAN  0u
#define CATALOG_ALPHA 1u
#define CATALOG_RANGE 2u
#define ALPHA_GROUPS 10u
#define RANGE_GROUPS 11u
#define RANGE_SIZE  100u
#define POSITION_MASK (APP_MR_COUNT - 1u)
#define ROW_UNIT       APP_MR_COUNT

#define GROUP_ROWS    5u
#define CHANNEL_ROWS  5u
#define FIRST_ROW     1u
#define FOOTER_LINE   6u
#define TINY_W        4u
#define VALUE_END_X  127u
#define CHANNEL_NAME_X  5u

#define INPUT_TICK_MS       15u
#define REPEAT_DELAY_MS     300u
#define REPEAT_MS            60u

#define CFG_MAGIC        0xC3u
#define CFG_CURSOR_MASK  0x1Fu
#define CFG_CATALOG_SHIFT 5u
#define CFG_VIEW_BIT     0x80u

_Static_assert((APP_MR_COUNT & POSITION_MASK) == 0u &&
               ROW_UNIT * CHANNEL_ROWS <= UINT16_MAX,
               "channel position packing requires a power-of-two memory count");
_Static_assert(ALPHA_GROUPS == CATALOG_ALPHA + 9u &&
               RANGE_GROUPS == CATALOG_RANGE + 9u,
               "directory count formula changed");
_Static_assert(ART_FOOT_GROUP_ALPHA == ART_FOOT_GROUP_SCAN + 128u &&
               ART_FOOT_GROUP_RANGE == ART_FOOT_GROUP_ALPHA + 128u,
               "group footers must remain contiguous");

typedef struct {
    uint16_t count[GROUP_COUNT];
    uint16_t alpha_count[ALPHA_GROUPS];
    uint8_t range_count[RANGE_GROUPS];
    uint8_t groups[GROUP_COUNT];
    uint8_t directory[RANGE_GROUPS];
    uint8_t directory_count;
} index_t;

static struct {
    const app_api_t *api;
    index_t *index;
    uint16_t selected;
    uint16_t top;
    uint16_t position;
    uint16_t scan_selected;
    uint32_t saved_cfg;
    uint8_t group_count;
    uint8_t group_selected;
    uint8_t group_top;
    uint8_t group;
    uint8_t catalog;
    uint8_t view;
    uint8_t input[4];
    uint8_t input_len;
    bool running;
} g;

_Static_assert(sizeof(index_t) == 122u && sizeof(g) == 32u,
               "ChannelBook RAM layout changed");

#define A (g.api)
#define I (*g.index)

/* One stub avoids repeating the far app_api_t member load at every call site. */
__attribute__((noinline))
static int memory_service(uint8_t op, uint16_t index, void *out)
{
    return A->mr(op, index, out);
}

__attribute__((noinline))
static char *put_u32(char *out, uint32_t value)
{
    return A->append_u32(out, value);
}

/* asset_read is far down the API table: keep one call site in the overlay. */
__attribute__((noinline))
static void asset(uint16_t offset, void *out, uint16_t length)
{
    A->asset_read(offset, out, length);
}

static uint8_t text_len(const char *text)
{
    uint8_t n = 0;
    while (text[n])
        n++;
    return n;
}

/* Frequency in 10 Hz units, with three to five decimals. */
static char *put_frequency(char *out, uint32_t frequency)
{
    const uint64_t qr = A->uidivmod(frequency, 100000u);
    uint32_t rest = (uint32_t)(qr >> 32);
    char digits[5];
    for (int8_t i = 4; i >= 0; i--) {
        const uint64_t d = A->uidivmod(rest, 10u);
        digits[i] = (char)('0' + (uint32_t)(d >> 32));
        rest = (uint32_t)d;
    }
    uint8_t n = 5;
    while (n > 3u && digits[n - 1u] == '0')
        n--;
    out = put_u32(out, (uint32_t)qr);
    *out++ = '.';
    for (uint8_t i = 0; i < n; i++)
        *out++ = digits[i];
    return out;
}

static char *put_group(char *out, uint8_t group)
{
    if (group == GROUP_ALL) {
        asset(T_ALL, out, T_ALL_LEN);
        return out + T_ALL_LEN - 1u;
    }
    if (group == GROUP_NONE) {
        asset(T_NONE, out, T_NONE_LEN);
        return out + T_NONE_LEN - 1u;
    }

    if (group < 10u)
        *out++ = '0';
    out = put_u32(out, group);
    *out++ = ' ';
    const int n = memory_service(APP_MR_LIST_NAME, group, out);
    if (n > 0) {
        out += n;
    } else {
        asset(T_LIST, out, T_LIST_LEN);
        out += T_LIST_LEN - 1u;
    }
    return out;
}

static void alpha_label(uint8_t group, char *out)
{
    asset((uint16_t)(A_ALPHA_GROUPS + group * ALPHA_LABEL_LEN),
          out, ALPHA_LABEL_LEN);
}

static uint8_t alpha_bucket(const char *name)
{
    uint8_t c = (uint8_t)name[0];
    if (!c)
        return ALPHA_GROUPS - 1u;
    c &= 0xDFu;
    if (c < 'A' || c > 'Z')
        return 0u;
    c -= 'A';
    if (c < 15u)
        return (uint8_t)(1u + ((uint16_t)c * 171u >> 9));
    if (c < 19u)
        return 6u;
    return c < 22u ? 7u : 8u;
}

static char *put_memory(char *out, uint16_t channel)
{
    const uint16_t number = (uint16_t)(channel + 1u);
    if (number < 1000u)
        *out++ = '0';
    if (number < 100u)
        *out++ = '0';
    if (number < 10u)
        *out++ = '0';
    return put_u32(out, number);
}

static char *put_range(char *out, uint8_t group)
{
    uint16_t first = (uint16_t)group * RANGE_SIZE + 1u;
    uint16_t last = first + RANGE_SIZE - 1u;
    if (last > APP_MR_COUNT)
        last = APP_MR_COUNT;
    out = put_memory(out, first - 1u);
    *out++ = '-';
    return put_memory(out, last - 1u);
}

static bool channel_in_group(uint16_t channel, uint8_t group)
{
    if (g.catalog == CATALOG_ALPHA) {
        app_mr_channel_t data;
        if (memory_service(APP_MR_READ, channel, &data) < 0)
            return false;
        return alpha_bucket(data.name) == group;
    }

    if (g.catalog == CATALOG_RANGE) {
        const uint16_t first = (uint16_t)group * RANGE_SIZE;
        return channel >= first && channel < first + RANGE_SIZE &&
               memory_service(APP_MR_READ, channel, NULL) >= 0;
    }

    const int list = memory_service(APP_MR_READ, channel, NULL);
    if (list < 0)
        return false;
    if (group == GROUP_ALL)
        return true;
    if (group == GROUP_NONE)
        return list == 0;
    return list == group || list == APP_MR_EVERY;
}

/* Next or previous member of a scan-list view, without wrapping. */
static uint16_t step_channel(uint16_t channel, int8_t direction, uint8_t group)
{
    int32_t n = channel == NO_CHANNEL
              ? (direction > 0 ? -1 : (int32_t)APP_MR_COUNT)
              : (int32_t)channel;
    for (n += direction; n >= 0 && n < (int32_t)APP_MR_COUNT; n += direction)
        if (channel_in_group((uint16_t)n, group))
            return (uint16_t)n;
    return NO_CHANNEL;
}

/* Count once at launch. An EVERY-LIST channel contributes to every counter,
 * but a list is shown only when named or explicitly used, avoiding 24 clones
 * of the same view. */
static void scan_lists(void)
{
    app_mr_channel_t data;
    uint32_t explicit_lists = 0;
    uint16_t every = 0;
    uint16_t channel = 0;
    uint16_t range_end = RANGE_SIZE;
    uint8_t range = 0;
    int list;

    for (; channel < APP_MR_COUNT; channel++) {
        list = memory_service(APP_MR_READ, channel, &data);
        if (channel == range_end) {
            range++;
            range_end += RANGE_SIZE;
        }
        if (list >= 0) {
            I.count[GROUP_ALL]++;
            if (list == APP_MR_EVERY) {
                every++;
            } else {
                const uint8_t group = list ? (uint8_t)list : GROUP_NONE;
                I.count[group]++;
                if (list > 0)
                    explicit_lists |= 1u << list;
            }
            I.alpha_count[alpha_bucket(data.name)]++;
            I.range_count[range]++;
        }
    }

    I.groups[g.group_count++] = GROUP_ALL;
    for (uint8_t i = 1; i <= APP_MR_LISTS; i++) {
        I.count[i] += every;
        const bool named = memory_service(APP_MR_LIST_NAME, i, NULL) > 0;
        if (I.count[i] && (named || (explicit_lists & (1u << i))))
            I.groups[g.group_count++] = i;
    }
    if (I.count[GROUP_NONE])
        I.groups[g.group_count++] = GROUP_NONE;
}

static uint8_t group_index(uint8_t group)
{
    for (uint8_t i = 0; i < g.group_count; i++)
        if (I.groups[i] == group)
            return i;
    return 0;
}

static void focus_group(uint8_t group)
{
    g.group_selected = group_index(group);
    if (g.group_selected < g.group_top)
        g.group_top = g.group_selected;
    else if (g.group_selected >= g.group_top + GROUP_ROWS)
        g.group_top = (uint8_t)(g.group_selected - GROUP_ROWS + 1u);
}

static uint8_t active_group(void)
{
    return g.catalog == CATALOG_SCAN
         ? g.group : I.directory[g.group_selected];
}

static uint16_t active_count(uint8_t group)
{
    if (g.catalog == CATALOG_ALPHA)
        return I.alpha_count[group];
    if (g.catalog == CATALOG_RANGE)
        return I.range_count[group];
    return I.count[group];
}

static uint8_t catalog_group_count(void)
{
    return g.catalog ? I.directory_count : g.group_count;
}

static void focus_catalog_group(uint8_t group)
{
    g.group_selected = 0u;
    while (I.directory[g.group_selected] != group)
        g.group_selected++;
    g.group_top = g.group_selected >= GROUP_ROWS
                ? g.group_selected - GROUP_ROWS + 1u : 0u;
}

static void build_directory(void)
{
    I.directory_count = 0u;
    const uint8_t count = g.catalog + 9u;
    for (uint8_t group = 0u; group < count; group++)
        if (active_count(group))
            I.directory[I.directory_count++] = group;
}

/* Position a channel and keep one preceding row visible when possible. */
static void place_channel(uint16_t channel)
{
    const uint8_t group = active_group();
    g.selected = channel;
    g.position = 0;
    uint16_t n = step_channel(NO_CHANNEL, 1, group);
    while (n != NO_CHANNEL && n != channel) {
        g.position++;
        n = step_channel(n, 1, group);
    }
    g.top = channel;
    n = step_channel(g.top, -1, group);
    if (n != NO_CHANNEL) {
        g.top = n;
        g.position += ROW_UNIT;
    }
}

static void open_group(uint8_t group, uint16_t preferred)
{
    if (g.catalog == CATALOG_SCAN) {
        g.group = group;
        focus_group(group);
    } else {
        focus_catalog_group(group);
    }
    if (!channel_in_group(preferred, group))
        preferred = step_channel(NO_CHANNEL, 1, group);
    place_channel(preferred);
    g.view = VIEW_CHANNELS;
}

static void move_group(int8_t direction)
{
    const uint8_t count = catalog_group_count();
    if (direction > 0) {
        g.group_selected++;
        if (g.group_selected >= count)
            g.group_selected = 0;
    } else if (g.group_selected) {
        g.group_selected--;
    } else {
        g.group_selected = count - 1u;
    }

    if (g.group_selected < g.group_top)
        g.group_top = g.group_selected;
    else if (g.group_selected >= g.group_top + GROUP_ROWS)
        g.group_top = (uint8_t)(g.group_selected - GROUP_ROWS + 1u);
}

static void move_channel(int8_t direction)
{
    const uint8_t group = active_group();
    const uint16_t count = active_count(group);
    uint16_t next = step_channel(g.selected, direction, group);
    if (next == NO_CHANNEL) {
        next = step_channel(NO_CHANNEL, direction, group);
        g.position = direction > 0 ? 0u : (uint16_t)(count - 1u);
        g.top = next;
        if (direction < 0) {
            for (uint8_t i = 1; i < CHANNEL_ROWS; i++) {
                const uint16_t previous = step_channel(g.top, -1, group);
                if (previous == NO_CHANNEL)
                    break;
                g.top = previous;
                g.position += ROW_UNIT;
            }
        }
    } else {
        g.position = (uint16_t)(g.position + direction);
        if (direction < 0) {
            if (g.position >= ROW_UNIT)
                g.position -= ROW_UNIT;
            else
                g.top = next;
        } else if (g.position < ROW_UNIT * (CHANNEL_ROWS - 1u)) {
            g.position += ROW_UNIT;
        } else {
            g.top = step_channel(g.top, 1, group);
        }
    }
    g.selected = next;
}

static void draw_shell(void)
{
    A->display_clear();
}

static uint8_t tiny_right_x(const char *text)
{
    return (uint8_t)(VALUE_END_X + 2u - text_len(text) * TINY_W);
}

static void draw_row(uint8_t line, const char *label, const char *value,
                     bool selected)
{
    const uint8_t value_x = tiny_right_x(value);
    if (selected)
        A->print_tiny(">", 1u, (uint8_t)(line * 8u + 2u), false, true);
    A->print_normal(label, CHANNEL_NAME_X, 0u, line);
    A->print_tiny(value, value_x, (uint8_t)(line * 8u + 2u), false, true);
}

static uint8_t draw_header_capsule(const char *label, uint8_t x)
{
    const uint8_t end = (uint8_t)(x + text_len(label) * TINY_W);
    A->print_inverse(label, x, 0u, false, true, end);
    return end;
}

static void draw_header(const char *label, const char *middle,
                        const char *value)
{
    const uint8_t label_end = draw_header_capsule(label, 2u);
    if (middle != NULL)
        draw_header_capsule(middle, (uint8_t)(label_end + 4u));
    draw_header_capsule(value,
                        (uint8_t)(VALUE_END_X - text_len(value) * TINY_W));
}

static void draw_status(void)
{
    A->status_clear();
    asset(ART_STATUS, A->status_line, ART_STATUS_LEN);
    A->draw_battery();
    A->blit_status();
}

static void draw_footer(uint16_t offset)
{
    asset(offset, A->fb[FOOTER_LINE], 128u);
}

static void draw_groups(void)
{
    char label[T_ALL_LEN], value[5], title[TEXT_MAX], *out;
    const uint8_t count = catalog_group_count();
    draw_shell();
    if (g.catalog == CATALOG_ALPHA) {
        asset(T_ALPHANUM_INDEX, title, T_ALPHANUM_INDEX_LEN);
    } else if (g.catalog == CATALOG_RANGE) {
        asset(T_MEM_NUM_INDEX, title, T_MEM_NUM_INDEX_LEN);
    } else {
        asset(T_SCAN_LISTS, title, T_SCAN_LISTS_LEN);
    }
    out = put_u32(value, I.count[GROUP_ALL]);
    *out = '\0';
    draw_header(title, NULL, value);

    for (uint8_t i = 0; i < GROUP_ROWS && g.group_top + i < count; i++) {
        const uint8_t index = g.group_top + i;
        const uint8_t group = g.catalog == CATALOG_SCAN
                            ? I.groups[index] : I.directory[index];
        if (g.catalog == CATALOG_ALPHA) {
            alpha_label(group, label);
        } else if (g.catalog == CATALOG_RANGE) {
            out = put_range(label, group);
            *out = '\0';
        } else {
            out = put_group(label, group);
            *out = '\0';
        }
        out = put_u32(value, active_count(group));
        *out = '\0';
        draw_row(FIRST_ROW + i, label, value, index == g.group_selected);
    }
    draw_footer((uint16_t)(ART_FOOT_GROUP_SCAN +
                           ((uint16_t)g.catalog << 7)));
}

static void draw_channels(void)
{
    char label[T_ALL_LEN], memory[5], value[12], *out;
    const uint8_t group = active_group();
    const uint16_t count = active_count(group);
    draw_shell();
    if (g.catalog == CATALOG_ALPHA) {
        alpha_label(group, label);
    } else if (g.catalog == CATALOG_RANGE) {
        out = put_range(label, group);
        *out = '\0';
    } else {
        out = put_group(label, group);
        *out = '\0';
    }
    out = put_memory(memory, g.selected);
    *out = '\0';
    out = put_u32(value, (g.position & POSITION_MASK) + 1u);
    *out++ = '/';
    out = put_u32(out, count);
    *out = '\0';
    draw_header(label, memory, value);

    uint16_t channel = g.top;
    for (uint8_t i = 0; i < CHANNEL_ROWS && channel != NO_CHANNEL; i++) {
        app_mr_channel_t data;
        const int list = memory_service(APP_MR_READ, channel, &data);
        if (list >= 0) {
            char fallback[T_CHANNEL_LEN];
            const char *name = data.name;
            if (!name[0]) {
                asset(T_CHANNEL, fallback, sizeof(fallback));
                name = fallback;
            }
            out = put_frequency(value, data.rx_frequency);
            *out = '\0';
            draw_row(FIRST_ROW + i, name, value, channel == g.selected);
        }
        channel = step_channel(channel, 1, group);
    }

    draw_footer((uint16_t)(ART_FOOT_LIST + ((uint16_t)g.catalog << 7)));
}

static uint16_t entered_channel(void)
{
    uint16_t number = 0;
    for (uint8_t i = 0; i < g.input_len; i++)
        number = (uint16_t)(number * 10u + g.input[i]);
    return number;
}

static uint16_t input_match(app_mr_channel_t *data)
{
    const uint16_t channel = entered_channel();
    if (!channel || memory_service(APP_MR_READ, channel - 1u, data) < 0)
        return NO_CHANNEL;
    return channel - 1u;
}

static void draw_search(void)
{
    app_mr_channel_t data;
    char prompt[8] = ">> ", fallback[T_NOT_FOUND_LEN], *out = prompt + 3;
    for (uint8_t i = 0; i < g.input_len; i++)
        *out++ = (char)('0' + g.input[i]);
    *out = '\0';

    draw_footer(ART_FOOT_SEARCH);
    A->print_tiny(prompt, 2u, 50u, false, true);
    const bool found = input_match(&data) != NO_CHANNEL;
    const char *result = data.name;
    if (!found || !result[0]) {
        const uint16_t offset = found ? T_CHANNEL : T_NOT_FOUND;
        const uint8_t length = found ? T_CHANNEL_LEN : T_NOT_FOUND_LEN;
        asset(offset, fallback, length);
        result = fallback;
    }
    A->print_tiny(result, tiny_right_x(result), 50u, false, true);
}

static void draw(void)
{
    if (g.view == VIEW_GROUPS)
        draw_groups();
    else
        draw_channels();
    if (g.input_len)
        draw_search();
    A->blit_full();
}

static void input_digit(uint8_t digit)
{
    if (g.input_len < sizeof(g.input))
        g.input[g.input_len++] = digit;
}

static void tune_selected(void)
{
    if (memory_service(APP_MR_STAGE, g.selected, NULL) == 0)
        g.running = false;
}

static void input_confirm(void)
{
    const uint16_t channel = input_match(NULL);
    if (channel == NO_CHANNEL)
        return;
    g.input_len = 0;
    g.catalog = CATALOG_SCAN;
    open_group(GROUP_ALL, channel);
}

static void toggle_catalog(void)
{
    if (g.catalog == CATALOG_SCAN) {
        g.scan_selected = g.selected;
        g.group = I.groups[g.group_selected];
    }
    if (++g.catalog <= CATALOG_RANGE) {
        build_directory();
        g.group_selected = g.group_top = 0u;
        return;
    }
    g.catalog = CATALOG_SCAN;
    g.selected = g.scan_selected;
    focus_group(g.group);
}

/* Map the physical navigation keys to visual row movement. */
static int8_t screen_direction(uint8_t key)
{
    return key == APP_KEY_UP ? -1 : key == APP_KEY_DOWN;
}

static void handle_key(uint8_t key, int8_t direction)
{
    if (g.input_len) {
        if (key <= APP_KEY_9)
            input_digit(key);
        else if (key == APP_KEY_MENU)
            input_confirm();
        else if (key == APP_KEY_EXIT || key == APP_KEY_STAR)
            g.input_len--;
        else if (key == APP_KEY_F)
            g.input_len = 0;
        return;
    }

    if (key <= APP_KEY_9) {
        input_digit(key);
        return;
    }

    if (direction) {
        if (g.view == VIEW_GROUPS)
            move_group(direction);
        else
            move_channel(direction);
        return;
    }

    if (key == APP_KEY_MENU) {
        if (g.view == VIEW_GROUPS) {
            if (g.catalog != CATALOG_SCAN) {
                open_group(I.directory[g.group_selected], g.selected);
            } else {
                open_group(I.groups[g.group_selected], g.selected);
            }
        } else {
            tune_selected();
        }
        return;
    }

    if (key == APP_KEY_F) {
        toggle_catalog();
        g.view = VIEW_GROUPS;
        return;
    }

    if (key == APP_KEY_EXIT) {
        if (g.view == VIEW_CHANNELS) {
            if (g.catalog == CATALOG_SCAN)
                focus_group(g.group);
            g.view = VIEW_GROUPS;
        } else {
            g.running = false;
        }
        return;
    }
}

static void load_position(void)
{
    uint32_t cfg;
    A->cfg_load((uint8_t *)&cfg, sizeof(cfg));
    const uint8_t mode = (uint8_t)(cfg >> 8);
    const uint8_t catalog = (mode >> CFG_CATALOG_SHIFT) & 3u;
    const uint8_t cursor = mode & CFG_CURSOR_MASK;
    const uint16_t selected = (uint16_t)(cfg >> 16);

    g.saved_cfg = cfg;

    if ((uint8_t)cfg != CFG_MAGIC || catalog > CATALOG_RANGE)
        goto invalid;

    g.group = GROUP_ALL;
    g.scan_selected = selected;
    if (catalog == CATALOG_SCAN) {
        if (I.groups[group_index(cursor)] != cursor)
            goto invalid;
        g.catalog = CATALOG_SCAN;
        open_group(cursor, selected);
        g.scan_selected = g.selected;
        if (!(mode & CFG_VIEW_BIT))
            g.view = VIEW_GROUPS;
        return;
    }

    g.catalog = catalog;
    build_directory();
    if (cursor >= catalog + 9u || !active_count(cursor))
        goto invalid;
    g.selected = selected;
    focus_catalog_group(cursor);
    if (mode & CFG_VIEW_BIT)
        open_group(cursor, selected);
    else
        g.view = VIEW_GROUPS;
    return;

invalid:
    g.saved_cfg = UINT32_MAX;
    g.catalog = CATALOG_SCAN;
    open_group(GROUP_ALL, step_channel(NO_CHANNEL, 1, GROUP_ALL));
    g.scan_selected = g.selected;
}

static void save_position(void)
{
    const uint8_t cursor = g.catalog == CATALOG_SCAN
                         ? (g.view == VIEW_CHANNELS
                          ? g.group : I.groups[g.group_selected])
                         : (g.view == VIEW_CHANNELS
                          ? active_group() : I.directory[g.group_selected]);
    const uint8_t mode = cursor | (uint8_t)(g.catalog << CFG_CATALOG_SHIFT) |
                         (uint8_t)(g.view << 7);
    const uint32_t cfg = CFG_MAGIC | (uint32_t)mode << 8 |
                         (uint32_t)g.selected << 16;
    if (cfg == g.saved_cfg)
        return;
    A->cfg_save((const uint8_t *)&cfg, sizeof(cfg));
}

static void empty_message(void)
{
    char line1[TEXT_MAX], line2[TEXT_MAX];
    asset(T_EMPTY, line1, T_EMPTY_LEN);
    asset(T_PROGRAM, line2, T_PROGRAM_LEN);
redraw:
    A->display_clear();
    for (uint8_t page = 0u; page < ALERT_PAGES; page++)
        asset((uint16_t)(ART_ALERT + page * ALERT_W),
              A->fb[page + 1u] + 8u, ALERT_W);
    A->print_bold(line1, 34u, VALUE_END_X, 2u);
    A->print_tiny(line2, 38u, 34u, false, true);
    draw_footer(ART_FOOT_EXIT);
    A->blit_full();
    for (;;) {
        const uint8_t key = A->get_key();
        if (key == APP_KEY_EXIT)
            return;
        if (key == APP_KEY_WAKE) {
            draw_status();
            goto redraw;
        }
        A->delay_ms(INPUT_TICK_MS);
        A->backlight_update();
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    index_t index;
    /* Keep the freestanding compiler from lowering this to an external memset. */
    for (uint8_t i = sizeof(index); i;)
        ((volatile uint8_t *)&index)[--i] = 0u;

    g.api = api;
    g.index = &index;
    A->backlight_on();
    draw_status();

    scan_lists();
    if (!I.count[GROUP_ALL]) {
        empty_message();
        return;
    }
    load_position();

    uint8_t previous = A->get_key();  /* ignore the key still held by the launcher */
    uint16_t repeat_ms = 0;
    uint8_t battery_ticks = 0;
    bool dirty = true;
    g.running = true;

    while (g.running) {
        const uint8_t key = A->get_key();
        const int8_t direction = screen_direction(key);

        if (key == APP_KEY_SAVER) {
            previous = APP_KEY_INVALID;
        } else if (key == APP_KEY_WAKE) {
            previous = APP_KEY_INVALID;
            draw_status();
            dirty = true;
        } else if (key == APP_KEY_INVALID) {
            previous = key;
        } else if (key != previous) {
            A->backlight_on();
            previous = key;
            repeat_ms = REPEAT_DELAY_MS;
            handle_key(key, direction);
            dirty = true;
        } else if (direction) {
            if (repeat_ms > INPUT_TICK_MS) {
                repeat_ms -= INPUT_TICK_MS;
            } else {
                handle_key(key, direction);
                repeat_ms = REPEAT_MS;
                dirty = true;
            }
        }

        if (dirty && key != APP_KEY_SAVER) {
            draw();
            dirty = false;
        }
        A->delay_ms(INPUT_TICK_MS);
        if (++battery_ticks >= 200u) {
            battery_ticks = 0;
            A->battery_sample();
            if (key != APP_KEY_SAVER)
                draw_status();
        }
        A->backlight_update();
    }

    save_position();
}
