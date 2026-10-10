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

/* ACARS RX - VHF ACARS (AM-MSK, 2400 bit/s) receive-only overlay.
 *
 * The 1200/2400 Hz MSK audio is sampled from PA4 at 19.2 kHz. A complex
 * 1800 Hz mixer and a half-sine matched filter feed a carrier PLL and a
 * data-aided bit clock. The 0xff pre-key pulls the PLL onto the sender's tones,
 * whatever the +/-1 % error of the MCU's RC oscillator; the receiver then slides
 * bit by bit onto SYN SYN, in either polarity, and expects SOH. Odd parity and
 * the complete 16-bit BCS are checked before a message reaches the history. One
 * or two parity-marked bit errors are corrected only when the corrected frame
 * also has a valid BCS.
 *
 * Keys: UP/DOWN scroll and browse history, * normal/compact, F decoded/raw,
 * 1 speaker, 2 clear, EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "acarsrx_assets.h"

static inline volatile uint32_t *hw(uint32_t a){
    volatile uint32_t *p=(volatile uint32_t *)a;
    __asm__("" : "+l"(p));
    return p;
}

/* ---- MCU registers (PY32F071, SysTick at 48 MHz) ---- */
#define SYST_LOAD   (*(volatile uint32_t *)0xE000E014u)
#define SYST_VAL    (*(volatile uint32_t *)0xE000E018u)
#define ADC_SR      (hw(0x40012400u)[0x00/4])
#define ADC_CR2     (hw(0x40012400u)[0x08/4])
#define ADC_SMPR3   (hw(0x40012400u)[0x14/4])
#define ADC_SQR3    (hw(0x40012400u)[0x38/4])
#define ADC_DR      (hw(0x40012400u)[0x50/4])
#define GPIOA_MODER (*(volatile uint32_t *)0x50000000u)
#define DAC_CR      (hw(0x40007400u)[0x00/4])
#define DAC_SWTRIGR (hw(0x40007400u)[0x04/4])
#define DAC_DHR12R1 (hw(0x40007400u)[0x08/4])
#define RCC_APBENR1 (*(volatile uint32_t *)0x4002103Cu)
#define RCC_DACEN   (1u << 29)
#define DAC_CR_BIAS ((1u << 0) | (1u << 1) | (1u << 2) | (7u << 3))
#define BIAS_CODE   2048u
#define ADC_SR_EOC  (1u << 1)
#define ADC_START   ((1u << 22) | (1u << 20))
#define SMP8_POS    24u
#define SMP4_POS    12u
#define ADC_CH_PA4  4u

/* ---- sample clock and MSK PLL ---- */
#define FS              19200u
#define SAMPLE_CYC      (48000000u / FS)
#define HOUSE_EVERY     960u       /* 50 ms */
#define BUSY_MAX        60u        /* never starve keys for more than 3 s */
#define RF_HOLD_SLOTS   20u        /* one second without SPI polling */
#define REFRESH_SLOTS   20u        /* diagnostics and battery every second */
#define PHASE_NOMINAL   6144u      /* 1800 / 19200 of a 16-bit turn */
#define BIT_PHASE       49152      /* 3/4 turn = one 2400 bit/s symbol */
#define PLL_I_LIMIT     (192 * 16)
#define ACQ_GAIN        24         /* phase gain multiplier before the text */
#define ACQ_I           3          /* integrator step per unit of phase step */
#define TRACK_I         8          /* integrator step once in the text */
#define TIMING_ACQ      1024       /* bit clock step before the text, 1/6 sample */
#define TIMING_TRACK    256
#define PREKEY_LEAK     3          /* a bit against the run costs three with it */
#define SYNC_WORD       0x1616u    /* SYN SYN in the last 16 bits */
#define SYNC_INVERTED   0xE9E9u

