/* Host harness for the KISS TNC (test_tnc.py): runs app_main against a mock
 * API. argv: assets.bin adc.bin (uint16 samples) host.bin (bytes from the KISS
 * client, available from the start). Prints the KISS bytes sent to the host
 * ("OUT" lines), the modulator's tone writes with their cycle time and the
 * screen's text lines ("LINE").
 * RSSI=dBm: the signal the receiver reads (default -120); SQL=1: the squelch
 * open all along (BK4819 REG_0C bit 1), but for 100 ms after a key-up; SQL=2:
 * likewise at SQL 0 (REG_78's open threshold 0); LATE=n,s:
 * the last n bytes from the host arrive at s seconds; WIDE=1: a wide VFO, 2: a
 * narrower one (6.25 kHz); RING=1: the host's bytes through a 256-byte ring at
 * USB full speed, as the firmware's port has it; REPLY=n: the last n bytes from
 * the host come 55 ms after the app first hands it a frame (a host answering).
 * blit_full takes 11 ms, as on the radio. */
#define HOST_TEST
#define _Static_assert(...)   /* the API size check assumes 32-bit pointers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_hw.h"
#include "../../app_api.h"

uint32_t host_reg[16];
static uint64_t hc;                          /* cycles since start */
static uint16_t *adc; static size_t nadc, iadc;
static uint8_t *hin; static size_t nhin, ihin;
static unsigned char assets[4096]; static size_t nassets;

uint32_t host_systval(void){ hc += 1250; return SYST_LOAD - (uint32_t)(hc % (SYST_LOAD + 1u)); }
uint32_t host_adc(void){ return iadc < nadc ? adc[iadc++] : (iadc++, 2048u); }

static size_t late; static uint64_t late_at;   /* LATE=n,s: the last n host bytes only from s seconds on */
static int reply;                            /* REPLY=n: the last n host bytes come when the app hands the host a frame */
static int ring;                             /* RING=1: the port's 256-byte ring, bytes at USB full speed */
static uint16_t s_read(uint8_t port, void *b, uint16_t len){
    if(port != APP_SERIAL_USB) return 0;
    size_t have = hc < late_at ? nhin - late : nhin;
    if(ring){                                /* 64 bytes every 50 us, the early ones from the app's first read, */
        static uint64_t t0; if(!t0) t0 = hc; /* the late ones from their time; a lap loses the oldest */
        size_t early = nhin - late, sent = (size_t)((hc - t0) / 2400u + 1u) * 64u;
        have = sent < early ? sent : early;
        if(hc >= late_at){ size_t l = (size_t)((hc - late_at) / 2400u + 1u) * 64u; have += l < late ? l : late; }
        if(have > ihin + 256u){ ihin = have - 256u; printf("LAPPED %llu\n", (unsigned long long)hc); }
    }
    size_t n = have > ihin ? have - ihin : 0; if(n > len) n = len;
    memcpy(b, hin + ihin, n); ihin += n; return (uint16_t)n;
}
/* FAILW=n: the n-th write times out (the host did not take it). */
static int failw, nwrites; static uint8_t sevt;
static bool s_write(uint8_t port, const void *v, uint16_t len){
    const uint8_t *b = v;
    if(port != APP_SERIAL_USB) return false;
    if(++nwrites == failw){ printf("DROP %u\n", len); sevt |= APP_SERIAL_TIMEOUT; return false; }
    if(len){ printf("OUT"); for(int i = 0; i < len; i++) printf(" %02X", b[i]); printf("\n"); }
    if(reply && len > 2 && b[0] == 0xC0 && b[1] == 0x00 && late_at == ~0ull)
        late_at = hc + 55u * 48000u;            /* the host answers a frame 55 ms later */
    return true;
}
static uint8_t s_ctl(uint8_t port, uint32_t baud){
    (void)baud;
    if(port != APP_SERIAL_USB) return 0;
    uint8_t st = APP_SERIAL_OK | APP_SERIAL_HOST | sevt; sevt = 0; return st;
}
static int lockkey;                          /* LOCK=1: # held 0.5-1.1 s (lock), again 1.5-2.1 s (unlock); 2: a 0.2 s tap */
static uint8_t key(void){
    if(lockkey == 1 && ((iadc >= 4800u && iadc < 10560u) || (iadc >= 14400u && iadc < 20160u))) return APP_KEY_F;
    if(lockkey == 2 && iadc >= 4800u && iadc < 6720u) return APP_KEY_F;
    return iadc > nadc + 9600u * 3u ? APP_KEY_EXIT : APP_KEY_INVALID;
}
static void bkw(uint8_t r, uint16_t v){
    if(r == 0x71) printf("TONE %llu %u\n", (unsigned long long)hc, v);
    if(r == 0x70 && (v & 0x8000u)) printf("GAIN %u\n", (v >> 8) & 0x7Fu);
    if(r == 0x43) printf("R43 %04X\n", v);
    if(r == 0x2B) printf("R2B %04X\n", v);
}
static int sql_open;
static uint64_t tx_end_at;                  /* the squelch closes for 100 ms after a key-up */
static int wide;                             /* WIDE=1: REG_43 as on a wide VFO, 2 a narrower one */
static uint16_t bkr(uint8_t r){
    if(r == 0x43) return wide == 2 ? 0x2058u : wide ? 0x3028u : 0x4048u;
    if(r == 0x78) return sql_open == 2 ? 0x0000u : 0x1914u;   /* the open threshold 0: SQL 0 */
    return r == 0x0C && sql_open && hc - tx_end_at > 4800000u ? 2u : 0u;
}
static void txtone(uint16_t hz){ (void)hz; printf("TXCALL tone\n"); }
static void txset(void){ printf("TXCALL set_params\n"); }
static void txprep(void){ printf("TXCALL prepare\n"); }
static void setaf(uint8_t m){ printf("TXCALL af%u\n", m); }
static void ledf(bool on){ static int last = -1; if(on != last){ printf("LED %d\n", on); last = on; } }
static void txcarrier(bool on){ if(on) printf("TXSTART %llu\n", (unsigned long long)hc); }
static void txmute(bool m){ (void)m; }
static void txend(void){ printf("TXEND %llu\n", (unsigned long long)hc); tx_end_at = hc; }
static uint8_t txstate(void){ return 0; }
static uint32_t ticks(void){ return (uint32_t)(hc / 48000u); }
static uint32_t rnd(void){ return 0; }       /* p-persistence: always this slot */
static int16_t rssi_v = -120;
static int16_t rssi(void){ return rssi_v; }
static uint32_t rxf(void){ return 14480000u; }
static uint16_t aread(uint16_t off, void *buf, uint16_t len){
    if(off >= nassets) return 0; if(len > nassets - off) len = (uint16_t)(nassets - off);
    memcpy(buf, assets + off, len); return len;
}
static void cfgl(uint8_t *b, uint8_t n){ memset(b, 0xFF, n); }
static void cfgs(const uint8_t *b, uint8_t n){ (void)b; (void)n; }
static char *au32(char *o, uint32_t u){ return o + sprintf(o, "%u", u); }
static char *ai32(char *o, int32_t v){ return o + sprintf(o, "%d", v); }
static void dfreq(uint32_t f, uint8_t x, uint8_t l){ (void)f; (void)x; (void)l; }
static void led(bool on){ (void)on; }
static uint8_t status[128];
static void sclear(void){ memset(status, 0, sizeof status); }
static void bfull(void){ hc += 528000u; }     /* the whole screen to the LCD: 11 ms */
static void bstat(void){ printf("STATUS"); for(int i = 72; i < 81; i++) printf(" %02X", status[i]); printf("\n"); }
static void nop(void){} static void nopb(bool b){ (void)b; } static void nop8(uint8_t v){ (void)v; }
static void dly(uint32_t m){ hc += (uint64_t)m * 48000u; }
static void pn(const char *s, uint8_t x, uint8_t b, uint8_t l){ (void)b; printf("LINE %u %u %s\n", l, x, s); }
static void pi(const char *s, uint8_t x, uint8_t l, bool sb, bool f, uint8_t e){ (void)s; (void)x; (void)l; (void)sb; (void)f; (void)e; }

