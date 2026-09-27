/* Copyright 2026 Johan Denoyer F4WAT
 * https://github.com/jdenoy
 *
 * Based on the FoxHunt overlay app,
 * Copyright 2026 Armel F4HWN
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

/*
 * EPIRB Finder (RX only) - overlay app forked from FoxHunt. Direction finding of
 * a 121.5 / 243 MHz homing beacon with a directional antenna: tune the VFO (AM,
 * FM or USB) first, then launch. Feedback is RELATIVE to a peak, not absolute dBm, so it keeps
 * working when the signal gets strong: the closer the reading is to the peak, the
 * faster and higher the beeps; within ONPEAK_Q of it, a fast high "on peak" chirp.
 *
 *   FOLLOW (default): the peak falls back slowly (0.5/1/2/4 dB/s), so the app always
 *                     answers "is this the best direction of the last few seconds".
 *   SWEEP:            the peak never decays. MENU starts a new sweep: turn 360 deg,
 *                     then turn back until the feedback says you are on the maximum.
 *
 * Direction finding works on any modulation (RSSI is taken before demodulation and
 * the app never retunes). Listening follows the firmware (radio.c): AM and FM both
 * use the FM AF output, the VFO having already enabled the AM demodulator in REG_31;
 * USB uses the baseband output. The USB listen mode is picked at launch when the VFO
 * had it set (REG_47 is muted while the squelch is closed, so it can also be chosen
 * by hand).
 *
 * An auto attenuator steps the front end up before the RSSI saturates (and back
 * down when weak, FOLLOW only) and re-bases the peak by the measured offset.
 *
 * Keys (identical on UV-K5 and UV-K1; UP/DOWN go through nav_dir):
 *   1 mode FOLLOW/SWEEP · 2 audio off/beep/listen/listen USB · 3 attenuator step
 *   (manual) · 4 auto attenuator on/off · 5 decay rate · UP/DOWN attenuator (manual)
 *   MENU reset peak / new sweep · F reverses 2,3 · long F keypad lock · EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define LCD_WIDTH    128
#define BK_REG_13    0x13
#define BK_REG_31    0x31         /* bit 0: AM demodulator enabled by the VFO      */
#define BK_REG_47    0x47         /* bits 11:8: AF output type                     */
#define AF_USB       5            /* BK4819_AF_BASEBAND2, the firmware's USB output */

#define TICK_MS      50
#define LOCK_HOLD_MS 500
#define AUDIO_SETTLE 60
#define ATT_SETTLE   40

/* --- relative feedback, dB in Q8 (x256) --- */
#define Q8(db)       ((int32_t)(db) * 256)
#define WIN_Q        Q8(20)       /* delta at which feedback is at its slowest     */
#define ONPEAK_Q     (Q8(3) / 2)  /* 1.5 dB: "you are aimed"                       */
#define GATE_DBM     (-122)       /* below this, no beeps / AM stays muted         */
#define TONE_MIN     500
#define TONE_MAX     2000
#define TONE_PEAK    2800
#define RATE_SLOW    16           /* ticks between beeps at delta >= WIN           */
#define RATE_FAST    2            /* ticks between beeps just off the peak         */
#define BLIP_MS      40

/* --- auto attenuator --- */
#define ATT_UP_DBM   (-60)        /* step up (more attenuation) at/above this      */
#define ATT_DN_DBM   (-105)       /* step down (FOLLOW only) at/below this         */
#define ATT_HOLD     20           /* ticks between two automatic steps (1 s)       */
#define ATT_COUNT    6
#define ATT_BYP0     4

/* --- history graph, relative to the peak --- */
#define HIST_LEN     120
#define HIST_DECIM   3            /* 120 x 3 x 50 ms = 18 s, about one slow sweep  */
#define HIST_BIAS    200          /* stored as clamp(dBm + 200, 0..255)            */
#define GRAPH_X0     4
#define GRAPH_TOP    25
#define GRAPH_BOT    46
#define GRAPH_SPAN   30           /* dB shown below the peak                       */

