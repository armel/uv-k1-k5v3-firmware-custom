/* Copyright 2025 Armel FAUVEAU (F4HWN)
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

#include <stdint.h>
#include <stdbool.h>

#include "py32f0xx.h"           // NVIC_SystemReset

#include "helper/recovery.h"
#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "driver/system.h"      // SYSTEM_DelayMs
#include "driver/backlight.h"
#include "app/uart.h"
#include "ui/helper.h"          // UI_DisplayClear, UI_PrintString*

// External-flash calibration zone (see the mapping in App/driver/eeprom_compat.c:
// EEPROM logical 0xB000..0xB200 <-> PY25Q16 physical 0x010000..0x010200).
#define CALIB_ADDR   0x010000
#define CALIB_SIZE   512

bool RECOVERY_CalibrationIsWiped(void)
{
    uint8_t buf[64];
    bool    all_ff = true;
    bool    all_00 = true;

    for (uint32_t off = 0; off < CALIB_SIZE; off += sizeof(buf))
    {
        PY25Q16_ReadBuffer(CALIB_ADDR + off, buf, sizeof(buf));

        for (uint32_t i = 0; i < sizeof(buf); i++)
        {
            if (buf[i] != 0xFF) all_ff = false;
            if (buf[i] != 0x00) all_00 = false;
        }

        // As soon as a byte breaks both patterns, a real calibration is
        // present: nothing to recover.
        if (!all_ff && !all_00)
            return false;
    }

    return all_ff || all_00;
}

static void RECOVERY_DrawWaitScreen(void)
{
    UI_StatusClear();
    UI_DisplayClear();

    UI_PrintString("RESTORE", 0, 127, 1, 10);
    UI_PrintStringSmallNormal("CALIBRATION",       0, 127, 3);
    UI_PrintStringSmallNormal("Connect UV Studio",  0, 127, 5);
    UI_PrintStringSmallNormal("and restore calib",  0, 127, 6);

    ST7565_BlitStatusLine();  // blank status line
    ST7565_BlitFullScreen();
}

void RECOVERY_Loop(void)
{
    BACKLIGHT_TurnOn();
    RECOVERY_DrawWaitScreen();

    // Both counters are expressed in loop ticks (~10 ms each).
    uint16_t idle    = 0;   // time since the last serviced command
    uint16_t recheck = 0;   // time since the last calibration re-read

    for (;;)
    {
        bool activity = false;

#ifdef ENABLE_UART
        if (UART_IsCommandAvailable(UART_PORT_UART))
        {
            UART_HandleCommand(UART_PORT_UART);
            activity = true;
        }
#endif
#ifdef ENABLE_USB
        if (UART_IsCommandAvailable(UART_PORT_VCP))
        {
            UART_HandleCommand(UART_PORT_VCP);
            activity = true;
        }
#endif

        if (activity)
            idle = 0;
        else if (idle < 60000)
            idle++;

        // Re-read the calibration about once per second, but only reboot once
        // it is valid AND the link has been quiet for ~3 s. This guarantees we
        // never reset in the middle of an UV Studio transfer (which briefly
        // makes the zone look non-wiped after the first sector is written).
        if (++recheck >= 100)
        {
            recheck = 0;

            if (idle >= 300 && !RECOVERY_CalibrationIsWiped())
            {
                UI_DisplayClear();
                UI_PrintString("CALIB OK", 0, 127, 2, 10);
                UI_PrintString("REBOOT",   0, 127, 4, 10);
                ST7565_BlitStatusLine();
                ST7565_BlitFullScreen();

                SYSTEM_DelayMs(1500);
                NVIC_SystemReset();
            }
        }

        SYSTEM_DelayMs(10);
    }
}
