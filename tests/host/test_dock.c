/* Host-side unit tests for App/app/dock.c — the UV-K5 V3 dock protocol core.
 *
 * The "definition of done" for the F2 port is byte-compatibility with
 * radio-server's FirmwareFakeSerial + Uvk5Decoder
 * (radio_server/backends/uvk5/, tests/test_uvk5_transport.py). These cases
 * mirror that fake's acceptance / dispatch / reply rules and pin one
 * byte-exact reply vector as an independent oracle.
 *
 * Build & run:  make -C tests/host run     (or: cc -I App tests/host/test_dock.c App/app/dock.c)
 */

#include "app/dock.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- tiny test framework ---- */
static int g_checks = 0, g_fail = 0;
#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            printf("  FAIL: %s (line %d)\n", (msg), __LINE__);              \
            g_fail++;                                                       \
        }                                                                   \
    } while (0)

/* ---- fake HAL ---- */
static uint16_t g_regs[256];
static uint8_t  g_cap[2048];
static uint16_t g_caplen;
static int      g_reads, g_writes;

/* TX-state spy (F5). g_ev logs the *order* of events: 'w' on a REG_30 write,
 * '1'/'0' on a tx_set(on/off) callback — so "1w" proves the PA edge is driven
 * BEFORE the register write completes. */
static int      g_tx_calls, g_tx_last;   /* g_tx_last: 1=on, 0=off, -1=none yet */
static char     g_ev[128];
static int      g_evn;

static void ev_push(char c)
{
    if (g_evn < (int)sizeof(g_ev) - 1) g_ev[g_evn++] = c;
}
static uint16_t hal_read(void *u, uint16_t reg)
{
    (void)u; g_reads++;
    return g_regs[reg & 0xFF];
}
static void hal_write(void *u, uint16_t reg, uint16_t val)
{
    (void)u; g_writes++;
    if ((reg & 0xFF) == 0x30) ev_push('w');
    g_regs[reg & 0xFF] = val;
}
static void hal_send(void *u, const uint8_t *buf, uint16_t len)
{
    (void)u;
    if ((uint32_t)g_caplen + len <= sizeof(g_cap)) {
        memcpy(g_cap + g_caplen, buf, len);
        g_caplen = (uint16_t)(g_caplen + len);
    }
}
static void hal_tx(void *u, bool on)
{
    (void)u; g_tx_calls++; g_tx_last = on ? 1 : 0;
    ev_push(on ? '1' : '0');
}
/* set-VFO spy (0x0872). Records the last decoded channel and how many times the
 * firmware would have been asked to apply one — the count is what proves a
 * malformed or out-of-range frame was REFUSED rather than quietly passed on to
 * the radio's own VFO struct. */
static int        g_vfo_calls;
static dock_vfo_t g_vfo_last;

static void hal_set_vfo(void *u, const dock_vfo_t *vfo)
{
    (void)u; g_vfo_calls++; g_vfo_last = *vfo;
}
static const dock_hal_t HAL = { hal_read, hal_write, hal_send, NULL, hal_tx, hal_set_vfo };

static dock_ctx_t ctx;

static void reset(void)
{
    memset(g_regs, 0, sizeof(g_regs));
    g_caplen = 0; g_reads = 0; g_writes = 0;
    g_tx_calls = 0; g_tx_last = -1; g_evn = 0; memset(g_ev, 0, sizeof(g_ev));
    g_vfo_calls = 0; memset(&g_vfo_last, 0, sizeof(g_vfo_last));
    dock_init(&ctx, &HAL);
}

/* Build an obfuscated COMMAND frame with a real CRC (what radio-server sends).
 * payload = [opcode:u16][param_len:u16][params]; frame Size = 4 + param_len. */
