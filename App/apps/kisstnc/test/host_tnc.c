/* Host harness for the KISS TNC (test_tnc.py): runs app_main against a mock
 * API. argv: assets.bin adc.bin (uint16 samples) host.bin (bytes from the KISS
 * client, available from the start). Prints the KISS bytes sent to the host
 * ("OUT" lines) and the modulator's tone writes with their cycle time. */
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

static uint16_t s_read(uint8_t port, void *b, uint16_t len){
    if(port != APP_SERIAL_USB) return 0;
    size_t n = nhin - ihin; if(n > len) n = len;
    memcpy(b, hin + ihin, n); ihin += n; return (uint16_t)n;
}
/* FAILW=n: the n-th write times out (the host did not take it). */
static int failw, nwrites; static uint8_t sevt;
static bool s_write(uint8_t port, const void *v, uint16_t len){
    const uint8_t *b = v;
    if(port != APP_SERIAL_USB) return false;
    if(++nwrites == failw){ printf("DROP %u\n", len); sevt |= APP_SERIAL_TIMEOUT; return false; }
    if(len){ printf("OUT"); for(int i = 0; i < len; i++) printf(" %02X", b[i]); printf("\n"); }
    return true;
}
static uint8_t s_ctl(uint8_t port, uint32_t baud){
    (void)baud;
    if(port != APP_SERIAL_USB) return 0;
    uint8_t st = APP_SERIAL_OK | APP_SERIAL_HOST | sevt; sevt = 0; return st;
}
static int key2;                             /* press key 2 once, at 0.5 s */
static const char *callsign = "N0CALL";
static uint8_t key(void){
    if(key2 && iadc >= 4800u && iadc < 5400u) return APP_KEY_2;
    return iadc > nadc + 9600u * 3u ? APP_KEY_EXIT : APP_KEY_INVALID;
}
static void bkw(uint8_t r, uint16_t v){ if(r == 0x71) printf("TONE %llu %u\n", (unsigned long long)hc, v); }
static uint16_t bkr(uint8_t r){ (void)r; return 0; }
static void txtone(uint16_t hz){ (void)hz; printf("TXSTART %llu\n", (unsigned long long)hc); }
static void txmute(bool m){ if(m) printf("TXEND %llu\n", (unsigned long long)hc); }
static uint8_t txstate(void){ return 0; }
static uint32_t ticks(void){ return (uint32_t)(hc / 48000u); }
static uint32_t rnd(void){ return 0; }       /* p-persistence: always this slot */
static int16_t rssi(void){ return -90; }
static uint32_t rxf(void){ return 14480000u; }
static uint16_t aread(uint16_t off, void *buf, uint16_t len){
    if(off >= nassets) return 0; if(len > nassets - off) len = (uint16_t)(nassets - off);
    memcpy(buf, assets + off, len); return len;
}
static void cfgl(uint8_t *b, uint8_t n){ memset(b, 0xFF, n); }
static void cfgs(const uint8_t *b, uint8_t n){ (void)b; (void)n; }
static void call(char *b, uint8_t n){ (void)n; strcpy(b, callsign); }
static void nop(void){} static void nopb(bool b){ (void)b; } static void nop8(uint8_t v){ (void)v; }
static void dly(uint32_t m){ hc += (uint64_t)m * 48000u; }
static void pn(const char *s, uint8_t a, uint8_t b, uint8_t l){ (void)s; (void)a; (void)b; (void)l; }
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
    if(argc > 4){ key2 = 1; callsign = argv[4]; }   /* key 2 test frame, this callsign */
    if(getenv("FAILW")) failw = atoi(getenv("FAILW"));
    static app_api_t api;
    api.display_clear = nop; api.status_clear = nop; api.blit_full = nop; api.blit_status = nop;
    api.get_key = key; api.delay_ms = dly; api.print_normal = pn; api.print_inverse = pi;
    api.rssi_dbm = rssi; api.bk_read = bkr; api.bk_write = bkw; api.set_agc = nopb; api.set_af = nop8;
    api.audio_path = nopb; api.rx_freq = rxf; api.cfg_load = cfgl; api.cfg_save = cfgs;
    api.draw_battery = nop; api.battery_sample = nop; api.backlight_on = nop; api.backlight_update = nop;
    api.tx_state = txstate; api.tx_set_params = nop; api.tx_tone = txtone; api.tx_mute = txmute; api.tx_end = nop;
    api.boot_callsign = call; api.ticks_ms = ticks; api.rand32 = rnd; api.asset_read = aread;
    api.serial_read = s_read; api.serial_write = s_write; api.serial_ctl = s_ctl;
    app_main(&api);
    return 0;
}
