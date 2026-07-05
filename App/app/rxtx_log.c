/* Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0.
 */

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "app/generic.h"
#include "app/rxtx_log.h"
#include "audio.h"
#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "misc.h"
#include "ui/helper.h"
#include "ui/ui.h"

#define RXTX_LOG_FLASH_BASE          0x1E0000u
#define RXTX_LOG_FLASH_SECTOR_SIZE   0x1000u
#define RXTX_LOG_FLASH_SECTOR_COUNT  8u
#define RXTX_LOG_FLASH_SIZE          (RXTX_LOG_FLASH_SECTOR_SIZE * RXTX_LOG_FLASH_SECTOR_COUNT)
#define RXTX_LOG_FLASH_END           (RXTX_LOG_FLASH_BASE + RXTX_LOG_FLASH_SIZE)
#define RXTX_LOG_SLOT_COUNT          (RXTX_LOG_FLASH_SIZE / sizeof(RXTX_LogFlashEntry_t))
#define RXTX_LOG_VIEW_CACHE_COUNT    7u
#define RXTX_LOG_VIEW_SCAN_BUDGET    8u
#define RXTX_LOG_VIEW_ANCHOR_STRIDE  32u
#define RXTX_LOG_VIEW_ANCHOR_COUNT   ((RXTX_LOG_SLOT_COUNT + RXTX_LOG_VIEW_ANCHOR_STRIDE - 1u) / RXTX_LOG_VIEW_ANCHOR_STRIDE)
#define RXTX_LOG_ENTRY_COMMIT        0xA5u
#define RXTX_LOG_CHANNEL_NONE        0xFFFFu
#define RXTX_LOG_FLAG_TX             (1u << 0)
#define RXTX_LOG_FLAG_NAMED          (1u << 1)
#define RXTX_LOG_FLAG_MONITOR        (1u << 2)
#define RXTX_LOG_FLAG_SESSION        (1u << 3)
#define RXTX_LOG_FILTER_ALL          0u
#define RXTX_LOG_FILTER_RX           1u
#define RXTX_LOG_FILTER_TX           2u

typedef struct __attribute__((packed)) {
    uint32_t sequence;
    uint32_t frequency;
    uint32_t trafficSeq;
    uint16_t durationSeconds;
    uint16_t channel;
    uint8_t  flags;
    char     name[RXTX_LOG_NAME_LEN];
    uint8_t  reserved[3];
    uint8_t  crc;
    uint8_t  commit;
} RXTX_LogFlashEntry_t;

static_assert(sizeof(RXTX_LogFlashEntry_t) == 32);
static_assert(RXTX_LOG_VIEW_ANCHOR_COUNT <= 32);
// RXTX_LogEntry_t (RAM) and RXTX_LogFlashEntry_t must stay byte-identical up
// to the end of name[] so entries can be copied with a single memcpy.
static_assert(offsetof(RXTX_LogEntry_t, name) == offsetof(RXTX_LogFlashEntry_t, name));

static RXTX_LogEntry_t gViewCache[RXTX_LOG_VIEW_CACHE_COUNT];
static uint16_t        gViewCacheStart;
static uint8_t         gViewCacheCount;
static uint8_t         gViewCacheFilter;
static bool            gViewCacheHasOlder;
static bool            gViewCacheComplete;
static bool            gViewScanActive;
static uint16_t        gViewScanSlot;
static uint16_t        gViewScanScanned;
static uint16_t        gViewScanSkip;
static uint16_t        gViewScanIndex;
static uint16_t        gViewAnchorSlots[RXTX_LOG_VIEW_ANCHOR_COUNT];
static uint32_t        gViewAnchorMask;
static uint8_t         gViewAnchorFilter;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
// Wrap-around scrolling state: cycling UP past the first row (or DOWN past
// the last) requires discovering the total row count with a dedicated scan.
static bool            gViewCacheCircular;
static bool            gViewScanDiscoverTotal;
static bool            gViewScanWrapped;
static bool            gViewTotalKnown;
static bool            gViewWrapPending;
static uint16_t        gViewTotalRows;
#endif
static bool            gClearActive;
static uint8_t         gClearSector;
static bool            gMenuClearHandled;
static bool            gLogHasTraffic;
static uint32_t        gNextSequence;
static uint32_t        gNextTrafficSequence;
static uint32_t        gNextFlashAddress;

