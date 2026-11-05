#!/usr/bin/env python3
"""Host test of atisdscrx_app.c itself: the app is compiled for the host with a
simulated app_api_t, fed the ADC samples of test/model_dsc.py's channel (the
Flipper and sine sources, noise), and the texts it draws are compared with
what each call must show.

Linux only (the app's section attribute is ELF): run it in a Linux container,
e.g. mcr.microsoft.com/devcontainers/python:3.10-bookworm.

  host_test.py           builds the harness, runs every case, exits 1 on a failure
"""
import os, random, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import model_dsc as M

HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* app_api.h pins 32-bit offsets; the target build checks them */
#define _Static_assert(c, m)
#include "atisdscrx_app.c"

static uint8_t assets[4096]; static unsigned assetLen;
static uint16_t *adc; static unsigned nAdc, pos;
static uint8_t fb[7][128], status[128];
static char lines[8][64];
static unsigned beeps;

static uint16_t asset(uint16_t off,void *buf,uint16_t len){
    unsigned n=off<assetLen?assetLen-off:0; if(n>len)n=len;
    memcpy(buf,assets+off,n); return (uint16_t)n;
}
static uint16_t sampleAdc(void){ uint16_t v=pos<nAdc?adc[pos]:2048; pos++; return v; }   /* the clock runs on past the end */
static uint32_t cycles(void){ return pos*5000u; }
static uint32_t ticks(void){ return pos/96u*10u; }
/* EXIT 3 s after the last sample: the screen is served between calls, up to 1 s on */
/* Radio timing (optional argv 3..6): the samples a key scan and a full blit
 * take on the radio are lost (pos runs on), and the squelch (REG_0C bit 1) is
 * open over the carrier [sqOn, sqOff). */
static unsigned gapKey, gapBlit, sqOn, sqOff;
static uint8_t key(void){ pos+=gapKey; return pos>=nAdc+3u*9600u?APP_KEY_EXIT:APP_KEY_INVALID; }
static void noop(void){}
static void noopB(bool on){ (void)on; }
static void noopU8(uint8_t v){ (void)v; }
static void noopD(uint32_t ms){ (void)ms; }
static uint16_t bkRead(uint8_t r){ return r==0x0Cu && pos>=sqOn && pos<sqOff ? 2u : 0u; }
static int16_t rssi(void){ return -73; }
static uint32_t freq(void){ return 15652500u; }
static char *appendI(char *d,int32_t v){ return d+sprintf(d,"%d",(int)v); }
static void load(uint8_t *b,uint8_t n){ memset(b,0xFF,n); }
static void save(const uint8_t *b,uint8_t n){ (void)b; (void)n; }
static void beepTone(uint16_t hz,uint16_t ms){ (void)hz; (void)ms; beeps++; }
static void clear(void){ memset(lines,0,sizeof lines); }
static void bold(const char *s,uint8_t a,uint8_t b,uint8_t line){ (void)a;(void)b; if(line<8) snprintf(lines[line],64,"%s",s); }
static void normal(const char *s,uint8_t a,uint8_t b,uint8_t line){ (void)a;(void)b; if(line<8) snprintf(lines[line],64,"%s",s); }
static void tiny(const char *s,uint8_t x,uint8_t y,bool st,bool f){ (void)x;(void)st;(void)f; if(y==49) snprintf(lines[7],64,"%s",s); }
static void inverse(const char *s,uint8_t x,uint8_t y,bool st,bool f,uint8_t e){ (void)s;(void)x;(void)y;(void)st;(void)f;(void)e; }
static void bigfreq(uint32_t f,uint8_t x,uint8_t l){ (void)f;(void)x;(void)l; }
static void full(void){
    pos+=gapBlit;
    printf("SCREEN beeps=%u\n",beeps);
    for(unsigned i=0;i<8;i++) if(lines[i][0]) printf("%u|%s\n",i,lines[i]);
}

