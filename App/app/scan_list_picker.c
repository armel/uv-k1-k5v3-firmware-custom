/* Licensed under the Apache License, Version 2.0 (the "License");
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

/* Scan-list picker. During a memory scan, a short * shows the scan lists
 * (ALL, MIX, then each list that holds a channel) with their names and
 * channel counts in place of the main screen. The scan keeps running.
 * UP/DOWN move, MENU scans the selected list, EXIT or * closes.
 *
 * Built like the action picker: SCAN_LIST_PICKER_ProcessKey() is called from
 * ProcessKey() (app.c), SCAN_LIST_PICKER_Draw() from UI_DisplayMain() and
 * SCAN_LIST_PICKER_TimeSlice500ms() from APP_TimeSlice500ms(). */

#include <string.h>

#include "app/chFrScanner.h"
#include "app/scan_list_picker.h"
#include "audio.h"
#include "external/printf/printf.h"   /* the firmware's small sprintf */
#include "driver/st7565.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/main.h"

#define PICKER_ROWS   5u                       /* list rows, lines 1..5 */
#define PICKER_ITEMS  (MR_CHANNELS_LIST + 2u)  /* ALL, MIX, 24 lists    */

bool gScanListPickerOpen;

/* Built when the picker opens. items[] holds the SCAN_LIST_DEFAULT value
 * of each row: SCAN_LIST_MODE_ALL, SCAN_LIST_MODE_MIX or a list 1..24. */
static struct {
    uint8_t  items[PICKER_ITEMS];
    uint16_t counts[PICKER_ITEMS];   /* channels the row would scan */
    uint8_t  count;                  /* rows */
    uint8_t  cursor;                 /* selected row */
    uint8_t  top;                    /* first row on screen */
    uint8_t  timeout_500ms;
} picker;

static void picker_add(uint8_t item, uint16_t channels)
{
    picker.items[picker.count]  = item;
    picker.counts[picker.count] = channels;
    picker.count++;
}

/* Count the channels of each list in one pass, with the scanner's rules
 * (RADIO_CheckValidChannel / RADIO_IsChannelInScanList): excluded channels,
 * invalid ones and those in no list are not scanned, so not counted. A
 * channel whose list is SCAN_LIST_MODE_ALL is scanned with every list: it
 * counts in each, but an otherwise empty list is not offered for it. */
static void picker_build(void)
{
    uint16_t in_list[MR_CHANNELS_LIST + 1] = {0};   /* [1..24]; [0]: in every list */

    for (uint16_t ch = MR_CHANNEL_FIRST; IS_MR_CHANNEL(ch); ch++) {
        const ChannelAttributes_t att = *MR_GetChannelAttributes(ch);
        if (att.exclude || att.band > BAND7_470MHz)
            continue;
        if (att.scanlist == SCAN_LIST_MODE_ALL)
            in_list[0]++;
        else if (att.scanlist >= 1 && att.scanlist <= MR_CHANNELS_LIST)
            in_list[att.scanlist]++;
    }

    /* ALL scans every list; MIX the lists of SCAN_LIST_MIX_MASK. */
    uint16_t all = in_list[0];
    uint16_t mix = gEeprom.SCAN_LIST_MIX_MASK ? in_list[0] : 0;
    for (uint8_t l = 1; l <= MR_CHANNELS_LIST; l++) {
        all += in_list[l];
        if (gEeprom.SCAN_LIST_MIX_MASK & (1u << (l - 1u)))
            mix += in_list[l];
    }

    picker.count = 0;
    picker_add(SCAN_LIST_MODE_ALL, all);
    if (mix)
        picker_add(SCAN_LIST_MODE_MIX, mix);
    for (uint8_t l = 1; l <= MR_CHANNELS_LIST; l++)
        if (in_list[l])                        /* empty lists cannot be scanned */
            picker_add(l, in_list[l] + in_list[0]);
}

/* Keep the cursor row on screen. */
static void picker_scroll(void)
{
    if (picker.cursor < picker.top)
        picker.top = picker.cursor;
    else if (picker.cursor >= picker.top + PICKER_ROWS)
        picker.top = picker.cursor - PICKER_ROWS + 1u;
}

void SCAN_LIST_PICKER_Open(void)
{
    picker_build();

    /* start on the list being scanned (ALL when it is not offered) */
    picker.cursor = 0;
    for (uint8_t i = 0; i < picker.count; i++)
        if (picker.items[i] == gEeprom.SCAN_LIST_DEFAULT)
            picker.cursor = i;
    picker.top = 0;
    picker_scroll();

    picker.timeout_500ms = SCAN_LIST_PICKER_TIMEOUT_500MS;
    gScanListPickerOpen = true;
    gUpdateDisplay = true;
}