static bool            gSessionActive;
static uint8_t         gSessionFlags;
static uint32_t        gSessionFrequency;
static uint16_t        gSessionChannel;
static uint16_t        gSessionTicks500ms;
static char            gSessionName[RXTX_LOG_NAME_LEN + 1];

static uint16_t        gLogCursor;
static uint8_t         gLogFilter;

static uint8_t RXTX_LOG_Crc8(const void *data, uint16_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint8_t crc = 0x5Au;

    while (size-- > 0) {
        crc ^= *p++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
        }
    }

    return crc;
}

static bool RXTX_LOG_IsValidFlashEntry(const RXTX_LogFlashEntry_t *entry)
{
    if (entry->commit != RXTX_LOG_ENTRY_COMMIT ||
        entry->sequence == 0xFFFFFFFFu ||
        entry->frequency == 0xFFFFFFFFu)
        return false;

    return entry->crc == RXTX_LOG_Crc8(entry, sizeof(*entry) - 2);
}

static bool RXTX_LOG_IsBlankFlashEntry(const RXTX_LogFlashEntry_t *entry)
{
    const uint8_t *p = (const uint8_t *)entry;

    for (uint8_t i = 0; i < sizeof(*entry); i++) {
        if (p[i] != 0xFFu)
            return false;
    }

    return true;
}

static bool RXTX_LOG_IsTx(const RXTX_LogEntry_t *entry)
{
    return (entry->flags & RXTX_LOG_FLAG_TX) != 0;
}

static bool RXTX_LOG_IsSessionMarker(const RXTX_LogEntry_t *entry)
{
    return (entry->flags & RXTX_LOG_FLAG_SESSION) != 0;
}

static bool RXTX_LOG_IsTrafficFlags(uint8_t flags)
{
    return (flags & RXTX_LOG_FLAG_SESSION) == 0;
}

static bool RXTX_LOG_MatchesFlags(uint8_t flags)
{
    if ((flags & RXTX_LOG_FLAG_SESSION) != 0)
        return gLogFilter == RXTX_LOG_FILTER_ALL;

    if (gLogFilter == RXTX_LOG_FILTER_RX)
        return (flags & RXTX_LOG_FLAG_TX) == 0;

    if (gLogFilter == RXTX_LOG_FILTER_TX)
        return (flags & RXTX_LOG_FLAG_TX) != 0;

    return true;
}

const char *RXTX_LOG_GetFilterName(void)
{
    static const char *const filterNames[] = {"ALL", "RX", "TX"};

    return filterNames[gLogFilter];
}

static void RXTX_LOG_CopyFromFlash(RXTX_LogEntry_t *dst, const RXTX_LogFlashEntry_t *src)
{
    memcpy(dst, src, offsetof(RXTX_LogFlashEntry_t, reserved));
    dst->name[RXTX_LOG_NAME_LEN] = 0;
}

static uint32_t RXTX_LOG_SlotToAddress(uint16_t slot)
{
    return RXTX_LOG_FLASH_BASE + ((uint32_t)slot * sizeof(RXTX_LogFlashEntry_t));
}

static uint16_t RXTX_LOG_AddressToSlot(uint32_t address)
{
    return (uint16_t)((address - RXTX_LOG_FLASH_BASE) / sizeof(RXTX_LogFlashEntry_t));
}

static uint16_t RXTX_LOG_PreviousSlot(uint16_t slot)
{
    return slot == 0 ? (uint16_t)(RXTX_LOG_SLOT_COUNT - 1u) : (uint16_t)(slot - 1u);
}

static uint16_t RXTX_LOG_NextSlot(uint16_t slot)
{
    return (uint16_t)(slot + 1u) >= RXTX_LOG_SLOT_COUNT ? 0 : (uint16_t)(slot + 1u);
}

static void RXTX_LOG_StartViewCacheScan(uint16_t start, bool circular, bool discoverTotal);

static void RXTX_LOG_StartCursorView(uint16_t cursor)
{
    gLogCursor = cursor;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
    gViewWrapPending = false;
    RXTX_LOG_StartViewCacheScan(cursor,
        gViewTotalKnown &&
        gViewTotalRows > 0 &&
        cursor < gViewTotalRows &&
        (uint16_t)(cursor + RXTX_LOG_VIEW_CACHE_COUNT) > gViewTotalRows,
        false);
#else
    RXTX_LOG_StartViewCacheScan(cursor, false, false);
#endif
}

