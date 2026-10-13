/* DTMF Monitor — receive-only BK4819 overlay. Apache-2.0. */
#include <stddef.h>
#include "../app_api.h"
#include "monitor.h"

#define FOUND 0x0800u
static struct {
    const app_api_t *api;
    monitor_t monitor;
    uint8_t age, previous;
    bool speaker, audio_on, clear_pending;
    uint32_t clear_at;
} g;
#define A (g.api)

/* No libc in a freestanding overlay. */
void *memset(void *p, int c, size_t n) {
    uint8_t *b=p;
    while(n--) *b++=(uint8_t)c;
    return p;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d=dst; const uint8_t *s=src;
    while(n--) *d++=*s++;
    return dst;
}

static void line(const char *text, uint8_t row) {
    /* Resident small-font renderer has no clipping: 7px stride, <=18. */
    char safe[19]; unsigned i=0;
    while(i<18 && text[i]) {safe[i]=text[i];++i;}
    safe[i]=0; A->print_normal(safe,0,127,row);
}

static void draw(void) {
    A->display_clear();
    A->status_clear();
    A->print_inverse("DTMF RX",2,0,true,true,30);
    if(!g.monitor.count) A->print_inverse("WAIT",40,0,true,true,56);
    static const uint8_t speaker[]={0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c};
    if(g.speaker) memcpy(A->status_line+59,speaker,sizeof(speaker));
    static const uint8_t up[]={4,6,7,6,4}, down[]={16,48,112,48,16};
    for(unsigned i=0;i<5;i++) A->status_line[72+i]=
        (g.age ? up[i]:0) | (g.age+1<g.monitor.count ? down[i]:0);
    A->draw_battery();
    /* Fixed decimal formatting avoids printf/division helpers. */
    char f[]="0000.00000";
    uint32_t n=A->rx_freq();
    const uint32_t places[]={100000000,10000000,1000000,100000,10000,1000,100,10,1};
    for(unsigned i=0,j=0;i<9;i++,j++) {
        if(j==4) ++j;
        while(n>=places[i]) { ++f[j]; n-=places[i]; }
    }
    char *freq=f+(f[0]=='0');
    unsigned len=0; while(freq[len]) ++len;
    A->print_normal(freq+len-2,(uint8_t)((len-2)*13-10),0,5);
    freq[len-2]=0;
    A->display_freq(freq,0,4,false);
    const monitor_record_t *r=monitor_record(&g.monitor,g.age);
    if(r) {
        char state[]="SEQ 1/8 DONE +";
        state[4]='1'+g.age; state[6]='0'+g.monitor.count;
        if(!g.age && g.monitor.active) {
            state[8]='L';state[9]='I';state[10]='V';state[11]='E';
        }
        if(!r->truncated) state[12]=0;
        line(state,0);
        char row[17];
        for(unsigned part=0;part<2;part++) {
            unsigned i=0,start=part*16;
            while(i<16 && start+i<r->length) {row[i]=r->text[start+i]; ++i;}
            row[i]=0; line(row,1+part);
        }
    }
    if(g.clear_pending) A->print_tiny("Press * again to clear",20,24,false,true);
    /* Same 3x5 text and inverse key capsules as the other overlay apps. */
    for(unsigned x=0;x<128;x+=2) A->fb[3][x]|=0x80;
    A->print_inverse("NAV",2,6,false,true,14);
    A->print_tiny("Hist",17,49,false,true);
    A->print_inverse("MENU",35,6,false,true,51);
    A->print_tiny("Live",53,49,false,true);
    A->print_inverse("0",73,6,false,true,77);
    A->print_tiny("Audio",79,49,false,true);
    A->print_inverse("*",104,6,false,true,108);
    A->print_tiny("Clr",110,49,false,true);
    A->blit_status(); A->blit_full();
}

