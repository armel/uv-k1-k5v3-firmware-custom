#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../app_api.h"
static uint32_t now;
static uint32_t delivered;
static unsigned scenario, acknowledgements, frames, audio_on;
static uint16_t regs[256], original[256];
static bool saw_sequence, saw_help, held_kept, cleared, older, latest;
static unsigned compact_hints;
static uint8_t fb[7][128], status_line[128];
static unsigned battery_draws;
static bool audio_state;
static bool sq_open(void) {
    if(scenario==1) return now>=400 && now<600;
    if(scenario==4) return (now>=300 && now<500) || (now>=700 && now<800);
    return scenario==5;
}
static void nothing(void) {}
static void delay(uint32_t ms) {
    bool expected=false;
    if(scenario==1) expected=now>=400 && now<600;
    if(scenario==4) expected=now>=100 && now<750 && sq_open();
    if(scenario==5) expected=now>=100 && now<1500;
    assert(audio_state==expected);
    now+=ms; assert(now<5000);
}
static uint32_t ticks(void) { return now; }
static uint32_t freq(void) { return 43850000; }
static uint16_t read_reg(uint8_t r) {
    if(scenario==3 && r==0x0c) return delivered!=now && (now==10 || now==1110 || now==2210);
    if(scenario==3 && r==0x0b) return (now==10 ? 1:now==1110 ? 2:3)<<8;
    if(r==0x0c) return (sq_open()?2:0) | ((scenario==2 || (delivered!=now && (now==10 || now==110 || now==210))) ? 1:0);
    if(r==0x02) { delivered=now; return 0x0800; }
    if(r==0x0b) return (now==210 ? 15:1)<<8;
    return regs[r];
}
static void write_reg(uint8_t r,uint16_t v) {
    if(r==0x02) acknowledgements++;
    else regs[r]=v;
}
static uint8_t key(void) {
    unsigned exit_at=scenario==3 ? 3500:1500;
    if (now>=exit_at && now<exit_at+80) return scenario==1 ? APP_KEY_PTT:APP_KEY_EXIT;
    if (scenario==3 && now==1200) return APP_KEY_UP;
    if (scenario==3 && now==2500) return APP_KEY_MENU;
    if ((scenario==4 || scenario==5) && now==100) return APP_KEY_0;
    if (scenario==4 && now==750) return APP_KEY_0;
    if (scenario==1 && now==400) return APP_KEY_0;
    if (scenario==1 && now>=500 && now<=600) return APP_KEY_STAR;
    if (scenario==1 && now==700) return APP_KEY_STAR;
    if (scenario==0 && now==400) return APP_KEY_F;
    return APP_KEY_INVALID;
}
static void audio(bool on) {
    audio_state=on;
    if(on) { assert(sq_open()); audio_on++; }
    if(scenario==4 && on) assert(now==300 || now==700);
}
static void af(uint8_t mode) { assert(mode==APP_AF_FM || mode==APP_AF_MUTE); }
static void text(const char*s,uint8_t a,uint8_t b,uint8_t l) {
    (void)a;(void)b;(void)l;
    assert(strlen(s)<=18); /* real renderer: 7px stride, no clipping */
    if(scenario==1 && now==600 && !strcmp(s,"11#")) held_kept=true;

    if(scenario==3 && now==2300 && !strcmp(s,"1")) older=true;
    if(scenario==3 && now==2600 && !strcmp(s,"3")) latest=true;
    if(!strcmp(s,"11#")) saw_sequence=true;
    if(!strcmp(s,"DTMF HELP")) saw_help=true;
}
static void tiny(const char *s,uint8_t x,uint8_t y,bool status,bool fill) {
    (void)fill; assert(y+6<=(status?8:56) && x+strlen(s)*4<=128);
}
static void inverse(const char *s,uint8_t x,uint8_t row,bool status,bool fill,uint8_t end) {
    assert(x>=2 && end<128 && (status ? row==0:row==6));
    assert(end==x+strlen(s)*4);
    if(status && scenario==1 && now==800 && !strcmp(s,"WAIT")) cleared=true;
    assert(strcmp(s,"F"));
    tiny(s,x,row*8+1,status,fill); if(!status) compact_hints++;
}
static void draw_line(app_fb_t target,int16_t x1,int16_t y1,int16_t x2,int16_t y2,bool on) {
    (void)target;(void)on;
    assert(x1>=0 && x2<128 && y1>=0 && y2<56);
}
static int8_t nav(uint8_t k) { return k==APP_KEY_UP ? 1:k==APP_KEY_DOWN ? -1:0; }
static void display_freq(const char *s,uint8_t x,uint8_t row,bool center) {
    (void)center; unsigned w=x;
    for(const char *c=s;*c;c++) w+=*c=='.'?3:13;
    assert(w<=128 && row==4);
}
static void status_clear(void) { memset(status_line,0,sizeof(status_line)); }
static void battery(void) { battery_draws++; }
static void blit(void) { frames++; }

extern void app_main(const app_api_t*);
int main(void) {
    app_api_t api={.abi_major=1,.api_level=2,.api_size=sizeof(api),.fb=fb,
        .display_clear=nothing,.status_clear=status_clear,.status_line=status_line,.draw_battery=battery,.battery_sample=nothing,.display_freq=display_freq,.print_normal=text,.print_bold=text,
        .print_tiny=tiny,.print_inverse=inverse,.draw_line=draw_line,.blit_full=blit,.blit_status=nothing,.get_key=key,.delay_ms=delay,.rx_freq=freq,
        .bk_read=read_reg,.bk_write=write_reg,.audio_path=audio,.set_af=af,
        .nav_dir=nav,.ticks_ms=ticks,.backlight_update=nothing,.backlight_on=nothing};
    for(scenario=0;scenario<6;scenario++) {
        for(unsigned i=0;i<256;i++) regs[i]=original[i]=(uint16_t)(i*3+7);
        audio_state=false;now=0;delivered=0;compact_hints=battery_draws=0;acknowledgements=frames=audio_on=0;saw_sequence=saw_help=held_kept=cleared=older=latest=false;
        app_main(&api);
        assert(!audio_state);
        for(unsigned i=0;i<256;i++) if(i!=2) assert(regs[i]==original[i]);
        assert(frames>0 && now==(scenario==3 ? 3610:1610) && acknowledgements>=2);
        if(scenario==0) assert(saw_sequence && !saw_help && compact_hints>0);
        if(scenario==1) assert(audio_on==1 && held_kept && cleared);
        if(scenario==3) assert(older && latest);
        assert(battery_draws>0);
        if(scenario==4) assert(audio_on==2 && saw_sequence);
        if(scenario==5) assert(audio_on==1);
        if(scenario==2) assert(acknowledgements<1000); /* bounded stuck-IRQ drain */
    }
    puts("app: received IRQs, UI, speaker, held keys, PTT, cleanup, stuck IRQ PASS");
}