static uint16_t build_cmd(uint8_t *out, uint16_t opcode,
                          const uint8_t *params, uint16_t plen)
{
    uint8_t body[DOCK_MAX_PAYLOAD + 2];
    uint16_t size = (uint16_t)(4 + plen);
    body[0] = (uint8_t)(opcode & 0xFF); body[1] = (uint8_t)(opcode >> 8);
    body[2] = (uint8_t)(plen & 0xFF);   body[3] = (uint8_t)(plen >> 8);
    memcpy(body + 4, params, plen);
    uint16_t crc = dock_crc16(body, size);
    body[size] = (uint8_t)(crc & 0xFF); body[size + 1] = (uint8_t)(crc >> 8);
    dock_obfuscate(body, (uint16_t)(size + 2));

    uint16_t n = 0;
    out[n++] = 0xAB; out[n++] = 0xCD;
    out[n++] = (uint8_t)(size & 0xFF); out[n++] = (uint8_t)(size >> 8);
    memcpy(out + n, body, size + 2); n = (uint16_t)(n + size + 2);
    out[n++] = 0xDC; out[n++] = 0xBA;
    return n;
}

static void feed(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) dock_rx_byte(&ctx, buf[i]);
}

/* little-endian param builders */
static uint16_t p_read(uint8_t *p, const uint16_t *regs, uint16_t count)
{
    uint16_t n = 0;
    p[n++] = (uint8_t)(count & 0xFF); p[n++] = (uint8_t)(count >> 8);
    for (uint16_t i = 0; i < count; i++) {
        p[n++] = (uint8_t)(regs[i] & 0xFF); p[n++] = (uint8_t)(regs[i] >> 8);
    }
    return n;
}
static uint16_t p_write(uint8_t *p, const uint16_t *pairs, uint16_t count)
{
    uint16_t n = 0;
    p[n++] = (uint8_t)(count & 0xFF); p[n++] = (uint8_t)(count >> 8);
    for (uint16_t i = 0; i < count; i++) {
        p[n++] = (uint8_t)(pairs[i * 2] & 0xFF);     p[n++] = (uint8_t)(pairs[i * 2] >> 8);
        p[n++] = (uint8_t)(pairs[i * 2 + 1] & 0xFF); p[n++] = (uint8_t)(pairs[i * 2 + 1] >> 8);
    }
    return n;
}

