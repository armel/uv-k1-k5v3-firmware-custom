/* Copyright 2025 Armel F4HWN
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

#include "app/game.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "screenshot.h"
#endif

void APP_GameMenu(void) {
    bool menuActive = true;
    
    while(menuActive) {

        // For screenshot
        #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            getScreenShot(false);
        #endif

        UI_DisplayClear();
        UI_PrintStringSmallBold("SELECT GAME", 32, 0, 0);
        UI_PrintStringSmallNormal("1. Breakout", 16, 0, 2);
        UI_PrintStringSmallNormal("2. Snake", 16, 0, 4);
        
        ST7565_BlitFullScreen();
        
        KEY_Code_t key = KEYBOARD_Poll();
        
        if (key == KEY_1) {
            APP_RunBreakout();
            menuActive = false;
        }
        else if (key == KEY_2) {
            APP_RunSnake();
            menuActive = false;
        }
        else if (key == KEY_EXIT || key == KEY_MENU) {
            menuActive = false;
        }
        
        SYSTEM_DelayMs(100);
    }
    
    gRequestDisplayScreen = DISPLAY_MAIN;
}