static void RXTX_LOG_InvalidateViewAnchors(void)
{
    gViewAnchorMask = 0;
    gViewAnchorFilter = 0xFFu;
}

static void RXTX_LOG_EnsureViewAnchors(void)
{
    if (gViewAnchorFilter == gLogFilter)
        return;

    gViewAnchorMask = 0;
    gViewAnchorFilter = gLogFilter;
}

static void RXTX_LOG_RecordViewAnchor(uint16_t indexFromNewest, uint16_t slot)
{
    if ((indexFromNewest % RXTX_LOG_VIEW_ANCHOR_STRIDE) != 0)
        return;

    const uint16_t anchor = indexFromNewest / RXTX_LOG_VIEW_ANCHOR_STRIDE;
    if (anchor >= RXTX_LOG_VIEW_ANCHOR_COUNT)
        return;

    gViewAnchorSlots[anchor] = slot;
    gViewAnchorMask |= (uint32_t)(1u << anchor);
}

static bool RXTX_LOG_FindViewAnchor(uint16_t indexFromNewest, uint16_t *anchorIndex, uint16_t *slot)
{
    uint16_t anchor = indexFromNewest / RXTX_LOG_VIEW_ANCHOR_STRIDE;
    if (anchor >= RXTX_LOG_VIEW_ANCHOR_COUNT)
        anchor = RXTX_LOG_VIEW_ANCHOR_COUNT - 1u;

    do {
        if ((gViewAnchorMask & (uint32_t)(1u << anchor)) != 0) {
            *anchorIndex = (uint16_t)(anchor * RXTX_LOG_VIEW_ANCHOR_STRIDE);
            *slot = gViewAnchorSlots[anchor];
            return true;
        }
    } while (anchor-- > 0);

    return false;
}

static void RXTX_LOG_InvalidateViewCache(void)
{
    gViewCacheStart    = 0xFFFFu;
    gViewCacheCount    = 0;
    gViewCacheFilter   = 0xFFu;
    gViewCacheHasOlder = false;
    gViewCacheComplete = false;
    gViewScanActive    = false;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
    gViewCacheCircular = false;
    gViewTotalKnown    = false;
    gViewWrapPending   = false;
    gViewScanDiscoverTotal = false;
    gViewScanWrapped   = false;
#endif
    RXTX_LOG_InvalidateViewAnchors();
}

static void RXTX_LOG_NextFilter(void)
{
    gLogFilter++;
    if (gLogFilter > RXTX_LOG_FILTER_TX)
        gLogFilter = RXTX_LOG_FILTER_ALL;

    gLogCursor = 0;
    RXTX_LOG_InvalidateViewCache();
    gUpdateStatus = true;
    gUpdateDisplay = true;
}

static void RXTX_LOG_ResetLogCounters(void)
{
    gClearSector         = 0;
    gLogHasTraffic       = false;
    gNextSequence        = 0;
    gNextTrafficSequence = 0;
    gNextFlashAddress    = RXTX_LOG_FLASH_BASE;
    RXTX_LOG_InvalidateViewCache();
}

static void RXTX_LOG_StartClear(void)
{
    if (gClearActive)
        return;

    gClearActive   = true;
    gSessionActive = false;
    gLogCursor     = 0;
    RXTX_LOG_ResetLogCounters();
}

static void RXTX_LOG_StepClear(void)
{
    if (!gClearActive)
        return;

    PY25Q16_SectorErase(RXTX_LOG_FLASH_BASE + ((uint32_t)gClearSector * RXTX_LOG_FLASH_SECTOR_SIZE));

    gClearSector++;
    if (gClearSector >= RXTX_LOG_FLASH_SECTOR_COUNT) {
        gClearActive = false;
        RXTX_LOG_ResetLogCounters();
    }
}

static uint16_t RXTX_LOG_PageStart(uint16_t indexFromNewest)
{
    if (indexFromNewest >= RXTX_LOG_SLOT_COUNT)
        indexFromNewest = RXTX_LOG_SLOT_COUNT - 1u;

    return indexFromNewest;
}

static bool RXTX_LOG_AlignLastViewPage(void)
{
    if (gViewScanActive ||
        !gViewCacheComplete ||
        gViewCacheHasOlder ||
        gViewCacheCount == 0)
        return false;

    if (gViewCacheCount >= RXTX_LOG_VIEW_CACHE_COUNT) {
        if (gLogCursor == gViewCacheStart)
            return false;
        gLogCursor = gViewCacheStart;
        return true;
    }

    const uint16_t missingRows = RXTX_LOG_VIEW_CACHE_COUNT - gViewCacheCount;
    const uint16_t start = gViewCacheStart > missingRows ? (uint16_t)(gViewCacheStart - missingRows) : 0;
    if (start == gViewCacheStart)
        return false;

    gLogCursor = start;
    RXTX_LOG_StartViewCacheScan(start, false, false);
    return true;
}

