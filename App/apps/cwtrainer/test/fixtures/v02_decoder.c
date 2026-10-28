/* Copyright 2026 CW Trainer contributors
 * USB port and paddle engine adapted from CW USB / CW Keyer:
 * Copyright 2026 Armel F4HWN, Johan Denoyer F4WAT, Sebastien F4LPF.
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
 */
#include <stdbool.h>
#include <stdint.h>
#include "../app_api.h"

#ifndef TRAINER_HOST
#define GPIO_MODE (*(volatile uint32_t *)0x50000000u)
#define GPIO_PULL (*(volatile uint32_t *)0x5000000Cu)
#define GPIO_INPUT (*(volatile uint32_t *)0x50000010u)
#define USB_CLOCK (*(volatile uint32_t *)0x4002103Cu)
static uint32_t irqSave(void)
{
    uint32_t p;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(p) :: "memory");
    return p;
}
static void irqRestore(uint32_t p)
{
    __asm volatile("msr primask, %0" :: "r"(p) : "memory");
}
#endif
#define USB_PINS (0xFu << 22)
#define USB_PULLS (0x5u << 22)
#define USB_ENABLE (1u << 23)
#define CYC_MS 48000u
#define CFG_MAGIC 0xD1u
enum { STRAIGHT, IAMB_A, IAMB_B };
typedef struct {
    uint8_t magic, mode, wpm, reverse;
    uint16_t hz;
    uint8_t rxWpm, length;
    uint16_t rxHz;
} config_t;
_Static_assert(sizeof(config_t) == 10, "trainer config layout");
static struct {
    const app_api_t *a;
    config_t cfg;
    uint32_t mode, pull, clock, candidateAt, deadline, markAt, releaseAt;
    uint32_t keyAt, serviceAt;
    uint8_t raw, stable, element, last, memoryRaw, key;
    bool pending, mark, ready, releasing, keyHandled, redraw, help;
    uint32_t rxAt, decAt;
    uint8_t exercise, playing, rxIndex, rxMask, rxCode, code, used, score;
    bool reveal, corrected, overflow;
    char target[6], answer[17];
} g;
#define A (g.a)
/* Never read a callback before its complete field is within api_size. */
#ifndef TRAINER_API_END
#define TRAINER_API_END(field) (offsetof(app_api_t, field) + sizeof(((app_api_t *)0)->field))
#endif
#define HAS(api, field) ((api)->api_size >= TRAINER_API_END(field) && (api)->field)

static bool displayAvailable(const app_api_t *api)
{
    return HAS(api, display_clear) && HAS(api, status_clear) &&
        HAS(api, print_normal) && HAS(api, blit_status) && HAS(api, blit_full);
}

static bool servicesAvailable(const app_api_t *api)
{
    return displayAvailable(api) && HAS(api, get_key) && HAS(api, delay_ms) &&
        HAS(api, bk_write) && HAS(api, audio_path) && HAS(api, prepare_tone) &&
        HAS(api, set_af) && HAS(api, tones_off_rx) && HAS(api, cfg_load) &&
        HAS(api, cfg_save) && HAS(api, backlight_on) && HAS(api, backlight_update) &&
        HAS(api, uidivmod) && HAS(api, append_u32) && HAS(api, cycles_48mhz);
}

/* Root bit 1 followed by Morse bits (dit=0, dah=1), common to RX and decoder.
 * A-Z then 0-9. No audio samples and no large word list are held in RAM. */
static const uint8_t morse[] = {
    5,24,26,12,2,18,14,16,4,23,13,20,7,6,15,22,29,10,8,3,9,17,11,25,27,28,63,47,39,35,33,32,48,56,60,62
};

static char letter(uint8_t i) { return i < 26u ? (char)('A'+i) : (char)('0'+i-26u); }
static uint8_t encode(char c) { return morse[c <= '9' ? c-'0'+26 : c-'A']; }

static uint32_t unit(uint8_t wpm)
{
    return (uint32_t)A->uidivmod(1200u * CYC_MS, wpm);
}

