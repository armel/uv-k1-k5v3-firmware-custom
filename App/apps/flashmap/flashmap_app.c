/* Copyright 2026 Armel F4HWN
 * Licensed under the Apache License, Version 2.0.
 *
 * Read-only physical map of the external SPI flash. Firmware and app names
 * are taken directly from their on-flash headers; no resident state is changed.
 * The multiboot state marks the running firmware slot, the active config
 * bank and the current state sector LIVE.
 * MENU dumps the zone on the top card (hex or text, STAR toggles); in the
 * dump, UP/DOWN page through it and 0..9 jump to tenths of the zone.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "flashmap_assets.h"

#define VISIBLE_ROWS        7u
#define ROW_HEIGHT          8u
#define CARD_ROWS           3u
#define CARD_HEIGHT         (CARD_ROWS * ROW_HEIGHT)
#define SCROLL_FRAME_STEP   2u
#define INPUT_TICK_MS       15u
#define NAV_REPEAT_DELAY_MS 300u
#define NAV_REPEAT_MS       120u
#define DESC_MAX            32u

#define FW_SLOT_FIRST       6u
#define FW_SLOT_COUNT       5u
#define CONFIG_FIRST        (FW_SLOT_FIRST + FW_SLOT_COUNT)
#define CONFIG_COUNT        4u
#define STATE_FIRST         (CONFIG_FIRST + CONFIG_COUNT)
#define APP_SLOT_FIRST      (STATE_FIRST + 2u)
#define APP_SLOT_COUNT      48u
#define TAIL_FIRST          (APP_SLOT_FIRST + APP_SLOT_COUNT)
#define ENTRY_COUNT         (TAIL_FIRST + 2u)
#define ROW_COUNT           (ENTRY_COUNT * CARD_ROWS)
#define NO_ENTRY            0xFFu

#define FW_SLOT_BASE        0x00020000u
#define FW_SLOT_STRIDE      0x00020000u
#define FW_MAGIC            0x31424D46u
#define FW_HEADER_VERSION   1u
#define FW_FLAG_COMMITTED   0x0001u
#define FW_IMAGE_MAX        0x0001D800u

#define APP_REGION_BASE     0x00102000u
#define APP_SLOT_STRIDE     0x00002000u
#define APP_MAGIC           0x31504146u
#define APP_HEADER_VERSION  1u
#define APP_FLAG_COMMITTED  0x0001u
#define APP_CODE_MAX        0x00001000u

#define STATE_BASE          0x00100000u
#define STATE_SECTOR        0x00001000u
#define STATE_MAGIC_LEGACY  0x31504D46u   /* "FMP1" */
#define STATE_MAGIC_V2      0x32504D46u   /* "FMP2" (slot == config bank) */
#define STATE_MAGIC         0x33504D46u   /* "FMP3" */
#define BANK_COUNT          5u

#define DUMP_LINES          8u
#define DUMP_HEX_BYTES      8u            /* bytes per line in hex view  */
#define DUMP_TEXT_BYTES     16u           /* bytes per line in text view */
#define DUMP_LINE_HEIGHT    6u

typedef struct {
    uint32_t magic;
    uint16_t hdr_version;
    uint16_t flags;
    uint32_t image_size;
    uint32_t image_crc32;
    char name[16];
    char version[16];
    uint8_t reserved[16];
} fw_header_t;

typedef struct {
    uint32_t magic;
    uint16_t hdr_version;
    uint8_t abi_major;
    uint8_t api_min;
    uint32_t code_size;
    uint32_t code_crc32;
    uint16_t entry_off;
    uint16_t flags;
    char name[16];
    char version[16];
    uint32_t link_vma;
    uint32_t required_caps;
    uint16_t asset_size;
    uint16_t asset_crc;
} app_header_t;

/* Multiboot active-state record (mb_state_t in driver/mb_flash.h). */
typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t firmware_slot;
    uint8_t slot_inv;
    uint8_t config_bank;
    uint8_t bank_inv;
    uint32_t state_crc32;
} state_record_t;

