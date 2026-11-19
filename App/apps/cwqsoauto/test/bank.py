"""Bank ABI, future proxy descriptor contract and production-reader tests.

Run after build.py and functional.py. All edits below are to simulated RAM;
this does not exercise a physical Flash commit or implement a configurator.
"""
from pathlib import Path
import json
import os
import re
import struct
import subprocess

p = Path(__file__).resolve().parent
app = p.parent
root = p.parents[3]
header = (app / "cwqsoauto_assets.h").read_text()
constant = lambda name: int(re.search(r"#define " + name + r" (\d+)u", header)[1])
data = (app / "cwqsoauto_assets.bin").read_bytes()
base, stride = constant("MSG_MODELS"), constant("MSG_STRIDE")
assert (base, stride, constant("MSG_BYTES"), constant("MSG_COUNT")) == (512, 68, 64, 16)
expected = [b"CQ CQ CQ DE F4XXX F4XXX K", b"DE F4XXX UR \x02 \x02 NAME SEB HW?",
            b"73 TU E E"] + [f"MSG {i}".encode() for i in range(4, 15)] + [b"AGN?", b"?"]
proxy = (root / "App/apps/app_config_proxy.h").read_text()
assert int(re.search(r"APP_CONFIG_PROXY_MAX\s+(\d+)u", proxy)[1]) >= stride
offsets = []
for i, text in enumerate(expected):
    off = base + i * stride
    signature = b"CW" + bytes([1, i + 1])
    assert data[off:off + 4] == signature
    assert data[off + 4:off + stride] == (text + b"\0").ljust(64, b"\0")
    magic = int.from_bytes(signature, "little")
    # Exact packed layout of app_config_proxy_t (28 bytes).
    descriptor = struct.pack("<I16sHBBI", 0x31505843, b"CW QSO Auto", off, stride, 0, magic)
    assert len(descriptor) == 28 and off + stride <= len(data) <= 3840
    offsets.append(dict(message=i + 1, record=off, text=off + 4, magic=hex(magic)))

code = (p / "out/functional.c").read_text()
code = code.replace("static const uint8_t assets[]={", "static uint8_t assets[]={")
code = code.replace("int main(void)", "int functional_main(void)")
code += r'''
static unsigned bankCases;
static uint8_t original[sizeof(assets)];
static unsigned record(unsigned action){return MSG_MODELS+action*MSG_STRIDE;}
static void edit(unsigned action,const uint8_t *bytes,unsigned len){
    assert(len<=MSG_BYTES);
    unsigned off=record(action)+MSG_HEADER;
    memset(assets+off,0,MSG_BYTES);memcpy(assets+off,bytes,len);
    assert(!memcmp(assets,original,off));
    assert(!memcmp(assets+off+MSG_BYTES,original+off+MSG_BYTES,sizeof(assets)-off-MSG_BYTES));
}
static void clean(void){memcpy(assets,original,sizeof(assets));}
int main(void){
    memcpy(original,assets,sizeof(assets));
    setup(1,18);
    /* Every input byte: unsupported values must yield no Morse code. */
    for(unsigned c=0;c<256;c++){
        bool supported=(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='?';
        assert((morse(c)!=0)==supported);bankCases++;
    }
    for(unsigned a=0;a<16;a++){
        clean();setup(1,18);q.message=a;
        edit(a,(const uint8_t *)"CUSTOM \x02",8);
        strcpy(q.rst,"057");
        char result[16];unsigned n=0;
        do{assert(n<sizeof(result));result[n]=(char)nextCharacter();}while(result[n++]);
        assert(!strcmp(result,"CUSTOM 057"));bankCases++;
        for(unsigned length=1;length<=64;length++){
            clean();setup(1,60);q.message=a;
            uint8_t payload[64];memset(payload,'E',length);edit(a,payload,length);
            startAuto();runAuto();assert(markN==length && endN==1 && !rf && !g.txReady);
            assert(q.cursor<=65);bankCases++;
        }
        /* Bad first byte never prepares TX; bad following byte stops in RX. */
        const uint8_t bad[]={0,' ','.','a',0xff};
        for(unsigned b=0;b<sizeof(bad);b++){
            clean();setup(1,18);q.message=a;edit(a,bad+b,1);
            startAuto();runAuto();assert(!markN && !quietN && !rf && !g.txReady);bankCases++;
            clean();setup(1,18);q.message=a;
            uint8_t text[]={'E',bad[b]};edit(a,text,2);
            startAuto();runAuto();assert(markN==1 && endN==1 && !rf && !g.txReady);bankCases++;
        }
        clean();setup(1,18);q.message=a;
        edit(a,(const uint8_t *)"E  E",4);startAuto();runAuto();
        assert(markN==1 && endN==1 && !rf);bankCases++;
        /* A token in the last byte expands without reading the next record. */
        clean();setup(1,60);q.message=a;
        uint8_t text[64];memset(text,'E',63);text[63]=RST_TOKEN;edit(a,text,64);
        unsigned nread=0;while(nextCharacter()){assert(++nread<=66);}
        assert(nread==66);bankCases++;
    }
    puts("PASS bank reader: mutations, 64-byte boundary, all 256 input bytes, RST and RX");
    printf("{\"native_bank_cases\":%u}\n",bankCases);
    return 0;
}
'''
out = p / "out"
source, exe = out / "bank.c", out / "bank.exe"
source.write_text(code)
subprocess.run([os.environ.get("HOST_CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                str(source), "-o", str(exe)], check=True)
run = subprocess.run([str(exe)], capture_output=True, text=True)
assert run.returncode == 0, run.stdout + run.stderr
print(run.stdout, end="")
(out / "bank.json").write_text(json.dumps(dict(records=offsets, native=json.loads(run.stdout.splitlines()[-1]),
                                               proxy_contract="16 individually addressable 68-byte blocks; fixed target per launch"), indent=2))
print("PASS 16 initial records and 16 CFG_PROXY descriptor layouts (not a Flash integration test)")
