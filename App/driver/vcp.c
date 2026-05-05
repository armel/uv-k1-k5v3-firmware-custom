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

#include "driver/vcp.h"
#include "usb_config.h"
#include "py32f071_ll_bus.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "driver/keyboard.h"
#endif

uint8_t VCP_RxBuf[VCP_RX_BUF_SIZE];
volatile uint32_t VCP_RxBufPointer = 0;

void VCP_Init()
{
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA); // PA12:11
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USBD);

    cdc_acm_rx_buf_t rx_buf = {
        .buf = VCP_RxBuf,
        .size = sizeof(VCP_RxBuf),
        .write_pointer = &VCP_RxBufPointer,
    };
    cdc_acm_init(rx_buf);

    NVIC_SetPriority(USBD_IRQn, 3);
    NVIC_EnableIRQ(USBD_IRQn);
}

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
bool VCP_ScreenshotPing(void)
{
    // State machine for parsing incoming packets:
    //   Keepalive:       0x55 0xAA 0x00 0x00  → viewer alive
    //   Short key press: 0xAA 0x55 0x03 <key> → inject short press
    //   Long key press:  0xAA 0x55 0x04 <key> → inject long press
    //
    // State transitions:
    //   IDLE  → 0x55 → KA_1
    //   KA_1  → 0xAA → KA_2       (else IDLE)
    //   KA_2  → 0x00 → KA_3       (else IDLE)
    //   KA_3  → 0x00 → keepalive OK, IDLE
    //
    //   IDLE   → 0xAA → KEY_1
    //   KEY_1  → 0x55 → KEY_2     (else IDLE)
    //   KEY_2  → 0x03 → KEY_3     (short press, else check long)
    //   KEY_2  → 0x04 → KEY_3L    (long press, else IDLE)
    //   KEY_3  → <b>  → KEYBOARD_InjectKey(b, false), IDLE
    //   KEY_3L → <b>  → KEYBOARD_InjectKey(b, true), IDLE

    static uint8_t      read_ptr = 0;
    static ParseState_t state    = STATE_IDLE;

    bool     connected = false;
    uint8_t  write_ptr = VCP_RxBufPointer;  // snapshot once — ISR may update concurrently

    // Cap bytes processed per call to VCP_RX_BUF_SIZE.
    // Prevents unbounded loop if the ISR write pointer laps read_ptr
    // (buffer overflow / corrupted state), which would freeze the firmware.
    uint32_t processed = 0;

    while (read_ptr != write_ptr && processed < VCP_RX_BUF_SIZE)
    {
        uint8_t b = VCP_RxBuf[read_ptr];

        read_ptr++;
        if (read_ptr >= VCP_RX_BUF_SIZE)
            read_ptr = 0;
        processed++;

        if(KEYBOARD_ProcessProtocolByte(&state, b))
            connected = true;
    }

    return connected;
}
#endif // ENABLE_FEAT_F4HWN_SCREENSHOT
