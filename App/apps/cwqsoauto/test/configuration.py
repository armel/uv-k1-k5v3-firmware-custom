"""Compare persisted-config behavior with the frozen 88af6b16 implementation.

functional.py first regenerates a host translation of the production source.
The oracle below deliberately retains the old, independent stack-buffer logic.
"""
from pathlib import Path
import json
import os
import subprocess
import sys

p = Path(__file__).resolve().parent
subprocess.run([sys.executable, str(p / "functional.py")], check=True)
code = (p / "out/functional.c").read_text()
code = code.replace("int main(void)", "int functional_main(void)")
oracle = r'''
static void referenceLoadConfig(void)
{
    config_t cfg;
    A->cfg_load((uint8_t *)&cfg, sizeof(cfg));
    if (cfg.magic == CFG_MAGIC && cfg.wpm >= WPM_MIN && cfg.wpm <= WPM_MAX &&
        cfg.mode <= MODE_PADDLE && cfg.reverse <= 1u) {
        g.wpm = cfg.wpm; g.mode = cfg.mode; g.reverse = cfg.reverse;
        g.calSteps = cfg.calSteps >= CAL_MIN && cfg.calSteps <= CAL_MAX
            ? cfg.calSteps : CAL_DEFAULT;
    } else {
        g.wpm = WPM_DEFAULT; g.mode = MODE_STRAIGHT;
        g.reverse = false; g.calSteps = CAL_DEFAULT;
    }
    g.rxPitchHz = cfg.magic == CFG_MAGIC && cfg.rxPitchHz >= RX_PITCH_MIN &&
        cfg.rxPitchHz <= RX_PITCH_MAX ? cfg.rxPitchHz : RX_PITCH_DEFAULT;
    g.breakInMs = cfg.magic == CFG_MAGIC && cfg.breakInMs >= BREAKIN_MIN &&
        cfg.breakInMs <= BREAKIN_MAX ? cfg.breakInMs : BREAKIN_DEFAULT;
    g.speaker = cfg.magic == CFG_MAGIC && cfg.speaker <= 1u ? cfg.speaker : true;
    g.iambic = cfg.magic == CFG_MAGIC && cfg.reservedPort <= 2u
        ? (uint8_t)(cfg.reservedPort << 4) : IAMBIC_AUTO;
}
static unsigned configCases;
static void checkConfig(const uint8_t *input)
{
    uint8_t actual[12];
    memcpy(saved,input,12); useSaved=true;
    loadConfig(); saveConfig(); memcpy(actual,saved,12);
    memcpy(saved,input,12); referenceLoadConfig(); saveConfig();
    assert(!memcmp(actual,saved,12));
    assert(!quietN && !markN && !rf);
    configCases++;
}
int main(void)
{
    setup(1,18);
    const config_t base={CFG_MAGIC,18,1,0,14,700,300,1,0};
    uint8_t input[12];
    for(unsigned byte=0;byte<12;byte++) for(unsigned value=0;value<256;value++) {
        memcpy(input,&base,12); input[byte]=(uint8_t)value; checkConfig(input);
    }
    for(unsigned offset=4;offset<=8;offset+=2)
        for(unsigned value=0;value<65536;value++) {
            memcpy(input,&base,12); input[offset]=(uint8_t)value;
            input[offset+1]=(uint8_t)(value>>8); checkConfig(input);
        }
    uint32_t random=0x88af6b16u;
    for(unsigned trial=0;trial<10000;trial++) {
        for(unsigned byte=0;byte<12;byte++) {
            random=random*1664525u+1013904223u; input[byte]=(uint8_t)(random>>24);
        }
        if(trial&1u) input[0]=CFG_MAGIC;
        checkConfig(input);
    }
    printf("{\"configuration_cases\":%u}\n",configCases);
    return 0;
}
'''
out = p / "out"
test = out / "configuration.c"
test.write_text(code + oracle)
exe = out / "configuration.exe"
result = subprocess.run([os.environ.get("HOST_CC", "gcc"), "-std=c11", "-O2",
                         "-Wall", "-Wextra", "-fstrict-aliasing", "-ftrapv",
                         str(test), "-o", str(exe)], capture_output=True)
(out / "configuration-build.log").write_bytes(result.stdout + result.stderr)
assert result.returncode == 0, result.stderr.decode()
result = subprocess.run([str(exe)], capture_output=True)
(out / "configuration-run.log").write_bytes(result.stdout + result.stderr)
assert result.returncode == 0, result.stderr.decode()
summary = json.loads(result.stdout)
(out / "configuration.json").write_text(json.dumps(summary, indent=2))
print(summary)
