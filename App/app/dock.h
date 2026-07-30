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
 *   0x0870 enter / 0x0871 exit full-control,
 *   0x0873 set-VFO -> 0x0874 (F6), 0x0877 set-modulation -> 0x0878 (F7).
 * No keypress-sim, screen, scan or GPIO commands. The register commands are how
 * radio-server drives the chip; 0x0873 and 0x0877 exist because register writes
 * do not survive the 0x0871 handoff (see the long note below).
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
#define DOCK_CMD_WRITE_REGS     0x0850u
#define DOCK_CMD_READ_REGS      0x0851u
#define DOCK_CMD_ENTER_HW       0x0870u
#define DOCK_CMD_EXIT_HW        0x0871u
#define DOCK_CMD_SET_VFO        0x0873u
/* 0x0877, NOT 0x0875. radio-server ADR 0111 records the classic Quansheng Dock's
 * full-control extended set as "0x0872 modulation, 0x0873/4 backlight, 0x0875/6 AM
 * emulation", so 0x0875/6 is claimed. That census cannot be re-verified from this
 * tree (nicsure's source is not vendored here) and is therefore treated as claimed
 * rather than assumed free — which is exactly the check 0x0873's own allocation
 * skipped: ADR 0140 reasoned only about 0x0872 and took a pair the same census had
 * already spoken for. That one is shipped and cannot be walked back. This one is
 * cheap to place correctly, so it is placed correctly. */
#define DOCK_CMD_SET_MODULATION 0x0877u
#define DOCK_REPLY_REG_INFO     0x0951u
#define DOCK_REPLY_SET_VFO      0x0874u
#define DOCK_REPLY_SET_MOD      0x0878u

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
 * Frequencies in Hz. On any rejection these are zero.
 *
 * `power` is the radio's OWN OUTPUT_POWER_* value, not the 0/1/2 that was sent.
 * It is reported because the two scales are not the same and quietly disagreed:
 * the wire's "high" (2) landed on OUTPUT_POWER_LOW2 in an enum that runs
 * USER, LOW1..LOW5, MID, HIGH. A repeater simply does not open at that level,
 * and nothing about it is visible from the host. */
typedef struct {
    uint32_t rx_hz;
    uint32_t tx_hz;         /* the leg that actually radiates */
    uint16_t ctcss_tenths;  /* as applied; 0 = transmitting no tone */
    uint8_t  status;        /* DOCK_VFO_* */
    uint8_t  power;         /* the firmware's OUTPUT_POWER_*, as applied */
} dock_vfo_applied_t;

/* Offset direction, matching the firmware's TX_OFFSET_FREQUENCY_DIRECTION. */
#define DOCK_OFFSET_NONE 0u
#define DOCK_OFFSET_ADD  1u
#define DOCK_OFFSET_SUB  2u