static void appendDecoded(char c)
{
    if (g.used < 16u) { g.answer[g.used++] = c; g.answer[g.used] = 0; }
    else g.overflow = true;
    g.redraw = true;
}

static void finishLetter(void)
{
    if (g.code == 1u) return;
    char c = '?';
    for (uint8_t i=0; i<36u; i++) if (morse[i] == g.code) c = letter(i);
    appendDecoded(c);
    g.code = 1u;
}

static void decodeGap(uint32_t now)
{
    if (g.corrected) return; /* Keep the scored response stable while idle. */
    uint32_t d = unit(g.cfg.wpm), gap = now-g.decAt;
    if (gap >= d*5u/2u) finishLetter();
    if (gap >= d*7u && g.used && g.answer[g.used-1u] != ' ') appendDecoded(' ');
}

static void clearAnswer(void)
{
    g.used=0; g.answer[0]=0; g.code=1; g.overflow=false; g.corrected=false;
}

static void tone(bool on);

static void stopPlayback(void)
{
    /* Keep playing set until the final audio edge: automatic Morse must never
     * enter the user's decoder. The release gate is reset by the caller. */
    tone(false);
    g.playing=0;
}

static void playback(uint32_t now)
{
    if (!g.playing || !app_cycles_reached(now,g.rxAt)) return;
    uint32_t d = unit(g.cfg.rxWpm);
    if (g.mark) {
        tone(false);
        g.rxMask >>= 1;
        g.rxAt = now + d;
        if (!g.rxMask) { g.rxIndex++; g.rxAt = now + d*3u; }
    } else {
        if (!g.rxMask) {
            if (g.rxIndex == g.cfg.length) { g.playing=0; g.redraw=true; return; }
            g.rxCode = encode(g.target[g.rxIndex]);
            g.rxMask=32;
            while (!(g.rxCode & g.rxMask)) g.rxMask >>= 1;
            g.rxMask >>= 1;
        }
        tone(true);
        g.rxAt = now + d*((g.rxCode & g.rxMask) ? 3u : 1u);
    }
}

static void startExercise(bool fresh, uint32_t now)
{
    if (fresh || !g.target[0]) {
        for (uint8_t i=0; i<g.cfg.length; i++)
            g.target[i]=letter((uint8_t)(A->rand32()%36u));
        g.target[g.cfg.length]=0;
    }
    clearAnswer(); g.reveal=false; g.playing=1; g.rxIndex=g.rxMask=0; g.rxAt=now;
}

static void correct(void)
{
    finishLetter();
    while (g.used && g.answer[g.used-1u]==' ') g.answer[--g.used]=0;
    g.score=0;
    for (uint8_t i=0; i<g.cfg.length; i++)
        if (i<g.used && g.answer[i]==g.target[i]) g.score++;
    g.corrected=g.reveal=true;
}

static void unavailable(const app_api_t *api)
{
    /* Diagnostics use only individually checked baseline services, no GPIO/audio. */
    if (!displayAvailable(api) || !HAS(api, get_key) || !HAS(api, delay_ms)) return;
    api->display_clear(); api->status_clear();
    api->print_normal("API3 incomplete", 0, 127, 2);
    api->print_normal("EXIT to Labs", 0, 127, 4);
    api->blit_status(); api->blit_full();
    while (api->get_key() != APP_KEY_EXIT) api->delay_ms(10);
}

static void port(bool open)
{
    uint32_t irq = irqSave();
    if (open) {
        g.mode = GPIO_MODE & USB_PINS;
        g.pull = GPIO_PULL & USB_PINS;
        g.clock = USB_CLOCK & USB_ENABLE;
    }
    GPIO_PULL = (GPIO_PULL & ~USB_PINS) | (open ? USB_PULLS : g.pull);
    GPIO_MODE = (GPIO_MODE & ~USB_PINS) | (open ? 0u : g.mode);
    USB_CLOCK = (USB_CLOCK & ~USB_ENABLE) | (open ? 0u : g.clock);
    irqRestore(irq);
}

