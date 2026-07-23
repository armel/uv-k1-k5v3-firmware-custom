/* Copyright 2026 Kris Bennett (radio-server dock control mode)
 *
 * Portions derived from nicsure's "Quansheng Dock" firmware, app/uart.c
 * (https://github.com/nicsure/quansheng-dock-fw, Apache-2.0). See NOTICE.
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
 * dock.h - pure, host-compilable UV-K5 "dock control mode" protocol core.
 *
 * This module implements ONLY the wire surface radio-server drives:
 *   0x0850 write-registers, 0x0851 read-registers -> 0x0951 RegisterInfo,
 *   0x0870 enter / 0x0871 exit full-control.
 * No keypress-sim, screen, scan, GPIO or modulation commands (radio-server
 * does everything through BK4819 registers).
 *
 * It is deliberately free of any hardware or firmware-tree include so it can
 * be compiled and unit-tested on the host. All hardware access (BK4819 read/
 * write, UART byte-out) is reached through a caller-supplied dock_hal_t. The
 * definition of "correct" is byte-compatibility with radio-server's
 * FirmwareFakeSerial + Uvk5Decoder (radio_server/backends/uvk5/); the host
 * harness in tests/host/ mirrors that fake's rules.
 *
 * Wire framing (identical to the classic Quansheng Dock / the fake):
 *   [0xAB 0xCD][Size:u16 LE][ obf( payload[Size] + CRC[2] ) ][0xDC 0xBA]
 *   payload = [opcode:u16 LE][param_len:u16 LE][params...], Size = 4+param_len.
 *   Inbound COMMAND frames carry a real CRC-16/XMODEM over the plaintext
 *   payload (validated; mismatch dropped). Outbound REPLY frames carry a
 *   DUMMY obf(0xFF 0xFF) in the CRC slot, never a real CRC.
 *
 * Obfuscation is always on (this V3 tree hard-defines bIsEncrypted == true,
 * and radio-server's dock transport always obfuscates); the classic dock's
 * plaintext-0x0514 encryption toggle is intentionally not implemented here
 * because no frame radio-server sends to a working dock exercises it.
 */

#ifndef APP_DOCK_H
#define APP_DOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wire opcodes (little-endian on the wire). */
#define DOCK_CMD_WRITE_REGS 0x0850u
#define DOCK_CMD_READ_REGS  0x0851u
#define DOCK_CMD_ENTER_HW   0x0870u
#define DOCK_CMD_EXIT_HW    0x0871u
#define DOCK_REPLY_REG_INFO 0x0951u

/* Payload cap: matches radio_server frames.py MAX_PAYLOAD_SIZE (254). */
#define DOCK_MAX_PAYLOAD 254u
#define DOCK_RX_BUF      (DOCK_MAX_PAYLOAD + 10u)

/* Thin hardware seam. Firmware binds these to BK4819_ReadRegister /
 * BK4819_WriteRegister / UART_Send; the host harness binds fakes. */
typedef struct {
    uint16_t (*read_reg)(void *user, uint16_t reg);
    void     (*write_reg)(void *user, uint16_t reg, uint16_t value);
    void     (*send)(void *user, const uint8_t *buf, uint16_t len);
    void     *user;
} dock_hal_t;

typedef struct {
    const dock_hal_t *hal;
    bool     full_control;   /* set by 0x0870, cleared by 0x0871 */
    uint8_t  buf[DOCK_RX_BUF];
    uint16_t len;
} dock_ctx_t;

void dock_init(dock_ctx_t *ctx, const dock_hal_t *hal);

/* Dispatch ONE already-de-obfuscated, CRC-validated payload
 * ([opcode:u16][param_len:u16][params], size = 4+param_len). This is the
 * shared core the firmware calls from its top-level command handler and that
 * the deframer below calls on each accepted frame. */
void dock_dispatch(dock_ctx_t *ctx, const uint8_t *payload, uint16_t size);

/* Streaming deframer mirroring UART_IsCommandAvailable's acceptance rules
 * (sync AB CD, size bound, footer DC BA, de-obfuscate, validate command CRC,
 * drop-and-resync on any mismatch, oversize dropped not truncated). On each
 * accepted frame it calls dock_dispatch. Host-harness entry point. */
void dock_rx_byte(dock_ctx_t *ctx, uint8_t b);

/* Build and send a single 0x0951 RegisterInfo reply (obfuscated body, dummy
 * obf(0xFF 0xFF) CRC slot). Exposed for the harness. */
void dock_send_register_info(dock_ctx_t *ctx, uint16_t reg, uint16_t value);

/* Framing primitives (exposed for the harness). CRC-16/XMODEM. */
uint16_t dock_crc16(const uint8_t *data, uint16_t len);
void     dock_obfuscate(uint8_t *data, uint16_t len); /* self-inverse XOR */

#endif /* APP_DOCK_H */