typedef union {
    fw_header_t fw;
    app_header_t app;
} slot_header_t;

_Static_assert(sizeof(fw_header_t) == 64u && offsetof(fw_header_t, name) == 16u,
               "firmware header layout changed");
_Static_assert(sizeof(app_header_t) == 64u && offsetof(app_header_t, name) == 20u,
               "app header layout changed");
_Static_assert(sizeof(state_record_t) == 24u &&
               offsetof(state_record_t, state_crc32) == 20u,
               "multiboot state layout changed");

/* State sector verdicts, ordered by preference (also T_STATE indices). */
enum { MARK_MISSING = 0, MARK_CORRUPT, MARK_LEGACY, MARK_VALID };

struct globals {
    const app_api_t *api;
    uint32_t dump_start;
    uint32_t dump_end;
    uint32_t dump_addr;
    uint16_t scroll;
    uint16_t target;
    bool running;
    bool dirty;
    bool suspended;
    bool function_armed;
    bool dump;
    bool dump_text;
    uint8_t dump_entry;
    uint8_t live_fw;
    uint8_t live_cfg;
    uint8_t live_state;
};
static struct globals g;
#define A (g.api)

static const uint32_t first_starts[] = {
    0x000000u, 0x010000u, 0x010200u, 0x011000u, 0x012000u, 0x018000u,
};

static const uint32_t first_ends[] = {
    0x00FFFFu, 0x0101FFu, 0x010FFFu, 0x011FFFu, 0x017FFFu, 0x01FFFFu,
};

/* Entry shown at the top for keys 0..9: 1..9 in flash order, 0 the end. */
static const uint8_t section_entries[10] = {
    TAIL_FIRST + 1u,                    /* SONIC 4K */
    0u, 1u, 3u, 5u,                     /* CONFIG 0, CALIBRATION, LOGO, LOG */
    FW_SLOT_FIRST, CONFIG_FIRST, STATE_FIRST, APP_SLOT_FIRST, TAIL_FIRST,
};

static char hex_digit(uint8_t value)
{
    if (value < 10u)
        return (char)('0' + value);
    return (char)('A' + value - 10u);
}

static char *put_hex6(char *out, uint32_t address)
{
    for (int8_t shift = 20; shift >= 0; shift -= 4)
        *out++ = hex_digit((uint8_t)(address >> shift) & 0x0Fu);
    *out = '\0';
    return out;
}

/* The resident formatter (API level 3). One local stub keeps every call a
 * plain bl: the service lies past the ldr immediate range of the table. */
__attribute__((noinline))
static char *put_u32(char *out, uint32_t value)
{
    out = A->append_u32(out, value);
    *out = '\0';
    return out;
}

static uint8_t text_length(const char *text)
{
    uint8_t length = 0u;
    while (text[length] != '\0')
        length++;
    return length;
}

