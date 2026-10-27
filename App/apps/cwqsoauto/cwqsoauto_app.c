/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * USB-C paddle port and paddle keyer,
 * Copyright 2026 Johan Denoyer F4WAT
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

/* CW QSO AUTO, derived from CW USB 1.3 (a36dd121).
 * Original RF preparation, permission checks, PA shaping, receive path,
 * AUTO/A/B engine are retained locally: existing apps stay
 * unchanged. CQ expansion/edge scheduling is serviced in the main loop.
 * USB-C D+ (TIP/DIT) = PA12, D- (RING/DAH) = PA11, closed to GND.
 * The existing board contact-masking limitation after idle is not fixed here.
 * Synchronous PA ramps and RX-audio restoration still bound responsiveness.
 * v0.4: UP/DOWN select; digits shift RST; MENU opens separate CW settings.
 * PTT sends selection; SIDE1 repeats MSG01, SIDE2 stops after the current QSO.
 * UI simplifications: no help page/repeats, left-aligned frequency, calibration
 * in native REG_3B steps (not ppm). Radio controls and persistent layout remain.
 */

#define CW_KEYER_USB 1
#include "cwqsoauto_assets.h"
#include <stdbool.h>
#include <stdint.h>
#include "../app_api.h"

#ifndef CW_KEYER_USB
#define CW_KEYER_USB 0
#endif

/* Experimental engine only: 0 = original AUTO, 1 = shared AUTO/A/B engine.
 * USB key 8 selects the algorithm; the legacy 12-byte layout is retained.
 * Non-USB builds keep their original code and assets. */
#ifndef CW_USB_IAMBIC
#define CW_USB_IAMBIC 1
#endif
#if CW_USB_IAMBIC < 0 || CW_USB_IAMBIC > 1
#error "CW_USB_IAMBIC must be 0 (AUTO) or 1 (runtime AUTO/A/B)"
#endif
#define IAMBIC_ENGINE (CW_KEYER_USB && CW_USB_IAMBIC)

#if !CW_KEYER_USB
#include "cwkeyer_assets.h"
#endif

#define CFG_MAGIC          0xCCu   /* 0.4: crystal correction in REG_3B steps;
                                      0.2 used 0xCA, no longer converted */
#define WPM_DEFAULT        18u
#define WPM_MIN             5u
#define WPM_MAX            60u
#define CAL_DEFAULT        14   /* REG_3B steps: 14 x 0.1923 = +2.69 ppm */
#define CAL_MIN           -50
#define CAL_MAX            50
#define REG_XTAL         0x3Bu
#define RX_PITCH_DEFAULT  700u
#define RX_PITCH_MIN      300u
#define RX_PITCH_MAX      900u
#define RX_PITCH_STEP      50u
#define BREAKIN_DEFAULT   300u
#define BREAKIN_MIN       250u
#define BREAKIN_MAX      1500u
#define BREAKIN_STEP       50u
#define TUNE_STEP_10HZ      10
#define TUNE_MAX_10HZ     5000
#define UI_DEBOUNCE_MS      20u
#define RAMP_MS              4u
#define PLL_LOCK_MS           2u
#define MARK_MAX_MS      10000u
#define SERVICE_MS          10u
#define BATTERY_MS         500u
#if CW_KEYER_USB
_Static_assert(SERVICE_MS > 0u && SERVICE_MS <= UINT8_MAX && BATTERY_MS % SERVICE_MS == 0u &&
               BATTERY_MS / SERVICE_MS <= UINT8_MAX,
               "CW USB battery interval must fit in whole uint8_t ticks");
#endif
#define F_X                  72u
#define STATE_X              43u
#define TAG_LEFT_X            2u
#define TAG_RIGHT_X          66u
#define HELP_RIGHT_X         79u
#define DEBOUNCE_POLLS        3u   /* contacts: 5 identical polls, ~4.8 ms */
#define RX_SETTLE_MS         50u   /* RX return: REG_30 cycle to amplifier on */
#define AMP_SETTLE_MS        10u   /* TX: AF output settle before the amplifier */
#if CW_KEYER_USB
#define CONFIRM_CYCLES     9600u   /* paddle start: second read 200 us later */
#endif

_Static_assert(1200u / WPM_MAX > 2u * RAMP_MS,
               "CW mark must remain longer than both RF ramps");

#define REG_TX_LINK        0x30u
#define REG_AM_CTRL        0x31u
#define REG_PA_BIAS        0x36u
#define REG_FREQ_LOW       0x38u
#define REG_FREQ_HIGH      0x39u
#define REG_AUDIO_GAIN     0x48u
#define REG_SUBAUDIO       0x51u
#define REG_AFC            0x73u
#define REG_TONE           0x70u
#define REG_TONE_FREQ      0x71u
#define AF_ALAM              2u
#define AF_USB               5u
#define TX_LINK_OFF      0x0000u
#define TX_LINK_NO_MIC   0xC3FAu
#define TX_LINK_NO_PA    0xC3F2u
#define SIDETONE_GAIN        76u
#define TONE1_CONFIG      (0x8000u | (SIDETONE_GAIN << 8))
#define AUDIO_GAIN_BASE_MASK 0xFFF0u
#define AUDIO_DAC_GAIN_MAX     15u

/* These values are already returned by get_key(), but the app API only names
 * codes through PTT. They mirror KEY_SIDE2=17 and KEY_SIDE1=18. */
#define KEY_SIDE2          17u
#define KEY_SIDE1          18u
_Static_assert(KEY_SIDE2 == APP_KEY_PTT + 1u && KEY_SIDE1 == KEY_SIDE2 + 1u,
               "Local TX-key range must match the raw keyboard API");

enum { MODE_STRAIGHT, MODE_PADDLE };
#if CW_KEYER_USB
#define GPIOA_MODER (*(volatile uint32_t *)0x50000000u)
#define GPIOA_PUPDR (*(volatile uint32_t *)0x5000000Cu)
#define GPIOA_IDR   (*(volatile uint32_t *)0x50000010u)
#define RCC_APBENR1 (*(volatile uint32_t *)0x4002103Cu)
#define USBEN       (1u << 23)
#define PIN_DM      11u   /* PA11 USB D-, adapter ring  */
#define PIN_DP      12u   /* PA12 USB D+, adapter tip   */
#define USB_PINS    (0xFu << (2u * PIN_DM))   /* PA11/PA12 MODER and PUPDR */
#define USB_PULLUP  (0x5u << (2u * PIN_DM))
#endif
#define SYST_LOAD   (*(volatile uint32_t *)0xE000E014u)
#define SYST_VAL    (*(volatile uint32_t *)0xE000E018u)
#define P_DIT 1u
#define P_DAH 2u

typedef struct {
    uint8_t magic, wpm, mode, reverse;
    int16_t calSteps;
    uint16_t rxPitchHz, breakInMs;
    uint8_t speaker, reservedPort; /* USB: AUTO/A/B = 0/1/2; SIDE: zero. */
} config_t;

