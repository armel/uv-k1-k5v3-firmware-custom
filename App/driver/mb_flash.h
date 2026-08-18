/* Copyright 2026 F4HWN
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

/*
 * Multiboot flash programmer.
 *
 * M1 (validated): brick-critical core - reprogram the internal application flash
 * from an image held in the external SPI flash, running from RAM.
 *
 * M2 (this file): slot format + integrity validation. Each slot starts with a
 * header (magic, size, CRC32, name, version); a restore validates the header and
 * the image CRC32 *before* erasing anything, so an incompatible or corrupt image
 * can never brick the radio. There is a single firmware for both the K1 and the
 * K5v3 (the keypad difference is handled at runtime by the hidden SetNav menu),
 * so no per-model guard is needed.
 *
 * See docs/multiboot-design.md for the overall design.
 */

#ifndef DRIVER_MB_FLASH_H
#define DRIVER_MB_FLASH_H

#include <stdint.h>
#include <stdbool.h>

/* Internal flash application region (see Core/py32f071xb.ld:
 * FLASH origin 0x08002800, length 118 KiB). 0x08002800 is 256-byte aligned, so
 * the whole region can be page-erased (256 B granularity) without touching the
 * factory bootloader that lives just below it. */
#define MB_INT_APP_BASE     0x08002800u
#define MB_INT_APP_SIZE     0x0001D800u   /* 118 KiB */

/* External SPI flash slot layout (see docs/multiboot-design.md).
 * Each 128 KiB slot = one header sector (4 KiB) followed by the image.
 * Slot 0 is the firmware-managed BACKUP of the normally-flashed firmware
 * (written by MB_BackupInternalToSlot0, protected from host writes); slots
 * 1..4 are the user slots managed from UV Studio. */
#define MB_SLOT_STRIDE      0x00020000u   /* 128 KiB per slot            */
#define MB_SLOT_IMG_OFFSET  0x00001000u   /* image starts after header sector */
#define MB_SLOT0_EXT_BASE   0x00020000u   /* slot 0 (backup) header base  */
#define MB_SLOT_COUNT       5u            /* slot 0 backup + slots 1..4   */
#define MB_SLOT_BACKUP      0u            /* slot 0 = firmware base backup */

/* Slot header (stored at the slot base, first 4 KiB sector). 64 bytes. */
#define MB_SLOT_MAGIC       0x31424D46u   /* "FMB1" */
#define MB_HDR_VERSION      1u
#define MB_FLAG_COMMITTED   (1u << 0)     /* image written and verified */
#define MB_NAME_LEN         16
#define MB_VERSION_LEN      16

typedef struct __attribute__((packed)) {
    uint32_t magic;                    /* MB_SLOT_MAGIC                    */
    uint16_t hdr_version;              /* MB_HDR_VERSION                   */
    uint16_t flags;                    /* MB_FLAG_COMMITTED, ...           */
    uint32_t image_size;              /* bytes, <= MB_INT_APP_SIZE        */
    uint32_t image_crc32;             /* CRC-32 (zlib) over image_size B  */
    char     name[MB_NAME_LEN];       /* human-readable, NUL-terminated   */
    char     fw_version[MB_VERSION_LEN]; /* firmware version string        */
    uint8_t  reserved[16];            /* pad to 64 bytes, future use      */
} mb_slot_header_t;

/* Restore validation result (MB_OK never returns - the radio resets). */
enum {
    MB_OK = 0,
    MB_ERR_MAGIC,        /* no/invalid slot header            */
    MB_ERR_VERSION,      /* header format too new             */
    MB_ERR_NOT_COMMITTED,/* image not marked complete         */
    MB_ERR_SIZE,         /* image_size out of range           */
    MB_ERR_CRC,          /* image CRC32 mismatch              */
    MB_ERR_SPI,          /* external flash read/write timed out*/
    MB_ERR_SLOT,         /* slot index out of range           */
    MB_ERR_AUTH          /* write refused: timestamp mismatch */
};

/* Multi-slot API used by the boot selector. Validation always covers the full
 * image CRC before restore. progress_line may point to a 128-byte LCD page; the
 * RAM copier then fills it while reflashing. Pass NULL to disable LCD updates. */
uint8_t MB_ValidateSlot(uint8_t slot, mb_slot_header_t *out_header, uint32_t *out_crc);
uint8_t MB_RestoreSlot(uint8_t slot, uint8_t *progress_line);

