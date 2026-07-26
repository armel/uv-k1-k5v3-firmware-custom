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
#define DOCK_CMD_SET_VFO    0x0873u
#define DOCK_REPLY_REG_INFO 0x0951u
#define DOCK_REPLY_SET_VFO  0x0874u

/* Payload cap: matches radio_server frames.py MAX_PAYLOAD_SIZE (254). */
#define DOCK_MAX_PAYLOAD 254u
#define DOCK_RX_BUF      (DOCK_MAX_PAYLOAD + 10u)

/* ---- 0x0873 set-VFO, and its 0x0874 reply --------------------------------
 *
 * Everything else in this protocol writes BK4819 registers, and none of it
 * survives the handoff back to the radio: 0x0870 backs the registers up and
 * 0x0871 ends in RADIO_SetupRegisters(true), which retunes the synthesiser
 * from the radio's OWN VFO (app/uart.c Dock_EnterFullControl -> radio.c
 * "BK4819_SetFrequency(gRxVfo->pRX->Frequency)"). So a host that tunes by
 * register can never hand the radio a channel and walk away.
 *
 * 0x0873 sets the firmware's VFO instead, and then lets the firmware's own
 * RADIO_ApplyOffset / RADIO_ConfigureSquelchAndOutputPower / RADIO_SetupRegisters
 * do the work. After it, the radio is genuinely on the channel — screen, split,
 * CTCSS, and the per-band PA calibration the host cannot read out of flash —
 * exactly as if a thumb had dialled it. It is deliberately NOT part of the
 * full-control loop: a one-shot command, so the firmware main loop is never
 * starved (that starvation is what makes the radio ignore its own PTT pin).
 *
 * WHY IT REPLIES (0x0874), when the first draft of it did not.
 *
 * This command had five ways to do nothing at all and no way to say so: a short
 * frame, full-control held, a bad direction, a bad bandwidth/power, and a tone
 * the radio's table does not contain (which fell through to "no tone" — the
 * repeater then simply never opens). The host saw a successful write either
 * way. That is the exact fault class this fork keeps paying for: radio-server
 * ADR 0140/0141 spent four cycles on an intermittency because "no measurement"
 * and "no signal" were indistinguishable, and the tune here is *more* dangerous
 * than a register write, because a register write is undone by the next 0x0871
 * while a wrong VFO is what the radio transmits on after everyone walks away.
 *
 * So every 0x0873 now answers with a status AND the frequencies the radio
 * actually ended up on, read back out of its own VFO struct after
 * RADIO_ApplyOffset. Not the values the host asked for — the ones it got.
 *
 * UNITS: the wire carries plain Hz, both ways. The firmware's own VFO stores
 * frequency in units of 10 Hz (App/frequencies.c frequencyBandTable has 400 MHz
 * as 40000000), so app/uart.c converts on the way in and back on the way out.
 * Getting this wrong is not a small error and it is not a loud one:
 * FREQUENCY_GetBand() CLAMPS instead of failing, so 448525000 taken as 10 Hz
 * units resolves to BAND7_470MHz and programmes the synthesiser for 4.485 GHz
 * with band-7 PA calibration — no error anywhere, and no RF where anyone is
 * listening.
 */
#define DOCK_SET_VFO_PARAM_LEN 13u

/* 0x0874 status byte. 0 is the only success; every other value names a distinct
 * reason the radio is NOT on the channel that was asked for. */
#define DOCK_VFO_APPLIED        0u  /* on the channel, exactly as requested   */
#define DOCK_VFO_ERR_SHORT      1u  /* payload shorter than the parameter set */
#define DOCK_VFO_ERR_BUSY       2u  /* host holds full-control (0x0870)       */
#define DOCK_VFO_ERR_DIRECTION  3u  /* offset direction not NONE/ADD/SUB      */
#define DOCK_VFO_ERR_FIELD      4u  /* bandwidth or power off its scale       */
#define DOCK_VFO_ERR_NO_HAL     5u  /* built without the radio-side binding   */
#define DOCK_VFO_ERR_BAND       6u  /* rx or tx leg outside every band        */
#define DOCK_VFO_ERR_TONE       7u  /* tone absent from CTCSS_Options; VFO is
                                     * on frequency but would key WITHOUT the
                                     * tone, so the repeater stays shut       */

