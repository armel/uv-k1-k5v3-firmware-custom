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

static void Screenshot_Send(const uint8_t *buf, uint16_t len)
{
    if (gUSB_ScreenshotEnabled) {
        cdc_acm_data_send_with_dtr(buf, len);
    } else {
        UART_Send(buf, len);
    }
}

// SRAM optimization: minimize static allocations
// - previousFrame: 1024 bytes (REQUIRED - need to compare for delta)
// - No currentFrame or deltaFrame static buffers
static uint8_t previousFrame[1024] = {0};
static uint8_t forcedBlock = 0;
static uint8_t keepAlive = 10;

void getScreenShot(bool force)
{
    // Build frame in a temporary stack buffer
    // This is 1024 bytes but it's temporary and gets freed after the function
    uint8_t frameBuffer[1024];
    uint16_t index = 0;
    uint8_t acc = 0;
    uint8_t bitCount = 0;

    if (gUART_LockScreenshot > 0) {
        gUART_LockScreenshot--;
        return;
    }

    if (UART_IsCableConnected()) {
        keepAlive = 10;
        gUSB_ScreenshotEnabled = false;
    }

    if (VCP_ScreenshotPing()) {
        keepAlive = 10;
        gUSB_ScreenshotEnabled = true;
    }

    if (keepAlive > 0) {
        if (--keepAlive == 0) return;
    } else {
        return;
    }

    // ==== BUILD FRAME ONCE ====
    // Status line: 8 bit layers × 128 columns
    for (uint8_t b = 0; b < 8; b++) {
        for (uint8_t i = 0; i < 128; i++) {
            uint8_t bit = (gStatusLine[i] >> b) & 0x01;
            acc |= (bit << bitCount++);
            if (bitCount == 8) {
                frameBuffer[index++] = acc;
                acc = 0;
                bitCount = 0;
            }
        }
    }

    // Frame buffer: 7 lines × 8 bit layers × 128 columns
    for (uint8_t l = 0; l < 7; l++) {
        for (uint8_t b = 0; b < 8; b++) {
            for (uint8_t i = 0; i < 128; i++) {
                uint8_t bit = (gFrameBuffer[l][i] >> b) & 0x01;
                acc |= (bit << bitCount++);
                if (bitCount == 8) {
                    frameBuffer[index++] = acc;
                    acc = 0;
                    bitCount = 0;
                }
            }
        }
    }

    if (bitCount > 0)
        frameBuffer[index++] = acc;

    if (index != 1024)
        return;

    // ==== FIRST PASS: Count changed chunks ====
    uint16_t deltaLen = 0;
    uint8_t changedChunks[128];  // List of changed chunk indices
    uint8_t changedCount = 0;

    for (uint8_t chunk = 0; chunk < 128; chunk++) {
        uint8_t *cur = &frameBuffer[chunk * 8];
        uint8_t *prev = &previousFrame[chunk * 8];

        bool changed = memcmp(cur, prev, 8) != 0;
        bool isForced = (chunk == forcedBlock);
        bool fullUpdate = force;

        if (changed || isForced || fullUpdate) {
            changedChunks[changedCount++] = chunk;
            deltaLen += 9;
        }
    }

    forcedBlock = (forcedBlock + 1) % 128;

    if (deltaLen == 0)
        return;

    // Skip transmission if a key is currently pressed
    // UART_Send is blocking - would freeze the main loop and lose keypresses
    if (gKeyReading0 != KEY_INVALID)
        return;

    // ==== Send version marker (for backward compatibility detection) ====
    // New format: sends 0xFF before header
    // Old format: doesn't exist, so viewers can differentiate
    uint8_t versionMarker = 0xFF;
    Screenshot_Send(&versionMarker, 1);

    // ==== Send header ====
    uint8_t header[5] = {
        0xAA, 0x55, 0x02,
        (uint8_t)(deltaLen >> 8),
        (uint8_t)(deltaLen & 0xFF)
    };

    Screenshot_Send(header, 5);

    // ==== SECOND PASS: Send only changed chunks ====
    uint8_t chunk[9];
    
    for (uint8_t i = 0; i < changedCount; i++) {
        uint8_t chunkIdx = changedChunks[i];
        uint8_t *cur = &frameBuffer[chunkIdx * 8];
        uint8_t *prev = &previousFrame[chunkIdx * 8];

        chunk[0] = chunkIdx;
        memcpy(&chunk[1], cur, 8);
        
        Screenshot_Send(chunk, 9);
        
        // Update previousFrame for next comparison
        memcpy(prev, cur, 8);
    }

    uint8_t end = 0x0A;
    Screenshot_Send(&end, 1);
}