_Static_assert(sizeof(config_t) <= 16u, "CW Keyer config exceeds overlay slot");
#if IAMBIC_ENGINE
_Static_assert(sizeof(config_t) == 12u && __builtin_offsetof(config_t, reservedPort) == 11u,
               "CW USB legacy config layout must stay compatible");
#endif

/* The legacy config block leads; hot bytes follow, then halfwords and words.
 * Field order follows the Thumb-1 load/store immediate ranges: bytes first
 * (offsets up to 31), then halfwords (up to 62), then words (up to 124). A
 * field past its range costs an extra instruction at every access. */
static struct {
    /* Byte-addressable persistent block; keep the legacy 12-byte format. */
    uint8_t magic, wpm, mode, reverse;
    int16_t calSteps;
    uint16_t rxPitchHz, breakInMs;
    uint8_t speaker, reservedPort;
    bool running, txReady, keyDown, denied, fArm, redraw, statusRedraw;
    bool uiHandled;

#if CW_KEYER_USB
    bool portOpen;
    bool startupRelease; /* Entry-only held-input suppression; preserves hot offsets. */
#endif
    uint8_t uiCandidate, paBias, paControl;
    uint8_t padRaw, padStable, padPolls;
#if CW_KEYER_USB
    uint8_t ampMs;   /* amplifier on in that many service milliseconds */
#if IAMBIC_ENGINE
    uint8_t iambic; /* element bits 0/1, mark 2, pending 3, mode 4/5 */
#endif
#endif
    int16_t tuneOffset10Hz;
    uint16_t xtalBase;
#if CW_KEYER_USB
    uint8_t batteryTicks; /* Service intervals, 0..BATTERY_MS/SERVICE_MS-1. */
    uint8_t serviceMs; /* Bounded by SERVICE_MS (10), shares byte padding. */
    uint16_t markMs, hangElapsedMs;
#else
    uint16_t serviceMs, batteryMs, markMs, hangElapsedMs;
#endif
#if CW_KEYER_USB
    uint32_t moder, pupdr, apbenr1;   /* saved USB-port bits only */
#endif
    uint32_t ticksPerMs, lastVal;
#if CW_KEYER_USB
    /* Control release qualification (cycles) and UI debounce (ms) never
     * overlap: the release guard prevents entry to the UI handler. */
    uint32_t uiCandidateAt;
#else
    uint32_t uiCandidateAt, navRepeatAt;
#endif
#if CW_KEYER_USB
    uint32_t exitTicks;
#else
    uint32_t exitTicks;   /* EXIT read for this long in the keyer waits */
#endif
    const app_api_t *A;
} g;

/* uint8_t reverse/speaker safely accept arbitrary persisted bytes. All Flash
 * offsets and direct persistence accesses are checked against the C layout. */
_Static_assert(__builtin_offsetof(__typeof__(g), magic) == 0 &&
               __builtin_offsetof(config_t, magic) == 0, "Config layout: magic");
_Static_assert(__builtin_offsetof(__typeof__(g), wpm) == CFG_WPM_OFFSET &&
               __builtin_offsetof(config_t, wpm) == CFG_WPM_OFFSET, "Config layout: wpm");
_Static_assert(__builtin_offsetof(__typeof__(g), mode) == CFG_MODE_OFFSET &&
               __builtin_offsetof(config_t, mode) == CFG_MODE_OFFSET, "Config layout: mode");
_Static_assert(__builtin_offsetof(__typeof__(g), reverse) == CFG_REVERSE_OFFSET &&
               __builtin_offsetof(config_t, reverse) == CFG_REVERSE_OFFSET, "Config layout: reverse");
_Static_assert(__builtin_offsetof(__typeof__(g), calSteps) == CFG_CAL_OFFSET &&
               __builtin_offsetof(config_t, calSteps) == CFG_CAL_OFFSET, "Config layout: calSteps");
_Static_assert(__builtin_offsetof(__typeof__(g), rxPitchHz) == CFG_PITCH_OFFSET &&
               __builtin_offsetof(config_t, rxPitchHz) == CFG_PITCH_OFFSET, "Config layout: rxPitchHz");
_Static_assert(__builtin_offsetof(__typeof__(g), breakInMs) == CFG_BREAK_OFFSET &&
               __builtin_offsetof(config_t, breakInMs) == CFG_BREAK_OFFSET, "Config layout: breakInMs");
_Static_assert(__builtin_offsetof(__typeof__(g), speaker) == 10 &&
               __builtin_offsetof(config_t, speaker) == 10, "Config layout: speaker");
_Static_assert(__builtin_offsetof(__typeof__(g), reservedPort) == 11 &&
               __builtin_offsetof(config_t, reservedPort) == 11, "Config layout: reservedPort");

enum { G_API_OFFSET = __builtin_offsetof(__typeof__(g), A) };
#define A (g.A)

#if defined(__arm__)
#define API_LEAF __attribute__((naked, noipa, section(".text.api_leaf")))
/* Tail calls keep LR, SP and r4-r11 intact. Only argument/scratch registers
 * are touched. Basic asm only in naked functions; every ABI offset below is
 * checked against the C layouts. The address of g is resolved by the linker. */
#define API_DISP_cycles_48mhz "92"
#define API_DISP_asset_read "12"
#define API_TAIL_HIGH(field) __asm volatile ( \
    ".syntax unified\n ldr r3, =g\n ldr r3, [r3, #68]\n adds r3, #252\n" \
    "ldr r3, [r3, #" API_DISP_##field "]\n bx r3\n")
_Static_assert(G_API_OFFSET == 68u &&
               __builtin_offsetof(__typeof__(g), wpm) == 1u &&
               __builtin_offsetof(app_api_t, cycles_48mhz) == 344u &&
               __builtin_offsetof(app_api_t, asset_read) == 264u &&
               __builtin_offsetof(app_api_t, print_normal) == 60u &&
               __builtin_offsetof(app_api_t, bk_write) == 80u &&
               __builtin_offsetof(app_api_t, bk_read) == 76u &&
               __builtin_offsetof(app_api_t, uidivmod) == 272u,
               "Thumb API leaf offsets must match the C ABI");
#else
#define API_LEAF __attribute__((noinline))
#endif

API_LEAF
static uint32_t nowCycles(void)
{
#if defined(__arm__)
    API_TAIL_HIGH(cycles_48mhz);
#else
    return A->cycles_48mhz();
#endif
}

/* Fixed CALL/NAME are folded into the Flash templates by the generator.
 * RST_TOKEN expands the three editable digits without changing RF timing. */
static struct {
    uint32_t at; /* Morse / repeat deadline. Release qualification uses g. */
    uint8_t cursor, callAt, code;
    bool active, release, ready, releasing;
    uint8_t selected, message; /* QSO selection and current TX model are independent. */
    bool settings;
    char rst[4];
    bool repeat;          /* SIDE1 enabled; SIDE2 clears even during a mark. */
} q;

