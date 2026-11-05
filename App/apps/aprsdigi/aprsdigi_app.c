/* Copyright 2026 Armel F4HWN
 * Licensed under the Apache License, Version 2.0.
 */

/* APRS DIGI - a self-contained WIDE1-1 / WIDE2-1 digipeater.
 *
 * The receiver is the APRS RX Bell 202 software demodulator. A valid AX.25 UI
 * frame is eligible when WIDE1-1 is its first unused path address, or WIDE2-1
 * when the extended mode is selected. That address is replaced in place by
 * the boot callsign with SSID 3 and the H bit, then the FCS is rebuilt and the
 * APRS TX tone generator sends it. Logical packet hashes suppress duplicates
 * for 30 seconds, ignoring path and FCS.
 *
 * Set a non-zero squelch: transmission waits for 100-227 ms of closed squelch
 * and is abandoned after 3 seconds. EXIT stops the app. The loader restores
 * the resident radio configuration on return.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "aprsdigi_assets.h"

/* PA4 audio capture and the 48 MHz cycle clock are resident (API level 3);
 * the capture holds PA4 at mid-scale (ADC code 2048). */
#define BIAS_CODE      2048u
#define FS             9600u
#define CYC_PER_SAMPLE (48000000u / FS)
#define CYC_PER_BIT    (48000000u / 1200u)
#define HOUSE_EVERY    480u
#define BUSY_MAX       60u
#define REFRESH_SLOTS  20u

#define BP_B0          5356
#define BP_A1        (-10714)
#define BP_A2          5673
#define AGC_ATT        5
#define AGC_DEC        10
#define PK_MIN         16
#define PLL_STEP       8192
#define PLL_CTR        36864
#define PLL_SHIFT      2
#define NSL            3u
#define PRE_BITS       24u

#define FRAME_MAX      330u
#define FRAME_MIN      18u
#define FCS_GOOD       0xF0B8u
#define OWN_SSID       3u
#define DUPE_SLOTS     8u
#define DUPE_MS        30000u
#define HOLD_FREE_MIN  100u
#define HOLD_MAX_MS    3000u
#define SLICER_DUP_MS  50u

#define REG_51         0x51u
#define REG_70         0x70u
#define REG_71         0x71u
#define REG_0C         0x0Cu
#define F_MARK         12389u
#define F_SPACE        22714u
#define TONE_LEVEL     66u
#define TXDELAY_FLAGS  40u
#define TAIL_FLAGS     3u

enum { ST_WAIT=0, ST_HOLD, ST_TX, ST_DENIED, ST_NOCALL };

typedef struct {
    int32_t dc, x1, x2, y1, y2, im, qm, is, qs, pm, ps;
    uint32_t r, ks;
    int16_t ring[8][4];
    int8_t cm[8], cs[48];
} dem_t;

_Static_assert(COS2200==COS1200+COS1200_LEN &&
               offsetof(dem_t,cm)==DEMOD_INIT_LEN &&
               COS1200==DEMOD_INIT+DEMOD_INIT_LEN &&
               sizeof(dem_t)==DEMOD_INIT_LEN+COS1200_LEN+COS2200_LEN,
               "demodulator assets must match dem_t");

typedef struct {
    int32_t dprev, phase;
    uint16_t n, crc;
    uint8_t wa, wb, last, sr, ones, byte, nb, inframe, hdr, bits;
    bool pre;
    uint8_t buf[FRAME_MAX];
} sl_t;

typedef struct { uint32_t hash, at; } dupe_t;

/* Keep byte data, halfwords, then words in the short Thumb offset ranges. */
static struct {
    uint8_t own[7];
    char call[9];
    uint8_t prevKey, state, refresh, dupeNext;
    bool running, resetRx, space;
    uint8_t ones;
    int16_t rssi;
    uint16_t pendingLen, holdMs, rx, digi, dupes, drops;
    uint8_t mode;
    const app_api_t *A;
    uint8_t *pendingBuf;
    uint32_t pendingAt, freeAt, lastHash, lastAt, next;
    dupe_t seen[DUPE_SLOTS];
} g;

void *memset(void *d,int c,size_t n){
    uint8_t *p=d; while(n--) *p++=(uint8_t)c; return d;
}
void *memcpy(void *d,const void *s,size_t n){
    uint8_t *p=d; const uint8_t *q=s; while(n--) *p++=*q++; return d;
}

