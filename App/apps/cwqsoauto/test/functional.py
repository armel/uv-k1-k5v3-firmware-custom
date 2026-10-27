"""Host execution of production CQ/RF/UI source with simulated API and GPIO.
No physical RF, contact physics or resident stack validation is implied.
"""
from pathlib import Path
import re, subprocess, os, json
p=Path(__file__).resolve().parent
r=p.parents[3]
app=p.parent
out=p/'out';out.mkdir(exist_ok=True)
s=(app/'cwqsoauto_app.c').read_text()
api=(r/'App/apps/app_api.h').read_text()
api=re.sub(r'_Static_assert\(.*?\);','/* ARM layout tested by ARM build */',api,flags=re.S)
(out/'host_api.h').write_text(api)
s=s.replace('#include "../app_api.h"','#include "'+(out/'host_api.h').as_posix()+'"').replace('#include "cwqsoauto_assets.h"','#include "'+(app/'cwqsoauto_assets.h').as_posix()+'"')
for macro,value in [('GPIOA_MODER','moder'),('GPIOA_PUPDR','pupdr'),('GPIOA_IDR','input()'),('RCC_APBENR1','rcc'),('SYST_LOAD','479999u'),('SYST_VAL','(479999u-(simUs%10000u)*48u)')]:
 s=re.sub(r'^#define '+macro+r'\s+.*$','#define '+macro+' '+value,s,flags=re.M)
for name,body in [('irqSave','static uint32_t irqSave(void){return 0;}'),('irqRestore','static void irqRestore(uint32_t v){(void)v;}')]:
 m=re.search(r'^static [^\n]+\b'+name+r'\([^\n]*\)\n\{',s,re.M);end=s.index('\n}',m.start())+2;s=s[:m.start()]+body+s[end:]

