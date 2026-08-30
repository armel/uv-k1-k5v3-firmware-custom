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
 * Broadcast FM — overlay app. Sovereign: drives the BK1080 directly and owns the
 * radio while running (NO BK4819 dual-watch, unlike the resident FM). Reimplements
 * the full resident FM (fm.c / UI_DisplayFM) - VFO + 48 memories - sharing the
 * resident channel table (gFM_Channels) and config (gEeprom.FM_*), committed to
 * EEPROM on exit. The BK1080 audio is a hardware path, so playback needs no CPU loop.
 *
 * Keys (iso legacy): 0-9 = frequency (VFO) / channel (MR) entry · UP/DOWN = tune /
 *   channel step · STAR = manual scan · F+STAR = auto-scan (stores 48) · MENU = save
 *   (VFO) / delete (MR) · F+1 = band · F+3 = VFO<->MR · F+0 / EXIT = quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

#define STEP        1
#define SCAN_SETTLE 100
#define CHMAX       APP_FM_CH_MAX

static const char BAND_NAME[4][10] = { "87.5-108M", "76-108M", "76-90M", "64-76M" };
static const uint8_t FONT_F[8] = {0x7f,0x00,0x76,0x76,0x76,0x76,0x7e,0x7f};
static void cpy(uint8_t *d, const uint8_t *s, uint8_t n){ while(n--)*d++=*s++; }

static const app_api_t *A;
static uint16_t *ch;            /* shared gFM_Channels[48] */
static app_fm_state_t st;       /* freq_playing, sel_freq, band, is_mr, sel_ch */
static uint16_t lo, hi;
static int8_t   scanState;      /* 0 = off, +1 up, -1 down (non-blocking) */
static bool     autoScan;
static uint8_t  chPos;
static uint16_t scanTimer;      /* ms until the next scan step */
static bool     foundFreq;
static bool     askSave, askDelete;
static uint8_t  savePos;
static bool     fArm;
static uint8_t  inBox[4], inIdx;
static char     str[16];

static char *putu(char *o, uint32_t v){ char t[6]; int8_t n=0; do{t[n++]=(char)('0'+v%10);v/=10;}while(v&&n<6); while(n--)*o++=t[n]; return o; }
static char *put(char *o, const char *s){ while(*s)*o++=*s++; return o; }

static void bandLimits(void){ lo=A->fm_lo(st.band); hi=A->fm_hi(st.band); }
static uint16_t wrap(uint16_t f){ if(f<lo)return hi; if(f>hi)return lo; return f; }
static void tune(void){ A->fm_set_freq(st.freq_playing, st.band); }
static void dirty(void){ /* nothing yet; committed on exit */ }

static bool validCh(uint8_t c){ return c<CHMAX && ch[c]>=lo && ch[c]<hi; }
static uint8_t findNext(uint8_t c, int8_t dir)
{
    for(uint8_t i=0;i<CHMAX;i++){
        if(c==0xFF) c=CHMAX-1;
        else if(c>=CHMAX) c=0;
        if(validCh(c)) return c;
        c=(uint8_t)(c+dir);
    }
    return 0xFF;
}
static int configChannel(void)
{
    st.freq_playing=st.sel_freq;
    if(st.is_mr){
        uint8_t c=findNext(st.sel_ch,+1);
        if(c==0xFF){ st.is_mr=false; return -1; }
        st.sel_ch=c; st.freq_playing=ch[c];
    }
    return 0;
}

/* ---- drawing (mirror UI_DisplayFM) ---- */
static void freqBig(uint16_t f)
{
    uint16_t mhz=f/10u; char *o=str;
    if(mhz<100u)*o++=' ';
    o=putu(o,mhz); *o++='.'; *o++=(char)('0'+f%10u); *o='\0';
    A->display_freq(str,36,1,true);
}
/* append a zero-padded 2-digit number (1..99), return the end pointer */
static char *num2(char *o, uint8_t n){ if(n<10)*o++='0'; return putu(o,n); }