/* ---- ACARS frame ---- */
#define FRAME_MAX       240u
#define MESSAGE_MAX     226u       /* 240 - fixed 13-byte ACARS header - ETX */
#define HISTORY         3u
#define BODY_CHARS      18u
#define BODY_ROWS       3u
#define COMPACT_EXTRA   14u
#define VIEW_COMPACT    1u

#define WAIT_CAPS_X     40u
#define SPK_X           59u
#define CAPS_END        127u

#define REDRAW_ON       1u
#define REDRAW_FRAME    2u

enum { PREKEY, SOH1, TEXT, CRC1, CRC2, END };
enum { APP_UNKNOWN, APP_TERMINAL, APP_ARINC622, APP_LINK, APP_MEDIA, APP_MIAM };

typedef struct {
    char address[8];
    char label[3];
    char flight[7];
    char text[MESSAGE_MAX+1u];
    char sublabel[3], mfi[3];
    uint8_t textLen, appAt, down;
    int16_t rssi;
} message_t;

typedef struct {
    int32_t i, q;
} iq_t;

typedef struct {
    iq_t ring[16];
    int32_t dc;
    int32_t pllI;
    int32_t clock, xq;      /* bit clock; quadrature rail of the last decision */
    uint32_t phase;
    uint8_t ringAt, symbol;
    int8_t cosine[64];
    uint8_t taps[16];
} dem_t;

static struct {
    uint8_t prevKey, redraw;
    bool running;
    uint8_t spk, view, raw, refresh, busyFor, audioHold, rfHold, audioOn; /* persistent bytes first: speaker, view */
    uint8_t state, bits, inv;
    int8_t prekey;
    uint8_t len, parityErrors, parityAt[2];
    uint8_t count, cur, top, lim;
    uint16_t shift, adcMin, adcMax, ppNow, ppShown;
    uint16_t syncs, crcFails, parityFails, corrected, nOk;
    uint8_t crcBytes[2];
    uint8_t *frame;
    dem_t *dem;
    message_t **history;
    const app_api_t *A;
    uint32_t tPrev, tCycles, savedSqr3, savedSmpr3;
} g;

/* GCC may turn simple freestanding loops into these calls. */
void *memset(void *d,int c,size_t n){ uint8_t *p=d; while(n)p[--n]=(uint8_t)c; return d; }
void *memcpy(void *d,const void *s,size_t n){
    uint8_t *p=d; const uint8_t *q=s; while(n){ --n; p[n]=q[n]; } return d;
}

/* ---- formatting ---- */
static char *put(char *o,const char *s){ while(*s)*o++=*s++; return o; }
static unsigned sub(uint32_t *v,uint32_t d){ unsigned q=0; while(*v>=d){ *v-=d; q++; } return q; }
static const uint16_t P10[]={10000u,1000u,100u,10u,1u};
static char *puti(char *o,int32_t v){
    uint32_t u;
    if(v<0){ *o++='-'; u=(uint32_t)(-v); } else u=(uint32_t)v;
    bool lead=false;
    for(unsigned i=0;i<5u;i++){
        unsigned c=sub(&u,P10[i]);
        if(c||lead||i==4u){ *o++=(char)('0'+c); lead=true; }
    }
    return o;
}

/* Keep the four waiting diagnostics inside one 32-character tiny row. */
static char *putStat(char *o,uint16_t v){
    if(v>99u){ o=puti(o,99); *o++='+'; return o; }
    return puti(o,v);
}

static void drawFreq(uint32_t f,char *text){
    const app_api_t *A=g.A;
    unsigned mhz=0;
    while(f>=100000u){ f-=100000u; mhz++; }
    char *o=puti(text,(int32_t)mhz);
    *o++='.';
    for(unsigned i=0;i<5u;i++) *o++=(char)('0'+sub(&f,P10[i]));
    *o='\0';
    o-=2;
    A->print_normal(o,(uint8_t)((o-text)*13u-10u),0,5);
    *o='\0';
    A->display_freq(text,0,4,false);
}

