/* Copyright 2026 Armel F4HWN
 * Licensed under the Apache License, Version 2.0.
 */

/* KISS TNC - Bell 202 AX.25 modem on the Kenwood programming port.
 *
 * The receiver is the APRS RX demodulator. The transmitter is the APRS TX
 * tone generator. The host speaks KISS at 38400 8N1 on USART1, the same
 * jack the programming cable uses. KISS payloads are AX.25 without flags
 * or FCS; the TNC checks, adds and removes the FCS.
 *
 * USART1 RX is the firmware's circular DMA into a 256-byte buffer (DMA1
 * channel 2, App/driver/uart.c). Bytes are taken from that buffer and
 * cleared so the resident command parser and K5Viewer do not treat KISS
 * as a programming frame. EXIT restores the port.
 *
 * Carrier sense (key 3, saved): SQL uses the hardware squelch or the
 * modem, DCD uses only the modem, FDX transmits on the next slot without
 * waiting. KISS full-duplex selects FDX, and 0 selects SQL again.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "kisstnc_assets.h"

static inline volatile uint32_t *hw(uint32_t a){
    volatile uint32_t *p=(volatile uint32_t *)a;
    __asm__("" : "+l"(p));
    return p;
}

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

/* USART1 and its RX DMA, matching App/driver/uart.c. */
#define USART_SR    (hw(0x40013800u)[0x00/4])
#define USART_DR    (hw(0x40013800u)[0x04/4])
#define DMA_CCR     (hw(0x4002001Cu)[0])
#define DMA_CNDTR   (hw(0x4002001Cu)[1])
#define DMA_CMAR    (hw(0x4002001Cu)[3])
#define UART_RXN    256u
#define USART_TC    0x40u
#define USART_TXE   0x80u

#define RCC_DACEN      (1u << 29)
#define DAC_CR_BIAS    ((1u << 0) | (1u << 1) | (1u << 2) | (7u << 3))
#define BIAS_CODE      2048u
#define ADC_SR_EOC     (1u << 1)
#define ADC_CR2_START  ((1u << 22) | (1u << 20))
#define SMP8_POS       24u
#define SMP4_POS       12u
#define ADC_CH_PA4     4u

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
#define FRAME_MIN      17u
#define PAYLOAD_MAX    (FRAME_MAX - 2u)
#define FCS_GOOD       0xF0B8u
#define SLICER_DUP_MS  50u

#define REG_51         0x51u
#define REG_70         0x70u
#define REG_71         0x71u
#define REG_0C         0x0Cu
#define F_MARK         12389u
#define F_SPACE        22714u
#define LVL_MIN        10
#define LVL_MAX        127
#define TW_MIN         (-4)
#define TW_MAX         8
#define CFG_MAGIC      0xB1u

#define FEND           0xC0u
#define FESC           0xDBu
#define TFEND          0xDCu
#define TFESC          0xDDu

enum { ST_WAIT=0, ST_HOLD, ST_TX };
enum { SH_IDLE=0, SH_HOLD, SH_TX, SH_DENIED, SH_NOUART };
enum { DCD_SQL=0, DCD_MODEM, DCD_FDX };
enum { KS_IDLE=0, KS_CMD, KS_DATA, KS_ESC };

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
_Static_assert(TEXT_MAX<=40u,"status text must fit draw()'s buffer");

typedef struct {
    int32_t dprev, phase;
    uint16_t n, crc;
    uint8_t wa, wb, last, sr, ones, byte, nb, inframe, hdr, bits;
    bool pre;
    uint8_t buf[FRAME_MAX];
} sl_t;

/* Scalars stay inside the Thumb immediate ranges; the frame buffers are
 * app_main's stack, pointed at from here. */
static struct {
    uint8_t prevKey, shown, state, refresh, space, ones, holdDcd;
    uint8_t lvl, dcd, txd, p, slot, tail;
    int8_t tw;
    uint8_t fArm, running, resetRx;
    uint8_t kState, kCmd, kStore, kParam;
    uint8_t urd, oPhase, oEsc;
    uint16_t rx, txc, drops, pendingLen, kLen, oLen, oIdx;
    const app_api_t *A;
    volatile uint8_t *uart;
    uint8_t *out, *pending;
    char *note;
    uint32_t savedSqr3, savedSmpr3, slotAt, lastHash, lastAt, tPrev, tCyc, next;
} g;

