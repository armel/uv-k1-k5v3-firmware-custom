/* Copyright 2026 Armel F4HWN
 * SPDX-License-Identifier: Apache-2.0
 *
 * Triple VFO — modal overlay receiver/transmitter. A and B are the resident
 * Main Display VFOs; C is an app-persistent memory channel. The resident ABI
 * performs all RF sequencing while this blob owns the seven-line Tiny UI.
 */
#include <stdint.h>
#include <stdbool.h>
#include "../app_api.h"

#define TICK_MS       20u
#define LONG_MS      400u
#define CFG_MAGIC    0xC3u
#define VFO_COUNT       3u
#define MR_MAX       1024u
#define SCOPE_SAMPLES   43u
#define SCOPE_FLOOR    200u

static const app_api_t *A;
static app_trivfo_info_t vi[VFO_COUNT];
static uint8_t selected, state;
static uint16_t cChannel;
static bool running, fArm, txDenied, channelLabelOn, showFrequency;
static uint16_t scopeBuf[SCOPE_SAMPLES], scopeFloor;
static uint8_t scopeWrite, scopeReady, batteryTicks;
static bool scopeTx;
static char text[16];

/* Resident status.c uses this exact 8-column inverted glyph at x=69.  Keep
 * the overlay copy byte-for-byte identical so F has the same position and
 * polarity as every other firmware screen. */
static const uint8_t fontF[8] = {
    0x7f, 0x00, 0x76, 0x76, 0x76, 0x76, 0x7e, 0x7f
};

static uint8_t slen(const char *s){ uint8_t n=0; while(s[n])n++; return n; }
static bool copyName(char *d,const char *s){
    uint8_t n=0;
    while(n<10u){
        uint8_t c=(uint8_t)s[n];
        if(c==0u||c==0xffu) break;
        d[n++]=(char)c;
    }
    while(n>0u&&d[n-1u]==' ') n--;
    d[n]='\0';
    return n>0u;
}
static char *put(char *o,const char *s){ while(*s)*o++=*s++; return o; }
static char *putu(char *o,uint32_t v){ char t[10]; int8_t n=0; do{t[n++]=(char)('0'+v%10u);v/=10u;}while(v); while(n--)*o++=t[n]; return o; }
static char *puti(char *o,int16_t v){ if(v<0){*o++='-';v=(int16_t)-v;} return putu(o,(uint16_t)v); }
static char *put3(char *o,uint16_t v){ *o++=(char)('0'+(v/100u)%10u); *o++=(char)('0'+(v/10u)%10u); *o++=(char)('0'+v%10u); return o; }
static char *put4(char *o,uint16_t v){ *o++=(char)('0'+(v/1000u)%10u); return put3(o,(uint16_t)(v%1000u)); }

static void formatFreq(char *s,uint32_t f){
    char *o=putu(s,f/100000u); *o++='.'; f%=100000u;
    uint32_t p=10000u; while(p){ *o++=(char)('0'+(f/p)%10u); p/=10u; } *o='\0';
}
static void formatChannel(char *s,const app_trivfo_info_t *v){
    char *o=s;
    if(v->channel<MR_MAX) o=put4(o,(uint16_t)(v->channel+1u));
    else { *o++='F'; o=putu(o,(uint16_t)(v->channel-MR_MAX+1u)); }
    *o='\0';
}
static void formatStep(char *s,uint16_t step){
    char *o=putu(s,step/100u);
    if(step%100u){ *o++='.'; *o++=(char)('0'+(step/10u)%10u); *o++=(char)('0'+step%10u); }
    *o++='K'; *o='\0';
}
static void formatCode(char *s,const app_trivfo_info_t *v){
    char *o=s;
    if(v->code_type==1u){ o=putu(o,v->code_value/10u); *o++='.'; *o++=(char)('0'+v->code_value%10u); }
    else if(v->code_type==2u||v->code_type==3u){
        uint16_t n=v->code_value; *o++=(char)('0'+((n>>6)&7u)); *o++=(char)('0'+((n>>3)&7u)); *o++=(char)('0'+(n&7u)); *o++=(v->code_type==2u?'N':'I');
    } else return formatStep(s,v->step);
    *o='\0';
}

static void drawMeter(const app_trivfo_info_t *v){
    int16_t dbm=v->rssi_dbm; if(dbm>-53)dbm=-53;
    uint8_t s=0,over=0;
    if(dbm>=-93){ s=9; over=(uint8_t)(dbm+93); if(over>40)over=40; }
    else if(dbm>=-141) s=(uint8_t)((dbm+147)/6);

    char *o=text; if(dbm>-100)*o++=' '; o=puti(o,dbm); o=put(o," dBm"); *o='\0';
    A->print_tiny(text,2,1,false,true);
    if(over){ o=text; *o++='+'; if(over<10)*o++='0'; o=putu(o,over); }
    else { o=text; *o++='S'; o=putu(o,s); }
    *o='\0'; A->print_normal(text,38,0,0);

    uint8_t level=(uint8_t)(s+over/10u); if(level>13u)level=13u;
    for(uint8_t i=0;i<level;i++){
        uint8_t *p=&A->fb[0][62u+i*5u];
        p[0]=0x3e; p[1]=(i<9u)?0x3e:0x22; p[2]=(i<9u)?0x3e:0x22; p[3]=0x3e;
    }
}