/* ---- hardware shared with the resident battery ADC ---- */
static void clkStart(void){ g.tPrev=SYST_VAL; g.tCycles=0; }
static uint32_t clkNow(void){
    uint32_t v=SYST_VAL;
    g.tCycles+=(v<=g.tPrev)?g.tPrev-v:g.tPrev+(SYST_LOAD+1u-v);
    g.tPrev=v;
    return g.tCycles;
}

static void adcSelect(void){
    ADC_SMPR3=(g.savedSmpr3&~(7u<<SMP4_POS))|(((g.savedSmpr3>>SMP8_POS)&7u)<<SMP4_POS);
    ADC_SQR3=(g.savedSqr3&~0x1Fu)|ADC_CH_PA4;
}
static void adcRestore(void){ ADC_SQR3=g.savedSqr3; ADC_SMPR3=g.savedSmpr3; }
static uint16_t adcRead(void){
    ADC_CR2|=ADC_START;
    for(uint16_t n=0;!(ADC_SR&ADC_SR_EOC)&&n<2000u;n++){}
    return (uint16_t)(ADC_DR&0x0FFFu);
}

static void biasOn(uint32_t moder,uint32_t rcc){
    GPIOA_MODER=moder|(3u<<8);
    RCC_APBENR1=rcc|RCC_DACEN;
    DAC_CR=DAC_CR_BIAS;
    DAC_DHR12R1=BIAS_CODE;
    DAC_SWTRIGR=1u;
}
static void biasOff(uint32_t dac,uint32_t dhr,uint32_t rcc,uint32_t moder){
    DAC_CR=dac; DAC_DHR12R1=dhr; RCC_APBENR1=rcc; GPIOA_MODER=moder;
}

/* ---- ACARS BCS, CCITT polynomial, bytes and bits in transmitted order ---- */
static uint16_t crcAdd(uint16_t crc,uint8_t byte){
    for(uint8_t n=0;n<8u;n++){
        bool top=(crc&0x8000u)!=0;
        crc=(uint16_t)((crc<<1)|((byte>>n)&1u));
        if(top) crc^=0x1021u;
    }
    return crc;
}

static bool odd(uint8_t v){
    v^=v>>4; v^=v>>2; v^=v>>1;
    return (v&1u)!=0;
}

static uint16_t frameCrc(void){
    uint16_t crc=0;
    for(uint8_t n=0;n<g.len;n++) crc=crcAdd(crc,g.frame[n]);
    crc=crcAdd(crc,g.crcBytes[0]);
    return crcAdd(crc,g.crcBytes[1]);
}

/* Correct only bytes whose odd parity failed. A correction is accepted solely
 * when the complete BCS becomes zero, so diagnostics never become messages. */
static bool repair(void){
    if(!g.parityErrors||g.parityErrors>2u) return false;
    uint8_t p0=g.parityAt[0];
    for(uint8_t a=0;a<8u;a++){
        g.frame[p0]^=(uint8_t)(1u<<a);
        if(g.parityErrors==1u){
            if(frameCrc()==0u) return true;
        }else{
            uint8_t p1=g.parityAt[1];
            for(uint8_t b=0;b<8u;b++){
                g.frame[p1]^=(uint8_t)(1u<<b);
                if(frameCrc()==0u) return true;
                g.frame[p1]^=(uint8_t)(1u<<b);
            }
        }
        g.frame[p0]^=(uint8_t)(1u<<a);
    }
    return false;
}

static char clean(uint8_t c){
    c&=0x7Fu;
    if(c=='\r'||c=='\n') return '\n';
    return (c>=32u&&c<127u)?(char)c:' ';
}

static void copyField(char *out,uint8_t pos,uint8_t n){
    uint8_t k=0;
    while(k<n&&pos+k<g.len){ out[k]=clean(g.frame[pos+k]); k++; }
    while(k<n) out[k++]=' ';
    out[n]='\0';
}

