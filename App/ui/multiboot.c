/* Copyright 2026 F4HWN
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "driver/backlight.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/mb_flash.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "ui/helper.h"
#include "ui/multiboot.h"

static const char *mb_error_text(uint8_t err)
{
    switch (err)
    {
        case MB_OK:                return "OK";
        case MB_ERR_MAGIC:         return "empty";
        case MB_ERR_VERSION:       return "new header";
        case MB_ERR_NOT_COMMITTED: return "incomplete";
        case MB_ERR_SIZE:          return "bad size";
        case MB_ERR_CRC:           return "CRC ERROR";
        case MB_ERR_SPI:           return "SPI ERROR";
        case MB_ERR_SLOT:          return "bad slot";
        case MB_ERR_AUTH:          return "auth";
        default:                   return "error";
    }
}

static void mb_copy_label(char *dst, uint8_t cap, const char *src, uint8_t src_cap)
{
    uint8_t n = 0;
    while (n + 1u < cap && n < src_cap && src[n])
    {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static void mb_format_slot_label(char *dst, uint8_t cap, const mb_slot_header_t *header)
{
    const char *version = NULL;
    uint8_t version_cap = 0;
    uint8_t version_len = 0;
    uint8_t name_limit = cap - 1u;
    uint8_t n = 0;

    if (!header->name[0])
    {
        mb_copy_label(dst, cap, header->fw_version, MB_VERSION_LEN);
        return;
    }

    for (uint8_t i = 0; i + 1u < MB_VERSION_LEN && header->fw_version[i]; i++)
    {
        if (header->fw_version[i] == 'v' &&
            header->fw_version[i + 1u] >= '0' &&
            header->fw_version[i + 1u] <= '9')
        {
            version = &header->fw_version[i + 1u];
            version_cap = MB_VERSION_LEN - i - 1u;
            break;
        }
    }

    if (version)
    {
        while (version_len < version_cap && version[version_len])
            version_len++;
        if (version_len + 1u < cap)
            name_limit = cap - version_len - 2u;
    }

    while (n < name_limit && n < MB_NAME_LEN && header->name[n])
    {
        dst[n] = header->name[n];
        n++;
    }
    if (version && n + version_len + 1u < cap)
    {
        dst[n++] = ' ';
        for (uint8_t i = 0; i < version_len; i++)
            dst[n++] = version[i];
    }
    dst[n] = 0;
}

/* "MULTIBOOT" mode banner in the top status bar, shown on every screen - the
 * same way the firmware puts mode labels there (inverse 3x5 capsule). */
static void mb_status_bar(void)
{
    UI_StatusClear();
    GUI_DisplaySmallestInverse("MULTIBOOT", 47, 0, true, true, 83);
}

/* Bottom key-hint line: each key name as an inverse 3x5 capsule label, its
 * action in plain 3x5 text beside it. Drawn on the bottom line
 * (gFrameBuffer[6] -> y = 6*8+1 = 49). MENU is pinned to the left and EXIT to
 * the right, leaving an airy gap in the middle. "MENU"/"EXIT" are 4 chars
 * (16 px); their capsule spans [x-2 .. x+16]. */
static void mb_key_hints(const char *act_menu, const char *act_exit)
{
    const uint8_t sp = 6u;                              /* label <-> action gap  */
    const uint8_t ae = (uint8_t)strlen(act_exit);
    const uint8_t xm = 4u;                              /* MENU text; capsule at x=2 */
    const uint8_t xe = (uint8_t)(124u - ae * 4u - sp - 16u); /* EXIT action ends at x=124 */

    GUI_DisplaySmallestInverse("MENU", xm, 6, false, true, (uint8_t)(xm + 16u));
    GUI_DisplaySmallest(act_menu, (uint8_t)(xm + 16u + sp), 49, false, true);

    GUI_DisplaySmallestInverse("EXIT", xe, 6, false, true, (uint8_t)(xe + 16u));
    GUI_DisplaySmallest(act_exit, (uint8_t)(xe + 16u + sp), 49, false, true);
}