static void drawAudioScope(void){
    if(!scopeTx){
        for(uint8_t i=0;i<SCOPE_SAMPLES;i++) scopeBuf[i]=SCOPE_FLOOR;
        scopeFloor=SCOPE_FLOOR; scopeWrite=scopeReady=0; scopeTx=true;
    }

    if(scopeReady>=7u) scopeBuf[scopeWrite]=A->bk_read(0x64u);
    else scopeReady++;
    if(scopeBuf[scopeWrite]==0u) scopeBuf[scopeWrite]=SCOPE_FLOOR;
    scopeWrite=(uint8_t)((scopeWrite+1u)%SCOPE_SAMPLES);

    uint16_t min=scopeBuf[0], max=scopeBuf[0];
    for(uint8_t i=1;i<SCOPE_SAMPLES;i++){
        if(scopeBuf[i]<min) min=scopeBuf[i];
        if(scopeBuf[i]>max) max=scopeBuf[i];
    }
    if(scopeFloor>min) scopeFloor=(uint16_t)(scopeFloor-((scopeFloor-min)>>3)-1u);
    else scopeFloor=(uint16_t)(scopeFloor+2u);
    uint16_t range=max>scopeFloor?(uint16_t)(max-scopeFloor):0u;

    for(uint8_t i=0;i<SCOPE_SAMPLES;i++){
        uint8_t idx=(uint8_t)((scopeWrite+i)%SCOPE_SAMPLES), height=0;
        if(range>=50u){
            uint16_t value=scopeBuf[idx]>scopeFloor?(uint16_t)(scopeBuf[idx]-scopeFloor):0u;
            height=(uint8_t)((uint32_t)value*7u/range);
        }
        uint8_t mask=height?(uint8_t)((0x7fu<<(7u-height))&0x7fu):0x40u;
        A->fb[0][i*3u]=mask; A->fb[0][i*3u+1u]=mask;
    }
}

static void drawVfo(uint8_t n){
    const app_trivfo_info_t *v=&vi[n];
    uint8_t mainLine=(uint8_t)(n*2u+1u), techLine=(uint8_t)(mainLine+1u);
    uint8_t techY=(uint8_t)(techLine*8u+1u);

    formatChannel(text,v);
    if(!(v->flags&APP_TRIVFO_RECEIVING)||channelLabelOn)
        A->print_inverse(text,2,mainLine,false,true,(uint8_t)(slen(text)*4u+3u));
    if(n==selected && txDenied)
        A->print_tiny("TX DISABLE",22,(uint8_t)(mainLine*8u+1u),false,true);
    else {
        bool useName=false;
        if(v->channel<MR_MAX&&!showFrequency) useName=copyName(text,v->name);
        if(!useName) formatFreq(text,v->frequency);
        if(n==selected)
            A->print_bold(text,22,0,mainLine);
        else
            A->print_normal(text,22,0,mainLine);
    }

    const char *mod=v->modulation==0u?"FM":v->modulation==1u?"AM":v->modulation==2u?"USB":v->modulation==3u?"BYP":v->modulation==4u?"RAW":"?";
    A->print_tiny(mod,3,techY,false,true);

    static const char *const power[7]={"LOW1","LOW2","LOW3","LOW4","LOW5","MID","HIGH"};
    uint8_t p=(v->power>=1u&&v->power<=7u)?(uint8_t)(v->power-1u):0u;
    A->print_tiny(power[p],24,techY,false,true);
    if(v->flags&APP_TRIVFO_USER_POWER){
        A->fb[techLine][19]=0x3e; A->fb[techLine][20]=0x1c; A->fb[techLine][21]=0x08;
    }

    if(v->offset_direction==1u) A->print_normal("+",41,0,techLine);
    else if(v->offset_direction==2u) A->print_normal("-",41,0,techLine);
    if(v->reverse) A->print_tiny("R",51,techY,false,true);

    if(v->code_type==1u){ A->print_tiny("CT",58,techY,false,true); formatCode(text,v); A->print_tiny(text,68,techY,false,true); }
    else if(v->code_type==2u||v->code_type==3u){ A->print_tiny("DC",58,techY,false,true); formatCode(text,v); A->print_tiny(text,68,techY,false,true); }
    else { formatStep(text,v->step); A->print_tiny(text,58,techY,false,true); }

    A->print_tiny(v->bandwidth==0u?"WIDE":v->bandwidth==1u?"NAR":"NAR+",91,techY,false,true);
    text[0]='S'; text[1]='Q'; text[2]='L'; text[3]=(char)('0'+(v->squelch%10u)); text[4]='\0';
    A->print_tiny(text,110,techY,false,true);
}

