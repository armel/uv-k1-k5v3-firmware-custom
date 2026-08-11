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

#include "superf.h"

#ifdef ENABLE_SUPERF

#include "misc.h"
#include "audio.h"
#include "ui/ui.h"
#include "ui/menu.h"
#include "ui/helper.h"
#include "action.h"
#include "external/printf/printf.h"
#include "settings.h"
#include <assert.h>
#include <string.h>

bool gSuperFActive = 0;
uint8_t gCurrentSuperFIndex = 1;

static void SuperF_KeyExit(void)
{
    GUI_SelectNextDisplay(DISPLAY_MAIN);
    gSuperFActive = false;
}

static void SuperF_KeyMenu(void)
{
    SuperF_KeyExit();
    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    action_opt_table[gSubMenu_SIDEFUNCTIONS[gCurrentSuperFIndex].id]();
}

void ACTION_SuperF(void)
{
    gSuperFActive = true; // useless?
    GUI_SelectNextDisplay(DISPLAY_SUPERF);
}

void SuperF_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (!bKeyPressed && Key != KEY_MENU)
        return;

    if (Key != KEY_MENU)
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

    switch (Key) {
    case KEY_UP:
        gCurrentSuperFIndex = (gCurrentSuperFIndex >= gSubMenu_SIDEFUNCTIONS_size - 1)? 1 : gCurrentSuperFIndex + 1;
        break;
    case KEY_DOWN:
        gCurrentSuperFIndex = (gCurrentSuperFIndex <= 1)? gSubMenu_SIDEFUNCTIONS_size - 1 : gCurrentSuperFIndex - 1;
        break;
    case KEY_MENU:
        if (!bKeyPressed)
            SuperF_KeyMenu();
        break;
    case KEY_EXIT:
        SuperF_KeyExit();
        return;
    default:
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        break;
    }

    gUpdateDisplay = true;
}

void UI_DisplaySuperF(void)
{
    UI_DisplayClear();
    char* message = "--Choose action--";
    char index[16];
    sprintf(index, "%u", gCurrentSuperFIndex);
    UI_PrintStringSmallNormal(message, 0, 127, 0);
    UI_PrintStringSmallNormal(gSubMenu_SIDEFUNCTIONS[(gCurrentSuperFIndex >= gSubMenu_SIDEFUNCTIONS_size - 1)? 1 : gCurrentSuperFIndex + 1].name, 0, 127, 2);
    UI_PrintString(gSubMenu_SIDEFUNCTIONS[gCurrentSuperFIndex].name, 0, 127, 3, 8);
    UI_PrintStringSmallNormal(gSubMenu_SIDEFUNCTIONS[(gCurrentSuperFIndex <= 1)? gSubMenu_SIDEFUNCTIONS_size - 1 : gCurrentSuperFIndex - 1].name, 0, 127, 5);
    ST7565_BlitFullScreen();
}

#endif // ENABLE_SUPERF
