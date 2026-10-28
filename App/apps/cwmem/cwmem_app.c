/* Copyright 2026 Johan Denoyer F4WAT
 * https://github.com/jdenoy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * CW Mem - message keyer opened from CW Keyer or CW USB with key 7.
 *
 * The keyer hands over its settings (WPM, tone, break-in, crystal
 * correction, speaker) and its UP/DOWN offset; CW USB hands over only once
 * armed and comes back armed.
 * Keys 1 to 9 send messages with the keyer's receive, TX keying and timing
 * code (shared source, CW_KEYER_MEM): 1 to 6 are fixed, with the callsign
 * and the serial number (NR), 7 to 9 are the user's. The callsign and the
 * custom messages sit in a user block of the assets (cwmem_user.h) that
 * CW Mem Edit writes, key 0; an empty callsign is the boot message's
 * (LINE 1). NR lives in this app's configuration. A touch on the key or
 * paddle of the sending app, PTT or EXIT hands back to it, with the same
 * offset, so the reply is answered with the usual key.
 */

#define CW_KEYER_MEM 1
#include "cwmem_assets.h"
#include "cwmem_user.h"
#include "../cwkeyer/cwkeyer_app.c"

#define MEM_MAGIC  0x4Eu   /* 'N': NR and the sending keyer */
#define NR_MAX     9999u
#define TOUCH_POLLS   3u   /* consecutive closed polls, about 3.6 ms */
#define COL_X         2u   /* a capsule starts 2 px left of its text */
#define WPM_X        34u   /* status bar: after the title, before the icon */
#define COL_STEP     43u   /* three capsule columns: text at x = 2, 45, 88 */
#define COL_CHARS    10u   /* 4 px per character */

typedef struct {
    uint8_t magic;
    uint8_t fromUsb;   /* the keyer to go back to after CW Mem Edit */
    uint16_t nr;
} memcfg_t;

_Static_assert(sizeof(memcfg_t) <= 16u, "CW Mem config exceeds overlay slot");
_Static_assert(MEMU_MSG_STRIDE <= MSG_STRIDE, "custom messages fit the send buffer");
_Static_assert(MEM_USER == 0u, "MEMU_* offsets are asset offsets: the block starts the assets");

static struct {
    handoff_t h;          /* the keyer's settings, handed back on exit */
    memcfg_t c;
    char from[APP_NAME_LEN + 1u];
    char call[MEMU_CALL_MAX + 2u];
    bool fArm, toEdit, noEdit, handled;
    uint8_t candidate, touchPolls;
    uint32_t candidateAt;
} m;

/* The key or paddle of the sending app, or PTT, held for TOUCH_POLLS polls:
 * a single bouncing reading never hands back. */
static bool touched(uint8_t key)
{
    const bool closed = key == APP_KEY_PTT ||
        (m.c.fromUsb ? usbContacts() != 0u
                     : key == KEY_SIDE1 || key == KEY_SIDE2);
    m.touchPolls = closed ? (uint8_t)(m.touchPolls + 1u) : 0u;
    return m.touchPolls >= TOUCH_POLLS;
}

/* Waits ms while serving the radio. false when the message must stop: a
 * touch (which also hands back) or any key but the one that started it. */
static bool memWait(uint16_t ms, uint8_t startKey)
{
    uint32_t left = ms * g.ticksPerMs;
    ticksSince();
    for (;;) {
        const uint8_t key = A->get_key();
        if (touched(key)) {
            g.running = false;
            return false;
        }
        /* Codes below PTT are the keypad; PTT and SIDE are touches. */
        if (key < APP_KEY_PTT && key != startKey)
            return false;
        const uint32_t spent = ticksSince();
        if (spent >= left)
            return true;
        left -= spent;
        if (left > g.ticksPerMs)
            serviceOneMs();
    }
}

/* NR with three digits at least: 7 -> "007". */
static char *putNr(char *out)
{
    const uint16_t nr = m.c.nr;
    if (nr >= 1000u)
        return putu(out, nr);
    char *end = putu(out, nr + 1000u);
    out[0] = out[1];
    out[1] = out[2];
    out[2] = out[3];
    return end - 1;
}

/* Message i: 0 to 5 from the fixed table, 6 to 8 from the user block. */
static void readMessage(uint8_t i, char *tpl)
{
    A->asset_read(i < 6u ? (uint16_t)(MSG + i * MSG_STRIDE)
                         : (uint16_t)(MEMU_MSG + (i - 6u) * MEMU_MSG_STRIDE),
                  tpl, MSG_STRIDE);
    tpl[MSG_STRIDE - 1u] = '\0';
}

/* Expands message i (callsign, NR) and keys it at the keyer's WPM, with the
 * keyer's ramp trims: 1:1 mark/space at half drive, 3-dot letter gaps and
 * 7-dot word gaps. Message 4 (TU) ends a QSO: NR moves on once it is sent. */
