// SPDX-License-Identifier: Apache-2.0
#ifndef APP_SCAN_SKIP_H
#define APP_SCAN_SKIP_H

#include <stdbool.h>
#include <stdint.h>

#define SCAN_SKIP_CAPACITY 32u
enum {
    SCAN_SKIP_SESSION,
    SCAN_SKIP_1_MIN,
    SCAN_SKIP_5_MIN,
    SCAN_SKIP_10_MIN,
    SCAN_SKIP_30_MIN,
    SCAN_SKIP_MODE_COUNT
};

extern uint8_t gScanSkipMode;

// Entries never leave RAM. Only the tagged preference byte is persisted.
uint8_t SCAN_SKIP_DecodeMode(uint8_t stored);
uint8_t SCAN_SKIP_EncodeMode(uint8_t mode);
void SCAN_SKIP_Reset(uint32_t now10ms);
bool SCAN_SKIP_Update(uint32_t now10ms);
bool SCAN_SKIP_Contains(uint16_t channel);
bool SCAN_SKIP_Remove(uint16_t channel);
void SCAN_SKIP_Add(uint16_t channel, uint32_t now10ms);
bool SCAN_SKIP_EndSession(void);

#endif