static void RXTX_LOG_StopViewScan(void)
{
    gViewScanActive    = false;
    gViewCacheComplete = true;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
    gViewScanDiscoverTotal = false;
#endif
}

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
static bool RXTX_LOG_TryWrapViewCacheScan(void)
{
    if (!gViewCacheCircular ||
        gViewScanWrapped ||
        !gViewTotalKnown ||
        gViewCacheCount >= RXTX_LOG_VIEW_CACHE_COUNT ||
        gViewScanIndex < gViewTotalRows)
        return false;

    gViewScanWrapped = true;
    gViewScanSlot = RXTX_LOG_AddressToSlot(gNextFlashAddress);
    gViewScanScanned = 0;
    gViewScanSkip = 0;
    gViewScanIndex = 0;
    return true;
}
#endif

static void RXTX_LOG_StepViewCacheScan(void)
{
    uint8_t budget = RXTX_LOG_VIEW_SCAN_BUDGET;
    bool capReached = false;

    while (gViewScanActive && budget-- > 0 && gViewScanScanned < RXTX_LOG_SLOT_COUNT) {
        RXTX_LogFlashEntry_t flashEntry;

        gViewScanSlot = RXTX_LOG_PreviousSlot(gViewScanSlot);
        gViewScanScanned++;

        PY25Q16_ReadBuffer(RXTX_LOG_SlotToAddress(gViewScanSlot), &flashEntry, sizeof(flashEntry));
        if (RXTX_LOG_IsBlankFlashEntry(&flashEntry)) {
            gViewScanScanned = RXTX_LOG_SLOT_COUNT;
            break;
        }

        if (!RXTX_LOG_IsValidFlashEntry(&flashEntry) ||
            !RXTX_LOG_MatchesFlags(flashEntry.flags))
            continue;

        // Traffic entries carry their own absolute rank (trafficSeq); the
        // distance from the newest one bounds how far back browsing goes,
        // no running counter needed to reconstruct it during the scan.
        if (RXTX_LOG_IsTrafficFlags(flashEntry.flags) &&
            (gNextTrafficSequence - 1u - flashEntry.trafficSeq) >= RXTX_LOG_VISIBLE_COUNT) {
            capReached = true;
            RXTX_LOG_StopViewScan();
            break;
        }

        RXTX_LOG_RecordViewAnchor(gViewScanIndex, gViewScanSlot);

        if (gViewScanSkip > 0) {
            gViewScanSkip--;
            gViewScanIndex++;
            continue;
        }

        if (gViewCacheCount < RXTX_LOG_VIEW_CACHE_COUNT) {
            RXTX_LOG_CopyFromFlash(&gViewCache[gViewCacheCount], &flashEntry);
            gViewCacheCount++;
        } else {
            gViewCacheHasOlder = true;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
            if (!gViewScanDiscoverTotal) {
                RXTX_LOG_StopViewScan();
                break;
            }
#else
            RXTX_LOG_StopViewScan();
            break;
#endif
        }

        gViewScanIndex++;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
        if (RXTX_LOG_TryWrapViewCacheScan())
            continue;
#endif
    }

    if (gViewScanScanned >= RXTX_LOG_SLOT_COUNT || capReached) {
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
        if (!gViewScanWrapped) {
            gViewTotalRows  = gViewScanIndex;
            gViewTotalKnown = gViewTotalRows > 0;
        }

        if (gViewWrapPending) {
            gViewWrapPending = false;
            if (gViewTotalRows > 1u) {
                RXTX_LOG_StartCursorView(gViewTotalRows - 1u);
                return;
            }
        }

        if (RXTX_LOG_TryWrapViewCacheScan())
            return;
#endif

        RXTX_LOG_StopViewScan();
    }

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
    if (!gViewCacheCircular && RXTX_LOG_AlignLastViewPage())
        return;
#else
    if (RXTX_LOG_AlignLastViewPage())
        return;
#endif
}

