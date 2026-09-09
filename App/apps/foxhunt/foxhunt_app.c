/* Copyright 2026 Armel F4HWN
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
 * FoxHunt (RX) — overlay app. Ported from App/app/foxhunt.c, hunt sub-mode only
 * (the beacon becomes a separate app). Signal-strength hunter: corrected dBm with
 * a big number, an IARU S-meter staircase or a scrolling history, peak/min hold,
 * a 1 s trend, a front-end attenuator ladder, and Geiger/station audio.
 *
 * Keys: 1 gauge (bar/history) · 2 audio (off/beep/station) · 3 + UP/DOWN attenuator
 *       MENU reset holds · long F keypad lock · EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define LCD_WIDTH        128
#define BK_REG_13        0x13

/* --- tuning (from foxhunt.c) --- */
#define DBM_FLOOR   (-141)
#define DBM_CEIL    (-53)
#define TICK_MS      50
#define TREND_TICKS  20
#define LOCK_HOLD_MS 500
#define SILENCE_DBM  (-120)
#define TONE_MIN     400
#define TONE_MAX     2400
#define RATE_SLOW    20
#define RATE_FAST    2
#define BLIP_MS      50
#define AUDIO_SETTLE 60
#define ATT_SETTLE   40

#define ATT_COUNT    6
#define ATT_BYP0     4
#define SEG_COUNT    13
#define BAR_X0       6
#define SEG_PITCH    9
#define SEG_W        8
#define SEG_BOTTOM   37

#define HIST_LEN     120
#define HIST_DECIM   3
#define GRAPH_X0     4
#define GRAPH_TOP    27
#define GRAPH_BOT    45
#define GRAPH_FLOOR  2

#define AUDIO_OFF     0
#define AUDIO_BEEP    1
#define AUDIO_STATION 2
#define GRAPH_BAR     0
#define GRAPH_HIST    1

#define CFG_MAGIC    0xF1   /* config validity marker */

static const uint16_t ATT_REG13[ATT_COUNT] = { 0x03DF, 0x03DD, 0x03DB, 0x03D9, 0x0379, 0x0139 };
static const uint8_t  ATT_DB[ATT_BYP0]     = { 0, 6, 15, 27 };

/* Status-bar / trend icons (bytes copied verbatim from App/bitmaps.c). */
static const uint8_t BMP_BARS[11]    = {0x40,0x40,0x00,0x70,0x70,0x00,0x7c,0x7c,0x00,0x7f,0x7f};
static const uint8_t BMP_GRAPH[15]   = {0x08,0x04,0x02,0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x02,0x04,0x08,0x10,0x20};
static const uint8_t BMP_SIGNAL[10]  = {0x08,0x1c,0x1c,0x08,0x00,0x22,0x1c,0x41,0x22,0x1c};
static const uint8_t BMP_SPEAKER[10] = {0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c};
static const uint8_t BMP_UP[11]      = {0x20,0x30,0x38,0x3c,0x3e,0x3f,0x3e,0x3c,0x38,0x30,0x20};
static const uint8_t BMP_DOWN[11]    = {0x01,0x03,0x07,0x0f,0x1f,0x3f,0x1f,0x0f,0x07,0x03,0x01};
static const uint8_t BMP_FLAT[11]    = {0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x12};
static const uint8_t FONT_F[9]       = {0x3e,0x7f,0x41,0x75,0x75,0x75,0x7d,0x7f,0x3e};
static const uint8_t FONT_LOCK[9]    = {0x7c,0x46,0x45,0x45,0x45,0x45,0x45,0x46,0x7c};

static void cpy(uint8_t *d, const uint8_t *s, uint8_t n){ while(n--)*d++=*s++; }

static const app_api_t *A;

static bool     foxLocked, fArm, fLongDone;
static uint16_t fHoldMs;
static uint8_t  foxAudioMode, foxGraphMode, attStep, audioTick;
static int16_t  curDbm, peakDbm, minDbm, trendRef, trendDelta;
static uint8_t  trendTick;
static uint8_t  histBuf[HIST_LEN];
static uint8_t  histHead, histTick;
static int16_t  histEma;
static uint8_t  prevKey;
static bool     running;
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