/* ---- 0x0877 set-modulation, and its 0x0878 reply -------------------------
 *
 * WHY A NEW OPCODE AND NOT A 14TH BYTE ON 0x0873.
 *
 * A field appended to 0x0873 changes the bytes of a frame radio-server already
 * sends, and it breaks in BOTH directions, silently. Old host + new firmware: the
 * 13-byte frame is refused DOCK_VFO_ERR_SHORT, which the host reads as a tuning
 * failure rather than as a version mismatch. New host + old firmware: the 14-byte
 * frame decodes fine, the 14th byte is ignored, and the radio tunes on a
 * modulation nobody set. A new opcode is additive instead — an old host never
 * sends it, and a new host sending it to a pre-F7 firmware falls through
 * dock_dispatch's `default:` to no reply at all, which is the same silence the
 * F-level probe already reads.
 *
 * THE WIRE HAS ITS OWN VALUES, ON PURPOSE.
 *
 * The firmware's ModulationMode_t (radio.h) is { FM, AM, USB, [BYP, RAW,]
 * UKNOWN } — the bracketed pair exists only under ENABLE_BYP_RAW_DEMODULATORS, so
 * the enum's numeric END MOVES WITH A BUILD FLAG. A wire bound derived from it
 * would mean different things in two builds of the same protocol, and this core
 * may not include a firmware header anyway. So the wire names its own values and
 * app/uart.c maps them explicitly, in both directions. That is the DOCK_POWER_MAP
 * lesson (see below) applied before it bites rather than after.
 *
 * THE MODULATION IS STICKY FOR THE SESSION, AND THAT IS THE POINT.
 *
 * 0x0873 has to write SOME modulation into the VFO it applies. It used to write
 * MODULATION_FM literally, so "set AM, then tune" silently landed the radio back
 * on FM. Sending the two as separate frames is not merely inconvenient, it is
 * unreliable: ADR 0131 established that this link DROPS frames — the firmware is
 * single-threaded and anything arriving while it is busy is discarded, not queued.
 * Tune lands, set-modulation is dropped, and the radio sits on the right channel
 * in the wrong demodulator with nothing on the wire having said so.
 *
 * So dock_ctx_t carries the modulation and 0x0873 applies it. After one successful
 * 0x0877 every later tune keeps it, and a dropped frame is a retryable 0x0877
 * instead of a silently mis-configured radio. It is SEEDED FROM A CONSTANT and
 * never from a read of the radio: seeding it from the radio's current state is the
 * "adopt whatever you find" fault ADR 0132 removed, and it would hand a repeater
 * channel whatever demodulator the last person left on the front panel. Seeded FM,
 * so a host that never sends 0x0877 gets byte-identical F6 behaviour.
 *
 * It is SESSION state, not radio state: Dock_EnsureInit runs once per radio power
 * cycle, so it outlives a host process restart. A reconnecting host must ASSERT the
 * modulation it wants rather than assume FM. That is a requirement on the host, not
 * a note.
 *
 * NON-FM STOPS THE RADIO'S OWN TRANSMIT PATH.
 *
 * Built without ENABLE_TX_WHEN_AM (which this tree is not), RADIO_PrepareTX sets
 * VFO_STATE_TX_DISABLE for any modulation that is not FM (radio.c). That path is
 * how the radio's PTT PIN keys — and radio-server's baofeng backend keys exactly
 * there, by asserting the AIOC's DTR line into that pin. So on that station a
 * successful set-AM stops the transmitter outright. The dock's own REG_30 keying
 * (0x0850) does not go through RADIO_PrepareTX and is NOT blocked, so the same
 * firmware state means "cannot transmit" on one backend and "transmits normally" on
 * another. A host cannot see a build flag, so DOCK_MOD_FLAG_TX_OK reports it.
 * The flag REPORTS the condition; it does not remove it. AM is receive-only here.
 */
#define DOCK_SET_MOD_PARAM_LEN 1u

/* Wire modulation values. Deliberately NOT ModulationMode_t — see above. */
#define DOCK_MOD_FM  0u
#define DOCK_MOD_AM  1u
/* Reserved: the NUMBER is nailed down so it can never mean anything else, but the
 * VALUE is refused at F7 (DOCK_MOD_ERR_FIELD). Nobody has put this radio on USB on
 * a bench, it cannot transmit in it on this build anyway, and a refusal never moves
 * the radio — so accepting it later is purely additive, while shipping it now would
 * be a confident answer nobody has checked. Guardrail 1. */
#define DOCK_MOD_USB 2u
#define DOCK_MOD_MAX_ACCEPTED DOCK_MOD_AM

/* Reply-only: the radio is on something this wire cannot name (BYP/RAW on a build
 * that has them, or anything a future firmware adds), OR the request was refused.
 *
 * NOT zero. 0x0874 blanks its frequency fields to 0 on a refusal because 0 Hz is
 * obviously not a channel; copying that literally here would blank modulation to 0,
 * which IS DOCK_MOD_FM — a refusal that ships a plausible claim the radio is on FM.
 * Same principle, correct sentinel. */
#define DOCK_MOD_UNKNOWN 0xFFu

