#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "app/scan_skip.h"

static void capacity(void)
{
    SCAN_SKIP_Reset(0);
    gScanSkipMode = SCAN_SKIP_SESSION;
    for (unsigned i = 0; i < 32; ++i)
        SCAN_SKIP_Add(i, 0);
    for (unsigned i = 0; i < 32; ++i)
        assert(SCAN_SKIP_Contains(i));
    SCAN_SKIP_Add(32, 0);
    assert(!SCAN_SKIP_Contains(0));
    for (unsigned i = 1; i <= 32; ++i)
        assert(SCAN_SKIP_Contains(i));
    // Refreshing a duplicate does not evict a different channel.
    SCAN_SKIP_Add(1, 0);
    assert(SCAN_SKIP_Contains(2));
    SCAN_SKIP_Add(33, 0);
    assert(!SCAN_SKIP_Contains(2) && SCAN_SKIP_Contains(1));
    // Run through many full queues, including four-digit memory IDs.
    for (unsigned i = 34; i < 1024; ++i)
        SCAN_SKIP_Add(i, 0);
    for (unsigned i = 0; i < 1024; ++i)
        assert(SCAN_SKIP_Contains(i) == (i >= 992));
    assert(SCAN_SKIP_Remove(1000));
    assert(!SCAN_SKIP_Remove(1000));
    assert(SCAN_SKIP_EndSession());
    assert(!SCAN_SKIP_EndSession());
    for (unsigned i = 0; i < 1024; ++i)
        assert(!SCAN_SKIP_Contains(i));
}

static void timers(void)
{
    const uint32_t durations[] = {0, 6000, 30000, 60000, 180000};
    for (unsigned mode = SCAN_SKIP_1_MIN; mode < SCAN_SKIP_MODE_COUNT; ++mode) {
        for (unsigned fraction = 0; fraction < 50; ++fraction) {
            // Cross SysTick rollover; expire no earlier than the full duration.
            const uint32_t start = UINT32_MAX - 1000;
            SCAN_SKIP_Reset(start);
            gScanSkipMode = mode;
            SCAN_SKIP_Add(1023, start + fraction);
            assert(!SCAN_SKIP_EndSession()); // Timed entries survive stopping scan.
            assert(!SCAN_SKIP_Update(start + fraction + durations[mode] - 1));
            assert(SCAN_SKIP_Contains(1023));
            assert(SCAN_SKIP_Update(start + fraction + durations[mode] + 49));
            assert(!SCAN_SKIP_Contains(1023));
        }
    }
    SCAN_SKIP_Reset(0);
    gScanSkipMode = SCAN_SKIP_SESSION;
    SCAN_SKIP_Add(1, 0);
    gScanSkipMode = SCAN_SKIP_1_MIN;
    SCAN_SKIP_Add(2, 0);
    SCAN_SKIP_Add(3, 3000);
    SCAN_SKIP_Add(2, 3000); // Refresh expiration as well as order.
    assert(!SCAN_SKIP_Update(6000));
    assert(SCAN_SKIP_EndSession());
    assert(!SCAN_SKIP_Contains(1));
    assert(SCAN_SKIP_Contains(2) && SCAN_SKIP_Contains(3));
    assert(SCAN_SKIP_Update(9000));
    assert(!SCAN_SKIP_Contains(2) && !SCAN_SKIP_Contains(3));
    // A delayed main loop removes all expired entries in one update.
    gScanSkipMode = SCAN_SKIP_30_MIN;
    for (unsigned i = 0; i < 32; ++i)
        SCAN_SKIP_Add(i, 9000);
    assert(SCAN_SKIP_Update(1000000));
    for (unsigned i = 0; i < 32; ++i)
        assert(!SCAN_SKIP_Contains(i));
    gScanSkipMode = SCAN_SKIP_SESSION;
    SCAN_SKIP_Add(99, 1000000);
    assert(!SCAN_SKIP_Update(2000000));
    assert(SCAN_SKIP_Contains(99));
    SCAN_SKIP_Reset(2000000); // Power/config bank reload.
    assert(!SCAN_SKIP_Contains(99));
}

int main(void)
{
    for (unsigned value = 0; value < 256; ++value) {
        unsigned mode = value >= 0xA0 && value < 0xA5 ? value - 0xA0 : 0;
        assert(SCAN_SKIP_DecodeMode(value) == mode);
        assert(SCAN_SKIP_DecodeMode(SCAN_SKIP_EncodeMode(value)) ==
               (value < SCAN_SKIP_MODE_COUNT ? value : SCAN_SKIP_SESSION));
    }
    capacity();
    timers();
    puts("PASS: FIFO overflow, duplicate refresh, removal, session cleanup, all timed durations, delayed ticks, rollover, settings encoding.");
}
