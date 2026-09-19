/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#include <string.h>

#ifdef ENABLE_AIRCOPY
    #include "app/aircopy.h"
#endif
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "helper/boot.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/menu.h"
#include "ui/ui.h"
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    #include "ui/multiboot.h"
#endif

BOOT_Mode_t BOOT_GetMode(void)
{
    unsigned int i;
    KEY_Code_t   Keys[2];
    bool         PttPressed[2];

    /* Poll the keypad even without PTT: holding MENU alone enters multiboot.
     * The two samples keep the same debounce rule as the legacy boot modes. */
    for (i = 0; i < 2; i++)
    {
        PttPressed[i] = GPIO_IsPttPressed();
        Keys[i] = KEYBOARD_Poll();
        SYSTEM_DelayMs(20);
    }

    #ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    if (!PttPressed[0] && !PttPressed[1] &&
        Keys[0] == KEY_MENU && Keys[1] == KEY_MENU)
    {
        gKeyReading0 = Keys[0];
        gKeyReading1 = Keys[0];
        gDebounceCounter = 2;
        return BOOT_MODE_MULTIBOOT;
    }
    #endif

    /* All historical special modes still require PTT for both samples. */
    if (!PttPressed[0] || !PttPressed[1])
        return BOOT_MODE_NORMAL;

    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    if (Keys[0] == (10 + gEeprom.SET_KEY))
    {
        return BOOT_MODE_RESCUE_OPS;  // Secret KEY pressed
    }
    #endif

    if (Keys[0] == Keys[1])
    {
        gKeyReading0 = Keys[0];
        gKeyReading1 = Keys[0];

        gDebounceCounter = 2;

        if (Keys[0] == KEY_SIDE1)
            return BOOT_MODE_F_LOCK;

        #ifdef ENABLE_AIRCOPY
            if (Keys[0] == KEY_SIDE2) {
                gAirCopyBootMode = true;
                return BOOT_MODE_AIRCOPY;
            }
        #endif
    }

    return BOOT_MODE_NORMAL;
}

void BOOT_ProcessMode(BOOT_Mode_t Mode)
{
    GUI_DisplayType_t display = DISPLAY_MAIN;

    if (Mode == BOOT_MODE_F_LOCK)
    {
        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
            gEeprom.CURRENT_STATE = 0; // Don't resume is active...
        #endif 
        display = DISPLAY_MENU;
    }
    #ifdef ENABLE_AIRCOPY
        else
        if (Mode == BOOT_MODE_AIRCOPY)
        {
            gEeprom.DUAL_WATCH               = DUAL_WATCH_OFF;
            gEeprom.BATTERY_SAVE             = 0;
            #ifdef ENABLE_VOX
                gEeprom.VOX_SWITCH           = false;
            #endif
            gEeprom.CROSS_BAND_RX_TX         = CROSS_BAND_OFF;
            gEeprom.AUTO_KEYPAD_LOCK         = false;
            gEeprom.KEY_1_SHORT_PRESS_ACTION = ACTION_OPT_NONE;
            gEeprom.KEY_1_LONG_PRESS_ACTION  = ACTION_OPT_NONE;
            gEeprom.KEY_2_SHORT_PRESS_ACTION = ACTION_OPT_NONE;
            gEeprom.KEY_2_LONG_PRESS_ACTION  = ACTION_OPT_NONE;
            gEeprom.KEY_M_LONG_PRESS_ACTION  = ACTION_OPT_NONE;

            RADIO_InitInfo(gRxVfo, FREQ_CHANNEL_LAST - 1, DEFAULT_FREQ); // LPD

            gRxVfo->CHANNEL_BANDWIDTH        = BANDWIDTH_NARROW;
            gRxVfo->OUTPUT_POWER             = OUTPUT_POWER_LOW1;

            RADIO_ConfigureSquelchAndOutputPower(gRxVfo);

            gCurrentVfo = gRxVfo;

            RADIO_SetupRegisters(true);
            BK4819_SetupAircopy();
            BK4819_ResetFSK();

            gAircopyState = AIRCOPY_READY;

            gEeprom.BACKLIGHT_TIME = 61;
            gEeprom.KEY_LOCK = 0;

            #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
                gEeprom.CURRENT_STATE = 0; // Don't resume is active...
            #endif 

            display = DISPLAY_AIRCOPY;
        }
    #endif

    #ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
        else if (Mode == BOOT_MODE_MULTIBOOT)
            UI_MultibootSelector();
    #endif

    GUI_SelectNextDisplay(display);
}