static uint8_t contacts(void)
{
    uint32_t pins = ~GPIO_INPUT;
    uint8_t p = (uint8_t)(((pins >> 12) & 1u) | ((pins >> 10) & 2u));
    if (g.cfg.mode == STRAIGHT) return p & 1u; /* Mono ring ignored. */
    return g.cfg.reverse ? (uint8_t)((p >> 1) | ((p & 1u) << 1)) : p;
}

static void tone(bool on)
{
    /* Only the local alarm output: no RF setup, PA, frequency or TX callback.
     * Like CW USB, key REG_71 rather than rebuilding the audio path per edge. */
    uint16_t hz = g.playing ? g.cfg.rxHz : g.cfg.hz;
    uint16_t word = (uint16_t)((hz * 1353245u + 65536u) >> 17);
    A->bk_write(0x71u, on ? word : 0u);
    if (!g.playing && g.mark != on) {
        uint32_t now = A->cycles_48mhz();
        if (on) { decodeGap(now); g.decAt=now; g.corrected=false; }
        else {
            uint32_t duration=now-g.decAt, d=unit(g.cfg.wpm);
            uint8_t dash = g.cfg.mode==STRAIGHT ? duration>=2u*d : g.element==2u;
            if (duration>=d/3u) {
                if (g.code<32u && duration<=5u*d) g.code=(uint8_t)((g.code<<1)|dash);
                else g.code=0;
            }
            g.decAt=now;
        }
    }
    g.mark = on;
}

static void resetKeyer(void)
{
    tone(false);
    g.element = g.last = g.raw = g.stable = g.memoryRaw = 0;
    g.pending = g.ready = g.releasing = false;
}

/* Same opposite-tap and mark-only squeeze memory as CW USB. Two identical
 * observations qualify memory; start/straight edges use a timed debounce. */
static void remember(uint8_t p)
{
    if (p == g.memoryRaw && g.element &&
        (p == (g.element ^ 3u) ||
         (p == 3u && g.cfg.mode == IAMB_B && g.mark)))
        g.pending = true;
    g.memoryRaw = p;
}

static uint32_t dotCycles(void)
{
    return unit(g.cfg.wpm);
}

static void startElement(uint8_t el, uint32_t now)
{
    g.element = el;
    g.pending = false;
    g.memoryRaw = 0;
    tone(true);
    g.markAt = now;
    g.deadline = now + dotCycles() * (el == 2u ? 3u : 1u);
}

static void step(uint32_t now, uint8_t p)
{
    if (p != g.raw) { g.raw = p; g.candidateAt = now; }
    if (now - g.candidateAt >= 3u * CYC_MS) g.stable = p;
    /* Startup/mode changes/timeout require 20 ms observed release. No RF-style
     * five-second arming or swallowed first press after ordinary idle. */
    if (!g.ready) {
        if (p) g.releasing = false;
        else if (!g.releasing) { g.releasing = true; g.releaseAt = now; }
        else if (now - g.releaseAt >= 20u * CYC_MS) g.ready = true;
        return;
    }
    if (g.cfg.mode == STRAIGHT) {
        if (g.stable && !g.mark) { tone(true); g.markAt = now; }
        if (!g.stable && g.mark) tone(false);
        if (g.mark && now - g.markAt >= 10000u * CYC_MS) resetKeyer();
        return;
    }
    if (g.element) {
        remember(p);
        if (!app_cycles_reached(now, g.deadline)) return;
        if (g.mark) {
            tone(false);
            /* Anchor each deadline at the observed audio edge: no shortened
             * gap when a keypad scan took longer than expected. */
            g.deadline = now + dotCycles();
            return;
        }
        uint8_t next = g.pending ? (g.element ^ 3u) : g.stable;
        g.last = g.element;
        g.element = 0;
        if (next) startElement(next == 3u ? (g.last ^ 3u) : next, now);
    } else if (g.stable) {
        /* A new squeeze begins with a dit, as in CW USB. */
        startElement(g.stable == 3u ? 1u : g.stable, now);
    }
}

