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

#ifndef UI_WELCOME_H
#define UI_WELCOME_H

#include <stdbool.h>
#include <stdint.h>

void UI_DisplayReleaseKeys(void);
void UI_DisplayWelcome(void);

#ifdef ENABLE_FEAT_F4HWN_QRCODE
// Draw the embedded GitHub QR code (V4, 33x33) at (origin_x, origin_y).
// `wiki` selects the wiki URL bitmap, otherwise the repo URL bitmap.
void UI_DrawQRCode(bool wiki, uint8_t origin_x, uint8_t origin_y);
#endif

#ifdef ENABLE_FEAT_F4HWN_MEM
// Compute current FLASH and SRAM usage as percentages × 100 (e.g. 7559 = 75.59%).
void UI_GetMemPercents(uint16_t *flash_pct_x100, uint16_t *ram_pct_x100);
#endif

#endif