#define MODE_FOLLOW  0
#define MODE_SWEEP   1
#define AUDIO_OFF    0
#define AUDIO_BEEP   1
#define AUDIO_LSN    2            /* listen, AF as the firmware sets it for AM/FM  */
#define AUDIO_USB    3            /* listen, USB baseband output                   */
#define AUDIO_COUNT  4
#define DECAY_COUNT  4
#define DECAY_DEF    1

#define CFG_MAGIC    0xE5
#define CFG_LEN      6

static const uint16_t ATT_REG13[ATT_COUNT] = { 0x03DF, 0x03DD, 0x03DB, 0x03D9, 0x0379, 0x0139 };
static const uint8_t  ATT_DB[ATT_BYP0]     = { 0, 6, 15, 27 };
/* per-tick peak decay in Q8: dB/s * 256 / 20 */
static const uint8_t  DECAY_Q8[DECAY_COUNT]  = { 6, 13, 26, 51 };
static const char    *const DECAY_TXT[DECAY_COUNT] = { ".5", "1", "2", "4" };

static const uint8_t BMP_SIGNAL[10]  = {0x08,0x1c,0x1c,0x08,0x00,0x22,0x1c,0x41,0x22,0x1c};
static const uint8_t BMP_SPEAKER[10] = {0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c};
static const uint8_t FONT_F[9]       = {0x3e,0x7f,0x41,0x75,0x75,0x75,0x7d,0x7f,0x3e};
static const uint8_t FONT_LOCK[9]    = {0x7c,0x46,0x45,0x45,0x45,0x45,0x45,0x46,0x7c};

static void cpy(uint8_t *d, const uint8_t *s, uint8_t n){ while(n--)*d++=*s++; }

static const app_api_t *A;

static bool     locked, fArm, fLongDone, autoAtt, running;
static uint16_t fHoldMs;
static bool     vfoAm;
static uint8_t  mode, audioMode, attStep, decayIdx, audioTick, attHold, prevKey;
static int16_t  rawDbm;
static int32_t  smQ, peakQ;       /* smoothed reading and peak, dBm in Q8 */
static uint8_t  hist[HIST_LEN];
static uint8_t  histHead, histTick, histMax;
static char     str[16];

/* ---- tiny formatting ---- */
static uint8_t slen(const char *s){ uint8_t n=0; while(s[n])n++; return n; }
static char *put(char *o,const char *s){ while(*s)*o++=*s++; return o; }
static char *puti(char *o,int v){
    uint32_t u;
    if(v<0){*o++='-';u=(uint32_t)(-v);} else u=(uint32_t)v;
    char t[6]; int8_t n=0;
    do{t[n++]=(char)('0'+u%10u);u/=10u;}while(u&&n<6);
    while(n--)*o++=t[n];
    return o;
}
static void i2str(char *out,int v){ *puti(out,v)='\0'; }

/* Q8 -> nearest whole dB (symmetric rounding) */
static int16_t qdb(int32_t q){ return (int16_t)(q<0 ? -((-q+128)>>8) : (q+128)>>8); }

static uint8_t cycleIndex(uint8_t value, uint8_t count, int8_t dir)
{
    if (dir > 0)
        return ++value < count ? value : 0u;
    return value > 0u ? (uint8_t)(value - 1u) : (uint8_t)(count - 1u);
}

static uint8_t histEnc(int32_t q){
    int16_t v=(int16_t)(qdb(q)+HIST_BIAS);
    return (uint8_t)(v<0?0:v>255?255:v);
}

/* ---- radio ---- */
static void applyAtt(void){
    uint16_t reg = A->bk_read(BK_REG_13);
    reg = (uint16_t)((reg & ~0x03FFu) | ATT_REG13[attStep]);
    A->bk_write(BK_REG_13, reg);
}

static void setAudio(void){
    if(audioMode==AUDIO_OFF){ A->audio_path(false); return; }
    A->audio_path(true);
    A->delay_ms(AUDIO_SETTLE);
    A->set_af(APP_AF_MUTE);
}

