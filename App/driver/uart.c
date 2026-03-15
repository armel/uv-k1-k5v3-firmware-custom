/* Copyright 2025 muzkr https://github.com/muzkr
 * Copyright 2023 Dual Tachyon
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

#include <stdbool.h>
#include <string.h>
#include "py32f071_ll_bus.h"
#include "py32f071_ll_system.h"
#include "py32f071_ll_dma.h"
#include "py32f071_ll_gpio.h"
#include "py32f071_ll_usart.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "driver/keyboard.h"
// Packet types for serial key injection (K5Viewer → radio)
#define UART_TYPE_KEY       0x03
#define UART_TYPE_KEY_LONG  0x04
#endif

#define USARTx USART1
#define DMA_CHANNEL LL_DMA_CHANNEL_2

// CRITICAL FIX: Define UART TX timeout
// If UART TX buffer doesn't clear within this many iterations, skip byte and continue
// Prevents permanent freeze if UART is stuck
#define UART_TX_TIMEOUT_ITERATIONS 10000

static bool UART_IsLogEnabled;
uint8_t UART_DMA_Buffer[256];

void UART_Init(void)
{
    // PA9 TX
    // PA10 RX

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_USART1);

    // Pins
    do
    {
        LL_GPIO_InitTypeDef GPIO_InitStruct;
        LL_GPIO_StructInit(&GPIO_InitStruct);
        GPIO_InitStruct.Pin = LL_GPIO_PIN_9 | LL_GPIO_PIN_10;
        GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
        GPIO_InitStruct.Alternate = LL_GPIO_AF1_USART1;
        GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
        GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;

        LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    } while (0);

    // DMA
    do
    {
        LL_DMA_DisableChannel(DMA1, DMA_CHANNEL);

        LL_DMA_InitTypeDef DMA_InitStruct;
        LL_DMA_StructInit(&DMA_InitStruct);

        DMA_InitStruct.Direction = LL_DMA_DIRECTION_PERIPH_TO_MEMORY;
        DMA_InitStruct.Mode = LL_DMA_MODE_CIRCULAR;
        DMA_InitStruct.PeriphOrM2MSrcAddress = LL_USART_DMA_GetRegAddr(USARTx);
        DMA_InitStruct.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
        DMA_InitStruct.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE;
        DMA_InitStruct.MemoryOrM2MDstAddress = (uint32_t)UART_DMA_Buffer;
        DMA_InitStruct.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE;
        DMA_InitStruct.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;
        DMA_InitStruct.NbData = sizeof(UART_DMA_Buffer);
        DMA_InitStruct.Priority = LL_DMA_PRIORITY_HIGH;

        LL_DMA_Init(DMA1, DMA_CHANNEL, &DMA_InitStruct);

        LL_SYSCFG_SetDMARemap(DMA1, DMA_CHANNEL, LL_SYSCFG_DMA_MAP_USART1_RD);

    } while (0);

    LL_APB1_GRP2_ForceReset(LL_APB1_GRP2_PERIPH_USART1);
    LL_APB1_GRP2_ReleaseReset(LL_APB1_GRP2_PERIPH_USART1);

    // USART
    do
    {
        LL_USART_Disable(USARTx);

        LL_USART_InitTypeDef USART_InitStruct;
        LL_USART_StructInit(&USART_InitStruct);

        USART_InitStruct.BaudRate = 38400;
        USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
        LL_USART_Init(USARTx, &USART_InitStruct);

        LL_USART_EnableDMAReq_RX(USARTx);

    } while (0);

    LL_DMA_EnableChannel(DMA1, DMA_CHANNEL);
    LL_USART_Enable(USARTx);
    LL_USART_TransmitData8(USARTx, 0);
}

void UART_Send(const void *pBuffer, uint32_t Size)
{
    const uint8_t *pData = (const uint8_t *)pBuffer;
    uint32_t i;

    for (i = 0; i < Size; i++)
    {
        // CRITICAL FIX: Add timeout to UART TX busy-wait
        // Original: while (!LL_USART_IsActiveFlag_TXE(USARTx)) ;
        // Problem: If UART TX stuck or disconnected, this is infinite loop
        // Result: Radio freeze for 100ms+ while trying to send one character
        //
        // Solution: Add iteration counter timeout
        // If TXE flag doesn't set within timeout iterations, skip byte and continue
        // This caps maximum freeze at ~10ms per UART_Send() call
        
        uint32_t timeout = UART_TX_TIMEOUT_ITERATIONS;
        while (!LL_USART_IsActiveFlag_TXE(USARTx) && timeout > 0)
        {
            timeout--;
        }
        
        // Send byte only if TXE flag is set
        // If timeout occurred, skip this byte and continue
        if (timeout > 0)
        {
            LL_USART_TransmitData8(USARTx, pData[i]);
        }
    }
}

void UART_LogSend(const void *pBuffer, uint32_t Size)
{
    if (UART_IsLogEnabled) {
        UART_Send(pBuffer, Size);
    }
}

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
    bool UART_IsCableConnected(void) {
        typedef enum {
            STATE_IDLE = 0,
            STATE_KA_1,
            STATE_KA_2,
            STATE_KA_3,
            STATE_KEY_1,
            STATE_KEY_2,
            STATE_KEY_3,
            STATE_KEY_3L,
        } ParseState_t;

        static uint8_t     read_ptr = 0;
        static ParseState_t state   = STATE_IDLE;

        bool connected = false;

        // DMA write position: NbData counts DOWN from 256
        uint8_t write_ptr = (uint8_t)(sizeof(UART_DMA_Buffer) - 
                                       LL_DMA_GetDataLength(DMA1, DMA_CHANNEL));

        uint8_t processed = 0;
        while (read_ptr != write_ptr && processed < sizeof(UART_DMA_Buffer))
        {
            uint8_t b = UART_DMA_Buffer[read_ptr++];
            // read_ptr wraps naturally at 256 since it's uint8_t
            processed++;

            switch (state)
            {
                case STATE_IDLE:
                    if      (b == 0x55) state = STATE_KA_1;
                    else if (b == 0xAA) state = STATE_KEY_1;
                    break;
                case STATE_KA_1:
                    state = (b == 0xAA) ? STATE_KA_2 : STATE_IDLE;
                    break;
                case STATE_KA_2:
                    state = (b == 0x00) ? STATE_KA_3 : STATE_IDLE;
                    break;
                case STATE_KA_3:
                    if (b == 0x00) connected = true;
                    state = STATE_IDLE;
                    break;
                case STATE_KEY_1:
                    state = (b == 0x55) ? STATE_KEY_2 : STATE_IDLE;
                    break;
                case STATE_KEY_2:
                    if      (b == UART_TYPE_KEY)      state = STATE_KEY_3;
                    else if (b == UART_TYPE_KEY_LONG) state = STATE_KEY_3L;
                    else                              state = STATE_IDLE;
                    break;
                case STATE_KEY_3:
                    KEYBOARD_InjectKey(b);
                    connected = true;
                    state = STATE_IDLE;
                    break;
                case STATE_KEY_3L:
                    KEYBOARD_InjectKeyLong(b);
                    connected = true;
                    state = STATE_IDLE;
                    break;
                default:
                    state = STATE_IDLE;
                    break;
            }
        }

        return connected;
    }
#endif