static void accept(void){
    if(g.len<13u) return;
    message_t *m=g.history[HISTORY-1u];
    for(uint8_t n=HISTORY-1u;n;n--) g.history[n]=g.history[n-1u];
    g.history[0]=m;
    copyField(m->address,1,7);
    if(m->address[0]=='.'){
        for(uint8_t n=0;n<6u;n++) m->address[n]=m->address[n+1u];
        m->address[6]='\0';
    }
    copyField(m->label,9,2);
    uint8_t block=(uint8_t)(g.frame[11]&0x7Fu);
    m->down=(uint8_t)(block>='0'&&block<='9');
    if(m->down&&g.len>=23u) copyField(m->flight,17,6);
    else{ for(uint8_t n=0;n<6u;n++) m->flight[n]=' '; m->flight[6]='\0'; }
    uint8_t out=0;
    /* Downlinks prefix their text with a four-character sequence number and
     * the six-character flight ID already shown in the header. Short blocks
     * have no such prefix, so keep their payload from immediately after STX. */
    uint8_t start=m->down&&g.len>=23u?23u:13u;
    for(uint8_t n=start;n+1u<g.len&&out<MESSAGE_MAX;n++){
        char c=clean(g.frame[n]);
        if(c=='\n'&&out&&m->text[out-1u]=='\n') continue;
        m->text[out++]=c;
    }
    while(out&&m->text[out-1u]==' ') out--;
    m->text[out]='\0';
    m->textLen=out;
    m->sublabel[0]=m->mfi[0]='\0';
    m->appAt=0;
    if(m->label[0]=='H'&&m->label[1]=='1'){
        if(m->down&&out>=4u&&m->text[0]=='#'&&m->text[3]=='B'){
            m->sublabel[0]=m->text[1]; m->sublabel[1]=m->text[2];
            m->sublabel[2]='\0'; m->appAt=4u;
        }else if(!m->down&&out>=5u&&m->text[0]=='-'&&m->text[1]==' '&&m->text[2]=='#'){
            m->sublabel[0]=m->text[3]; m->sublabel[1]=m->text[4];
            m->sublabel[2]='\0'; m->appAt=5u;
        }
        if(m->appAt&&out>=m->appAt+4u&&m->text[m->appAt]=='/'&&m->text[m->appAt+3u]==' '){
            m->mfi[0]=m->text[m->appAt+1u]; m->mfi[1]=m->text[m->appAt+2u];
            m->mfi[2]='\0'; m->appAt+=4u;
        }
    }
    m->rssi=g.A->rssi_dbm();
    if(g.count<HISTORY) g.count++;
    g.cur=g.top=0;
    g.nOk++;
    g.redraw|=REDRAW_FRAME;
}

static void resetDecoder(void){
    g.state=PREKEY;
    g.bits=8u;
    g.prekey=0;
}

static void finishFrame(uint8_t byte){
    g.crcBytes[1]=byte;
    bool ok=(g.parityErrors==0u&&frameCrc()==0u);
    if(!ok&&repair()){ ok=true; g.corrected++; }
    if(ok) accept();
    else if(g.parityErrors) g.parityFails++;
    else g.crcFails++;
    g.state=END;
}

/* Bytes after SYN SYN, already byte-aligned and in true polarity. */
static void decodedByte(uint8_t byte){
    if(g.state==SOH1){
        if(byte!=0x01u){ resetDecoder(); return; }
        g.state=TEXT;
        g.len=0;
        g.parityErrors=0;
        g.syncs++;
        return;
    }
    if(g.state==TEXT){
        if(g.len>=FRAME_MAX){ resetDecoder(); return; }
        g.frame[g.len]=byte;
        if(!odd(byte)){
            if(g.parityErrors<2u) g.parityAt[g.parityErrors]=g.len;
            g.parityErrors++;
        }
        g.len++;
        if(byte==0x83u||byte==0x97u) g.state=CRC1;
        return;
    }
    if(g.state==CRC1){ g.crcBytes[0]=byte; g.state=CRC2; return; }
    if(g.state==CRC2){ finishFrame(byte); return; }
    resetDecoder();
}