/* One Thumb stub shares the high-offset API function-pointer lookup. */
API_LEAF
static void readAsset(uint16_t offset __attribute__((unused)), void *buffer __attribute__((unused)), uint16_t length __attribute__((unused)))
{
#if defined(__arm__)
    API_TAIL_HIGH(asset_read);
#else
    A->asset_read(offset, buffer, length);
#endif
}

__attribute__((noinline))
static uint8_t assetByte(uint16_t offset)
{
    uint8_t c = 0;
    readAsset(offset, &c, 1u);
    return c;
}

_Static_assert(MSG_MODELS == 512u && MSG_STRIDE == 68u && MSG_BYTES == 64u &&
               MSG_COUNT == 16u && MSG_HEADER == 4u, "Message bank ABI");
_Static_assert(MORSE == '/' && MORSE_PAD_LEN == '/', "Zero-padded Morse ABI");

static uint8_t nextCharacter(void)
{
    for (;;) {
        uint8_t c = 0;
        if (q.cursor < MSG_BYTES)
            c = assetByte(MSG_MODELS + q.message * MSG_STRIDE + MSG_HEADER + q.cursor);
        if (c != RST_TOKEN) { q.cursor++; return c; }
        c = (uint8_t)q.rst[q.callAt++];
        if (c) return c;
        q.callAt = 0u;
        q.cursor++;
    }
}

/* Sentinel encoding, least significant bit first (0 dit / 1 dah). */
static uint8_t morse(uint8_t c)
{
    /* Bytes below '/' are the zero-filled MORSE_PAD, including NUL/space. */
    if (c > 'Z') return 0;
    return assetByte(MORSE + c - '/');
}



#if IAMBIC_ENGINE
enum { IAMBIC_AUTO = 0u, IAMBIC_A = 16u, IAMBIC_B = 32u };
/* Foreground only: callers pass AUTO/A/B. Busy requests are refused; retry
 * after RX. The UI now keeps every mode reachable without volatile state. */
static void setIambicMode(unsigned mode)
{
    if (!g.txReady && !(g.iambic & 3u))
        g.iambic = (uint8_t)mode;
}
#endif

__attribute__((noinline))
static char *text(char *buf, uint16_t offset)
{
    readAsset(offset, buf, TEXT_MAX);
    return buf;
}

/* Appends an asset text read straight into the output; returns its end,
 * which is already NUL-terminated. Needs TEXT_MAX bytes free at out. */
__attribute__((noinline))
static char *putText(char *out, uint16_t offset)
{
#if CW_KEYER_USB
    /* Share the read and its buffer-preserving return with text(). */
    out = text(out, offset);
#else
    readAsset(offset, out, TEXT_MAX);
#endif
    while (*out)
        out++;
    return out;
}


/* The crystal correction is a signed count of BK4829 REG_3B steps (5 Hz on
 * the 26 MHz reference, 0.1923 ppm each). Positive means the radio's measured
 * carrier is high: the chip is told its crystal is faster, which lowers every
 * RX and TX frequency in proportion. The display shows it in ppm. */

/* The VFO frequency moved by the UP/DOWN offset, in 10 Hz units. The
 * offset stays within +/-TUNE_MAX_10HZ (50 kHz) and the VFO never goes below
 * 18 MHz, so the sum can neither wrap below zero nor overflow. */
static uint32_t tunedFrequency(uint32_t frequency)
{
    return frequency + (uint32_t)(int32_t)g.tuneOffset10Hz;
}

API_LEAF
static void setAudioGain(uint16_t base __attribute__((unused)), uint8_t gain __attribute__((unused)))
{
#if defined(__arm__)
    __asm volatile (
        ".syntax unified\n orrs r1, r0\n movs r0, #72\n ldr r3, =g\n"
        "ldr r3, [r3, #68]\n ldr r3, [r3, #80]\n bx r3\n");
#else
    A->bk_write(REG_AUDIO_GAIN, (uint16_t)(base | gain));
#endif
}

static void fadeAudioGain(uint16_t base, bool up)
{
    for (uint8_t step = 1u; step <= AUDIO_DAC_GAIN_MAX; step++) {
        const uint8_t gain = up ? step : (uint8_t)(AUDIO_DAC_GAIN_MAX - step);
        setAudioGain(base, gain);
        A->delay_ms(1u);
    }
}

static void configureCwRx(bool enableAudio)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    uint32_t frequency = tunedFrequency(a->rx_freq());
    const uint32_t pitch = (uint32_t)a->uidivmod(g.rxPitchHz, 10u);
    if (frequency > pitch)
        frequency -= pitch;

    a->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    a->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));
    const uint16_t rxLink = a->bk_read(REG_TX_LINK);
    a->bk_write(REG_TX_LINK, TX_LINK_OFF);
    a->bk_write(REG_TX_LINK, rxLink);

    /* Use BK4829 AF output 5, the upper-sideband baseband route, as a CW
     * product detector. Tuning below the carrier produces the receive tone. */
    a->bk_write(REG_AM_CTRL, a->bk_read(REG_AM_CTRL) & 0xFFFEu);
    /* The RX level, filter and audio register / value pairs (RX_PROFILE in
     * gen_assets.py). */
    uint16_t rxProfile[RX_PROFILE_LEN / 4u][2];
    readAsset(RX_PROFILE, rxProfile, sizeof(rxProfile));
    for (uint8_t i = 0u; i < RX_PROFILE_LEN / 4u; i++)
        a->bk_write((uint8_t)rxProfile[i][0], rxProfile[i][1]);
    a->bk_write(REG_AFC, a->bk_read(REG_AFC) | 0x0010u);
    a->set_agc(true);
    a->set_af(AF_USB);
    const uint16_t gainBase = a->bk_read(REG_AUDIO_GAIN) & AUDIO_GAIN_BASE_MASK;
    setAudioGain(gainBase, enableAudio ? AUDIO_DAC_GAIN_MAX : 0u);
    /* CW must not use the FM squelch gate: its opening delay clips dits and
     * repeatedly chops the recovered tone at every carrier transition. */
    a->audio_path(enableAudio && g.speaker);
}

/* The amplifier goes off before anything moves the AF output: a gain fade
 * with the amplifier on steps the AF_ALAM DC level (a click), and so does
 * the REG_30 cycle in configureCwRx. */
static void speakerOff(uint16_t gainBase)
{
    A->audio_path(false);
    setAudioGain(gainBase, 0u);
}

/* After configureCwRx(false): the AF output re-centres (as in EPIRB 406),
 * then the amplifier comes back and the gain fades in. */
static void speakerOn(uint16_t gainBase)
{
    const app_api_t *const a = A;
    a->delay_ms(RX_SETTLE_MS);
    a->audio_path(true);
    a->delay_ms(20u);
    fadeAudioGain(gainBase, true);
}

API_LEAF
static uint16_t gainBase(void)
{
#if defined(__arm__)
    /* 8-byte aligned call frame; uint16_t bk_read returns zero-extended. */
    __asm volatile (
        ".syntax unified\n push {r4, lr}\n ldr r3, =g\n"
        "ldr r3, [r3, #68]\n ldr r3, [r3, #76]\n movs r0, #72\n"
        "blx r3\n movs r1, #15\n bics r0, r1\n pop {r4, pc}\n");
#else
    return A->bk_read(REG_AUDIO_GAIN) & AUDIO_GAIN_BASE_MASK;
#endif
}