static void draw(void)
{
    char b[12], *o;

    A->display_clear();
    A->status_clear();
    A->draw_battery();
    if(fArm) cpy(A->status_line+69,FONT_F,8);   /* F armed indicator */
    A->print_string("FM",2,0,0,8);
    A->print_normal(BAND_NAME[st.band],1,0,6);

    /* ---- status (line 3) ---- */
    if(askSave)        { o=put(b,"SAVE?"); *o='\0'; }
    else if(askDelete) { o=put(b,"DEL?");  *o='\0'; }
    else if(scanState) {
        if(autoScan){ o=put(b,"A-SCAN("); o=putu(o,chPos); *o++=')'; }   /* count like UI_DisplayFM */
        else o=put(b,"M-SCAN");
        *o='\0';
    }
    else if(st.is_mr)  { o=put(b,"MR(CH"); o=num2(o,(uint8_t)(st.sel_ch+1)); *o++=')'; *o='\0'; }
    else {
        o=put(b,"VFO"); *o='\0';
        for(uint8_t i=0;i<CHMAX;i++) if(ch[i]==st.freq_playing){ o=put(b,"VFO(CH"); o=num2(o,(uint8_t)(i+1)); *o++=')'; *o='\0'; break; }
    }
    A->print_string(b,0,127,3,10);

    /* ---- line 1: input in progress / channel prompt / frequency ---- */
    if(inIdx>0){
        if(st.is_mr||askSave){                          /* channel number: "CH-XX" */
            o=put(b,"CH-");
            *o++=(char)('0'+inBox[0]);
            *o++=(inIdx>1)?(char)('0'+inBox[1]):'-';
            *o='\0';
            A->print_string(b,0,127,1,10);
        } else {                                        /* VFO frequency: "ddd.d" */
            b[0]=(char)('0'+inBox[0]);
            b[1]=(inIdx>1)?(char)('0'+inBox[1]):'-';
            b[2]=(inIdx>2)?(char)('0'+inBox[2]):'-';
            b[3]='.';
            b[4]=(inIdx>3)?(char)('0'+inBox[3]):'-';
            b[5]='\0';
            A->display_freq(b,36,1,false);
        }
    } else if(askSave){                                 /* pick a save slot */
        o=put(b,"CH-"); o=num2(o,(uint8_t)(savePos+1)); *o='\0';
        A->print_string(b,0,127,1,10);
    } else if(askDelete){
        o=put(b,"CH-"); o=num2(o,(uint8_t)(st.sel_ch+1)); *o='\0';
        A->print_string(b,0,127,1,10);
    } else {
        freqBig(st.freq_playing);
    }
}
static void show(void){ draw(); A->blit_status(); A->blit_full(); }

/* ---- non-blocking scan state machine (mirrors ACTION_Scan_FM / FM_Play / FM_Tune) ---- */
static void scanTuneNext(void)   /* FM_Tune(freq, scanState, false): step + mute + settle */
{
    A->fm_mute(true);
    foundFreq=false;
    st.freq_playing=wrap((uint16_t)(st.freq_playing+scanState));
    tune();
    scanTimer=SCAN_SETTLE;
}
static void stopScan(void)       /* FM_PlayAndUpdate */
{
    if(autoScan){ st.is_mr=true; st.sel_ch=0; }
    scanState=0; autoScan=false;
    configChannel();
    tune();
    A->fm_mute(false);
}
static void scanStep(void)       /* FM_Play: one step when the settle timer expires */
{
    if(A->fm_valid(st.freq_playing,lo)==0){          /* station locked */
        if(!autoScan){
            scanState=0; foundFreq=true;
            if(!st.is_mr) st.sel_freq=st.freq_playing;
            A->fm_mute(false);                       /* audio on */
            return;
        }
        if(chPos<CHMAX) ch[chPos++]=st.freq_playing;
        if(chPos>=CHMAX){ stopScan(); return; }
    }
    if(autoScan && st.freq_playing>=A->fm_hi(1)){ stopScan(); return; }
    scanTuneNext();
}
static void startManual(int8_t dir)
{
    if(scanState){ stopScan(); return; }             /* STAR toggles */
    autoScan=false; chPos=0; scanState=dir;
    scanTuneNext();
}
static void startAuto(void)
{
    if(scanState){ stopScan(); return; }
    autoScan=true; chPos=0; scanState=+1;
    for(uint8_t i=0;i<CHMAX;i++) ch[i]=0xFFFF;
    st.freq_playing=lo;
    A->fm_mute(true);
    tune();                                          /* FM_Tune(lo,1,true): no step */
    scanTimer=SCAN_SETTLE;
}

/* ---- digit entry ---- */
static void digit(uint8_t d)
{
    if(askDelete||scanState) return;
    if(askSave || st.is_mr){                     /* 2-digit channel */
        if(inIdx<2) inBox[inIdx++]=d;
        if(inIdx>=2){
            uint8_t c=(uint8_t)(inBox[0]*10+inBox[1]-1); inIdx=0;
            if(askSave){ if(c<CHMAX) savePos=c; }
            else if(validCh(c)){ st.sel_ch=c; st.freq_playing=ch[c]; tune(); }
        }
        return;
    }
    /* VFO frequency: 4 digits, with the leading-digit>1 zero-pad trick */
    inBox[inIdx++]=d;
    if(inIdx==1 && inBox[0]>1){ inBox[1]=inBox[0]; inBox[0]=0; inIdx=2; }
    else if(inIdx>3){
        uint16_t f=(uint16_t)(inBox[0]*1000+inBox[1]*100+inBox[2]*10+inBox[3]); inIdx=0;
        if(f>=lo && f<=hi){ st.sel_freq=f; st.freq_playing=f; tune(); }
    }
}

/* ---- MENU: save (VFO) / delete (MR) ---- */
static void menu(void)
{
    inIdx=0;
    if(!st.is_mr){                               /* VFO: save */
        if(askSave){ ch[savePos]=st.freq_playing; dirty(); }
        askSave=!askSave;
    } else {                                     /* MR: delete */
        if(askDelete){ ch[st.sel_ch]=0xFFFF; configChannel(); tune(); dirty(); }
        askDelete=!askDelete;
    }
}

