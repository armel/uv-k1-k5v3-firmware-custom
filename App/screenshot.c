/* Copyright 2024 Armel F4HWN
 * https://github.com/armel
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

#include "debugging.h"
#include "driver/st7565.h"
#include "screenshot.h"
#include "misc.h"
#include "driver/vcp.h"
#include "driver/keyboard.h"
#include "driver/bk4819.h"
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
#include "app/rxtx_log.h"
#endif

// SRAM optimization: minimize static allocations
// - previousHash: one fingerprint per 8-byte chunk instead of a full
//   1024-byte copy of the previous frame (chunks are only compared,
//   never retransmitted from history). A 16-bit fingerprint keeps the
//   collision odds at ~1/65536 per chunk, so a stale chunk slipping
//   through is rare; the forcedBlock rotation repairs that residual
//   within at most 128 frames.
// Stack optimization: chunks are computed on demand straight from
// gStatusLine/gFrameBuffer instead of building the whole 1024-byte
// frame on the stack. Changed chunks are tracked in a 16-byte bitmap.
// Peak stack drops from ~1.3 KB to ~40 bytes, at the cost of
// transforming each transmitted chunk twice (negligible next to the
// blocking UART transfer).
static uint16_t previousHash[128];
static uint8_t previousStateFlags = 0xFF;
static uint8_t forcedBlock = 0;
static uint8_t keepAlive = 3;
static bool hasConnectionPing = false;
static bool wasConnected = false;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
static uint32_t previousRfLogSignature = 0;
static bool rfLogSent = false;
#endif

// FNV-1a over one 8-byte chunk, folded to 16 bits
static uint16_t SCREENSHOT_Hash(const uint8_t *data)
{
    uint32_t h = 2166136261u;
    for (uint8_t i = 0; i < 8; i++) {
        h = (h ^ data[i]) * 16777619u;
    }
    return (uint16_t)(h ^ (h >> 16));
}

void SCREENSHOT_ParseInput(void)
{
    if (SCREENSHOT_IsLocked())
        return;

    if (UART_IsCableConnected()) {
        keepAlive = 15;
        hasConnectionPing = true;
        gUSB_ScreenshotEnabled = false;
    }
    else if (VCP_ScreenshotPing()) {
        keepAlive = 15;
        hasConnectionPing = true;
        gUSB_ScreenshotEnabled = true;
    }
}

static void SCREENSHOT_Send(const uint8_t *buf, uint16_t len)
{
    if (gUSB_ScreenshotEnabled) {
        cdc_acm_data_send_with_dtr(buf, len);
    } else {
        UART_Send(buf, len);
    }
}

enum {
    SCREENSHOT_CHUNK_SIZE = 8,
    SCREENSHOT_CHUNKS_PER_LINE = 16,
    SCREENSHOT_HALF_LINE_COLUMNS = LCD_WIDTH / 2,
    SCREENSHOT_MARKER_BASE = 0xF0,
    SCREENSHOT_TYPE_DIFF = 0x02,
    SCREENSHOT_TYPE_RXTX_LOG = 0x05,
    SCREENSHOT_FLAG_DEEP_SLEEP = 1 << 0,
    SCREENSHOT_FLAG_LED_RED = 1 << 1,
    SCREENSHOT_FLAG_LED_GREEN = 1 << 2,
};

static uint8_t SCREENSHOT_StateFlags(void)
{
    uint8_t flags = 0;

#ifdef ENABLE_FEAT_F4HWN_SLEEP
    if (gWakeUp)
        flags |= SCREENSHOT_FLAG_DEEP_SLEEP;
#endif

    if (BK4819_IsGpioOutSet(BK4819_GPIO5_PIN1_RED))
        flags |= SCREENSHOT_FLAG_LED_RED;

    if (BK4819_IsGpioOutSet(BK4819_GPIO6_PIN2_GREEN))
        flags |= SCREENSHOT_FLAG_LED_GREEN;

    return flags;
}

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
static bool SCREENSHOT_HasPendingRfLogUpdate(void)
{
    if ((gSerialViewerFeatures & SERIAL_VIEWER_FEATURE_RF_LOG) == 0)
        return false;

    return !rfLogSent || RXTX_LOG_K5ViewerSignature() != previousRfLogSignature;
}

static void SCREENSHOT_SendRfLog(void)
{
    // Capture the signature before streaming: a state change landing
    // during the blocking send still differs afterwards and triggers a
    // resend on the next cycle.
    previousRfLogSignature = RXTX_LOG_K5ViewerSignature();
    rfLogSent = true;

    const uint16_t len = RXTX_LOG_K5VIEWER_PACKET_SIZE;
    uint8_t header[5] = {
        0xAA, 0x55, SCREENSHOT_TYPE_RXTX_LOG,
        (uint8_t)(len >> 8),
        (uint8_t)(len & 0xFF)
    };

    SCREENSHOT_Send(header, 5);
    RXTX_LOG_SendK5ViewerPacket(SCREENSHOT_Send);

    uint8_t end = 0x0A;
    SCREENSHOT_Send(&end, 1);
}
#endif

bool SCREENSHOT_HasPendingStateChange(void)
{
    if (gUART_LockScreenshot > 0 || keepAlive == 0 || !hasConnectionPing)
        return false;

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
    if (SCREENSHOT_HasPendingRfLogUpdate())
        return true;
#endif

    return !wasConnected || SCREENSHOT_StateFlags() != previousStateFlags;
}

// Compute one 8-byte output chunk directly from the display buffers.
// Frame layout: per line (status + 7 frame lines), 8 bit layers of
// 16 bytes each. Each bit layer is split into two 64-column chunks.
static void SCREENSHOT_Chunk(uint8_t chunkIdx, uint8_t *dest)
{
    const uint8_t chunkInLine = chunkIdx % SCREENSHOT_CHUNKS_PER_LINE;
    const uint8_t line = chunkIdx / SCREENSHOT_CHUNKS_PER_LINE;
    const uint8_t bit = chunkInLine / 2;
    const uint8_t columnBase = (chunkInLine % 2) * SCREENSHOT_HALF_LINE_COLUMNS;
    const uint8_t *src = (line == 0 ? gStatusLine : gFrameBuffer[line - 1])
                         + columnBase;

    for (uint8_t j = 0; j < SCREENSHOT_CHUNK_SIZE; j++) {
        uint8_t acc = 0;
        for (uint8_t k = 0; k < SCREENSHOT_CHUNK_SIZE; k++) {
            if (src[j * SCREENSHOT_CHUNK_SIZE + k] & (1 << bit)) acc |= (1 << k);
        }
        dest[j] = gSetting_set_inv ? ~acc : acc;
    }
}

void SCREENSHOT_Update(bool force)
{
    if (SCREENSHOT_IsLocked())
        return;

    if (keepAlive > 0) {
        if (--keepAlive == 0) {
            // Connection just lost → reset state for next reconnection
            wasConnected = false;
            hasConnectionPing = false;
            previousStateFlags = 0xFF;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
            gSerialViewerFeatures = 0;
            rfLogSent = false;
#endif
            return;
        }
    } else {
        return;
    }

    // Connection is alive — detect reconnection and force full frame
    if (!wasConnected) {
        force = true;
    }

    const uint8_t stateFlags = SCREENSHOT_StateFlags();
    const bool stateChanged = (stateFlags != previousStateFlags);
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
    const bool rfLogPending = SCREENSHOT_HasPendingRfLogUpdate();
#else
    const bool rfLogPending = false;
#endif

    // ==== FIRST PASS: Count changed chunks ====
    uint16_t deltaLen = 0;
    uint8_t changedBitmap[16] = {0};  // 1 bit per chunk
    uint8_t chunk[9];                 // [0] = index, [1..8] = payload

    for (uint8_t chunkIdx = 0; chunkIdx < 128; chunkIdx++) {
        SCREENSHOT_Chunk(chunkIdx, &chunk[1]);

        bool changed = SCREENSHOT_Hash(&chunk[1]) != previousHash[chunkIdx];
        bool isForced = (chunkIdx == forcedBlock);

        if (changed || isForced || force) {
            changedBitmap[chunkIdx >> 3] |= 1 << (chunkIdx & 7);
            deltaLen += 9;
        }
    }

    forcedBlock = (forcedBlock + 1) % 128;

    if (deltaLen == 0 && !stateChanged && !rfLogPending)
        return;

    // Skip transmission if a key is currently pressed
    // UART_Send is blocking - would freeze the main loop and lose keypresses
    if (gKeyReading0 != KEY_INVALID)
        return;

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
    // A pending RF log packet may be the only thing to send: skip the
    // frame section when it carries nothing. Without the RF log stream
    // the early return above already guarantees this condition.
    if (deltaLen != 0 || stateChanged)
#endif
    {
        // ==== Send version marker and state flags ====
        // 0xF0 keeps a resync-safe marker before the standard AA 55 header.
        uint8_t versionMarker = SCREENSHOT_MARKER_BASE | stateFlags;
        SCREENSHOT_Send(&versionMarker, 1);

        // ==== Send header ====
        uint8_t header[5] = {
            0xAA, 0x55, SCREENSHOT_TYPE_DIFF,
            (uint8_t)(deltaLen >> 8),
            (uint8_t)(deltaLen & 0xFF)
        };

        SCREENSHOT_Send(header, 5);

        // ==== SECOND PASS: Send only changed chunks ====
        for (uint8_t chunkIdx = 0; chunkIdx < 128; chunkIdx++) {
            if (!(changedBitmap[chunkIdx >> 3] & (1 << (chunkIdx & 7))))
                continue;

            chunk[0] = chunkIdx;
            SCREENSHOT_Chunk(chunkIdx, &chunk[1]);

            SCREENSHOT_Send(chunk, 9);

            // Update the fingerprint only once the chunk is actually sent,
            // so chunks skipped by an early return stay marked as changed.
            // Hashing the recomputed payload also keeps the fingerprint in
            // sync if the display buffer changed between the two passes.
            previousHash[chunkIdx] = SCREENSHOT_Hash(&chunk[1]);
        }

        uint8_t end = 0x0A;
        SCREENSHOT_Send(&end, 1);
    }

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG_K5VIEWER
    if (rfLogPending)
        SCREENSHOT_SendRfLog();
#endif

    previousStateFlags = stateFlags;
    wasConnected = true;
}
