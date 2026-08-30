/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "apps/app_overlay.h"

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS

#include <string.h>
#include "apps/app_menu.h"
#include "driver/st7565.h"
#include "driver/keyboard.h"
#include "driver/system.h"
#include "driver/gpio.h"
#include "ui/helper.h"
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
#include "k5viewer.h"
#endif

/* "F4HWN APPS" banner and the same thin separator used by the multiboot
 * selector.  Bit 3 leaves room for the selected-row capsule's top edge. */
static void app_status_bar(void)
{
    UI_StatusClear();
    GUI_DisplaySmallestInverse("F4HWN APPS", 44, 0, true, true, 84);

    for (uint8_t x = 2u; x < LCD_WIDTH - 2u; x++)
        gFrameBuffer[0][x] |= 0x08u;
}

/* Bottom key hints, matching the multiboot selector. */
static void app_key_hints(void)
{
    const uint8_t sp = 6u;
    const char *act_exit = "QUIT";
    const uint8_t ae = (uint8_t)strlen(act_exit);
    const uint8_t xm = 4u;
    const uint8_t xe = (uint8_t)(124u - ae * 4u - sp - 16u);

    GUI_DisplaySmallestInverse("MENU", xm, 6, false, true, (uint8_t)(xm + 16u));
    GUI_DisplaySmallest("RUN", (uint8_t)(xm + 16u + sp), 49, false, true);

    GUI_DisplaySmallestInverse("EXIT", xe, 6, false, true, (uint8_t)(xe + 16u));
    GUI_DisplaySmallest(act_exit, (uint8_t)(xe + 16u + sp), 49, false, true);
}

/* Fixed selection capsule around the primary information (the app name).
 * Slot number stays in the normal font; size is plain 3x5 metadata. */
#define APP_NAME_BOX_START 12u
#define APP_NAME_BOX_END   102u
#define APP_NAME_TEXT_X    14u

static void app_invert_name(uint8_t line)
{
    gFrameBuffer[line][APP_NAME_BOX_START] ^= 0x7Fu;
    for (uint8_t x = APP_NAME_BOX_START + 1u; x < APP_NAME_BOX_END; x++)
    {
        gFrameBuffer[line][x]      ^= 0xFFu;
        gFrameBuffer[line - 1u][x] ^= 0x80u;
    }
    gFrameBuffer[line][APP_NAME_BOX_END] ^= 0x7Fu;
}