/*
 * Host-tool slot management (M4, "Firmware Slots" in UV Studio). Everything is
 * bounds-checked (slot < MB_SLOT_COUNT, offset+len <= MB_SLOT_STRIDE) and writes
 * touch the EXTERNAL flash only, so none of this is brick-critical: a bad slot is
 * simply refused at restore by the CRC validation above.
 *
 *  - MB_SlotInfo   reads the 64-byte header only (fast, no CRC recompute) and
 *                  returns MB_OK / MB_ERR_* describing the header state.
 *  - MB_SlotErase  erases the whole 128 KiB slot region (header + image).
 *  - MB_SlotWrite  programs `len` bytes at slot_base+offset. The slot MUST have
 *                  been erased first (NOR flash only clears 1->0 bits); the host
 *                  writes the image, then the COMMITTED header last.
 * Use MB_ValidateSlot afterwards to confirm the full image CRC.
 */
uint8_t MB_SlotInfo(uint8_t slot, mb_slot_header_t *out_header);
uint8_t MB_SlotErase(uint8_t slot);
uint8_t MB_SlotWrite(uint8_t slot, uint32_t offset, const uint8_t *data, uint32_t len);

/* -------------------------------------------------------------------------- */
/* Per-profile settings banks.                                                */
/*                                                                            */
/* Each firmware slot gets its own copy of the user configuration (memory     */
/* channels, names, VFOs, settings) so switching firmware no longer shares -  */
/* or silently clobbers - settings between editions. The banking itself is a  */
/* single address offset applied in the flash driver (see                     */
/* PY25Q16_SetProfileBase); everything below PY25Q16_PROFILE_SHARED_FROM      */
/* (0x010000, the calibration boundary) is per-profile, everything at/above   */
/* stays shared. Slot N is bound to profile N. Profile 0 (slot 0 = base       */
/* backup) reuses the historical config region at 0x000000 - no migration.    */
/*                                                                            */
/* External SPI flash (PY25Q16, 2 MiB) map:                                   */
/*   0x000000  profile 0 config  (channels/settings)  ] per-profile           */
/*   0x010000  calibration (512 B)                     ] shared               */
/*   0x011000  boot logo (4 KiB)                       ] shared               */
/*   0x020000  slot 0 firmware  (BACKUP, 128 KiB)      ] firmware-managed      */
/*   0x040000  slot 1 firmware  (128 KiB)              ]                       */
/*   0x060000  slot 2 firmware                         ] user (UV Studio)      */
/*   0x080000  slot 3 firmware                         ]                       */
/*   0x0A0000  slot 4 firmware                         ]                       */
/*   0x0C0000  profile 1 config  (64 KiB)              ]                       */
/*   0x0D0000  profile 2 config  (64 KiB)              ] per-profile           */
/*   0x0E0000  profile 3 config  (64 KiB)              ]                       */
/*   0x0F0000  profile 4 config  (64 KiB)              ]                       */
/*   0x100000  multiboot state A (4 KiB marker)        ] shared               */
/*   0x101000  multiboot state B (4 KiB marker)        ] redundant            */
/*   0x102000  -- free ~888 KiB --                                            */
/*   0x1E0000  RX/TX log (32 KiB)                       ] shared               */
/*                                                                            */
/* Banks are 64 KiB for headroom; the live config footprint is ~44 KiB (max   */
/* physical config address 0x00A170). Keep the profile banks past the last    */
/* slot if MB_SLOT_COUNT ever grows.                                          */

#define MB_PROFILE_COUNT        MB_SLOT_COUNT     /* one profile per slot (0..4) */
#define MB_PROFILE_BANK_SIZE    0x00010000u       /* 64 KiB per config bank      */
#define MB_PROFILE1_EXT_BASE    0x000C0000u       /* profiles 1..4, right after slot 4 */
#define MB_PROFILE_STATE_A_BASE 0x00100000u       /* redundant marker sector A */
#define MB_PROFILE_STATE_B_BASE 0x00101000u       /* redundant marker sector B */

/* Active-profile marker: two alternating external-flash sectors, never banked,
 * outside the EEPROM logical map. A new record is verified in the inactive
 * sector before it supersedes the previous one, so a power loss always leaves
 * at least one usable state. The expected firmware identity is stored here as
 * well: boot resolution never depends on the slot header remaining readable. */
