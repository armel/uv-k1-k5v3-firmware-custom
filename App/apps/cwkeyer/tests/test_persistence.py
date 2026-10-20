# SPDX-License-Identifier: Apache-2.0
"""Exercise actual app and resident config/Flash functions with simulated NOR."""
import argparse,os,re,shutil,subprocess
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--output-dir',type=Path,required=True)
args=parser.parse_args()
r=Path(__file__).resolve().parents[4];out=args.output_dir.resolve()
resident=(r/'App/apps/app_overlay.c').read_text(encoding='utf-8')
driver=(r/'App/driver/py25q16.c').read_text(encoding='utf-8')
def fn(s,n):
    p=re.search(r'^(?:static )?[^\n]+\b'+n+r'\([^\n]*\)\n\{',s,re.M).start()
    return s[p:s.index('\n}',p)+2]
prefix=r'''
#define main waveform_main
#include "keyer-test.c"
#undef main
#include <stddef.h>
#include <setjmp.h>
'''
prefix+='#include "'+(r/'App/apps/app_config_proxy.h').as_posix()+'"\n'
prefix+=r'''
#define APP_CFG_OFFSET 0x40u
#define APP_CFG_OWNER 0x50u
#define SECTOR_SIZE 4096u
static uint8_t flash[0x200000],SectorCache[SECTOR_SIZE],official[SECTOR_SIZE];
static uint32_t SectorCacheAddr=0x1000000;
static unsigned eraseCalls,programCalls,stageCalls,cutErase,cutProgram;
static jmp_buf powerCut;
static struct {
 bool cfg_proxy;uint8_t run_slot,cfg_target_slot,cfg_proxy_len,cfg_len;
 uint32_t cfg_target_offset,cfg_target_magic;
 uint8_t cfg_buf[APP_CONFIG_PROXY_MAX];
} app_state;
static uint32_t BankMap(uint32_t addr){return addr;}
static void WaitWIP(void){}
static void ReadBufferRaw(uint32_t addr,void *buf,uint32_t n){assert(addr+n<=sizeof(flash));memcpy(buf,flash+addr,n);}
static void PY25Q16_ReadBuffer(uint32_t addr,void *buf,uint32_t n){ReadBufferRaw(addr,buf,n);}
static void SectorErase(uint32_t addr){
 assert(addr==APP_SLOT_BASE(1));++eraseCalls;memset(flash+addr,0xff,SECTOR_SIZE);
 if(cutErase)longjmp(powerCut,1);
}
static void SectorProgram(uint32_t addr,const uint8_t *buf,uint32_t n){
 assert(addr>=APP_SLOT_BASE(1) && addr+n<=APP_SLOT_BASE(1)+SECTOR_SIZE);++programCalls;
 for(uint32_t i=0;i<n;i++){
   assert((flash[addr+i]&buf[i])==buf[i]);flash[addr+i]&=buf[i];
   if(cutProgram && i+1==cutProgram)longjmp(powerCut,1);
 }
}
'''
prefix+=fn(driver,'PY25Q16_WriteBuffer')+'\n'+fn(resident,'app_cfg_load')+'\n'+fn(resident,'app_cfg_save')+'\n'
test=r'''
static void stage(const uint8_t *p,uint8_t n){++stageCalls;app_cfg_save(p,n);}
static uint32_t freq(void){return 43365000;}
static uint16_t readReg(uint8_t r){(void)r;return 0;}
static void af(uint8_t v){(void)v;}
static uint64_t signedDivide(int32_t n,int32_t d){return ((uint64_t)(uint32_t)(n%d)<<32)|(uint32_t)(n/d);}
static app_api_t persistApi;
static unsigned base=APP_REGION_BASE+APP_SLOT_STRIDE;
static void boot(void){
 init(0,18);persistApi=api;persistApi.cfg_load=app_cfg_load;persistApi.cfg_save=stage;
 persistApi.rx_freq=freq;persistApi.tx_freq=freq;persistApi.bk_read=readReg;
 persistApi.set_agc=audio;persistApi.set_af=af;persistApi.idivmod=signedDivide;
 A=&persistApi;g.txReady=false;
 memset(&app_state,0,sizeof(app_state));app_state.run_slot=1;
 SectorCacheAddr=0x1000000;stageCalls=0;loadConfig();
}
static void commit(void){
 if(app_state.cfg_len){
   PY25Q16_WriteBuffer(base+APP_CFG_OFFSET,app_state.cfg_buf,app_state.cfg_len,false);
   PY25Q16_WriteBuffer(base+APP_CFG_OWNER,flash+base+offsetof(app_header_t,name),APP_NAME_LEN,false);
 }
 assert(!memcmp(flash+APP_SLOT_BASE(0),official,sizeof(official)));
}
static void closeApp(void){saveConfig();commit();}
static void nameAt(unsigned slot,const char *s){
 memset(flash+APP_SLOT_BASE(slot)+offsetof(app_header_t,name),0,APP_NAME_LEN);
 strcpy((char*)flash+APP_SLOT_BASE(slot)+offsetof(app_header_t,name),s);
}
static void startFresh(void){
 memset(flash,0xff,sizeof(flash));nameAt(0,"CW Keyer");nameAt(1,"CW Keyer");
 memset(flash+APP_SLOT_BASE(0)+APP_CFG_OFFSET,0x36,16);
 memcpy(flash+APP_SLOT_BASE(0)+APP_CFG_OWNER,"CW Keyer\0\0\0\0\0\0\0\0",16);
 memcpy(official,flash+APP_SLOT_BASE(0),sizeof(official));eraseCalls=programCalls=0;
}
static void testPersistence(void){
 startFresh();boot();assert(g.keyer==0 && g.wpm==18 && !g.cfgDirty);
 closeApp();assert(!stageCalls && !eraseCalls && !programCalls);
 handleUiKey(APP_KEY_8);assert(g.keyer==1 && g.cfgDirty && !stageCalls && !calls);
 closeApp();assert(stageCalls==1 && flash[base+0x4c]==1);
 boot();assert(g.keyer==1);unsigned e=eraseCalls,p=programCalls;
 closeApp();assert(!stageCalls && eraseCalls==e && programCalls==p);
 handleUiKey(APP_KEY_8);assert(g.keyer==2);closeApp();boot();assert(g.keyer==2);
 handleUiKey(APP_KEY_8);assert(!g.keyer);closeApp();boot();assert(!g.keyer);
 puts("PASS first launch, no write at rest, AUTO/A/B round trips and RAM-reset reboot; official slot intact");
 // Simulate an old 0.5 record: only its exact 12-byte prefix exists.
 const uint8_t old[12]={0xcb,31,1,1,0xfa,0xff,0xee,2,0xf4,1,0,1};
 memcpy(flash+base+0x40,old,12);memset(flash+base+0x4c,0xff,4);
 boot();assert(!g.keyer && g.wpm==31 && g.mode==1 && g.reverse && g.port==1);
 assert(g.calSteps==-6 && g.rxPitchHz==750 && g.breakInMs==500 && !g.speaker);
 closeApp();assert(!stageCalls && flash[base+0x4c]==0xff);
 handleUiKey(APP_KEY_8);closeApp();assert(!memcmp(flash+base+0x40,old,12));
 // 0.5 writes only 12 bytes: extension survives, its other settings remain readable.
 uint8_t rollback[12];memcpy(rollback,flash+base+0x40,12);rollback[1]=32;
 PY25Q16_WriteBuffer(base+0x40,rollback,12,false);boot();assert(g.wpm==32 && g.keyer==1);
 puts("PASS legacy 0.5 extension absent, exact prefix preserved; 0.5 rollback record remains compatible");
 // The main branch's 0.2 calibration migration remains supported.
 memcpy(flash+base+0x40,old,12);flash[base+0x40]=0xca;
 memset(flash+base+0x4c,0xff,4);boot();
 assert(g.wpm==31 && g.port==PORT_SIDE && g.calSteps==0 && !g.keyer);
 memcpy(flash+base+0x40,old,12);
 // A renamed experimental slot has a different owner; no implicit migration.
 memset(flash+base+0x50,0,16);memcpy(flash+base+0x50,"CW Keyer DEV",12);
 boot();assert(!g.keyer && g.wpm==WPM_DEFAULT);closeApp();assert(!stageCalls);
 memset(flash+base+0x50,0,16);memcpy(flash+base+0x50,"CW Keyer",8);
 puts("PASS 0.2 calibration migration and development-name mismatch without writes");
 for(unsigned tag=0;tag<256;tag++){
   flash[base+0x4c]=1;flash[base+0x4d]=tag;boot();assert(g.keyer==(tag==0xd3?1:0));
   closeApp();assert(!stageCalls);
 }
 flash[base+0x4d]=0xd3;
 for(unsigned k=3;k<256;k++){
   flash[base+0x4c]=k;boot();assert(!g.keyer);closeApp();assert(!stageCalls);
 }
 flash[base+0x4c]=2;flash[base+0x40]=0;boot();assert(!g.keyer);closeApp();assert(!stageCalls);
 flash[base+0x40]=0xcb;flash[base+0x41]=0;boot();assert(!g.keyer && g.wpm==18);
 memcpy(flash+base+0x40,old,12);flash[base+0x4c]=2;
 memset(flash+base+0x50,'X',16);boot();assert(!g.keyer && g.wpm==18);
 memset(flash+base+0x50,0xff,16);boot();assert(g.keyer==2); // resident accepts legacy untagged owner
 puts("PASS 255 invalid tags, all 253 invalid mode bytes, invalid magic/WPM, mismatched owner and legacy untagged owner");
 // Touch each setting through its actual handler; mocks implement only radio API effects.
 boot();unsigned w=g.wpm;handleUiKey(APP_KEY_2);assert(g.wpm==w+1);
 handleUiKey(APP_KEY_F);handleUiKey(APP_KEY_2);assert(g.wpm==w && !g.fArm);
 unsigned tone=g.rxPitchHz;handleUiKey(APP_KEY_3);assert(g.rxPitchHz==tone+50);
 unsigned bk=g.breakInMs;handleUiKey(APP_KEY_4);assert(g.breakInMs==bk+50);
 int cal=g.calSteps;handleUiKey(APP_KEY_6);assert(g.calSteps==cal+1);
 bool sp=g.speaker;handleUiKey(APP_KEY_0);assert(g.speaker!=sp);
 closeApp();boot();assert(g.rxPitchHz==tone+50 && g.breakInMs==bk+50 && g.calSteps==cal+1 && g.speaker!=sp);
 assert(g.wpm==w);handleUiKey(APP_KEY_EXIT);assert(!g.running && !calls);
 puts("PASS settings 0/2/3/4/6/F and EXIT, stored alongside mode; 1/5/7/8 covered by waveform suite");
 // A changed-then-restored mode stages RAM once, but the actual NOR driver writes nothing.
 boot();e=eraseCalls;p=programCalls;handleUiKey(APP_KEY_8);handleUiKey(APP_KEY_8);handleUiKey(APP_KEY_8);
 closeApp();assert(stageCalls==1 && eraseCalls==e && programCalls==p);
 puts("PASS cycle back to original mode: resident NOR memcmp avoids erase/program");
 // Power loss before exit: only volatile settings were changed.
 boot();unsigned saved=g.keyer;handleUiKey(APP_KEY_8);assert(!stageCalls);
 boot();assert(g.keyer==saved);
 puts("PASS power loss before exit loses uncommitted mode, preserves stored value");
 // Power loss during non-atomic NOR RMW: characterize failure, do not claim recovery.
 uint8_t stable[SECTOR_SIZE];memcpy(stable,flash+base,sizeof(stable));
 for(unsigned cut=0;cut<5;cut++){
   memcpy(flash+base,stable,sizeof(stable));
   memcpy(flash+base+0x40,old,12);flash[base+0x4c]=0;flash[base+0x4d]=0;
   memcpy(flash+base+0x50,"CW Keyer\0\0\0\0\0\0\0\0",16);
   boot();handleUiKey(APP_KEY_8);saveConfig();
   cutErase=cut==0;cutProgram=cut==1?1:cut==2?64:cut==3?77:100;
   if(!setjmp(powerCut)){commit();assert(0);}
   cutErase=cutProgram=0;
   assert(!memcmp(flash+APP_SLOT_BASE(0),official,sizeof(official)));
   bool headerIntact=!memcmp(flash+base+offsetof(app_header_t,name),"CW Keyer\0\0\0\0\0\0\0\0",16);
   boot();printf("PASS interrupted write %u: restored mode %u, slot name intact=%u; official untouched\n",cut,g.keyer,headerIntact);
 }
}
int main(void){setvbuf(stdout,0,_IONBF,0);testPersistence();return 0;}
'''
p=out/'persistence-test.c';p.write_text(prefix+test,encoding='utf-8')
exe=out/'persistence-test.exe'
subprocess.run([os.environ.get('HOST_CC',shutil.which('gcc') or 'cc'),'-std=gnu11','-Wall','-Wextra',str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
assert (r/'App/apps/cwkeyer/cwkeyer_assets.bin').read_bytes()[:4]!=bytes.fromhex('43585031')
print('PASS local config path: no config-proxy descriptor')