/* ---- integer MSK receiver ---- */
static int32_t absi(int32_t v){ return v<0?-v:v; }

static void pllUpdate(int32_t vo,int32_t error){
    dem_t *d=g.dem;
    int32_t av=absi(vo)>>8,ae=absi(error)>>8;
    int32_t sign=error<0?-1:1;
    int32_t p;
    if(ae>(av<<1)) p=40;
    else if(ae>av) p=28;
    else if((ae<<1)>av) p=16;
    else if((ae<<2)>av) p=6;
    else p=0;
    if(error==0) sign=0;
    /* The RC oscillator may be 1 % off: the integrator has to find up to
     * 18 Hz within the pre-key, then only follow it through the text. */
    int32_t integral=p?TRACK_I:0;
    if(g.state<TEXT){ integral=p*ACQ_I; p*=ACQ_GAIN; }
    d->phase=(uint32_t)(d->phase+sign*p);
    if(g.prekey>=8||g.prekey<=-8||g.state!=PREKEY){
        d->pllI+=sign*integral;
        if(d->pllI>PLL_I_LIMIT) d->pllI=PLL_I_LIMIT;
        if(d->pllI<-PLL_I_LIMIT) d->pllI=-PLL_I_LIMIT;
    }else d->pllI=0;
}

static void decision(void){
    dem_t *d=g.dem;
    int32_t i=0,q=0;
    uint8_t slot=d->ringAt;
    for(uint8_t n=0;n<16u;n++){
        iq_t *v=&d->ring[slot];
        i+=(int32_t)d->taps[n]*v->i;
        q+=(int32_t)d->taps[n]*v->q;
        slot=(uint8_t)((slot+1u)&15u);
    }
    int32_t vo=i,xq=q,error;
    if(d->symbol&1u){ vo=q; xq=i; error=vo>=0?-i:i; }
    else error=vo>=0?q:-q;
    bool bit=(d->symbol&2u)?vo<0:vo>0;
    /* Bit clock. The quadrature rail of the previous decision sits between
     * the symbols before and after it. When those two differ (equal bits: the
     * rail sign alternates) its sign tells an early clock from a late one. */
    if(((g.shift>>14)&1u)==bit){
        int32_t step=g.state<TEXT?TIMING_ACQ:TIMING_TRACK;
        d->clock+=(d->xq^vo)<0?-step:step;
    }
    d->xq=xq;
    pllUpdate(vo,error);
    d->symbol++;
    g.shift=(uint16_t)((g.shift>>1)|(bit?0x8000u:0u));
    if(g.state==PREKEY){
        /* Leaky run counter: noise decays to zero and leaves the keys and the
         * screen alone, the pre-key tone ramps up in either polarity. */
        int32_t run=bit?1:-1;
        if((g.prekey^run)<0) run*=PREKEY_LEAK;
        run+=g.prekey;
        if(run>=-127&&run<=127) g.prekey=(int8_t)run;
        /* Every bit alignment is searched; the bit clock has had + and * to
         * settle. An inverted match only flips the bytes that follow. */
        if(g.shift==SYNC_WORD||g.shift==SYNC_INVERTED){
            g.inv=(uint8_t)-(g.shift&1u);
            g.state=SOH1;
            g.bits=8u;
        }
        return;
    }
    if(--g.bits==0u){ g.bits=8u; decodedByte((uint8_t)((g.shift>>8)^g.inv)); }
}

static void sample(uint16_t raw){
    dem_t *d=g.dem;
    if(raw<g.adcMin) g.adcMin=raw;
    if(raw>g.adcMax) g.adcMax=raw;
    d->dc+=(((int32_t)raw<<4)-d->dc)>>6;
    int32_t x=(int32_t)raw-(d->dc>>4);
    int32_t step=(int32_t)PHASE_NOMINAL+(d->pllI>>4);
    d->clock+=step;
    if(d->clock>=BIT_PHASE){ d->clock-=BIT_PHASE; decision(); }
    d->phase=(uint32_t)(d->phase+step)&0xFFFFu;
    uint8_t phase=(uint8_t)(d->phase>>10);
    d->ring[d->ringAt].i=x*d->cosine[phase];
    d->ring[d->ringAt].q=x*d->cosine[(phase+16u)&63u];
    d->ringAt=(uint8_t)((d->ringAt+1u)&15u);
}

