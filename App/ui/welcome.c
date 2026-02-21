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
#include <stdint.h>

#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "helper/battery.h"
#include "settings.h"
#include "misc.h"
#include "ui/helper.h"
#include "ui/welcome.h"
#include "ui/status.h"
#include "version.h"
#include "bitmaps.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
    #include "screenshot.h"
#endif

#ifdef ENABLE_FEAT_F4HWN_MEM
// Linker symbols (provided by the linker script)
extern uint8_t _sdata;          // Start of .data in RAM
extern uint8_t _edata;          // End of .data in RAM
extern uint8_t _sbss;           // Start of .bss in RAM
extern uint8_t _ebss;           // End of .bss in RAM

// _eflash_used must be defined in the linker script immediately after the last
// section with a FLASH load address (after .noncacheable). Example:
//
//   .noncacheable : {
//       ...
//   } > RAM AT> FLASH
//   _eflash_used = LOADADDR(.noncacheable) + SIZEOF(.noncacheable);
//
// This gives the exact byte count that the linker reports as FLASH used.
extern uint8_t _eflash_used;

// Absolute symbols: their *address* IS the numeric size value (ARM/CMSIS convention).
// RAM = .data + gap + .bss + heap_reserve + stack_reserve
extern uint8_t _Min_Heap_Size;
extern uint8_t _Min_Stack_Size;

// Region sizes (must match your linker MEMORY regions)
#define RAM_SIZE_BYTES     (16u * 1024u)
#define FLASH_SIZE_BYTES   (118u * 1024u)

// Base address of FLASH — must match ORIGIN(FLASH) in your linker script
#define FLASH_BASE         (0x08002800u)

static inline uint32_t span(const void* a, const void* b)
{
    return (uint32_t)((uintptr_t)b - (uintptr_t)a);
}

static void build_usage(uint32_t* ram_used, uint32_t* flash_used)
{
    // RAM: span from start of .data to end of .bss covers .data + alignment gap + .bss.
    // Then add heap and stack reservations (absolute linker symbols: address = size).
    // Proof: (0x20002A60 - 0x20000000) + 0x200 + 0x400 = 10848 + 512 + 1024 = 12384 B ✓
    const uint32_t heap_size  = (uint32_t)(uintptr_t)&_Min_Heap_Size;
    const uint32_t stack_size = (uint32_t)(uintptr_t)&_Min_Stack_Size;
    *ram_used = span(&_sdata, &_ebss) + heap_size + stack_size;

    // FLASH: _eflash_used is placed by the linker script right after the last
    // section copied to FLASH (.data LMA + .noncacheable LMA).
    // Note: _etext is NOT usable here because this linker script places .rodata
    // sections AFTER _etext, making it an unreliable end-of-flash marker.
    *flash_used = span((void*)FLASH_BASE, &_eflash_used);
}

static inline uint16_t pct_x100(uint32_t used, uint32_t total)
{
    return (uint16_t)((used * 10000u) / total); // 7559 => 75.59%
}
#endif

void UI_DisplayReleaseKeys(void)
{
    memset(gStatusLine,  0, sizeof(gStatusLine));
#if defined(ENABLE_FEAT_F4HWN_CTR) || defined(ENABLE_FEAT_F4HWN_INV)
        ST7565_ContrastAndInv();
#endif
    UI_DisplayClear();

    UI_PrintString("RELEASE", 0, 127, 1, 10);
    UI_PrintString("ALL KEYS", 0, 127, 3, 10);

    ST7565_BlitStatusLine();  // blank status line
    ST7565_BlitFullScreen();
}

