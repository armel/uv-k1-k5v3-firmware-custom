from pathlib import Path
import ast,re,subprocess,json,sys,os
p=Path(__file__).resolve().parent
r=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else p.parents[3];label='iambic-tests';out=p/'out-functional';out.mkdir(exist_ok=True)
def resident_function(source,name):
 m=re.search(r'\bvoid '+name+r'\([^;]*?\)\s*\{',source,re.S);start=m.start();end=source.index('{',start)+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
font=(r/'App/font.c').read_text();pos=font.index('const uint8_t gFont3x5[][3]')
renderer=font[pos:font.index(';',pos)+1]+'\n#define gFrameBuffer fb\n#define gStatusLine status\n'
helper=(r/'App/ui/helper.c').read_text()
renderer+='\n'.join(resident_function(helper,n) for n in ['UI_DrawPixelBuffer','PutPixel','PutPixelStatus','GUI_DisplaySmallest','GUI_DisplaySmallestInverse'])
renderer+='\nstatic unsigned modesDrawn,firstReadyAt,firstReadyState;\nstatic unsigned nEdges,edgeTime[256];static bool edgeOn[256];\nstatic void renderedTiny(const char *s,uint8_t x,uint8_t y,bool st,bool fill){\n GUI_DisplaySmallest(s,x,y,st,fill);\n if(x==48 && y==33 && !st){\n  const char *names[]={"AUTO","IAMB A","IAMB B"};unsigned mode=g.iambic>>4;\n  assert(mode<3 && !strcmp(s,names[mode]) && fill);\n  unsigned pixels=0;for(unsigned i=48;i<72;i++)pixels+=fb[4][i]!=0;\n  assert(pixels);modesDrawn|=1u<<mode;\n }\n}\nstatic void renderedInverse(const char*s,uint8_t x,uint8_t y,bool st,bool fill,uint8_t end){\n GUI_DisplaySmallestInverse(s,x,y,st,fill,end);\n}\nstatic void clearDisplay(void){memset(fb,0,sizeof(fb));}\nstatic void clearStatus(void){memset(status,0,sizeof(status));}\nstatic void showStatus(void){\n assert(!memcmp(status+32,assets+(g.armState>=4?BMP_READY:BMP_LOCK),7));\n if(g.armState>=4 && !firstReadyAt){firstReadyAt=simUs;firstReadyState=g.armState;}\n if(g.armState<6)assert(!g.txReady && !g.keyDown);\n}\nstatic void showFull(void){\n const unsigned mode=g.iambic>>4;const char *names[]={"AUTO","IAMB A","IAMB B"};\n uint8_t actual[24];memcpy(actual,fb[4]+48,24);\n memset(fb[4]+48,0,24);GUI_DisplaySmallest(names[mode],48,33,false,true);\n assert(!memcmp(actual,fb[4]+48,24));\n}\nstatic void recordEdge(bool on){assert(nEdges<256);edgeTime[nEdges]=simUs;edgeOn[nEdges++]=on;}\nstatic void extraInit(void){modesDrawn=firstReadyAt=firstReadyState=nEdges=0;}\n'

def literal(file,name):
 t=ast.parse(file.read_text());return next(ast.literal_eval(n.value) for n in t.body if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id==name for x in n.targets))