int main(void)
{
    uint8_t frame[512], params[512];
    uint16_t flen, plen;

    /* 1. Connect probe: ReadRegisters([0x30]) -> exactly one 0x0951 reply,
     *    byte-exact against a hand-computed golden (independent oracle). */
    reset();
    g_regs[0x30] = 0xC1FE;
    { uint16_t r[] = { 0x30 }; plen = p_read(params, r, 1); }
    flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
    feed(frame, flen);
    {
        static const uint8_t golden[] = {
            0xAB, 0xCD, 0x08, 0x00,
            0x47, 0x65, 0x10, 0xE6, 0x1E, 0x91, 0xF3, 0x81, 0xDE, 0xCA,
            0xDC, 0xBA,
        };
        CHECK(g_reads == 1, "connect probe: exactly one register read");
        CHECK(g_caplen == sizeof(golden), "connect probe: reply length");
        CHECK(g_caplen == sizeof(golden) &&
              memcmp(g_cap, golden, sizeof(golden)) == 0,
              "connect probe: byte-exact 0x0951 reply vector");
    }

    /* 2. WriteRegisters -> no reply, register store updated. */
    reset();
    { uint16_t pr[] = { 0x30, 0xC1FE }; plen = p_write(params, pr, 1); }
    flen = build_cmd(frame, DOCK_CMD_WRITE_REGS, params, plen);
    feed(frame, flen);
    CHECK(g_writes == 1 && g_regs[0x30] == 0xC1FE, "write: store updated");
    CHECK(g_caplen == 0, "write: no reply");

    /* 3. ReadRegisters of several -> one 0x0951 per register, in order. */
    reset();
    g_regs[0x38] = 0x1111; g_regs[0x39] = 0x2222; g_regs[0x33] = 0x3333;
    { uint16_t r[] = { 0x38, 0x39, 0x33 }; plen = p_read(params, r, 3); }
    flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
    feed(frame, flen);
    CHECK(g_reads == 3, "multi-read: three reads");
    CHECK(g_caplen == 3 * 16, "multi-read: three framed replies");
    /* second reply's register field (offset 16 + 4, de-obfuscated) == 0x39 */
    if (g_caplen == 48) {
        uint8_t body[10];
        memcpy(body, g_cap + 16 + 4, 10);            /* 2nd frame, payload at +4 */
        dock_obfuscate(body, 10);
        /* payload = [opcode:u16][param_len:u16][reg:u16][value:u16] */
        uint16_t op  = (uint16_t)(body[0] | (body[1] << 8));
        uint16_t reg = (uint16_t)(body[4] | (body[5] << 8));
        uint16_t val = (uint16_t)(body[6] | (body[7] << 8));
        CHECK(op == DOCK_REPLY_REG_INFO && reg == 0x39 && val == 0x2222,
              "multi-read: 2nd reply opcode/reg/value");
    }

    /* 4. Bad command CRC -> dropped: no dispatch, no reply. */
    reset();
    { uint16_t r[] = { 0x30 }; plen = p_read(params, r, 1); }
    flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
    frame[6] ^= 0xFF;                 /* corrupt an obfuscated payload byte */
    feed(frame, flen);
    CHECK(g_reads == 0 && g_caplen == 0, "bad CRC: dropped");

    /* 5. Bad footer -> dropped. */
    reset();
    { uint16_t r[] = { 0x30 }; plen = p_read(params, r, 1); }
    flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
    frame[flen - 1] = 0x00;           /* break 0xBA */
    feed(frame, flen);
    CHECK(g_reads == 0 && g_caplen == 0, "bad footer: dropped");

    /* 6. Zero-size frame -> dropped, and a following good frame still parses. */
    reset();
    g_regs[0x30] = 0x1234;
    {
        uint8_t junk[] = { 0xAB, 0xCD, 0x00, 0x00, 0xDC, 0xBA };
        feed(junk, sizeof(junk));
        CHECK(g_caplen == 0, "zero-size: dropped");
        { uint16_t r[] = { 0x30 }; plen = p_read(params, r, 1); }
        flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
        feed(frame, flen);
        CHECK(g_reads == 1, "recover after zero-size");
    }

    /* 7. Oversize (Size > 254) -> dropped. */
    reset();
    {
        uint8_t big[8] = { 0xAB, 0xCD, 0xFF, 0x00 /* Size=255 */, 0, 0, 0xDC, 0xBA };
        feed(big, sizeof(big));
        CHECK(g_reads == 0 && g_caplen == 0, "oversize: dropped");
    }

    /* 8. Leading garbage + bad 2nd-preamble byte, then a good frame. */
    reset();
    g_regs[0x30] = 0x55AA;
    {
        uint8_t noise[] = { 0x00, 0x11, 0xAB, 0x00, 0xAB, 0x22 };  /* AB not followed by CD */
        feed(noise, sizeof(noise));
        { uint16_t r[] = { 0x30 }; plen = p_read(params, r, 1); }
        flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
        feed(frame, flen);
        CHECK(g_reads == 1, "resync past garbage / bad 2nd preamble");
    }

    /* 9. Frame split one byte at a time still parses (streaming). */
    reset();
    g_regs[0x30] = 0xABCD;
    { uint16_t r[] = { 0x30 }; plen = p_read(params, r, 1); }
    flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
    feed(frame, flen);               /* feed() already delivers byte-by-byte */
    CHECK(g_reads == 1 && g_caplen == 16, "streaming byte-by-byte");

    /* 10. Full-control: 0x0870 sets flag; register R/W works while in it;
     *     0x0871 clears it. (Matches the fake's full_control flag + shared dispatch.) */
    reset();
    flen = build_cmd(frame, DOCK_CMD_ENTER_HW, params, 0);
    feed(frame, flen);
    CHECK(ctx.full_control, "0x0870 enters full-control");
    g_regs[0x30] = 0x0BEE;
    { uint16_t r[] = { 0x30 }; plen = p_read(params, r, 1); }
    flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
    feed(frame, flen);
    CHECK(g_reads == 1 && ctx.full_control, "read works inside full-control");
    flen = build_cmd(frame, DOCK_CMD_EXIT_HW, params, 0);
    feed(frame, flen);
    CHECK(!ctx.full_control, "0x0871 exits full-control");

    /* 11. Malformed frame immediately followed by a good one, single buffer. */
    reset();
    g_regs[0x51] = 0x9999;
    {
        uint8_t buf[600]; uint16_t n = 0;
        uint16_t bad = build_cmd(frame, DOCK_CMD_READ_REGS, params, 0); /* count=0 read: valid but no reply */
        (void)bad;
        /* a truly malformed lead: AB CD with wrong footer */
        uint8_t junk[] = { 0xAB, 0xCD, 0x04, 0x00, 1, 2, 3, 4, 5, 6, 0x00, 0x00 };
        memcpy(buf + n, junk, sizeof(junk)); n = (uint16_t)(n + sizeof(junk));
        { uint16_t r[] = { 0x51 }; plen = p_read(params, r, 1); }
        flen = build_cmd(frame, DOCK_CMD_READ_REGS, params, plen);
        memcpy(buf + n, frame, flen); n = (uint16_t)(n + flen);
        feed(buf, n);
        CHECK(g_reads == 1, "good frame after malformed in same buffer");
    }

    /* ===== F5: REG_30 TX-state seam (drives the physical PA) ===== */
    /* The app's TX word is 0xC1FE (ENABLE_TX_DSP, bit1, set); its RX word is
     * 0xBFF1 (TX_DSP clear). dock.c edge-detects that bit and calls tx_set()
     * BEFORE completing the REG_30 write. */

    /* 12. Key: write REG_30=0xC1FE -> tx_set(true) fires once, before the write. */
    reset();
    { uint16_t pr[] = { 0x30, 0xC1FE }; plen = p_write(params, pr, 1); }
    flen = build_cmd(frame, DOCK_CMD_WRITE_REGS, params, plen);
    feed(frame, flen);
    CHECK(g_tx_calls == 1 && g_tx_last == 1, "key: tx_set(true) fires once");
    CHECK(strcmp(g_ev, "1w") == 0, "key: PA engaged BEFORE the REG_30 write");
    CHECK(ctx.tx_on && g_regs[0x30] == 0xC1FE, "key: tx_on set, REG_30 written");

    /* 13. Key then un-key: each edge drives the PA before its write ("1w0w"). */
    reset();
    { uint16_t pr[] = { 0x30, 0xC1FE }; plen = p_write(params, pr, 1); }
    flen = build_cmd(frame, DOCK_CMD_WRITE_REGS, params, plen); feed(frame, flen);
    { uint16_t pr[] = { 0x30, 0xBFF1 }; plen = p_write(params, pr, 1); }
    flen = build_cmd(frame, DOCK_CMD_WRITE_REGS, params, plen); feed(frame, flen);
    CHECK(g_tx_calls == 2 && g_tx_last == 0, "un-key: tx_set(false) fires");
    CHECK(strcmp(g_ev, "1w0w") == 0, "each PA edge precedes its REG_30 write");
    CHECK(!ctx.tx_on, "un-key: tx_on cleared");

    /* 14. Edge-detect: repeated same-state key writes fire tx_set once. */
    reset();
    for (int k = 0; k < 3; k++) {
        uint16_t pr[] = { 0x30, 0xC1FE }; plen = p_write(params, pr, 1);
        flen = build_cmd(frame, DOCK_CMD_WRITE_REGS, params, plen); feed(frame, flen);
    }
    CHECK(g_tx_calls == 1 && g_writes == 3, "repeat key: one PA engage, three writes");
    CHECK(strcmp(g_ev, "1www") == 0, "repeat key: PA edge only on the first write");

    /* 15. Writes to other registers never touch the TX state. */
    reset();
    { uint16_t pr[] = { 0x38, 0xC1FE, 0x36, 0x7FFF }; plen = p_write(params, pr, 2); }
    flen = build_cmd(frame, DOCK_CMD_WRITE_REGS, params, plen);
    feed(frame, flen);
    CHECK(g_tx_calls == 0 && !ctx.tx_on, "non-REG_30 writes never key");

    /* 16. Fail-safe: a dangling key is dropped at 0x0870 enter and at 0x0871 exit. */
    reset();
    { uint16_t pr[] = { 0x30, 0xC1FE }; plen = p_write(params, pr, 1); }        /* key, no un-key */
    flen = build_cmd(frame, DOCK_CMD_WRITE_REGS, params, plen); feed(frame, flen);
    flen = build_cmd(frame, DOCK_CMD_ENTER_HW, params, 0); feed(frame, flen);   /* 0x0870 */
    CHECK(g_tx_calls == 2 && g_tx_last == 0 && !ctx.tx_on, "0x0870 enter clears a stale key");
    { uint16_t pr[] = { 0x30, 0xC1FE }; plen = p_write(params, pr, 1); }        /* key again inside */
    flen = build_cmd(frame, DOCK_CMD_WRITE_REGS, params, plen); feed(frame, flen);
    CHECK(g_tx_calls == 3 && ctx.tx_on, "re-key inside full-control");
    flen = build_cmd(frame, DOCK_CMD_EXIT_HW, params, 0); feed(frame, flen);    /* 0x0871 */
    CHECK(g_tx_calls == 4 && g_tx_last == 0 && !ctx.tx_on, "0x0871 exit drops the PA");

    /* ---- 0x0872 set-VFO -------------------------------------------------
     *
     * This is the one command whose whole point is to outlive the dock session,
     * so its failure modes are different in kind from the register commands:
     * a register write that goes wrong is undone by the next 0x0871, while a
     * VFO written wrong is what the radio transmits on after we walk away.
     * Hence "refuse" rather than "clamp" everywhere below. */

    /* 17. A well-formed repeater channel decodes field for field. K0PRA:
     *     receive 448.525, transmit 5 MHz down, 100.0 Hz, wide, high power. */
    reset();
    {
        uint8_t v[DOCK_SET_VFO_PARAM_LEN] = {
            0xC8, 0xF2, 0xBB, 0x1A,   /* rx_hz  448 525 000 */
            0x40, 0x4B, 0x4C, 0x00,   /* offset   5 000 000 */
            0xE8, 0x03,               /* ctcss tenths 1000 = 100.0 Hz */
            DOCK_OFFSET_SUB,          /* direction */
            0x00,                     /* wide */
            0x02,                     /* high power */
        };
        flen = build_cmd(frame, DOCK_CMD_SET_VFO, v, sizeof(v));
        feed(frame, flen);
    }
    CHECK(g_vfo_calls == 1, "0x0872: a well-formed channel is applied once");
    CHECK(g_vfo_last.rx_hz == 448525000u, "0x0872: rx frequency decoded");
    CHECK(g_vfo_last.offset_hz == 5000000u, "0x0872: offset decoded");
    CHECK(g_vfo_last.ctcss_tenths == 1000u, "0x0872: CTCSS decoded in tenths");
    CHECK(g_vfo_last.direction == DOCK_OFFSET_SUB, "0x0872: direction decoded");
    CHECK(g_vfo_last.narrow == 0 && g_vfo_last.power == 2, "0x0872: bandwidth/power decoded");
    CHECK(g_writes == 0, "0x0872: writes no registers itself");
    CHECK(g_caplen == 0, "0x0872: sends no reply");

    /* 18. A simplex channel is expressible: no offset, no tone. */
    reset();
    {
        uint8_t v[DOCK_SET_VFO_PARAM_LEN] = {
            0x40, 0x5E, 0x92, 0x1A,   /* 445 800 000 */
            0, 0, 0, 0,               /* no offset */
            0, 0,                     /* no tone */
            DOCK_OFFSET_NONE, 0, 1,
        };
        flen = build_cmd(frame, DOCK_CMD_SET_VFO, v, sizeof(v));
        feed(frame, flen);
    }
    CHECK(g_vfo_calls == 1 && g_vfo_last.rx_hz == 445800000u, "0x0872: simplex channel applies");
    CHECK(g_vfo_last.ctcss_tenths == 0 && g_vfo_last.direction == DOCK_OFFSET_NONE,
          "0x0872: no tone and no offset survive as zero, not as garbage");

    /* 19. A truncated payload is refused, not read past. The frame is otherwise
     *     valid, so nothing but the length check stands between a short frame
     *     and reading whatever follows it in the RX buffer. */
    reset();
    {
        uint8_t v[DOCK_SET_VFO_PARAM_LEN - 1] = { 0 };
        flen = build_cmd(frame, DOCK_CMD_SET_VFO, v, sizeof(v));
        feed(frame, flen);
    }
    CHECK(g_vfo_calls == 0, "0x0872: a short payload is refused");

    /* 20. Out-of-range fields are refused rather than clamped. A bad direction
     *     byte silently treated as "simplex" would transmit on the repeater's
     *     OUTPUT — on top of the machine, and on top of whoever it is repeating. */
    reset();
    {
        uint8_t v[DOCK_SET_VFO_PARAM_LEN] = {
            0x40, 0x5E, 0x92, 0x1A, 0x40, 0x4B, 0x4C, 0x00, 0xE8, 0x03,
            0x07,        /* direction: not one of NONE/ADD/SUB */
            0, 1,
        };
        flen = build_cmd(frame, DOCK_CMD_SET_VFO, v, sizeof(v));
        feed(frame, flen);
        CHECK(g_vfo_calls == 0, "0x0872: an unknown offset direction is refused");

        v[10] = DOCK_OFFSET_SUB; v[11] = 0x05;    /* bandwidth: neither wide nor narrow */
        flen = build_cmd(frame, DOCK_CMD_SET_VFO, v, sizeof(v));
        feed(frame, flen);
        CHECK(g_vfo_calls == 0, "0x0872: an unknown bandwidth is refused");

        v[11] = 0; v[12] = 0x09;                  /* power: off the end of the scale */
        flen = build_cmd(frame, DOCK_CMD_SET_VFO, v, sizeof(v));
        feed(frame, flen);
        CHECK(g_vfo_calls == 0, "0x0872: an unknown power level is refused");
    }

    /* 21. Refused inside full-control. Applying a VFO mid-dock would call
     *     RADIO_SetupRegisters underneath a host that believes it owns the
     *     synthesiser — the "adopt whatever you find" fault ADR 0132 removed. */
    reset();
    flen = build_cmd(frame, DOCK_CMD_ENTER_HW, params, 0); feed(frame, flen);
    {
        uint8_t v[DOCK_SET_VFO_PARAM_LEN] = {
            0x40, 0x5E, 0x92, 0x1A, 0, 0, 0, 0, 0, 0, DOCK_OFFSET_NONE, 0, 1,
        };
        flen = build_cmd(frame, DOCK_CMD_SET_VFO, v, sizeof(v));
        feed(frame, flen);
        CHECK(g_vfo_calls == 0, "0x0872: refused while the host holds full-control");

        flen = build_cmd(frame, DOCK_CMD_EXIT_HW, params, 0); feed(frame, flen);
        flen = build_cmd(frame, DOCK_CMD_SET_VFO, v, sizeof(v));
        feed(frame, flen);
        CHECK(g_vfo_calls == 1, "0x0872: accepted once full-control is released");
    }

    /* 22. Never keys. This command runs while the radio is a radio, so if it
     *     could touch the TX state it would key one outside the dock's own
     *     fail-safe seams, with nothing tracking it. */
    CHECK(g_tx_calls == 0 && !ctx.tx_on, "0x0872: no PA activity of its own");

    /* ---- report ---- */
    printf("dock host tests: %d checks, %d failures\n", g_checks, g_fail);
    if (g_fail) { printf("RESULT: FAIL\n"); return 1; }
    printf("RESULT: PASS\n");
    return 0;
}
