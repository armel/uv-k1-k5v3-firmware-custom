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
 * Beacon (fox) — overlay app. Ported from App/app/foxhunt.c, beacon sub-mode only.
 * Turns the radio into a hidden ARDF transmitter: keys up on the TX VFO and repeats
 * a CW fox identifier (MOE..MO5 / MO / "<call> MOE") as MCW for the TX window, then
 * stays silent for the idle gap. The carrier stays up during the window; only the
 * tone is keyed per Morse element.
 *
 * Keys: 1 TX window · 2 idle gap · 3 fox id (F reverses) · MENU restart idle
 *       long F keypad lock · EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define LCD_WIDTH    128
#define TICK_MS      50
#define LOCK_HOLD_MS 500
#define MORSE_UNIT   100
#define TONE_HZ      1000
#define CALL_MAX     12

#define IDLE_DEF 30
#define IDLE_MIN 5
#define IDLE_MAX 240
#define IDLE_STEP 5
#define TX_DEF   30
#define TX_MIN   5
#define TX_MAX   60
#define TX_STEP  5

#define FOX_MO   5
#define FOX_CALL 6
#define FOX_COUNT 7
#define MSG_CALL 1
#define MSG_ID   3
#define MSG_ONE  2

#define CFG_MAGIC 0xF4

/* Sentinel-prefixed Morse (leading 1 sentinel, then elements MSB-first: 0 dit, 1 dah). */
static const uint8_t M_LET[26] = {0x05,0x18,0x1A,0x0C,0x02,0x12,0x0E,0x10,0x04,0x17,0x0D,0x14,0x07,0x06,0x0F,0x16,0x1D,0x0A,0x08,0x03,0x09,0x11,0x0B,0x19,0x1B,0x1C};
static const uint8_t M_DIG[10] = {0x3F,0x2F,0x27,0x23,0x21,0x20,0x30,0x38,0x3C,0x3E};
static const char    FOX_TAIL[5] = {'E','I','S','H','5'};
static const uint8_t BMP_TX[16] = {0x1c,0x22,0x41,0x1c,0x22,0x00,0x08,0x1c,0x1c,0x08,0x00,0x22,0x1c,0x41,0x22,0x1c};
static const uint8_t FONT_LOCK[9] = {0x7c,0x46,0x45,0x45,0x45,0x45,0x45,0x46,0x7c};

static const app_api_t *A;

static bool     foxLocked, fArm, fLongDone, running, phaseTx;
static uint16_t fHoldMs;
static uint8_t  beaconIdle, idleLeft, idleTick, beaconTx, secShown, charsSent, foxFox;
static uint16_t txMsLeft;
static char     foxCall[CALL_MAX+1];
static char     msg[17];
static uint8_t  prevKey;
static char     str[16];

/* ---- tiny formatting ---- */
static uint8_t slen(const char *s){ uint8_t n=0; while(s[n])n++; return n; }
static char *put(char *o,const char *s){ while(*s)*o++=*s++; return o; }
static char *putu(char *o,uint32_t v){ char t[6]; int8_t n=0; do{t[n++]=(char)('0'+v%10);v/=10;}while(v&&n<6); while(n--)*o++=t[n]; return o; }
static void put2(char **o,uint8_t v){ if(v<10)*(*o)++='0'; *o=putu(*o,v); }
static void cpy(uint8_t *d,const uint8_t *s,uint8_t n){ while(n--)*d++=*s++; }
static const char *findsp(const char *s){ while(*s){ if(*s==' ')return s; s++; } return NULL; }
static uint8_t mmin(uint8_t a,uint8_t b){ return a<b?a:b; }

/* ---- settings cycles ---- */
static uint8_t rangeStep(uint8_t v,uint8_t lo,uint8_t hi,uint8_t step,int8_t dir){
    if(dir>0) return (v>=hi)?lo:(uint8_t)(v+step);
    return (v<=lo)?hi:(uint8_t)(v-step);
}
static bool settingKey(uint8_t key,int8_t dir){
    switch(key){
        case APP_KEY_1: beaconTx  =rangeStep(beaconTx,  TX_MIN,  TX_MAX,  TX_STEP,  dir); return true;
        case APP_KEY_2: beaconIdle=rangeStep(beaconIdle,IDLE_MIN,IDLE_MAX,IDLE_STEP,dir); return true;
        case APP_KEY_3: foxFox=(uint8_t)((foxFox+(dir>0?1:FOX_COUNT-1))%FOX_COUNT);       return true;
        default: return false;
    }
}

/* ---- message ---- */
static void buildMsg(void){
    char *o=msg;
    if(foxFox==FOX_CALL){
        if(foxCall[0]){ o=put(o,foxCall); o=put(o," MOE"); }
        else o=put(o,"MOE");
    } else if(foxFox==FOX_MO){ o=put(o,"MO"); }
    else { o=put(o,"MO"); *o++=FOX_TAIL[foxFox]; }
    *o='\0';
}
static void foxLabel(char *out){
    char *o=put(out,"FOX ");
    if(foxFox==FOX_CALL) o=put(o,"CALL");
    else if(foxFox==FOX_MO) o=put(o,"MO");
    else { o=put(o,"MO"); *o++=FOX_TAIL[foxFox]; }
    *o='\0';
}
static uint8_t morseByte(char c){
    if(c>='a'&&c<='z') c-=32;
    if(c>='A'&&c<='Z') return M_LET[c-'A'];
    if(c>='0'&&c<='9') return M_DIG[c-'0'];
    if(c=='/')         return 0x32;
    return 0;
}

