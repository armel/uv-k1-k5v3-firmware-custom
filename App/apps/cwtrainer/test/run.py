"""Compile the actual trainer against simulated GPIO/API; no radio required.
Python 3 + GCC (HOST_CC overrides gcc). Generated files stay in test/out.
"""
import os
from pathlib import Path
import re
import subprocess
import json

here = Path(__file__).resolve().parent
app = here.parent
repo = app.parents[2]
out = here / 'out'
out.mkdir(exist_ok=True)
api = (app.parent / 'app_api.h').read_text()
api = re.sub(r'_Static_assert\(.*?\);', '', api, flags=re.S)
(out / 'host_api.h').write_text(api)
def function(text, name):
    m = re.search(r'^[\w \t*]+\b' + name + r'\([^;{]*?\)\s*\{', text, re.M)
    assert m, name
    start = end = text.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end-1]

source = (app / 'cwtrainer_app.c').read_text().replace(
    '#include "../app_api.h"', '#include "host_api.h"').replace(
    'section(".text.entry"), ', '')  # ELF section name, rejected by Mach-O hosts
import runpy
runpy.run_path(str(app/'gen_assets.py'))['generate']()
source=source.replace('#include "cwtrainer_assets.h"',(app/'cwtrainer_assets.h').read_text())
prefix = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TRAINER_HOST 1
/* Native pointers are 64-bit; api_size still expresses the actual ARM ABI. */
#define TRAINER_API_END(field) (4u + ((offsetof(app_api_t,field)-offsetof(app_api_t,fb))/sizeof(void*)+1u)*4u)
static uint32_t moder, pull, clockreg, simPins=0xffffffffu;
static uint32_t simPB=0xffffffffu;
#define GPIOB_INPUT simPB
#define GPIO_MODE moder
#define GPIO_PULL pull
#define USB_CLOCK clockreg
#define GPIO_INPUT simPins
static unsigned irqDepth;
static uint32_t irqSave(void){ assert(!irqDepth); irqDepth++;return 1; }
static void irqRestore(uint32_t p){assert(p==1 && irqDepth==1);irqDepth--;}
'''

helper = (repo / 'App/ui/helper.c').read_text()
font = (repo / 'App/font.c').read_text()
font_start = font.index('const uint8_t gFontSmallPacked[')
renderer = r'''
/* v7 fonts: FW_ASSET arrays read through FW_ASSETS_Read; host reads them in place. */
#define FW_ASSET
#define FW_ASSETS_Read(src,dst,n) memcpy((dst),(src),(n))
#define FONT_GetSmallFont(variant,bold) ((void)(variant),(void)(bold),gFontSmallPacked)
#define UI_FONT_SETTING 0u
#define LCD_BLANK() false
#define FONT_SMALL_WIDTH 6u
#define FONT_SMALL_PACKED_SIZE 494u
#define LCD_WIDTH 128u
#define FRAME_LINES 7u
static uint8_t gFrameBuffer[7][128], gStatusLine[128], panel[8][128];
static unsigned lcdPage,lcdCol,lcdWrites,lcdRefreshes,cs;
static void CS_Assert(void){assert(!cs);cs=1;}
static void CS_Release(void){assert(cs);cs=0;}
static void ST7565_WriteByte(uint8_t b){assert(cs && b==0x40);}
static void ST7565_SelectColumnAndLine(unsigned c,unsigned l){assert(cs && c==4 && l<8);lcdCol=c;lcdPage=l;}
static void A0_Set(void){}
static void SPI_WriteByte(uint8_t b){assert(cs && lcdCol>=4 && lcdCol<132);panel[lcdPage][lcdCol++-4]=b;lcdWrites++;}
'''
renderer += font[font_start:font.index(';', font_start)+1] + '\n'
tiny_start=font.index('const uint8_t gFont3x5[')
renderer+=font[tiny_start:font.index(';',tiny_start)+1]+'\n'
for name, signature in [
    ('UI_PrintStringBuffer', 'static void UI_PrintStringBuffer(const char *pString,uint8_t *buffer,const uint8_t *font)'),
    ('UI_PrintStringSmall', 'static void UI_PrintStringSmall(const char *pString,uint8_t Start,uint8_t End,uint8_t Line,const uint8_t *font)'),
    ('UI_PrintStringSmallNormal', 'static void UI_PrintStringSmallNormal(const char *pString,uint8_t Start,uint8_t End,uint8_t Line)'),
    ('UI_DisplayClear', 'static void UI_DisplayClear(void)'),
    ('UI_StatusClear', 'static void UI_StatusClear(void)')]:
    renderer += signature + '{' + function(helper, name) + '}\n'
for name in ['UI_DrawPixelBuffer','PutPixel','PutPixelStatus','GUI_DisplaySmallest','GUI_DisplaySmallestInverse']:
    signature=re.search(r'^[\w \t*]+\b'+name+r'\([^;{]*?\)\s*\{',helper,re.M)[0]
    renderer+=signature+function(helper,name)+'}\n'
lcd = (repo / 'App/driver/st7565.c').read_text()
for name, signature in [
    ('DrawLine', 'static void DrawLine(uint8_t column,uint8_t line,const uint8_t *lineBuffer,unsigned size_defVal)'),
    ('ST7565_BlitScreen', 'static void ST7565_BlitScreen(uint8_t line)'),
    ('ST7565_BlitFullScreen', 'static void ST7565_BlitFullScreen(void)'),
    ('ST7565_BlitStatusLine', 'static void ST7565_BlitStatusLine(void)')]:
    renderer += signature + '{' + function(lcd, name) + '}\n'
driver = r'''
static app_api_t api;
static uint64_t us;
static uint32_t origin;
static unsigned edges, writes, saves, prepared, draws, cases;
static unsigned audioLatency;
static bool brokenAssets;
static char observedAnswer[17],observedTarget[6];
static uint8_t observedScore;
static bool observedCorrected;
static uint8_t resultPanel[8][128];
static struct {uint64_t t; bool on;uint16_t word;} edge[4096];
static config_t saved;
static uint64_t exitUs, closeUs, openUs;
static uint64_t pttAt,pttUntil;
static uint8_t cfgMode, keyValue;
static bool scriptedMenu;
static bool legacyConfig, fixedRandom;
static uint8_t cfgLength;
static uint8_t cfgWpm;
static uint8_t cfgGap;
static unsigned nKeyEvents,nContacts;
static struct {uint64_t t;uint8_t value;} keys[20],contactsTimeline[20];
static void noop(void){}
static void delay(uint32_t n){us+=n*1000;assert(us<30000000);}
static uint32_t cycles(void){return origin+(uint32_t)(us*48);}
static uint64_t divu(uint32_t n,uint32_t d){assert(d);return ((uint64_t)(n%d)<<32)|(n/d);}
static void writeReg(uint8_t r,uint16_t v){
 assert(r==0x70 || r==0x71);writes++;
 us+=audioLatency;
 if(r==0x70){assert(v==0 || v==0x9c00);return;}
 assert(!v || (v>=3097 && v<=10324));
 if((v!=0)!=g.mark){assert(edges<4096);edge[edges].t=us;edge[edges].word=v;edge[edges++].on=v!=0;}
}
static void af(uint8_t mode){assert(mode==0 || mode==2);}
static void audio(bool on){(void)on;}
static void prepare(void){prepared++;}
static void load(uint8_t *b,uint8_t n){assert(n==12);config_t c={CFG_MAGIC,cfgMode,cfgWpm,0,700,20,cfgLength,600,cfgGap};memcpy(b,&c,n);if(legacyConfig)memset(b+6,0xff,6);}
static void save(const uint8_t*b,uint8_t n){assert(n==12);memcpy(&saved,b,n);saves++;}
static uint32_t randomValue(void){static uint32_t n;return fixedRandom?0:n++*17u+3u;}
static char *append(char*s,uint32_t n){return s+sprintf(s,"%u",n);}
static uint16_t asset(uint16_t offset,void *buf,uint16_t len){
 static const uint8_t data[]={ASSET_DATA};if(brokenAssets)return 0;assert(offset+len<=sizeof(data));memcpy(buf,data+offset,len);return len;
}
static int8_t nav(uint8_t k){return k==APP_KEY_UP?1:k==APP_KEY_DOWN?-1:0;}
static void print(const char*s,uint8_t x,uint8_t e,uint8_t l){assert(strlen(s)<=18 && x==0 && e==127 && l<=6);if(s==g.target)assert(g.reveal);UI_PrintStringSmallNormal(s,x,e,l);}
static void tiny(const char*s,uint8_t x,uint8_t y,bool status,bool fill){
 if(!status){assert((x==2 || x==88) && (y==25 || (x==2 && y==1)));
  if(!g.exercise)assert(strcmp(s,"4 LEN") && strcmp(s,"7 NEW") && strcmp(s,"9 CHECK") && strcmp(s,"9 SHOW"));}
 GUI_DisplaySmallest(s,x,y,status,fill);
}
static void blitStatus(void){ST7565_BlitStatusLine();draws++;us+=1000;assert(!memcmp(panel[0],gStatusLine,128));}
static void blitFull(void){unsigned before=lcdWrites;ST7565_BlitFullScreen();assert(lcdWrites-before==896);assert(!memcmp(panel+1,gFrameBuffer,896));draws++;lcdRefreshes++;us+=11000;}
static uint8_t getkey(void){
 us+=200;
 simPB=0xffffffffu;
 if(us>=pttAt && us<pttUntil)simPB&=~(1u<<10);
 simPins=0xffffffffu;
 if(us>=closeUs && us<openUs)simPins&=~(1u<<12);
 if(nContacts){unsigned p=0;for(unsigned i=0;i<nContacts;i++)if(us>=contactsTimeline[i].t)p=contactsTimeline[i].value;simPins=0xffffffffu & ~((p&1?1u<<12:0)|(p&2?1u<<11:0));}
 if(scriptedMenu && us>=60000 && us<100000)return APP_KEY_MENU;
 uint8_t key=keyValue;for(unsigned i=0;i<nKeyEvents;i++)if(us>=keys[i].t)key=keys[i].value;
 if(key==APP_KEY_INVALID && !(simPB&(1u<<10)))key=APP_KEY_PTT;
 if(g.corrected){strcpy(observedAnswer,g.answer);strcpy(observedTarget,g.target);observedScore=g.score;observedCorrected=true;memcpy(resultPanel,panel,1024);}
 if(us>=exitUs)return us-exitUs<30000 || us-exitUs>=60000?APP_KEY_EXIT:APP_KEY_INVALID;
 return key;
}
static void setup(unsigned mode){
 memset(&g,0,sizeof(g));memset(&api,0,sizeof(api));
 us=origin=edges=writes=saves=prepared=draws=0;irqDepth=0;
 audioLatency=0;observedCorrected=false;
 brokenAssets=false;
 lcdWrites=lcdRefreshes=cs=0;scriptedMenu=false;memset(panel,0xa5,sizeof(panel));
 legacyConfig=fixedRandom=false;nKeyEvents=nContacts=0;cfgLength=5;cfgWpm=20;cfgGap=0;
 moder=0xa5a5a5a5u;pull=0x5a5a5a5au;clockreg=0xcfffffff;
 simPins=0xffffffffu;cfgMode=mode;keyValue=APP_KEY_INVALID;
 closeUs=1000000;openUs=1200000;exitUs=1500000;
 pttAt=pttUntil=0;simPB=0xffffffffu;
 api.abi_major=1;api.api_level=3;api.api_size=368;
 api.cycles_48mhz=cycles;api.uidivmod=divu;api.bk_write=writeReg;
 api.rand32=randomValue;
 api.asset_read=asset;api.print_inverse=GUI_DisplaySmallestInverse;api.nav_dir=nav;
 api.print_tiny=tiny;api.fb=gFrameBuffer;
 api.set_af=af;api.audio_path=audio;api.prepare_tone=prepare;
 api.tones_off_rx=noop;api.cfg_load=load;api.cfg_save=save;
 api.append_u32=append;api.print_normal=print;api.blit_status=blitStatus;api.blit_full=blitFull;
 api.display_clear=UI_DisplayClear;api.status_clear=UI_StatusClear;api.backlight_on=noop;
 api.backlight_update=noop;api.get_key=getkey;api.delay_ms=delay;
 /* All TX/RF callbacks remain NULL: an accidental call fails the test. */
 A=&api;g.cfg=(config_t){CFG_MAGIC,mode,20,0,700,20,5,600,0};clearAnswer();
 g.key=APP_KEY_INVALID;g.ready=true;
}
static void tick(unsigned p){us+=1000;step(cycles(),(uint8_t)p);}
static void run(unsigned ms,unsigned p){for(unsigned i=0;i<ms;i++)tick(p);}
static void check(const char *name){assert(!g.mark);printf("PASS %s\n",name);cases++;}
static const char *const patterns[]={
 ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..",
 "--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-",
 "-.--","--..","-----",".----","..---","...--","....-",".....","-....","--...","---..","----.",
 ".-.-.-","--..--","..--..",".----.","-.-.--","-..-.","-.--.","-.--.-",".-...","---...","-.-.-.","-...-",
 ".-.-.","-....-","..--.-",".-..-.","...-..-",".--.-.","-.-.-","...-.-","...-.","........","...---...","-.-..-..",
 "-...-.-",".-.-","-..---"
};
#ifdef POOL_COUNT /* absent from the V0.2 negative control */
_Static_assert(sizeof patterns/sizeof *patterns==POOL_COUNT,"one pattern per pool character, in pool order");
#endif
static void playAll(void){unsigned limit=0;while(g.playing){us+=1000;playback(cycles());assert(++limit<30000);}}
static void userChar(unsigned index){
 for(const char *p=patterns[index];*p;p++){
  startElement(*p=='-'?2:1,cycles());run(*p=='-'?180:60,0);run(60,0);
 }
 us+=90000;decodeGap(cycles());
}
static unsigned charIndex(char c){return c>='A'?(unsigned)(c-'A'):(unsigned)(26+c-'0');}
static void press(uint8_t key){ui(key,cycles());us+=21000;ui(key,cycles());ui(APP_KEY_INVALID,cycles());us+=21000;ui(APP_KEY_INVALID,cycles());}
static void keyEvent(uint64_t t,uint8_t key){assert(nKeyEvents<20);keys[nKeyEvents].t=t;keys[nKeyEvents++].value=key;}
static void contactEvent(uint64_t t,uint8_t p){assert(nContacts<20);contactsTimeline[nContacts].t=t;contactsTimeline[nContacts++].value=p;}
static void exerciseKeys(void){keyEvent(60000,APP_KEY_0);keyEvent(100000,APP_KEY_INVALID);keyEvent(120000,APP_KEY_0);keyEvent(160000,APP_KEY_INVALID);keyEvent(180000,APP_KEY_7);keyEvent(220000,APP_KEY_INVALID);}
static void timedPoll(unsigned p){us+=1000;uint32_t now=cycles();step(now,(uint8_t)p);if(!g.mark)decodeGap(cycles());}
static void quiet(unsigned micros){uint64_t until=us+micros;while(us<until)timedPoll(0);}
static void traceWord(const char *word){
 unsigned dot=1200000/g.cfg.wpm;
 for(const char*c=word;*c;c++){
  for(const char*p=patterns[charIndex(*c)];*p;p++){
   if(g.cfg.mode==STRAIGHT){uint64_t end=us+dot*(*p=='-'?3u:1u);while(us<end)timedPoll(1);quiet(dot+4000);}
   else {startElement(*p=='-'?2:1,cycles());while(g.element)timedPoll(0);}
  }
  quiet(dot*2u);
 }
}
static void verifyGrid(void){
 uint8_t rendered[384];memcpy(rendered,gFrameBuffer+4,384);
 static const char *const labels[3][9]={
  {"1 KEY","2 WPM","3 HZ","","5 SWAP","6 CLEAR","","",""},
  {"","2 WPM","3 HZ","4 LEN","","","7 NEW","8 GAP","9 SHOW"},
  {"1 KEY","2 WPM","3 HZ","4 LEN","5 SWAP","6 CLEAR","7 NEW","8 GAP","9 CHECK"}};
 memset(gFrameBuffer+4,0,384);
 for(unsigned i=0;i<9;i++)if(labels[g.exercise][i][0]){
  uint8_t x=2+43*(i%3),line=4+i/3;
  GUI_DisplaySmallestInverse(labels[g.exercise][i],x,line,false,true,x+38);
  assert(gFrameBuffer[line][x+31]==0x7f); /* black background, white glyphs */
 }
 assert(!memcmp(rendered,gFrameBuffer+4,384));
}
int main(void){
 setup(STRAIGHT);run(5000,0);run(4,1);assert(g.mark && edges==1 && edge[0].t==5004000);run(4,0);check("first straight edge after 5s idle, 3ms debounce");
 setup(IAMB_A);run(5000,0);run(4,1);run(200,0);assert(edges==2 && edge[0].t==5004000 && edge[1].t-edge[0].t==60000);check("first DIT after 5s idle: 4ms latency, 60ms mark");
 for(unsigned mode=IAMB_A;mode<=IAMB_B;mode++){
  setup(mode);run(4,3);run(20,3);run(300,0);
  assert(edges==(mode==IAMB_A?2:4));
  if(mode==IAMB_B){assert(edge[2].t-edge[1].t==60000 && edge[3].t-edge[2].t==180000);}
  check(mode==IAMB_A?"A squeeze release has no tail":"B squeeze release adds opposite tail");
  setup(mode);run(4,1);run(10,1);run(3,2);run(300,0);assert(edges==4);check("isolated opposite tap remembered");
  setup(mode);run(4,1);run(61,0);run(10,3);run(300,0);assert(edges==2);check("space-only squeeze does not queue a tail");
  setup(mode);run(604,3);run(500,0);assert(edges>=6);check("held squeeze alternates");
 }
 setup(IAMB_A);run(1,1);run(1,0);run(1,1);run(100,0);assert(!edges);check("short contact bounce rejected");
 setup(IAMB_A);g.ready=false;run(100,1);assert(!edges);run(21,0);run(4,1);run(200,0);assert(edges==2);check("held startup contact waits for release");
 setup(STRAIGHT);run(10010,1);assert(!g.ready && !g.mark && edges==2);run(1000,1);assert(edges==2);run(21,0);run(4,1);run(4,0);assert(edges==4);check("10s straight limit latches until release");
 setup(IAMB_A);origin=0xffff0000u;run(4,1);run(200,0);assert(edges==2 && edge[1].t-edge[0].t==60000);check("cycle counter wrap");
 setup(IAMB_A);g.cfg.reverse=1;simPins=~(1u<<11);assert(contacts()==1);g.cfg.mode=STRAIGHT;assert(contacts()==0);simPins=~((1u<<11)|(1u<<12));assert(contacts()==1);check("swap and mono straight key");
 for(unsigned wpm=5;wpm<=60;wpm++){
  setup(IAMB_A);g.cfg.wpm=wpm;run(4,1);run(1000,0);
  unsigned wanted=1200000/wpm;assert(edges==2 && edge[1].t-edge[0].t>=wanted && edge[1].t-edge[0].t<wanted+1001);
 }check("all WPM 5..60: timing within one simulated poll");
 setup(IAMB_A);run(4,1);run(1,3);run(1,1);run(200,0);assert(edges==2);check("single opposite spike does not seed memory");
 setup(IAMB_A); /* Inject the documented board dropout: do not hide it. */
 run(5000,0);run(4,1);run(200,0);run(100,1);run(400,0);assert(edges>=4);check("known 200ms electrical dropout reproduces split paddle sequence");
 for(unsigned mode=0;mode<=2;mode++){
  setup(mode);app_main(&api);assert(saves==1 && saved.mode==mode && prepared==1);
  assert(moder==0xa5a5a5a5u && pull==0x5a5a5a5au && clockreg==0xcfffffffu && !irqDepth);
  assert(edges>=2 && edge[0].t>=closeUs && edge[0].t<closeUs+6000);
  check("full app lifecycle: first press, safe API writes, saved config, GPIO restore");
 }
 setup(IAMB_B);closeUs=100000;openUs=400000;exitUs=140000;app_main(&api);assert(edges==2 && edge[1].t<exitUs+2000);check("EXIT interrupts a mark within one loop");
 setup(STRAIGHT);keyValue=APP_KEY_PTT;closeUs=openUs=2000000;app_main(&api);assert(!edges);check("PTT ignored, no RF callback");
 setup(STRAIGHT);api.api_size=300;app_main(&api);assert(!writes && !saves && !prepared);check("old API rejected before hardware access");
 setup(STRAIGHT);cfgMode=255;app_main(&api);assert(saved.mode==STRAIGHT && saved.hz==700 && saved.wpm==18);check("invalid config resets defaults");
 setup(IAMB_A);run(4,1);ui(APP_KEY_1,cycles());run(25,1);ui(APP_KEY_1,cycles());assert(g.cfg.mode==IAMB_A);run(300,0);check("mode change refused while keying");
 setup(STRAIGHT);ui(APP_KEY_1,cycles());us+=21000;ui(APP_KEY_1,cycles());assert(g.cfg.mode==IAMB_A && !g.ready);
 run(21,0);ui(APP_KEY_5,cycles());us+=21000;ui(APP_KEY_5,cycles());assert(g.cfg.reverse==1);check("resting mode and contact order controls");
 setup(STRAIGHT);g.cfg.wpm=60;ui(APP_KEY_UP,cycles());us+=21000;ui(APP_KEY_UP,cycles());assert(g.cfg.wpm==60);
 g.cfg.hz=1000;ui(APP_KEY_3,cycles());us+=21000;ui(APP_KEY_3,cycles());assert(g.cfg.hz==1000);check("UI bounds at max WPM and maximum pitch");
 setup(STRAIGHT);port(true);moder^=1;pull^=1;clockreg^=1;port(false);
 assert(moder==0xa5a5a5a4u && pull==0x5a5a5a5bu && clockreg==0xcffffffeu);check("GPIO restore preserves unrelated concurrent bit changes");
 setup(STRAIGHT);run(5000,0);run(2,1);run(200,0);assert(!edges);run(4,1);run(4,0);assert(edges==2);check("short initial electrical pulse suppressed, delayed first straight mark reproduced");
 setup(STRAIGHT);run(5000,0);run(6,1);run(200,0);run(30,1);run(4,0);assert(edges==4);check("long initial electrical pulse splits straight mark, known hardware limit");
 setup(STRAIGHT);api.blit_full=NULL;app_main(&api);assert(!writes && !prepared && !saves);check("missing LCD callback rejected safely");
 for(unsigned size=368;size<=384;size+=(size==368?12:4)){
  setup(IAMB_B);api.api_size=size;app_main(&api);assert(prepared==1 && saves==1 && lcdRefreshes>=1 && edges>=2);check("API3 368/380/384 starts, renders and keys with baseline LCD services");
 }
 setup(STRAIGHT);draw();uint8_t mainScreen[896];memcpy(mainScreen,panel+1,896);
 assert(lcdWrites==1024 && lcdRefreshes==1);unsigned ink=0;for(unsigned i=0;i<896;i++)ink+=mainScreen[i]!=0;assert(ink>100);check("actual font renderer and LCD blit produce nonempty main screen");
 FILE *screen=fopen("main.fb","wb");assert(screen);assert(fwrite(panel,1,1024,screen)==1024);fclose(screen);
 g.exercise=1;draw();assert(memcmp(mainScreen,panel+1,896));
 screen=fopen("rx.fb","wb");assert(screen);assert(fwrite(panel,1,1024,screen)==1024);fclose(screen);
 check("RX screen shows its useful keypad commands");
 setup(STRAIGHT);press(APP_KEY_MENU);assert(!g.playing);check("MENU reserved; physical keypad grid stays visible");
 #define MISSING(field) do{setup(STRAIGHT);api.field=NULL;app_main(&api);assert(!writes && !prepared && !saves);assert(moder==0xa5a5a5a5u && pull==0x5a5a5a5au && clockreg==0xcfffffffu);check("missing service " #field " rejected before GPIO/audio");}while(0)
 MISSING(display_clear);MISSING(status_clear);MISSING(print_normal);MISSING(blit_status);MISSING(blit_full);
 MISSING(get_key);MISSING(delay_ms);MISSING(bk_write);MISSING(audio_path);MISSING(prepare_tone);
 MISSING(set_af);MISSING(tones_off_rx);MISSING(cfg_load);MISSING(cfg_save);MISSING(backlight_on);
 MISSING(backlight_update);MISSING(uidivmod);MISSING(append_u32);MISSING(cycles_48mhz);
 MISSING(rand32);MISSING(print_tiny);MISSING(fb);
 MISSING(asset_read);MISSING(print_inverse);MISSING(nav_dir);
 for(unsigned i=0;i<POOL_COUNT;i++){
  setup(STRAIGHT);unsigned code=1;for(const char*p=patterns[i];*p;p++)code=code*2+(*p=='-');
  assert(encode(letter(i))==code);g.cfg.length=1;g.target[0]=letter(i);g.target[1]=0;startExercise(false,cycles());playAll();
  unsigned n=0;for(const char*p=patterns[i];*p;p++,n++){
   assert(edge[2*n].on && !edge[2*n+1].on && edge[2*n+1].t-edge[2*n].t==(*p=='-'?180000:60000));
   if(n)assert(edge[2*n].t-edge[2*n-1].t==60000);
  }assert(edges==2*n && !g.used && !g.reveal);check("A-Z/0-9/punctuation/prosigns independent encode and RX mark/space timing");
  setup(STRAIGHT);for(const char*p=patterns[i];*p;p++){
   tone(true);us+=(*p=='-'?180000:60000);tone(false);us+=60000;
  }us+=90000;decodeGap(cycles());assert(g.used==1 && g.answer[0]==letter(i));check("A-Z/0-9/punctuation/prosigns straight timing decoder");
  for(unsigned mode=IAMB_A;mode<=IAMB_B;mode++){
   setup(mode);userChar(i);assert(g.used==1 && g.answer[0]==letter(i));check("A-Z/0-9/punctuation/prosigns produced IAMB A/B elements decode");
  }
 }
 setup(IAMB_A);g.cfg.length=2;strcpy(g.target,"ET");startExercise(false,cycles());playAll();assert(edge[2].t-edge[1].t==180000);check("RX inter-character gap is three units");
 setup(STRAIGHT);tone(true);us+=60000;tone(false);us+=149000;decodeGap(cycles());assert(!g.used);us+=1000;decodeGap(cycles());assert(!strcmp(g.answer,"E"));us+=270000;decodeGap(cycles());assert(!strcmp(g.answer,"E "));check("letter 2.5 units and word 7 units separators");
 setup(STRAIGHT);tone(true);us+=120000;tone(false);us+=150000;decodeGap(cycles());assert(!strcmp(g.answer,"T"));check("straight two-unit threshold classifies dah");
 setup(STRAIGHT);tone(true);us+=10000;tone(false);us+=500000;decodeGap(cycles());assert(!g.used);check("too-short straight glitch ignored");
 setup(STRAIGHT);tone(true);us+=400000;tone(false);us+=150000;decodeGap(cycles());assert(!strcmp(g.answer,"*"));check("out-of-range straight duration becomes unknown '*'");
 setup(STRAIGHT);for(unsigned i=0;i<10;i++){tone(true);us+=60000;tone(false);us+=60000;}tone(true);us+=180000;tone(false);us+=150000;decodeGap(cycles());assert(!strcmp(g.answer,"*"));check("overlong sign stays unknown even if a dah follows");
 setup(STRAIGHT);for(const char*p="..--..";*p;p++){tone(true);us+=(*p=='-'?180000:60000);tone(false);us+=60000;}us+=90000;decodeGap(cycles());assert(!strcmp(g.answer,"?"));check("real ? decodes as ?, distinct from unknown");
 setup(IAMB_A);g.exercise=2;strcpy(g.target,"F4LPF");startExercise(false,cycles());draw();assert(!g.reveal);playAll();assert(!g.used && !g.reveal && !g.mark);
 for(const char*p=g.target;*p;p++){userChar(charIndex(*p));}correct();assert(!strcmp(g.answer,"F4LPF") && g.score==5 && g.corrected && g.reveal);draw();check("full listen-produce-decode-score cycle F4LPF 5/5");
 clearAnswer();userChar(charIndex('F'));userChar(charIndex('4'));correct();assert(g.score==2 && g.used==2 && g.reveal);check("incomplete answer scores 2/5 without inventing characters");
 clearAnswer();strcpy(g.answer,"F4LXF");g.used=5;correct();assert(g.score==4);check("wrong answer keeps actual text and scores positional errors");
 clearAnswer();strcpy(g.answer,"F4LPFX");g.used=6;correct();assert(g.score==5 && g.used!=g.cfg.length);draw();check("extra response characters prevent OK even when prefix matches");
 clearAnswer();g.exercise=2;for(unsigned i=0;i<25;i++)appendDecoded('E');assert(g.used==16 && g.answer[16]==0 && g.overflow);correct();check("fixed exercise response buffer rejects overflow safely");
 setup(IAMB_B);g.exercise=2;press(APP_KEY_7);char original[6];strcpy(original,g.target);assert(g.playing && !g.reveal);playAll();correct();press(18u);assert(!strcmp(original,g.target) && !g.reveal && !g.used);playAll();press(APP_KEY_2);assert(!g.reveal && !g.used);check("replay preserves target; retry clears correction and hides target");
 setup(STRAIGHT);g.exercise=1;press(APP_KEY_7);playback(cycles());assert(g.mark);stopPlayback();check("automatic audio cancellation stops tone");
 setup(STRAIGHT);g.exercise=1;press(APP_KEY_7);playback(cycles());assert(g.mark);press(APP_KEY_0);assert(!g.playing && !g.mark && g.exercise==2);check("changing exercise aborts automatic audio");
 setup(STRAIGHT);g.exercise=1;g.cfg.rxWpm=40;g.cfg.rxHz=600;g.cfg.wpm=20;g.cfg.hz=700;g.cfg.length=1;strcpy(g.target,"E");startExercise(false,cycles());playAll();assert(edge[1].t-edge[0].t==30000 && edge[0].word==6195);tone(true);assert(edge[2].word==7227);us+=60000;tone(false);check("RX/TX speeds and 600/700 Hz tones are independent");
 for(unsigned length=1;length<=5;length++){setup(IAMB_A);g.cfg.length=length;startExercise(true,cycles());assert(strlen(g.target)==length);for(unsigned i=0;i<length;i++)assert(g.target[i]>' ' && g.target[i]!='*' && encode(g.target[i])>1);playAll();}check("random groups lengths 1..5 drawn from the pool, never '*'");
 setup(IAMB_B);legacyConfig=true;app_main(&api);assert(saved.mode==IAMB_B && saved.wpm==20 && saved.hz==700 && saved.rxWpm==18 && saved.rxHz==600 && saved.length==5);check("V0.1.1 six-byte config retains TX settings and defaults RX extension");
 setup(IAMB_A);fixedRandom=true;cfgLength=1;exerciseKeys();exitUs=350000;app_main(&api);assert(edges>=2 && !g.playing && !g.mark && saves==1);check("EXIT during automatic playback stops tone and restores port");
 setup(IAMB_A);fixedRandom=true;cfgLength=1;exerciseKeys();keyEvent(1600000,APP_KEY_9);keyEvent(1640000,APP_KEY_INVALID);
 contactEvent(1000000,1);contactEvent(1010000,0);contactEvent(1200000,2);contactEvent(1210000,0);exitUs=2000000;
 app_main(&api);assert(observedCorrected && !strcmp(observedTarget,"A") && !strcmp(observedAnswer,"A") && observedScore==1 && !g.mark);check("complete event loop RX>TX: automatic A, USB-C dit/dah, decode, correction 1/1");
 screen=fopen("result.fb","wb");assert(screen);assert(fwrite(resultPanel,1,1024,screen)==1024);fclose(screen);
 setup(IAMB_A);g.exercise=2;g.cfg.length=1;strcpy(g.target,"E");press(18u);playAll();run(100,1);assert(!g.mark);run(21,0);run(4,1);run(220,0);decodeGap(cycles());assert(!strcmp(g.answer,"E"));check("held paddle during automatic audio requires release before answer");
 setup(STRAIGHT);closeUs=1000000;openUs=1060000;app_main(&api);assert(g.answer[0]=='E');check("full STRAIGHT event loop decodes filtered physical contact");
 for(unsigned wpm=5;wpm<=60;wpm++){
  setup(IAMB_A);g.cfg.rxWpm=wpm;g.cfg.length=1;strcpy(g.target,"T");startExercise(false,cycles());playAll();
  unsigned wanted=3600000/wpm;assert(edges==2 && edge[1].t-edge[0].t>=wanted && edge[1].t-edge[0].t<wanted+1002);
 }check("all RX WPM 5..60: automatic dah duration within one poll");
 const unsigned irregular[]={24000,108000,132000,288000};
 for(unsigned i=0;i<4;i++){setup(STRAIGHT);tone(true);us+=irregular[i];tone(false);us+=150000;decodeGap(cycles());assert(g.used==1 && g.answer[0]==(i<2?'E':'T'));}check("documented straight timing tolerances accept irregular dits/dahs");
 const char *const words[]={"A","S","O","C","SOS"};
 for(unsigned mode=STRAIGHT;mode<=IAMB_B;mode++)for(unsigned wpm=5;wpm<=60;wpm++)for(unsigned k=0;k<5;k++){
  setup(mode);g.cfg.wpm=wpm;audioLatency=wpm&1?50:500;origin=0xfff00000u;
  quiet(5000000);traceWord(words[k]);assert(!strcmp(g.answer,words[k]));check("latency + wrap + idle: A/S/O/C/SOS at 5..60 WPM in STRAIGHT/A/B");
 }
 setup(IAMB_A);g.cfg.wpm=15;audioLatency=50;traceWord("A");assert(!strcmp(g.answer,"A"));check("hardware regression: 15 WPM A is A, never ET, with 50us register writes");
 setup(IAMB_A);g.decAt=0xfffffff0u;g.code=2;decodeGap(0xffffffe0u);assert(!g.used);decodeGap(0x01000000u);assert(g.answer[0]=='E');check("reject stale sample but accept natural forward clock wrap");
 setup(STRAIGHT);press(APP_KEY_F);press(APP_KEY_2);assert(g.cfg.wpm==19 && !g.fArm);press(APP_KEY_2);assert(g.cfg.wpm==20);press(APP_KEY_F);press(APP_KEY_3);assert(g.cfg.hz==650);check("F numeric decrement is consumed, next numeric press increments");
 press(APP_KEY_0);press(APP_KEY_2);press(APP_KEY_3);assert(g.cfg.rxWpm==21 && g.cfg.rxHz==650 && g.cfg.wpm==20 && g.cfg.hz==650);check("RX and TX keyboard settings remain independent");
 setup(STRAIGHT);ui(APP_KEY_UP,cycles());us=20000;ui(APP_KEY_UP,cycles());assert(g.cfg.wpm==21);us=419000;ui(APP_KEY_UP,cycles());assert(g.cfg.wpm==21);us=420000;ui(APP_KEY_UP,cycles());assert(g.cfg.wpm==22);us=520000;ui(APP_KEY_UP,cycles());assert(g.cfg.wpm==23);check("CW USB navigation repeat: 400ms then 100ms");
 setup(STRAIGHT);press(APP_KEY_1);uint8_t selected=g.cfg.mode;us+=1000000;ui(APP_KEY_1,cycles());assert(g.cfg.mode==selected);check("held numeric key acts once");
 for(unsigned mode=IAMB_A;mode<=IAMB_B;mode++){
  setup(mode);cfgWpm=15;audioLatency=50;origin=0xfff00000u;
  contactEvent(1000000,1);contactEvent(1010000,0);contactEvent(1160000,2);contactEvent(1170000,0);exitUs=1800000;
  app_main(&api);assert(!strcmp(g.answer,"A"));check("full GPIO/keypad/UI loop, 15 WPM A with audio latency is never ET");
 }
 setup(IAMB_A);audioLatency=50;startElement(2,cycles());quiet(10000);stopPlayback();quiet(500000);assert(!g.used);check("cancelled partial paddle element never becomes a completed character");
 setup(STRAIGHT);brokenAssets=true;app_main(&api);assert(!prepared && !writes && !saves && moder==0xa5a5a5a5u);check("missing or unreadable asset signature refuses before GPIO/audio");
 setup(STRAIGHT);press(APP_KEY_F);press(APP_KEY_MENU);assert(g.fArm);check("reserved MENU does not change F prefix");
 setup(STRAIGHT);g.cfg.hz=300;press(APP_KEY_F);press(APP_KEY_3);assert(g.cfg.hz==300);g.cfg.length=1;press(APP_KEY_F);press(APP_KEY_4);assert(g.cfg.length==1);check("F tone and group bounds stay valid");
 setup(IAMB_A);g.exercise=1;press(APP_KEY_7);playback(cycles());bool dirty=g.redraw;press(APP_KEY_F);assert(g.playing && !g.fArm && g.redraw==dirty);stopPlayback();check("F during automatic audio cannot trigger an in-gap screen refresh");
 for(unsigned wpm=5;wpm<=60;wpm++){
  setup(STRAIGHT);g.cfg.wpm=wpm;g.code=2;g.decAt=0xffff0000u;
  uint32_t threshold=(57600000u/wpm)*5u/2u;
  decodeGap(g.decAt+threshold-1);assert(!g.used);decodeGap(g.decAt+threshold);assert(g.used==1 && g.answer[0]=='E');
 }check("letter boundary one cycle below/at threshold across wrap, 5..60 WPM");
 setup(STRAIGHT);cfgWpm=15;audioLatency=50;closeUs=openUs=0;pttAt=100000;pttUntil=180000;exitUs=600000;app_main(&api);
 assert(edges==2 && g.answer[0]=='E' && edge[0].t<105000 && edge[1].t<185000);check("physical PB10 PTT produces local straight E with actual duration");
 setup(STRAIGHT);closeUs=100000;openUs=220000;pttAt=180000;pttUntil=300000;exitUs=600000;app_main(&api);
 assert(edges==2 && g.answer[0]=='T');check("STRAIGHT simultaneous USB/PTT is one OR contact, no mixed events");
 for(unsigned mode=IAMB_A;mode<=IAMB_B;mode++){
  setup(mode);pttAt=50000;pttUntil=650000;contactEvent(100000,1);contactEvent(110000,0);contactEvent(260000,2);contactEvent(270000,0);exitUs=700000;app_main(&api);
  assert(edges==4 && g.answer[0]=='A');check("IAMB A/B ignores PTT while USB paddle retains its own event stream");
 }
 setup(STRAIGHT);pttAt=100000;pttUntil=1000000;closeUs=openUs=0;exitUs=200000;app_main(&api);assert(edges==2 && edge[1].t<203000);check("EXIT keypad remains readable over held PTT and cuts local tone");
 setup(IAMB_A);g.exercise=2;g.cfg.length=1;strcpy(g.target,"A");press(18u);playback(cycles());assert(g.mark);press(18u);assert(g.playing==1 && !g.mark && !g.reveal);playAll();assert(!g.used);check("RX>TX SIDE1 replay remains unchanged without overlap or revealing target");
 setup(IAMB_A);g.exercise=2;g.cfg.length=1;strcpy(g.target,"E");ui(18u,cycles());us+=21000;ui(18u,cycles());uint32_t replayAt=g.rxAt;us+=100000;ui(18u,cycles());assert(g.rxAt==replayAt);playAll();check("RX>TX held SIDE1 triggers replay once, not repeatedly");
 setup(IAMB_A);startElement(1,cycles());press(18u);assert(g.mark && g.element==1);quiet(500000);check("SIDE1 is disabled in TX FREE without interrupting the keyer");
 setup(STRAIGHT);g.exercise=1;press(APP_KEY_8);assert(!g.playing && g.cfg.gap==1);g.exercise=0;press(APP_KEY_8);assert(g.cfg.gap==1);press(APP_KEY_4);assert(g.cfg.length==5);press(APP_KEY_7);assert(!g.playing);check("8 GAP in RX only; irrelevant commands disabled in TX FREE");
 setup(IAMB_A);char all[101];for(unsigned i=0;i<100;i++){all[i]=(char)('A'+i%26);appendDecoded(all[i]);}all[100]=0;
 assert(g.used==36 && !strcmp(g.answer,all+64) && !g.overflow);draw();
 screen=fopen("scroll.fb","wb");assert(screen);assert(fwrite(panel,1,1024,screen)==1024);fclose(screen);check("continuous TX keeps latest visible lines and scrolls at 48 characters");
 press(APP_KEY_6);assert(!g.used && !g.answer[0]);draw();check("CLEAR voluntarily clears entire TX display window");
 for(unsigned latency=0;latency<=500;latency+=500)for(unsigned good=0;good<2;good++){
  setup(IAMB_A);audioLatency=latency;g.exercise=2;g.cfg.length=1;strcpy(g.target,"A");strcpy(g.answer,good?"A":"E");g.used=1;press(APP_KEY_9);assert(g.playing==(good?2:3));playAll();
  assert(edges==4 && edge[0].word==(good?9086:4543) && edge[2].word==(good?10324:3407));
  assert(edge[1].t-edge[0].t>=60000 && edge[1].t-edge[0].t<61001 && edge[2].t-edge[1].t>=40000 && edge[2].t-edge[1].t<41001 && edge[3].t-edge[2].t>=60000 && edge[3].t-edge[2].t<61001 && g.used==1);check("success/error two-tone sequence, 60/40/60ms, excluded from decoder with audio latency");
 }
 setup(IAMB_A);g.exercise=2;g.cfg.length=1;strcpy(g.target,"A");startElement(2,cycles());press(APP_KEY_9);assert(g.mark && !g.playing && !g.corrected);quiet(500000);check("CHECK refuses during manipulation and never truncates a Morse element");
 setup(IAMB_A);fixedRandom=true;cfgLength=1;exerciseKeys();keyEvent(1600000,APP_KEY_9);keyEvent(1640000,APP_KEY_INVALID);contactEvent(1000000,1);contactEvent(1010000,0);contactEvent(1200000,2);contactEvent(1210000,0);exitUs=1650000;app_main(&api);
 assert(observedCorrected && !g.mark && !g.playing && edge[edges-1].t<exitUs+2000);check("EXIT interrupts correction sound cleanly");

 /* V0.3.1: exact total character silence, independently of mark/element speed. */
 for(unsigned exercise=1;exercise<=2;exercise++)for(unsigned wpm=5;wpm<=60;wpm++)
 for(unsigned gap=0;gap<4;gap++)for(unsigned latency=0;latency<3;latency++){
  setup(IAMB_A);g.exercise=exercise;g.cfg.rxWpm=wpm;g.cfg.length=2;g.cfg.gap=gap;
  audioLatency=latency==0?0:latency==1?50:500;origin=0xfff00000u;
  strcpy(g.target,"AS");startExercise(false,cycles());playAll();assert(edges==10 && !g.used);
  const unsigned half[]={6,9,12,18};double dot=1200000.0/wpm,spacing=dot*half[gap]/2;
  double silence=edge[4].t-edge[3].t;
  assert(silence>=spacing-1 && silence<spacing+1001+audioLatency);
  for(unsigned i=0;i<10;i+=2){double duration=edge[i+1].t-edge[i].t,expected=dot*(i==2?3:1);
   assert(duration>=expected-1 && duration<expected+1001+audioLatency);}
  for(unsigned i=1;i<9;i+=2)if(i!=3){double duration=edge[i+1].t-edge[i].t;
   assert(duration>=dot-1 && duration<dot+1001+audioLatency);}
  check("RX/RX>TX GAP 1/1.5/2/3 at 5..60 WPM, 0/50/500us, wrap; DIT/DAH/intra gaps unchanged");
 }
 for(unsigned mode=STRAIGHT;mode<=IAMB_B;mode++)for(unsigned gap=0;gap<4;gap++){
  setup(mode);g.cfg.gap=gap;g.cfg.wpm=15;audioLatency=500;traceWord("ASOCSOS");
  assert(!strcmp(g.answer,"ASOCSOS"));check("RX GAP cannot change STRAIGHT or paddle TX decoder");
 }
 for(unsigned mode=0;mode<3;mode++){
  setup(STRAIGHT);g.exercise=mode;draw();verifyGrid();check("command assets equal real resident inverse-font renderer, correct physical keypad and polarity");
 }
 setup(IAMB_A);g.exercise=1;g.reveal=true;g.corrected=true;strcpy(g.target,"ZZZZZ");press(KEY_SIDE1);
 assert(!strcmp(g.target,"ZZZZZ") && g.reveal && g.corrected && !g.playing && !edges);
 char selectedTarget[6];strcpy(selectedTarget,g.target);us+=2000000;ui(KEY_SIDE1,cycles()); /* press helper already released */
 us+=21000;ui(KEY_SIDE1,cycles());strcpy(selectedTarget,g.target);us+=2000000;ui(KEY_SIDE1,cycles());
 assert(!strcmp(selectedTarget,g.target) && !g.playing && !edges);check("RX SIDE1 ignored on press and hold, preserving target and reveal");
 setup(IAMB_A);g.exercise=1;strcpy(g.target,"AS");g.cfg.length=2;g.pttReady=true;
 press(APP_KEY_PTT);assert(g.playing && !g.pttReady && !strcmp(g.target,"AS") && !g.reveal);playAll();
 unsigned firstEdges=edges;press(APP_KEY_PTT);assert(!g.playing && edges==firstEdges && !strcmp(g.target,"AS"));
 g.pttReady=true;press(APP_KEY_PTT);playback(cycles());assert(g.mark);g.pttReady=true;press(APP_KEY_PTT);
 assert(!g.mark && g.playing && !strcmp(g.target,"AS") && !g.reveal);playAll();check("RX PTT replays identical target, latched until release, restarts without overlap");
 setup(STRAIGHT);g.exercise=1;g.pttReady=true;press(APP_KEY_PTT);assert(!g.playing && !g.target[0] && !edges);
 check("RX PTT without selected target cannot generate a new group");
 setup(STRAIGHT);g.exercise=1;press(APP_KEY_8);press(APP_KEY_8);press(APP_KEY_8);assert(g.cfg.gap==3);
 press(APP_KEY_8);assert(g.cfg.gap==0);g.exercise=2;press(APP_KEY_8);assert(g.cfg.gap==1);
 g.exercise=0;press(APP_KEY_8);assert(g.cfg.gap==1);check("8 cycles all four GAP levels in RX modes only");
 for(unsigned gap=0;gap<4;gap++){
  setup(STRAIGHT);cfgGap=gap;closeUs=openUs=0;app_main(&api);assert(saved.gap==gap && saves==1);
  check("GAP persistent config preserves all four values");
 }
 setup(STRAIGHT);cfgGap=255;closeUs=openUs=0;app_main(&api);assert(saved.gap==0 && saved.hz==700 && saved.rxHz==600);
 check("V0.3 config trailing erased GAP migrates to 1x without losing RX/TX settings");
 /* Full event loop: key scan priority, physical PTT, long hold, actual redraw. */
 for(unsigned mode=STRAIGHT;mode<=IAMB_B;mode++){
  setup(mode);closeUs=openUs=0;keyEvent(60000,APP_KEY_0);keyEvent(100000,APP_KEY_INVALID);
  keyEvent(120000,APP_KEY_7);keyEvent(800000,APP_KEY_INVALID);pttAt=900000;pttUntil=2500000;exitUs=2800000;
  fixedRandom=true;cfgLength=1;app_main(&api);assert(edges==8 && edge[4].t>900000 && edge[7].t<1300000);
  check("RX 7 held generates once, PTT held replays once, no pioche in any keyer mode");
 }
 for(unsigned mode=STRAIGHT;mode<=IAMB_B;mode++){
  setup(mode);closeUs=openUs=0;pttAt=0;pttUntil=1000000;exitUs=1400000;
  keyEvent(60000,APP_KEY_0);keyEvent(100000,APP_KEY_INVALID);keyEvent(120000,APP_KEY_7);keyEvent(160000,APP_KEY_INVALID);
  fixedRandom=true;cfgLength=1;app_main(&api);assert(edges==4 && !g.playing);check("PTT held across TX-to-RX transition cannot replay or key");
 }
 setup(STRAIGHT);closeUs=openUs=0;pttAt=900000;pttUntil=2000000;exitUs=2400000;
 keyEvent(60000,APP_KEY_0);keyEvent(100000,APP_KEY_INVALID);keyEvent(120000,APP_KEY_7);keyEvent(160000,APP_KEY_INVALID);
 keyEvent(1100000,APP_KEY_0);keyEvent(1140000,APP_KEY_INVALID);fixedRandom=true;cfgLength=1;app_main(&api);
 assert(edges==8 && edge[7].t<1140000);check("held RX replay PTT cannot key when switching to RX>TX");
 setup(IAMB_A);closeUs=openUs=0;pttAt=900000;pttUntil=2000000;exitUs=950000;
 keyEvent(60000,APP_KEY_0);keyEvent(100000,APP_KEY_INVALID);keyEvent(120000,APP_KEY_7);keyEvent(160000,APP_KEY_INVALID);
 fixedRandom=true;cfgLength=1;app_main(&api);assert(edges==6 && edge[5].t<exitUs+2000 && !g.playing);
 check("EXIT interrupts PTT replay while PTT remains held, no RF callback");
 /* V0.3.2: receive-only commands and compact labels. */
 setup(IAMB_B);g.exercise=1;g.reveal=true;strcpy(g.target,"AS");g.cfg.length=2;
 for(unsigned i=0;i<3;i++){
  const uint8_t ignored[]={APP_KEY_1,APP_KEY_5,APP_KEY_6};config_t before=g.cfg;
  press(ignored[i]);assert(!memcmp(&before,&g.cfg,sizeof(before)) && g.reveal && !strcmp(g.target,"AS") && !g.playing);
  check("RX TRAIN KEY/SWAP/CLEAR disabled, configuration and revealed target preserved");
 }
 for(unsigned i=0;i<3;i++){
  const uint8_t ignored[]={APP_KEY_1,APP_KEY_5,APP_KEY_6};setup(IAMB_A);g.exercise=1;
  strcpy(g.target,"AS");g.cfg.length=2;startExercise(false,cycles());playback(cycles());
  uint32_t deadline=g.rxAt;press(ignored[i]);assert(g.playing==1 && g.mark && g.rxAt==deadline && !strcmp(g.target,"AS"));
  stopPlayback();check("disabled RX controls cannot interrupt an active Morse element");
 }
 setup(IAMB_B);g.exercise=1;
 press(APP_KEY_2);assert(g.cfg.rxWpm==21);press(APP_KEY_F);press(APP_KEY_2);assert(g.cfg.rxWpm==20);
 press(APP_KEY_3);assert(g.cfg.rxHz==650);press(APP_KEY_8);assert(g.cfg.gap==1);
 check("RX TRAIN retains WPM including F decrement, tone and GAP");
 g.exercise=2;press(APP_KEY_1);assert(g.cfg.mode==STRAIGHT);press(APP_KEY_5);assert(g.cfg.reverse==1);
 strcpy(g.answer,"AS");g.used=2;g.corrected=g.reveal=true;press(APP_KEY_6);assert(!g.used && !g.reveal && !g.corrected);
 check("RX>TX retains KEY/SWAP/CLEAR unchanged");
 for(unsigned exercise=1;exercise<=2;exercise++){
  setup(STRAIGHT);g.exercise=exercise;draw();assert(gFrameBuffer[0][2]);
  for(unsigned x=0;x<128;x++)assert(!(gFrameBuffer[0][x]&0xc0));
  g.cfg.length=2;strcpy(g.target,"AS");g.reveal=true;draw();
  uint8_t actual[128];memcpy(actual,gFrameBuffer[0],128);memset(gFrameBuffer[0],0,128);
  UI_PrintStringSmallNormal("AS",0,127,0);assert(!memcmp(actual,gFrameBuffer[0],128));
  check("RX and RX>TX instructions use 3x5 font, revealed Morse retains normal font");
 }
 setup(IAMB_A);g.exercise=1;press(KEY_SIDE1);assert(!g.target[0] && !g.playing && !edges);
 check("RX SIDE1 without target cannot generate or play");
 setup(IAMB_A);g.exercise=1;strcpy(g.target,"AS");g.cfg.length=2;startExercise(false,cycles());playback(cycles());
 uint32_t unchangedDeadline=g.rxAt;unsigned unchangedEdges=edges;press(KEY_SIDE1);us+=1000000;ui(KEY_SIDE1,cycles());
 assert(g.playing==1 && g.mark && edges==unchangedEdges && g.rxAt==unchangedDeadline && !strcmp(g.target,"AS"));
 stopPlayback();check("RX SIDE1 during playback cannot stop, restart or replace target");
 setup(STRAIGHT);g.exercise=1;assert(!strcmp(text(T_READY),"PTT RPLY"));draw();
 uint8_t readyPixels[128];memcpy(readyPixels,gFrameBuffer[0],128);memset(gFrameBuffer[0],0,128);
 GUI_DisplaySmallest("PTT RPLY",2,1,false,true);assert(!memcmp(readyPixels,gFrameBuffer[0],128));
 check("RX ready label is exactly PTT RPLY in existing tiny font, no S1 NEW");
 printf("CASES %u\n",cases);
}
'''
driver=driver.replace('ASSET_DATA',','.join(map(str,(app/'cwtrainer_assets.bin').read_bytes())))
driver=driver.replace('playAll();press(APP_KEY_2);','playAll();press(APP_KEY_6);')
test = out / 'test.c'
test.write_text(prefix + renderer + source + driver)
exe = out / ('test.exe' if os.name == 'nt' else 'test')
cmd = [os.environ.get('HOST_CC', 'gcc'), '-std=c11', '-O1', '-Wall', '-Wextra',
       '-Werror', '-fsanitize=undefined', '-I', str(out), str(test), '-o', str(exe)]
result = subprocess.run(cmd, capture_output=True, text=True)
# MinGW installations may not ship libubsan; use traps, never skip assertions.
if result.returncode and ('ubsan' in result.stderr or 'unrecognized' in result.stderr):
    cmd[cmd.index('-fsanitize=undefined')] = '-ftrapv'
    result = subprocess.run(cmd, capture_output=True, text=True)
(out / 'build.log').write_text(result.stdout + result.stderr)
if result.returncode:
    raise SystemExit(result.stderr)
result = subprocess.run([str(exe)], capture_output=True, text=True, cwd=out)
(out / 'run.log').write_text(result.stdout + result.stderr)
print(result.stdout, end='')
if result.returncode:
    raise SystemExit(result.stderr or f'host test failed: {result.returncode}')

# The actual previous V0.2 source is a negative control, not a rewritten decoder.
old_source=(here/'fixtures/v02_decoder.c').read_text().replace('#include "../app_api.h"','#include "host_api.h"').replace('section(".text.entry"), ', '')
old_driver=driver[:driver.index('int main(void)')]
old_driver=old_driver.replace('n==12','n==10').replace('cfgLength,600,cfgGap','cfgLength,600').replace('20,5,600,0','20,5,600').replace('memset(b+6,0xff,6)','memset(b+6,0xff,4)')
old_driver=old_driver.replace('decodeGap(cycles());}', 'decodeGap(now);}')
old_main=r'''int main(void){setup(IAMB_A);g.cfg.wpm=15;audioLatency=50;traceWord("A");
printf("V0.2 negative control: A -> [%s]\n",g.answer);fflush(stdout);
char compact[17];unsigned n=0;for(unsigned i=0;i<g.used;i++)if(g.answer[i]!=' ')compact[n++]=g.answer[i];compact[n]=0;
assert(!strcmp(compact,"ET"));}
'''
old_test=out/'negative.c';old_exe=out/('negative.exe' if os.name=='nt' else 'negative')
old_test.write_text(prefix+renderer+old_source+old_driver+old_main)
old_command=cmd.copy();old_command[old_command.index(str(test))]=str(old_test);old_command[old_command.index(str(exe))]=str(old_exe)
old_command.append('-Wno-unused-function')  # after -Wall: clang honours the last flag
subprocess.run(old_command,check=True)
old_result=subprocess.run([str(old_exe)],capture_output=True,text=True,cwd=out)
(out/'negative.log').write_text(old_result.stdout+old_result.stderr)
assert old_result.returncode==0,old_result.stderr
print(old_result.stdout.strip())

# Inspect real resident services in addition to NULL RF callbacks in the mock.
bk = (repo / 'App/driver/bk4829.c').read_text()
prepare = function(bk, 'BK4819_PrepareToPlayTone')
assert 'ENABLE_PA_GAIN' not in prepare and 'ToggleGpioOut' not in prepare
assert re.findall(r'\b(BK4819_\w+)\(', prepare) == [
    'BK4819_EnterTxMute', 'BK4819_SetAF', 'BK4819_WriteRegister',
    'BK4819_WriteRegister', 'BK4819_WriteRegister']
assert 'BK4819_REG_30_ENABLE_AF_DAC | BK4819_REG_30_ENABLE_DISC_MODE | BK4819_REG_30_ENABLE_TX_DSP' in prepare
assert 'BK4819_REG_50, 0xBB18' in function(bk, 'BK4819_EnterTxMute')
for name in ['BK4819_SetAF', 'BK4819_TurnsOffTones_TurnsOnRX', 'BK4819_ExitTxMute']:
    body = function(bk, name)
    assert 'ToggleGpioOut' not in body and 'ENABLE_PA_GAIN' not in body
assert not re.search(r'A->(?:tx_\w+|beam_\w+|fm_\w+|serial_\w+|handoff)\s*\(', source)
print('PASS resident audio path audit: PA gain disabled, no PA GPIO activation')

keyboard=(repo/'App/driver/keyboard.c').read_text()
key_header=(repo/'App/driver/keyboard.h').read_text()
key_enum=re.search(r'enum KEY_Code_e\s*\{(.*?)\}',key_header,re.S)[1]
key_enum=re.sub(r'//[^\n]*','',key_enum)
names=[part.strip().split('=')[0].strip() for part in key_enum.split(',') if part.strip()]
assert names.index('KEY_SIDE1')==18 and names.index('KEY_PTT')==16
get_key=function(keyboard,'KEYBOARD_GetKey')
assert 'btn == KEY_INVALID && GPIO_IsPttPressed()' in get_key
assert not re.search(r'RADIO_|Transmit|SetTx',get_key)
assert 'GPIO_PIN_PTT            = GPIO_MAKE_PIN(GPIOB, LL_GPIO_PIN_10)' in (repo/'App/driver/gpio.h').read_text()
print('PASS source audit: SIDE1=18, PTT=16/PB10; keypad precedes PTT; no RF key handler')

# Run the same lifecycle tests with the real resident audio function bodies.
# Only the lowest hardware register/GPIO accesses are simulated in this variant.
gpio = (repo / 'App/driver/gpio.h').read_text()
assert 'GPIO_PIN_AUDIO_PATH     = GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_8)' in gpio
resident = r'''
#include "bk4819-regs.h"
typedef unsigned BK4819_AF_Type_t;
enum { BK4819_AF_MUTE=0, BK4819_AF_BEEP=3, GPIO_PIN_AUDIO_PATH=8 };
static uint16_t hw[128];
static void BK4819_WriteRegister(BK4819_REGISTER_t r,uint16_t v){
 assert(r==0x30 || r==0x47 || r==0x50 || r==0x70 || r==0x71);
 if(r==0x30)assert(!(v & BK4819_REG_30_ENABLE_PA_GAIN));
 hw[r]=v;
}
static void GPIO_SetOutputPin(uint32_t p){assert(p==GPIO_PIN_AUDIO_PATH);}
static void GPIO_ResetOutputPin(uint32_t p){assert(p==GPIO_PIN_AUDIO_PATH);}
'''
for name in ['BK4819_EnterTxMute', 'BK4819_ExitTxMute', 'BK4819_SetAF',
             'BK4819_PrepareToPlayTone', 'BK4819_TurnsOffTones_TurnsOnRX']:
    signature = re.search(r'void\s+' + name + r'\([^;]*?\)\s*\{', bk, re.S)[0]
    resident += signature + function(bk, name) + '}\n'
for name in ['GPIO_EnableAudioPath', 'GPIO_DisableAudioPath']:
    resident += 'static void ' + name + '(void){' + function(gpio, name) + '}\n'
real_driver = driver.replace('static void prepare(void){prepared++;}',
    'static void prepare(void){prepared++;BK4819_PrepareToPlayTone(true);assert(hw[0x30]==0x0302);}')
real_driver = real_driver.replace('api.tones_off_rx=noop;', 'api.tones_off_rx=BK4819_TurnsOffTones_TurnsOnRX;')
real_driver = real_driver.replace('assert(mode==0 || mode==2);', 'assert(mode==0 || mode==2);BK4819_SetAF(mode);')
real_driver = real_driver.replace('(void)on;', 'if(on)GPIO_EnableAudioPath();else GPIO_DisableAudioPath();')
real_driver = real_driver.replace('writes++;', 'writes++;BK4819_WriteRegister((BK4819_REGISTER_t)r,v);')
resident_test = out / 'resident.c'
resident_test.write_text(prefix + resident + renderer + source + real_driver)
resident_exe = out / ('resident.exe' if os.name == 'nt' else 'resident')
resident_command = cmd.copy()
resident_command[resident_command.index(str(test))] = str(resident_test)
resident_command[resident_command.index(str(exe))] = str(resident_exe)
resident_command[1:1] = ['-I', str(repo / 'App/driver')]
subprocess.run(resident_command, check=True)
resident_result = subprocess.run([str(resident_exe)], capture_output=True, text=True, cwd=out)
(out / 'resident.log').write_text(resident_result.stdout + resident_result.stderr)
assert resident_result.returncode == 0, resident_result.stderr
resident_cases = int(re.search(r'CASES (\d+)', resident_result.stdout)[1])
print(f'PASS {resident_cases} lifecycle/engine cases with actual resident audio bodies: no RF PA gain or GPIO write')
prefix_results = {}
for size, first_absent in [(368, 'serial_read'), (380, 'blit_page'), (384, 'lcd_feed')]:
    # Remove the tail entirely, rather than merely changing api_size on a full
    # mock. Compilation then also proves no reference to absent fields remains.
    struct_start = api.index('typedef struct app_api {')
    tail = re.search(r'^    [^\n]*\(\*' + first_absent + r'\)', api, re.M).start()
    struct_end = api.index('} app_api_t;', struct_start)
    truncated = api[:tail] + api[struct_end:]
    header = out / f'api{size}.h'
    header.write_text(truncated)
    variant = out / f'api{size}.c'
    variant.write_text(prefix + renderer + source.replace('host_api.h', header.name) +
                       driver.replace('api.api_size=368;', f'api.api_size={size};'))
    variant_exe = out / (f'api{size}.exe' if os.name == 'nt' else f'api{size}')
    variant_command = cmd.copy()
    variant_command[variant_command.index(str(test))] = str(variant)
    variant_command[variant_command.index(str(exe))] = str(variant_exe)
    subprocess.run(variant_command, check=True)
    run_result = subprocess.run([str(variant_exe)], capture_output=True, text=True, cwd=out)
    (out / f'api{size}.log').write_text(run_result.stdout + run_result.stderr)
    assert run_result.returncode == 0, run_result.stderr
    prefix_results[str(size)] = int(re.search(r'CASES (\d+)', run_result.stdout)[1])
    print(f'PASS API3 {size}: {prefix_results[str(size)]} cases, truncated C API struct')

# Export the actual captured LCD bytes with only the Python standard library.
import struct
import zlib
def png_chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))

for name in ['main', 'rx', 'result', 'scroll']:
    data = (out / f'{name}.fb').read_bytes()
    pixels = [0 if data[(y//8)*128+x] & (1 << (y%8)) else 255
              for y in range(64) for x in range(128)]
    scanlines = b''.join((b'\0' + bytes(v for v in pixels[y*128:(y+1)*128] for _ in range(4))) * 4
                         for y in range(64))
    png = (b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', 512, 256, 8, 0, 0, 0, 0)) +
           png_chunk(b'IDAT', zlib.compress(scanlines)) + png_chunk(b'IEND', b''))
    (out / f'{name}.png').write_bytes(png)
summary = {'host_cases': int(re.search(r'CASES (\d+)', result.stdout)[1]),
           'negative_control': 'previous V0.2 reproduces A -> ET at 15 WPM, audio writes 50us',
           'resident_variant_cases': resident_cases, 'wpm_values': 56,
           'truncated_api_variants': prefix_results,
           'framebuffer_and_lcd': 'actual font/helper/LCD bodies, simulated SPI panel',
           'resident_audio_audit': 'PASS', 'compiler_command': cmd}
summary['physical_keys_audit']='SIDE1=18, PTT=16/PB10, keypad priority; read-only GPIO; no RF'
summary['version']='0.4'
summary['gap_waveform_cases']=1344
summary['gap_factors']=[1,1.5,2,3]
summary['audio_latencies_us']=[0,50,500]
summary['grid_assets']='byte-for-byte match with resident inverse 3x5 renderer in all modes'
summary['rx_controls']='SIDE1 disabled; 7 NEW; PTT exact REPLAY; held-key and mode-transition release gates PASS'
(out / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
