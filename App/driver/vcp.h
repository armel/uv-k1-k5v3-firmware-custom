/* Copyright 2025 muzkr https://github.com/muzkr
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
 *
 */

#ifndef _DRIVER_VCP_H
#define _DRIVER_VCP_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "usb_config.h"

#define VCP_RX_BUF_SIZE 256

extern uint8_t VCP_RxBuf[VCP_RX_BUF_SIZE];
extern volatile uint32_t VCP_RxBufPointer;

void VCP_Init();

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
bool VCP_ScreenshotPing(void);
#endif

static inline void VCP_Send(const uint8_t *Buf, uint32_t Size)
{
    cdc_acm_data_send_with_dtr(Buf, Size);
}

static inline void VCP_SendStr(const char *Str)
{
    if (Str)
    {
        cdc_acm_data_send_with_dtr((const uint8_t *)Str, strlen(Str));
    }
}

static inline void VCP_SendAsync(const uint8_t *Buf, uint32_t Size)
{
    cdc_acm_data_send_with_dtr_async(Buf, Size);
}

#endif // _DRIVER_VCP_H
