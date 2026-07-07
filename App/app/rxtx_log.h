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
// The channel name is not stored: it is resolved from `channel` at display
// time via SETTINGS_FetchChannelName.
typedef struct {
    uint32_t frequency;
    uint32_t trafficSeq;
    uint16_t durationSeconds;
    uint16_t channel;
    uint8_t  flags;
    uint8_t  sMeter;
    uint8_t  battVolt;
} RXTX_LogEntry_t;

void RXTX_LOG_Init(void);
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
