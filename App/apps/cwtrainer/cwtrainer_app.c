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
#include "cwtrainer_assets.h"

#ifndef TRAINER_HOST
#define GPIO_MODE (*(volatile uint32_t *)0x50000000u)
#define GPIO_PULL (*(volatile uint32_t *)0x5000000Cu)
#define GPIO_INPUT (*(volatile uint32_t *)0x50000010u)
#define USB_CLOCK (*(volatile uint32_t *)0x4002103Cu)
#define GPIOB_INPUT (*(volatile const uint32_t *)0x50000410u)
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
#define KEY_SIDE1 18u /* KEY_Code_e; API baseline does not name side buttons. */
enum { STRAIGHT, IAMB_A, IAMB_B };
typedef struct {
    uint8_t magic, mode, wpm, reverse;
    uint16_t hz;
    uint8_t rxWpm, length;
    uint16_t rxHz;
    uint8_t gap;
} config_t;
_Static_assert(sizeof(config_t) == 12, "trainer config layout");
static struct {
    const app_api_t *a;
    config_t cfg;
    uint32_t mode, pull, clock, candidateAt, deadline, markAt, releaseAt;
    uint32_t keyAt, serviceAt;
    uint8_t raw, stable, element, memoryRaw, key;
    bool pending, mark, ready, releasing, keyHandled, redraw;
    uint32_t rxAt, decAt;
    uint16_t rxMask, rxCode, code;
    uint8_t exercise, playing, rxIndex, used, score;
    bool reveal, corrected, overflow;
    char target[6], answer[49];
    char text[TEXT_SIZE];
    bool fArm, exitHeld, pttReady, success;
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

/* All used pointers precede append_u32 in the append-only ABI. Aligned memcpy
 * checks their representation without aliasing through an incompatible type. */
#define SLOT(field) ((offsetof(app_api_t,field)-offsetof(app_api_t,fb))/sizeof(((app_api_t*)0)->get_key))
_Static_assert(sizeof(uintptr_t)==sizeof(((app_api_t*)0)->get_key), "API pointer width");
static bool servicesAvailable(const app_api_t *api)
{
    static const uint8_t required[]={
        SLOT(fb),
        SLOT(display_clear),SLOT(status_clear),SLOT(print_normal),SLOT(blit_status),SLOT(blit_full),
        SLOT(get_key),SLOT(delay_ms),SLOT(bk_write),SLOT(audio_path),SLOT(prepare_tone),
        SLOT(set_af),SLOT(tones_off_rx),SLOT(cfg_load),SLOT(cfg_save),SLOT(backlight_on),
        SLOT(backlight_update),SLOT(uidivmod),SLOT(append_u32),SLOT(cycles_48mhz),
        SLOT(asset_read),SLOT(print_inverse),SLOT(print_tiny),SLOT(nav_dir),SLOT(rand32)
    };
    if(api->api_size<TRAINER_API_END(append_u32))return false;
    for(uint8_t i=0;i<sizeof(required);i++){
        const uint8_t *p=(const uint8_t*)api+offsetof(app_api_t,fb)+required[i]*sizeof(api->get_key);
        uintptr_t bits;
        __builtin_memcpy(&bits,__builtin_assume_aligned(p,sizeof(bits)),sizeof(bits));
        if(!bits)return false;
    }
    return true;
}

static char *text(uint32_t id)
{
    A->asset_read((uint16_t)id*TEXT_SIZE,g.text,TEXT_SIZE);
    g.text[TEXT_SIZE-1u]=0;
    return g.text;
}

static char *put(char *out, uint32_t id)
{
    const char *p=text(id);
    while(*p)*out++=*p++;
    return out;
}

/* Up to 4 little-endian asset bytes, 0 where the read falls short. */
static __attribute__((noinline)) uint32_t readAsset(uint16_t offset, uint8_t len)
{
    uint32_t v=0; A->asset_read(offset,&v,len); return v;
}

/* Morse lives in the assets as direct tables: the RX group pool, code ->
 * character and character -> code (root bit 1, dit=0, dah=1, up to 9 elements).
 * Prosigns without a character show as an ASCII sign; '*' marks unknown. */
static char letter(uint8_t i) { return (char)readAsset((uint16_t)(POOL_OFFSET+i),1); }
static uint16_t encode(char c) { return (uint16_t)readAsset((uint16_t)(ENCODE_OFFSET+2u*(uint8_t)(c-'!')),2); }

static uint32_t unit(uint8_t wpm)
{
    return (uint32_t)A->uidivmod(1200u * CYC_MS, wpm);
}

static uint32_t remu(uint32_t n, uint32_t d)
{
    return (uint32_t)(A->uidivmod(n,d)>>32);
}

static void appendDecoded(char c)
{
    if (!g.exercise && g.used==48u) {
        for(uint8_t i=0;i<32u;i++)g.answer[i]=g.answer[i+16u];
        g.used=32;
    }
    if (g.used < (g.exercise?16u:48u)) { g.answer[g.used++] = c; g.answer[g.used] = 0; }
    else g.overflow = true;
    g.redraw = true;
}

static void finishLetter(void)
{
    if (g.code == 1u) return;
    appendDecoded((char)readAsset((uint16_t)(DECODE_OFFSET+g.code),1));
    g.code = 1u;
}

static void decodeGap(uint32_t now)
{
    if (g.corrected) return; /* Keep the scored response stable while idle. */
    uint32_t d = unit(g.cfg.wpm), gap = now-g.decAt;
    if ((int32_t)gap < 0) return; /* Earlier sample, not a natural forward wrap. */
    if (gap >= d*5u/2u) finishLetter();
    if (gap >= d*7u && g.used && g.answer[g.used-1u] != ' ') appendDecoded(' ');
}

static void clearAnswer(void)
{
    g.used=0; g.answer[0]=0; g.code=1; g.overflow=false; g.corrected=false;g.reveal=false;
}

static uint32_t tone(bool on);
static void resetKeyer(void);

static void stopPlayback(void)
{
    /* Keep playing set until the final audio edge: automatic Morse must never
     * enter the user's decoder. The release gate is reset by the caller. */
    g.element=0; /* Cancellation is not a completed paddle element. */
    resetKeyer();
    g.playing=0;
}

static void playback(uint32_t now)
{
    if (!g.playing || !app_cycles_reached(now,g.rxAt)) return;
    if(g.playing>1){
        now=tone(!g.mark);
        if(!g.mark && ++g.rxMask==2){g.playing=0;return;}
        g.rxAt=now+(g.mark?60u:40u)*CYC_MS;
        return;
    }
    uint32_t d = unit(g.cfg.rxWpm);
    if (g.mark) {
        now=tone(false);
        g.rxMask >>= 1;
        g.rxAt = now + d;
        if (!g.rxMask) {
            /* Total silence from the final falling edge, not an extra gap. */
            static const uint8_t halfUnits[]={6,9,12,18};
            g.rxIndex++; g.rxAt = now + (d>>1)*halfUnits[g.cfg.gap];
        }
    } else {
        if (!g.rxMask) {
            if (g.rxIndex == g.cfg.length) { g.playing=0; g.redraw=true; return; }
            g.rxCode = encode(g.target[g.rxIndex]);
            g.rxMask=MAX_CODE;
            while (!(g.rxCode & g.rxMask)) g.rxMask >>= 1;
            g.rxMask >>= 1;
        }
        now=tone(true);
        g.rxAt = now + d*((g.rxCode & g.rxMask) ? 3u : 1u);
    }
}

static void startExercise(bool fresh, uint32_t now)
{
    if (fresh || !g.target[0]) {
        for (uint8_t i=0; i<g.cfg.length; i++)
            g.target[i]=letter((uint8_t)remu(A->rand32(),POOL_COUNT));
        g.target[g.cfg.length]=0;
    }
    clearAnswer(); g.playing=1; g.rxIndex=g.rxMask=0; g.rxAt=now;
}

static void correct(void)
{
    finishLetter();
    while (g.used && g.answer[g.used-1u]==' ') g.answer[--g.used]=0;
    g.score=0;
    for (uint8_t i=0; i<g.cfg.length; i++)
        if (i<g.used && g.answer[i]==g.target[i]) g.score++;
    g.success=g.score==g.cfg.length && g.used==g.cfg.length && !g.overflow;
    g.corrected=g.reveal=true;
}

static void unavailable(const app_api_t *api)
{
    /* Diagnostics use only individually checked baseline services, no GPIO/audio. */
    if (!displayAvailable(api) || !HAS(api, get_key) || !HAS(api, delay_ms)) return;
    api->display_clear(); api->status_clear();
    api->print_normal("API3? EXIT", 0, 127, 2);
    api->blit_status(); api->blit_full();
    while (api->get_key() != APP_KEY_EXIT) api->delay_ms(10);
}

static __attribute__((noinline)) void port(bool open)
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

static uint32_t tone(bool on)
{
    /* Only the local alarm output: no RF setup, PA, frequency or TX callback.
     * Like CW USB, key REG_71 rather than rebuilding the audio path per edge. */
    uint16_t hz = g.playing ? g.cfg.rxHz : g.cfg.hz;
    static const uint16_t feedback[]={880,1000,440,330};
    if(g.playing>1 && on)hz=feedback[(g.playing-2u)*2u+g.rxMask];
    uint16_t word = (uint16_t)((hz * 1353245u + 65536u) >> 17);
    A->bk_write(0x71u, on ? word : 0u);
    uint32_t now = A->cycles_48mhz();
    if (!g.playing && g.mark != on) {
        if (on) { decodeGap(now); g.decAt=now; g.corrected=false; }
        else {
            uint32_t duration=now-g.decAt, d=unit(g.cfg.wpm);
            /* Paddle completion is a motor event, independent of measured
             * duration; only STRAIGHT needs timing classification/filtering. */
            bool paddle=g.cfg.mode!=STRAIGHT;
            uint8_t dash = paddle ? g.element==2u : duration>=2u*d;
            if (paddle ? g.element!=0 : duration*3u>=d) {
                if (g.code && g.code<MAX_CODE && (paddle || duration<=5u*d)) g.code=(uint16_t)((g.code<<1)|dash);
                else g.code=0;
            }
            g.decAt=now;
        }
    }
    g.mark = on;
    return now;
}

static void resetKeyer(void)
{
    tone(false);
    g.element = g.raw = g.stable = g.memoryRaw = 0;
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
    now=tone(true);
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
        if (g.stable && !g.mark) { now=tone(true); g.markAt = now; }
        if (!g.stable && g.mark) tone(false);
        if (g.mark && now - g.markAt >= 10000u * CYC_MS) resetKeyer();
        return;
    }
    if (g.element) {
        remember(p);
        if (!app_cycles_reached(now, g.deadline)) return;
        if (g.mark) {
            now=tone(false);
            /* Anchor each deadline at the observed audio edge: no shortened
             * gap when a keypad scan took longer than expected. */
            g.deadline = now + dotCycles();
            return;
        }
        uint8_t next = g.pending ? (g.element ^ 3u) : g.stable;
        uint8_t last = g.element;
        g.element = 0;
        if (next) startElement(next == 3u ? (last ^ 3u) : next, now);
    } else if (g.stable) {
        /* A new squeeze begins with a dit, as in CW USB. */
        startElement(g.stable == 3u ? 1u : g.stable, now);
    }
}