static void blip(uint16_t freq){
    A->prepare_tone();
    A->play_tone_raw(freq, BLIP_MS);
    A->tones_off_rx();
    A->set_agc(false);
    applyAtt();
}

static void resetPeak(void){
    peakQ=smQ;
    uint8_t v=histEnc(smQ);
    for(uint8_t i=0;i<HIST_LEN;i++) hist[i]=v;
    histHead=0; histTick=0; histMax=0;
}

/* Change the attenuator and shift every stored level by the MEASURED offset, so the
 * peak and the history stay comparable with the new readings. */
static void attSet(uint8_t step){
    if(step==attStep) return;
    attStep=step;
    applyAtt();
    A->delay_ms(ATT_SETTLE);
    int32_t after=0;
    for(uint8_t i=0;i<4;i++){ after+=A->rssi_dbm(); A->delay_ms(5); }
    after=after*64;                       /* sum of 4 dBm -> mean in Q8 */
    int32_t off=after-smQ;
    smQ=after; peakQ+=off;
    int16_t offDb=qdb(off);
    for(uint8_t i=0;i<HIST_LEN;i++){
        if(!hist[i]) continue;
        int16_t v=(int16_t)(hist[i]+offDb);
        hist[i]=(uint8_t)(v<1?1:v>255?255:v);
    }
    attHold=ATT_HOLD;
}

static void autoAttTick(void){
    if(attHold){ attHold--; return; }
    if(!autoAtt) return;
    if(rawDbm>=ATT_UP_DBM && attStep<ATT_COUNT-1)
        attSet((uint8_t)(attStep+1));
    else if(mode==MODE_FOLLOW && rawDbm<=ATT_DN_DBM && attStep>0)
        attSet((uint8_t)(attStep-1));
}

static void measure(void){
    rawDbm=A->rssi_dbm();
    smQ+=(Q8(rawDbm)-smQ)/2;
    if(mode==MODE_FOLLOW) peakQ-=DECAY_Q8[decayIdx];
    if(smQ>peakQ) peakQ=smQ;

    uint8_t v=histEnc(smQ);
    if(v>histMax) histMax=v;
    if(++histTick>=HIST_DECIM){
        hist[histHead]=histMax;
        if(++histHead>=HIST_LEN) histHead=0;
        histTick=0; histMax=0;
    }
}

static int32_t delta(void){ int32_t d=peakQ-smQ; return d<0?0:d>WIN_Q?WIN_Q:d; }

static void feedback(void){
    bool live = qdb(smQ)>=GATE_DBM;
    if(audioMode>=AUDIO_LSN){ A->set_af(live?(audioMode==AUDIO_USB?AF_USB:APP_AF_FM):APP_AF_MUTE); return; }
    if(audioMode!=AUDIO_BEEP || !live){ audioTick=0; return; }

    int32_t d=delta();
    uint8_t  rate;
    uint16_t tone;
    if(d<=ONPEAK_Q){ rate=1; tone=TONE_PEAK; }
    else {
        rate=(uint8_t)(RATE_FAST+(RATE_SLOW-RATE_FAST)*d/WIN_Q);
        tone=(uint16_t)(TONE_MAX-(TONE_MAX-TONE_MIN)*d/WIN_Q);
    }
    if(++audioTick>=rate){ audioTick=0; blip(tone); }
}

/* ---- drawing ---- */
static void vline(int16_t x,int16_t y0,int16_t y1){ A->draw_line(A->fb,x,y0,x,y1,true); }
static void tag(const char *s,uint8_t x,uint8_t line){
    A->print_inverse(s,x,line,false,true,(uint8_t)(x+slen(s)*4));
}