static void RXTX_LOG_StartViewCacheScan(uint16_t start, bool circular, bool discoverTotal)
{
    uint16_t anchorIndex;
    uint16_t anchorSlot;

    start = RXTX_LOG_PageStart(start);
    RXTX_LOG_EnsureViewAnchors();

    gViewCacheStart    = start;
    gViewCacheCount    = 0;
    gViewCacheFilter   = gLogFilter;
    gViewCacheHasOlder = false;
    gViewCacheComplete = false;
    gViewScanActive    = false;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
    gViewCacheCircular     = circular;
    gViewScanDiscoverTotal = discoverTotal;
    gViewScanWrapped       = false;
#else
    (void)circular;
    (void)discoverTotal;
#endif

    if (gNextSequence == 0) {
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
        gViewWrapPending = false;
        gViewScanDiscoverTotal = false;
#endif
        gViewCacheComplete = true;
        return;
    }

    if (RXTX_LOG_FindViewAnchor(start, &anchorIndex, &anchorSlot)) {
        gViewScanSlot  = RXTX_LOG_NextSlot(anchorSlot);
        gViewScanSkip  = start - anchorIndex;
        gViewScanIndex = anchorIndex;
    } else {
        gViewScanSlot  = RXTX_LOG_AddressToSlot(gNextFlashAddress);
        gViewScanSkip  = start;
        gViewScanIndex = 0;
    }

    gViewScanScanned = 0;
    gViewScanActive  = true;
}

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
static void RXTX_LOG_RequestWrapToLast(void)
{
    gViewWrapPending = true;

    if (gViewScanActive &&
        gViewCacheFilter == gLogFilter &&
        gViewCacheStart == 0 &&
        !gViewCacheCircular) {
        gViewScanDiscoverTotal = true;
        return;
    }

    RXTX_LOG_StartViewCacheScan(0, false, true);
}
#endif

static bool RXTX_LOG_ViewCacheCovers(uint16_t indexFromNewest)
{
    return gViewCacheFilter == gLogFilter &&
           indexFromNewest >= gViewCacheStart &&
           indexFromNewest < (uint16_t)(gViewCacheStart + gViewCacheCount);
}

static bool RXTX_LOG_SlotIsBlank(uint32_t address)
{
    RXTX_LogFlashEntry_t entry;

    PY25Q16_ReadBuffer(address, &entry, sizeof(entry));

    return RXTX_LOG_IsBlankFlashEntry(&entry);
}

static void RXTX_LOG_PrepareNextSlot(void)
{
    if (gNextFlashAddress >= RXTX_LOG_FLASH_END)
        gNextFlashAddress = RXTX_LOG_FLASH_BASE;

    if (!RXTX_LOG_SlotIsBlank(gNextFlashAddress)) {
        if ((gNextFlashAddress % RXTX_LOG_FLASH_SECTOR_SIZE) != 0) {
            gNextFlashAddress += RXTX_LOG_FLASH_SECTOR_SIZE - (gNextFlashAddress % RXTX_LOG_FLASH_SECTOR_SIZE);
            if (gNextFlashAddress >= RXTX_LOG_FLASH_END)
                gNextFlashAddress = RXTX_LOG_FLASH_BASE;
        }

        if (!RXTX_LOG_SlotIsBlank(gNextFlashAddress)) {
            const uint32_t sector = gNextFlashAddress - (gNextFlashAddress % RXTX_LOG_FLASH_SECTOR_SIZE);
            PY25Q16_SectorErase(sector);
        }
    }
}

static void RXTX_LOG_AdvanceFlashAddress(void)
{
    gNextFlashAddress += sizeof(RXTX_LogFlashEntry_t);
    if (gNextFlashAddress >= RXTX_LOG_FLASH_END)
        gNextFlashAddress = RXTX_LOG_FLASH_BASE;
}

static uint32_t RXTX_LOG_WriteEntry(const RXTX_LogEntry_t *src)
{
    RXTX_LogFlashEntry_t entry;
    uint8_t commit = RXTX_LOG_ENTRY_COMMIT;

    memset(&entry, 0xFF, sizeof(entry));
    memcpy(&entry, src, offsetof(RXTX_LogFlashEntry_t, reserved));
    entry.crc = RXTX_LOG_Crc8(&entry, sizeof(entry) - 2);

    RXTX_LOG_PrepareNextSlot();

    const uint32_t address = gNextFlashAddress;
    PY25Q16_WriteBuffer(gNextFlashAddress, &entry, sizeof(entry), false);
    PY25Q16_WriteBuffer(gNextFlashAddress + sizeof(entry) - 1u, &commit, 1, false);
    RXTX_LOG_AdvanceFlashAddress();

    return address;
}