static void draw(void)
{
    char s[24],*o=put(s,T_TX+g.exercise);
    o=A->append_u32(o,g.exercise?g.cfg.rxWpm:g.cfg.wpm);*put(o,g.fArm?T_F:T_WPM)=0;
    A->display_clear();A->status_clear();
    A->print_inverse(s,2,0,true,true,90);
    A->print_tiny(text(T_ZERO),99,1,true,true);
    if(!g.exercise){
        for(uint8_t row=0;row<3 && row*16u<g.used;row++){
            uint8_t end=(uint8_t)((row+1u)*16u);
            char saved=g.answer[end];g.answer[end]=0;
            A->print_normal(g.answer+row*16u,0,127,row);
            g.answer[end]=saved;
        }
    }else{
        if(g.reveal)A->print_normal(g.target,0,127,0);
        else A->print_tiny(text(g.playing?T_LISTEN:T_READY+g.exercise-1u),2,1,false,true);
        A->print_normal(g.answer,0,127,1);
        if(g.corrected){o=A->append_u32(s,g.score);*o++='/';o=A->append_u32(o,g.cfg.length);*put(o,g.success?T_OK:T_ERR)=0;}
        else{o=put(s,T_EDIT_RX);o=A->append_u32(o,g.cfg.length);*o=0;}
        A->print_normal(s,0,127,2);
    }
    o=put(s,T_STRAIGHT+g.cfg.mode);*o++=' ';
    o=A->append_u32(o,g.exercise?g.cfg.rxHz:g.cfg.hz);*put(o,T_HZ)=0;
    A->print_tiny(s,2,25,false,true);
    if(g.exercise)A->print_tiny(text(T_GAP_0+g.cfg.gap),88,25,false,true);
    /* Three pages of font-derived pixels, not code loaded from assets. */
    A->asset_read(GRID_OFFSET+384u*g.exercise,A->fb[4],384);
    A->blit_status();A->blit_full();g.redraw=false;
}

