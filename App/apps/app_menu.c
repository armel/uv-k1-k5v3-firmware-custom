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

/* Highlight a row (same rounded-invert look as the multiboot selector). */
static void app_invert_row(uint8_t line)
{
    gFrameBuffer[line][0] ^= 0x7Fu;
    for (uint8_t x = 1u; x < LCD_WIDTH - 1u; x++)
    {
        gFrameBuffer[line][x]      ^= 0xFFu;
        gFrameBuffer[line - 1u][x] ^= 0x80u;
    }
    gFrameBuffer[line][LCD_WIDTH - 1u] ^= 0x7Fu;
}

/* Debounced blocking key read, then wait for release (mirrors mb_get_key). */
static KEY_Code_t app_get_key(void)
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

/* Visible app rows (framebuffer lines 1..APP_MENU_ROWS; line 0 is the header). */
#define APP_MENU_ROWS 6u

void APP_MenuOpen(void)
{
    /* Apps are installed from UV Studio (0x073x) into physical slots 0..N-1
     * (shown to the user as 1..N there). Scan them all; the list is empty until
     * the user pushes one ("No apps installed"). */
    app_header_t hdr[APP_SLOT_COUNT];
    uint8_t list[APP_SLOT_COUNT];   /* slot indices of the committed apps */
    uint8_t count = 0;

    for (uint8_t slot = 0; slot < APP_SLOT_COUNT; slot++)
    {
        if (APP_SlotInfo(slot, &hdr[slot]) == APP_OK &&
            (hdr[slot].flags & APP_FLAG_COMMITTED))
            list[count++] = slot;
    }

    /* Remember the cursor across open/close of the Apps menu. Clamp in case the
     * installed-app set changed since we were last here. */
    static uint8_t sel = 0;
    static uint8_t top = 0;             /* first visible row of the scrolling window */
    if (count == 0u || sel >= count)
        sel = top = 0u;
    app_wait_release();

    for (;;)
    {
        UI_DisplayClear();
        UI_StatusClear();   /* wipe the VFO status line (DW, battery, ...) first */
        /* 10 glyphs x ~4 px = 40 px wide, centred: x=(128-40)/2=44, endX=x+40. */
        GUI_DisplaySmallestInverse("F4HWN APPS", 44, 0, true, true, 84);

        if (count == 0)
        {
            UI_PrintStringSmallNormal("No apps installed", 2, 126, 3);
        }
        else
        {
            /* Scrolling window: slide [top, top+APP_MENU_ROWS) so it always holds
             * the selection, keeping all APP_SLOT_COUNT apps reachable - not just
             * the first APP_MENU_ROWS. */
            if (sel < top)
                top = sel;
            else if (sel >= (uint8_t)(top + APP_MENU_ROWS))
                top = (uint8_t)(sel - APP_MENU_ROWS + 1u);

            for (uint8_t i = top; i < count && (uint8_t)(i - top) < APP_MENU_ROWS; i++)
            {
                char line[19];
                app_copy(line, sizeof(line), hdr[list[i]].name, APP_NAME_LEN);
                const uint8_t fbLine = (uint8_t)(i - top + 1u);
                UI_PrintStringSmallNormal(line, 2, 0, fbLine);
                if (i == sel)
                    app_invert_row(fbLine);
            }
        }

        ST7565_BlitStatusLine();
        ST7565_BlitFullScreen();

        const KEY_Code_t key = app_get_key();
        if (key == KEY_EXIT)
            return;
        if (count == 0)
            continue;

        switch (key)
        {
            case KEY_UP:
                sel = (sel == 0u) ? (uint8_t)(count - 1u) : (uint8_t)(sel - 1u);
                break;
            case KEY_DOWN:
                sel = (uint8_t)((sel + 1u) % count);
                break;
            case KEY_MENU:
            {
                const uint8_t rc = APP_LaunchOverlay(list[sel]);  /* runs until the app exits */
                if (rc != APP_OK)
                    app_show_error(hdr[list[sel]].name, rc);      /* no longer silent */
                app_wait_release();
                break;
            }
            default:
                break;
        }
    }
}

#endif /* ENABLE_FEAT_F4HWN_OVERLAY_APPS */