/* ---- radio helpers ---- */
static int16_t iabs16(int16_t v){ return v<0?-v:v; }

static void applyAtt(void){
    uint16_t reg = A->bk_read(BK_REG_13);
    reg = (uint16_t)((reg & ~0x03FFu) | ATT_REG13[attStep]);
    A->bk_write(BK_REG_13, reg);
}

static void setAudio(void){
    if(foxAudioMode==AUDIO_OFF){ A->audio_path(false); return; }
    A->audio_path(true);
    A->delay_ms(AUDIO_SETTLE);
    A->set_af(APP_AF_MUTE);
}

static int32_t lerp(int16_t dbm,int32_t lo,int32_t hi){
    if(dbm<=DBM_FLOOR) return lo;
    if(dbm>=DBM_CEIL)  return hi;
    const int32_t delta = hi - lo;
    const uint32_t distance = ((uint32_t)(dbm-DBM_FLOOR) *
                               (uint32_t)(delta < 0 ? -delta : delta)) /
                              (uint32_t)(DBM_CEIL-DBM_FLOOR);
    return lo + (delta < 0 ? -(int32_t)distance : (int32_t)distance);
}
static void blip(uint16_t freq){
    A->prepare_tone();
    A->play_tone_raw(freq, BLIP_MS);
    A->tones_off_rx();
    A->set_agc(false);
    applyAtt();
}

static uint8_t fillCount(int16_t dbm){
    int16_t n;
    if(dbm<-141) return 0;
    if(dbm<=-93) n=(int16_t)(1u+(uint16_t)(dbm+141)/6u);
    else         n=(int16_t)(9u+(uint16_t)(dbm+93)/10u);
    if(n>SEG_COUNT) n=SEG_COUNT;
    return (uint8_t)n;
}
static void buildS(char *out,int16_t dbm){
    if(dbm>=-93){ int16_t o=dbm-(-93); if(o>40)o=40; char *p=put(out,"S9+"); if(o<10)*p++='0'; i2str(p,o); }
    else if(dbm<-141) put(out,"S0")[0]='\0';
    else { char *p=put(out,"S"); i2str(p,(int)((uint16_t)(dbm+147)/6u)); }
}

static int16_t div_trunc_pow2(int32_t v, uint8_t shift)
{
    if (v < 0)
        return (int16_t)-((uint32_t)(-v) >> shift);
    return (int16_t)((uint32_t)v >> shift);
}

static uint8_t cycleIndex(uint8_t value, uint8_t count, int8_t dir)
{
    if (dir > 0)
        return ++value < count ? value : 0u;
    return value > 0u ? (uint8_t)(value - 1u) : (uint8_t)(count - 1u);
}

static void histSample(void){
    histEma += div_trunc_pow2((int32_t)curDbm * 8 - histEma, 2u);
    if(++histTick<HIST_DECIM) return;
    histTick=0;
    histBuf[histHead]=fillCount(div_trunc_pow2(histEma, 3u));
    if(++histHead>=HIST_LEN) histHead=0;
}
static void rebase(void){
    curDbm=A->rssi_dbm();
    peakDbm=minDbm=trendRef=curDbm;
    trendDelta=0; trendTick=0;
    uint8_t lvl=fillCount(curDbm);
    for(uint8_t i=0;i<HIST_LEN;i++) histBuf[i]=lvl;
    histHead=0; histTick=0; histEma=(int16_t)(curDbm*8);
}
static void attCycle(int8_t dir){
    attStep=cycleIndex(attStep,ATT_COUNT,dir);
    applyAtt();
    A->delay_ms(ATT_SETTLE);
    rebase();
}