static void draw(void)
{
    char s[20], *o;
    A->display_clear();
    A->status_clear();
    if (g.help) {
        static const char *const lines[] = {
            "0 TX / RX / RX>TX", "1 KEY  5 SWAP",
            "UP/DN WPM 3/6 HZ", "4 LENGTH 7 NEW", "8 REPLAY 2 CLEAR",
            "9 SHOW / SCORE", "MENU BACK EXIT END"
        };
        for (uint8_t i = 0; i < 7; i++) A->print_normal(lines[i], 0, 127, i);
    } else {
    A->print_normal(g.exercise==0 ? "TX TRAINER 0.2" :
                    g.exercise==1 ? "RX TRAINER 0.2" : "RX>TX 0.2", 0, 127, 0);
    A->print_normal(g.cfg.mode == STRAIGHT ? "STRAIGHT" :
                    g.cfg.mode == IAMB_A ? "IAMB A" : "IAMB B", 0, 127, 2);
    o = A->append_u32(s, g.exercise ? g.cfg.rxWpm : g.cfg.wpm); *o++ = ' '; *o++ = 'W'; *o++ = 'P'; *o++ = 'M'; *o = 0;
    A->print_normal(s, 0, 127, 3);
    o = A->append_u32(s, g.exercise ? g.cfg.rxHz : g.cfg.hz); *o++ = ' '; *o++ = 'H'; *o++ = 'z'; *o = 0;
    A->print_normal(s, 0, 127, 4);
    A->print_normal(g.reveal ? g.target : g.playing ? "LISTEN" :
                    g.exercise ? "7 NEW 8 REPLAY" : "MENU: HELP", 0,127,1);
    A->print_normal(g.answer,0,127,6);
    if (g.corrected) {
        o=A->append_u32(s,g.score); *o++='/'; o=A->append_u32(o,g.cfg.length);
        const char *t=g.score==g.cfg.length && g.used==g.cfg.length && !g.overflow ? " OK" : " ERR";
        while (*t) *o++=*t++;
        *o=0;
    } else { s[0]=(char)('0'+g.cfg.length);s[1]=0; }
    A->print_normal(s,0,127,5);
    }
    /* Same baseline services as CW USB, including older API3 tables. */
    A->blit_status(); A->blit_full();
    g.redraw = false;
}

