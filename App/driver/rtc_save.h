/* Copyright 2026
 * https://github.com/armel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * RTC time persistence: save the 2000-epoch Beijing seconds counter to
 * external SPI Flash so the radio can restore it on the next power-up.
 * Note: the RTC stops when the radio is off, so the restored value is
 * the last saved time, not the current time.
 */

#ifndef DRIVER_RTC_SAVE_H
#define DRIVER_RTC_SAVE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

// Saves the current RTC counter to Flash.
void RTC_SaveTimeToFlash(void);

// Loads the saved RTC counter from Flash and applies it to the RTC.
// Returns true if a valid saved time was found and applied.
bool RTC_LoadTimeFromFlash(void);

#endif // ENABLE_FEAT_F4HWN_DOPPLER

#endif // DRIVER_RTC_SAVE_H