static void returnToRx(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    const uint16_t base = gainBase();
#if CW_KEYER_USB
    if (g.speaker)
        a->audio_path(false);
    setAudioGain(base, 0u);
#else
    if (g.speaker)
        speakerOff(base);
    else
        setAudioGain(base, 0u);
#endif
    a->set_af(AF_USB);
    a->delay_ms(10u);
    a->tx_end();
    g.txReady = false;
#if CW_KEYER_USB
    g.ampMs = 0u;
#endif
    g.hangElapsedMs = 0u;
    configureCwRx(false);
    if (g.speaker)
        speakerOn(base);
    else
        setAudioGain(base, AUDIO_DAC_GAIN_MAX);
    g.statusRedraw = true;
}

/* A receive retune (pitch, crystal, offset) with the same muting. */
static void retuneRx(void)
{
    if (g.txReady) {
        returnToRx();
        return;
    }
    if (!g.speaker) {
        configureCwRx(true);
        return;
    }
    const uint16_t base = gainBase();
    speakerOff(base);
    configureCwRx(false);
    speakerOn(base);
}

API_LEAF
static uint16_t dotMs(void)
{
#if defined(__arm__)
    /* Validated WPM is 5..60: resident quotient is already zero-extended
     * and <=240, satisfying the uint16_t return ABI without a truncation. */
    __asm volatile (
        ".syntax unified\n ldr r3, =g\n ldrb r1, [r3, #1]\n"
        "ldr r3, [r3, #68]\n adds r3, #252\n"
        "ldr r3, [r3, #20]\n movs r0, #75\n lsls r0, r0, #4\n bx r3\n");
#else
    return (uint16_t)(uint32_t)A->uidivmod(1200u, g.wpm);
#endif
}

static void drawStatus(uint16_t title, uint8_t titleChars)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    a->status_clear();
    char t[TEXT_MAX];
    text(t, title);
    if (*t)
        a->print_inverse(t, 2u, 0u, true, true,
                         (uint8_t)(2u + titleChars * 4u));
    a->draw_battery();
}

API_LEAF
static void normalText(const char *s __attribute__((unused)), uint8_t x __attribute__((unused)), uint8_t line __attribute__((unused)))
{
#if defined(__arm__)
    __asm volatile (
        ".syntax unified\n mov r3, r2\n ldr r2, =g\n"
        "ldr r2, [r2, #68]\n ldr r2, [r2, #60]\n"
        "mov ip, r2\n movs r2, #0\n bx ip\n");
#else
    A->print_normal(s, x, 0u, line);
#endif
}

/* Constant labels share both asset loading and the normal-font API call. */
__attribute__((noinline))
static void label(uint16_t offset, uint8_t line)
{
    char s[TEXT_MAX];
    normalText(text(s, offset), 2u, line);
}

/* VFO units are 10 Hz. Append the last zero as text: exact Hz without
 * multiplication overflow, software division or a second numeric buffer. */
static void drawSmallFrequency(char *s)
{
    char *end = A->append_u32(s, tunedFrequency(A->tx_freq()));
    readAsset(T_FREQ_TAIL, end, T_FREQ_TAIL_LEN);
    normalText(s, 2u, 0u);
}

static void drawMainStatus(void)
{
    const uint16_t state = q.active ? T_TX + q.message * T_TX_STRIDE :
        g.denied ? T_DENIED : T_IDLE;
    drawStatus(q.settings ? (g.fArm ? T_SETTINGS_F : T_SETTINGS) : state, T_TITLE_CHARS);
}

static void drawMain(void)
{
    char s[TEXT_MAX];
    drawMainStatus();

    drawSmallFrequency(s);
    if (!q.settings) {
        label(T_MESSAGES + q.selected * T_MESSAGES_STRIDE, 2u);
        if (q.selected >= 3u) {
            /* Preview only: never touch the stored record or TX cursors.
             * '*' denotes the one-byte RST token; '>' marks hidden text. */
            readAsset(MSG_MODELS + q.selected * MSG_STRIDE + MSG_HEADER, s, TEXT_MAX);
            for (int i = TEXT_MAX - 2; i >= 0; i--)
                if (s[i] == RST_TOKEN) s[i] = '*';
            if (s[TEXT_MAX - 1u]) s[TEXT_MAX - 2u] = '>';
            s[TEXT_MAX - 1u] = 0;
            normalText(s, 2u, 3u);
            A->print_tiny(text(s, T_PREVIEW_HELP), 2u, 49u, false, true);
        }
        /* Selection on line 2, preview (04..16) on line 3, RST on line 4.
         * The bottom-right MENU hint is separate from the message and RST. */
        label(T_RST, 4u);
        normalText(q.rst, 30u, 4u);
        A->print_tiny(text(s, T_MENU), 110u, 49u, false, true);
        return;
    }
    /* Descriptors use the checked layout above. Halfword offsets are aligned
     * and address int16_t or its permitted unsigned counterpart. */
    /* STRAIGHT omits descriptor 7; all capsules occupy disjoint lines/columns. */
    for (int i = 6 + g.mode; i >= 0; i--) {
        uint8_t field[6];
        readAsset(CONFIG_FIELDS + 6u * i, field, sizeof(field));
        if (i < 6) {
            const uint8_t *valueAt = (const uint8_t *)&g + (field[2] & 127u);
            const int16_t value = field[2] & 128u ? *(const int16_t *)valueAt : *valueAt;
            char *end = putText(s, field[0] + value * field[1]);
            if (!field[1]) end = A->append_i32(end, value);
            *end = 0;
        } else {
            unsigned offset = T_TUNE;
            if (i == 7) {
                offset = T_VERSION + T_VERSION_STRIDE + (g.iambic & 48u);
                field[5] = (g.iambic & 48u) ? 35u : 27u;
            }
            text(s, offset);
        }
        A->print_inverse(s, field[3], field[4], false, true, field[5]);
    }
}

static void draw(void)
{
    A->display_clear();
    drawMain();
    A->blit_status();
    A->blit_full();
    g.redraw = false;
    g.statusRedraw = false;
}

static void redrawMainStatus(void)
{
    drawMainStatus();
    A->blit_status();
    g.statusRedraw = false;
}

static void writeBias(uint8_t bias)
{
    A->bk_write(REG_PA_BIAS, (uint16_t)((uint16_t)bias << 8) | g.paControl);
}

static uint16_t toneControlWord(uint16_t frequency)
{
    return (uint16_t)((frequency * 1353245u + (1u << 16)) >> 17);
}

static void sidetone(bool on)
{
    A->bk_write(REG_TONE_FREQ, on ? toneControlWord(g.rxPitchHz) : 0u);
}

#if IAMBIC_ENGINE
static void rememberPaddle(void);
#endif