static void drawCloseness(void){
    int32_t d=delta();
    A->draw_rect(A->fb,1,16,126,23,true);
    uint8_t len=(uint8_t)(((WIN_Q-d)*123)/WIN_Q);
    for(uint8_t x=0;x<len;x++) vline(2+x,18,21);
    /* on-peak marker: the last ONPEAK_Q of the bar */
    uint8_t mk=(uint8_t)(2+((WIN_Q-ONPEAK_Q)*123)/WIN_Q);
    vline(mk,16,23);
}

static void drawHist(void){
    int16_t top=(int16_t)(qdb(peakQ)+HIST_BIAS);
    const uint8_t span=GRAPH_BOT-GRAPH_TOP;
    for(uint8_t x=GRAPH_X0;x<GRAPH_X0+HIST_LEN;x+=4) vline(x,GRAPH_TOP,GRAPH_TOP);  /* dotted peak line */
    A->draw_line(A->fb,GRAPH_X0,GRAPH_BOT,GRAPH_X0+HIST_LEN-1,GRAPH_BOT,true);
    uint8_t idx=histHead;
    for(uint8_t c=0;c<HIST_LEN;c++){
        uint8_t v=hist[idx]; if(++idx>=HIST_LEN)idx=0;
        int16_t below=(int16_t)(top-v);             /* dB under the peak */
        if(below<0) below=0;
        if(below>=GRAPH_SPAN) continue;
        uint8_t y=(uint8_t)(GRAPH_TOP+((uint32_t)below*span)/GRAPH_SPAN);
        vline(GRAPH_X0+c,y,GRAPH_BOT);
    }
}

static void draw(void){
    char big[8];

    A->display_clear();
    A->status_clear();
    A->print_inverse("EPIRB DF",2,0,true,true,34);
    A->draw_battery();
    if(audioMode==AUDIO_BEEP)    cpy(A->status_line+40,BMP_SIGNAL,10);
    else if(audioMode>=AUDIO_LSN){
        cpy(A->status_line+40,BMP_SPEAKER,10);
        A->print_inverse(audioMode==AUDIO_USB?"USB":vfoAm?"AM":"FM",52,0,true,true,64);
    }
    if(locked)    cpy(A->status_line+70,FONT_LOCK,9);
    else if(fArm) cpy(A->status_line+70,FONT_F,sizeof(FONT_F));

    /* big: dB under the peak */
    int16_t dDb=qdb(delta());
    i2str(big,dDb?-dDb:0);
    A->display_freq(big,2,0,false);
    A->print_normal("dB",(uint8_t)(slen(big)*13+4),0,1);

    /* right: absolute level and frequency */
    { char *o=puti(str,qdb(smQ)); put(o,"dBm")[0]='\0';
      A->print_normal(str,(uint8_t)(127-slen(str)*7),0,0); }
    { uint32_t f=A->rx_freq()/100u;            /* kHz */
      char *o=puti(str,(int)(f/1000u)); *o++='.';
      uint32_t k=f%1000u; *o++=(char)('0'+k/100u); *o++=(char)('0'+(k/10u)%10u); *o++=(char)('0'+k%10u); *o='\0';
      A->print_normal(str,(uint8_t)(127-slen(str)*7),0,1); }

    drawCloseness();
    drawHist();

    /* bottom tags: mode, attenuator, peak */
    { char *o;
      if(mode==MODE_FOLLOW){ o=put(str,"FOL "); o=put(o,DECAY_TXT[decayIdx]); o=put(o,"dB/s"); }
      else o=put(str,"SWEEP");
      *o='\0'; tag(str,2,6); }
    { char *o=put(str,autoAtt?"A ":"M ");
      if(attStep<ATT_BYP0){ o=puti(o,ATT_DB[attStep]); o=put(o,"dB"); }
      else o=put(o,(attStep==ATT_BYP0)?"BYP":"BYP+");
      *o='\0'; tag(str,54,6); }
    { char *o=put(str,"PK "); o=puti(o,qdb(peakQ)); *o='\0';
      tag(str,(uint8_t)(126-slen(str)*4),6); }
}