/* ---- screen ---- */
static bool pair(const char *s,char a,char b){ return s[0]==a&&s[1]==b; }

static uint8_t appType(const message_t *m){
    const char *id=m->mfi[0]?m->mfi:m->label;
    if(pair(id,'A','6')||pair(id,'A','A')||pair(id,'B','6')||pair(id,'B','A'))
        return APP_ARINC622;
    if(pair(m->label,'Q','0')) return APP_LINK;
    if(pair(m->label,'S','A')) return APP_MEDIA;
    if(pair(m->label,'M','A')) return APP_MIAM;
    if(pair(m->label,'H','1')) return APP_TERMINAL;
    return APP_UNKNOWN;
}

static uint8_t buildRows(const message_t *m,char *body,const char *ui){
    char *o=body;
    uint8_t type=appType(m);
    *o++=m->label[0]; *o++=m->label[1]; *o++=' ';
    if(g.raw||type==APP_UNKNOWN) o=put(o,m->text);
    else{
        o=put(o,m->down?ui+T_DOWN:ui+T_UP); *o++='\n';
        if(m->sublabel[0]){
            o=put(o,ui+T_SUB); *o++=m->sublabel[0]; *o++=m->sublabel[1];
            if(m->mfi[0]){ o=put(o,ui+T_MFI); *o++=m->mfi[0]; *o++=m->mfi[1]; }
            *o++='\n';
        }
        const char *name=ui+T_TERMINAL;
        if(type==APP_ARINC622) name=ui+T_ARINC622;
        else if(type==APP_LINK) name=ui+T_LINK;
        else if(type==APP_MEDIA) name=ui+T_MEDIA;
        else if(type==APP_MIAM) name=ui+T_MIAM;
        o=put(o,name); *o++='\n';
        o=put(o,ui+T_DATA); o=put(o,m->text+m->appAt);
    }
    *o='\0';
    uint8_t width=(uint8_t)(BODY_CHARS+(g.view?COMPACT_EXTRA:0u));
    uint8_t rows=0,col=0;
    for(char *p=body;;p++){
        if(*p=='\n'||*p=='\0'||++col==width){
            rows++; col=0;
            if(*p=='\0') break;
        }
    }
    return rows;
}

static void drawBody(char *body){
    char row[BODY_CHARS+COMPACT_EXTRA+1u];
    uint8_t width=(uint8_t)(BODY_CHARS+(g.view?COMPACT_EXTRA:0u));
    uint8_t visible=(uint8_t)(BODY_ROWS+(g.view?1u:0u));
    uint8_t line=0,col=0,shown=0;
    for(char *p=body;;p++){
        char c=*p;
        if(c!='\n'&&c!='\0'&&col<width) row[col++]=c;
        if(c=='\n'||c=='\0'||col==width){
            if(line>=g.top&&shown<visible){
                row[col]='\0';
                if(g.view) g.A->print_tiny(row,0,(uint8_t)(8u+shown*6u),false,true);
                else g.A->print_normal(row,0,0,(uint8_t)(shown+1u));
                shown++;
            }
            line++; col=0;
            if(c=='\0') break;
        }
    }
}

static void separator(void){
    uint8_t *b=g.A->fb[0];
    for(unsigned x=0;x<128u;x+=2u){ b[384u+x]|=0x80u; b[385u+x]&=0x7Fu; }
}