static void mb_show_message(const char *line1, const char *line2, const char *line3)
{
    UI_DisplayClear();
    mb_status_bar();
    if (line1) UI_PrintStringSmallNormal(line1, 2, 126, 2);
    if (line2) UI_PrintStringSmallNormal(line2, 2, 126, 4);
    if (line3) UI_PrintStringSmallNormal(line3, 2, 126, 6);
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static void mb_wait_release(void)
{
    uint8_t stable = 0;
    while (stable < 10u)
    {
        if (!GPIO_IsPttPressed() && KEYBOARD_Poll() == KEY_INVALID)
            stable++;
        else
            stable = 0;
        SYSTEM_DelayMs(10);
    }
}

static KEY_Code_t mb_get_key(void)
{
    for (;;)
    {
        KEY_Code_t key = KEYBOARD_Poll();
        if (key != KEY_INVALID)
        {
            SYSTEM_DelayMs(30);
            if (KEYBOARD_Poll() == key)
            {
                while (KEYBOARD_Poll() != KEY_INVALID)
                    SYSTEM_DelayMs(10);
                return key;
            }
        }
        SYSTEM_DelayMs(10);
    }
}

static void mb_scan_slots(mb_slot_header_t headers[MB_SLOT_COUNT], uint8_t status[MB_SLOT_COUNT])
{
    mb_show_message("Scanning slots...", NULL, "Please wait");
    for (uint8_t slot = 0; slot < MB_SLOT_COUNT; slot++)
        status[slot] = MB_ValidateSlot(slot, &headers[slot], NULL);
}

static void mb_render_slots(uint8_t selected,
                            const mb_slot_header_t headers[MB_SLOT_COUNT],
                            const uint8_t status[MB_SLOT_COUNT])
{
    char line[19]; /* 18 glyphs max: 18 * 7 px fits from x=2 to x=126. */

    UI_DisplayClear();
    mb_status_bar();

    for (uint8_t slot = 0; slot < MB_SLOT_COUNT; slot++)
    {
        const uint8_t fbLine = (uint8_t)(slot + 1u); /* page 1 stays blank */

        memset(line, 0, sizeof(line));
        line[0] = (char)('0' + slot);
        line[1] = ' ';
        line[2] = ' ';

        if (status[slot] == MB_OK)
            mb_format_slot_label(&line[3], sizeof(line) - 3u, &headers[slot]);
        else
            mb_copy_label(&line[3], sizeof(line) - 3u, mb_error_text(status[slot]), 20u);

        UI_PrintStringSmallNormal(line, 2, 0, fbLine);

        /* Selected row: full-width inverse bar, like the firmware menu list. */
        if (slot == selected)
            for (uint8_t x = 0; x < LCD_WIDTH; x++)
                gFrameBuffer[fbLine][x] ^= 0xFFu;
    }

    mb_key_hints("SELECT", "QUIT");

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static void mb_prepare_progress(uint8_t slot)
{
    char title[] = "Restore slot 0";
    title[13] = (char)('0' + slot);

    UI_DisplayClear();
    mb_status_bar();
    UI_PrintStringSmallNormal(title, 2, 126, 1);
    UI_PrintStringSmallNormal("DO NOT POWER OFF", 2, 126, 3);
    UI_PrintStringSmallNormal("Writing & Verify", 2, 126, 5);

    /* Same rounded outline and hatch pattern as the scan progress gauge. */
    gFrameBuffer[6][3] = 0x0Cu;
    gFrameBuffer[6][4] = 0x12u;
    gFrameBuffer[6][123] = 0x12u;
    gFrameBuffer[6][124] = 0x0Cu;
    for (uint8_t x = 5; x < 123u; x++)
        gFrameBuffer[6][x] = 0x21u;
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static void mb_confirm_screen(uint8_t slot)
{
    char title[] = "Restore slot 0?";
    title[13] = (char)('0' + slot);

    UI_DisplayClear();
    mb_status_bar();
    UI_PrintStringSmallNormal(title, 2, 126, 3);
    mb_key_hints("CONFIRM", "BACK");
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

void UI_MultibootSelector(void)
{
    mb_slot_header_t headers[MB_SLOT_COUNT];
    uint8_t status[MB_SLOT_COUNT];
    uint8_t selected = 0;

    /* Clear + blit the LCD BEFORE the backlight comes on, otherwise it reveals
     * the random power-on contents of the display RAM for a moment. */
    mb_show_message("Release keys", NULL, NULL);
    BACKLIGHT_TurnOn();
    mb_wait_release();
    mb_scan_slots(headers, status);

    for (uint8_t slot = 0; slot < MB_SLOT_COUNT; slot++)
    {
        if (status[slot] == MB_OK)
        {
            selected = slot;
            break;
        }
    }

    for (;;)
    {
        mb_render_slots(selected, headers, status);
        KEY_Code_t key = mb_get_key();

        if (key == KEY_EXIT)
        {
            /* MENU was latched by BOOT_GetMode(). Do not let that stale boot
             * key reach the normal application after leaving the selector. */
            gKeyReading0 = KEY_INVALID;
            gKeyReading1 = KEY_INVALID;
            gDebounceCounter = 0;
            return;
        }
        if (key == KEY_UP)
        {
            selected = (uint8_t)((selected + MB_SLOT_COUNT - 1u) % MB_SLOT_COUNT);
            continue;
        }
        if (key == KEY_DOWN)
        {
            selected = (uint8_t)((selected + 1u) % MB_SLOT_COUNT);
            continue;
        }
        if (key != KEY_MENU)
            continue;

        if (status[selected] != MB_OK)
        {
            mb_show_message("SLOT NOT VALID", mb_error_text(status[selected]), "Press any key");
            (void)mb_get_key();
            continue;
        }

        mb_confirm_screen(selected);
        key = mb_get_key();
        if (key != KEY_MENU)
            continue;

        mb_prepare_progress(selected);
        uint8_t err = MB_RestoreSlot(selected, gFrameBuffer[6]);

        /* Only reached when the final pre-erase validation refused the slot. */
        status[selected] = err;
        mb_show_message("RESTORE REFUSED", mb_error_text(err), "Press any key");
        (void)mb_get_key();
        mb_scan_slots(headers, status);
    }
}