h='\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\nstatic unsigned nEvents,exitAt,rawSeen,requestN,markN,legacyN,quietN,prepStart,prepEnd,firstMark,firstLegacyRF,extraPrepare,whichMode,whichAlgo;\nstatic struct {unsigned t,p;} events[8];\nstatic unsigned physical(void){unsigned n=0;for(unsigned i=0;i<nEvents;i++)if(simUs>=events[i].t)n=events[i].p;return n;}\nstatic uint32_t input(void){unsigned n=physical();if(n)rawSeen++;return 0xffffu&~((n&1?1u<<12:0)|(n&2?1u<<11:0));}\nstatic void event(unsigned t,unsigned n){assert(nEvents<8);events[nEvents].t=t;events[nEvents++].p=n;}\nstatic void noop(void){}\nstatic void delay(uint32_t n){simUs+=n*1000;assert(simUs<150000000);}\nstatic uint8_t getkey(void){simUs+=200;assert(simUs<150000000);uint8_t k=APP_KEY_INVALID;for(unsigned i=0;i<nKeys;i++)if(simUs>=keys[i].t)k=keys[i].key;return ((simUs>=exitAt && simUs<exitAt+100000) || simUs>=exitAt+200000)?APP_KEY_EXIT:k;}\nstatic uint32_t cycles(void){simUs++;return simUs*48+cycleOrigin;}\nstatic uint32_t ticks(void){return simUs/1000;}\nstatic uint8_t permitted(void){return denyTx || (denyAfter && markN>=denyAfter);}\nstatic void legacy(void){assert(!"legacy TX forbidden");\n legacyN++;requestN++;prepStart=simUs;\n simUs+=10000; /* External PA enabled, initially zero bias. */\n simUs+=5000; /* PowerAmplifier writes calibrated nonzero bias. */\n if(!firstLegacyRF)firstLegacyRF=simUs;\n simUs+=10000+extraPrepare;prepEnd=simUs;\n}\nstatic void quiet(void){quietN++;requestN++;prepStart=simUs;simUs+=extraPrepare;prepEnd=simUs;}\nstatic void gate(bool on){if(on!=rf){assert(nEdges<2048);edgeTime[nEdges]=simUs;edgeOn[nEdges++]=on;rf=on;}if(on){assert(quietN);markN++;if(!firstMark)firstMark=simUs;assert(requestN);}}\nstatic void endtx(void){assert(!rf);endN++;}\nstatic uint16_t readreg(uint8_t reg){return reg==REG_PA_BIAS?0x6400:0;}\nstatic void writereg(uint8_t reg,uint16_t val){if(reg==REG_PA_BIAS && (val>>8))assert(quietN);}\nstatic void audio(bool on){(void)on;}\nstatic void af(uint8_t n){(void)n;}\nstatic uint32_t radioFrequency=43365000;static uint32_t frequency(void){return radioFrequency;}\nstatic uint64_t divu(uint32_t n,uint32_t d){return ((uint64_t)(n%d)<<32)|(n/d);}\nstatic uint64_t divs(int32_t n,int32_t d){return ((uint64_t)(uint32_t)(n%d)<<32)|(uint32_t)(n/d);}\nstatic void load(uint8_t *buf,uint8_t len){\n#if IS_DEV\n config_t c={CFG_MAGIC,18,whichMode,0,CAL_DEFAULT,700,300,1,PORT_USB,whichAlgo,0xD3};assert(len==14);\n#else\n config_t c={CFG_MAGIC,18,whichMode,0,CAL_DEFAULT,700,300,1,whichAlgo};assert(len==12);\n#endif\n memcpy(buf,&c,len);\n}\nstatic uint8_t saved[12];static void save(const uint8_t *buf,uint8_t len){assert(len==12);memcpy(saved,buf,len);}\nstatic const uint8_t assets[]={ASSET_BYTES};\nstatic uint16_t asset(uint16_t off,void *buf,uint16_t len){memset(buf,0,len);unsigned n=off<sizeof(assets)?sizeof(assets)-off:0;if(n>len)n=len;memcpy(buf,assets+off,n);return n;}\nstatic char screen[7][32];static void normal(const char*s,uint8_t x,uint8_t end,uint8_t line){assert(line<7 && x+strlen(s)*7<=128);snprintf(screen[line]+(x-2u)/7u,32-(x-2u)/7u,"%s",s);if(line==2)snprintf(selectedLabel,32,"%s",s);if(line==3)snprintf(rstLabel,32,"%s",s);if(!strncmp(s,"TX ",3))autoLabels++;if(!strcmp(s,"INVALID BOOT CALL"))badLabels++;(void)x;(void)end;(void)line;}\nstatic void tiny(const char*s,uint8_t x,uint8_t y,bool st,bool fill){(void)s;(void)x;(void)y;(void)st;(void)fill;}\nstatic char headerText[32];static void clearStatus(void){headerText[0]=0;}static void inverse(const char*s,uint8_t x,uint8_t y,bool st,bool fill,uint8_t end){if(st){snprintf(headerText,32,"%s",s);if(!strncmp(s,"TX ",3))autoLabels++;}assert(x+strlen(s)*4<=128);\n if(!st && y>=4 && y<=6 && (x==2 || x==66))snprintf(capsules[(y-4)*2+(x==66)],32,"%s",s);\n (void)fill;(void)end;\n}\nstatic void clearScreen(void){memset(screen,0,sizeof(screen));memset(capsules,0,sizeof(capsules));}\nstatic void bigtext(const char*s,uint8_t x,uint8_t y,bool st){(void)s;(void)x;(void)y;(void)st;assert(!"large text forbidden");}\nstatic void bigfreq(uint32_t f,uint8_t x,uint8_t y){(void)f;(void)x;(void)y;assert(!"large frequency forbidden");}\nstatic char *append(char *dst,uint32_t n){return dst+sprintf(dst,"%u",n);}\nstatic int16_t rssi(void){assert(!"RSSI must be absent");return -120;}\nstatic int8_t nav(uint8_t n){(void)n;return 0;}\nstatic void blit(unsigned line){(void)line;}\nstatic uint8_t fb[7][128],status[128];\nstatic app_api_t api;\nstatic void init(unsigned mode){\n memset(&g,0,sizeof(g));memset(&q,0,sizeof(q));memset(&api,0,sizeof(api));nKeys=denyTx=denyAfter=nEdges=endN=autoLabels=badLabels=cycleOrigin=0;rf=false;simUs=nEvents=rawSeen=requestN=markN=legacyN=quietN=prepStart=prepEnd=firstMark=firstLegacyRF=extraPrepare=0;\n radioFrequency=43365000;whichMode=mode;exitAt=4200000;moder=pupdr=rcc=0;\n api.api_size=sizeof(api);api.fb=fb;api.status_line=status;api.get_key=getkey;api.delay_ms=delay;api.ticks_ms=ticks;\n api.bk_read=readreg;api.bk_write=writereg;api.audio_path=audio;api.set_af=af;api.set_agc=audio;\n api.tx_state=permitted;api.tx_set_params=legacy;api.tx_carrier=gate;api.tx_end=endtx;api.tx_mute=audio;api.tx_freq=frequency;api.rx_freq=frequency;\n api.cfg_load=load;api.cfg_save=save;api.asset_read=asset;api.idivmod=divs;api.uidivmod=divu;\n api.display_clear=clearScreen;api.status_clear=clearStatus;api.blit_full=noop;api.blit_status=noop;api.blit_line=blit;api.print_normal=normal;api.print_tiny=tiny;api.print_inverse=inverse;api.display_freq=bigtext;\n api.backlight_on=noop;api.backlight_update=noop;api.battery_sample=noop;api.draw_battery=noop;api.nav_dir=nav;api.rssi_dbm=rssi;\n#if !IS_DEV\n api.boot_callsign=app_boot_callsign;api.append_i32=appendSigned;api.api_size=368;api.cycles_48mhz=cycles;api.tx_prepare=quiet;api.draw_frequency=bigfreq;api.append_u32=append;\n#endif\n}\n'
extra='#include <assert.h>\n\n#include <stdint.h>\n#include <stdbool.h>\n#include <string.h>\n#include <stdio.h>\nstatic uint32_t simUs,moder,pupdr,rcc,cycleOrigin;\nstatic unsigned nKeys,denyTx,denyAfter,nEdges,endN,autoLabels,badLabels;\nstatic struct {unsigned t;uint8_t key;} keys[32];\nstatic unsigned edgeTime[2048];static bool edgeOn[2048],rf;\nstatic uint32_t input(void);\nstatic char *appendSigned(char *dst,int32_t n){return dst+sprintf(dst,"%d",n);}\nstatic void app_boot_callsign(char *b,uint8_t n){(void)b;(void)n;assert(!"Boot Call forbidden in v0.2");}\n\nstatic char capsules[6][32],selectedLabel[32],rstLabel[32];\n'