/* What the radio ended up on, read back after the firmware's own RADIO_ApplyOffset.
 * Frequencies in Hz. On any rejection these are zero. */
typedef struct {
    uint32_t rx_hz;
    uint32_t tx_hz;         /* the leg that actually radiates */
    uint16_t ctcss_tenths;  /* as applied; 0 = transmitting no tone */
    uint8_t  status;        /* DOCK_VFO_* */
} dock_vfo_applied_t;

/* Offset direction, matching the firmware's TX_OFFSET_FREQUENCY_DIRECTION. */
#define DOCK_OFFSET_NONE 0u
#define DOCK_OFFSET_ADD  1u
#define DOCK_OFFSET_SUB  2u

/* One repeater channel, decoded from the wire.
 *
 * `ctcss_tenths` is tenths of a Hz (1000 = 100.0 Hz), 0 for none, so the wire
 * carries the tone itself rather than an index into a table both sides would
 * have to agree on for ever — the firmware resolves it against its own
 * CTCSS_Options, which is the only table that can be wrong in a way that
 * matters. */
typedef struct {
    uint32_t rx_hz;         /* Hz, as sent — NOT the firmware's 10 Hz units */
    uint32_t offset_hz;     /* Hz */
    uint16_t ctcss_tenths;
    uint8_t  direction;     /* DOCK_OFFSET_* */
    uint8_t  narrow;        /* 0 = wide FM, 1 = narrow */
    uint8_t  power;         /* 0 low, 1 mid, 2 high */
} dock_vfo_t;

/* Thin hardware seam. Firmware binds these to BK4819_ReadRegister /
 * BK4819_WriteRegister / UART_Send; the host harness binds fakes. */
typedef struct {
    uint16_t (*read_reg)(void *user, uint16_t reg);
    void     (*write_reg)(void *user, uint16_t reg, uint16_t value);
    void     (*send)(void *user, const uint8_t *buf, uint16_t len);
    void     *user;
    /* Optional (may be NULL). Called on a REG_30 TX-enable *edge* — before the
     * register write completes — so the firmware can engage/disengage the
     * external PA chain (REG_33 PA-enable GPIO + REG_36 bias) that a bare
     * REG_30 write leaves dark (F5 / radio-server Chain B). on=true on key,
     * on=false on un-key and at the fail-safe seams (enter/exit/overflow). */
    void     (*tx_set)(void *user, bool on);
    /* Optional (may be NULL — then 0x0873 answers DOCK_VFO_ERR_NO_HAL). Called
     * on a validated 0x0873 with a decoded channel. The firmware binds this to
     * its own RADIO_* chain; the host harness binds a spy. Never called while
     * full_control is set — applying a VFO mid-dock would reprogram the chip
     * under the host's feet, which is the "adopt whatever state you find" fault
     * class ADR 0132 removed.
     *
     * MUST fill `out`: `status` DOCK_VFO_APPLIED plus the frequencies the radio
     * is now on, or a DOCK_VFO_ERR_* it alone can detect (band, tone) with the
     * frequencies left zero. It reports what happened; it does not re-validate
     * what dock.c already checked. */
    void     (*set_vfo)(void *user, const dock_vfo_t *vfo, dock_vfo_applied_t *out);
} dock_hal_t;

typedef struct {
    const dock_hal_t *hal;
    bool     full_control;   /* set by 0x0870, cleared by 0x0871 */
    bool     tx_on;          /* cached REG_30 TX-enable state, for edge-detect */
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

/* Build and send the 0x0874 set-VFO reply. Exposed for the harness. */
void dock_send_set_vfo_reply(dock_ctx_t *ctx, const dock_vfo_applied_t *r);

/* Framing primitives (exposed for the harness). CRC-16/XMODEM. */
uint16_t dock_crc16(const uint8_t *data, uint16_t len);
void     dock_obfuscate(uint8_t *data, uint16_t len); /* self-inverse XOR */

#endif /* APP_DOCK_H */