/* PA bias in four 1 ms steps, 1/4, 1/2, 3/4 and full drive going up, the
 * same levels the other way down to 0. */
static void rampBias(bool up)
{
    const uint8_t quarter = (uint8_t)(g.paBias >> 2);
    const uint8_t level[5] = { 0u, quarter, (uint8_t)(g.paBias >> 1),
                               (uint8_t)(g.paBias - quarter), g.paBias };
    for (uint8_t i = 1u; i <= 4u; i++) {
        writeBias(level[up ? i : 4u - i]);
        A->delay_ms(1u);
#if IAMBIC_ENGINE
        rememberPaddle();
#endif
    }
}

static void rampUp(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    a->bk_write(REG_TX_LINK, TX_LINK_NO_MIC);
    a->tx_carrier(true);
    rampBias(true);
}

static void rampDown(void)
{
    rampBias(false);
    A->tx_carrier(false);
    A->bk_write(REG_TX_LINK, TX_LINK_NO_PA);
}

#if CW_KEYER_USB
static uint32_t irqSave(void)
{
    uint32_t primask;
    __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
    return primask;
}

static void irqRestore(uint32_t primask)
{
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

/* open: pull-up, then input mode, then USB clock off, as validated on the bench
 * with Key Probe 0.3; close puts the saved bits back. Read-modify-write of the
 * named bits only, IRQs masked. */
static void portSet(bool open)
{
    if (open == g.portOpen)
        return;
    const uint32_t irq = irqSave();
    if (open) {
        g.moder = GPIOA_MODER & USB_PINS;
        g.pupdr = GPIOA_PUPDR & USB_PINS;
        g.apbenr1 = RCC_APBENR1 & USBEN;
    }
    GPIOA_PUPDR = (GPIOA_PUPDR & ~USB_PINS) | (open ? USB_PULLUP : g.pupdr);
    GPIOA_MODER = (GPIOA_MODER & ~USB_PINS) | (open ? 0u : g.moder);
    RCC_APBENR1 = (RCC_APBENR1 & ~USBEN) | (open ? 0u : g.apbenr1);
    irqRestore(irq);
    g.portOpen = open;
}
#endif

/* CPU clock ticks since the previous call, from the 10 ms SysTick down-counter.
 * Valid while calls are less than 10 ms apart. */
static uint32_t ticksSince(void)
{
    const uint32_t now = SYST_VAL;
    const uint32_t prev = g.lastVal;
    g.lastVal = now;
    return prev >= now ? prev - now : prev + SYST_LOAD + 1u - now;
}

#if CW_KEYER_USB
__attribute__((noinline))
static uint8_t usbContacts(void)
{
    const uint32_t pins = ~GPIOA_IDR;
    return (uint8_t)(((pins >> PIN_DP) & 1u) | ((pins >> (PIN_DM - 1u)) & 2u));
}


#endif

/* Both contacts are sampled together, so a squeeze is visible. */
static uint8_t paddles(uint8_t key)
{
    uint8_t p = 0u;
#if CW_KEYER_USB
    (void)key;
    p = usbContacts();
#else
    if (key == KEY_SIDE1) {
        p = P_DIT;
    } else if (key == KEY_SIDE2) {
        p = P_DAH;
    }
#endif
    /* In the USB variant a straight key uses the tip only, so a mono plug
     * works. In the SIDE variant both keys remain parallel straight keys. */
    if (g.mode == MODE_STRAIGHT) {
#if CW_KEYER_USB
        return (uint8_t)(p & P_DIT);
#else
        return (uint8_t)(p != 0u);
#endif
    }
    return g.reverse ? (uint8_t)(((p & 1u) << 1) | (p >> 1)) : p;
}

/* A contact change reaches the straight-key and keyer-start decisions once it
 * has read the same on DEBOUNCE_POLLS + 2 consecutive polls: the change, the
 * DEBOUNCE_POLLS confirmations, then the poll that latches it. At one poll
 * per ~1.2 ms (keypad scan plus the 1 ms wait), that is about 4.8 ms. */
static uint8_t debounced(uint8_t p)
{
    if (p != g.padRaw) {
        g.padRaw = p;
        g.padPolls = 0u;
    } else if (g.padPolls < DEBOUNCE_POLLS) {
        g.padPolls++;
    } else {
        g.padStable = p;
    }
    return g.padStable;
}

#if IAMBIC_ENGINE
/* Two equal observations qualify active memory; starts retain the existing
 * USB confirmation. A remembers isolated opposite taps. B additionally
 * remembers a squeeze during the mark, including both RF ramps. A squeeze
 * appearing only in the space does not queue a tail. No active element means
 * no memory, so PTT and startup-held contacts cannot seed the next sequence. */
static void rememberPaddle(void)
{
    const uint8_t state = g.iambic;
    if (state < IAMBIC_A || !(state & 3u))
        return;
    const uint8_t p = paddles(APP_KEY_INVALID);
    if (p != g.padRaw) {
        g.padRaw = p;
        return;
    }
    if (p == ((state & 3u) ^ 3u) ||
        (p == 3u && (state & 36u) == 36u))
        g.iambic = state | 8u;
}
#endif

/* Called by keyOn only, after tx_state has allowed the transmission. */
static void prepareTx(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    a->audio_path(false);
    /* tx_prepare (API level 3, table of 368 B) leaves PA_ENABLE off and skips
     * the FM waits; tx_set_params returns with RF enabled after a 10 ms
     * unshaped burst. Either way, remove PA drive, apply the calibrated
     * frequency, then start every mark with the same controlled ramp.
     * REG_38/39 use 10 Hz synthesizer units. */
#if CW_KEYER_USB
    /* app_main refuses API tables shorter than 368 before any keying.
     * USB never uses the legacy preparation that emits an unshaped burst. */
    a->tx_prepare();
#else
    if (a->api_size >= 368u)
        a->tx_prepare();
    else
        a->tx_set_params();
#endif
    const uint16_t pa = a->bk_read(REG_PA_BIAS);
    g.paBias = (uint8_t)(pa >> 8);
    g.paControl = (uint8_t)pa;

    a->tx_carrier(false);
    writeBias(0u);

    /* REG_50 mutes modulation, not the RF carrier. Keep it muted for the whole
     * keying session, remove every modulation source, and disable the mic ADC.
     * RF is keyed with internal PA gain, external PA bias and PA_ENABLE. */
    a->tx_mute(true);
    a->bk_write(REG_SUBAUDIO, 0u);
    a->bk_write(REG_TONE, 0u);
    const uint32_t frequency = tunedFrequency(a->tx_freq());
    a->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    a->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));

    /* REG_38/39 are written after the TX setup, so restart the TX link to
     * recalibrate the VCO and latch the corrected synthesizer frequency. Keep
     * internal PA gain disabled when the link comes back up. */
    a->bk_write(REG_TX_LINK, TX_LINK_OFF);
    a->bk_write(REG_TX_LINK, TX_LINK_NO_PA);

    /* AF_ALAM is the BK4829's local alarm/tone output. Unlike AF_BEEP, it
     * remains audible while REG_50 keeps the TX modulation path muted. Keep
     * Tone1 enabled and key it through REG_71; rewriting REG_70 on every Morse
     * edge produces an audible pop. */
    a->bk_write(REG_TONE, TONE1_CONFIG);
    sidetone(false);
    a->set_af(AF_ALAM);