static uint32_t crc32(const uint8_t *data, uint8_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (length-- != 0u) {
        crc ^= *data++;
        for (uint8_t bit = 0u; bit < 8u; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void entry_range(uint16_t entry, uint32_t *start, uint32_t *end)
{
    if (entry < FW_SLOT_FIRST) {
        *start = first_starts[entry];
        *end = first_ends[entry];
    } else if (entry < CONFIG_FIRST) {
        *start = FW_SLOT_BASE + (uint32_t)(entry - FW_SLOT_FIRST) * FW_SLOT_STRIDE;
        *end = *start + FW_SLOT_STRIDE - 1u;
    } else if (entry < STATE_FIRST) {
        *start = 0x0C0000u + (uint32_t)(entry - CONFIG_FIRST) * 0x00010000u;
        *end = *start + 0x0000FFFFu;
    } else if (entry < APP_SLOT_FIRST) {
        *start = STATE_BASE + (uint32_t)(entry - STATE_FIRST) * STATE_SECTOR;
        *end = *start + STATE_SECTOR - 1u;
    } else if (entry < TAIL_FIRST) {
        *start = APP_REGION_BASE + (uint32_t)(entry - APP_SLOT_FIRST) * APP_SLOT_STRIDE;
        *end = *start + APP_SLOT_STRIDE - 1u;
    } else if (entry == TAIL_FIRST) {
        *start = 0x162000u;
        *end = 0x1DDFFFu;
    } else {
        *start = 0x1DE000u;
        *end = 0x1FFFFFu;
    }
}

static void format_range(char *out, uint32_t start, uint32_t end)
{
    *out++ = '0';
    *out++ = 'x';
    out = put_hex6(out, start);
    *out++ = '-';
    *out++ = '0';
    *out++ = 'x';
    put_hex6(out, end);
}

static void format_size(char *out, uint32_t start, uint32_t end)
{
    const uint32_t bytes = end - start + 1u;
    if (bytes < 1024u) {
        out = put_u32(out, bytes);
        *out++ = ' ';
        *out++ = 'B';
        *out = '\0';
        return;
    }
    out = put_u32(out, bytes >> 10);
    if ((bytes & 0x03FFu) != 0u) {
        *out++ = '.';
        *out++ = (char)('0' + (((bytes & 0x03FFu) * 10u) >> 10));
    }
    *out++ = ' ';
    *out++ = 'K';
    *out++ = 'B';
    *out = '\0';
}

static void asset_text(uint16_t offset, char *out, uint16_t length)
{
    A->asset_read(offset, out, length);
    out[length - 1u] = '\0';
}

static char *append_asset(char *out, uint16_t offset, uint16_t length)
{
    asset_text(offset, out, length);
    return out + text_length(out);
}

static char *put_slot(char *out, bool firmware, uint8_t slot)
{
    if (firmware) {
        *out++ = 'F';
        *out++ = 'W';
        *out++ = (char)('0' + slot);
    } else {
        const uint8_t shown_slot = (uint8_t)(slot + 1u);
        const uint64_t qr = A->uidivmod(shown_slot, 10u);
        *out++ = 'A';
        *out++ = (char)('0' + (uint8_t)qr);
        *out++ = (char)('0' + (uint8_t)(qr >> 32));
    }
    *out++ = ' ';
    *out = '\0';
    return out;
}

static bool header_is_empty(uint32_t magic)
{
    return magic == 0xFFFFFFFFu || magic == 0u;
}

static bool read_fw_header(uint8_t slot, fw_header_t *header)
{
    A->sys_storage_read(FW_SLOT_BASE + (uint32_t)slot * FW_SLOT_STRIDE,
                        header, sizeof(*header));
    return header->magic == FW_MAGIC &&
           header->hdr_version == FW_HEADER_VERSION &&
           (header->flags & FW_FLAG_COMMITTED) != 0u &&
           header->image_size != 0u && header->image_size <= FW_IMAGE_MAX;
}

static bool read_app_header(uint8_t slot, app_header_t *header)
{
    A->sys_storage_read(APP_REGION_BASE + (uint32_t)slot * APP_SLOT_STRIDE,
                        header, sizeof(*header));
    return header->magic == APP_MAGIC &&
           header->hdr_version == APP_HEADER_VERSION &&
           (header->flags & APP_FLAG_COMMITTED) != 0u &&
           header->code_size != 0u && header->code_size <= APP_CODE_MAX;
}

/* Copy the printable prefix of a header string; returns the new end. */
static char *copy_printable(char *out, const char *text, uint8_t maximum)
{
    uint8_t count = 0u;
    while (count < maximum) {
        const uint8_t ch = (uint8_t)text[count];
        if (ch < 0x20u || ch > 0x7Eu)
            break;
        out[count++] = (char)ch;
    }
    out[count] = '\0';
    return out + count;
}

static void format_slot(uint8_t slot, bool firmware, char *out)
{
    slot_header_t header;
    const bool valid = firmware ? read_fw_header(slot, &header.fw)
                                : read_app_header(slot, &header.app);
    char *name = put_slot(out, firmware, slot);

    if (!valid) {
        const uint8_t state = header_is_empty(header.fw.magic) ? 0u : 1u;
        asset_text(T_HEADER_STATE + state * T_HEADER_STATE_STRIDE,
                   name, T_HEADER_STATE_STRIDE);
    } else if (copy_printable(name, firmware ? header.fw.name : header.app.name,
                              10u) == name) {
        asset_text(T_HEADER_STATE + 2u * T_HEADER_STATE_STRIDE,
                   name, T_HEADER_STATE_STRIDE);
    }
}

static char *put_kb(char *out, uint32_t bytes)
{
    out = put_u32(out, (bytes + 1023u) >> 10);
    *out++ = 'K';
    *out++ = 'B';
    *out = '\0';
    return out;
}

/* "6.0.1 97/118 KB": header version and image size against the slot limit. */
static void format_fw_description(uint8_t slot, char *out)
{
    fw_header_t header;
    if (!read_fw_header(slot, &header)) {
        asset_text(T_DESC + 6u * T_DESC_STRIDE, out, T_DESC_STRIDE);
        return;
    }
    char *tail = copy_printable(out, header.version, 10u);
    if (tail != out)
        *tail++ = ' ';
    tail = put_u32(tail, (header.image_size + 1023u) >> 10);
    asset_text(T_FW_MAX, tail, T_FW_MAX_LEN);
}

/* "1.13: 4KB code, 1KB assets" */
static void format_app_description(uint8_t slot, char *out)
{
    app_header_t header;
    if (!read_app_header(slot, &header)) {
        asset_text(T_DESC + 8u * T_DESC_STRIDE, out, T_DESC_STRIDE);
        return;
    }
    char *tail = copy_printable(out, header.version, 6u);
    if (tail != out) {
        *tail++ = ':';
        *tail++ = ' ';
    }
    tail = put_kb(tail, header.code_size);
    tail = append_asset(tail, T_UNIT, T_UNIT_STRIDE);
    if (header.asset_size != 0u) {
        *tail++ = ',';
        *tail++ = ' ';
        tail = put_kb(tail, header.asset_size);
        append_asset(tail, T_UNIT + T_UNIT_STRIDE, T_UNIT_STRIDE);
    }
}

/* Same checks as the resident MB_ReadActiveState, minus the SPI error path. */
static uint8_t read_state(uint8_t sector, state_record_t *record)
{
    const uint8_t *raw = (const uint8_t *)record;
    A->sys_storage_read(STATE_BASE + (uint32_t)sector * STATE_SECTOR,
                        record, sizeof(*record));
    if (record->magic == 0xFFFFFFFFu)
        return MARK_MISSING;
    if (record->magic == STATE_MAGIC_LEGACY) {
        /* FMP1: { magic, index, ~index }, one index for slot and bank. */
        record->firmware_slot = raw[4];
        record->config_bank = raw[4];
        return (uint8_t)~raw[5] == raw[4] && raw[4] < BANK_COUNT
            ? MARK_LEGACY : MARK_CORRUPT;
    }

    const bool version2 = record->magic == STATE_MAGIC_V2;
    if ((!version2 && record->magic != STATE_MAGIC) ||
        (uint8_t)~record->slot_inv != record->firmware_slot ||
        record->firmware_slot >= BANK_COUNT ||
        record->image_size == 0u || record->image_size > FW_IMAGE_MAX ||
        crc32(raw, offsetof(state_record_t, state_crc32)) != record->state_crc32)
        return MARK_CORRUPT;
    if (version2)
        record->config_bank = record->firmware_slot;
    else if ((uint8_t)~record->bank_inv != record->config_bank ||
             record->config_bank >= BANK_COUNT)
        return MARK_CORRUPT;
    return MARK_VALID;
}

/* "Gen 42: FW1, CONFIG 1", "Legacy: FW0, CONFIG 0", "Erased"... */
static void format_state_description(uint8_t sector, char *out)
{
    state_record_t record;
    const uint8_t mark = read_state(sector, &record);
    char *tail = append_asset(out, T_STATE + mark * T_STATE_STRIDE,
                              T_STATE_STRIDE);
    if (mark < MARK_LEGACY)
        return;
    if (mark == MARK_VALID)
        tail = put_u32(tail, record.generation);
    *tail++ = ':';
    *tail++ = ' ';
    asset_text(T_LIVE_PAIR, tail, T_LIVE_PAIR_LEN);
    tail[LIVE_PAIR_FW] = (char)(tail[LIVE_PAIR_FW] + record.firmware_slot);
    tail[LIVE_PAIR_CFG] = (char)(tail[LIVE_PAIR_CFG] + record.config_bank);
}

/* Pick the record the resident would boot from and mark its three cards. */
static void load_live(void)
{
    state_record_t records[2];
    uint8_t marks[2];
    marks[0] = read_state(0u, &records[0]);
    marks[1] = read_state(1u, &records[1]);

    const uint8_t chosen = marks[1] > marks[0] ||
        (marks[0] == MARK_VALID && marks[1] == MARK_VALID &&
         (int32_t)(records[1].generation - records[0].generation) > 0);
    const state_record_t *live = &records[chosen];

    g.live_fw = NO_ENTRY;
    g.live_cfg = NO_ENTRY;
    g.live_state = NO_ENTRY;
    if (marks[chosen] < MARK_LEGACY)
        return;
    g.live_state = (uint8_t)(STATE_FIRST + chosen);
    g.live_fw = (uint8_t)(FW_SLOT_FIRST + live->firmware_slot);
    g.live_cfg = live->config_bank == 0u
        ? 0u : (uint8_t)(CONFIG_FIRST + live->config_bank - 1u);
}

static bool entry_is_live(uint16_t entry)
{
    return entry == g.live_fw || entry == g.live_cfg || entry == g.live_state;
}

static void format_label(uint16_t entry, char *out)
{
    if (entry < FW_SLOT_FIRST) {
        asset_text(T_ZONE + entry * T_ZONE_STRIDE, out, T_ZONE_STRIDE);
    } else if (entry < CONFIG_FIRST) {
        format_slot((uint8_t)(entry - FW_SLOT_FIRST), true, out);
    } else if (entry < STATE_FIRST) {
        const uint16_t zone = 6u + entry - CONFIG_FIRST;
        asset_text(T_ZONE + zone * T_ZONE_STRIDE, out, T_ZONE_STRIDE);
    } else if (entry < APP_SLOT_FIRST) {
        const uint16_t zone = 10u + entry - STATE_FIRST;
        asset_text(T_ZONE + zone * T_ZONE_STRIDE, out, T_ZONE_STRIDE);
    } else if (entry < TAIL_FIRST) {
        format_slot((uint8_t)(entry - APP_SLOT_FIRST), false, out);
    } else {
        const uint16_t zone = 12u + entry - TAIL_FIRST;
        asset_text(T_ZONE + zone * T_ZONE_STRIDE, out, T_ZONE_STRIDE);
    }
}

static void format_description(uint16_t entry, char *out)
{
    uint16_t description;
    if (entry < FW_SLOT_FIRST) {
        description = entry;
    } else if (entry < CONFIG_FIRST) {
        format_fw_description((uint8_t)(entry - FW_SLOT_FIRST), out);
        return;
    } else if (entry < STATE_FIRST) {
        description = 7u;
    } else if (entry < APP_SLOT_FIRST) {
        format_state_description((uint8_t)(entry - STATE_FIRST), out);
        return;
    } else if (entry < TAIL_FIRST) {
        format_app_description((uint8_t)(entry - APP_SLOT_FIRST), out);
        return;
    } else {
        description = (uint16_t)(9u + entry - TAIL_FIRST);
    }
    asset_text(T_DESC + description * T_DESC_STRIDE, out, T_DESC_STRIDE);
}

static uint8_t entry_type(uint16_t entry)
{
    if (entry == 0u || (entry >= CONFIG_FIRST && entry < STATE_FIRST))
        return 0u;
    if (entry == 1u)
        return 1u;
    if (entry == 3u)
        return 2u;
    if (entry == 2u || entry == 4u || entry == TAIL_FIRST)
        return 3u;
    if (entry == 5u)
        return 4u;
    if (entry >= FW_SLOT_FIRST && entry < CONFIG_FIRST)
        return 5u;
    if (entry >= STATE_FIRST && entry < APP_SLOT_FIRST)
        return 6u;
    if (entry >= APP_SLOT_FIRST && entry < TAIL_FIRST)
        return 7u;
    return 8u;
}

static void draw_type_capsule(uint16_t entry, uint8_t line)
{
    char type[T_TYPE_STRIDE];
    const uint8_t index = entry_is_live(entry) ? 9u : entry_type(entry);
    asset_text(T_TYPE + index * T_TYPE_STRIDE, type, T_TYPE_STRIDE);
    A->print_inverse(type, (uint8_t)(127u - text_length(type) * 4u),
                     line, false, true, 127u);
}

static bool entry_is_unused(uint16_t entry)
{
    return entry == 2u || entry == 4u || entry == TAIL_FIRST;
}

static void draw_ribbon(bool separator, uint8_t line)
{
    const int16_t y = (int16_t)line * ROW_HEIGHT;

    A->draw_line(A->fb, 5, y, 5, y + 7, true);
    if (separator)
        for (int16_t x = 5; x < 128; x += 2)
            A->draw_line(A->fb, x, y + 7, x, y + 7, true);
}

static void draw_unused_band(uint8_t line)
{
    const int16_t y = (int16_t)line * ROW_HEIGHT;
    A->draw_line(A->fb, 6, y, 6, y + 7, true);
    A->draw_line(A->fb, 7, y, 7, y + 7, true);
}

static void print_size(uint32_t start, uint32_t end, uint8_t y)
{
    char size[8];
    format_size(size, start, end);
    A->print_tiny(size, (uint8_t)(127u - text_length(size) * 4u), y, false, true);
}

static void draw_row(uint16_t row, uint8_t line)
{
    const uint64_t qr = A->uidivmod(row, CARD_ROWS);
    const uint8_t part = (uint8_t)(qr >> 32);
    const uint8_t text_x = 10u;
    const uint8_t y = (uint8_t)(line * ROW_HEIGHT + 1u);
    const uint16_t entry = (uint16_t)qr;

    if (entry_is_unused(entry))
        draw_unused_band(line);
    draw_ribbon(part == 2u, line);
    if (part == 0u) {
        char label[20];
        format_label(entry, label);
        A->print_normal(label, text_x, 0u, line);
        draw_type_capsule(entry, line);
    } else if (part == 1u) {
        char description[DESC_MAX];
        format_description(entry, description);
        A->print_tiny(description, text_x, y, false, true);
    } else {
        char range[20];
        uint32_t start;
        uint32_t end;
        entry_range(entry, &start, &end);
        format_range(range, start, end);
        A->print_tiny(range, text_x, y, false, true);
        print_size(start, end, y);
    }
}

static void shift_frame(uint8_t pixels, const uint8_t *next_row)
{
    for (uint8_t line = 0u; line < VISIBLE_ROWS; line++) {
        for (uint8_t x = 0u; x < 128u; x++) {
            const uint8_t next = line + 1u < VISIBLE_ROWS
                ? A->fb[line + 1u][x] : next_row[x];
            A->fb[line][x] = (uint8_t)((A->fb[line][x] >> pixels) |
                                      (next << (ROW_HEIGHT - pixels)));
        }
    }
}

static void draw_status(void)
{
    char title[T_TITLE_LEN];
    A->status_clear();
    asset_text(T_TITLE, title, T_TITLE_LEN);
    A->print_inverse(title, 2u, 0u, true, true, 38u);
    if (g.function_armed)
        A->asset_read(BMP_F, A->status_line + 70u, BMP_F_LEN);
    A->draw_battery();
    A->blit_status();
}

static void draw(void)
{
    uint8_t next_row[128];
    const uint16_t first = g.scroll / ROW_HEIGHT;
    const uint8_t phase = (uint8_t)(g.scroll & (ROW_HEIGHT - 1u));

    draw_status();
    if (phase != 0u) {
        A->display_clear();
        const uint16_t next = first + VISIBLE_ROWS;
        if (next < ROW_COUNT)
            draw_row(next, 0u);
        for (uint8_t x = 0u; x < 128u; x++)
            next_row[x] = A->fb[0][x];
    }

    A->display_clear();
    for (uint8_t visible = 0u; visible < VISIBLE_ROWS; visible++) {
        const uint16_t row = first + visible;
        if (row >= ROW_COUNT)
            break;
        draw_row(row, visible);
    }
    if (phase != 0u)
        shift_frame(phase, next_row);
    A->blit_full();
}

/* The last card can reach the top: the top card is the one MENU dumps. */
static uint16_t maximum_scroll(void)
{
    return (ENTRY_COUNT - 1u) * CARD_HEIGHT;
}

static void navigate(int8_t direction)
{
    const uint16_t maximum = maximum_scroll();
    int32_t target = (int32_t)g.target +
                     (int32_t)direction * (int32_t)CARD_HEIGHT;
    if (target < 0)
        target = 0;
    if (target > maximum)
        target = maximum;
    g.target = (uint16_t)target;
}

/* Instant jump: the smooth scroll would crawl across dozens of cards. */
static void jump_to_entry(uint8_t entry)
{
    const uint16_t target = (uint16_t)(entry * CARD_HEIGHT);
    g.target = target;
    g.scroll = target;
    g.dirty = true;
}

/* ---- hexdump of the zone on the top card ---- */

static uint32_t dump_page(void)
{
    return DUMP_LINES * (g.dump_text ? DUMP_TEXT_BYTES : DUMP_HEX_BYTES);
}

/* Zone starts and sizes are multiples of the page, so aligning the offset
 * keeps whole pages inside the zone. */
static void dump_seek(uint32_t offset)
{
    const uint32_t page = dump_page();
    const uint32_t last = g.dump_end + 1u - page - g.dump_start;
    offset &= ~(page - 1u);
    if (offset > last)
        offset = last;
    g.dump_addr = g.dump_start + offset;
    g.dirty = true;
}

static void dump_step(int8_t direction)
{
    const int32_t offset = (int32_t)(g.dump_addr - g.dump_start) +
                           (int32_t)direction * (int32_t)dump_page();
    dump_seek(offset < 0 ? 0u : (uint32_t)offset);
}

/* Keys 0..9 (and F+UP/DOWN as 0/10) jump to tenths of the zone. */
static void dump_tenths(uint8_t tenths)
{
    const uint32_t tenth = (uint32_t)A->uidivmod(g.dump_end - g.dump_start + 1u, 10u);
    dump_seek(tenth * tenths);
}

static void open_dump(void)
{
    g.dump_entry = (uint8_t)A->uidivmod(g.target, CARD_HEIGHT);
    entry_range(g.dump_entry, &g.dump_start, &g.dump_end);
    g.dump = true;
    dump_seek(0u);
}

static void step(int8_t direction)
{
    if (g.dump)
        dump_step(direction);
    else
        navigate(direction);
}

static void draw_dump(void)
{
    uint8_t bytes[DUMP_LINES * DUMP_TEXT_BYTES];
    char text[DESC_MAX];
    const uint8_t per_line = g.dump_text ? DUMP_TEXT_BYTES : DUMP_HEX_BYTES;

    draw_status();
    A->display_clear();
    format_label(g.dump_entry, text);
    A->print_inverse(text, 2u, 0u, false, true,
                     (uint8_t)(2u + text_length(text) * 4u));
    asset_text(T_DUMP_MODE + g.dump_text * T_DUMP_MODE_STRIDE,
               text, T_DUMP_MODE_STRIDE);
    A->print_tiny(text, (uint8_t)(127u - text_length(text) * 4u), 1u, false, true);

    A->sys_storage_read(g.dump_addr, bytes, dump_page());
    for (uint8_t line = 0u; line < DUMP_LINES; line++) {
        const uint8_t *data = &bytes[line * per_line];
        char *out = put_hex6(text, g.dump_addr + (uint32_t)line * per_line);
        *out++ = ' ';
        for (uint8_t i = 0u; i < per_line; i++) {
            const uint8_t value = data[i];
            if (g.dump_text) {
                *out++ = value >= 0x20u && value <= 0x7Eu ? (char)value : '.';
            } else {
                *out++ = hex_digit(value >> 4);
                *out++ = hex_digit(value & 0x0Fu);
                *out++ = ' ';
            }
        }
        if (!g.dump_text)
            out--;                       /* drop the trailing space */
        *out = '\0';
        A->print_tiny(text, 1u, (uint8_t)(ROW_HEIGHT + line * DUMP_LINE_HEIGHT),
                      false, true);
    }
    A->blit_full();
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    g.running = true;
    g.dirty = true;
    A->backlight_on();
    load_live();

    uint8_t previous = APP_KEY_INVALID;
    uint16_t repeat_ms = 0u;
    while (g.running) {
        const uint8_t key = A->get_key();
        if (key == APP_KEY_SAVER) {
            previous = APP_KEY_INVALID;
            repeat_ms = 0u;
            g.suspended = true;
            g.function_armed = false;
        } else if (key == APP_KEY_WAKE) {
            previous = APP_KEY_INVALID;
            repeat_ms = 0u;
            g.suspended = false;
            g.function_armed = false;
            g.dirty = true;
        } else if (key == APP_KEY_INVALID) {
            previous = key;
            repeat_ms = 0u;
        } else if (key != previous) {
            const int8_t direction = (key == APP_KEY_DOWN) - (key == APP_KEY_UP);
            A->backlight_on();
            if (key == APP_KEY_EXIT) {
                if (g.dump) {
                    g.dump = false;      /* EXIT leaves the dump first */
                    g.dirty = true;
                } else {
                    g.running = false;
                }
            } else if (key == APP_KEY_F) {
                g.function_armed = !g.function_armed;
                g.dirty = true;
            } else if (direction != 0) {
                if (g.function_armed) {
                    if (g.dump)
                        dump_tenths(direction < 0 ? 0u : 10u);
                    else
                        jump_to_entry(direction < 0 ? 0u : ENTRY_COUNT - 1u);
                    g.function_armed = false;
                } else {
                    step(direction);
                }
            } else if (key == APP_KEY_MENU) {
                g.function_armed = false;
                if (g.dump) {
                    g.dump = false;
                    g.dirty = true;
                } else {
                    open_dump();
                }
            } else if (key == APP_KEY_STAR && g.dump) {
                g.function_armed = false;
                g.dump_text = !g.dump_text;
                dump_seek(g.dump_addr - g.dump_start);
            } else if (key <= APP_KEY_9) {
                g.function_armed = false;
                if (g.dump)
                    dump_tenths(key);
                else
                    jump_to_entry(section_entries[key]);
            } else if (key != APP_KEY_PTT) {
                g.function_armed = false;
                A->play_tone(500u, 60u);
            }
            previous = key;
            repeat_ms = direction != 0 && !g.function_armed
                ? NAV_REPEAT_DELAY_MS : 0u;
        } else {
            const int8_t direction = (key == APP_KEY_DOWN) - (key == APP_KEY_UP);
            if (direction != 0) {
                if (repeat_ms > INPUT_TICK_MS) {
                    repeat_ms -= INPUT_TICK_MS;
                } else {
                    A->backlight_on();
                    step(direction);
                    repeat_ms = NAV_REPEAT_MS;
                }
            }
        }

        if (!g.suspended) {
            if (g.scroll < g.target) {
                g.scroll += SCROLL_FRAME_STEP;
                g.dirty = true;
            }
            if (g.scroll > g.target) {
                g.scroll -= SCROLL_FRAME_STEP;
                g.dirty = true;
            }
            if (g.dirty) {
                if (g.dump)
                    draw_dump();
                else
                    draw();
                g.dirty = false;
            }
        }

        A->delay_ms(INPUT_TICK_MS);
        A->backlight_update();
    }
}
