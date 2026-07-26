/* Copyright 2026 Kris Bennett (radio-server dock control mode)
 *
 * Portions derived from nicsure's "Quansheng Dock" firmware, app/uart.c
 * (https://github.com/nicsure/quansheng-dock-fw, Apache-2.0): the framing,
 * the 16-byte XOR obfuscation table, the dummy-CRC reply, and the
 * register-read/write dispatch. See NOTICE.
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

#include "app/dock.h"

#include <string.h>

/* 16-byte XOR obfuscation table, identical to the classic dock and to this
 * tree's app/uart.c Obfuscation[16] (and radio_server frames.py OBFUSCATION). */
static const uint8_t DOCK_OBF[16] = {
    0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40,
    0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80,
};

uint16_t dock_crc16(const uint8_t *data, uint16_t len)
{
    /* CRC-16/XMODEM: poly 0x1021, init 0, no reflection, no final xor.
     * Same as this tree's driver/crc.c CRC_Calculate. */
    uint16_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

void dock_obfuscate(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        data[i] ^= DOCK_OBF[i % 16];
}

/* BK4819 REG_30 bit 1 = ENABLE_TX_DSP: set in the app's TX word (0xC1FE),
 * clear in its RX word (0xBFF1) — the single bit that distinguishes a transmit
 * key from receive. Defined locally so this core stays free of any firmware-tree
 * include (mirrors bk4819-regs.h BK4819_REG_30_MASK_ENABLE_TX_DSP). */
#define DOCK_REG30_TX_DSP 0x0002u

/* Edge-detect the REG_30 TX-enable state and, only on a change, notify the HAL
 * so it can drive the physical PA. Idempotent: a force-off when already off is a
 * no-op, so the enter/exit/overflow fail-safe calls never spuriously re-key. */
static void dock_set_tx(dock_ctx_t *ctx, bool on)
{
    if (on == ctx->tx_on) return;
    ctx->tx_on = on;
    if (ctx->hal->tx_set)
        ctx->hal->tx_set(ctx->hal->user, on);
}

void dock_init(dock_ctx_t *ctx, const dock_hal_t *hal)
{
    ctx->hal          = hal;
    ctx->full_control = false;
    ctx->tx_on        = false;
    ctx->len          = 0;
}

/* Longest parameter block this core replies with (0x0874's 12; 0x0951 uses 4).
 * Named rather than implied by the largest caller, so adding a reply that does
 * not fit is a compile-time size check instead of a stack overrun. */
#define DOCK_REPLY_MAX_PARAMS 12u

/* Assemble and emit one reply frame: preamble, Size, obfuscated
 * [opcode][param_len][params] + dummy CRC, footer. */
static void dock_send_payload(dock_ctx_t *ctx, uint16_t opcode,
                              const uint8_t *params, uint16_t plen)
{
    if (plen > DOCK_REPLY_MAX_PARAMS) return;   /* unreachable; see the #define */

    uint8_t body[4 + DOCK_REPLY_MAX_PARAMS + 2];
    const uint16_t size = (uint16_t)(4 + plen);
    body[0] = (uint8_t)(opcode & 0xFF);
    body[1] = (uint8_t)(opcode >> 8);
    body[2] = (uint8_t)(plen & 0xFF);
    body[3] = (uint8_t)(plen >> 8);
    if (plen)
        memcpy(body + 4, params, plen);

    /* Replies carry a DUMMY CRC: obf(0xFF 0xFF) in the CRC slot. Obfuscate
     * payload + {0xFF,0xFF} together over Size+2 bytes (matches SendReply). */
    body[size]     = 0xFF;
    body[size + 1] = 0xFF;
    dock_obfuscate(body, (uint16_t)(size + 2));

    uint8_t frame[2 + 2 + (4 + DOCK_REPLY_MAX_PARAMS + 2) + 2];
    uint16_t n = 0;
    frame[n++] = 0xAB; frame[n++] = 0xCD;                 /* preamble */
    frame[n++] = (uint8_t)(size & 0xFF);
    frame[n++] = (uint8_t)(size >> 8);                    /* Size (LE) */
    memcpy(frame + n, body, (size_t)(size + 2));
    n = (uint16_t)(n + size + 2);                         /* obf body + dummy CRC */
    frame[n++] = 0xDC; frame[n++] = 0xBA;                 /* footer */

    ctx->hal->send(ctx->hal->user, frame, n);
}

void dock_send_register_info(dock_ctx_t *ctx, uint16_t reg, uint16_t value)
{
    /* payload = [0x0951][param_len=4][reg:u16][value:u16], Size = 8. */
    const uint8_t p[4] = {
        (uint8_t)(reg & 0xFF),   (uint8_t)(reg >> 8),
        (uint8_t)(value & 0xFF), (uint8_t)(value >> 8),
    };
    dock_send_payload(ctx, DOCK_REPLY_REG_INFO, p, sizeof(p));
}

void dock_send_set_vfo_reply(dock_ctx_t *ctx, const dock_vfo_applied_t *r)
{
    /* payload = [0x0874][param_len=12][status:u8][reserved:u8][rx:u32][tx:u32]
     * [ctcss_tenths:u16]. The reserved byte is zero and exists so a later flags
     * field can be added without moving the two frequencies. */
    const uint8_t p[12] = {
        r->status, 0,
        (uint8_t)(r->rx_hz),        (uint8_t)(r->rx_hz >> 8),
        (uint8_t)(r->rx_hz >> 16),  (uint8_t)(r->rx_hz >> 24),
        (uint8_t)(r->tx_hz),        (uint8_t)(r->tx_hz >> 8),
        (uint8_t)(r->tx_hz >> 16),  (uint8_t)(r->tx_hz >> 24),
        (uint8_t)(r->ctcss_tenths), (uint8_t)(r->ctcss_tenths >> 8),
    };
    dock_send_payload(ctx, DOCK_REPLY_SET_VFO, p, sizeof(p));
}

/* Little-endian u32 off the wire. Byte-at-a-time rather than a cast, because
 * the payload sits at an arbitrary offset in the RX buffer and this core is
 * compiled for both an ARM target and the host harness. */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void dock_dispatch(dock_ctx_t *ctx, const uint8_t *payload, uint16_t size)
{
    if (size < 4) return;                     /* too short for inner header */
    const uint16_t opcode = (uint16_t)(payload[0] | (payload[1] << 8));
    const uint16_t plen   = (uint16_t)(payload[2] | (payload[3] << 8));
    if ((uint32_t)plen + 4u > size) return;   /* inner length overruns frame */
    const uint8_t *params = payload + 4;

    switch (opcode) {
    case DOCK_CMD_ENTER_HW:
        ctx->full_control = true;
        dock_set_tx(ctx, false);   /* fail-safe: never inherit a stale key */
        break;

    case DOCK_CMD_EXIT_HW:
        ctx->full_control = false;
        dock_set_tx(ctx, false);   /* drop the PA before leaving full-control */
        break;

    case DOCK_CMD_SET_VFO: {
        /* Set the firmware's VFO so the RADIO retunes itself, rather than
         * writing registers the 0x0871 exit is going to throw away. See the
         * long note in dock.h.
         *
         * EVERY path below answers 0x0874, including every refusal. The first
         * draft of this command was silent, which made "refused" and "applied"
         * the same event on the wire — and this is the one command whose result
         * outlives the dock session, so a caller that cannot tell them apart
         * walks away believing the radio is on a channel it never reached. */
        dock_vfo_applied_t res;
        memset(&res, 0, sizeof(res));

        if (plen < DOCK_SET_VFO_PARAM_LEN) {
            res.status = DOCK_VFO_ERR_SHORT;        /* never read past the payload */
        } else if (ctx->full_control) {
            res.status = DOCK_VFO_ERR_BUSY;         /* the host owns the chip */
        } else {
            dock_vfo_t vfo;
            vfo.rx_hz        = rd32(params);
            vfo.offset_hz    = rd32(params + 4);
            vfo.ctcss_tenths = (uint16_t)(params[8] | (params[9] << 8));
            vfo.direction    = params[10];
            vfo.narrow       = params[11];
            vfo.power        = params[12];
            /* Refuse nonsense rather than pass it into the radio's own VFO
             * struct. A bad direction byte would otherwise transmit somewhere
             * unintended, which on a repeater input is somebody else's problem,
             * not ours. */
            if (vfo.direction > DOCK_OFFSET_SUB)
                res.status = DOCK_VFO_ERR_DIRECTION;
            else if (vfo.narrow > 1u || vfo.power > 2u)
                res.status = DOCK_VFO_ERR_FIELD;
            else if (!ctx->hal->set_vfo)
                res.status = DOCK_VFO_ERR_NO_HAL;
            else
                ctx->hal->set_vfo(ctx->hal->user, &vfo, &res);
        }

        /* The wire contract is unconditional: a non-zero status never carries
         * frequencies. Enforced here rather than trusted to each HAL, so a
         * binding that forgets cannot publish a channel the radio is not on. */
        if (res.status != DOCK_VFO_APPLIED) {
            res.rx_hz = 0; res.tx_hz = 0; res.ctcss_tenths = 0;
        }
        dock_send_set_vfo_reply(ctx, &res);
        break;
    }

    case DOCK_CMD_WRITE_REGS: {
        if (plen < 2) break;
        const uint16_t count = (uint16_t)(params[0] | (params[1] << 8));
        const uint8_t *p = params + 2;
        const uint16_t avail = (uint16_t)(plen - 2);      /* bytes of pair data */
        for (uint16_t i = 0; i < count; i++) {
            if ((uint32_t)(i + 1) * 4u > avail) break;    /* each pair = 4 bytes */
            const uint16_t reg = (uint16_t)(p[i * 4]     | (p[i * 4 + 1] << 8));
            const uint16_t val = (uint16_t)(p[i * 4 + 2] | (p[i * 4 + 3] << 8));
            /* Drive the PA on the REG_30 TX-enable edge BEFORE completing the
             * write (key: PA up then write; un-key: PA down then write). */
            if (reg == 0x30)
                dock_set_tx(ctx, (val & DOCK_REG30_TX_DSP) != 0);
            ctx->hal->write_reg(ctx->hal->user, reg, val);
        }
        break;                                            /* no reply */
    }

    case DOCK_CMD_READ_REGS: {
        if (plen < 2) break;
        const uint16_t count = (uint16_t)(params[0] | (params[1] << 8));
        const uint8_t *p = params + 2;
        const uint16_t avail = (uint16_t)(plen - 2);      /* bytes of register list */
        for (uint16_t i = 0; i < count; i++) {
            if ((uint32_t)(i + 1) * 2u > avail) break;    /* each register = 2 bytes */
            const uint16_t reg = (uint16_t)(p[i * 2] | (p[i * 2 + 1] << 8));
            const uint16_t val = ctx->hal->read_reg(ctx->hal->user, reg);
            dock_send_register_info(ctx, reg, val);       /* one 0x0951 per register */
        }
        break;
    }

    default:
        break;                                            /* unknown -> no reply */
    }
}

/* Try to extract and dispatch complete frames from the front of the buffer.
 * Mirrors app/uart.c UART_IsCommandAvailable + radio_server FirmwareFakeSerial
 * _consume(): drop-and-resync on any malformed input, never truncate. */
static void dock_consume(dock_ctx_t *ctx)
{
    for (;;) {
        uint8_t *buf = ctx->buf;
        uint16_t n   = ctx->len;

        /* Sync to preamble start 0xAB. */
        uint16_t start = 0;
        while (start < n && buf[start] != 0xAB) start++;
        if (start > 0) {
            memmove(buf, buf + start, n - start);
            ctx->len = (uint16_t)(n - start);
            n = ctx->len;
        }
        if (n < 4) return;                    /* need preamble + Size */
        if (buf[1] != 0xCD) {                 /* not 0xCD after 0xAB - advance */
            memmove(buf, buf + 1, n - 1);
            ctx->len = (uint16_t)(n - 1);
            continue;
        }

        const uint16_t size  = (uint16_t)(buf[2] | (buf[3] << 8));
        const uint16_t total = (uint16_t)(size + 8);
        if (size == 0 || size > DOCK_MAX_PAYLOAD) {   /* bogus length - resync */
            memmove(buf, buf + 2, n - 2);
            ctx->len = (uint16_t)(n - 2);
            continue;
        }
        if (n < total) return;                /* wait for the rest of the frame */

        const uint16_t footer = (uint16_t)(4 + size + 2);
        if (buf[footer] != 0xDC || buf[footer + 1] != 0xBA) {  /* bad footer */
            memmove(buf, buf + 2, n - 2);
            ctx->len = (uint16_t)(n - 2);
            continue;
        }

        /* Accept: de-obfuscate payload + CRC (Size+2 bytes), validate CRC. */
        uint8_t work[DOCK_MAX_PAYLOAD + 2];
        memcpy(work, buf + 4, size + 2);
        dock_obfuscate(work, (uint16_t)(size + 2));
        const uint16_t crc = (uint16_t)(work[size] | (work[size + 1] << 8));
        if (dock_crc16(work, size) == crc)
            dock_dispatch(ctx, work, size);   /* mismatch -> silently dropped */

        /* Consume the whole frame regardless of CRC result. */
        memmove(buf, buf + total, n - total);
        ctx->len = (uint16_t)(n - total);
    }
}

void dock_rx_byte(dock_ctx_t *ctx, uint8_t b)
{
    if (ctx->len >= DOCK_RX_BUF) ctx->len = 0;   /* overflow guard: resync */
    ctx->buf[ctx->len++] = b;
    dock_consume(ctx);
}