static void draw(void){
    for(uint8_t i=0;i<VFO_COUNT;i++) {
        A->trivfo_get(i,&vi[i]);
        /* The resident scanner can temporarily select the VFO carrying the
         * reception.  Mirror that selection locally so the bold main field
         * follows it immediately. */
        if(vi[i].flags&APP_TRIVFO_SELECTED) selected=i;
    }
    A->display_clear(); A->status_clear();
    /* Keep the application title in the resident upper-left position. */
    A->draw_battery();
    A->print_inverse("TRIPLE VFO",2,0,true,true,42);
    if(fArm){ for(uint8_t i=0;i<8u;i++) A->status_line[69u+i]=fontF[i]; }
    for(uint8_t i=0;i<VFO_COUNT;i++) drawVfo(i);
    if(state==APP_TRIVFO_TX_STATE&&(vi[selected].flags&APP_TRIVFO_AUDIO_BAR))
        drawAudioScope();
    else {
        scopeTx=false;
        for(uint8_t i=0;i<VFO_COUNT;i++)
            if(vi[i].flags&APP_TRIVFO_RECEIVING){ drawMeter(&vi[i]); break; }
    }
    A->blit_status(); A->blit_full();
}

static void selectNext(void){ selected=(uint8_t)((selected+1u)%VFO_COUNT); A->trivfo_select(selected); }
static void toggleNameFrequency(void){ showFrequency=!showFrequency; }
static void stepSelected(uint8_t key){ int8_t d=A->nav_dir(key); uint16_t ch=A->trivfo_step(selected,d); if(selected==2u&&ch!=0xFFFFu)cChannel=ch; }

static void loadCfg(void){ uint8_t c[4]; A->cfg_load(c,4); cChannel=(c[0]==CFG_MAGIC)?(uint16_t)(c[1]|((uint16_t)c[2]<<8)):0xFFFFu; }
static void saveCfg(void){ uint8_t c[3]={CFG_MAGIC,(uint8_t)cChannel,(uint8_t)(cChannel>>8)}; A->cfg_save(c,3); }

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api){
    A=api; running=true; fArm=false; txDenied=false; channelLabelOn=true; showFrequency=false; selected=scopeWrite=scopeReady=batteryTicks=0; scopeTx=false; scopeFloor=SCOPE_FLOOR; loadCfg();
    cChannel=A->trivfo_enter(cChannel); A->trivfo_select(0); A->backlight_on();

    uint8_t held=APP_KEY_INVALID, blinkTicks=0; uint16_t heldMs=0; bool longDone=false, ptt=false;
    while(running){
        uint8_t key=A->get_key();
        if(key==APP_KEY_PTT){
            if(!ptt){ ptt=true; txDenied=A->trivfo_ptt(true)!=0; }
        } else if(ptt){ ptt=false; A->trivfo_ptt(false); txDenied=false; }

        if(key==APP_KEY_INVALID||key==APP_KEY_PTT){
            if(held!=APP_KEY_INVALID&&!longDone){
                if(held==APP_KEY_F) fArm=!fArm;
                else if(held==APP_KEY_1&&fArm){ fArm=false; toggleNameFrequency(); }
                else if(held==APP_KEY_2&&fArm){ fArm=false; selectNext(); }
                else if(held==APP_KEY_EXIT) running=false;
            }
            held=APP_KEY_INVALID; heldMs=0; longDone=false;
        } else if(key!=held){
            held=key; heldMs=0; longDone=false;
            if(key==APP_KEY_UP||key==APP_KEY_DOWN){ stepSelected(key); longDone=true; }
        } else {
            heldMs=(uint16_t)(heldMs+TICK_MS);
            if((key==APP_KEY_UP||key==APP_KEY_DOWN)&&heldMs>=300u){ stepSelected(key); }
            else if(!longDone&&heldMs>=LONG_MS){
                longDone=true;
                /* Long-1 toggles name/frequency and Long-2 selects the next
                 * VFO; both are deliberately independent of F. */
                if(key==APP_KEY_1){ fArm=false; toggleNameFrequency(); }
                else if(key==APP_KEY_2){ fArm=false; selectNext(); }
            }
        }

        state=A->trivfo_tick();
        if(state==APP_TRIVFO_RX){
            if(++blinkTicks>=25u){ blinkTicks=0; channelLabelOn=!channelLabelOn; }
        } else { blinkTicks=0; channelLabelOn=true; }
        draw();
        /* Match MAIN: no loaded-voltage samples during TX, then allow one
         * second for the pack to recover before refreshing the four-sample
         * battery average. */
        if(state==APP_TRIVFO_TX_STATE) batteryTicks=0;
        else if(++batteryTicks>=50u){ batteryTicks=0; A->battery_sample(); }
        A->delay_ms(TICK_MS); A->backlight_update();
    }
    if(ptt)
        A->trivfo_ptt(false);
    saveCfg();
    A->trivfo_leave();
}
