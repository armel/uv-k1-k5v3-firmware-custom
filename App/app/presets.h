/* DualMode channel presets - PMR446, Emergency, Marine
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_PRESETS_H
#define APP_PRESETS_H

#ifdef ENABLE_FEAT_DUALMODE

#include <stdbool.h>
#include <stdint.h>

/* Load preset channels to EEPROM (List 1: PMR446, List 2: Emergency+Marine, List 3: empty) */
void PRESETS_LoadDualModeChannels(void);

/* Save frequency to first free channel in List 3. freqHz=0 uses gRxVfo. Returns true if saved. */
bool PRESETS_QuickBookmark(uint32_t freqHz);

#endif

#endif