static void sendMessage(uint8_t i, uint8_t key)
{
    char tpl[MSG_STRIDE], s[MSG_STRIDE * 4u];
    readMessage(i, tpl);
    char *out = s;
    for (const char *p = tpl; *p; p++) {
        if (*p == '\x01') {
            for (const char *q = m.call; *q; q++)
                *out++ = *q;
        } else if (*p == '\x02') {
            out = putNr(out);
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';

    const uint16_t dot = dotMs();
    bool ok = true;
    for (const char *p = s; *p && ok; p++) {
        if (*p == ' ') {
            ok = memWait((uint16_t)(4u * dot), key);
            continue;
        }
        uint8_t code = 0u;
        if (*p >= ',' && *p <= 'Z')
            A->asset_read((uint16_t)(MORSE + (*p - ',')), &code, 1u);
        if (!code)
            continue;
        uint8_t bit = 0x80u;
        while (!(code & bit))
            bit >>= 1;
        for (bit >>= 1; bit && ok; bit >>= 1) {
            if (!keyOn())
                return;   /* refused: TX OFF is shown, nothing more is sent */
            ok = memWait((uint16_t)((code & bit ? 3u * dot : dot) - RAMP_MS - 1u),
                         key);
            keyOff();
            if (ok)
                ok = memWait((uint16_t)(dot - RAMP_MS + 1u), key);
        }
        if (ok)
            ok = memWait((uint16_t)(2u * dot), key);
    }
    if (ok && i == 3u && m.c.nr < NR_MAX)
        m.c.nr++;
    g.redraw = true;
}

static void handleKey(uint8_t key)
{
    const int8_t direction = A->nav_dir(key);
    const bool f = m.fArm;
    m.fArm = false;
    if (g.denied || m.noEdit) {   /* TX OFF / NO EDIT stay until the next key */
        g.denied = false;
        m.noEdit = false;
        g.statusRedraw = true;
    }
    if (g.help) {
        if (key == APP_KEY_EXIT || key == APP_KEY_MENU) {
            g.help = false;
            g.redraw = true;
        }
        return;
    }
    if (direction != 0) {
        if (f) {
            /* F then UP / DOWN: the serial number, 1 to NR_MAX. */
            const uint16_t nr = (uint16_t)(m.c.nr + direction);
            if (nr >= 1u && nr <= NR_MAX)
                m.c.nr = nr;
            g.redraw = true;
        } else if (adjustFrequency(direction)) {
            g.redraw = true;
        }
        g.statusRedraw = true;
        return;
    }
    if (key >= APP_KEY_1 && key <= APP_KEY_9) {
        sendMessage((uint8_t)(key - APP_KEY_1), key);
        return;
    }
    switch (key) {
        case APP_KEY_0:
            /* CW Mem Edit hands back here; the keyer is remembered in the
             * configuration. Without the editor, say so and stay. */
            m.h.tune = g.tuneOffset10Hz;
            if (A->handoff(CW_MEM_EDIT_APP_NAME, &m.h, sizeof(m.h))) {
                m.toEdit = true;
                g.running = false;
            } else {
                m.noEdit = true;
            }
            break;
        case APP_KEY_F:
            m.fArm = !f;
            break;
        case APP_KEY_MENU:
            disarm();
            g.help = true;
            g.redraw = true;
            break;
        case APP_KEY_EXIT:
            g.running = false;
            break;
        default:
            break;
    }
    g.statusRedraw = true;
}

static void drawMemStatus(void)
{
    char s[TEXT_MAX + 4u];
    drawStatus(g.help ? T_HELP : g.denied ? T_DENIED
               : m.noEdit ? T_NOEDIT : T_TITLE, T_TITLE_CHARS);
    /* The keyer's speed, which CW Mem sends at. */
    putText(putu(s, g.wpm), T_WPM);
    A->print_tiny(s, WPM_X, 1u, true, true);   /* fill: set the pixels */
    if (m.fArm || (g.speaker && !g.help))
        A->asset_read(m.fArm ? BMP_F : BMP_SPEAKER, A->status_line + F_X,
                      BMP_SPEAKER_LEN);
}

static void drawMem(void)
{
    char s[TEXT_MAX + MSG_STRIDE];
    drawMemStatus();
    drawBigFrequency(s);
    drawSmeter();
    /* Rows and columns, not i / 3 and i % 3: no libgcc division. */
    uint8_t i = 0u;
    for (uint8_t line = 4u; line < 7u; line++) {
        for (uint8_t x = COL_X; x < COL_X + 3u * COL_STEP; x += COL_STEP, i++) {
            char *out = putText(s, (uint16_t)(T_LABEL + i * T_LABEL_STRIDE));
            if (i == 1u) {
                for (const char *q = m.call; *q; q++)
                    *out++ = *q;
                *out = '\0';
            } else if (i == 2u) {
                *putNr(out) = '\0';
            } else if (i >= 6u) {
                readMessage(i, out);   /* the capsule shows its first characters */
            }
            s[COL_CHARS] = '\0';
            A->print_inverse(s, x, line, false, true,
                             (uint8_t)(x + COL_CHARS * 4u - 1u));
        }
    }
}

/* MENU: the keys in two columns of capsules, as in the keyers. */
static void drawMemHelp(void)
{
    char t[TEXT_MAX];
    drawMemStatus();
    for (uint8_t i = 0u; i < 12u; i++) {
        if (text(t, (uint16_t)(T_HELP_KEYS + i * T_HELP_KEYS_STRIDE))[0])
            A->print_inverse(t, i < 6u ? TAG_LEFT_X : HELP_RIGHT_X,
                             (uint8_t)(i < 6u ? i + 1u : i - 5u), false, true,
                             i < 6u ? HELP_RIGHT_X - 4u : 127u);   /* full width */
    }
    A->print_tiny(text(t, T_HELP_STEP), 2u, 49u, false, true);
}

static void drawAll(void)
{
    A->display_clear();
    if (g.help)
        drawMemHelp();
    else
        drawMem();
    A->blit_status();
    A->blit_full();
    g.redraw = false;
    g.statusRedraw = false;
}

/* Opened from the menu: there are no settings to use and nowhere to go
 * back to, so only say how to open it. */
static void showStart(void)
{
    const app_api_t *const a = A;
    char t[TEXT_MAX];
    a->display_clear();
    drawStatus(T_TITLE, T_TITLE_CHARS);
    for (uint8_t i = 0u; i < 3u; i++)
        a->print_normal(text(t, (uint16_t)(T_START + i * T_START_STRIDE)),
                        0u, 127u, (uint8_t)(2u + i * 2u));
    a->blit_status();
    a->blit_full();
    while (a->get_key() != APP_KEY_EXIT)
        a->delay_ms(10u);
}

/* The user block's callsign, else the boot message's (LINE 1). */
static void loadCall(void)
{
    A->asset_read(MEMU_CALL, m.call, MEMU_CALL_MAX);
    m.call[MEMU_CALL_MAX] = '\0';
    if (!m.call[0] || m.call[0] == ' ')
        A->boot_callsign(m.call, sizeof(m.call));
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    if (api->handoff_read(&m.h, sizeof(m.h), m.from) != (int16_t)sizeof(m.h) ||
        m.h.cfg.magic != CFG_MAGIC) {
        showStart();
        return;
    }
    api->cfg_load((uint8_t *)&m.c, sizeof(m.c));
    if (m.c.magic != MEM_MAGIC || m.c.nr == 0u || m.c.nr > NR_MAX) {
        m.c.magic = MEM_MAGIC;
        m.c.nr = 1u;
    }
    /* From a keyer ("CW USB" or "CW Keyer"), remember which one; back from
     * CW Mem Edit ("CW Mem Edit"), go back to the remembered one. */
    if (m.from[3] == 'M') {
        const char *name = m.c.fromUsb ? "CW USB" : "CW Keyer";
        for (uint8_t i = 0u; (m.from[i] = name[i]) != '\0'; i++)
            ;
    } else {
        m.c.fromUsb = m.from[3] == 'U';
    }
    /* The keyer validated these when it loaded them. */
    g.wpm = m.h.cfg.wpm;
    g.calSteps = m.h.cfg.calSteps;
    g.rxPitchHz = m.h.cfg.rxPitchHz;
    g.breakInMs = m.h.cfg.breakInMs;
    g.speaker = m.h.cfg.speaker;
    g.tuneOffset10Hz = m.h.tune;
    loadCall();

    g.running = true;
    g.smDbm = SMETER_DBM_START;
    m.candidate = APP_KEY_INVALID;
    if (m.c.fromUsb)
        portSet(true);
    g.ticksPerMs = (uint32_t)api->uidivmod(SYST_LOAD + 1u, 10u);
    api->backlight_on();
    g.xtalBase = api->bk_read(REG_XTAL);
    api->bk_write(REG_XTAL, (uint16_t)(g.xtalBase + g.calSteps));
    configureCwRx(true);
    drawAll();

    while (g.running) {
        uint8_t key = A->get_key();
        if (key == APP_KEY_SAVER || key == APP_KEY_WAKE)
            key = APP_KEY_INVALID;
        if (touched(key))
            break;
        /* A key acts once, after UI_DEBOUNCE_MS of the same reading. */
        const uint32_t now = A->ticks_ms();
        if (key != m.candidate) {
            m.candidate = key;
            m.candidateAt = now;
            m.handled = false;
        } else if (!m.handled && key != APP_KEY_INVALID &&
                   (uint32_t)(now - m.candidateAt) >= UI_DEBOUNCE_MS) {
            m.handled = true;
            A->backlight_on();
            handleKey(key);
        }

        if (g.redraw && !g.keyDown) {
            drawAll();
        } else if (g.statusRedraw && !g.keyDown) {
            drawMemStatus();
            A->blit_status();
            g.statusRedraw = false;
        }
        serviceOneMs();
    }

    disarm();
    A->bk_write(REG_XTAL, g.xtalBase);
    portSet(false);
    A->cfg_save((const uint8_t *)&m.c, sizeof(m.c));
    m.h.tune = g.tuneOffset10Hz;
    if (!m.toEdit)
        A->handoff(m.from, &m.h, sizeof(m.h));
    A->set_af(APP_AF_MUTE);
    A->audio_path(false);
}