static void draw(void){
    const app_api_t *A=g.A;
    char ui[UI_SIZE] __attribute__((aligned(4)));
    char text[MESSAGE_MAX+64u];
    A->asset_read(0,ui,UI_SIZE);
    A->display_clear();
    A->status_clear();
    A->print_inverse(ui+T_TITLE,2,0,true,true,(uint8_t)(2u+T_TITLE_CHARS*4u));
    A->draw_battery();

    if(!g.count){
        A->print_inverse(ui+T_WAIT,WAIT_CAPS_X,0,true,true,
                         (uint8_t)(WAIT_CAPS_X+T_WAIT_CHARS*4u));
    }else{
        message_t *m=g.history[g.cur];
        A->print_bold(m->address,0,67,0);
        A->print_bold(m->flight,69,g.count>1u?110u:127u,0);
        A->print_inverse(g.raw?ui+T_RAW:ui+T_DEC,WAIT_CAPS_X,0,true,true,
                         (uint8_t)(WAIT_CAPS_X+3u*4u));
        uint8_t rows=buildRows(m,text,ui);
        uint8_t visible=(uint8_t)(BODY_ROWS+(g.view?1u:0u));
        g.lim=rows>visible?(uint8_t)(rows-visible):0u;
        drawBody(text);
    }

    unsigned tail=g.spk;
    if(g.top||g.cur) tail+=2u;
    if(g.top<g.lim||g.cur+1u<g.count) tail+=4u;
    A->asset_read((uint16_t)(BMP_TAIL+tail*TAIL_W),A->status_line+SPK_X,TAIL_W);
    if(g.count>1u){
        char *o=text;
        *o++=(char)('1'+g.cur); *o++='/'; *o++=(char)('0'+g.count); *o='\0';
        A->print_inverse(text,CAPS_END-12u,0,false,true,CAPS_END);
    }

    separator();
    drawFreq(A->rx_freq(),text);
    if(g.count){
        message_t *m=g.history[g.cur];
        char *o=puti(text,m->rssi); o=put(o,ui+T_DBM);
        o=put(o,ui+T_RX); o=puti(o,g.nOk);
        o=put(o,ui+T_FIX); o=puti(o,g.corrected);
        *o='\0';
        A->print_tiny(text,0,49,false,true);
    }else{
        char *o=put(text,ui+T_ADC); o=puti(o,g.ppShown);
        o=put(o,ui+T_SYNC); o=putStat(o,g.syncs);
        o=put(o,ui+T_CRC); o=putStat(o,g.crcFails);
        o=put(o,ui+T_PAR); o=putStat(o,g.parityFails); *o='\0';
        A->print_tiny(text,0,49,false,true);
    }
}

static void handleKeys(void){
    uint8_t key=g.A->get_key();
    int d=(key==APP_KEY_DOWN)-(key==APP_KEY_UP);
    unsigned top=(unsigned)(g.top+d);
    if(d&&top<=g.lim){
        if(top!=g.top){ g.top=(uint8_t)top; g.redraw|=REDRAW_ON; }
    }else if(d&&key!=g.prevKey&&(unsigned)(g.cur+d)<g.count){
        g.cur=(uint8_t)(g.cur+d);
        g.top=0;
        g.redraw|=REDRAW_ON;
    }
    if(key==APP_KEY_INVALID||key==g.prevKey){ g.prevKey=key; return; }
    g.prevKey=key;
    g.redraw|=REDRAW_ON;
    if(key==APP_KEY_EXIT) g.running=false;
    else if(key==APP_KEY_1) g.spk^=1u;
    else if(key==APP_KEY_STAR){ g.view^=VIEW_COMPACT; g.top=0; }
    else if(key==APP_KEY_F){ g.raw^=1u; g.top=0; }
    else if(key==APP_KEY_2){
        g.count=g.cur=g.top=g.lim=0;
        g.syncs=g.crcFails=g.parityFails=g.corrected=g.nOk=0;
    }
}