static __attribute__((noinline)) void ui(uint8_t key, uint32_t now)
{
    if (key != g.key) { g.key = key; g.keyAt = now; g.keyHandled = false; }
    int8_t direction=A->nav_dir(key);
    if (!g.keyHandled) {
        if(now-g.keyAt<20u*CYC_MS)return;
        g.keyAt=now+400u*CYC_MS;
    } else {
        if(!direction || !app_cycles_reached(now,g.keyAt))return;
        g.keyAt=now+100u*CYC_MS;
    }
    g.keyHandled = true;
    if(g.exercise==1 && (key==APP_KEY_1 || key==APP_KEY_5 || key==APP_KEY_6 || key==KEY_SIDE1))return;
    if(key==APP_KEY_9 && (g.mark || g.element || g.raw || g.stable))return;
    if(key==APP_KEY_PTT){
        if(g.exercise!=1 || !g.pttReady || !g.target[0])return;
        g.pttReady=false;
    }
    if (key==APP_KEY_0 || key==APP_KEY_7 || key==KEY_SIDE1 || key==APP_KEY_PTT || key==APP_KEY_9 || key==APP_KEY_6) {
        if(!g.exercise && (key==APP_KEY_7 || key==KEY_SIDE1 || key==APP_KEY_9))return;
        stopPlayback();
        if (key==APP_KEY_0) { if(++g.exercise==3)g.exercise=0;g.pttReady=false;clearAnswer(); }
        else if (key==APP_KEY_6) {clearAnswer();}
        else if (g.exercise) {
            if (key==APP_KEY_9) { if(g.target[0]) { if(g.exercise==2) {
                correct();g.playing=g.success?2:3;
                g.rxMask=0;g.rxAt=now;
            }else g.reveal=true; } }
            else startExercise(key==APP_KEY_7,now);
        }
        g.fArm=false;g.redraw=true;return;
    }
    /* Changes only at rest. A held contact cannot key across a mode change. */
    if (g.element || g.mark || g.raw || g.stable) return;
    if (g.playing) return;
    if(key==APP_KEY_F){g.fArm=!g.fArm;g.redraw=true;return;}
    uint8_t *wpm=g.exercise ? &g.cfg.rxWpm : &g.cfg.wpm;
    uint16_t *hz=g.exercise ? &g.cfg.rxHz : &g.cfg.hz;
    if(!direction)direction=g.fArm?-1:1;
    if(key==APP_KEY_UP || key==APP_KEY_DOWN)key=APP_KEY_2;
    switch (key) {
    case APP_KEY_1: if(++g.cfg.mode==3)g.cfg.mode=0; break;
    case APP_KEY_2: {uint8_t n=*wpm+direction;if((uint8_t)(n-5u)<=55u)*wpm=n;break;}
    case APP_KEY_3: {uint16_t n=*hz+direction*50;if((uint16_t)(n-300u)<=700u)*hz=n;break;}
    case APP_KEY_4: {if(!g.exercise)return;uint8_t n=g.cfg.length+direction;if((uint8_t)(n-1u)<=4u)g.cfg.length=n;g.target[0]=0;clearAnswer();break;}
    case APP_KEY_5: g.cfg.reverse ^= 1u; break;
    case APP_KEY_8: if(!g.exercise)return;g.cfg.gap=(g.cfg.gap+1u)&3u;break;
    default: return;
    }
    resetKeyer();
    g.fArm=false;
    g.code=1;
    g.redraw = true;
    A->backlight_on();
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    if (!api || api->abi_major != 1) return;
    if (api->api_level < 3 || !servicesAvailable(api)) {
        unavailable(api);
        return;
    }
    g.exercise=g.playing=0;g.target[0]=0;clearAnswer();
    g.a = api;
    if(readAsset(0,4)!=0x33335443u){unavailable(api);return;}
    A->cfg_load((uint8_t *)&g.cfg, sizeof(g.cfg));
    if (g.cfg.magic != CFG_MAGIC || g.cfg.mode > IAMB_B ||
        (uint8_t)(g.cfg.wpm-5u)>55u || g.cfg.reverse > 1 ||
        (uint16_t)(g.cfg.hz-300u)>700u || remu(g.cfg.hz,50u)) {
        g.cfg.magic = CFG_MAGIC;
        g.cfg.mode = STRAIGHT;
        g.cfg.wpm = 18;
        g.cfg.reverse = 0;
        g.cfg.hz = 700;
        g.cfg.rxWpm = 0;
    }
    if ((uint8_t)(g.cfg.rxWpm-5u)>55u || (uint8_t)(g.cfg.length-1u)>4u ||
        (uint16_t)(g.cfg.rxHz-300u)>700u || remu(g.cfg.rxHz,50u)) {
        g.cfg.rxWpm=18;g.cfg.rxHz=600;g.cfg.length=5;
    }
    if(g.cfg.gap>3)g.cfg.gap=0;
    port(true);
    A->audio_path(false);
    A->prepare_tone(); /* Resident local-beep setup: REG_30=0x0302, PA gain off. */
    A->bk_write(0x70u, 0x9C00u); /* Tone1, gain 28; no RF GPIO access. */
    resetKeyer();
    A->set_af(2u); /* ALAM local tone route, audible with TX modulation muted. */
    A->delay_ms(20);
    A->audio_path(true);
    g.key = APP_KEY_INVALID;
    g.fArm=g.exitHeld=g.pttReady=false;
    g.keyHandled = false;
    g.keyAt = g.serviceAt = A->cycles_48mhz();
    A->backlight_on();
    draw();
    for (;;) {
        uint8_t key = A->get_key();
        if(key==APP_KEY_EXIT){
            if(!g.exitHeld){
                g.exitHeld=true;
                if(!g.playing && !(g.exercise && g.target[0]))break;
                g.target[0]=0;clearAnswer();
                stopPlayback();g.code=1;g.redraw=true;
            }
        }else g.exitHeld=false;
        uint32_t now = A->cycles_48mhz();
        uint8_t p = contacts();
        bool ptt=!(GPIOB_INPUT&(1u<<10));
        if(!ptt)g.pttReady=true;
        /* PB10 is read-only. STRAIGHT combines both local contacts; iambic
         * modes deliberately ignore PTT, so their event streams cannot mix. */
        if(g.cfg.mode==STRAIGHT && ptt)p=1;
        {
            if (g.playing) playback(now);
            else {
                if (g.exercise!=1) step(now,p);
                if (!g.mark) decodeGap(A->cycles_48mhz());
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
    stopPlayback();
    A->audio_path(false);
    A->bk_write(0x70u, 0);
    A->set_af(APP_AF_MUTE);
    A->tones_off_rx();
    port(false);
    A->cfg_save((const uint8_t *)&g.cfg, sizeof(g.cfg));
}