static void ui(uint8_t key, uint32_t now)
{
    if (key != g.key) { g.key = key; g.keyAt = now; g.keyHandled = false; }
    if (g.keyHandled || now - g.keyAt < 20u * CYC_MS) return;
    g.keyHandled = true;
    if (key == APP_KEY_MENU) {
        stopPlayback();
        resetKeyer(); /* MENU also aborts an active element before drawing. */
        g.code=1;
        g.help = !g.help;
        g.redraw = true;
        A->backlight_on();
        return;
    }
    if (g.help) return;
    if (key==APP_KEY_0 || (key>=APP_KEY_7 && key<=APP_KEY_9) || key==APP_KEY_2) {
        stopPlayback(); resetKeyer();
        if (key==APP_KEY_0) { g.exercise=(g.exercise+1u)%3u;g.reveal=false;clearAnswer(); }
        else if (key==APP_KEY_2) {clearAnswer();g.reveal=false;}
        else if (g.exercise) {
            if (key==APP_KEY_9) { if(g.target[0]) { if(g.exercise==2) correct();else g.reveal=true; } }
            else startExercise(key==APP_KEY_7,now);
        }
        g.redraw=true;return;
    }
    /* Changes only at rest. A held contact cannot key across a mode change. */
    if (g.element || g.mark || g.raw || g.stable) return;
    if (g.playing) return;
    uint8_t *wpm=g.exercise ? &g.cfg.rxWpm : &g.cfg.wpm;
    uint16_t *hz=g.exercise ? &g.cfg.rxHz : &g.cfg.hz;
    switch (key) {
    case APP_KEY_1: g.cfg.mode = (uint8_t)((g.cfg.mode + 1u) % 3u); break;
    case APP_KEY_UP: if (*wpm < 60) (*wpm)++; break;
    case APP_KEY_DOWN: if (*wpm > 5) (*wpm)--; break;
    case APP_KEY_3: if (*hz > 300) *hz -= 50; break;
    case APP_KEY_6: if (*hz < 1000) *hz += 50; break;
    case APP_KEY_4: g.cfg.length=g.cfg.length%5u+1u;g.target[0]=0;clearAnswer();break;
    case APP_KEY_5: g.cfg.reverse ^= 1u; break;
    default: return;
    }
    resetKeyer();
    g.code=1;
    g.redraw = true;
    A->backlight_on();
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    if (!api || api->abi_major != 1) return;
    if (api->api_level < 3 || !servicesAvailable(api) || !HAS(api, rand32)) {
        unavailable(api);
        return;
    }
    g.exercise=g.playing=0;g.target[0]=0;g.reveal=false;clearAnswer();
    g.a = api;
    A->cfg_load((uint8_t *)&g.cfg, sizeof(g.cfg));
    if (g.cfg.magic != CFG_MAGIC || g.cfg.mode > IAMB_B ||
        g.cfg.wpm < 5 || g.cfg.wpm > 60 || g.cfg.reverse > 1 ||
        g.cfg.hz < 300 || g.cfg.hz > 1000 || g.cfg.hz % 50u) {
        g.cfg.magic = CFG_MAGIC;
        g.cfg.mode = STRAIGHT;
        g.cfg.wpm = 18;
        g.cfg.reverse = 0;
        g.cfg.hz = 700;
        g.cfg.rxWpm = 0;
    }
    if (g.cfg.rxWpm<5 || g.cfg.rxWpm>60 || g.cfg.length<1 || g.cfg.length>5 ||
        g.cfg.rxHz<300 || g.cfg.rxHz>1000 || g.cfg.rxHz%50u) {
        g.cfg.rxWpm=18;g.cfg.rxHz=600;g.cfg.length=5;
    }
    port(true);
    A->audio_path(false);
    A->prepare_tone(); /* Resident local-beep setup: REG_30=0x0302, PA gain off. */
    A->bk_write(0x70u, 0x9C00u); /* Tone1, gain 28; no RF GPIO access. */
    resetKeyer();
    A->set_af(2u); /* ALAM local tone route, audible with TX modulation muted. */
    A->delay_ms(20);
    A->audio_path(true);
    g.key = APP_KEY_INVALID;
    g.help = false;
    g.keyHandled = false;
    g.keyAt = g.serviceAt = A->cycles_48mhz();
    A->backlight_on();
    draw();
    for (;;) {
        uint8_t key = A->get_key();
        if (key == APP_KEY_EXIT) break; /* Abort even during a dah. */
        uint32_t now = A->cycles_48mhz();
        uint8_t p = contacts();
        /* PTT is deliberately ignored: only the passive USB-C adapter keys. */
        if (!g.help) {
            if (g.playing) playback(now);
            else {
                if (g.exercise!=1) step(now,p);
                if (!g.mark) decodeGap(now);
            }
        }
        ui(key, now);
        if (g.redraw && !g.element && !g.mark) draw();
        if (now - g.serviceAt >= 100u * CYC_MS) {
            g.serviceAt = now;
            A->backlight_update();
            A->backlight_on(); /* GPIO paddle presses do not wake get_key(). */
        }
        A->delay_ms(1);
    }
    stopPlayback();resetKeyer();
    A->audio_path(false);
    A->bk_write(0x70u, 0);
    A->set_af(APP_AF_MUTE);
    A->tones_off_rx();
    port(false);
    A->cfg_save((const uint8_t *)&g.cfg, sizeof(g.cfg));
}