static void house(void){
    const app_api_t *A=g.A;
    uint16_t pp=(uint16_t)(g.adcMax-g.adcMin);
    if(pp>g.ppNow) g.ppNow=pp;
    g.adcMin=0xFFFFu;
    g.adcMax=0;
    if(g.redraw&REDRAW_FRAME) A->backlight_on();
    A->backlight_update();
    if(++g.refresh>=REFRESH_SLOTS){
        g.refresh=0;
        g.ppShown=g.ppNow; g.ppNow=0;
        g.redraw|=REDRAW_ON;
    }
    if(!g.redraw) return;
    g.redraw=0;
    adcRestore();
    A->battery_sample();
    adcSelect();
    draw();
    A->blit_status();
    A->blit_full();
}

/* ---- continuous receiver ---- */
static void listen(void){
    adcSelect();
    clkStart();
    resetDecoder();
    uint32_t next=0;
    uint16_t n=0;
    g.redraw=REDRAW_ON;
    g.adcMin=0xFFFFu;
    while(g.running){
        while((int32_t)(clkNow()-next)<0){}
        next+=SAMPLE_CYC;
        sample(adcRead());
        if(++n<HOUSE_EVERY) continue;
        n=0;
        /* Screen and battery work discard enough samples to lose the short
         * pre-key, so they wait while a carrier is up. The key scan costs
         * about four samples: harmless on a carrier lead, fatal inside a
         * block, so it only waits while the modem itself is working, and
         * never longer than BUSY_MAX. A key is answered at once, even on a
         * channel whose squelch stays open. */
        bool modem=g.state!=PREKEY||g.prekey>=16||g.prekey<=-16;
        bool busy=true;
        if(!modem||g.busyFor+1u>=BUSY_MAX){
            if(!g.rfHold&&(g.A->bk_read(0x0Cu)&2u)) g.rfHold=RF_HOLD_SLOTS;
            handleKeys();
            busy=!modem&&g.rfHold&&!g.redraw;
        }
        if(g.rfHold) g.rfHold--;
        /* Open the speaker only after the complete ACARS header, then hold
         * it across short OOK dropouts. */
        if(g.state>=TEXT) g.audioHold=4u;
        bool audible=g.audioHold!=0u;
        if(g.audioHold) g.audioHold--;
        uint8_t audio=(uint8_t)(g.spk&&audible);
        if(audio!=g.audioOn){ g.A->audio_path(audio); g.audioOn=audio; }
        if(busy&&++g.busyFor<BUSY_MAX) continue;
        g.busyFor=0;
        house();
        next=clkNow();
    }
    adcRestore();
}

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api){
    message_t frames[HISTORY];
    message_t *history[HISTORY];
    uint8_t frame[FRAME_MAX];
    dem_t dem;
    for(uint8_t n=0;n<HISTORY;n++) history[n]=&frames[n];
    memset(&dem,0,sizeof dem);
    dem.dc=(int32_t)BIAS_CODE<<4;
    _Static_assert(TAPS==COSINE+COSINE_LEN,
                   "MSK cosine and matched-filter assets must stay contiguous");
    api->asset_read(COSINE,dem.cosine,(uint16_t)(COSINE_LEN+TAPS_LEN));

    g.A=api;
    g.history=history;
    g.frame=frame;
    g.dem=&dem;
    g.prevKey=APP_KEY_INVALID;
    g.savedSqr3=ADC_SQR3;
    g.savedSmpr3=ADC_SMPR3;
    const uint32_t moder=GPIOA_MODER,dac=DAC_CR,dhr=DAC_DHR12R1,rcc=RCC_APBENR1;
    biasOn(moder,rcc);
    api->cfg_load(&g.spk,2);
    g.spk=(uint8_t)(g.spk==1u);
    if(g.view!=VIEW_COMPACT) g.view=0;
    api->set_af(APP_AF_AM);
    api->delay_ms(50);
    api->backlight_on();
    g.running=true;
    listen();
    biasOff(dac,dhr,rcc,moder);
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
    api->cfg_save(&g.spk,2);
}
