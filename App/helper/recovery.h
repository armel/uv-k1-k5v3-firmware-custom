/* Copyright 2025 Armel FAUVEAU (F4HWN)
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

#ifndef HELPER_RECOVERY_H
#define HELPER_RECOVERY_H

#include <stdbool.h>

// True when the external-flash calibration zone (0x010000, 512 bytes) is
// entirely erased (all 0xFF) or entirely zeroed (all 0x00) -- i.e. a radio
// whose calibration has been wiped by a faulty firmware. The check is
// deliberately strict: a real calibration always contains mixed bytes, so it
// can never be mistaken for a wiped one.
bool RECOVERY_CalibrationIsWiped(void);

// Recovery waiting screen. Displays "RESTORE CALIBRATION" and keeps the
// UART/USB link alive so an external tool (UV Studio) can rewrite the
// calibration zone. Never returns: once a valid calibration has been written
// and the link has gone quiet, the radio is reset to boot normally.
//
// This path deliberately bypasses the battery / reduced-service logic that
// would otherwise trap a wiped radio in a reboot loop.
void RECOVERY_Loop(void) __attribute__((noreturn));

#endif
