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
 * The ticks are the MIX selection (SCAN_LIST_MIX_MASK, the one the menu's
 * MIX editor sets), always shown. F ticks or unticks a list; as in that
 * editor the last ticked list cannot be unticked. Changes are kept when the
 * picker closes, whatever closes it. MENU on MIX scans the ticked lists.
 * Leaving with EXIT or * while MIX is scanned applies changed ticks at once,
 * from the ticked list under the cursor, then through the others in memory
 * order.
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
    uint32_t ticks;                  /* the MIX selection, bit l-1 for list l */
    bool     ticks_changed;
    uint16_t in_every;               /* channels in every list (scanlist ALL) */
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

    /* ALL scans every list; MIX is counted from the ticks (picker_mix_count) */
    uint16_t all = in_list[0];
    for (uint8_t l = 1; l <= MR_CHANNELS_LIST; l++)
        all += in_list[l];

    picker.in_every = in_list[0];
    picker.count = 0;
    picker_add(SCAN_LIST_MODE_ALL, all);
    picker_add(SCAN_LIST_MODE_MIX, 0);       /* row 1, always offered */
    for (uint8_t l = 1; l <= MR_CHANNELS_LIST; l++)
        if (in_list[l])                        /* empty lists cannot be scanned */
            picker_add(l, in_list[l] + in_list[0]);
}

/* MIX row count: the channels of the ticked lists, and those in every list. */
static void picker_mix_count(void)
{
    uint16_t mix = picker.in_every;
    for (uint8_t i = 2; i < picker.count; i++)
        if (picker.ticks & (1u << (picker.items[i] - 1u)))
            mix += picker.counts[i] - picker.in_every;
    picker.counts[1] = mix;
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

    /* the ticks show the MIX selection, whatever is scanned (as the MIX
     * editor, an empty one stands for all lists) */
    picker.ticks = gEeprom.SCAN_LIST_MIX_MASK & SCAN_LIST_MIX_MASK_ALL;
    if (picker.ticks == 0)
        picker.ticks = SCAN_LIST_MIX_MASK_ALL;
    picker.ticks_changed = false;
    picker_mix_count();

    picker.timeout_500ms = SCAN_LIST_PICKER_TIMEOUT_500MS;
    gScanListPickerOpen = true;
    gUpdateDisplay = true;
}

/* Whatever closes the picker keeps changed ticks: they are saved as MIX,
 * after the key, as the menu's MIX editor saves them. */
void SCAN_LIST_PICKER_Close(void)
{
    if (!gScanListPickerOpen)
        return;
    if (picker.ticks_changed) {
        gEeprom.SCAN_LIST_MIX_MASK = picker.ticks;
        gRequestSaveSettings = true;
        picker.ticks_changed = false;
    }
    gScanListPickerOpen = false;
    gUpdateDisplay = true;
}

/* First channel of list l the scan reaches, in its direction: the place to
 * start a MIX scan "from" that list. 0xFFFF when it has none of its own. */
static uint16_t picker_first_channel(uint8_t l)
{
    uint16_t found = 0xFFFF;
    for (uint16_t ch = MR_CHANNEL_FIRST; IS_MR_CHANNEL(ch); ch++) {
        const ChannelAttributes_t att = *MR_GetChannelAttributes(ch);
        if (att.exclude || att.band > BAND7_470MHz || att.scanlist != l)
            continue;
        found = ch;
        if (gScanStateDir > 0)
            break;          /* upward: the lowest; downward: the highest */
    }
    return found;
}

/* MENU: scan the row under the cursor (a list, ALL, or MIX with the ticks):
 * the same steps as the scan key pressed during a scan (ACTION_Scan): show
 * the new list's name, save it with the resume state, go on with the next
 * channel of that list. */
static void picker_scan_row(void)
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

/* EXIT / *: close. If MIX is scanned and its ticks changed, go on with the
 * new set from the ticked list under the cursor (else from where it is). */
static void picker_leave(void)
{
    const bool remix = picker.ticks_changed &&
                       gEeprom.SCAN_LIST_DEFAULT == SCAN_LIST_MODE_MIX;
    const uint8_t item = picker.items[picker.cursor];

    SCAN_LIST_PICKER_Close();
    if (!remix || gScanStateDir == SCAN_OFF)
        return;
    if (item <= MR_CHANNELS_LIST && (gEeprom.SCAN_LIST_MIX_MASK & (1u << (item - 1u)))) {
        const uint16_t start = picker_first_channel(item);
        if (start != 0xFFFF) {
            CHFRSCANNER_ResumeAt(start, gScanStateDir);
            return;
        }
    }
    CHFRSCANNER_ManualResume(gScanStateDir);
}

/* F: tick or untick the list under the cursor. ALL and MIX cannot be, nor
 * (as in the MIX editor) the last ticked list: MIX is never empty. */
static void picker_tick(void)
{
    const uint8_t  item = picker.items[picker.cursor];
    const uint32_t bit  = 1u << (item - 1u);
    if (item > MR_CHANNELS_LIST || picker.ticks == bit) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }
    picker.ticks ^= bit;
    picker.ticks_changed = true;
    picker_mix_count();
    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    gUpdateDisplay = true;
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
                picker_scan_row();
            return true;

        case KEY_F:         /* tick / untick */
            if (isPressed && !isHeld)
                picker_tick();
            return true;

        case KEY_EXIT:
        case KEY_STAR:      /* the key that opened it */
            if (!isPressed)
                picker_leave();
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

/* A check mark, 7 columns of one 8-pixel line (bit 0 at the top). */
static const uint8_t picker_check[7] = { 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x04 };

/* Title and position on line 0, rows on lines 1..5 (label left, a check
 * mark for ticked lists, count right, the list being scanned in bold, the
 * cursor row inverted), key help on line 6. */
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
        if (item <= MR_CHANNELS_LIST && (picker.ticks & (1u << (item - 1u))))
            memcpy(&gFrameBuffer[line][LCD_WIDTH - 5u * 7u - 9u], picker_check, sizeof(picker_check));

        if (index == picker.cursor) {
            for (uint8_t x = 0; x < LCD_WIDTH; x++) {
                gFrameBuffer[line][x]      ^= 0xFFu;
                gFrameBuffer[line - 1u][x] ^= 0x80u;   /* top edge of the bar */
            }
        }
    }

    GUI_DisplaySmallest("F TICK  MENU SCAN  EXIT BACK", 1, 6 * 8 + 2, false, true);
}
