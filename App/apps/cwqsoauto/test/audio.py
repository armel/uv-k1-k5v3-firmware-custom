"""Observed HP/Tone1/RF commands; does not model analog settling or clicks."""
from support import app,p
import os,subprocess,json
driver=r'''
enum {HP_OFF=1,HP_ON,AF_RX,AF_TONE,TONE_OFF,TONE_ON,RF_OFF,RF_ON};
static unsigned logN,logs[128][2],writeN,writes[256][2];static bool hp,tone;static unsigned route;
static void record(unsigned k){assert(logN<128);logs[logN][0]=k;logs[logN++][1]=simUs;}
static void hpProbe(bool on){hp=on;record(on?HP_ON:HP_OFF);}
static void afProbe(uint8_t mode){if(mode==AF_ALAM){assert(!hp && !tone);record(AF_TONE);}if(mode==AF_USB)record(AF_RX);route=mode;}
static void regProbe(uint8_t r,uint16_t v){writereg(r,v);assert(writeN<256);writes[writeN][0]=r;writes[writeN++][1]=v;if(r==REG_TONE_FREQ){tone=v!=0;record(tone?TONE_ON:TONE_OFF);}}
static void gateProbe(bool on){if(on){assert(tone && route==AF_ALAM);record(RF_ON);}else if(rf)record(RF_OFF);gate(on);}
static unsigned timeOf(unsigned k){for(unsigned i=0;i<logN;i++)if(logs[i][0]==k)return logs[i][1];return 0;}
int main(void){
 for(unsigned paddle=1;paddle<=2;paddle++)for(unsigned speaker=0;speaker<2;speaker++){
  setup(1,18);g.iambic=IAMBIC_A;g.speaker=speaker;g.breakInMs=1500;logN=writeN=route=0;hp=tone=false;
  api.audio_path=hpProbe;api.set_af=afProbe;api.bk_write=regProbe;api.tx_carrier=gateProbe;
  configureCwRx(true);assert(hp==(bool)speaker && route==AF_USB);logN=writeN=0;
  runPaddle(paddle);assert(markN==1 && !rf && !tone);unsigned first=edgeTime[0],duration=edgeTime[1]-edgeTime[0];
#if EARLY_HP
  if(speaker){assert(hp);assert(timeOf(HP_ON)==timeOf(AF_TONE));assert(timeOf(TONE_ON)-timeOf(HP_ON)==2000);}
  else assert(!hp);
#else
  if(speaker)assert(timeOf(HP_ON)>timeOf(TONE_ON));
#endif
  printf("{\"paddle\":%u,\"speaker\":%u,\"first_rf_us\":%u,\"rf_width_us\":%u,\"events\":[",paddle,speaker,first,duration);
  for(unsigned i=0;i<logN;i++)printf("%s[%u,%u]",i?",":"",logs[i][0],logs[i][1]);printf("],\"writes\":[");
  for(unsigned i=0;i<writeN;i++)printf("%s[%u,%u]",i?",":"",writes[i][0],writes[i][1]);printf("]}");puts("");
  unsigned before=simUs;logN=0;runPaddle(paddle);assert(markN==2 && quietN==1 && edgeTime[2]==before);
  for(unsigned i=0;i<logN;i++)assert(logs[i][0]!=HP_ON && logs[i][0]!=AF_TONE);
  returnToRx();assert(!g.txReady && !g.ampMs && !rf && !tone && route==AF_USB && hp==(bool)speaker);
 }
 return 0;
}
'''

code=(app/'test/out/functional.c').read_text().replace('int main(void)','int functional_main(void)')
src=p/'audio.c';exe=p/'audio.exe';src.write_text('#define EARLY_HP 1\n'+code+driver)
build=subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1',str(src),'-o',str(exe)],capture_output=True,text=True)
assert build.returncode==0,build.stderr
run=subprocess.run([str(exe)],capture_output=True,text=True);assert run.returncode==0,run.stderr
rows=[json.loads(line) for line in run.stdout.splitlines()];assert len(rows)==4
(p/'audio.json').write_text(json.dumps(rows,indent=2))
print('PASS 4 audio command traces: silent Tone1/HP before PLL wait, RF widths, next element, RX restore')
