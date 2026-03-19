/* Copyright 2025 bg7nzl
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ui/digmode.h"
#include "app/digmode.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "ui/ui.h"

/*
 * 128x64 LCD, 7 rows of 8px each + 1 status row.
 *
 * Layout:
 *   Row 0: mode indicator
 *   Row 1: base TX frequency (large)
 *   Row 2: (continued)
 *   Row 3: audio offset
 *   Row 4: actual RF output
 *   Row 5: status bar
 *   Row 6: debug info
 */

void UI_DisplayDigmode(void)
{
    char buf[22];

    UI_DisplayClear();

    const DigmodeDisplayState_t *d = &gDigmodeDisplay;

    /* Row 0: mode indicator */
    if (d->sched_waiting)
        UI_PrintStringSmallBold("DIG WAIT", 0, 0, 0);
    else if (d->tx_active)
        UI_PrintStringSmallBold("DIG TX", 0, 0, 0);
    else
        UI_PrintStringSmallNormal("DIG RX", 0, 0, 0);

    /* Row 1-2: base TX frequency in MHz (large font) */
    {
        uint32_t f = d->base_freq;  // 10 Hz units
        uint32_t mhz  = f / 100000;
        uint32_t frac = f % 100000;
        sprintf(buf, "%3u.%05u", (unsigned)mhz, (unsigned)frac);
        UI_DisplayFrequency(buf, 16, 1, false);
    }

    if (d->sched_waiting)
    {
        /* Countdown to scheduled TX start */
        uint32_t ms = d->countdown_ms;
        uint32_t s  = ms / 1000;
        uint32_t m  = s / 60;
        uint32_t h  = m / 60;
        sprintf(buf, "TX in %u:%02u:%02u",
                (unsigned)h, (unsigned)(m % 60), (unsigned)(s % 60));
        UI_PrintStringSmallBold(buf, 0, 0, 4);
    }
    else
    {
        /* Row 3: current audio offset */
        uint16_t dhz = d->cur_audio_dhz;
        uint16_t hz  = dhz / 10;
        uint16_t d1  = dhz % 10;
        sprintf(buf, "AF:%4u.%u Hz", hz, d1);
        UI_PrintStringSmallNormal(buf, 0, 0, 4);
    }

    /* Row 4: actual RF output frequency */
    {
        uint32_t f = d->cur_rf_freq;
        uint32_t mhz  = f / 100000;
        uint32_t frac = f % 100000;
        sprintf(buf, "RF:%3u.%05u", (unsigned)mhz, (unsigned)frac);
        UI_PrintStringSmallNormal(buf, 0, 0, 5);
    }

    /* Row 6: debug info */
    sprintf(buf, "FIFO:%u CRC:%u",
            (unsigned)d->fifo_depth,
            (unsigned)d->crc_fail_count);
    UI_PrintStringSmallNormal(buf, 0, 0, 6);

    ST7565_BlitFullScreen();
}