/* ---- drawing ---- */
static void fillRect(int16_t x0,int16_t y0,int16_t x1,int16_t y1){
    for(int16_t x=x0;x<=x1;x++) A->draw_line(A->fb,x,y0,x,y1,true);
}
static void drawBar(void){
    uint8_t n=fillCount(curDbm);
    for(uint8_t i=0;i<n;i++){
        int16_t sx=BAR_X0+i*SEG_PITCH;
        fillRect(sx,SEG_BOTTOM-i,sx+SEG_W-1,SEG_BOTTOM);
    }
    A->draw_line(A->fb,BAR_X0,SEG_BOTTOM+2,BAR_X0+(SEG_COUNT-1)*SEG_PITCH+SEG_W-1,SEG_BOTTOM+2,true);
}
static void drawHist(void){
    const uint8_t floorY=GRAPH_BOT-GRAPH_FLOOR;
    const uint8_t span=floorY-GRAPH_TOP;
    A->draw_line(A->fb,GRAPH_X0,GRAPH_BOT,GRAPH_X0+HIST_LEN-1,GRAPH_BOT,true);
    uint8_t prevY=0, idx=histHead;
    for(uint8_t c=0;c<HIST_LEN;c++){
        uint8_t lvl=histBuf[idx]; if(++idx>=HIST_LEN)idx=0;
        if(lvl>SEG_COUNT)lvl=SEG_COUNT;
        uint8_t x=GRAPH_X0+c;
        uint8_t y=(uint8_t)(floorY-((uint32_t)lvl*span)/(uint32_t)SEG_COUNT);
        for(uint8_t yy=y+1;yy<=floorY;yy++) if(((x+yy)&1)==0) A->draw_line(A->fb,x,yy,x,yy,true);
        if(c==0){ A->draw_line(A->fb,x,y,x,y,true); }
        else { uint8_t lo=(y<prevY)?y:prevY, hi=(y<prevY)?prevY:y; A->draw_line(A->fb,x,lo,x,hi,true); }
        prevY=y;
    }
}
static void tag(const char *s,uint8_t x,uint8_t line){
    A->print_inverse(s,x,line,false,true,(uint8_t)(x+slen(s)*4));
}
static void draw(void){
    char big[8], sMeter[8];

    A->display_clear();
    A->status_clear();
    A->print_inverse("FOX HUNT",2,0,true,true,34);
    A->draw_battery();

    /* status-bar icons: graph mode @38, audio mode @55, F/lock @69 */
    if(foxGraphMode==GRAPH_HIST) cpy(A->status_line+38,BMP_GRAPH,15);
    else                         cpy(A->status_line+38,BMP_BARS,11);
    if(foxAudioMode==AUDIO_BEEP)          cpy(A->status_line+55,BMP_SIGNAL,10);
    else if(foxAudioMode==AUDIO_STATION)  cpy(A->status_line+55,BMP_SPEAKER,10);
    if(foxLocked)  cpy(A->status_line+70,FONT_LOCK,9);
    else if(fArm)  cpy(A->status_line+70,FONT_F,sizeof(FONT_F));

    i2str(big,curDbm);
    A->display_freq(big,2,0,false);
    A->print_normal("dBm",(uint8_t)(slen(big)*13+4),0,1);

    /* trend arrow (line 0, right) + signed delta (line 1) */
    cpy(A->fb[0]+115, trendDelta>0?BMP_UP:trendDelta<0?BMP_DOWN:BMP_FLAT, 11);
    if(trendDelta!=0){
        char *p=str; if(trendDelta>0)*p++='+'; else{*p++='-';}
        int16_t a=iabs16(trendDelta); if(a<10){*p++='0';} i2str(p,a);
        A->print_normal("dBm",127-3*7,0,1);
        A->print_normal(str,(uint8_t)(127-3*7-2-slen(str)*7),1,1);
    }

    i2str(put(str,"PK "),peakDbm);
    tag(str,4,2);
    i2str(put(str,"MN "),minDbm);
    tag(str,(uint8_t)(((uint32_t)LCD_WIDTH-(uint32_t)slen(str)*4u)/2u),2);
    buildS(sMeter,curDbm);
    A->print_inverse(sMeter,(uint8_t)(126-slen(sMeter)*4),2,false,true,125);

    if(foxGraphMode==GRAPH_HIST) drawHist();
    else drawBar();

    if(attStep<ATT_BYP0){ char *o=put(str,"ATT "); o=puti(o,ATT_DB[attStep]); put(o,"dB")[0]='\0'; }
    else put(str,(attStep==ATT_BYP0)?"BYP":"BYP+")[0]='\0';
    tag(str,4,6);

    { uint32_t f=A->rx_freq(); char *o=puti(str,(int)(f/100000u)); *o++='.';
      uint32_t fr=f%100000u; for(int8_t d=4;d>=0;d--){ uint32_t p=1; for(int8_t k=0;k<d;k++)p*=10; *o++=(char)('0'+(fr/p)%10);} *o='\0'; }
    A->print_normal(str,(uint8_t)(126-slen(str)*7),0,6);
}

