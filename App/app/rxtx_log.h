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
#define RXTX_LOG_NAME_LEN      10

typedef enum {
    RXTX_LOG_DIR_RX = 0,
    RXTX_LOG_DIR_TX = 1,
} RXTX_LogDirection_t;

// Field order mirrors RXTX_LogFlashEntry_t (rxtx_log.c) so both layouts
// match byte-for-byte up to the end of name[] and can be copied in one pass.
typedef struct {
    uint32_t sequence;
    uint32_t frequency;
    uint32_t trafficSeq;
    uint16_t durationSeconds;
    uint16_t channel;
    uint8_t  flags;
    char     name[RXTX_LOG_NAME_LEN + 1];
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