static void inc(uint16_t *v){ if(*v<9999u) (*v)++; }

/* The resident cycle clock, for the TX bit timing (the receiver holds the
 * service in a register). */
static uint32_t clkCyc(void){ return g.A->cycles_48mhz(); }

__attribute__((noinline))
static uint16_t crcByte(uint16_t c,uint8_t v){
    c^=v;
    for(uint8_t i=0;i<8u;i++) c=(c&1u)?(uint16_t)((c>>1)^0x8408u):(uint16_t)(c>>1);
    return c;
}

static bool callChar(uint8_t c){
    return (c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' ';
}

/* The caller already guarantees FRAME_MIN and a valid FCS. */
static uint16_t uiEnd(const uint8_t *f,uint16_t n){
    uint16_t end=(uint16_t)(n-2u), e=13u;
    while(!(f[e]&1u) && e+7u<end) e+=7u;
    if(!(f[e]&1u)||e+2u>=end||f[e+1u]!=0x03u||f[e+2u]!=0xF0u) return 0;
    return e;
}

static bool sameCall(const uint8_t *a,const uint8_t *b){
    for(uint8_t i=0;i<6u;i++) if(a[i]!=b[i]) return false;
    return (a[6]&0x1Eu)==(b[6]&0x1Eu);
}

static bool isWide(const uint8_t *a){
    static const uint8_t wide[4]={'W'<<1,'I'<<1,'D'<<1,'E'<<1};
    for(uint8_t i=0;i<4u;i++) if(a[i]!=wide[i]) return false;
    uint8_t n=(uint8_t)(a[4]>>1);
    return a[5]==(' '<<1) && n>='1' && n<=(uint8_t)('1'+g.mode) &&
           ((a[6]>>1)&15u)==1u;
}

/* Hash destination, source, control/PID and info; path and FCS are ignored. */
static uint32_t logicalHash(const uint8_t *f,uint16_t n,uint16_t e){
    uint32_t h=2166136261u;
    for(uint16_t i=0;i<14u;i++){ h^=f[i]; h*=16777619u; }
    for(uint16_t i=(uint16_t)(e+1u);i<n-2u;i++){ h^=f[i]; h*=16777619u; }
    return h?h:1u;
}

static dupe_t *duplicate(uint32_t h,uint32_t now){
    for(uint8_t i=0;i<DUPE_SLOTS;i++)
        if(g.seen[i].hash==h && now-g.seen[i].at<DUPE_MS) return &g.seen[i];
    return NULL;
}
static void remember(uint32_t h,uint32_t now){
    g.seen[g.dupeNext].hash=h;
    g.seen[g.dupeNext].at=now;
    g.dupeNext=(uint8_t)((g.dupeNext+1u)%DUPE_SLOTS);
}

/* Queue an eligible frame after replacing WIDE1-1 or WIDE2-1 by MYCALL-3*. */
static void accept(const sl_t *m){
    const uint8_t *f=m->buf;
    uint16_t e=uiEnd(f,m->n);
    if(!e) return;
    uint32_t now=g.A->ticks_ms();
    uint32_t h=logicalHash(f,m->n,e);
    /* The three slicers often decode the same on-air frame. Count and process
     * it once; this short filter is separate from network duplicate history. */
    if(g.lastHash==h && now-g.lastAt<SLICER_DUP_MS) return;
    g.lastHash=h; g.lastAt=now;
    inc(&g.rx);
    g.rssi=g.A->rssi_dbm();

    uint16_t target=0;
    bool ownUsed=false;
    for(uint16_t p=20u;p<=e;p+=7u){
        const uint8_t *a=f+p-6u;
        if((a[6]&0x80u)&&sameCall(a,g.own)) ownUsed=true;
        if(!(a[6]&0x80u) && !target) target=(uint16_t)(p-6u);
    }
    bool eligible=target&&isWide(f+target)&&!ownUsed;
    /* A path-changing repeat by another digipeater has the same logical hash.
     * Cancel our queued copy. Another copy of the original WIDE1-1 packet is
     * only suppressed: it does not prove that a digipeater already sent it. */
    dupe_t *dupe=duplicate(h,now);
    if(dupe){
        if(g.state==ST_HOLD&&dupe->at==g.pendingAt&&!eligible){
            g.freeAt=0; g.state=ST_WAIT;
        }
        inc(&g.dupes);
        return;
    }
    if(!eligible){ inc(&g.drops); return; }

    if(g.state==ST_HOLD){ inc(&g.drops); return; }

    remember(h,now);
    memcpy(g.pendingBuf,f,m->n);
    memcpy(g.pendingBuf+target,g.own,6u);
    g.pendingBuf[target+6u]=(uint8_t)((f[target+6u]&1u)|0xE0u|(OWN_SSID<<1));
    uint16_t c=0xFFFFu;
    for(uint16_t i=0;i<m->n-2u;i++) c=crcByte(c,g.pendingBuf[i]);
    c^=0xFFFFu;
    g.pendingBuf[m->n-2u]=(uint8_t)c;
    g.pendingBuf[m->n-1u]=(uint8_t)(c>>8);
    g.pendingLen=m->n;
    g.pendingAt=now;
    g.holdMs=(uint16_t)(HOLD_FREE_MIN+(g.A->rand32()&127u));
    g.freeAt=0;
    g.state=ST_HOLD;
    g.refresh=REFRESH_SLOTS;
}

__attribute__((noinline))
static void hdlcBit(sl_t *m,uint8_t b){
    if(m->bits<255u) m->bits++;
    m->sr=(uint8_t)((m->sr>>1)|(b<<7));
    if(m->sr==0x7Eu){
        bool ok=m->inframe&&m->n>=FRAME_MIN&&m->crc==FCS_GOOD;
        m->pre=m->bits==8u;
        m->bits=0;
        if(ok) accept(m);
        m->inframe=1; m->hdr=1; m->n=0; m->crc=0xFFFFu;
        m->ones=m->byte=m->nb=0;
        return;
    }
    if(b){
        if(++m->ones>6u){ m->ones=7u; m->inframe=0; return; }
    }else{
        if(m->ones==5u){ m->ones=0; return; }
        m->ones=0;
    }
    if(!m->inframe) return;
    m->byte|=(uint8_t)(b<<m->nb);
    if(++m->nb==8u){
        if(m->n<FRAME_MAX){
            m->crc=crcByte(m->crc,m->byte);
            if(m->n<6u&&((m->byte&1u)||!callChar((uint8_t)(m->byte>>1)))) m->hdr=0;
            m->buf[m->n++]=m->byte;
        }else m->inframe=0;
        m->byte=m->nb=0;
    }
}

static bool busy(const sl_t *m){
    return (m->pre&&m->bits<PRE_BITS)||(m->inframe&&m->n&&m->hdr);
}

static int32_t mag(int32_t i,int32_t q){
    if(i<0) i=-i;
    if(q<0) q=-q;
    if(i<q){ int32_t t=i; i=q; q=t; }
    return i+(q>>2)+(q>>3);
}

__attribute__((noinline))
static int32_t tone(int32_t i,int32_t q,int32_t *pk){
    int32_t mt=mag(i,q),p=*pk;
    p+=mt>p?(mt-p)>>AGC_ATT:-(p>>AGC_DEC);
    if(p<PK_MIN) p=PK_MIN;
    *pk=p;
    return mt;
}

static void front(dem_t *m,int32_t adc,int32_t *a,int32_t *b){
    m->dc+=((adc<<4)-m->dc)>>6;
    int32_t x=adc-(m->dc>>4);
    int32_t y=(BP_B0*(x-m->x2)-BP_A1*m->y1-BP_A2*m->y2)>>14;
    m->x2=m->x1; m->x1=x; m->y2=m->y1; m->y1=y;
    int16_t *o=m->ring[m->r];
    uint32_t k=m->ks+12u; if(k>=48u) k-=48u;
    int32_t p;
    p=(y*m->cm[m->r])>>8;         m->im+=p-o[0]; o[0]=(int16_t)p;
    p=(y*m->cm[(m->r+6u)&7u])>>8; m->qm+=p-o[1]; o[1]=(int16_t)p;
    p=(y*m->cs[m->ks])>>8;        m->is+=p-o[2]; o[2]=(int16_t)p;
    p=(y*m->cs[k])>>8;            m->qs+=p-o[3]; o[3]=(int16_t)p;
    m->r=(m->r+1u)&7u;
    if(++m->ks>=48u) m->ks=0;
    int32_t mm=tone(m->im,m->qm,&m->pm),ms=tone(m->is,m->qs,&m->ps);
    *a=mm*m->ps; *b=ms*m->pm;
}

static void slice(sl_t *m,int32_t a,int32_t b){
    int32_t d=a*m->wa-b*m->wb;
    m->phase+=PLL_STEP;
    if((d>0)!=(m->dprev>0)) m->phase+=(PLL_CTR-m->phase)>>PLL_SHIFT;
    m->dprev=d;
    if(m->phase>=65536){
        m->phase-=65536;
        uint8_t lv=(uint8_t)(d>0);
        hdlcBit(m,(uint8_t)(lv==m->last));
        m->last=lv;
    }
}

static void setTone(void){
    g.A->bk_write(REG_71,(uint16_t)(g.space?F_SPACE:F_MARK));
    g.A->bk_write(REG_70,(uint16_t)(0x8000u|(TONE_LEVEL<<8)));
}
static void sendBit(uint8_t b){
    while((int32_t)(clkCyc()-g.next)<0){}
    if(!b){ g.space=!g.space; setTone(); }
    g.next+=CYC_PER_BIT;
}
__attribute__((noinline))
static void sendByte(uint8_t v,bool stuff){
    for(uint8_t i=0;i<8u;i++){
        uint8_t b=v&1u; v>>=1; sendBit(b);
        if(stuff){
            if(!b) g.ones=0;
            else if(++g.ones==5u){ sendBit(0); g.ones=0; }
        }
    }
}

static void send(void){
    const app_api_t *A=g.A;
    if(A->tx_state()){
        g.state=ST_DENIED; inc(&g.drops); return;
    }
    g.state=ST_TX;
    A->tx_set_params();
    A->bk_write(REG_51,0);
    A->tx_tone(1200);
    g.space=false; setTone();
    g.next=clkCyc()+CYC_PER_BIT;
    for(uint8_t i=0;i<TXDELAY_FLAGS;i++) sendByte(0x7Eu,false);
    g.ones=0;
    for(uint16_t i=0;i<g.pendingLen;i++) sendByte(g.pendingBuf[i],true);
    for(uint8_t i=0;i<TAIL_FLAGS;i++) sendByte(0x7Eu,false);
    while((int32_t)(clkCyc()-g.next)<0){}
    A->tx_mute(true);
    A->tx_end();
    /* Let the restored RX synthesizer and discriminator settle before decode. */
    A->set_af(APP_AF_FM);
    A->delay_ms(50);
    g.freeAt=0; g.state=ST_WAIT; g.resetRx=true;
    inc(&g.digi);
}

/* The resident formatter (API level 3). One local stub keeps every call a
 * plain bl: the service lies past the ldr immediate range of the table. */
__attribute__((noinline))
static char *puti(char *o,int32_t v){ return g.A->append_i32(o,v); }
#define putu puti
static char *text(uint16_t off,char *s){ g.A->asset_read(off,s,TEXT_MAX); return s; }
static void field(uint8_t x,uint8_t line,uint16_t label,uint16_t value,char *s){
    char *o=s; text(label,s); while(*o) o++;
    char *v=o; o=putu(o,value); *o='\0';
    g.A->print_normal(s,x,0,line);
    g.A->print_bold(v,(uint8_t)(x+(uint8_t)(v-s)*7u),0,line);
}
static void draw(void){
    const app_api_t *A=g.A;
    char s[TEXT_MAX+5u];
    static const uint8_t states[]={T_WAIT,T_HOLD,T_TX,T_DENIED,T_NOCALL};
    A->display_clear(); A->status_clear();
    A->print_inverse(text(T_TITLE,s),2,0,true,true,(uint8_t)(2u+T_TITLE_CHARS*4u));
    text(states[g.state],s);
    uint8_t n=0; while(s[n]) n++;
    A->print_inverse(s,43,0,true,true,(uint8_t)(43u+n*4u));
    if(g.state!=ST_NOCALL){
        s[0]='W'; s[1]='1'; n=2u;
        if(g.mode) s[n++]='2';
        s[n]='\0';
        A->print_inverse(s,70,0,true,true,(uint8_t)(70u+n*4u));
    }
    A->draw_battery();
    A->print_bold(g.call,0,0,0);
    field(0,1,T_RX,g.rx,s);
    field(65,1,T_DIGI,g.digi,s);
    field(0,2,T_DUPE,g.dupes,s);
    field(65,2,T_DROP,g.drops,s);
    uint8_t *b=A->fb[0];
    for(uint8_t x=0;x<128u;x+=2u){ b[384u+x]|=0x80u; b[385u+x]&=0x7Fu; }
    g.A->draw_frequency(A->rx_freq(),0,4);   /* the main screen's big digits, lines 4-5 */
    if(g.rx){
        char *o=puti(s,g.rssi);
        A->asset_read(T_DBM,o,T_DBM_LEN);
        A->print_tiny(s,0,49,false,true);
    }
    A->blit_status(); A->blit_full();
}

static void house(void){
    const app_api_t *A=g.A;
    uint8_t key=A->get_key();
    if(key!=APP_KEY_INVALID&&key!=g.prevKey){
        A->backlight_on();
        if(key==APP_KEY_EXIT) g.running=false;
        else if(key==APP_KEY_1){ g.mode^=1u; g.refresh=REFRESH_SLOTS; }
    }
    g.prevKey=key;
    if(!g.running) return;

    uint32_t now=A->ticks_ms();
    if(g.state==ST_HOLD){
        if(now-g.pendingAt>=HOLD_MAX_MS){
            g.freeAt=0; g.state=ST_WAIT; inc(&g.drops);
        }else if(!(A->bk_read(REG_0C)&2u)){
            if(!g.freeAt) g.freeAt=now;
            else if(now-g.freeAt>=g.holdMs) send();
        }else g.freeAt=0;
    }

    A->battery_sample();         /* yields the capture's ADC itself */
    if(++g.refresh>=REFRESH_SLOTS){ g.refresh=0; draw(); }
    A->backlight_update();
}

__attribute__((noinline))
static void listen(void){
    dem_t d;
    sl_t sl[NSL];
reset:
    memset(sl,0,sizeof sl);
    g.A->asset_read(DEMOD_INIT,&d,sizeof d);
    sl[0].wa=2; sl[0].wb=3;
    sl[1].wa=1; sl[1].wb=1;
    sl[2].wa=3; sl[2].wb=2;
    g.resetRx=false;
    /* The two per-sample services, held in registers: their table slots lie
     * past the ldr immediate range. */
    uint32_t (*const clk)(void)=g.A->cycles_48mhz;
    uint16_t (*const sample)(void)=g.A->af_capture_sample;
    uint32_t next=clk();
    uint16_t cnt=0;
    uint8_t busyFor=0;
    while(g.running){
        while((int32_t)(clk()-next)<0){}
        next+=CYC_PER_SAMPLE;
        int32_t a,b;
        front(&d,sample(),&a,&b);
        for(uint8_t k=0;k<NSL;k++) slice(&sl[k],a,b);
        if(++cnt<HOUSE_EVERY) continue;
        cnt=0;
        bool bz=false;
        for(uint8_t k=0;k<NSL;k++) bz|=busy(&sl[k]);
        if(bz&&++busyFor<BUSY_MAX) continue;
        busyFor=0;
        house();
        if(g.resetRx) goto reset;
        next=clk();
    }
}

static bool makeOwn(const app_api_t *A){
    char raw[12];
    A->boot_callsign(raw,sizeof raw);
    uint8_t n=0;
    while(raw[n]){
        if(n>=6u||!callChar((uint8_t)raw[n])||raw[n]==' ') return false;
        g.call[n]=raw[n]; g.own[n]=(uint8_t)(raw[n]<<1); n++;
    }
    if(!n) return false;
    uint8_t len=n;
    while(n<6u) g.own[n++]=(uint8_t)(' '<<1);
    g.own[6]=(uint8_t)(0x60u|(OWN_SSID<<1));
    g.call[len++]='-'; g.call[len++]='3'; g.call[len]='\0';
    return true;
}

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api){
    uint8_t pending[FRAME_MAX];
    g.A=api; g.pendingBuf=pending; g.prevKey=APP_KEY_INVALID;
    api->cfg_load(&g.mode,1u);
    g.mode=(uint8_t)(g.mode==1u);
    api->af_capture_open();      /* PA4 bias and ADC channel 4, restored by close */
    api->set_af(APP_AF_FM);
    api->delay_ms(50);
    api->backlight_on();
    g.running=true;
    if(!makeOwn(api)){
        g.state=ST_NOCALL; draw();
        while(g.running){
            uint8_t key=api->get_key();
            if(key==APP_KEY_EXIT&&key!=g.prevKey) g.running=false;
            g.prevKey=key; api->delay_ms(50); api->backlight_update();
        }
    }else{
        g.state=ST_WAIT; draw(); listen();
    }
    api->af_capture_close();
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
    api->cfg_save(&g.mode,1u);
}