void UI_DisplayWelcome(void)
{
    char WelcomeString0[16];
    char WelcomeString1[16];
    char WelcomeString2[16];
    char WelcomeString3[32];

    memset(gStatusLine,  0, sizeof(gStatusLine));

#if defined(ENABLE_FEAT_F4HWN_CTR) || defined(ENABLE_FEAT_F4HWN_INV)
        ST7565_ContrastAndInv();
#endif
    UI_DisplayClear();

#ifdef ENABLE_FEAT_F4HWN
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
    
    if (gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_NONE || gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_SOUND) {
        ST7565_FillScreen(0x00);
#else
    if (gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_NONE || gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_FULL_SCREEN) {
        ST7565_FillScreen(0xFF);
#endif
    } else {
        memset(WelcomeString0, 0, sizeof(WelcomeString0));
        memset(WelcomeString1, 0, sizeof(WelcomeString1));

        // 0x0EB0
        PY25Q16_ReadBuffer(0x00A0C8, WelcomeString0, 16);
        // 0x0EC0
        PY25Q16_ReadBuffer(0x00A0D8, WelcomeString1, 16);

        sprintf(WelcomeString2, "%u.%02uV %u%%",
                gBatteryVoltageAverage / 100,
                gBatteryVoltageAverage % 100,
                BATTERY_VoltsToPercent(gBatteryVoltageAverage));

        if (gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_VOLTAGE)
        {
            strcpy(WelcomeString0, "VOLTAGE");
            strcpy(WelcomeString1, WelcomeString2);
        }
        else if(gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_ALL)
        {
            if(strlen(WelcomeString0) == 0 && strlen(WelcomeString1) == 0)
            {
                strcpy(WelcomeString0, "WELCOME");
                strcpy(WelcomeString1, WelcomeString2);
            }
            else if(strlen(WelcomeString0) == 0 || strlen(WelcomeString1) == 0)
            {
                if(strlen(WelcomeString0) == 0)
                {
                    strcpy(WelcomeString0, WelcomeString1);
                }
                strcpy(WelcomeString1, WelcomeString2);
            }
        }
        else if(gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_MESSAGE)
        {
            if(strlen(WelcomeString0) == 0)
            {
                strcpy(WelcomeString0, "WELCOME");
            }

            if(strlen(WelcomeString1) == 0)
            {
                strcpy(WelcomeString1, "BIENVENUE");
            }
        }

        UI_PrintString(WelcomeString0, 0, 127, 0, 10);
        UI_PrintString(WelcomeString1, 0, 127, 2, 10);

#ifdef ENABLE_FEAT_F4HWN
        UI_PrintStringSmallNormal(Version, 0, 128, 4);

        UI_DrawLineBuffer(gFrameBuffer, 0, 35, 18, 35, 1);
        gFrameBuffer[4][19] ^= 0x7F;
        for (uint8_t x = 20; x < 108; x++)
        {
            gFrameBuffer[4][x] ^= 0xFF;
            gFrameBuffer[3][x] ^= 0x80;
        }
        gFrameBuffer[4][108] ^= 0x7F;
        UI_DrawLineBuffer(gFrameBuffer, 109, 35, 127, 35, 1);

        #ifdef ENABLE_FEAT_F4HWN_MEM
            uint32_t ram_used   = 0;
            uint32_t flash_used = 0;
            build_usage(&ram_used, &flash_used);

            const uint16_t ram_pct   = pct_x100(ram_used,   RAM_SIZE_BYTES);
            const uint16_t flash_pct = pct_x100(flash_used, FLASH_SIZE_BYTES);

            // No floats: 7559 => 75.59%
            sprintf(WelcomeString3,
            "FLASH %u.%02u %% - SRAM  %u.%02u %%",
            (unsigned)(flash_pct / 100), (unsigned)(flash_pct % 100),
            (unsigned)(ram_pct / 100),   (unsigned)(ram_pct % 100));

            GUI_DisplaySmallest(WelcomeString3, 5, 1, true, true);
            ST7565_BlitStatusLine();
        #endif

        sprintf(WelcomeString3, "%s Edition", Edition);
        UI_PrintStringSmallNormal(WelcomeString3, 0, 127, 6);

#else
        UI_PrintStringSmallNormal(Version, 0, 127, 6);
#endif

        //ST7565_BlitStatusLine();  // blank status line : I think it's useless
        ST7565_BlitFullScreen();

        #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            getScreenShot(true);
        #endif
    }
}