static void RXTX_LOG_WriteSessionMarker(void)
{
    RXTX_LogEntry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.sequence = gNextSequence++;
    entry.channel  = RXTX_LOG_CHANNEL_NONE;
    entry.flags    = RXTX_LOG_FLAG_SESSION;

    RXTX_LOG_WriteEntry(&entry);
    RXTX_LOG_InvalidateViewCache();
}

static void RXTX_LOG_EnsureViewCache(void)
{
    const uint16_t pageStart = RXTX_LOG_PageStart(gLogCursor);

    if (gViewCacheFilter == gLogFilter &&
        gViewCacheStart == pageStart &&
        (gViewScanActive ||
         gViewCacheComplete ||
         RXTX_LOG_ViewCacheCovers(gLogCursor)))
        return;

    RXTX_LOG_StartCursorView(gLogCursor);
}

static bool RXTX_LOG_GetFilteredEntry(uint16_t indexFromNewest, RXTX_LogEntry_t *entry)
{
    if (indexFromNewest >= RXTX_LOG_SLOT_COUNT)
        return false;

    if (!RXTX_LOG_ViewCacheCovers(indexFromNewest))
        return false;

    *entry = gViewCache[indexFromNewest - gViewCacheStart];
    return true;
}

static void RXTX_LOG_CaptureSession(uint8_t flags, const VFO_Info_t *vfo)
{
    if (gClearActive)
        return;

    const uint32_t frequency = (flags & RXTX_LOG_FLAG_TX) ? vfo->pTX->Frequency : vfo->pRX->Frequency;
    const bool isMemoryChannel = IS_MR_CHANNEL(vfo->CHANNEL_SAVE);
    const uint16_t channel = isMemoryChannel ? vfo->CHANNEL_SAVE : RXTX_LOG_CHANNEL_NONE;

    if (gSessionActive &&
        gSessionFlags == flags &&
        gSessionFrequency == frequency &&
        gSessionChannel == channel)
        return;

    RXTX_LOG_EndActive();

    gSessionActive     = true;
    gSessionFlags      = flags;
    gSessionFrequency  = frequency;
    gSessionChannel    = channel;
    gSessionTicks500ms = 0;
    // vfo->Name is already truncated/trimmed/null-terminated by its sole
    // writer, SETTINGS_FetchChannelName (settings.c), so a plain copy is enough.
    if (isMemoryChannel)
        strcpy(gSessionName, vfo->Name);
    else
        gSessionName[0] = 0;
}

void RXTX_LOG_Init(void)
{
    uint32_t maxSequence = 0;
    uint32_t maxAddress = RXTX_LOG_FLASH_BASE;
    uint32_t maxTrafficSeq = 0;
    uint8_t lastEntryFlags = 0;
    bool found = false;
    bool foundTraffic = false;

    gLogCursor        = 0;
    gLogFilter        = RXTX_LOG_FILTER_ALL;
    gSessionActive    = false;
    gClearActive      = false;
    gClearSector      = 0;
    gMenuClearHandled = false;
    gLogHasTraffic    = false;
    gNextFlashAddress = RXTX_LOG_FLASH_BASE;
    RXTX_LOG_InvalidateViewCache();

    for (uint32_t address = RXTX_LOG_FLASH_BASE; address < RXTX_LOG_FLASH_END; address += sizeof(RXTX_LogFlashEntry_t)) {
        RXTX_LogFlashEntry_t flashEntry;

        PY25Q16_ReadBuffer(address, &flashEntry, sizeof(flashEntry));
        if (!RXTX_LOG_IsValidFlashEntry(&flashEntry))
            continue;

        if (RXTX_LOG_IsTrafficFlags(flashEntry.flags)) {
            gLogHasTraffic = true;
            if (!foundTraffic || flashEntry.trafficSeq > maxTrafficSeq) {
                foundTraffic = true;
                maxTrafficSeq = flashEntry.trafficSeq;
            }
        }

        if (!found || flashEntry.sequence > maxSequence) {
            found = true;
            maxSequence = flashEntry.sequence;
            maxAddress = address;
            lastEntryFlags = flashEntry.flags;
        }
    }

    if (found) {
        gNextSequence = maxSequence + 1u;
        gNextFlashAddress = maxAddress + sizeof(RXTX_LogFlashEntry_t);
        if (gNextFlashAddress >= RXTX_LOG_FLASH_END)
            gNextFlashAddress = RXTX_LOG_FLASH_BASE;
    } else {
        gNextSequence = 0;
    }

    gNextTrafficSequence = foundTraffic ? maxTrafficSeq + 1u : 0;

    // Skip the marker if the log already ends with one (e.g. repeated
    // reboots with no RX/TX in between) to avoid stacking empty separators.
    if (!found || (lastEntryFlags & RXTX_LOG_FLAG_SESSION) == 0)
        RXTX_LOG_WriteSessionMarker();
}