#if CW_KEYER_USB
    /* Experimental: wake HP with Tone1 at zero frequency during the
     * existing PLL wait. No extra wait; analog clicks remain unqualified. */
    g.ampMs = 0u;
    a->audio_path(g.speaker);
#else
    /* CW Keyer has no arming interval. Keep the tone and RF off until the AF
     * output has settled, then enable the amplifier before the first mark so
     * its local sidetone is not clipped. */
    if (g.speaker)
        a->delay_ms(AMP_SETTLE_MS);
    a->audio_path(g.speaker);
#endif
    a->delay_ms(PLL_LOCK_MS);
    g.txReady = true;
    g.denied = false;
    g.statusRedraw = true;
}

/* tx_state is asked before every mark: a refusal ends a prepared session,
 * or leaves the receiver as it is when none was prepared. */
static bool keyOn(void)
{
    if (A->tx_state() != 0u) {
        if (g.txReady)
            returnToRx();
        else
            g.redraw = true;
        g.denied = true;
        return false;
    }
    if (!g.txReady)
        prepareTx();
    sidetone(true);
    rampUp();
    g.keyDown = true;
    g.markMs = 0u;
    g.hangElapsedMs = 0u;
    return true;
}

static void keyOff(void)
{
    rampDown();
    sidetone(false);
    g.keyDown = false;
}

static void disarm(void)
{
    if (g.keyDown)
        keyOff();
    if (g.txReady)
        returnToRx();
    g.denied = false;
    g.statusRedraw = true;
}

/* No queued manual request is allowed to cross an automatic stop. */
static void stopAuto(void)
{
    q.active = q.ready = q.releasing = false;
    q.release = true;
    g.iambic &= 48u;
    g.padRaw = g.padStable = g.padPolls = 0u;
    disarm();
    g.redraw = true;
}

/* PTT is consumed once. A fresh, stable full release is required again
 * after completion/refusal/cancellation; no request is queued during CQ. */
static void startAuto(void)
{
    q.cursor = q.callAt = 0u;
    q.code = morse(nextCharacter());
    q.release = true;
    q.active = q.code != 0u;
    q.repeat &= q.active; /* No repeated launch of an empty/unsupported model. */
    q.ready = q.releasing = false;
    q.at = nowCycles();
    g.redraw = true;
}

/* One edge at most per foreground pass; no whole-message wait. The same
 * keyOn/keyOff protections, PA ramps and silent preparation serve manual TX.
 * Deadlines start AFTER synchronous callbacks, never before TX preparation. */
static void serviceAuto(void)
{
    if (!app_cycles_reached(nowCycles(), q.at)) return;
    const uint16_t dot = dotMs();
    unsigned duration;
    if (!g.keyDown) {
        duration = (q.code & 1u ? 3u : 1u) * dot - RAMP_MS - 1u;
        if (!keyOn()) { { q.repeat = false; stopAuto(); } return; }
        q.code >>= 1;
    } else {
        keyOff();
        duration = dot - RAMP_MS + 1u;
        if (q.code == 1u) {
            uint8_t c = nextCharacter();
            if (c == ' ') { duration += 4u * dot; c = nextCharacter(); }
            q.code = morse(c);
            duration = q.code ? duration + 2u * dot : 3000u;
        }
    }
    q.at = nowCycles() + duration * 48000u;
    if (!q.code) stopAuto();
}

