"""Render production draw() at 128x64 with the resident fonts/helpers.

Run configuration.py (or functional.py) first to regenerate the host harness.
Battery sampling/icon is stubbed; these are simulated screens, not photographs.
"""
from pathlib import Path
import os
import re
import struct
import subprocess
import zlib

p = Path(__file__).resolve().parent
root = p.parents[3]
out = p / "out-visual"
out.mkdir(exist_ok=True)
code = (p / "out/functional.c").read_text().replace("int main(void)", "int functional_main(void)")
code = code.replace("static const uint8_t assets[]={", "static uint8_t assets[]={")
helper = (root / "App/ui/helper.c").read_text()

def function(name):
    match = re.search(r"\bvoid " + name + r"\([^;]*?\)\s*\{", helper, re.S)
    start = match.start()
    end = helper.index("{", start) + 1
    depth = 1
    while depth:
        depth += (helper[end] == "{") - (helper[end] == "}")
        end += 1
    return helper[start:end]

renderer = '\n#include "' + (root / "App/font.h").as_posix() + '"\n'
renderer += "#define gFrameBuffer fb\n#define gStatusLine status\n"
renderer += "\n".join(function(name) for name in [
    "UI_DrawPixelBuffer", "PutPixel", "PutPixelStatus", "GUI_DisplaySmallest",
    "GUI_DisplaySmallestInverse", "UI_PrintStringBuffer", "UI_PrintStringSmall",
    "UI_PrintStringSmallNormal"])
renderer += r'''
static void renderNormal(const char *s,uint8_t x,uint8_t end,uint8_t line){
    normal(s,x,end,line); UI_PrintStringSmallNormal(s,x,end,line);
}
static void renderInverse(const char *s,uint8_t x,uint8_t line,bool st,bool fill,uint8_t end){
    inverse(s,x,line,st,fill,end);GUI_DisplaySmallestInverse(s,x,line,st,fill,end);
}
static void renderTiny(const char *s,uint8_t x,uint8_t y,bool st,bool fill){
    assert(!st && x+strlen(s)*4<=128 && y+6<=56);
    GUI_DisplaySmallest(s,x,y,st,fill);
}
static void renderClear(void){clearScreen();memset(fb,0,sizeof(fb));}
static void renderStatusClear(void){memset(status,0,sizeof(status));}
static void frame(const char *name){
    api.print_normal=renderNormal;api.print_inverse=renderInverse;
    api.print_tiny=renderTiny;
    api.display_clear=renderClear;api.status_clear=renderStatusClear;
    draw();assert(!quietN && !markN);
    FILE *file=fopen(name,"wb");assert(file);fprintf(file,"P4\n128 64\n");
    for(unsigned y=0;y<64;y++)for(unsigned block=0;block<16;block++){
        unsigned packed=0;
        for(unsigned bit=0;bit<8;bit++){
            unsigned x=block*8+bit;
            uint8_t pixels=y<8?status[x]:fb[(y-8)/8][x];
            packed=(packed<<1)|((pixels>>(y%8))&1u);
        }
        fputc(packed,file);
    }
    assert(!fclose(file));
}
int main(void){
    for(unsigned selection=0;selection<16;selection++){
        setup(1,18);q.selected=selection;
        char name[32];sprintf(name,"qso-msg-%02u.pbm",selection+1);frame(name);
    }
    setup(1,18);frame("qso-lock.pbm");
    setup(1,18);q.selected=1;strcpy(q.rst,"579");frame("qso-ready.pbm");
    q.active=true;q.message=14;frame("qso-side-tx.pbm");
    setup(1,18);q.selected=2;frame("qso-73.pbm");
    setup(1,18);q.settings=true;g.iambic=IAMBIC_B;frame("settings.pbm");
    g.fArm=true;frame("settings-f.pbm");
    setup(0,5);q.settings=true;frame("settings-straight.pbm");
    setup(1,18);q.settings=true;frame("settings-auto.pbm");
    g.iambic=IAMBIC_A;frame("settings-iamb-a.pbm");
    g.iambic=IAMBIC_B;g.wpm=60;g.rxPitchHz=900;g.breakInMs=1500;
    g.calSteps=-50;g.reverse=true;frame("settings-iamb-b.pbm");
    for(unsigned algo=0;algo<3;algo++){
        setup(1,18);q.settings=true;g.iambic=algo<<4;draw();
        handleUiKeyDirection(APP_KEY_1,0);assert(g.redraw);
        char name[48];sprintf(name,"settings-cleared-%u.pbm",algo);frame(name);
        for(unsigned x=0;x<128;x++)assert(!fb[3][x]);
    }
    setup(1,18);q.settings=true;g.iambic=IAMBIC_B;draw();
    handleUiKeyDirection(APP_KEY_8,0);assert(g.iambic==IAMBIC_AUTO && g.redraw);
    frame("settings-b-to-auto.pbm");
    for(unsigned x=28;x<128;x++)assert(!fb[3][x]);
    uint8_t *preview=assets+MSG_MODELS+3*MSG_STRIDE+MSG_HEADER;
    setup(1,18);q.selected=3;memset(preview,0,64);memcpy(preview,"TU F5ABC",8);
    frame("preview-custom-short.pbm");
    memset(preview,0,64);memcpy(preview,"CQ CQ CQ DE F5ABC F5ABC K",25);
    frame("preview-custom-long.pbm");
    memset(preview,0,64);memcpy(preview,"UR \2 TU",7);
    frame("preview-custom-rst.pbm");
    memset(preview,0,64);frame("preview-empty.pbm");
    memset(preview,RST_TOKEN,64);frame("preview-64-rst.pbm");
    q.selected=15;preview=assets+MSG_MODELS+15*MSG_STRIDE+MSG_HEADER;
    memset(preview,'E',64);frame("preview-64-bytes.pbm");
    return 0;
}
'''
test = out / "render.c"
test.write_text(code + renderer)
exe = out / "render.exe"
subprocess.run([os.environ.get("HOST_CC", "gcc"), "-std=c11", "-O1", "-Wall", "-Wextra",
                str(test), str(root / "App/font.c"), "-o", str(exe)], check=True)
subprocess.run([str(exe)], cwd=out, check=True)

def png(path, pixels, scale=1):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))
    rows = []
    for row in pixels:
        expanded = bytes(value for value in row for _ in range(scale))
        rows.extend([b"\0" + expanded] * scale)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 128*scale, 64*scale, 8, 0, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))

for file in sorted(out.glob("*.pbm")):
    bitmap = file.read_bytes().split(b"\n", 2)[2]
    assert len(bitmap) == 1024
    pixels = [[0 if bitmap[y*16+x//8] & (128 >> (x%8)) else 255
               for x in range(128)] for y in range(64)]
    png(file.with_suffix(".png"), pixels)
    png(file.with_name(file.stem + "-4x.png"), pixels, 4)
print("PASS 36 screens: 16 selections, six custom previews, four SETTINGS modes, cleared STRAIGHT and shrinking B-to-AUTO frame")