void *memset(void *d,int c,size_t n){
    uint8_t *p=d; while(n--) *p++=(uint8_t)c; return d;
}
void *memcpy(void *d,const void *s,size_t n){
    uint8_t *p=d; const uint8_t *q=s; while(n--) *p++=*q++; return d;
}

static void inc(uint16_t *v){ if(*v<9999u) (*v)++; }
static void uartPoll(void);
static void draw(void);
static void snap(uint8_t way,const uint8_t *f,uint16_t n);

static void clkStart(void){ g.tPrev=SYST_VAL; g.tCyc=0; }
static uint32_t clkCyc(void){
    uint32_t v=SYST_VAL;
    g.tCyc+=(v<=g.tPrev)?g.tPrev-v:g.tPrev+(SYST_LOAD+1u-v);
    g.tPrev=v;
    return g.tCyc;
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
static void adcSelPA4(void){
    ADC_SMPR3=(g.savedSmpr3&~(7u<<SMP4_POS))|(((g.savedSmpr3>>SMP8_POS)&7u)<<SMP4_POS);
    ADC_SQR3=(g.savedSqr3&~0x1Fu)|ADC_CH_PA4;
}
static void adcRestore(void){ ADC_SQR3=g.savedSqr3; ADC_SMPR3=g.savedSmpr3; }
static uint16_t adcRead(void){
    ADC_CR2|=ADC_CR2_START;
    for(uint16_t n=0;!(ADC_SR&ADC_SR_EOC)&&n<2000u;n++){}
    return (uint16_t)(ADC_DR&0x0FFFu);
}

__attribute__((noinline))
static uint16_t crcByte(uint16_t c,uint8_t v){
    c^=v;
    for(uint8_t i=0;i<8u;i++) c=(c&1u)?(uint16_t)((c>>1)^0x8408u):(uint16_t)(c>>1);
    return c;
}

static bool callChar(uint8_t c){
    return (c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' ';
}

/* One flag is 6.667 ms. KISS times are 10 ms units, so the count is *3/2. */
static uint16_t flagCount(uint8_t tenMs,uint16_t minN){
    uint16_t v=(uint16_t)(tenMs+(tenMs>>1));
    return v<minN?minN:v;
}

static void onFrame(const sl_t *m){
    uint16_t n=m->n;
    if(n<FRAME_MIN||n>FRAME_MAX) return;
    uint32_t now=g.A->ticks_ms();
    uint32_t h=n;
    for(uint16_t i=0;i<n;i++) h=(h<<5)-h+m->buf[i];
    if(!h) h=1u;
    if(g.lastHash==h && now-g.lastAt<SLICER_DUP_MS) return;
    g.lastHash=h; g.lastAt=now;
    if(g.oPhase){ inc(&g.drops); return; }
    n=(uint16_t)(n-2u);
    memcpy(g.out,m->buf,n);
    g.oLen=n; g.oIdx=0; g.oEsc=0; g.oPhase=1u;
    inc(&g.rx);
    snap(1u,m->buf,n);
}

__attribute__((noinline))
static void hdlcBit(sl_t *m,uint8_t b){
    if(m->bits<255u) m->bits++;
    m->sr=(uint8_t)((m->sr>>1)|(b<<7));
    if(m->sr==0x7Eu){
        bool ok=m->inframe&&m->n>=FRAME_MIN&&m->n<=FRAME_MAX&&m->crc==FCS_GOOD;
        m->pre=m->bits==8u;
        m->bits=0;
        if(ok) onFrame(m);
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
    uint32_t v=g.lvl;
    if(g.space){
        v=(v*(uint32_t)(8+g.tw))>>3;
        if(v>LVL_MAX) v=LVL_MAX;
        if(!v) v=1u;
    }
    g.A->bk_write(REG_71,(uint16_t)(g.space?F_SPACE:F_MARK));
    g.A->bk_write(REG_70,(uint16_t)(0x8000u|(v<<8)));
}
static void sendBit(uint8_t b){
    /* Keep draining the host during the bit wait so a 1 s transmission
     * cannot overflow the 256-byte DMA buffer. */
    while((int32_t)(clkCyc()-g.next)<0) uartPoll();
    if(!b){ g.space^=1u; setTone(); }
    g.next+=CYC_PER_BIT;
}
__attribute__((noinline))
static void sendByte(uint8_t v,uint8_t stuff){
    for(uint8_t i=0;i<8u;i++){
        uint8_t b=v&1u; v>>=1; sendBit(b);
        if(stuff){
            if(!b) g.ones=0;
            else if(++g.ones==5u){ sendBit(0); g.ones=0; }
        }
    }
}

static void waitMs(uint32_t ms){
    uint32_t t=g.A->ticks_ms();
    while(g.A->ticks_ms()-t<ms) uartPoll();
}

static void send(void){
    const app_api_t *A=g.A;
    if(A->tx_state()){
        g.state=ST_WAIT; g.shown=SH_DENIED; g.pendingLen=0;
        inc(&g.drops); g.refresh=REFRESH_SLOTS; return;
    }
    g.state=ST_TX; g.shown=SH_TX;
    draw();
    A->tx_set_params();
    A->bk_write(REG_51,0);
    A->tx_tone(1200);
    g.space=0; setTone();
    clkStart(); g.next=CYC_PER_BIT;
    uint16_t n=flagCount(g.txd,4u);
    for(uint16_t i=0;i<n;i++) sendByte(0x7Eu,0);
    g.ones=0;
    for(uint16_t i=0;i<g.pendingLen;i++) sendByte(g.pending[i],1);
    n=flagCount(g.tail,2u);
    for(uint16_t i=0;i<n;i++) sendByte(0x7Eu,0);
    while((int32_t)(clkCyc()-g.next)<0) uartPoll();
    A->tx_mute(true);
    A->tx_end();
    A->set_af(APP_AF_FM);
    A->audio_path(false);
    /* pending can be reused: a host frame during the settle may queue. */
    g.state=ST_WAIT; g.shown=SH_IDLE;
    inc(&g.txc); g.resetRx=1;
    waitMs(50);
    g.refresh=REFRESH_SLOTS;
}

static void finish(void){
    uint8_t type=(uint8_t)(g.kCmd&0x0Fu);
    if(type==0u){
        if(g.kStore && g.kLen>=15u && g.kLen<=PAYLOAD_MAX){
            snap(2u,g.pending,g.kLen);
            uint16_t c=0xFFFFu;
            for(uint16_t i=0;i<g.kLen;i++) c=crcByte(c,g.pending[i]);
            c^=0xFFFFu;
            g.pending[g.kLen++]=(uint8_t)c;
            g.pending[g.kLen++]=(uint8_t)(c>>8);
            g.pendingLen=g.kLen;
            g.state=ST_HOLD; g.shown=SH_HOLD; g.slotAt=0;
        }else if(g.kLen) inc(&g.drops);
    }else if(g.kLen){
        uint8_t v=g.kParam;
        if(type==1u) g.txd=v;
        else if(type==2u) g.p=v;
        else if(type==3u) g.slot=v;
        else if(type==4u) g.tail=v;
        else if(type==5u){ g.dcd=v?DCD_FDX:DCD_SQL; g.refresh=REFRESH_SLOTS; }
    }
    g.kStore=0; g.kLen=0;
}

static void kissByte(uint8_t b){
    if(b==FEND){
        if(g.kState==KS_ESC) g.kStore=0;
        if(g.kState==KS_DATA||g.kState==KS_ESC) finish();
        g.kState=KS_CMD;
        return;
    }
    if(g.kState==KS_CMD){
        g.kCmd=b; g.kLen=0;
        g.kStore=((b&0x0Fu)==0u && g.state==ST_WAIT)?1u:0u;
        g.kState=KS_DATA;
        return;
    }
    if(g.kState==KS_ESC){
        g.kState=KS_DATA;
        if(b==TFEND) b=FEND;
        else if(b==TFESC) b=FESC;
        else { g.kStore=0; g.kLen=1000u; return; }
    }else if(b==FESC){
        if(g.kState==KS_DATA) g.kState=KS_ESC;
        return;
    }else if(g.kState!=KS_DATA) return;

    if(g.kLen==0u) g.kParam=b;
    if(g.kStore){
        if(g.kLen>=PAYLOAD_MAX) g.kStore=0;
        else g.pending[g.kLen]=b;
    }
    if(g.kLen<1000u) g.kLen++;
}

static void kissPump(void){
    if(!g.oPhase || !g.uart) return;
    if(!(USART_SR&USART_TXE)) return;
    uint8_t b;
    if(g.oPhase==1u){ b=FEND; g.oPhase=2u; }
    else if(g.oPhase==2u){ b=0x00; g.oPhase=3u; g.oIdx=0; g.oEsc=0; }
    else if(g.oEsc){ b=g.oEsc; g.oEsc=0; g.oIdx++; }
    else if(g.oIdx>=g.oLen){ b=FEND; g.oPhase=0; }
    else {
        b=g.out[g.oIdx];
        if(b==FEND){ b=FESC; g.oEsc=TFEND; }
        else if(b==FESC){ b=FESC; g.oEsc=TFESC; }
        else g.oIdx++;
    }
    USART_DR=b;
}

static uint8_t uartWr(void){
    return (uint8_t)(UART_RXN-(DMA_CNDTR&0xFFFFu));
}

static void uartPoll(void){
    if(g.uart){
        uint8_t wr=uartWr(), n=0;
        while(g.urd!=wr && n<8u){
            uint8_t b=g.uart[g.urd];
            g.uart[g.urd++]=0;          /* hide it from the resident parsers */
            kissByte(b);
            n++;
        }
    }
    kissPump();
}

static void uartOpen(void){
    g.uart=0;
    if((DMA_CCR&1u)==0u || DMA_CMAR==0u){ g.shown=SH_NOUART; return; }
    g.uart=(volatile uint8_t *)DMA_CMAR;
    g.urd=uartWr();
}

static void uartClose(void){
    if(!g.uart) return;
    uint32_t t=g.A->ticks_ms();
    while(g.oPhase && g.A->ticks_ms()-t<500u) kissPump();
    DMA_CCR&=~1u;
    for(uint16_t i=0;i<1000u && (DMA_CCR&1u);i++){}
    for(uint16_t i=0;i<UART_RXN;i++) g.uart[i]=0;
    (void)USART_SR; (void)USART_DR;     /* drop a byte left in the shifter */
    DMA_CCR|=1u;
    g.uart=0;
}

static const uint16_t p10[]={10000u,1000u,100u,10u,1u};
static char *putu(char *o,uint16_t v){
    bool lead=false;
    for(uint8_t i=0;i<4u;i++){
        uint8_t n=0; while(v>=p10[i+1u]){ v-=p10[i+1u]; n++; }
        if(n||lead||i==3u){ *o++=(char)('0'+n); lead=true; }
    }
    return o;
}
static char *puti(char *o,int16_t v){
    uint16_t u=(uint16_t)v;
    if(v<0){ *o++='-'; u=(uint16_t)(-v); }
    return putu(o,u);
}
static char *text(uint16_t off,char *s){ g.A->asset_read(off,s,TEXT_MAX); return s; }
static void drawFreq(uint32_t f,char *s){
    uint16_t mhz=0;
    while(f>=100000u){ f-=100000u; mhz++; }
    char *o=putu(s,mhz); *o++='.';
    for(uint8_t i=0;i<5u;i++){
        uint8_t d=0; uint32_t p=p10[i];
        while(f>=p){ f-=p; d++; }
        *o++=(char)('0'+d);
    }
    *o='\0';
    g.A->print_normal(s,0,127,3);
}

/* "RX CALL-SSID" / "TX CALL-SSID". note[] is app_main's stack. */
static void snap(uint8_t way,const uint8_t *f,uint16_t n){
    char *o=g.note;
    if(n>=14u){
        *o++=way==1u?'R':'T'; *o++='X'; *o++=' ';
        for(uint8_t i=0;i<6u;i++){
            char c=(char)(f[7u+i]>>1);
            if(c>' ') *o++=c;
        }
        uint8_t s=(uint8_t)((f[13]>>1)&15u);
        if(s){ *o++='-'; o=putu(o,s); }
    }
    *o='\0';
    g.refresh=REFRESH_SLOTS;
}

static void draw(void){
    const app_api_t *A=g.A;
    char s[40];
    uint16_t st=T_IDLE, mode=T_SQL;
    if(g.shown==SH_HOLD) st=T_HOLD;
    else if(g.shown==SH_TX) st=T_TX;
    else if(g.shown==SH_DENIED) st=T_DENIED;
    else if(g.shown==SH_NOUART) st=T_NOUART;
    if(g.dcd==DCD_MODEM) mode=T_DCD;
    else if(g.dcd==DCD_FDX) mode=T_FDX;
    A->display_clear(); A->status_clear();
    A->print_inverse(text(T_TITLE,s),2,0,true,true,(uint8_t)(2u+T_TITLE_CHARS*4u));
    text(st,s);
    uint8_t n=0; while(s[n]) n++;
    A->print_inverse(s,38,0,true,true,(uint8_t)(38u+n*4u));
    if(g.shown!=SH_NOUART){
        text(mode,s); n=0; while(s[n]) n++;
        A->print_inverse(s,78,0,true,true,(uint8_t)(78u+n*4u));
    }
    if(g.fArm) A->print_inverse("F",96,0,true,true,104);
    A->draw_battery();
    if(g.note[0]) A->print_bold(g.note,0,0,0);
    else if(g.shown!=SH_NOUART) A->print_bold(text(T_LISTEN,s),0,0,0);
    char *o=s;
    *o++='L'; o=putu(o,g.lvl); *o++=' '; o=puti(o,g.tw); *o='\0';
    A->print_normal(s,0,0,1);
    drawFreq(A->rx_freq(),s);
    A->blit_status(); A->blit_full();
}

static uint8_t carrier(uint8_t modem){
    if(g.dcd==DCD_FDX) return 0;
    if(modem) return 1;
    if(g.dcd==DCD_MODEM) return 0;
    return (g.A->bk_read(REG_0C)&2u)?1u:0u;
}

static void keys(void){
    const app_api_t *A=g.A;
    uint8_t key=A->get_key();
    if(key!=APP_KEY_INVALID && key!=g.prevKey && key!=APP_KEY_SAVER && key!=APP_KEY_WAKE){
        A->backlight_on();
        if(key==APP_KEY_EXIT) g.running=0;
        else if(key==APP_KEY_F) g.fArm=1u;
        else if(key==APP_KEY_1 || key==APP_KEY_2){
            if(key==APP_KEY_1){
                int16_t n=(int16_t)g.lvl+(g.fArm?-4:4);
                if(n<LVL_MIN) n=LVL_MIN;
                if(n>LVL_MAX) n=LVL_MAX;
                g.lvl=(uint8_t)n;
            }else{
                int16_t n=(int16_t)g.tw+(g.fArm?-1:1);
                if(n<TW_MIN) n=TW_MIN;
                if(n>TW_MAX) n=TW_MAX;
                g.tw=(int8_t)n;
            }
            g.fArm=0; g.refresh=REFRESH_SLOTS;
        }else if(key==APP_KEY_3){
            g.dcd=g.dcd>=DCD_FDX?0u:(uint8_t)(g.dcd+1u);
            g.fArm=0; g.refresh=REFRESH_SLOTS;
        }
    }
    g.prevKey=key;
}

static void house(uint8_t modem){
    const app_api_t *A=g.A;
    uartPoll();
    keys();
    if(!g.running) return;
    if(g.state==ST_HOLD){
        if(g.dcd==DCD_FDX) send();
        else if(!carrier(modem)){
            uint32_t now=A->ticks_ms();
            uint32_t need=(uint32_t)g.slot*10u;
            if(!g.slotAt) g.slotAt=now?now:1u;
            else if(now-g.slotAt>=need){
                g.slotAt=now;
                if((uint8_t)A->rand32()<=g.p) send();
            }
        }else g.slotAt=0;
    }
    if(!g.running) return;
    adcRestore();
    A->battery_sample();
    if(++g.refresh>=REFRESH_SLOTS){ g.refresh=0; draw(); }
    A->backlight_update();
    uartPoll();
    adcSelPA4();
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
    g.resetRx=0;
    adcSelPA4(); clkStart();
    uint32_t next=0;
    uint16_t cnt=0;
    uint8_t busyFor=0;
    while(g.running){
        while((int32_t)(clkCyc()-next)<0) uartPoll();
        next+=CYC_PER_SAMPLE;
        int32_t a,b;
        front(&d,adcRead(),&a,&b);
        for(uint8_t k=0;k<NSL;k++) slice(&sl[k],a,b);
        uartPoll();
        if(++cnt<HOUSE_EVERY) continue;
        cnt=0;
        uint8_t bz=0;
        for(uint8_t k=0;k<NSL;k++) bz=(uint8_t)(bz|busy(&sl[k]));
        if(bz) g.holdDcd=3u;
        else if(g.holdDcd) g.holdDcd--;
        /* FDX with a queued frame transmits over the in-progress reception. */
        if(bz && !(g.state==ST_HOLD && g.dcd==DCD_FDX) && ++busyFor<BUSY_MAX) continue;
        busyFor=0;
        house((uint8_t)(bz||g.holdDcd));
        if(g.resetRx) goto reset;
        next=clkCyc();
    }
    adcRestore();
}

static void loadCfg(const app_api_t *A){
    uint8_t cfg[4];
    g.lvl=66; g.tw=0; g.dcd=DCD_SQL;
    g.txd=30; g.p=255; g.slot=10; g.tail=3;
    A->cfg_load(cfg,4);
    if(cfg[0]!=CFG_MAGIC || cfg[1]<LVL_MIN || cfg[1]>LVL_MAX || cfg[2]>12u) return;
    g.lvl=cfg[1];
    g.tw=(int8_t)((int8_t)cfg[2]+TW_MIN);
    if(cfg[3]<=DCD_FDX) g.dcd=cfg[3];
}

static void saveCfg(const app_api_t *A){
    uint8_t cfg[4];
    cfg[0]=CFG_MAGIC; cfg[1]=g.lvl;
    cfg[2]=(uint8_t)(g.tw-TW_MIN); cfg[3]=g.dcd;
    A->cfg_save(cfg,4);
}

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api){
    uint8_t pending[FRAME_MAX];
    uint8_t out[PAYLOAD_MAX];
    char note[65];
    g.A=api; g.pending=pending; g.out=out; g.note=note;
    note[0]='\0';
    g.prevKey=APP_KEY_INVALID; g.running=1;
    loadCfg(api);
    uartOpen();
    api->backlight_on();
    if(!g.uart){
        draw();
        while(g.running){
            keys();
            if(g.refresh){ g.refresh=0; draw(); }
            api->delay_ms(50);
            api->backlight_update();
        }
    }else{
        g.savedSqr3=ADC_SQR3; g.savedSmpr3=ADC_SMPR3;
        const uint32_t savedModer=GPIOA_MODER,savedDac=DAC_CR;
        const uint32_t savedRcc=RCC_APBENR1,savedDhr=DAC_DHR12R1;
        biasOn(savedModer,savedRcc);
        api->set_af(APP_AF_FM);
        api->audio_path(false);
        api->delay_ms(50);
        g.shown=SH_IDLE; draw();
        listen();
        biasOff(savedDac,savedDhr,savedRcc,savedModer);
        api->set_af(APP_AF_MUTE);
        api->audio_path(false);
    }
    uartClose();
    saveCfg(api);
}