/* 0x0878 flags. */
#define DOCK_MOD_FLAG_TX_OK 0x01u  /* the radio will key its own PTT path in this
                                    * modulation; see the note above */

/* 0x0878 status byte. The NUMBERS ARE 0x0874's, holes and all: a host that already
 * decodes a set-VFO status reuses the same table, and "status 4 means a field was
 * off its scale" stays true whichever command produced it. 3 (DIRECTION), 6 (BAND)
 * and 7 (TONE) cannot arise here and are left unused rather than renumbered — holes
 * are free, a code whose meaning depends on which opcode you were looking at is not. */
#define DOCK_MOD_APPLIED       0u  /* on this modulation, exactly as requested   */
#define DOCK_MOD_ERR_SHORT     1u  /* payload shorter than the parameter set     */
#define DOCK_MOD_ERR_BUSY      2u  /* host holds full-control (0x0870)           */
#define DOCK_MOD_ERR_FIELD     4u  /* not a modulation this firmware accepts     */
#define DOCK_MOD_ERR_NO_HAL    5u  /* built without the radio-side binding       */

/* What the radio ended up on, read back out of its own VFO after the firmware
 * applied it — not the value that was handed in. On any rejection `modulation` and
 * `raw` are DOCK_MOD_UNKNOWN and `flags` is 0. */
typedef struct {
    uint8_t status;      /* DOCK_MOD_* */
    uint8_t modulation;  /* DOCK_MOD_*, or DOCK_MOD_UNKNOWN */
    /* The radio's OWN ModulationMode_t value, reported for the same reason 0x0874
     * reports OUTPUT_POWER_*: when the two scales disagree, only the raw value makes
     * it visible. DIAGNOSTIC ONLY — its numbering moves with
     * ENABLE_BYP_RAW_DEMODULATORS, so never branch on it. */
    uint8_t raw;
    uint8_t flags;       /* DOCK_MOD_FLAG_* */
} dock_mod_applied_t;

/* One repeater channel to apply.
 *
 * NOT a straight decode of the wire: `modulation` is carried from the session's
 * sticky value, not from the 0x0873 payload, which has no modulation field and must
 * not grow one (see the 0x0877 note above). Every other member is off the wire.
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
    uint8_t  modulation;    /* DOCK_MOD_*. NOT on the wire — session sticky value */
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
    /* Optional (may be NULL — then 0x0877 answers DOCK_MOD_ERR_NO_HAL). Called on a
     * validated 0x0877 with a wire DOCK_MOD_* value that dock.c has already range-
     * checked. Never called while full_control is set, for the same reason set_vfo
     * is not.
     *
     * MUST fill `out`: DOCK_MOD_APPLIED plus the modulation the radio is NOW on,
     * read back out of its own VFO — not the value it was handed. It reports what
     * happened; it does not re-validate what dock.c already checked. */
    void     (*set_modulation)(void *user, uint8_t wire_mod, dock_mod_applied_t *out);
} dock_hal_t;

typedef struct {
    const dock_hal_t *hal;
    bool     full_control;   /* set by 0x0870, cleared by 0x0871 */
    bool     tx_on;          /* cached REG_30 TX-enable state, for edge-detect */
    /* Session sticky modulation, DOCK_MOD_*. Applied by 0x0873 as well as 0x0877.
     * SEEDED FROM A CONSTANT in dock_init and only ever advanced by an 0x0877 that
     * the radio actually applied — never read back out of the radio, which would be
     * the ADR 0132 "adopt whatever state you find" fault. See the note above. */
    uint8_t  modulation;
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

/* Build and send the 0x0878 set-modulation reply. Exposed for the harness. */
void dock_send_set_mod_reply(dock_ctx_t *ctx, const dock_mod_applied_t *r);

/* Framing primitives (exposed for the harness). CRC-16/XMODEM. */
uint16_t dock_crc16(const uint8_t *data, uint16_t len);
void     dock_obfuscate(uint8_t *data, uint16_t len); /* self-inverse XOR */

#endif /* APP_DOCK_H */
