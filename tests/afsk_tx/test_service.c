/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app/afsk_tx.h"
#include "app/afsk_hw.h"
#include "app/afsk_core.h"
#include "afsk_mock.h"

static uint32_t time_now, armed_deadline, mask;
static uint8_t denial, prepare_error;
static uint32_t token = 1;
static bool ptt, pa, timer_ok, change_on_prepare;
static unsigned pa_enables, restores, stops, tone_changes, arms;
static uint32_t tone_ticks;
static char events[4096];
static unsigned event_count;
static void event(char c) { assert(event_count < sizeof(events)); events[event_count++] = c; }
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1; }
void __set_PRIMASK(uint32_t previous) { mask = previous; }
uint8_t AFSK_HW_Info(app_tx_info_t *out)
{
    *out = (app_tx_info_t){ .size = sizeof(*out), .denial = denial,
        .flags = APP_TX_INFO_SIMPLEX, .channel_token = token };
    return denial;
}
bool AFSK_HW_AcquireTimer(void) { return timer_ok; }
void AFSK_HW_ReleaseTimer(void) { event('R'); }
uint32_t AFSK_HW_Now(void) { return time_now; }
void AFSK_HW_Arm(uint32_t deadline) { ++arms; armed_deadline = deadline; }
void AFSK_HW_Stop(void) { ++stops; event('S'); }
bool AFSK_HW_PTT(void) { return ptt; }
uint8_t AFSK_HW_Prepare(const app_tx_info_t *info, uint8_t gain)
{
    assert(!pa); assert(info->channel_token == token); assert(gain == 66);
    event('P');
    if (change_on_prepare) ++token;
    return prepare_error;
}
void AFSK_HW_PA(bool on)
{
    if (on) { ++pa_enables; event('+'); } else event('-');
    pa = on;
}
void AFSK_HW_Tone(bool space)
{
    (void)space; ++tone_changes; event('T'); time_now += tone_ticks;
}
void AFSK_HW_Restore(void) { assert(!pa); ++restores; event('X'); }
static const uint8_t vector[] = {
    0x82,0xa0,0xb4,0x9e,0xac,0x62,0xe0,0x9c,0x60,0x86,0x82,0x98,0x98,0x73,
    0x03,0xf0,0x21,0x35,0x35,0x34,0x35,0x2e,0x30,0x30,0x4e,0x2f,0x30,0x33,
    0x37,0x33,0x37,0x2e,0x30,0x30,0x45,0x3e,0x54,0x45,0x53,0x54,0xd6,0xfd
};
static app_afsk_opts_t opts(void)
{
    return (app_afsk_opts_t){ .size = sizeof(app_afsk_opts_t),
        .flags = APP_AFSK_REQUIRE_PHYSICAL_PTT, .channel_token = token,
        .preamble_flags = 45, .tail_flags = 3, .tone_gain = 66, .max_on_ms = 2000 };
}
static app_afsk_status_t poll(void)
{
    app_afsk_status_t s = { .size = sizeof(s) };
    AFSK_TX_Poll(&s); return s;
}
static void reset(void)
{
    AFSK_TX_Cancel();
    ptt = timer_ok = true; pa = change_on_prepare = false;
    denial = prepare_error = 0; time_now = mask = event_count = 0;
    pa_enables = restores = stops = tone_changes = arms = tone_ticks = 0;
}
static void tick(void) { time_now = armed_deadline; AFSK_TX_Tick(time_now); }
static void fcs(uint8_t *frame, unsigned length)
{
    uint16_t crc = 0xffff;
    for (unsigned n=0; n<length-2; ++n) {
        crc ^= frame[n];
        for (unsigned b=0; b<8; ++b) crc = (crc>>1) ^ ((crc&1) ? 0x8408 : 0);
    }
    crc ^= 0xffff; frame[length-2] = crc; frame[length-1] = crc>>8;
}
int main(void)
{
    reset();
    assert(AFSK_ValidateFrame(vector, sizeof(vector)));
    uint8_t frame[129]; memcpy(frame, vector, sizeof(vector));
    app_afsk_opts_t o = opts();
    assert(AFSK_TX_Submit(NULL,42,&o)==APP_AFSK_BAD_ARGUMENT);
    assert(AFSK_TX_Submit(frame,17,&o)==APP_AFSK_BAD_ARGUMENT);
    assert(AFSK_TX_Submit(frame,129,&o)==APP_AFSK_BAD_ARGUMENT);
    frame[0]^=1; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_BAD_ARGUMENT); frame[0]^=1;
    o.flags=0; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_BAD_ARGUMENT); o=opts();
    o.reserved=1; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_BAD_ARGUMENT); o=opts();
    o.size=2; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_BAD_ARGUMENT); o=opts();
    o.max_on_ms=2001; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_BAD_ARGUMENT); o=opts();
    o.channel_token=token+1; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_CHANNEL_CHANGED); o=opts();
    denial=APP_AFSK_LOW_BATTERY; assert(AFSK_TX_Submit(frame,42,&o)==denial); denial=0;
    ptt=false; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_PTT_RELEASED); ptt=true;
    timer_ok=false; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_INTERNAL); timer_ok=true;
    mask=1; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_INTERNAL); mask=0;
    assert(!pa_enables && !pa && !AFSK_TX_OwnsRF());
    change_on_prepare=true; assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_CHANNEL_CHANGED);
    assert(!pa_enables && !pa && restores==1); reset(); o=opts();
    prepare_error=APP_AFSK_PTT_RELEASED;
    assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_PTT_RELEASED);
    assert(!pa_enables && !pa && !AFSK_TX_OwnsRF()); reset(); o=opts();

    /* Caller buffers can be destroyed immediately after accepted submission. */
    assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_OK);
    assert(AFSK_TX_Submit(frame,42,&o)==APP_AFSK_BUSY);
    memset(frame,0xff,sizeof(frame)); o.max_on_ms=100; o.tail_flags=10;
    app_tx_info_t info={.size=sizeof(info)};
    assert(AFSK_TX_Info(&info)==APP_AFSK_BUSY && (info.flags&APP_TX_INFO_BUSY));
    for (unsigned n=0;n<722;++n) tick();
    assert(pa && poll().state==APP_AFSK_ACTIVE && poll().bits_sent==723);
    unsigned prior_events=event_count;
    tick(); assert(!pa && AFSK_TX_OwnsRF());
    assert(events[prior_events]=='-' && events[prior_events+1]=='S');
    app_afsk_status_t s=poll();
    assert(s.state==APP_AFSK_DONE && !s.error && s.bits_sent==723 && s.pa_on_us==602500);
    assert(!AFSK_TX_OwnsRF() && restores==1);
    assert(events[event_count-2]=='X' && events[event_count-1]=='R');
    AFSK_TX_Cancel(); assert(poll().state==APP_AFSK_DONE && restores==1);

    reset(); o=opts();
    assert(AFSK_TX_Submit(vector,42,&o)==0); ptt=false; tick();
    assert(!pa && poll().error==APP_AFSK_PTT_RELEASED);
    reset(); o=opts(); assert(AFSK_TX_Submit(vector,42,&o)==0);
    time_now=armed_deadline+3; AFSK_TX_Tick(time_now);
    s=poll(); assert(!pa && s.state==APP_AFSK_ERROR && s.error==APP_AFSK_TIMING);
    assert(s.max_lateness_cycles==1200);
    /* The eighth flag bit changes tone. Allowed IRQ lateness plus slow SPI
     * must abort before arming a compare that has elapsed or lacks margin. */
    for (unsigned spi=96; spi<=98; spi+=2) {
        reset(); o=opts(); assert(AFSK_TX_Submit(vector,42,&o)==0);
        for (unsigned n=0; n<6; ++n) tick();
        unsigned prior_arms=arms;
        tone_ticks=spi; time_now=armed_deadline+2; AFSK_TX_Tick(time_now);
        s=poll();
        assert(!pa && s.state==APP_AFSK_ERROR && s.error==APP_AFSK_TIMING);
        assert(arms==prior_arms && s.max_lateness_cycles==800);
    }
    reset(); o=opts(); assert(AFSK_TX_Submit(vector,42,&o)==0);
    for (unsigned n=0; n<6; ++n) tick();
    unsigned prior_arms=arms;
    tone_ticks=95; time_now=armed_deadline+2; AFSK_TX_Tick(time_now);
    s=poll();
    assert(pa && s.state==APP_AFSK_ACTIVE && arms==prior_arms+1);
    assert(armed_deadline-time_now==3);
    reset(); o=opts(); o.max_on_ms=100;
    assert(AFSK_TX_Submit(vector,42,&o)==0);
    for(unsigned n=0;n<150 && pa;++n) tick();
    s=poll(); assert(s.error==APP_AFSK_TIMEOUT && s.pa_on_us<100000);
    reset(); o=opts(); assert(AFSK_TX_Submit(vector,42,&o)==0);
    prior_events=event_count; AFSK_TX_Cancel();
    assert(!pa && !AFSK_TX_OwnsRF() && poll().error==APP_AFSK_CANCELED);
    assert(events[prior_events]=='-' && events[prior_events+1]=='S' && events[prior_events+2]=='X');

    /* Maximum frame and run of ones prove stuffing crosses byte boundaries. */
    memset(frame,0xff,128); fcs(frame,128); assert(AFSK_ValidateFrame(frame,128));
    reset(); o=opts(); assert(AFSK_TX_Submit(frame,128,&o)==0);
    for(unsigned n=0;n<2500 && pa;++n) tick();
    s=poll(); assert(s.state==APP_AFSK_DONE && s.pa_on_us<2000000);
    /* Last five ones must still insert zero before the tail, even at EOF. */
    afsk_hdlc_t h; o.preamble_flags=o.tail_flags=1; AFSK_HDLC_Init(&h,&o);
    uint8_t byte=0xf8,bit; unsigned count=0;
    while(AFSK_HDLC_Next(&h,&byte,1,1,&bit)) ++count;
    assert(count==25);
    app_tx_info_t bad={.size=2}; assert(AFSK_TX_Info(&bad)==APP_AFSK_BAD_ARGUMENT);
    puts("AFSK service: validation, copied buffers, final-symbol timing, PTT, timeout, lateness, cancel and cleanup passed");
}