h='\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\nstatic unsigned nEvents,exitAt,rawSeen,requestN,markN,legacyN,quietN,prepStart,prepEnd,firstMark,firstLegacyRF,extraPrepare,whichMode,whichAlgo;\nstatic struct {unsigned t,p;} events[8];\nstatic unsigned physical(void){unsigned n=0;for(unsigned i=0;i<nEvents;i++)if(simUs>=events[i].t)n=events[i].p;return n;}\nstatic uint32_t input(void){unsigned n=physical();if(n)rawSeen++;return 0xffffu&~((n&1?1u<<12:0)|(n&2?1u<<11:0));}\nstatic void event(unsigned t,unsigned n){assert(nEvents<8);events[nEvents].t=t;events[nEvents++].p=n;}\nstatic void noop(void){}\nstatic void delay(uint32_t n){simUs+=n*1000;assert(simUs<8000000);}\nstatic uint8_t getkey(void){simUs+=200;assert(simUs<8000000);return simUs>=exitAt?APP_KEY_EXIT:APP_KEY_INVALID;}\nstatic uint32_t cycles(void){simUs+=10;return simUs*48;}\nstatic uint32_t ticks(void){return simUs/1000;}\nstatic uint8_t permitted(void){return 0;}\nstatic void legacy(void){\n legacyN++;requestN++;prepStart=simUs;\n simUs+=10000; /* External PA enabled, initially zero bias. */\n simUs+=5000; /* PowerAmplifier writes calibrated nonzero bias. */\n if(!firstLegacyRF)firstLegacyRF=simUs;\n simUs+=10000+extraPrepare;prepEnd=simUs;\n}\nstatic void quiet(void){quietN++;requestN++;prepStart=simUs;simUs+=extraPrepare;prepEnd=simUs;}\nstatic void gate(bool on){if(on){markN++;if(!firstMark)firstMark=simUs;assert(requestN);}}\nstatic void endtx(void){}\nstatic uint16_t readreg(uint8_t reg){return reg==REG_PA_BIAS?0x6400:0;}\nstatic void writereg(uint8_t reg,uint16_t val){(void)reg;(void)val;}\nstatic void audio(bool on){(void)on;}\nstatic void af(uint8_t n){(void)n;}\nstatic uint32_t frequency(void){return 43365000;}\nstatic uint64_t divu(uint32_t n,uint32_t d){return ((uint64_t)(n%d)<<32)|(n/d);}\nstatic uint64_t divs(int32_t n,int32_t d){return ((uint64_t)(uint32_t)(n%d)<<32)|(uint32_t)(n/d);}\nstatic void load(uint8_t *buf,uint8_t len){\n#if IS_DEV\n config_t c={CFG_MAGIC,18,whichMode,0,CAL_DEFAULT,700,300,1,PORT_USB,whichAlgo,0xD3};assert(len==14);\n#else\n config_t c={CFG_MAGIC,18,whichMode,0,CAL_DEFAULT,700,300,1,whichAlgo};assert(len==12);\n#endif\n memcpy(buf,&c,len);\n}\nstatic void save(const uint8_t *buf,uint8_t len){(void)buf;(void)len;}\nstatic const uint8_t assets[]={ASSET_BYTES};\nstatic uint16_t asset(uint16_t off,void *buf,uint16_t len){memset(buf,0,len);unsigned n=off<sizeof(assets)?sizeof(assets)-off:0;if(n>len)n=len;memcpy(buf,assets+off,n);return n;}\nstatic void normal(const char*s,uint8_t x,uint8_t end,uint8_t line){(void)s;(void)x;(void)end;(void)line;}\nstatic void tiny(const char*s,uint8_t x,uint8_t y,bool st,bool fill){(void)s;(void)x;(void)y;(void)st;(void)fill;}\nstatic void inverse(const char*s,uint8_t x,uint8_t y,bool st,bool fill,uint8_t end){(void)s;(void)x;(void)y;(void)st;(void)fill;(void)end;}\nstatic void bigtext(const char*s,uint8_t x,uint8_t y,bool st){(void)s;(void)x;(void)y;(void)st;}\nstatic void bigfreq(uint32_t f,uint8_t x,uint8_t y){(void)f;(void)x;(void)y;}\nstatic char *append(char *dst,uint32_t n){return dst+sprintf(dst,"%u",n);}\nstatic int16_t rssi(void){return -120;}\nstatic int8_t nav(uint8_t n){(void)n;return 0;}\nstatic void blit(unsigned line){(void)line;}\nstatic uint8_t fb[7][128],status[128];\nstatic app_api_t api;\nstatic void init(unsigned mode){\n memset(&g,0,sizeof(g));memset(&api,0,sizeof(api));simUs=nEvents=rawSeen=requestN=markN=legacyN=quietN=prepStart=prepEnd=firstMark=firstLegacyRF=extraPrepare=0;\n whichMode=mode;exitAt=4200000;moder=pupdr=rcc=0;\n api.api_size=sizeof(api);api.fb=fb;api.status_line=status;api.get_key=getkey;api.delay_ms=delay;api.ticks_ms=ticks;\n api.bk_read=readreg;api.bk_write=writereg;api.audio_path=audio;api.set_af=af;api.set_agc=audio;\n api.tx_state=permitted;api.tx_set_params=legacy;api.tx_carrier=gate;api.tx_end=endtx;api.tx_mute=audio;api.tx_freq=frequency;api.rx_freq=frequency;\n api.cfg_load=load;api.cfg_save=save;api.asset_read=asset;api.idivmod=divs;api.uidivmod=divu;\n api.display_clear=noop;api.status_clear=noop;api.blit_full=noop;api.blit_status=noop;api.blit_line=blit;api.print_normal=normal;api.print_tiny=tiny;api.print_inverse=inverse;api.display_freq=bigtext;\n api.backlight_on=noop;api.backlight_update=noop;api.battery_sample=noop;api.draw_battery=noop;api.nav_dir=nav;api.rssi_dbm=rssi;\n#if !IS_DEV\n api.api_size=368;api.cycles_48mhz=cycles;api.tx_prepare=quiet;api.draw_frequency=bigfreq;api.append_u32=append;\n#endif\n}\n'
h=h.replace('extraPrepare,whichMode,whichAlgo;','extraPrepare,whichMode,whichAlgo,cycleOrigin,denyTx,lockLabels,armLabels,paWrites;')
h=h.replace('assert(simUs<8000000)','assert(simUs<40000000)')
h=h.replace('return simUs*48;','return simUs*48+cycleOrigin;')
h=h.replace('return 0;}\nstatic void legacy','return denyTx;}\nstatic void legacy')
h=h.replace('legacyN++;requestN++;prepStart=simUs;','assert(!"legacy fallback must not run");legacyN++;requestN++;prepStart=simUs;')
h=h.replace('quietN++;requestN++;','assert(g.armState==6);quietN++;requestN++;')
h=h.replace('if(on){markN++;','if(on){assert(g.armState==6 && quietN);markN++;')
h=h.replace('(void)reg;(void)val;','if(reg==REG_PA_BIAS && (val>>8)){assert(g.armState==6 && quietN);paWrites++;}')
h=h.replace('(void)s;(void)x;(void)y;(void)st;(void)fill;(void)end;', '(void)x;(void)y;(void)st;(void)fill;(void)end;if(!strcmp(s,"LOCK"))lockLabels++;if(!strcmp(s,"ARM"))armLabels++;')
h=h.replace('whichMode=mode;exitAt=4200000;moder=pupdr=rcc=0;', 'whichMode=mode;exitAt=4200000;moder=pupdr=rcc=cycleOrigin=denyTx=lockLabels=armLabels=paWrites=0;')
h=h.replace('simUs+=200;assert(simUs<40000000);return simUs>=exitAt?APP_KEY_EXIT:APP_KEY_INVALID;', 'simUs+=scanUs;assert(simUs<40000000);uint8_t k=APP_KEY_INVALID;for(unsigned i=0;i<nKeys;i++)if(simUs>=keyEvents[i].t)k=keyEvents[i].key;return simUs>=exitAt?APP_KEY_EXIT:k;')
h=h.replace('static unsigned nEvents,','static unsigned scanUs=200,nKeys;\nstatic struct {unsigned t;uint8_t key;} keyEvents[16];\nstatic unsigned nEvents,')
h=h.replace('events[8]','events[64]').replace('assert(nEvents<8)','assert(nEvents<64)')
h=h.replace('memset(&g,0,sizeof(g));memset(&api,0,sizeof(api));','memset(&g,0,sizeof(g));memset(&api,0,sizeof(api));nKeys=0;scanUs=200;')
h=h.replace('static void save(const uint8_t *buf,uint8_t len){(void)buf;(void)len;}', 'static uint8_t stored[12];static void save(const uint8_t *buf,uint8_t len){assert(len==12);memcpy(stored,buf,len);}')
h=h.replace('static uint16_t asset(uint16_t off,void *buf,uint16_t len){', 'static uint16_t asset(uint16_t off,void *buf,uint16_t len){if(buf==A->status_line+32u){assert(len==7);if(off==BMP_LOCK){assert(g.armState<4);lockLabels++;}else{assert(off==BMP_READY && g.armState>=4);armLabels++;}}')
h=h.replace('static int16_t rssi(void){return -120;}', 'static unsigned rssiCalls;static int16_t rssiValue=-120;static int16_t rssi(void){rssiCalls++;return rssiValue;}')
h=h.replace('static int8_t nav(uint8_t n){(void)n;return 0;}', 'static int8_t nav(uint8_t n){return n==APP_KEY_UP?1:n==APP_KEY_DOWN?-1:0;}')
h=h.replace('static void load(uint8_t *buf,uint8_t len){', 'static unsigned cfgOverride;static uint8_t cfgRaw[12];static void load(uint8_t *buf,uint8_t len){')
h=h.replace('memcpy(buf,&c,len);', 'if(cfgOverride)memcpy(buf,cfgRaw,len);else memcpy(buf,&c,len);')
h=h.replace('nKeys=0;scanUs=200;', 'nKeys=0;scanUs=200;cfgOverride=0;')
h='static void renderedTiny(const char*,uint8_t,uint8_t,bool,bool);\nstatic void renderedInverse(const char*,uint8_t,uint8_t,bool,bool,uint8_t);\nstatic void clearDisplay(void),clearStatus(void),showStatus(void),showFull(void),recordEdge(bool),extraInit(void);\n'+h

