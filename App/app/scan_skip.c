// SPDX-License-Identifier: Apache-2.0
#include "scan_skip.h"
#include <string.h>

#ifdef ENABLE_FEAT_F4HWN_SCAN_SKIP
typedef struct {
    uint16_t channel;
    uint16_t remaining; // Half-second ticks; UINT16_MAX means this scan session.
} ScanSkip;

static ScanSkip entries[SCAN_SKIP_CAPACITY];
static uint8_t count;
static uint32_t lastTick;
uint8_t gScanSkipMode;

uint8_t SCAN_SKIP_DecodeMode(uint8_t stored)
{
    return stored >= 0xA0 && stored < 0xA0 + SCAN_SKIP_MODE_COUNT
        ? stored - 0xA0 : SCAN_SKIP_SESSION;
}

uint8_t SCAN_SKIP_EncodeMode(uint8_t mode)
{
    return 0xA0 + (mode < SCAN_SKIP_MODE_COUNT ? mode : SCAN_SKIP_SESSION);
}

static void RemoveAt(uint8_t index)
{
    --count;
    memmove(&entries[index], &entries[index + 1],
            (count - index) * sizeof(entries[0]));
}

void SCAN_SKIP_Reset(uint32_t now10ms)
{
    count = 0;
    lastTick = now10ms;
}

bool SCAN_SKIP_Contains(uint16_t channel)
{
    for (uint8_t i = 0; i < count; ++i)
        if (entries[i].channel == channel)
            return true;
    return false;
}

bool SCAN_SKIP_Remove(uint16_t channel)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].channel == channel) {
            RemoveAt(i);
            return true;
        }
    }
    return false;
}

bool SCAN_SKIP_Update(uint32_t now10ms)
{
    // Use elapsed SysTicks so menus/overlay apps do not pause expiration.
    const uint32_t elapsed = (uint32_t)(now10ms - lastTick) / 50u;
    lastTick += elapsed * 50u;
    bool changed = false;
    for (uint8_t i = 0; i < count;) {
        if (entries[i].remaining == UINT16_MAX) {
            ++i;
        } else if (elapsed >= entries[i].remaining) {
            RemoveAt(i);
            changed = true;
        } else {
            entries[i++].remaining -= elapsed;
        }
    }
    return changed;
}

void SCAN_SKIP_Add(uint16_t channel, uint32_t now10ms)
{
    static const uint16_t duration[] = {UINT16_MAX, 120, 600, 1200, 3600};
    SCAN_SKIP_Update(now10ms);
    // Re-skipping an existing channel refreshes its timer and FIFO position.
    SCAN_SKIP_Remove(channel);
    if (count == SCAN_SKIP_CAPACITY)
        RemoveAt(0);
    uint16_t remaining = duration[gScanSkipMode < SCAN_SKIP_MODE_COUNT
                                  ? gScanSkipMode : SCAN_SKIP_SESSION];
    // Round up so a new timed skip never expires before its full duration.
    if (remaining != UINT16_MAX && now10ms != lastTick)
        ++remaining;
    entries[count++] = (ScanSkip){channel, remaining};
}

bool SCAN_SKIP_EndSession(void)
{
    bool changed = false;
    for (uint8_t i = 0; i < count;) {
        if (entries[i].remaining == UINT16_MAX) {
            RemoveAt(i);
            changed = true;
        } else {
            ++i;
        }
    }
    return changed;
}
#endif