#define MB_PROFILE_LEGACY_MAGIC 0x31504D46u       /* "FMP1" (single 8-byte record) */
#define MB_PROFILE_MAGIC        0x32504D46u       /* "FMP2" (redundant identity record) */
typedef struct __attribute__((packed)) {
    uint32_t magic;        /* MB_PROFILE_MAGIC                         */
    uint32_t generation;   /* monotonically increasing record version */
    uint32_t image_size;   /* expected internal image size             */
    uint32_t image_crc32;  /* expected internal image CRC-32            */
    uint8_t  index;        /* active profile 0..MB_PROFILE_COUNT-1     */
    uint8_t  index_inv;    /* ~index, quick integrity check            */
    uint8_t  reserved[2];  /* fixed zero for deterministic state CRC   */
    uint32_t state_crc32;  /* CRC-32 over all preceding fields          */
} mb_profile_state_t;

/* External-flash base of a profile's config bank. Profile 0 -> 0 (historical
 * region, identity map); profiles 1..N -> past the slots. Out-of-range -> 0. */
uint32_t MB_ProfileBase(uint8_t profile);

/* Result of reading the active-profile marker. A boot must treat these very
 * differently: MISSING = fresh radio (adopting the running firmware as Main is
 * fine); IO / CORRUPT = uncertain (must NOT overwrite Main). */
typedef enum {
    MB_MARK_VALID = 0,   /* read OK, well-formed; *state populated       */
    MB_MARK_LEGACY,      /* valid FMP1 index; identity not stored        */
    MB_MARK_MISSING,     /* read OK but both sectors erased            */
    MB_MARK_CORRUPT,     /* read OK but magic/integrity bad            */
    MB_MARK_IO,          /* could not be read (SPI error)              */
} mb_mark_status_t;

/* Read the newest valid active-profile marker, distinguishing the states above. */
mb_mark_status_t MB_ReadActiveProfile(mb_profile_state_t *state);

/* Convenience wrapper for non-critical callers (e.g. cursor pre-selection):
 * the valid index, or 0 for anything not cleanly VALID. */
uint8_t MB_GetActiveProfile(void);

/* Write the active-profile marker into the inactive redundant sector and read it
 * back to confirm it landed. The previous valid record is kept intact. The slot
 * header supplies the expected internal firmware identity. */
uint8_t MB_SetActiveProfile(uint8_t profile);

/* Erase a profile's whole config bank (host "Reset config" for a user slot):
 * the next boot on that slot reads 0xFF and re-seeds factory defaults. Profile 0
 * (the base) is refused - reset it by factory-resetting the base firmware.
 * External flash only, never brick-critical. */
uint8_t MB_ProfileErase(uint8_t profile);

/* -------------------------------------------------------------------------- */
/* Slot 0 self-backup (base firmware).                                        */
/* -------------------------------------------------------------------------- */

/* Progress callback for the (slow) slot-0 self-backup; may be NULL. */
typedef void (*mb_progress_fn)(uint32_t done, uint32_t total);

/* Does the running internal firmware carry the DECLARED identity of `slot`
 * (CRC32 over image_size == the slot header's image_crc32)? This checks the
 * header's claim, not the stored image itself (that is validated once, right
 * after a backup). IO is reported separately so a transient SPI error is never
 * mistaken for a mismatch - which would wrongly trigger a self-backup over Main. */
typedef enum {
    MB_FW_MATCH = 0,   /* internal carries this slot's declared identity */
    MB_FW_MISMATCH,    /* reliably not this slot (or slot has no header)  */
    MB_FW_IO,          /* slot header unreadable: uncertain               */
} mb_fw_match_t;

mb_fw_match_t MB_InternalMatchesSlot(uint8_t slot);

/* Compare internal flash with the identity stored in a validated marker. */
bool MB_InternalMatchesProfile(const mb_profile_state_t *state);

/* Back up the running internal firmware into slot 0 (erase + full image +
 * COMMITTED header, name=edition, version) and validate the stored image by a
 * full external CRC read-back. External flash only, never brick-critical; a
 * failure (SPI or CRC) leaves the slot uncommitted and does not advance the
 * marker, so boot resolution retries it. progress may be NULL. */
uint8_t MB_BackupInternalToSlot0(mb_progress_fn progress);

#endif /* DRIVER_MB_FLASH_H */