/* ---- drawing ---- */
static void tag(const char *s,uint8_t x,uint8_t line){ A->print_inverse(s,x,line,false,true,(uint8_t)(x+slen(s)*4)); }

static void chrome(void){
    A->display_clear();
    A->status_clear();
    A->print_inverse("BEACON",2,0,true,true,26);
    A->draw_battery();
    if(foxLocked) cpy(A->status_line+69,FONT_LOCK,9);
    { char *o=putu(str,A->tx_freq()/100000u); *o++='.'; uint32_t fr=A->tx_freq()%100000u;
      for(int8_t d=4;d>=0;d--){ uint32_t p=1; for(int8_t k=0;k<d;k++)p*=10; *o++=(char)('0'+(fr/p)%10);} *o='\0'; }
    A->print_normal(str,(uint8_t)(126-slen(str)*7),0,6);
}
static void drawTxSeconds(void){
    uint8_t sec=(uint8_t)((txMsLeft+999u)/1000u);
    for(uint8_t x=0;x<LCD_WIDTH;x++) A->fb[0][x]=0;
    A->print_normal("TX",1,0,0);
    char *o=str; put2(&o,sec); *o++='s'; *o='\0';
    A->print_normal(str,(uint8_t)(127-slen(str)*7),0,0);
    secShown=sec;
}
static void drawMessage(uint8_t vis){
    const char *sp=findsp(msg);
    if(!sp){
        uint8_t idLen=slen(msg), v=mmin(vis,idLen);
        char id[17]; cpy((uint8_t*)id,(const uint8_t*)msg,v); id[v]='\0';
        A->print_string(id,(uint8_t)((LCD_WIDTH-idLen*8u)/2u),0,MSG_ONE,8);
        return;
    }
    uint8_t callLen=(uint8_t)(sp-msg), vc=mmin(vis,callLen);
    char call[CALL_MAX+1]; cpy((uint8_t*)call,(const uint8_t*)msg,vc); call[vc]='\0';
    A->print_string(call,(uint8_t)((LCD_WIDTH-callLen*8u)/2u),0,MSG_CALL,8);
    if(vis>callLen+1){
        const char *id=sp+1; uint8_t idLen=slen(id), vi=mmin((uint8_t)(vis-callLen-1),idLen);
        char idb[8]; cpy((uint8_t*)idb,(const uint8_t*)id,vi); idb[vi]='\0';
        A->print_string(idb,(uint8_t)((LCD_WIDTH-idLen*8u)/2u),0,MSG_ID,8);
    }
}
static void updateProgress(uint8_t vis){
    const char *sp=findsp(msg); uint8_t top;
    charsSent=vis;
    if(!sp) top=MSG_ONE;
    else { uint8_t callLen=(uint8_t)(sp-msg); if(vis==callLen+1) return; top=(vis<=callLen)?MSG_CALL:MSG_ID; }
    for(uint8_t x=0;x<LCD_WIDTH;x++){ A->fb[top][x]=0; A->fb[top+1][x]=0; }
    drawMessage(charsSent);
    A->blit_line(top); A->blit_line(top+1);
}
static void beaconDraw(bool txNow,uint8_t il){
    chrome();
    if(txNow){
        cpy(A->status_line+48,BMP_TX,16);
        drawTxSeconds();
        drawMessage(charsSent);
    } else {
        A->print_string("IDLE",0,127,1,10);
        char *o=str; put2(&o,il); *o++='s'; *o='\0';
        A->print_string(str,0,127,3,10);
    }
    char *o=put(str,"TX "); o=putu(o,beaconTx); o=put(o,"s"); *o='\0'; tag(str,4,5);
    o=put(str,"IDLE "); o=putu(o,beaconIdle); o=put(o,"s"); *o='\0'; tag(str,4,6);
    foxLabel(str); tag(str,(uint8_t)(125-slen(str)*4),5);
}
static void blit(void){ A->blit_status(); A->blit_full(); }

/* ---- keypad-lock long-press ---- */
static void lockTrack(uint8_t key,uint16_t ms){
    if(key!=APP_KEY_F){ fHoldMs=0; fLongDone=false; return; }
    if(fLongDone) return;
    fHoldMs+=ms;
    if(fHoldMs>=LOCK_HOLD_MS){ fLongDone=true; foxLocked=!foxLocked; fArm=false; A->backlight_on(); }
}

