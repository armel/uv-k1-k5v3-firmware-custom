/* Copyright 2025 muzkr
 * https://github.com/muzkr
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

#ifndef DRIVER_PY25Q16_H
#define DRIVER_PY25Q16_H

#include <stdint.h>
#include <stdbool.h>

void PY25Q16_Init();
void PY25Q16_ReadBuffer(uint32_t Address, void *pBuffer, uint32_t Size);
void PY25Q16_ReadBufferSafe(uint32_t Address, void *pBuffer, uint32_t Size);
void PY25Q16_WriteBuffer(uint32_t Address, const void *pBuffer, uint32_t Size, bool Append);
void PY25Q16_SectorErase(uint32_t Address);

/* Drop the internal single-sector write cache. Call after erasing/programming
 * flash behind the driver's back (e.g. the raw multiboot slot/profile ops) so a
 * later write cannot skip or resurrect data based on a stale cached sector. It
 * is also called before multiboot reuses the cache storage as a RAM overlay. */
void PY25Q16_InvalidateCache(void);

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
/*
 * Multiboot per-profile config banking.
 *
 * Each firmware slot owns a private copy of the user configuration (memory
 * channels, names, VFOs, settings). A non-zero profile base transparently
 * shifts every flash access BELOW PY25Q16_PROFILE_SHARED_FROM into the active
 * profile's bank; calibration, boot logo, firmware slots and the multiboot
 * marker all live at/above that boundary and stay shared across every profile.
 *
 * This is the single choke point: both the EEPROM emulation (eeprom_compat.c)
 * and the firmware's direct config reads/writes (settings.c) end up here, so
 * one offset covers them all - no per-call-site patching.
 *
 * Set once at boot, before any settings read, from
 *   PY25Q16_SetProfileBase(MB_ProfileBase(MB_BootResolveProfile()));
 * and never changed again during a session (the profile only changes through a
 * reflash + reset), so the banking itself never needs a cache flush. Raw profile
 * erases behind the driver explicitly call PY25Q16_InvalidateCache().
 */
#define PY25Q16_PROFILE_SHARED_FROM  0x00010000u   /* calibration boundary (see flash map) */
void PY25Q16_SetProfileBase(uint32_t Base);
#endif

#endif