h=h.replace('(void)s;(void)x;(void)y;(void)st;(void)fill;', 'renderedTiny(s,x,y,st,fill);')
h=h.replace('(void)x;(void)y;(void)st;(void)fill;(void)end;', 'renderedInverse(s,x,y,st,fill,end);')
h=h.replace('api.display_clear=noop;api.status_clear=noop;', 'api.display_clear=clearDisplay;api.status_clear=clearStatus;')
h=h.replace('api.blit_full=noop;api.blit_status=noop;', 'api.blit_full=showFull;api.blit_status=showStatus;')
h=h.replace('static void gate(bool on){', 'static void gate(bool on){recordEdge(on);')
h=h.replace('nKeys=0;scanUs=200;', 'extraInit();nKeys=0;scanUs=200;')

driver=r'''
static unsigned cases;
static void keyboard(unsigned t,uint8_t k){assert(nKeys<16);keyEvents[nKeys].t=t;keyEvents[nKeys++].key=k;}
static void setup(unsigned mode,unsigned algorithm){whichAlgo=algorithm;init(mode);A=&api;g.mode=mode;}
static void sample(unsigned delta,unsigned contacts,uint8_t key){simUs+=delta;nEvents=0;event(0,contacts);armUsb(key);assert(!requestN && !markN && !paWrites);}
static void releaseInitial(void){for(unsigned i=0;i<25;i++)sample(1000,0,APP_KEY_INVALID);assert(g.armState==2);}
static void hold(unsigned contacts,unsigned step){
 sample(1000,contacts,APP_KEY_INVALID);assert(g.armState==3);unsigned start=simUs;
 while(g.armState==3){sample(step,contacts,APP_KEY_INVALID);if(simUs-start<5000000)assert(g.armState!=4);}
 assert(g.armState==4 && simUs-start>=5000000);
}
static void releaseFinal(void){g.padStable=3;sample(1000,0,APP_KEY_INVALID);assert(g.armState==5);for(unsigned i=0;i<25;i++)sample(1000,0,APP_KEY_INVALID);assert(g.armState==6 && !g.padStable);}
static void finish(const char *name){app_main(&api);assert(!g.txReady && !g.keyDown && !g.portOpen && !g.armState);printf("{\"case\":\"%s\",\"mode\":%u,\"algorithm\":%u,\"prepare\":%u,\"marks\":%u,\"PA_writes\":%u,\"LOCK\":%u,\"ARM\":%u}\n",name,whichMode,whichAlgo,quietN,markN,paWrites,lockLabels,armLabels);cases++;}
int main(void){
 setbuf(stdout,NULL);
 for(unsigned mode=0;mode<2;mode++){
  for(unsigned step=1000;step<=43000;step+=21000){
   setup(mode,0);releaseInitial();hold(mode?3:1,step);releaseFinal();assert(!quietN && !markN);cases++;
  }
  setup(mode,0);cycleOrigin=0xffffffffu-100000000u;releaseInitial();hold(mode?3:1,17000);releaseFinal();cases++;
  setup(mode,0);for(unsigned i=0;i<6000;i++)sample(1000,mode?3:1,APP_KEY_INVALID);assert(!g.armState);cases++;
  setup(mode,0);releaseInitial();sample(1000,mode?3:1,APP_KEY_INVALID);
  for(unsigned i=0;i<4900;i++)sample(1000,mode?3:1,APP_KEY_INVALID);
  sample(1000,0,APP_KEY_INVALID);assert(g.armState==2);hold(mode?3:1,1000);sample(1000,0,APP_KEY_INVALID);sample(1000,mode?3:1,APP_KEY_INVALID);assert(g.armState==4);releaseFinal();cases++;
  setup(mode,0);releaseInitial();sample(1000,mode?3:1,APP_KEY_INVALID);sample(100000,mode?3:1,APP_KEY_INVALID);assert(g.armState==3);unsigned stamp=g.armSince;
  sample(1000,mode?3:1,APP_KEY_PTT);assert(g.armState==2);assert(stamp);cases++;
  setup(mode,0);releaseInitial();sample(1000,mode?3:1,APP_KEY_INVALID);adjustSetting(APP_KEY_1,1);assert(g.armState==0);cases++;
  setup(mode,0);releaseInitial();sample(1000,mode?3:1,APP_KEY_INVALID);g.help=true;sample(1000,mode?3:1,APP_KEY_INVALID);assert(g.armState==0);cases++;
  setup(mode,0);api.api_size=364;for(unsigned i=0;i<6000;i++)sample(1000,mode?3:1,APP_KEY_INVALID);assert(!g.armState);cases++;
 }
 for(unsigned mode=0;mode<2;mode++)for(unsigned algorithm=0;algorithm<(HAS_IAMBIC?3u:1u);algorithm++){
  setup(mode,algorithm);event(100000,mode?3:1);event(5200000,0);event(5400000,1);event(5500000,0);event(6800000,mode?2:1);event(6900000,0);event(8000000,1);event(8100000,0);exitAt=9500000;
  finish("arm then three RX-TX cycles");assert(quietN==3 && markN>=3 && paWrites && lockLabels && armLabels);
  setup(mode,algorithm);event(100000,mode?3:1);event(5200000,0);event(5400000,1);event(5408000,0);event(6800000,mode?2:1);event(6808000,0);exitAt=8200000;
  finish("brief first requests after RX");assert(quietN==2 && markN>=2);
  setup(mode,algorithm);event(100000,mode?3:1);exitAt=6500000;finish("held after 5s never released");assert(!quietN && !markN && armLabels);
  setup(mode,algorithm);event(0,mode?3:1);event(5500000,0);exitAt=6000000;finish("closed at entry");assert(!quietN && !markN && !armLabels);
  setup(mode,algorithm);event(100000,mode?3:1);exitAt=2000000;finish("EXIT during countdown");assert(!quietN && !markN);
  setup(mode,algorithm);g.armState=6;exitAt=300000;finish("reopen discards prior ARM");assert(!quietN && !markN && !armLabels);
  setup(mode,algorithm);event(100000,mode?3:1);event(5200000,0);event(5400000,1);event(5500000,0);denyTx=1;exitAt=6500000;finish("TX permission denied after arming");assert(!quietN && !markN && armLabels);
  setup(mode,algorithm);keyboard(100000,APP_KEY_PTT);keyboard(6000000,APP_KEY_INVALID);exitAt=6500000;finish("PTT cannot arm or transmit");assert(!quietN && !markN && !armLabels);
 }

#if HAS_IAMBIC
 {
  for(unsigned armed=0;armed<2;armed++)for(unsigned algorithm=0;algorithm<3;algorithm++){
   setup(1,algorithm);loadConfig();g.armState=armed?6:2;
   unsigned expected=(algorithm+1)%3;handleUiKey(APP_KEY_8);assert((g.iambic>>4)==expected);assert(g.armState==(armed?6:2));
   saveConfig();uint8_t snapshot[12];memcpy(snapshot,stored,12);g.armState=0;saveConfig();assert(!memcmp(snapshot,stored,12));assert(stored[11]==expected);
   whichAlgo=stored[11];memset(&g,0,sizeof(g));A=&api;loadConfig();assert((g.iambic>>4)==expected && !g.armState);cases++;
  }
 }

#endif
 for(unsigned st=0;st<=6;st++)for(unsigned help=0;help<2;help++)for(unsigned denied=0;denied<2;denied++){
  setup(1,0);g.armState=st;g.help=help;g.denied=denied;drawMainStatus();assert(!memcmp(status+32,assets+(st>=4?BMP_READY:BMP_LOCK),7));assert(st>=4?armLabels:lockLabels);cases++;
 }
 setup(1,0);rssiCalls=0;g.smDbm=-120;for(unsigned n=0;n<200;n++)serviceOneMs();assert(!rssiCalls);g.armState=6;for(unsigned n=0;n<200;n++)serviceOneMs();assert(rssiCalls);cases++;
 for(int v=-160;v<=-53;v++){setup(1,0);g.armState=6;g.smDbm=v;drawSmeter();unsigned expected=v>=-93?9+(v+93)/10:v>-147?(v+147)/6:0;assert(smeterLevel(v)==expected);cases++;}
 setup(1,0);releaseInitial();sample(1000,3,APP_KEY_INVALID);simUs+=100000;drawMainStatus();sample(1000,3,APP_KEY_INVALID);unsigned start=simUs;while(g.armState==3)sample(1000,3,APP_KEY_INVALID);assert(simUs-start>=5000000);releaseFinal();cases++;
 setup(1,0);g.uiHandled=true;resetUiInput();handleUiInput(APP_KEY_8);assert(!g.uiHandled && !g.iambic);simUs+=19999;handleUiInput(APP_KEY_8);assert(!g.iambic);simUs+=2;handleUiInput(APP_KEY_8);assert(g.iambic==16 && g.uiHandled);simUs+=1000000;handleUiInput(APP_KEY_8);assert(g.iambic==16);resetUiInput();handleUiInput(APP_KEY_8);simUs+=20001;handleUiInput(APP_KEY_8);assert(g.iambic==32);cases++;
 setup(1,0);resetUiInput();handleUiInput(APP_KEY_UP);simUs+=20001;handleUiInput(APP_KEY_UP);assert(g.tuneOffset10Hz==10);simUs+=399000;handleUiInput(APP_KEY_UP);assert(g.tuneOffset10Hz==10);simUs+=1001;handleUiInput(APP_KEY_UP);assert(g.tuneOffset10Hz==20);simUs+=100001;handleUiInput(APP_KEY_UP);assert(g.tuneOffset10Hz==30);resetUiInput();handleUiInput(APP_KEY_DOWN);simUs+=20001;handleUiInput(APP_KEY_DOWN);assert(g.tuneOffset10Hz==20);cases++;
 for(unsigned c=0;c<4;c++){setup(1,0);nEvents=0;event(0,c);unsigned expected=0xffffu&~((c&1?1u<<12:0)|(c&2?1u<<11:0));assert(input()==expected && usbContacts()==c);cases++;}
 setup(1,0);moder=0xabcdef01;pupdr=0xdeadbeef;rcc=0xfedcba98;unsigned m=moder,p=pupdr,r=rcc;portSet(true);assert((moder&USB_PINS)==0 && (pupdr&USB_PINS)==USB_PULLUP && !(rcc&USBEN));portSet(false);assert(moder==m && pupdr==p && rcc==r);cases++;
 for(unsigned mode=0;mode<2;mode++)for(unsigned algo=0;algo<3;algo++){
  setup(mode,algo);event(100000,mode?3:1);event(5200000,0);event(5400000,1);event(5750000,0);exitAt=5700000;finish("EXIT requested during TX, contacts released");assert(quietN==1 && markN);}
 setup(0,0);event(100000,1);event(5200000,0);event(5400000,1);event(20500000,0);exitAt=21000000;finish("corrected STRAIGHT mark-limit release behavior");assert(quietN==1 && markN==1);
 const unsigned magic[]={0,0xca,0xcb,0xff};
 for(unsigned m=0;m<4;m++)for(unsigned v=0;v<256;v++){
  setup(1,0);config_t c={magic[m],18,1,0,CAL_DEFAULT,700,300,1,v};memcpy(cfgRaw,&c,12);cfgOverride=1;loadConfig();assert((g.iambic>>4)==(magic[m]==CFG_MAGIC && v<=2?v:0));assert(!g.armState);saveConfig();assert(stored[11]==(g.iambic>>4));cases++;}

 for(unsigned mode=0;mode<2;mode++)for(unsigned algorithm=0;algorithm<3;algorithm++){
  setup(mode,algorithm);releaseInitial();sample(1000,mode?3:1,APP_KEY_INVALID);
  for(unsigned n=0;n<4800;n++)sample(1000,mode?3:1,APP_KEY_INVALID);
  assert(g.armState==3);drawMainStatus();assert(!memcmp(status+32,assets+BMP_LOCK,7));
  unsigned deadlineUs=g.armSince/48;
  while(simUs+1010<deadlineUs-100010)sample(1000,mode?3:1,APP_KEY_INVALID);
  sample(deadlineUs-100010-simUs,mode?3:1,APP_KEY_INVALID);assert(g.armState==3);
  while(simUs+1010<deadlineUs-11)sample(1000,mode?3:1,APP_KEY_INVALID);
  sample(deadlineUs-11-simUs,mode?3:1,APP_KEY_INVALID);assert(g.armState==3);
  sample(1,mode?3:1,APP_KEY_INVALID);assert(g.armState==4);drawMainStatus();assert(!memcmp(status+32,assets+BMP_READY,7));
  for(unsigned n=0;n<5000;n++)sample(1000,mode?3:1,APP_KEY_INVALID);
  assert(g.armState==4 && !quietN && !markN);
  if(mode){sample(1000,1,APP_KEY_INVALID);assert(g.armState==4);}
  sample(1000,0,APP_KEY_INVALID);assert(g.armState==5);sample(1000,mode?3:1,APP_KEY_INVALID);assert(g.armState==4);
  drawMainStatus();assert(!memcmp(status+32,assets+BMP_READY,7));releaseFinal();assert(g.armState==6);cases++;
 }
 setup(1,0);memset(status,0,sizeof(status));GUI_DisplaySmallest("1.2 IAMB A",84,1,true,false);unsigned dark=0;for(unsigned n=0;n<128;n++)dark+=status[n]!=0;assert(!dark);cases++;
 setup(1,0);keyboard(100000,APP_KEY_8);keyboard(160000,APP_KEY_INVALID);exitAt=300000;finish("actual key 8 selection saved on close");assert(stored[11]==1 && modesDrawn==3 && !quietN);
 unsigned restored=stored[11];setup(1,restored);exitAt=300000;finish("actual saved selection restored on reopen");assert(stored[11]==1 && modesDrawn==2 && !quietN && !armLabels);
 for(unsigned holdEnd=8000000;holdEnd<=10000000;holdEnd+=2000000)for(unsigned mode=0;mode<2;mode++)for(unsigned algorithm=0;algorithm<3;algorithm++){
  setup(mode,algorithm);event(100000,mode?3:1);event(holdEnd,0);event(holdEnd+200000,mode?2:1);event(holdEnd+300000,0);exitAt=holdEnd+1200000;
  finish("Ready before release, first DAH or STRAIGHT afterwards");assert(firstReadyAt>=5100000 && firstReadyAt<holdEnd && firstReadyState==4);assert(firstMark>holdEnd+20000 && quietN==1 && markN);
 }
 setup(1,0);keyboard(200000,APP_KEY_8);keyboard(260000,APP_KEY_INVALID);event(500000,3);event(7000000,0);
 keyboard(7200000,APP_KEY_8);keyboard(7260000,APP_KEY_INVALID);event(7500000,1);event(7600000,0);
 keyboard(8200000,APP_KEY_8);keyboard(8260000,APP_KEY_INVALID);exitAt=9000000;
 finish("keyboard 8 before ARM, after ARM, after RX");assert(modesDrawn==7 && stored[11]==0 && quietN==1);
 setup(1,1);exitAt=300000;finish("reopen persisted IAMB A is visible but LOCK");assert(modesDrawn==2 && stored[11]==1 && !quietN);
 setup(1,2);exitAt=300000;finish("reopen persisted IAMB B is visible but LOCK");assert(modesDrawn==4 && stored[11]==2 && !quietN);
 for(unsigned algorithm=0;algorithm<3;algorithm++){
  setup(0,algorithm);event(100000,1);event(5200000,0);event(5400000,1);event(20500000,0);event(20800000,1);event(20900000,0);exitAt=22000000;
  finish("long STRAIGHT, no restart on release, fresh contact allowed");assert(quietN==2 && markN==2);
  unsigned ons=0;for(unsigned n=0;n<nEdges;n++)if(edgeOn[n]){ons++;if(ons==2)assert(edgeTime[n]>=20800000);}assert(ons==2);
 }

 printf("{\"passed_cases\":%u}\n",cases);return 0;
}
'''
s=(r/'App/apps/cwkeyer/cwkeyer_app.c').read_text();api=(r/'App/apps/app_api.h').read_text();api=re.sub(r'_Static_assert\(.*?\);','/* ARM layout checked separately */',api,flags=re.S);(out/'host_api.h').write_text(api)
s=s.replace('#include "../app_api.h"','#include "'+(out/'host_api.h').as_posix()+'"')
assets=r/'App/apps/cwusb/cwusb_assets.h';s='#define CW_KEYER_USB 1\n#include "'+assets.as_posix()+'"\n'+s
for macro,value in [('GPIOA_MODER','moder'),('GPIOA_PUPDR','pupdr'),('GPIOA_IDR','input()'),('RCC_APBENR1','rcc'),('SYST_LOAD','9999u'),('SYST_VAL','(9999u-simUs%10000u)')]:s=re.sub(r'^#define '+macro+r'\s+.*$','#define '+macro+' '+value,s,flags=re.M)
for name,new in [('irqSave','static uint32_t irqSave(void){return 0;}'),('irqRestore','static void irqRestore(uint32_t v){(void)v;}')]:
 start=re.search(r'^static [^\n]+\b'+name+r'\([^\n]*\)\n\{',s,re.M).start();end=s.index('\n}',start)+2;s=s[:start]+new+s[end:]
data=assets.with_suffix('.bin').read_bytes();h=h.replace('ASSET_BYTES',','.join(map(str,data)))
code='#define IS_DEV 0\n#define HAS_IAMBIC '+str(int(label!='official-tests'))+'\n#include <stdint.h>\nstatic uint32_t simUs,moder,pupdr,rcc;\nstatic uint32_t input(void);\n'+s+h+renderer+driver
test=out/'test.c';test.write_text(code);exe=test.with_suffix('.exe')
res=subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-Wall','-Wextra','-ftrapv',str(test),'-o',str(exe)],capture_output=True);(out/'build.log').write_bytes(res.stdout+res.stderr);assert not res.returncode,res.stderr.decode()
res=subprocess.run([str(exe)],capture_output=True);(out/'run.log').write_bytes(res.stdout+res.stderr);assert not res.returncode,res.stderr.decode()
rows=[json.loads(l) for l in res.stdout.splitlines()];(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(label,rows[-1])