/* ---- interactive TX delay: drains the window, keys, live seconds. true = abort ---- */
static bool txDelay(uint16_t ms){
    while(ms){
        uint16_t slice=(ms>10)?10:ms;
        A->delay_ms(slice); ms-=slice;
        txMsLeft=(txMsLeft>slice)?(uint16_t)(txMsLeft-slice):0;
        A->backlight_update();
        if((uint8_t)((txMsLeft+999u)/1000u)!=secShown){ drawTxSeconds(); A->blit_line(0); }

        uint8_t key=A->get_key();
        lockTrack(key,slice);
        if(key==APP_KEY_INVALID||key==prevKey){ prevKey=key; continue; }
        prevKey=key;
        A->backlight_on();
        if(foxLocked) continue;
        if(key==APP_KEY_EXIT){ running=false; return true; }
        if(key==APP_KEY_MENU) return true;   /* cut short, restart idle */
        if(key==APP_KEY_F){ fArm=!fArm; beaconDraw(true,0); blit(); continue; }
        if(settingKey(key,fArm?-1:1)){ fArm=false; beaconDraw(true,0); blit(); }
    }
    return false;
}
static bool morseChar(char c,uint8_t vis){
    uint8_t code=morseByte(c);
    if(code==0){ if(txDelay(MORSE_UNIT*4)) return true; updateProgress(vis); return false; }
    uint8_t bit=0x80; while(!(code&bit))bit>>=1;
    for(bit>>=1;bit;bit>>=1){
        uint16_t on=(code&bit)?(MORSE_UNIT*3):MORSE_UNIT;
        A->tx_mute(false);
        if(txDelay(on)){ A->tx_mute(true); return true; }
        A->tx_mute(true);
        if(txDelay(MORSE_UNIT)) return true;
    }
    updateProgress(vis);
    return txDelay(MORSE_UNIT*2);
}
static void transmit(void){
    A->tx_set_params();
    A->tx_tone(TONE_HZ);
    A->tx_mute(true);
    txMsLeft=(uint16_t)beaconTx*1000u;
    bool stop=false;
    while(!stop && txMsLeft>0){
        buildMsg();
        charsSent=0;
        beaconDraw(true,0); blit();
        for(uint8_t i=0;!stop && msg[i];i++) stop=morseChar(msg[i],(uint8_t)(i+1));
        if(!stop && txMsLeft>0) stop=txDelay(MORSE_UNIT*7);
    }
    A->tx_mute(true);
    A->tx_end();   /* PA off + restore RX */
}

/* ---- idle-phase keys ---- */
static void idleKeys(void){
    uint8_t key=A->get_key();
    lockTrack(key,TICK_MS);
    if(key==APP_KEY_INVALID||key==prevKey){ prevKey=key; return; }
    prevKey=key;
    A->backlight_on();
    if(foxLocked) return;
    if(key==APP_KEY_F){ fArm=!fArm; return; }
    switch(key){
        case APP_KEY_EXIT: running=false; break;
        case APP_KEY_MENU: idleLeft=beaconIdle; idleTick=0; break;
        default: settingKey(key,fArm?-1:1); break;
    }
    fArm=false;
}

static void tickDelay(void){ for(uint8_t i=0;i<TICK_MS/10;i++){ A->delay_ms(10); A->backlight_update(); } }

/* ---- config (deferred) ---- */
static void loadConfig(void){
    uint8_t c[4]; A->cfg_load(c,4);
    if(c[0]==CFG_MAGIC){
        if(c[1]>=IDLE_MIN&&c[1]<=IDLE_MAX&&(c[1]%IDLE_STEP)==0) beaconIdle=c[1];
        if(c[2]<FOX_COUNT) foxFox=c[2];
        if(c[3]>=TX_MIN&&c[3]<=TX_MAX&&(c[3]%TX_STEP)==0) beaconTx=c[3];
    }
}
static void saveConfig(void){ uint8_t c[4]={CFG_MAGIC,beaconIdle,foxFox,beaconTx}; A->cfg_save(c,4); }

static void txDenied(void){
    chrome();
    A->print_string("TX OFF",0,127,1,8);
    blit();
    for(uint8_t i=0;i<20;i++) tickDelay();
}

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api){
    A=api;
    foxLocked=fArm=fLongDone=false; fHoldMs=0;
    beaconIdle=IDLE_DEF; beaconTx=TX_DEF; foxFox=FOX_CALL;
    prevKey=APP_KEY_INVALID;
    A->boot_callsign(foxCall,sizeof(foxCall));
    loadConfig();
    A->backlight_on();

    phaseTx=true; idleLeft=beaconIdle; idleTick=0;
    running=true;
    while(running){
        if(phaseTx){
            if(A->tx_state()!=0){ txDenied(); phaseTx=false; idleLeft=beaconIdle; idleTick=0; continue; }
            transmit();
            phaseTx=false; idleLeft=beaconIdle; idleTick=0;
            continue;
        }
        idleKeys();
        if(!running) break;
        beaconDraw(false,idleLeft); blit();
        if(++idleTick>=(1000/TICK_MS)){ idleTick=0; if(idleLeft>0)idleLeft--; if(idleLeft==0)phaseTx=true; }
        A->battery_sample();
        tickDelay();
    }

    saveConfig();
    A->tx_end();          /* safety: PA off + RX restored */
    A->audio_path(false);
}