void RXTX_LOG_BeginRx(const VFO_Info_t *vfo, FUNCTION_Type_t function)
{
    if (vfo == NULL)
        return;

    uint8_t flags = 0;
    if (function == FUNCTION_MONITOR)
        flags |= RXTX_LOG_FLAG_MONITOR;

    RXTX_LOG_CaptureSession(flags, vfo);
}

void RXTX_LOG_BeginTx(const VFO_Info_t *vfo)
{
    if (vfo == NULL)
        return;

    RXTX_LOG_CaptureSession(RXTX_LOG_FLAG_TX, vfo);
}

void RXTX_LOG_EndActive(void)
{
    if (!gSessionActive)
        return;

    if (gClearActive) {
        gSessionActive = false;
        return;
    }

    RXTX_LogEntry_t entry;
    memset(&entry, 0, sizeof(entry));

    entry.sequence        = gNextSequence++;
    entry.trafficSeq      = gNextTrafficSequence++;
    entry.frequency       = gSessionFrequency;
    entry.durationSeconds = (gSessionTicks500ms + 1u) / 2u;
    entry.channel         = gSessionChannel;
    entry.flags           = gSessionFlags;
    strcpy(entry.name, gSessionName);

    if (entry.name[0] != 0)
        entry.flags |= RXTX_LOG_FLAG_NAMED;

    if (entry.durationSeconds == 0)
        entry.durationSeconds = 1;

    RXTX_LOG_WriteEntry(&entry);
    gLogHasTraffic = true;
    RXTX_LOG_InvalidateViewCache();

    gSessionActive = false;
}

void RXTX_LOG_Tick500ms(void)
{
    if (gSessionActive && gSessionTicks500ms < 0xFFFEu)
        gSessionTicks500ms++;
}

void RXTX_LOG_Task10ms(void)
{
    if (gClearActive) {
        RXTX_LOG_StepClear();
    } else if (gViewScanActive) {
        RXTX_LOG_StepViewCacheScan();
    } else {
        return;
    }

    if (gScreenToDisplay == DISPLAY_RXTX_LOG)
        gUpdateDisplay = true;
}

void ACTION_RxTxLog(void)
{
    gLogCursor = 0;
    RXTX_LOG_InvalidateViewCache();
    gUpdateStatus = true;
    GUI_SelectNextDisplay(DISPLAY_RXTX_LOG);
}

void RXTX_LOG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (Key == KEY_PTT) {
        GENERIC_Key_PTT(bKeyPressed);
        return;
    }

    if (!bKeyPressed && !bKeyHeld && Key != KEY_MENU)
        return;

    if (gClearActive) {
        if (Key == KEY_MENU && !bKeyPressed)
            gMenuClearHandled = false;
        gUpdateDisplay = true;
        return;
    }

    switch (Key) {
    case KEY_UP:
        if (gLogCursor > 0) {
            RXTX_LOG_StartCursorView(gLogCursor - 1u);
        }
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
        else if (gViewTotalKnown && gViewTotalRows > 1u) {
            RXTX_LOG_StartCursorView(gViewTotalRows - 1u);
        } else {
            RXTX_LOG_RequestWrapToLast();
        }
#endif
        gUpdateDisplay = true;
        break;

    case KEY_DOWN:
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
        gViewWrapPending = false;
#endif
        RXTX_LOG_EnsureViewCache();
        if (gViewCacheCount > 0) {
            if (gViewScanActive)
                break;

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
            if (gViewTotalKnown && gViewTotalRows > 1u) {
                uint16_t next = gLogCursor + 1u;
                if (next >= gViewTotalRows)
                    next = 0;
                RXTX_LOG_StartCursorView(next);
                gUpdateDisplay = true;
                break;
            }
#endif

            if (gViewCacheComplete && !gViewCacheHasOlder)
                break;

            const uint16_t next = gLogCursor + 1u;
            if (next < (uint16_t)(gViewCacheStart + gViewCacheCount)) {
                gLogCursor = next;
            } else if (gViewCacheHasOlder) {
                gLogCursor = gViewCacheStart + gViewCacheCount;
                RXTX_LOG_StartCursorView(gLogCursor);
            }
        }
        gUpdateDisplay = true;
        break;

    case KEY_MENU:
        if (bKeyHeld) {
            if (bKeyPressed && !gMenuClearHandled) {
                gMenuClearHandled = true;
                RXTX_LOG_StartClear();
                gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
                gUpdateStatus = true;
                gUpdateDisplay = true;
            }
        } else if (!bKeyPressed) {
            if (!gMenuClearHandled)
                RXTX_LOG_NextFilter();
            gMenuClearHandled = false;
        }
        break;

    case KEY_EXIT:
        gRequestDisplayScreen = DISPLAY_MAIN;
        gUpdateStatus = true;
        break;

    default:
        if (!bKeyHeld)
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        break;
    }
}