/* REG_0C bit 1 is the live hardware squelch result. The loader installed
 * the selected VFO's calibrated RSSI/noise/glitch thresholds already.
 * Gate only the external speaker path; never pause the DTMF detector. */
static void update_audio(void) {
    bool on=g.speaker && (A->bk_read(0x0c)&2u);
    if(on!=g.audio_on) { A->audio_path(on); g.audio_on=on; }
}

#ifndef DTMF_HOST_TEST
__attribute__((section(".text.entry")))
#endif
void app_main(const app_api_t *api) {
    memset(&g,0,sizeof(g)); g.api=api;
    /* Loader also restores the VFO/radio. Preserve detector-specific state
     * ourselves, including the old IRQ mask; never invoke TX services. */
    const uint8_t ids[]={0x21,0x24,0x47,0x3f};
    uint16_t saved[4];
    for(unsigned i=0;i<4;i++) saved[i]=A->bk_read(ids[i]);
    A->bk_write(0x3f,0);
    A->bk_write(0x02,0); (void)A->bk_read(0x02);
    A->bk_write(0x21,0x06d8);
    A->bk_write(0x24,0xc17f); /* resident threshold 130, DTMF enabled */
    A->bk_write(0x3f,FOUND);
    A->set_af(APP_AF_FM); A->audio_path(false);
    g.previous=A->get_key();
    uint32_t last_draw=A->ticks_ms()-100, last_sample=A->ticks_ms();
    A->battery_sample();
    for (;;) {
        uint32_t now=A->ticks_ms();
        /* Acknowledge before reading status, like resident APP_CheckRadio.
         * A cap keeps keys responsive with a stuck or noisy IRQ source. */
        for(unsigned tries=0;tries<4 && (A->bk_read(0x0c)&1);tries++) {
            A->bk_write(0x02,0);
            if(A->bk_read(0x02)&FOUND) {
                char c=monitor_character((A->bk_read(0x0b)>>8)&15);
                if(monitor_event(&g.monitor,c,now) && g.age) {
                    if(g.age<MONITOR_HISTORY-1) ++g.age;
                }
            }
        }
        monitor_tick(&g.monitor,now);
        if(g.clear_pending && (uint32_t)(now-g.clear_at)>=2000) g.clear_pending=false;
        uint8_t key=A->get_key();
        if(key!=g.previous && key!=APP_KEY_INVALID && key<APP_KEY_INVALID) {
            if(key==APP_KEY_EXIT || key==APP_KEY_PTT) {
                A->audio_path(false);
                /* Consume this press, even for direct shortcut launches. */
                for(unsigned released=0;released<3;) {
                    released=A->get_key()==APP_KEY_INVALID ? released+1 : 0;
                    A->backlight_update(); A->delay_ms(10);
                }
                break;
            }
            if(key!=APP_KEY_STAR) g.clear_pending=false;
            if(key==APP_KEY_0) {
                g.speaker=!g.speaker;
            } else if(key==APP_KEY_MENU) {g.age=0;g.clear_pending=false;}
            else if(key==APP_KEY_STAR) {
                if(g.clear_pending) {monitor_clear(&g.monitor);g.age=0;g.clear_pending=false;}
                else {g.clear_pending=true;g.clear_at=now;}
            } else {
                int8_t dir=A->nav_dir(key);
                if(dir>0 && g.age+1<g.monitor.count) ++g.age;
                else if(dir<0 && g.age) --g.age;
                g.clear_pending=false;
            }
        }
        update_audio();
        g.previous=key;
        A->backlight_update();
        if((uint32_t)(now-last_sample)>=5000) {A->battery_sample();last_sample=now;}
        if((uint32_t)(now-last_draw)>=100) { draw(); last_draw=now; }
        A->delay_ms(10);
    }
    A->audio_path(false);
    A->bk_write(0x3f,0);
    A->bk_write(0x02,0); (void)A->bk_read(0x02);
    for(unsigned i=0;i<4;i++) A->bk_write(ids[i],saved[i]);
}