static void serviceOneMs(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    a->delay_ms(1u);
#if CW_KEYER_USB
    if (g.ampMs && --g.ampMs == 0u)
        a->audio_path(g.speaker);
    const uint16_t hangMs = g.breakInMs;
#else
    const uint16_t hangMs = g.breakInMs;
#endif
    /* hangElapsedMs needs no reset here: keyOn() zeroes it and it only counts
     * while the key is up. */
    if (g.keyDown && ++g.markMs >= MARK_MAX_MS) {
        keyOff();
        if (g.txReady)
            returnToRx();
        g.denied = true;
        g.statusRedraw = true;
    }
    if (!q.active && g.txReady && !g.keyDown && ++g.hangElapsedMs >= hangMs) {
        returnToRx();
    }
    if (++g.serviceMs >= SERVICE_MS) {
        g.serviceMs = 0u;
        a->backlight_update();
#if CW_KEYER_USB
        if (++g.batteryTicks >= BATTERY_MS / SERVICE_MS) {
            g.batteryTicks = 0u;
#else
        g.batteryMs = (uint16_t)(g.batteryMs + SERVICE_MS);
        if (g.batteryMs >= BATTERY_MS) {
            g.batteryMs = 0u;
#endif
            if (!g.txReady) {
                a->battery_sample();
                g.statusRedraw = true;
            }
            /* A sleeping overlay consumes the first SIDE press as a wake key.
             * Keep the keyer awake so a dit or dah is never discarded. */
            a->backlight_on();
        }
    }
}

/* Element wait. AUTO looks at paddles after the space; experimental A/B
 * also observes them during waits. EXIT aborts once read for UI_DEBOUNCE_MS of
 * waiting: while a SIDE contact bounces, the keypad scan can miss the zero
 * column and report another key of the same row (SIDE1: MENU, UP, DOWN or
 * EXIT; SIDE2: 1, 2, 3 or STAR), and a single such reading must not end
 * the app. */
static bool keyerWait(uint16_t ms)
{
    uint32_t left = ms * g.ticksPerMs;
    ticksSince();
    for (;;) {
        const uint8_t key = A->get_key();
        /* A PTT/SIDE seen during manual keying is consumed, never deferred to
         * the end of a held/squeezed paddle run. */
        if (key >= APP_KEY_PTT && key <= KEY_SIDE1) {
            q.ready = q.releasing = false;
            q.release = true;
        }
#if IAMBIC_ENGINE
        rememberPaddle();
#endif
        const uint32_t spent = ticksSince();
        if (key != APP_KEY_EXIT) {
            g.exitTicks = 0u;
        } else if ((g.exitTicks += spent) >= UI_DEBOUNCE_MS * g.ticksPerMs) {
            g.running = false;
            return false;
        }
        if (spent >= left)
            return g.running;
        left -= spent;
        /* Poll without sleeping for the last millisecond: overshoot stays
         * below one keypad scan instead of a whole 1 ms step. */
        if (left > g.ticksPerMs)
            serviceOneMs();
    }
}

static void runPaddle(uint8_t p)
{
#if CW_KEYER_USB
    uint8_t last = 0u;
#endif
    g.exitTicks = 0u;
    while (p) {
#if CW_KEYER_USB
        const uint8_t el = p != (P_DIT | P_DAH) ? p : last == P_DIT ? P_DAH : P_DIT;
#else
        const uint8_t el = p;
#endif
#if IAMBIC_ENGINE
        g.iambic = (g.iambic & 48u) | el | 4u;
#endif
        if (!keyOn())
            break;
        const uint16_t dot = dotMs();
        /* The PA bias ramps take RAMP_MS at each edge and cross half drive
         * about one millisecond off centre, so the waits are trimmed to give a
         * 1:1 mark/space at the half-drive points and an exact 2-dot period. */
        const bool done = keyerWait((uint16_t)((el == P_DAH ? 3u * dot : dot) - RAMP_MS - 1u));
        keyOff();
#if IAMBIC_ENGINE
        g.iambic &= (uint8_t)~4u;
#endif
        if (!done || !keyerWait((uint16_t)(dot - RAMP_MS + 1u)))
            break;
#if CW_KEYER_USB
        last = el;
#endif
#if IAMBIC_ENGINE
        p = g.iambic & 8u ? (el ^ 3u) : paddles(A->get_key());
#else
        p = paddles(A->get_key());
#endif
    }
#if IAMBIC_ENGINE
    g.iambic &= 48u;
#endif
}

/* One step up or down, refused when it would leave [lo, hi]: the bounded
 * settings and the frequency offset all follow this rule. */
static bool stepWithin(int16_t *value, int8_t direction, int16_t step,
                       int16_t lo, int16_t hi)
{
#if CW_KEYER_USB
    /* All callers stay in [-5010, 5010]; native width avoids narrowing
     * before the range check. The stored setting remains int16_t. */
    const int32_t next = (int32_t)*value + direction * step;
#else
    const int16_t next = (int16_t)(*value + direction * step);
#endif
    if (next < lo || next > hi)
        return false;
    *value = (int16_t)next; /* The checked bounds are int16_t. */
    return true;
}

static bool adjustFrequency(int8_t direction)
{
    if (!stepWithin(&g.tuneOffset10Hz, direction, TUNE_STEP_10HZ,
                    -TUNE_MAX_10HZ, TUNE_MAX_10HZ))
        return false;

    retuneRx();
    g.denied = false;
    return true;
}

static void adjustSetting(uint8_t key, int8_t direction)
{
    bool retune = false;
    switch (key) {
        case APP_KEY_0:
            g.speaker = !g.speaker;
            A->audio_path(g.speaker);
            break;
        case APP_KEY_1:
            g.mode = (uint8_t)(g.mode ^ 1u);
            break;
        case APP_KEY_2: {
            int16_t wpm = g.wpm;
            if (!stepWithin(&wpm, direction, 1, WPM_MIN, WPM_MAX))
                return;
            g.wpm = (uint8_t)wpm;
            break; }
        case APP_KEY_3:
            if (!stepWithin((int16_t *)&g.rxPitchHz, direction, RX_PITCH_STEP,
                            RX_PITCH_MIN, RX_PITCH_MAX))
                return;
            retune = true;
            break;
        case APP_KEY_4:
            if (!stepWithin((int16_t *)&g.breakInMs, direction, BREAKIN_STEP,
                            BREAKIN_MIN, BREAKIN_MAX))
                return;
            break;
        case APP_KEY_5:
            g.reverse = !g.reverse;
            break;
        case APP_KEY_6:
            if (!stepWithin(&g.calSteps, direction, 1, CAL_MIN, CAL_MAX))
                return;
            A->bk_write(REG_XTAL, (uint16_t)(g.xtalBase + g.calSteps));
            retune = true;
            break;
        default:
            return;
    }
    if (retune)
        retuneRx();
    g.redraw = true;
}

#if CW_KEYER_USB
_Static_assert(APP_KEY_DOWN == APP_KEY_UP + 1u, "QSO navigation key order");
static void handleUiKeyDirection(uint8_t key, int8_t direction)
#else
static void handleUiKey(uint8_t key)
#endif
{
    if (key == APP_KEY_MENU) {
        q.settings = !q.settings;
    } else if (key == APP_KEY_EXIT) {
        if (q.settings) q.settings = false;
        else g.running = false;
    } else if (direction) {
        if (q.settings) adjustFrequency(direction);
        else {
            /* nav_dir identifies UP/DOWN; use their physical order for QSO. */
            const unsigned next = q.selected + 2u * (key - APP_KEY_UP) - 1u;
            q.selected = (uint8_t)(next & (MSG_COUNT - 1u));
        }
    } else if (!q.settings) {
        if (key <= APP_KEY_9) {
            q.rst[0] = q.rst[1]; q.rst[1] = q.rst[2];
            q.rst[2] = (char)('0' + key);
        }
    } else {
        if (key == APP_KEY_8)
            setIambicMode(g.iambic < IAMBIC_B ? g.iambic + 16u : IAMBIC_AUTO);
        else if (key == APP_KEY_F) {
            g.fArm = !g.fArm;
            g.statusRedraw = true;
            return;
        } else adjustSetting(key, g.fArm ? -1 : 1);
    }
    g.fArm = false;
    g.redraw = true;
}

#if CW_KEYER_USB
static inline void handleUiKey(uint8_t key)
{
    handleUiKeyDirection(key, A->nav_dir(key));
}
#endif

static void resetUiInput(void)
{
    g.uiCandidate = APP_KEY_INVALID;
#if !CW_KEYER_USB
    g.uiHandled = false;
#endif
#if !CW_KEYER_USB
    g.navRepeatAt = 0u;
#endif
}

static void handleUiInput(uint8_t key)
{
    const uint32_t now = A->ticks_ms();
    if (key != g.uiCandidate) {
        g.uiCandidate = key;
        g.uiCandidateAt = now;
        g.uiHandled = false;
    #if !CW_KEYER_USB
    g.navRepeatAt = 0u;
#endif
        return;
    }
    if (!g.uiHandled) {
        if ((uint32_t)(now - g.uiCandidateAt) < UI_DEBOUNCE_MS)
            return;
        A->backlight_on();
#if CW_KEYER_USB
        handleUiKeyDirection(key, A->nav_dir(key));
#else
        handleUiKey(key);
#endif
        g.uiHandled = true;
        return;
    }
 }

static void loadConfig(void)
{
    A->cfg_load((uint8_t *)&g + __builtin_offsetof(__typeof__(g), magic), sizeof(config_t));
    if (g.magic != CFG_MAGIC) {
        readAsset(DEFAULT_CONFIG, (uint8_t *)&g + __builtin_offsetof(__typeof__(g), magic), sizeof(config_t));
        g.iambic = IAMBIC_AUTO;
        return;
    }
    /* Preserve the grouped legacy validity rule for WPM/mode/reverse/cal. */
    if (g.wpm >= WPM_MIN && g.wpm <= WPM_MAX &&
        g.mode <= MODE_PADDLE && g.reverse <= 1u) {
        g.calSteps = g.calSteps >= CAL_MIN && g.calSteps <= CAL_MAX
            ? g.calSteps : CAL_DEFAULT;
    } else {
        g.wpm = WPM_DEFAULT;
        g.mode = MODE_STRAIGHT;
        g.reverse = false;
        g.calSteps = CAL_DEFAULT;
    }
    g.rxPitchHz = g.rxPitchHz >= RX_PITCH_MIN && g.rxPitchHz <= RX_PITCH_MAX
        ? g.rxPitchHz : RX_PITCH_DEFAULT;
    g.breakInMs = g.breakInMs >= BREAKIN_MIN && g.breakInMs <= BREAKIN_MAX
        ? g.breakInMs : BREAKIN_DEFAULT;
    g.speaker = g.magic == CFG_MAGIC && g.speaker <= 1u
        ? g.speaker : true;
#if IAMBIC_ENGINE
    /* Only app entry calls loadConfig, before TX or any element. Older
     * USB configs wrote zero here; absent/invalid bytes select AUTO. */
    g.iambic = g.magic == CFG_MAGIC && g.reservedPort <= 2u
        ? (uint8_t)(g.reservedPort << 4) : IAMBIC_AUTO;
#endif
}

static void saveConfig(void)
{
    g.magic = CFG_MAGIC;
    g.reservedPort = (uint8_t)(g.iambic >> 4);
    A->cfg_save((const uint8_t *)&g + __builtin_offsetof(__typeof__(g), magic), sizeof(config_t));
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    if (api->api_size < 368u) return; /* cycles_48mhz and silent tx_prepare required. */
    A = api;
    q.repeat = false;
    q.active = q.ready = q.releasing = false;
    q.release = true; /* Consume all held entry controls before accepting input. */
    g.startupRelease = true;
    g.padRaw = g.padStable = g.padPolls = 0u;
    q.selected = q.message = 0u;
    q.settings = false;
    q.rst[0] = '5'; q.rst[1] = q.rst[2] = '9'; q.rst[3] = 0;
    g.running = true;
    g.redraw = true;
    g.statusRedraw = false;
    resetUiInput();
    loadConfig();
#if CW_KEYER_USB
    portSet(true);
#endif
    g.ticksPerMs = (uint32_t)A->uidivmod(SYST_LOAD + 1u, 10u);
    A->backlight_on();
    g.xtalBase = A->bk_read(REG_XTAL);
    A->bk_write(REG_XTAL, (uint16_t)(g.xtalBase + g.calSteps));
    configureCwRx(true);
    draw();

    while (g.running) {
        uint8_t key = A->get_key();
        const uint8_t contacts = usbContacts();
        if (key == KEY_SIDE2) q.repeat = false;
        if (q.active || q.repeat) {
            if (key == APP_KEY_EXIT || contacts ||
                g.denied) { q.repeat = false; stopAuto(); }
            else if (q.active) serviceAuto();
            else if (app_cycles_reached(nowCycles(), q.at)) goto repeatLaunch;
            else goto idleInput;
            if (!q.active && g.redraw) draw();
            goto service;
        }
idleInput:
        /* 20 ms stable release prevents a bounce or a held/queued PTT
         * from triggering another CQ. Only real idle (not WAKE) qualifies. */
        if (!q.ready) {
            if (key == APP_KEY_INVALID &&
                !(g.mode == MODE_STRAIGHT ? contacts & P_DIT : contacts)) {
                const uint32_t now = nowCycles();
                if (!q.releasing) {
                    g.uiCandidateAt = now;
                    q.releasing = true;
                } else if (now - g.uiCandidateAt >= 960000u) {
                    q.ready = true;
                    q.release = false;
                    g.startupRelease = false;
                }
            } else q.releasing = false;
        }
        if (q.release) {
            if (g.startupRelease && key == APP_KEY_EXIT) handleUiInput(key);
            else resetUiInput();
            goto service;
        }
        if (key == KEY_SIDE1 && q.repeat) {
            goto txKeyDone;
        }
        if (key >= APP_KEY_PTT && key <= KEY_SIDE1) {
            /* No manual PTT/SIDE carrier on overlap.
             * A TX key overlapping a paddle cannot queue an automatic start. */
            const bool launch = key != KEY_SIDE2 && q.ready && !contacts && !g.keyDown && !q.settings;
            { q.repeat = false; stopAuto(); } /* Also discard stale filtered manual contacts. */
            q.release = true;
            if (launch) {
                q.repeat = key == KEY_SIDE1;
repeatLaunch:
                q.message = q.repeat ? 0u : q.selected;
                startAuto();
            }
txKeyDone:
            resetUiInput();
            if (g.redraw) draw();
            goto service;
        }
        if (key == APP_KEY_SAVER) {
            resetUiInput();
            goto service;
        }
        if (key == APP_KEY_WAKE)
            key = APP_KEY_INVALID;

        /* A denied key-down is latched until every keying control is released;
         * otherwise a held PTT would call tx_state and repaint every millisecond. */
        if (g.denied) {
            if (key == APP_KEY_PTT || paddles(key)) {
                resetUiInput();
                goto service;
            }
            g.denied = false;
#if CW_KEYER_USB
            /* Refusal bypassed debounce while held: discard its stale closure. */
            g.padStable = 0u;
#endif
            g.statusRedraw = true;
        }

        /* Keying is ignored while the help page is shown: PTT and the
         * paddle contacts then only reach the help key handler. */
        const uint8_t raw = paddles(key);
        uint8_t pad = debounced(raw);
        /* Paddle start on the first closed reading. On USB-C it is confirmed
         * 200 us later (a spike is not a contact); SIDE keys come from the
         * debounced keypad scan. Bounce no longer matters once an element
         * runs: the paddles are read again after the space. */
        if (g.mode != MODE_STRAIGHT && raw && !pad) {
#if CW_KEYER_USB
            const uint32_t t = nowCycles();
            while (nowCycles() - t < CONFIRM_CYCLES)
                ;
            if (paddles(key) == raw)
#endif
                pad = raw;
        }
#if CW_KEYER_USB
        const bool keying = q.ready;
#else
        const bool keying = !g.help;
#endif
        const bool straight = keying &&
            (g.mode == MODE_STRAIGHT && pad);
        if (straight) {
            resetUiInput();
            if (!g.keyDown)
                keyOn();
        } else if (g.keyDown) {
            keyOff();
            resetUiInput();
        } else if (keying && g.mode != MODE_STRAIGHT && pad) {
            resetUiInput();
            A->backlight_on();
            runPaddle(pad);
            g.padRaw = g.padStable = g.padPolls = 0u;   /* no stale restart */
        } else if (key != APP_KEY_INVALID) {
            handleUiInput(key);
        } else {
            resetUiInput();
        }

        if (g.redraw && !g.keyDown)
            draw();
        else if (g.statusRedraw && !g.keyDown)
            redrawMainStatus();
service:
        serviceOneMs();
    }

    disarm();
    A->bk_write(REG_XTAL, g.xtalBase);
#if CW_KEYER_USB
    portSet(false);
#endif
    saveConfig();
    A->set_af(APP_AF_MUTE);
    A->audio_path(false);
}
