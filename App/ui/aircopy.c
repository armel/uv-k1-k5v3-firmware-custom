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

#ifdef ENABLE_AIRCOPY

#include <string.h>

#include "app/aircopy.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "radio.h"
#include "ui/aircopy.h"
#include "ui/helper.h"
#include "ui/inputbox.h"

void UI_DisplayAircopy(void)
{
    char String[16];
    char *pPrintStr;

    UI_DisplayClear();

    if (gAircopyState == AIRCOPY_READY) {
        pPrintStr = "AIR COPY(RDY)";
    } else if (gAircopyState == AIRCOPY_TRANSFER) {
        if (gAircopyAll) {
            // All mode: show the slice being replicated in place of the title.
            const uint8_t m = AIRCOPY_CurrentSliceMap();
            if (m < AIRCOPY_NUM_BANKS)
                sprintf(String, "MEM %03u-%03u", (m * 128) + 1, (m + 1) * 128);
            else
                strcpy(String, "SETTINGS");
            pPrintStr = String;
        } else {
            pPrintStr = "AIR COPY";
        }
    } else if (gAircopyState == AIRCOPY_COMPLETE) {
        pPrintStr = "AIR COPY OK";
    } else {
        pPrintStr = "AIR COPY FAIL";
    }

    UI_PrintString(pPrintStr, 2, 127, 0, 8);

    if (gInputBoxIndex == 0) {
        uint32_t frequency = gRxVfo->freq_config_RX.Frequency;
        sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);
        // show the remaining 2 small frequency digits
        UI_PrintStringSmallNormal(String + 7, 97, 0, 3);
        String[7] = 0;
    } else {
        const char *ascii = INPUTBOX_GetAscii();
        sprintf(String, "%.3s.%.3s", ascii, ascii + 3);
    }

    // show the main large frequency digits
    UI_DisplayFrequency(String, 16, 2, false);

    const uint16_t totalBlocks = AIRCOPY_GetTotalBlocks();
    uint16_t doneBlocks = gAirCopyBlockNumber;

    if (doneBlocks > totalBlocks)
        doneBlocks = totalBlocks;

    // Draw memory selection
    if (gAircopyState == AIRCOPY_READY) 
    {
        if(gAircopyCurrentMapIndex < AIRCOPY_NUM_BANKS) {
            sprintf(String, "MEM %03u - %03u", (gAircopyCurrentMapIndex * 128) + 1, (gAircopyCurrentMapIndex + 1) * 128);
        } else if(gAircopyCurrentMapIndex == AIRCOPY_NUM_BANKS) {
            strcpy(String, "Settings");
        } else {
            strcpy(String, "All (Mem+Set)");
        }
        UI_PrintString(String, 2, 127, 5, 8);
    } 
    else 
    {
        uint16_t percent = (doneBlocks * 10000) / totalBlocks;
        const unsigned displayedErrors = gErrorsDuringAirCopy > 99u
                                       ? 99u
                                       : gErrorsDuringAirCopy;

        if (gAircopyState == AIRCOPY_COMPLETE || gAircopyState == AIRCOPY_FAILED) {
            sprintf(String, "%s %u/%u %s:%u",
                    gAircopyState == AIRCOPY_COMPLETE ? "OK" : "KO",
                    doneBlocks, totalBlocks,
                    gAirCopyIsSendMode ? "RT" : "ER",
                    displayedErrors);
        } else if (gAirCopyIsSendMode == 0) {
            sprintf(String, "RX:%02u.%02u ER:%u", percent / 100, percent % 100,
                    displayedErrors);
        } else {
            sprintf(String, "TX:%02u.%02u RT:%u", percent / 100, percent % 100,
                    displayedErrors);
        }

        UI_PrintString(String, 2, 127, 5, 8);

        gFrameBuffer[4][1] = 0x3c;
        gFrameBuffer[4][2] = 0x42;
        gFrameBuffer[4][3] = 0x81;
        // Match the former DDA gauge exactly, including its partial first pixel.
        const uint8_t filled = (doneBlocks * AIRCOPY_BAR_WIDTH + totalBlocks - 1u)
                             / totalBlocks;
        for (uint8_t col = 0; col < AIRCOPY_BAR_WIDTH; col++)
            gFrameBuffer[4][col + 4] = col < filled ? 0xBD : 0x81;
        gFrameBuffer[4][124] = 0x81;
        gFrameBuffer[4][125] = 0x42;
        gFrameBuffer[4][126] = 0x3c;
    }

    ST7565_BlitFullScreen();
}

#endif
