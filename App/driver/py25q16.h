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

#ifdef ENABLE_FEAT_F4HWN_EXT_FLASH_RW
/* Full external-flash access by TRUE physical address, bypassing the active
 * config-bank mapping (BankMap) and the sector cache. Backs the host
 * dump/restore UART commands (uart.c: 0x0738 read, 0x073A sector erase,
 * 0x073C write), so the whole 2 MiB image can be captured and every
 * non-calibration sector can be rewritten regardless of which config bank is
 * currently selected. The UART layer protects calibration mutations. */

/* Total capacity of the external SPI flash (2 MiB). */
#define PY25Q16_TOTAL_SIZE  0x00200000u

/* Physical erase granularity exposed by the raw host protocol. */
#define PY25Q16_SECTOR_SIZE 0x00001000u

/* Device-specific calibration occupies the start of this protected sector. */
#define PY25Q16_CALIBRATION_SECTOR_BASE 0x00010000u

/* Largest raw transfer accepted by one host command. */
#define PY25Q16_RAW_CHUNK_SIZE 128u

/* Raw read: waits for WIP=0, then reads by physical address. */
void PY25Q16_ReadBufferPhysical(uint32_t Address, void *pBuffer, uint32_t Size);

/* Raw 4 KiB sector erase; Address must already be sector-aligned. */
void PY25Q16_SectorErasePhysical(uint32_t Address);

/* Raw page-program by physical address; the sector must already be erased. */
void PY25Q16_WriteBufferPhysical(uint32_t Address, const void *pBuffer, uint32_t Size);
#endif

/* Drop the internal single-sector write cache. Call after erasing/programming
 * flash behind the driver's back (e.g. the raw multiboot slot/bank ops) so a
 * later write cannot skip or resurrect data based on a stale cached sector. It
 * is also called before multiboot reuses the cache storage as a RAM overlay. */
void PY25Q16_InvalidateCache(void);

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
/* The 4 KiB sector cache, reused as the overlay-app execution workspace. */
uint8_t *PY25Q16_OverlayBuffer(void);
#endif

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
/*
 * Multiboot per-bank config banking.
 *
 * Each firmware slot gets its own config bank by default (memory channels,
 * names, VFOs, settings), though SetCfg can point the running firmware at a
 * different bank. A non-zero bank base transparently shifts every flash access
 * BELOW PY25Q16_BANK_SHARED_FROM into the active bank; calibration, boot logo,
 * firmware slots and the multiboot marker all live at/above that boundary and
 * stay shared across every bank.
 *
 * This is the single choke point: both the EEPROM emulation (eeprom_compat.c)
 * and the firmware's direct config reads/writes (settings.c) end up here, so
 * one offset covers them all - no per-call-site patching.
 *
 * Set once at boot, before any settings read, from
 *   PY25Q16_SetBankBase(MB_BankBase(MB_BootResolveState()));
 * and never changed again during a session (a slot restore or SetCfg selection
 * records the next bank and resets first), so the banking itself never needs a
 * cache flush. Raw bank erases behind the driver explicitly call
 * PY25Q16_InvalidateCache().
 */
#define PY25Q16_BANK_SHARED_FROM  0x00010000u   /* calibration boundary (see flash map) */
void PY25Q16_SetBankBase(uint32_t Base);
#endif

#endif