void SCAN_LIST_PICKER_Close(void)
{
    if (!gScanListPickerOpen)
        return;
    gScanListPickerOpen = false;
    gUpdateDisplay = true;
}

/* Scan the selected list: the same steps as the scan key pressed during a
 * scan (ACTION_Scan): show the new list's name, save it with the resume
 * state, go on with the next channel of that list. */
static void picker_apply(void)
{
    gEeprom.SCAN_LIST_DEFAULT = picker.items[picker.cursor];
    SCAN_LIST_PICKER_Close();
    UI_MAIN_NotifyScanListChanged();
#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    SETTINGS_WriteCurrentState();
#endif
    if (gScanStateDir != SCAN_OFF)   /* the scan may have stopped since the last tick */
        CHFRSCANNER_ManualResume(gScanStateDir);
}

/* true when the key was used by the picker. */
bool SCAN_LIST_PICKER_ProcessKey(KEY_Code_t key, bool isPressed, bool isHeld)
{
    if (!gScanListPickerOpen)
        return false;
    if (isPressed)
        picker.timeout_500ms = SCAN_LIST_PICKER_TIMEOUT_500MS;

    switch (key) {
        case KEY_UP:        /* previous row, wraps; held: repeats */
        case KEY_DOWN:      /* next row, wraps; held: repeats */
            if (isPressed) {
                if (key == KEY_UP)
                    picker.cursor = picker.cursor ? picker.cursor - 1u : picker.count - 1u;
                else
                    picker.cursor = (picker.cursor + 1u < picker.count) ? picker.cursor + 1u : 0u;
                picker_scroll();
                if (!isHeld)
                    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
                gUpdateDisplay = true;
            }
            return true;

        case KEY_MENU:      /* short press, on release */
            if (!isPressed && !isHeld)
                picker_apply();
            return true;

        case KEY_EXIT:
        case KEY_STAR:      /* the key that opened it */
        case KEY_F:
            if (!isPressed)
                SCAN_LIST_PICKER_Close();
            return true;

        case KEY_PTT:       /* never delay TX: close and let PTT through */
            SCAN_LIST_PICKER_Close();
            return false;

        default:            /* e.g. digits (list jump), long MENU (exclude) */
            return true;
    }
}

/* Close when there is nothing left to pick, or after the idle timeout. */
void SCAN_LIST_PICKER_TimeSlice500ms(void)
{
    if (!gScanListPickerOpen)
        return;
    if (gScanStateDir == SCAN_OFF || !IS_MR_CHANNEL(gNextMrChannel) ||
        --picker.timeout_500ms == 0)
        SCAN_LIST_PICKER_Close();
}

/* "ALL", "MIX", "01 RPT", or "LIST 05" for a list without a name. */
static void picker_label(char *s, uint8_t item)
{
    if (item == SCAN_LIST_MODE_ALL) {
        strcpy(s, "ALL");
    } else if (item == SCAN_LIST_MODE_MIX) {
        strcpy(s, "MIX");
    } else {
        const char *name = gListName[item - 1u];
        if (IsEmptyName(name, sizeof(gListName[0])))
            sprintf(s, "LIST %02u", item);
        else
            sprintf(s, "%02u %.4s", item, name);
    }
}

/* Title and position on line 0, rows on lines 1..5 (label left, count
 * right, the list being scanned in bold, the cursor row inverted), key help
 * on line 6. */
void SCAN_LIST_PICKER_Draw(void)
{
    char left[12], right[8];

    UI_PrintStringSmallBold("SCAN LIST", 1, 0, 0);
    sprintf(right, "%u/%u", picker.cursor + 1u, picker.count);
    UI_PrintStringSmallNormal(right, LCD_WIDTH - strlen(right) * 7u, 0, 0);

    for (uint8_t i = 0; i < PICKER_ROWS && picker.top + i < picker.count; i++) {
        const uint8_t index = picker.top + i;
        const uint8_t line  = 1u + i;
        const uint8_t item  = picker.items[index];

        picker_label(left, item);
        sprintf(right, "%u", picker.counts[index]);
        if (item == gEeprom.SCAN_LIST_DEFAULT)
            UI_PrintStringSmallBold(left, 1, 0, line);
        else
            UI_PrintStringSmallNormal(left, 1, 0, line);
        UI_PrintStringSmallNormal(right, LCD_WIDTH - strlen(right) * 7u, 0, line);

        if (index == picker.cursor) {
            for (uint8_t x = 0; x < LCD_WIDTH; x++) {
                gFrameBuffer[line][x]      ^= 0xFFu;
                gFrameBuffer[line - 1u][x] ^= 0x80u;   /* top edge of the bar */
            }
        }
    }

    GUI_DisplaySmallest("MENU SCAN IT   EXIT BACK", 1, 6 * 8 + 2, false, true);
}