/* Debounced blocking key read, then wait for release (mirrors mb_get_key). */
static KEY_Code_t app_get_key(void)
{
    for (;;)
    {
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
        /* APP_MenuOpen() is modal and does not return to APP_Update(). Keep
         * serial key injection and the viewer connection alive while waiting. */
        K5VIEWER_ParseInput();
        K5VIEWER_Update(false);
#endif
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

static void app_wait_release(void)
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

static void app_copy(char *dst, uint8_t cap, const char *src, uint8_t src_cap)
{
    uint8_t n = 0;
    while (n + 1u < cap && n < src_cap && src[n])
    {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

/* Display the code payload rounded up to 0.1 KiB, so the compact value never
 * understates the space occupied by the app.  The space before "KB" is omitted
 * because this secondary value is rendered in the tiny 3x5 font. */
static void app_format_size(char out[6], uint32_t bytes)
{
    if (bytes > APP_OVERLAY_MAX)
    {
        memcpy(out, "--KB", 5u);
        return;
    }

    const uint16_t tenths = (uint16_t)((bytes * 10u + 1023u) / 1024u);
    out[0] = (char)('0' + tenths / 10u);
    out[1] = '.';
    out[2] = (char)('0' + tenths % 10u);
    out[3] = 'K';
    out[4] = 'B';
    out[5] = '\0';
}

/* Human-readable reason for an APP_LaunchOverlay / APP_ValidateSlot failure. */
static const char *app_err_text(uint8_t rc)
{
    switch (rc)
    {
        case APP_ERR_SLOT:          return "BAD SLOT";
        case APP_ERR_MAGIC:         return "NO APP";
        case APP_ERR_ABI:           return "ABI MISMATCH";
        case APP_ERR_NOT_COMMITTED: return "INCOMPLETE";
        case APP_ERR_SIZE:          return "BAD SIZE";
        case APP_ERR_CRC:           return "CRC ERROR";
        case APP_ERR_VMA:           return "VMA MISMATCH";
        case APP_ERR_AUTH:          return "AUTH";
        default:                    return "ERROR";
    }
}

/* A launch failed: name the app and the reason, then wait for a key. Without this
 * an incompatible app would silently "do nothing" when selected. */
static void app_show_error(const char *name, uint8_t rc)
{
    char nm[19];
    app_copy(nm, sizeof(nm), name, APP_NAME_LEN);

    UI_DisplayClear();
    UI_StatusClear();
    GUI_DisplaySmallestInverse("APP ERROR", 46, 0, true, true, 82);
    UI_PrintStringSmallNormal(nm, 2, 0, 2);                 /* which app */
    UI_PrintStringSmallNormal(app_err_text(rc), 2, 0, 4);   /* why       */
    UI_PrintStringSmallNormal("Press any key", 2, 0, 6);
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();

    app_get_key();   /* blocking: dismiss on any key */
}

/* Five visible slots; line 0 holds the separator and line 6 the key hints. */
#define APP_MENU_ROWS 5u
#define APP_MENU_SLOT_COUNT 8u

_Static_assert(APP_MENU_SLOT_COUNT <= APP_SLOT_COUNT,
               "APP_MENU_SLOT_COUNT exceeds the physical app slot count");

void APP_MenuOpen(void)
{
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    /* Detach the modal selector from the key state that triggered F+7. The
     * normal K5Viewer updater suppresses frames while a key is held; without
     * clearing this stale state, the selector could never publish its first
     * frame to the viewer. */
    gKeyReading0 = KEY_INVALID;
    gKeyReading1 = KEY_INVALID;
#endif

    /* Apps are installed from UV Studio (0x073x) into physical slots 0..N-1,
     * shown here as 1..N.  Keep empty slots in the list so their location is
     * visible and selectable while scrolling. */
    app_header_t hdr[APP_MENU_SLOT_COUNT];
    bool installed[APP_MENU_SLOT_COUNT];

    for (uint8_t slot = 0; slot < APP_MENU_SLOT_COUNT; slot++)
    {
        installed[slot] = APP_SlotInfo(slot, &hdr[slot]) == APP_OK &&
                          (hdr[slot].flags & APP_FLAG_COMMITTED);
    }

    /* Remember the physical slot and scrolling window across menu openings. */
    static uint8_t sel = 0;
    static uint8_t top = 0;             /* first visible row of the scrolling window */
    if (sel >= APP_MENU_SLOT_COUNT || top > APP_MENU_SLOT_COUNT - APP_MENU_ROWS)
        sel = top = 0u;
    app_wait_release();

    for (;;)
    {
        UI_DisplayClear();
        app_status_bar();   /* also wipes the VFO status line (DW, battery, ...) */

        /* Slide [top, top+APP_MENU_ROWS) so it always contains the selection. */
        if (sel < top)
            top = sel;
        else if (sel >= (uint8_t)(top + APP_MENU_ROWS))
            top = (uint8_t)(sel - APP_MENU_ROWS + 1u);

        for (uint8_t slot = top;
             slot < APP_MENU_SLOT_COUNT && (uint8_t)(slot - top) < APP_MENU_ROWS;
             slot++)
        {
            char number[2];
            char name[14];
            char size[6];
            const uint8_t visible_number = (uint8_t)(slot + 1u);
            const uint8_t fbLine = (uint8_t)(slot - top + 1u);

            number[0] = (char)('0' + visible_number);
            number[1] = '\0';

            UI_PrintStringSmallNormal(number, 2u, 0, fbLine);
            if (installed[slot])
            {
                app_copy(name, sizeof(name), hdr[slot].name, APP_NAME_LEN);
                app_format_size(size, hdr[slot].code_size);
                UI_PrintStringSmallNormal(name, APP_NAME_TEXT_X, 0, fbLine);
                GUI_DisplaySmallest(size,
                                    (uint8_t)(LCD_WIDTH - 2u - strlen(size) * 4u),
                                    (uint8_t)(fbLine * 8u + 1u), false, true);
            }
            else
            {
                UI_PrintStringSmallNormal("Empty", APP_NAME_TEXT_X, 0, fbLine);
            }

            if (slot == sel)
                app_invert_name(fbLine);
        }

        app_key_hints();

        ST7565_BlitStatusLine();
        ST7565_BlitFullScreen();
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
        K5VIEWER_Update(false);
#endif

        const KEY_Code_t key = app_get_key();
        if (key == KEY_EXIT)
            return;

        switch (key)
        {
            case KEY_UP:
                sel = (sel == 0u) ? (uint8_t)(APP_MENU_SLOT_COUNT - 1u) : (uint8_t)(sel - 1u);
                break;
            case KEY_DOWN:
                sel = (uint8_t)((sel + 1u) % APP_MENU_SLOT_COUNT);
                break;
            case KEY_MENU:
            {
                if (!installed[sel])
                    break;

                const uint8_t rc = APP_LaunchOverlay(sel);  /* runs until the app exits */
                if (rc != APP_OK)
                    app_show_error(hdr[sel].name, rc);      /* no longer silent */
                app_wait_release();
                break;
            }
            default:
                break;
        }
    }
}

#endif /* ENABLE_FEAT_F4HWN_OVERLAY_APPS */