/* ---- UP/DOWN: tune (VFO) or channel step (MR) ---- */
static void upDown(int8_t step)
{
    if(scanState){ scanState=step; scanTuneNext(); return; }   /* continue scan, new direction */
    if(askSave){
        if(step>0){ if(++savePos>=CHMAX) savePos=0; }
        else savePos=savePos?(uint8_t)(savePos-1u):(uint8_t)(CHMAX-1u);
        return;
    }
    if(st.is_mr){
        uint8_t c=findNext((uint8_t)(st.sel_ch+step),step);
        if(c!=0xFF && c!=st.sel_ch){ st.sel_ch=c; st.freq_playing=ch[c]; tune(); }
    } else {
        st.sel_freq=wrap((uint16_t)(st.sel_freq+step));
        st.freq_playing=st.sel_freq; tune();
    }
}

static void toggleMr(void)
{
    st.is_mr=!st.is_mr;
    if(configChannel()!=0){ /* no valid channel: stayed VFO */ }
    tune();
    askSave=askDelete=false; inIdx=0;
}

static bool running;

static void cycleBand(void)
{
    st.band=(uint8_t)((st.band+1)&3u); bandLimits();
    if(st.freq_playing<lo||st.freq_playing>hi){ st.freq_playing=lo; st.sel_freq=lo; }
    tune();
}
/* F+key and long-press functions (band / VFO<->MR / auto-scan / quit). */
static void func(uint8_t key)
{
    switch(key){
        case APP_KEY_0:    running=false; break;
        case APP_KEY_1:    cycleBand(); break;
        case APP_KEY_3:    toggleMr(); break;
        case APP_KEY_STAR: startAuto(); break;
        default: break;
    }
}
static void onShort(uint8_t key)
{
    if(key==APP_KEY_F){ fArm=!fArm; return; }
    if(fArm){ fArm=false; func(key); return; }        /* F + key */
    if(key<=APP_KEY_9){ digit(key); return; }
    switch(key){
        case APP_KEY_UP:
        case APP_KEY_DOWN: upDown(A->nav_dir(key)); break;
        case APP_KEY_STAR: startManual(+1); break;
        case APP_KEY_MENU: menu(); break;
        case APP_KEY_EXIT:
            if(inIdx) inIdx--;
            else if(askSave||askDelete) askSave=askDelete=false;
            else running=false;
            break;
        default: break;
    }
}
static void onLong(uint8_t key){ fArm=false; func(key); }   /* long band / MR / scan / quit */

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api)
{
    A=api;
    ch=A->fm_channels;
    A->fm_state(&st,false);          /* inherit the resident FM state */
    if(st.band>3) st.band=0;
    bandLimits();
    if(st.freq_playing<lo||st.freq_playing>hi) st.freq_playing=lo;
    if(st.sel_freq<lo||st.sel_freq>hi) st.sel_freq=st.freq_playing;
    if(st.is_mr) configChannel();
    askSave=askDelete=fArm=false; inIdx=0; savePos=0;

    A->fm_enter(st.freq_playing,st.band);

    /* short = on release (held < ~0.5 s) · long = at the 0.5 s threshold ·
       UP/DOWN = immediate step on press, then auto-repeat while held. */
    running=true;
    uint8_t  held=APP_KEY_INVALID;
    uint16_t heldMs=0;
    bool     firedLong=false;
    bool     dirty=true;                               /* redraw only on change */

    while(running){
        uint8_t key=A->get_key();
        bool rep=(key==APP_KEY_UP||key==APP_KEY_DOWN);

        if(key==APP_KEY_INVALID){
            if(held!=APP_KEY_INVALID && !firedLong &&
               held!=APP_KEY_UP && held!=APP_KEY_DOWN){
                onShort(held); dirty=true;
            }
            held=APP_KEY_INVALID; heldMs=0; firedLong=false;
        } else if(key!=held){
            held=key; heldMs=0; firedLong=false;
            if(rep){ onShort(key); dirty=true; }       /* immediate first step */
        } else {
            heldMs=(uint16_t)(heldMs+50u);
            if(rep){
                if(heldMs>=300){ onShort(held); dirty=true; }   /* auto-repeat */
            } else if(!firedLong && heldMs>=400){
                firedLong=true; onLong(held); dirty=true;
            }
        }

        /* non-blocking scan: one step each time the settle timer expires */
        if(scanState){
            if(scanTimer<=50) scanStep();
            else scanTimer=(uint16_t)(scanTimer-50);
            dirty=true;
        }

        if(dirty){ show(); dirty=false; }
        A->delay_ms(50);
    }

    /* push our state back to the resident FM and persist (config + 48 channels) */
    if(!st.is_mr) st.sel_freq=st.freq_playing;
    A->fm_state(&st,true);
    A->fm_commit();
    A->fm_exit();
}