driver=(p/'functional.c').read_text()

h=h.replace('static int8_t nav(uint8_t n){(void)n;return 0;}', 'static int8_t nav(uint8_t n){return n==APP_KEY_UP?1:n==APP_KEY_DOWN?-1:0;}')
h=h.replace('static void load(uint8_t *buf,uint8_t len){','static bool useSaved;static void load(uint8_t *buf,uint8_t len){if(useSaved){memcpy(buf,saved,len);return;}')
h=h.replace('static bool useSaved;', 'static uint8_t saved[12];static bool useSaved;').replace('static uint8_t saved[12];static void save','static void save')
code='#define IS_DEV 0\n'+extra+s+h.replace('ASSET_BYTES',','.join(map(str,(app/'cwqsoauto_assets.bin').read_bytes())))+driver
test=out/'functional.c';test.write_text(code)
exe=out/'functional.exe'
a=subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-Wall','-Wextra','-ftrapv',str(test),'-o',str(exe)],capture_output=True)
(out/'build.log').write_bytes(a.stdout+a.stderr)
assert a.returncode==0,a.stderr.decode()
a=subprocess.run([str(exe)],capture_output=True);(out/'run.log').write_bytes(a.stdout+a.stderr)
assert a.returncode==0,a.stderr.decode()
rows=[json.loads(line) for line in a.stdout.splitlines()]
# Independent Morse oracle: check every emitted element and every gap, at all 56 speeds.
patterns=dict(zip('ABCDEFGHIJKLMNOPQRSTUVWXYZ',['.-','-...','-.-.','-..','.','..-.','--.','....','..','.---','-.-','.-..','--','-.','---','.--.','--.-','.-.','...','-','..-','...-','.--','-..-','-.--','--..']))
patterns['/']='-..-.'
patterns['?']='..--..'
patterns.update(dict(zip('0123456789',['-----','.----','..---','...--','....-','.....','-....','--...','---..','----.'])))
header=(app/'cwqsoauto_assets.h').read_text()
base=int(re.search(r'#define MORSE (\d+)u',header).group(1));assets=(app/'cwqsoauto_assets.bin').read_bytes()
assert assets.count(b'F4XXX')==3 and assets.count(b'SEB')==1
assert b'F4XXX' not in (app/'cwqsoauto.bin').read_bytes()

for char,pattern in patterns.items():
 code=assets[base+ord(char)-ord('/')];decoded=''
 while code>1:
  decoded+='-' if code&1 else '.';code>>=1
 assert decoded==pattern,(char,decoded,pattern)
templates=['CQ CQ CQ DE F4XXX F4XXX K','DE F4XXX UR {rst} {rst} NAME SEB HW?','73 TU E E','AGN?','?']
for row in rows[:-1]:
 message=templates[row['message']].format(rst=row['rst'])
 expected=[]
 for i,c in enumerate(message):
  if c==' ':continue
  for j,el in enumerate(patterns[c]):
   gap=1 if j+1<len(patterns[c]) else 7 if i+1<len(message) and message[i+1]==' ' else 3
   expected.append((3 if el=='-' else 1,gap))
 edges=row['edges'];dot=(1200//row['speed'])*1000
 assert len(edges)==2*len(expected),(row['speed'],len(edges),len(expected))
 for i,(mark,gap) in enumerate(expected):
  actual=edges[2*i+1]-edges[2*i];target=mark*dot+3000
  assert target<=actual<=target+1400,(row['speed'],i,'mark',actual,target)
  if i+1<len(expected):
   actual=edges[2*i+2]-edges[2*i+1];target=gap*dot-3000
   assert target<=actual<=target+1400,(row['speed'],i,'gap',actual,target)
(out/'results.json').write_text(json.dumps(rows,indent=2))
print(rows[-1],'; 5 messages and gaps checked at 5..60 WPM')