#include "app.c"

static void *slurp(const char *p, size_t *n){
    FILE *f = fopen(p, "rb"); if(!f){ *n = 0; return NULL; }
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(*n + 1); *n = fread(b, 1, *n, f); fclose(f); return b;
}
int main(int argc, char **argv){
    size_t n; void *a = slurp(argv[1], &n); memcpy(assets, a, n); nassets = n;
    adc = slurp(argv[2], &nadc); nadc /= 2;
    hin = slurp(argv[3], &nhin);
    if(getenv("RSSI")) rssi_v = (int16_t)atoi(getenv("RSSI"));
    if(getenv("SQL")) sql_open = atoi(getenv("SQL"));
    if(getenv("WIDE")) wide = atoi(getenv("WIDE"));
    if(getenv("RING")) ring = atoi(getenv("RING"));
    if(getenv("REPLY")){ late = (size_t)atoi(getenv("REPLY")); late_at = ~0ull; reply = 1; }
    if(getenv("LATE")){ late = (size_t)atoi(getenv("LATE")); late_at = (uint64_t)(atof(strchr(getenv("LATE"), ',') + 1) * 48e6); }
    if(getenv("FAILW")) failw = atoi(getenv("FAILW"));
    static app_api_t api;
    api.display_clear = nop; api.status_clear = nop; api.blit_full = nop; api.blit_status = nop;
    api.get_key = key; api.delay_ms = dly; api.print_normal = pn; api.print_inverse = pi;
    api.rssi_dbm = rssi; api.bk_read = bkr; api.bk_write = bkw; api.set_agc = nopb; api.set_af = setaf;
    api.audio_path = nopb; api.rx_freq = rxf; api.cfg_load = cfgl; api.cfg_save = cfgs;
    api.draw_battery = nop; api.battery_sample = nop; api.backlight_on = nop; api.backlight_update = nop;
    api.tx_state = txstate; api.tx_set_params = txset; api.tx_prepare = txprep; api.tx_carrier = txcarrier; api.tx_tone = txtone; api.tx_mute = txmute; api.tx_end = txend;
    api.ticks_ms = ticks; api.rand32 = rnd; api.asset_read = aread;
    api.status_line = status; api.blit_status = bstat; api.status_clear = sclear; api.blit_full = bfull;
    if(getenv("LOCK")) lockkey = atoi(getenv("LOCK"));
    api.append_u32 = au32; api.append_i32 = ai32; api.draw_frequency = dfreq; api.led = ledf;
    api.serial_read = s_read; api.serial_write = s_write; api.serial_ctl = s_ctl;
    app_main(&api);
    return 0;
}
