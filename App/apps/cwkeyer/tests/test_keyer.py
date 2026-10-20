# SPDX-License-Identifier: Apache-2.0
"""Host tests of the actual candidate C source, outside the overlay."""
import argparse,hashlib,json,os,re,shutil,struct,subprocess,zlib
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--output-dir',type=Path,required=True)
args=parser.parse_args()
r=Path(__file__).resolve().parents[4]; f=r/'App/apps/cwkeyer'
out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
s=(f/'cwkeyer_app.c').read_text(encoding='utf-8')
base=(Path(__file__).parent/'legacy_wait.c').read_text(encoding='utf-8')
def function(t,n):
    p=re.search(r'^static [^\n]+\b'+n+r'\([^\n]*\)\n\{',t,re.M).start()
    return t[p:t.index('\n}',p)+2]
test=s
for macro,var in [('GPIOA_MODER','fakeModer'),('GPIOA_PUPDR','fakePupdr'),('GPIOA_IDR','input()'),('RCC_APBENR1','fakeRcc'),('SYST_LOAD','9999u'),('SYST_VAL','(9999u-simUs%10000u)')]:
    test=re.sub(r'^#define '+macro+r'\s+.*$','#define '+macro+' '+var,test,flags=re.M)
test=test.replace(function(test,'irqSave'),'static uint32_t irqSave(void) { return 0; }')
test=test.replace(function(test,'irqRestore'),'static void irqRestore(uint32_t v) { (void)v; }')
test=test.replace('#include "../app_api.h"','#include "'+(r/'App/apps/app_api.h').as_posix()+'"')
test=test.replace('#include "cwkeyer_assets.h"','#include "'+(f/'cwkeyer_assets.h').as_posix()+'"')
test+='\n'+function(base,'keyerWait').replace('keyerWait','legacyWait')+'\n'+function(base,'runPaddle').replace('runPaddle','legacyRun').replace('keyerWait','legacyWait')
h=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct event { unsigned t,p; };
static struct event events[8];
static unsigned eventsN,exitAt,portSide,reverseInput,edges,onTime[32],offTime[32],calls;
static char trace[33];
static unsigned physical(void) {
 unsigned p=0;
 for(unsigned i=0;i<eventsN;i++) if(simUs>=events[i].t) p=events[i].p;
 return reverseInput ? ((p>>1)|((p&1)<<1)) : p;
}
static uint32_t input(void) {
 unsigned p=physical();return 0xffffu & ~((p&1?1u<<12:0)|(p&2?1u<<11:0));
}
static void noop(void) {}
static void delay(uint32_t ms) {simUs+=1000*ms;assert(simUs<10000000);}
static uint8_t key(void) {
 simUs+=200;assert(simUs<10000000);
 if(simUs>=exitAt)return APP_KEY_EXIT;
 unsigned p=physical();
 return !portSide ? APP_KEY_INVALID : p&1 ? KEY_SIDE1 : p&2 ? KEY_SIDE2 : APP_KEY_INVALID;
}
static void carrier(bool on) {
 calls++;
 if(on) { assert(edges<32);onTime[edges]=simUs;trace[edges]=(g.element&3)==P_DIT?'.':'-';trace[++edges]=0; }
 else {assert(edges);offTime[edges-1]=simUs;}
}
static uint8_t txState(void) {return 0;}
static void writeReg(uint8_t reg,uint16_t v) {(void)reg;(void)v;}
static int8_t nav(uint8_t k) {(void)k;return 0;}
static void audio(bool b) {(void)b;}
static uint64_t divide(uint32_t n,uint32_t d) {return ((uint64_t)(n%d)<<32)|(n/d);}
static const app_api_t api={.get_key=key,.delay_ms=delay,.bk_write=writeReg,
 .tx_state=txState,.tx_carrier=carrier,.backlight_update=noop,.backlight_on=noop,
 .nav_dir=nav,.audio_path=audio,.uidivmod=divide};
