/* Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef APP_RXTX_LOG_H
#define APP_RXTX_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"
#include "functions.h"
#include "radio.h"

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG

#define RXTX_LOG_VISIBLE_COUNT 512

// Field order mirrors the leading fields of RXTX_LogFlashEntry_t
// (rxtx_log.c) so both layouts match byte-for-byte up to and including
// battVolt, copied in one pass. Scan-only fields (sequence) sit past the
// copied prefix in the flash layout and are not cached in RAM.
// The channel name is not stored in flash: it is resolved from `channel` when
// exporting the K5Viewer packet.
typedef struct {
    uint32_t frequency;
    uint32_t trafficSeq;
    uint16_t durationSeconds;
    uint16_t channel;
    uint8_t  flags;
    uint8_t  sMeter;
    uint8_t  battVolt;
} RXTX_LogEntry_t;

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
#define RXTX_LOG_K5VIEWER_VERSION 2u
#define RXTX_LOG_K5VIEWER_ROW_COUNT 64u
#define RXTX_LOG_K5VIEWER_HISTORY_ROW_COUNT 64u
#define RXTX_LOG_K5VIEWER_HISTORY_START 0xFFFFFFFFu
#define RXTX_LOG_K5VIEWER_NAME_LENGTH 10u
#define RXTX_LOG_K5VIEWER_STATUS_ACTIVE      (1u << 0)
#define RXTX_LOG_K5VIEWER_STATUS_HAS_TRAFFIC (1u << 1)
#define RXTX_LOG_K5VIEWER_STATUS_CLEARING    (1u << 2)
#define RXTX_LOG_K5VIEWER_STATUS_DISABLED    (1u << 3)
#define RXTX_LOG_K5VIEWER_STATUS_PACKET_SIZE 4u

typedef struct __attribute__((packed)) {
    uint32_t frequency;
    uint32_t trafficSeq;
    uint16_t durationSeconds;
    uint16_t channel;
    uint8_t  flags;
    uint8_t  meter;
    uint8_t  battVolt;
    char     channelName[RXTX_LOG_K5VIEWER_NAME_LENGTH];
} RXTX_LogK5ViewerRow_t;

// On-wire packet layout: a disabled log sends only the 4-byte status header.
// An enabled log continues with one live row and
// RXTX_LOG_K5VIEWER_ROW_COUNT row slots, zero-padded past the last valid row.
// The packet is streamed row by row and never built whole in RAM.
#define RXTX_LOG_K5VIEWER_PACKET_SIZE (RXTX_LOG_K5VIEWER_STATUS_PACKET_SIZE + ((RXTX_LOG_K5VIEWER_ROW_COUNT + 1u) * sizeof(RXTX_LogK5ViewerRow_t)))
#define RXTX_LOG_K5VIEWER_HISTORY_PACKET_SIZE (RXTX_LOG_K5VIEWER_HISTORY_ROW_COUNT * sizeof(RXTX_LogK5ViewerRow_t))

uint32_t RXTX_LOG_K5ViewerSignature(void);
void RXTX_LOG_SendK5ViewerPacket(void (*send)(const uint8_t *data, uint16_t size));
// Sends the page of history rows below `beforeSeq` (HISTORY_START begins
// a dump below the live packet's coverage) and returns the bound for the
// next page, 0 once the visible history is exhausted.
uint32_t RXTX_LOG_SendK5ViewerHistoryPage(uint32_t beforeSeq, void (*send)(const uint8_t *data, uint16_t size));
#endif

void RXTX_LOG_Init(void);
bool RXTX_LOG_IsEnabled(void);
void RXTX_LOG_BeginRx(const VFO_Info_t *vfo, FUNCTION_Type_t function);
void RXTX_LOG_BeginTx(const VFO_Info_t *vfo);
void RXTX_LOG_EndActive(void);
void RXTX_LOG_Task10ms(void);
void RXTX_LOG_Tick500ms(void);
const char *RXTX_LOG_GetFilterName(void);

void ACTION_RxTxLog(void);
void RXTX_LOG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void UI_DisplayRxTxLog(void);

#endif

#endif
