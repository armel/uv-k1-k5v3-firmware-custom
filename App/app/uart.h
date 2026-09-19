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

#ifndef APP_UART_H
#define APP_UART_H

#include <stdbool.h>

enum
{
#if defined(ENABLE_UART)
    UART_PORT_UART,
#endif
#if defined(ENABLE_USB)
    UART_PORT_VCP,
#endif
};

bool UART_IsCommandAvailable(uint32_t Port);
void UART_HandleCommand(uint32_t Port);
void UART_ServiceCommands(void);

#ifdef ENABLE_CAT
void UART_ReportRSSI_Periodic(void);
void UART_ReportDTMF(char dtmf_code);
void UART_SendText(uint32_t Port, const char *str);
#endif

#endif