static void init(unsigned mode,unsigned wpm) {
 memset(&g,0,sizeof(g));A=&api;g.running=true;g.txReady=true;
 g.mode=MODE_PADDLE;g.port=PORT_USB;g.keyer=mode;g.wpm=wpm;
 g.breakInMs=1500;g.paBias=100;g.ticksPerMs=1000;g.lastVal=9999;
 simUs=edges=calls=eventsN=portSide=reverseInput=0;exitAt=9000000;trace[0]=0;
}
static void event(unsigned t,unsigned p) {events[eventsN++]=(struct event){t,p};}
static void run(const char *label,const char *expected) {
 runPaddle(paddles(key()));
 if(strcmp(trace,expected)){printf("FAIL %s mode%u: %s != %s\n",label,g.keyer,trace,expected);fflush(stdout);assert(0);}
 assert(!g.keyDown && !g.element);
 unsigned dot=(1200/g.wpm)*1000;
 for(unsigned i=0;i<edges;i++) if(exitAt==9000000) {
   unsigned mark=(trace[i]=='-'?3:1)*dot+3000;
   assert(offTime[i]-onTime[i]>=mark && offTime[i]-onTime[i]<=mark+400);
 }
 for(unsigned i=0;i+1<edges;i++) {
   unsigned expectedPeriod=(trace[i]=='-'?4:2)*dot;
   unsigned actual=onTime[i+1]-onTime[i];
   if(actual<expectedPeriod || actual>expectedPeriod+1200)printf("TIMING %s mode%u WPM%u actual%u expected%u\n",label,g.keyer,g.wpm,actual,expectedPeriod);
   assert(actual>=expectedPeriod && actual<=expectedPeriod+1200);
 }
 printf("PASS %-20s mode%u %2uWPM %-8s",label,g.keyer,g.wpm,trace);
 for(unsigned i=0;i<edges;i++)printf(" %u:%u",onTime[i],offTime[i]);
 puts("");
}
int main(void) {
 setvbuf(stdout,0,_IONBF,0);
 for(unsigned w=5;w<=60;w+= (w==5?13:42))for(unsigned m=0;m<3;m++) {
   unsigned d=(1200/w)*1000;
   init(m,w);event(0,1);event(5*d,0);run("DIT held","...");
   init(m,w);event(0,2);event(9*d,0);run("DAH held","---");
   init(m,w);event(0,3);event(7*d,0);run("squeeze held",m==2?".-.-":".-.");
   init(m,w);event(0,3);event(d/2,0);run("release in dit",m==2?".-":".");
   init(m,w);event(0,2);event(d/4,3);event(d,0);run("release in dah",m==2?"-.":"-");
   init(m,w);event(0,3);event(3*d/2,0);run("release in space",m==2?".-":".");
   init(m,w);event(0,1);event(d/2,0);event(5*d/4,3);event(7*d/4,0);run("space-only squeeze",".");
   init(m,w);event(0,1);event(d/2,0);run("single dit release",".");
   init(m,w);event(0,2);event(d,0);run("single dah release","-");
   init(m,w);event(0,1);event(d/4,2);event(d/2,0);run("opposite tap",m?".-":".");
   init(m,w);event(0,1);event(d/2,0);event(5*d/4,2);event(7*d/4,0);run("space opposite tap",m?".-":".");
   init(m,w);reverseInput=1;g.reverse=true;event(0,2);event(d,0);run("reversed DAH","-");
   init(m,w);g.port=PORT_SIDE;portSide=1;event(0,1);event(d/2,0);run("SIDE DIT",".");
   init(m,w);g.port=PORT_SIDE;portSide=1;event(0,2);event(9*d,0);run("SIDE DAH held","---");
   init(m,w);event(0,3);exitAt=d/2;run("EXIT during mark",".");assert(!g.running);
   init(m,w);event(0,3);exitAt=3*d/2;run("EXIT during space",".");assert(!g.running);
   init(m,w);event(0,1);event(d/2,0);event(d+1000,2);event(d+1200,0);run("one-sample glitch",".");
 }
 for(unsigned p=1;p<=3;p++) {
   init(0,60);event(0,p);event(140000,0);runPaddle(paddles(key()));
   unsigned count=edges,oldOn[32],oldOff[32];memcpy(oldOn,onTime,sizeof(oldOn));memcpy(oldOff,offTime,sizeof(oldOff));
   init(0,60);event(0,p);event(140000,0);legacyRun(paddles(key()));
   assert(count==edges && !memcmp(oldOn,onTime,count*sizeof(unsigned)) && !memcmp(oldOff,offTime,count*sizeof(unsigned)));
 }
 puts("PASS AUTO carrier timelines exactly match official runPaddle/keyerWait in host model");
 for(unsigned rev=0;rev<2;rev++) {
   init(0,18);g.reverse=rev;
   for(unsigned raw=0;raw<4;raw++) {eventsN=0;event(0,raw);assert(paddles(APP_KEY_INVALID)==(rev?((raw>>1)|((raw&1)<<1)):raw));}
   for(unsigned k=0;k<3;k++) {
     unsigned before=calls;handleUiKey(APP_KEY_8);assert(g.keyer==(k+1)%3 && calls==before);
   }
   uint32_t m=fakeModer=0xa1234567,p=fakePupdr=0x12345678,c=fakeRcc=0xffffffff;
   g.port=PORT_SIDE;g.portOpen=false;handleUiKey(APP_KEY_7);
   assert(g.port==PORT_USB && g.portOpen && !(fakeModer&USB_PINS) && !(fakeRcc&USBEN));
   handleUiKey(APP_KEY_7);assert(g.port==PORT_SIDE && !g.portOpen && fakeModer==m && fakePupdr==p && fakeRcc==c);
   handleUiKey(APP_KEY_1);assert(g.mode==MODE_STRAIGHT);
   handleUiKey(APP_KEY_5);assert(g.reverse!=rev);
   assert(calls==0);
 }
 puts("PASS keys 1/5/7/8, GPIO restoration, zero TX on settings; CW mode controls");
 return 0;
}
'''
p=out/'keyer-test.c'
p.write_text('#include <stdint.h>\nstatic uint32_t simUs,fakeModer,fakePupdr,fakeRcc;\nstatic uint32_t input(void);\n'+test+h,encoding='utf-8')
exe=out/'keyer-test.exe'
subprocess.run([os.environ.get('HOST_CC',shutil.which('gcc') or 'cc'),'-std=c11','-Wall','-Wextra',str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
b=(f/'CWKeyer.app').read_bytes()
magic,hdr,abi,api,n,crc,entry,flags,name,version,vma,caps,assetsN,assetsCRC=struct.unpack('<4sHBBIIHH16s16sIIHH',b[:64])
assert (magic,hdr,abi,api,entry,vma,caps)==(b'FAP1',1,1,2,0,0x20000280,0)
assert name.rstrip(b'\0')==b'CW Keyer' and version.rstrip(b'\0')==b'0.6'
assert n<=4096 and len(b)==64+n+assetsN and zlib.crc32(b[64:64+n])==crc
assert zlib.crc32(b[64+n:])&65535==assetsCRC
nm=os.environ.get('ARM_NM',shutil.which('arm-none-eabi-nm'))
if not nm: raise RuntimeError('Set ARM_NM or add arm-none-eabi-nm to PATH')
assert not subprocess.check_output([nm,'-u',str(f/'cwkeyer.elf')]).strip()
meta={'name':name.rstrip(b'\0').decode(),'version':version.rstrip(b'\0').decode(),'abi':abi,'api_min':api,'overlay':n,'free':4096-n,'assets':assetsN,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
(out/'artifacts.json').write_text(json.dumps(meta,indent=2))
print('PASS APP header, CRCs, size, API2, no undefined ARM symbols')
print(json.dumps(meta))