int main(int argc,char **argv){
    FILE *f=fopen(argv[1],"rb"); assetLen=(unsigned)fread(assets,1,sizeof assets,f); fclose(f);
    f=fopen(argv[2],"rb"); fseek(f,0,SEEK_END); nAdc=(unsigned)(ftell(f)/2); rewind(f);
    if(argc>6){ gapKey=(unsigned)atoi(argv[3]); gapBlit=(unsigned)atoi(argv[4]); sqOn=(unsigned)atoi(argv[5]); sqOff=(unsigned)atoi(argv[6]); }
    adc=malloc(nAdc*2u); if(fread(adc,2,nAdc,f)!=nAdc) return 2; fclose(f);
    static app_api_t api;
    api.fb=fb; api.status_line=status;
    api.display_clear=clear; api.status_clear=noop; api.print_bold=bold; api.print_normal=normal;
    api.print_tiny=tiny; api.print_inverse=inverse; api.blit_full=full; api.blit_status=noop;
    api.get_key=key; api.delay_ms=noopD; api.play_tone=beepTone; api.rssi_dbm=rssi; api.bk_read=bkRead;
    api.set_af=noopU8; api.audio_path=noopB; api.led=noopB; api.tones_off_rx=noop; api.rx_freq=freq;
    api.cfg_load=load; api.cfg_save=save; api.draw_battery=noop; api.battery_sample=noop;
    api.backlight_on=noop; api.backlight_update=noop; api.ticks_ms=ticks; api.asset_read=asset;
    api.append_i32=appendI; api.af_capture_open=noop; api.af_capture_sample=sampleAdc;
    api.af_capture_close=noop; api.cycles_48mhz=cycles; api.draw_frequency=bigfreq;
    app_main(&api);
    printf("END beeps=%u\n",beeps);
    return 0;
}
'''

# what each call must show: the title, the rows under it, and the beeps
# (a distress or urgency call: three)
COAST = "002275300 France"
SHIP = "227006480 France"
EXPECT = {
    "distress":   (["DISTRESS", SHIP, "SINKING 14:35", "43 18N 005 22E"], 3),
    "dist_nopos": (["DISTRESS", SHIP, "UNSPECIFIED --:--", "POSITION ?"], 3),
    "dist_ack":   (["DISTRESS ACK", COAST, "TO ALL SHIPS", "MAYDAY 227006480",
                    "SINKING 14:35", "43 18N 005 22E"], 3),
    "urgency":    (["URGENCY", COAST, "TO ALL SHIPS", "F3E SIMPLEX CH16"], 3),
    "safety":     (["SAFETY", COAST, "TO ALL SHIPS", "F3E SIMPLEX CH67"], 0),
    "routine":    (["ROUTINE", SHIP, "TO 228123456", "F3E SIMPLEX CH72"], 0),
    "test":       (["TEST", SHIP, "TO 002275300", "TEST"], 0),
}


def run(exe, assets, adc, timing=()):
    with tempfile.NamedTemporaryFile(suffix=".raw", delete=False) as f:
        f.write(b"".join(int(v).to_bytes(2, "little") for v in adc))
        path = f.name
    try:
        out = subprocess.run([exe, assets, path] + [str(t) for t in timing],
                             capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(path)
    screens, cur = [], None
    for ln in out.splitlines():
        if ln.startswith("SCREEN"):
            cur = {"beeps": int(ln.split("=")[1]), "rows": {}}
            screens.append(cur)
        elif ln.startswith("END"):
            beeps = int(ln.split("=")[1])
        else:
            k, v = ln.split("|", 1)
            cur["rows"][int(k)] = v
    return screens, beeps


def shown(screens):
    """The rows 0..5 of the last screen that shows a call (none: [])."""
    for s in reversed(screens):
        if s["rows"].get(7, "").startswith("-73dBm"):
            return [s["rows"][i] for i in range(6) if i in s["rows"]]
    return []


GAP_KEY = 4        # samples a key scan takes on the radio (~400 us, atisrx_app.c)
GAP_BLIT = 106     # samples a full blit takes (~11 ms, app_api.h lcd_feed)


def timing(exe, assets, trials=24):
    """Each call sent with a random lead of noise (0 to 1.2 s), so that it
    starts anywhere in the 50 ms key-scan cadence and the 5 s refresh, with
    the samples a key scan and a blit take lost as on the radio. Returns the
    calls decoded / sent."""
    tb = M.test_bits()
    rnd = random.Random(5)
    ok = sent = 0
    for name in ("distress", "dist_ack", "urgency", "routine"):
        want = EXPECT[name][0]
        wave = M.flipper_wave(tb[name][0])
        for t in range(trials):
            gap = rnd.randint(0, 1200)
            adc = M.channel(wave, "std", 0, 0, 0, t + 1, gap_ms=gap)
            on = gap * M.FS_ADC // 1000
            off = on + len(wave) * M.FS_ADC // M.FS_SIM
            got = shown(run(exe, assets, adc, (GAP_KEY, GAP_BLIT, on, off))[0])
            ok += got == want
            sent += 1
    return ok, sent


def main():
    tmp = tempfile.mkdtemp()
    assets, header = os.path.join(tmp, "atisdscrx_assets.bin"), os.path.join(tmp, "atisdscrx_assets.h")
    subprocess.run([sys.executable, "-B", os.path.join(APP, "gen_assets.py"), assets, header], check=True)
    src, exe = os.path.join(tmp, "harness.c"), os.path.join(tmp, "harness")
    with open(src, "w") as f:
        f.write(HARNESS)
    subprocess.run(["gcc", "-std=gnu11", "-O1", "-fno-builtin", "-Wall", "-Wextra",
                    "-Wno-unused-parameter", "-I", tmp, "-I", APP, "-o", exe, src], check=True)

    tb = M.test_bits()
    fails = 0
    cases = [(n, "flipper", 0.0, "std", 0) for n in tb]   # the app reads the STD audio
    cases += [(n, "sine", tw, "std", 1500) for tw in (0.0, 4.2) for n in ("distress", "dist_ack")]
    for name, src_kind, tw, mode, noise in cases:
        b = tb[name][0]
        wave = M.flipper_wave(b) if src_kind == "flipper" else M.sine_wave(b, twist_db=tw)
        screens, beeps = run(exe, assets, M.channel(wave, mode, noise, 0, 0, 1))
        got = shown(screens)
        want, beep = EXPECT.get(name, ([], 0))
        ok = got == want and beeps == beep
        fails += not ok
        print("%-4s %-7s %-4s %-10s %s" % ("ok" if ok else "FAIL", src_kind, mode, name,
                                           " / ".join(got) or "nothing") + ("" if ok else
              "\n     want " + " / ".join(want) + "  beeps %d, got %d" % (beep, beeps)))
    # two calls in a row: both kept, the newest shown, "2/2" possible
    seq = M.channel(M.flipper_wave(tb["routine"][0] + [0] * 600 + tb["distress"][0]), "std", 0, 0, 0, 1)
    got = shown(run(exe, assets, seq)[0])
    ok = got == EXPECT["distress"][0]
    fails += not ok
    print("%-4s routine then distress -> %s" % ("ok" if ok else "FAIL", " / ".join(got)))
    rnd = random.Random(9)
    x = [rnd.gauss(0, 6000) for _ in range(30 * M.FS_SIM)]
    x = M.rc_hp(M.biquad_lp(x, 5000), 30)
    adc = [min(4095, max(0, int(round(M.BIAS + M.LSB_PER_HZ * x[i * 10])))) for i in range(len(x) // 10)]
    got = shown(run(exe, assets, adc)[0])
    fails += bool(got)
    print("%-4s noise 30 s -> %s" % ("ok" if not got else "FAIL", " / ".join(got) or "nothing"))
    # The UV-K1 itself decodes 9 or 10 calls in 10 from a LimeSDR (STD audio,
    # 1.5 to 3.5 kHz of deviation): 95 % is the floor here.
    ok, sent = timing(exe, assets)
    good = ok * 100 >= sent * 95
    print("%-4s radio timing (key scan and blit gaps, squelch): %d/%d calls decoded (floor 95 %%)" %
          ("ok" if good else "FAIL", ok, sent))
    fails += not good
    print("failures: %d" % fails)
    return fails


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