static void RXTX_LOG_FormatFrequency(uint32_t frequency, char *buffer)
{
    sprintf(buffer, "%u.%05u", frequency / 100000u, frequency % 100000u);
}

static void RXTX_LOG_FormatTitle(const RXTX_LogEntry_t *entry, char *buffer)
{
    if ((entry->flags & RXTX_LOG_FLAG_NAMED) != 0) {
        strcpy(buffer, entry->name);
    } else {
        RXTX_LOG_FormatFrequency(entry->frequency, buffer);
    }
}

static void RXTX_LOG_DrawIndexBadge(uint16_t indexFromNewest, uint8_t line)
{
    char label[4];

    sprintf(label, "%03u", indexFromNewest + 1u);
    GUI_DisplaySmallestInverse(label, 2, line, false, true, 14);
}

static void RXTX_LOG_DrawSessionMarker(uint8_t line)
{
    const int16_t y = (int16_t)(line * 8u) + 3;

    UI_DrawLineBuffer(gFrameBuffer, 4, y, 123, y, true);
}

static void RXTX_LOG_ShowEmpty(bool showMessage)
{
    if (showMessage)
        UI_PrintStringSmallBold("NO LOG", 0, 127, 3);
    ST7565_BlitFullScreen();
}

void UI_DisplayRxTxLog(void)
{
    char duration[8];
    char title[16];
    RXTX_LogEntry_t entry;

    UI_DisplayClear();

    if (gClearActive) {
        RXTX_LOG_ShowEmpty(true);
        return;
    }

    RXTX_LOG_EnsureViewCache();

    if (gLogFilter == RXTX_LOG_FILTER_ALL && !gLogHasTraffic) {
        RXTX_LOG_ShowEmpty(true);
        return;
    }

    if (gViewCacheCount == 0) {
        RXTX_LOG_ShowEmpty(!gViewScanActive);
        return;
    }

    for (uint8_t row = 0; row < RXTX_LOG_VIEW_CACHE_COUNT; row++) {
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_WRAP
        if (gViewCacheCircular) {
            if (row >= gViewCacheCount)
                break;
            entry = gViewCache[row];
        } else
#endif
        {
            const uint16_t index = gLogCursor + row;
            if (!RXTX_LOG_GetFilteredEntry(index, &entry))
                break;
        }

        if (RXTX_LOG_IsSessionMarker(&entry)) {
            RXTX_LOG_DrawSessionMarker(row);
            continue;
        }

        const bool isTx = RXTX_LOG_IsTx(&entry);

        RXTX_LOG_FormatTitle(&entry, title);
        RXTX_LOG_DrawIndexBadge((uint16_t)(gNextTrafficSequence - 1u - entry.trafficSeq), row);

        if (isTx)
            UI_PrintStringSmallBold(title, 17, 0, row);
        else
            UI_PrintStringSmallNormal(title, 17, 0, row);

        GUI_DisplaySmallest(isTx ? "TX" : "RX", 96, (uint8_t)((row * 8u) + 1u), false, true);

        sprintf(duration, "%02u:%02u", entry.durationSeconds / 60u, entry.durationSeconds % 60u);
        GUI_DisplaySmallestInverse(duration, 107, row, false, true, 127);
    }

    ST7565_BlitFullScreen();
}

#endif