/* ---- config (deferred) ---- */
static void loadConfig(void){
    uint8_t c[CFG_LEN];
    A->cfg_load(c,CFG_LEN);
    if(c[0]!=CFG_MAGIC) return;
    if(c[1]<ATT_COUNT)    attStep=c[1];
    if(c[2]<=MODE_SWEEP)  mode=c[2];
    if(c[3]<AUDIO_COUNT)  audioMode=c[3];
    autoAtt=c[4]!=0;
    if(c[5]<DECAY_COUNT)  decayIdx=c[5];
}
static void saveConfig(void){
    uint8_t c[CFG_LEN]={CFG_MAGIC,attStep,mode,audioMode,(uint8_t)autoAtt,decayIdx};
    A->cfg_save(c,CFG_LEN);
}

/* ---- input ---- */
static void manualAtt(int8_t dir){
    autoAtt=false;
    attSet(cycleIndex(attStep,ATT_COUNT,dir));
}

static void handleKeys(void){
    uint8_t key=A->get_key();

    if(key==APP_KEY_F){
        if(!fLongDone){ fHoldMs+=TICK_MS; if(fHoldMs>=LOCK_HOLD_MS){ fLongDone=true; locked=!locked; fArm=false; A->backlight_on(); } }
    } else { fHoldMs=0; fLongDone=false; }

    if(key==APP_KEY_INVALID||key==prevKey){ prevKey=key; return; }
    prevKey=key;
    A->backlight_on();

    if(locked){
        if(key==APP_KEY_UP||key==APP_KEY_DOWN) manualAtt(A->nav_dir(key));
        else if(key==APP_KEY_MENU) resetPeak();
        return;
    }
    if(key==APP_KEY_F){ fArm=!fArm; return; }
    int8_t dir=fArm?-1:1;

    switch(key){
        case APP_KEY_EXIT: running=false; break;
        case APP_KEY_1:    mode^=1; resetPeak(); break;
        case APP_KEY_2:    audioMode=cycleIndex(audioMode,AUDIO_COUNT,dir); setAudio(); audioTick=0; break;
        case APP_KEY_3:    manualAtt(dir); break;
        case APP_KEY_4:    autoAtt=!autoAtt; attHold=0; break;
        case APP_KEY_5:    decayIdx=cycleIndex(decayIdx,DECAY_COUNT,dir); break;
        case APP_KEY_UP:
        case APP_KEY_DOWN: manualAtt(A->nav_dir(key)); break;
        case APP_KEY_MENU: resetPeak(); break;
        default: break;
    }
    fArm=false;
}

static void tickDelay(void){
    for(uint8_t i=0;i<TICK_MS/10;i++){ A->delay_ms(10); A->backlight_update(); }
}

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api){
    A=api;
    locked=fArm=fLongDone=false; fHoldMs=0;
    attStep=0; mode=MODE_FOLLOW; audioMode=AUDIO_BEEP; autoAtt=true; decayIdx=DECAY_DEF;
    attHold=0; audioTick=0;
    prevKey=APP_KEY_INVALID;

    loadConfig();
    /* before anything touches the AF: what did the VFO set up? */
    vfoAm=(A->bk_read(BK_REG_31)&1u)!=0;
    { uint8_t af=(uint8_t)((A->bk_read(BK_REG_47)>>8)&0x0Fu);
      if(audioMode==AUDIO_LSN && af==AF_USB)       audioMode=AUDIO_USB;
      else if(audioMode==AUDIO_USB && af==APP_AF_FM) audioMode=AUDIO_LSN; }
    A->backlight_on();
    A->set_agc(false);
    applyAtt();
    setAudio();
    A->delay_ms(ATT_SETTLE);
    rawDbm=A->rssi_dbm();
    smQ=Q8(rawDbm);
    resetPeak();

    running=true;
    while(running){
        handleKeys();
        if(!running) break;

        measure();
        autoAttTick();

        draw();
        A->blit_status();
        A->blit_full();

        feedback();
        A->battery_sample();
        tickDelay();
    }

    saveConfig();
    A->audio_path(false);
}