/* ---- config (deferred) ---- */
static void loadConfig(void){
    uint8_t c[4];
    A->cfg_load(c,4);
    if(c[0]==CFG_MAGIC){
        if(c[1]<ATT_COUNT)      attStep=c[1];
        if(c[2]<=GRAPH_HIST)    foxGraphMode=c[2];
        if(c[3]<=AUDIO_STATION) foxAudioMode=c[3];
    }
}
static void saveConfig(void){
    uint8_t c[4]={CFG_MAGIC,attStep,foxGraphMode,foxAudioMode};
    A->cfg_save(c,4);
}

/* ---- input ---- */
static void handleKeys(void){
    uint8_t key=A->get_key();

    /* long-press F -> keypad lock */
    if(key==APP_KEY_F){
        if(!fLongDone){ fHoldMs+=TICK_MS; if(fHoldMs>=LOCK_HOLD_MS){ fLongDone=true; foxLocked=!foxLocked; fArm=false; A->backlight_on(); } }
    } else { fHoldMs=0; fLongDone=false; }

    if(key==APP_KEY_INVALID||key==prevKey){ prevKey=key; return; }
    prevKey=key;
    A->backlight_on();

    if(foxLocked){
        if(key==APP_KEY_UP||key==APP_KEY_DOWN) attCycle(A->nav_dir(key));
        return;
    }
    if(key==APP_KEY_F){ fArm=!fArm; return; }
    int8_t dir=fArm?-1:1;

    switch(key){
        case APP_KEY_EXIT: running=false; break;
        case APP_KEY_1: foxGraphMode^=1; break;
        case APP_KEY_2:
            foxAudioMode=cycleIndex(foxAudioMode,3u,dir); setAudio();
            if(foxAudioMode==AUDIO_BEEP){ audioTick=RATE_SLOW; }
            break;
        case APP_KEY_3: attCycle(dir); break;
        case APP_KEY_UP:
        case APP_KEY_DOWN: attCycle(A->nav_dir(key)); break;
        case APP_KEY_MENU: peakDbm=minDbm=trendRef=curDbm; break;
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
    foxLocked=fArm=fLongDone=false; fHoldMs=0;
    attStep=0; foxGraphMode=GRAPH_BAR; foxAudioMode=AUDIO_OFF;
    prevKey=APP_KEY_INVALID;

    loadConfig();
    A->backlight_on();
    A->set_agc(false);
    applyAtt();
    setAudio();
    A->delay_ms(ATT_SETTLE);
    rebase();

    running=true;
    while(running){
        handleKeys();
        if(!running) break;

        curDbm=A->rssi_dbm();
        if(curDbm>peakDbm) peakDbm=curDbm;
        if(curDbm<minDbm)  minDbm=curDbm;
        if(++trendTick>=TREND_TICKS){ trendDelta=curDbm-trendRef; trendRef=curDbm; trendTick=0; }
        histSample();

        draw();
        A->blit_status();
        A->blit_full();

        if(foxAudioMode==AUDIO_BEEP && curDbm>=SILENCE_DBM){
            if(++audioTick>=(uint8_t)lerp(curDbm,RATE_SLOW,RATE_FAST)){ audioTick=0; blip((uint16_t)lerp(curDbm,TONE_MIN,TONE_MAX)); }
        } else if(foxAudioMode==AUDIO_STATION){
            A->set_af(curDbm>=SILENCE_DBM?APP_AF_FM:APP_AF_MUTE);
        }

        A->battery_sample();
        tickDelay();
    }

    saveConfig();
    A->audio_path(false);
}
