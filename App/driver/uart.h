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
 *
 */

#ifndef DRIVER_UART_H
#define DRIVER_UART_H

#include <stdint.h>
#include <stdbool.h>

// The alignment is part of the contract, not a hint. UART_DMA_Buffer is the
// destination of a circular DMA transfer, and the linker places VCP_ReplyBuf
// directly after it with no padding. If a build shifts this buffer onto an odd
// address - which any change to the size of a .bss object earlier in the link
// can do - the transfer runs into the reply buffer and the application stops
// answering CAT over USB.
extern uint8_t UART_DMA_Buffer[256] __attribute__((aligned(4)));

void UART_Init(void);
void UART_Send(const void *pBuffer, uint32_t Size);
void UART_LogSend(const void *pBuffer, uint32_t Size);

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    bool UART_IsCableConnected(void);
#endif

#endif

