"""Exercise the production foreground, not a separate repetition model."""
from support import app,p
import subprocess,os,json
driver=r'''
static unsigned scenario,target,stopAt,lastOff,completeN,prepareN,prepareAt[8],starts[8],ends[8],startMarks[8];
static unsigned checks;static bool held,spam;
static void txProbe(void){quiet();assert(prepareN<8);prepareAt[prepareN++]=simUs;}
static void rfProbe(bool on){
 bool was=rf;
 if(on && markN==73*(quietN-1)){starts[quietN-1]=simUs;startMarks[quietN-1]=markN;}
 gate(on);
 if(was && !on && !(markN%73)){lastOff=simUs;ends[markN/73-1]=simUs;completeN=markN/73;}
}
static void endProbe(void){endtx();if(stopAt)exitAt=simUs+3500000;if(scenario==6 && endN==1){event(simUs+500000,1);event(simUs+600000,0);exitAt=simUs+4000000;}}
static uint8_t denyNext(void){return endN!=0;}
static uint8_t scriptKey(void){
 uint8_t normal=getkey();if(simUs>=exitAt)return normal;
 if(!stopAt){
  if((scenario==0 || scenario==1) && quietN==target && markN>=73*(target-1)+5)stopAt=simUs;
  if(scenario>=2 && scenario<=4 && completeN==target && simUs>=lastOff+(scenario==2?1000000:scenario==3?2999000:3000000))stopAt=simUs;
  if(scenario==5 && completeN==1 && simUs>=lastOff+1000000)stopAt=simUs;
  if(stopAt && scenario>=2)exitAt=simUs+3500000;
 }
 if(stopAt && simUs-stopAt<200000)return scenario==5?APP_KEY_EXIT:KEY_SIDE2;
 if(simUs>=100000 && (held?!stopAt:simUs<200000))return KEY_SIDE1;
 if(spam && !stopAt && simUs>=300000 && (simUs/100000)%2==0)return KEY_SIDE1;
 return APP_KEY_INVALID;
}
static void initRepeat(unsigned w,unsigned algo){
 setup(1,w);whichAlgo=algo;config_t cfg={CFG_MAGIC,w,1,0,CAL_DEFAULT,700,300,1,algo};memcpy(saved,&cfg,sizeof(cfg));useSaved=true;
 stopAt=lastOff=completeN=prepareN=0;memset(starts,0,sizeof(starts));memset(ends,0,sizeof(ends));held=spam=false;exitAt=140000000;
 api.get_key=scriptKey;api.tx_prepare=txProbe;api.tx_carrier=rfProbe;api.tx_end=endProbe;
}
int main(void){
 setbuf(stdout,NULL);
 const unsigned speeds[]={5,18,35,60};
 for(unsigned w=0;w<4;w++)for(unsigned algo=0;algo<3;algo++)for(unsigned sc=0;sc<6;sc++){
  initRepeat(speeds[w],algo);scenario=sc;target=sc<2?(w?3:2):1;held=sc==0;spam=sc==1;
  if(sc==4)cycleOrigin=0xC0000000u; /* deadline wrap across normal app execution */
  finish();assert(markN==73*target && quietN==target && endN==target && !q.repeat && !rf);
  for(unsigned i=1;i<target;i++){
   unsigned gap=starts[i]-ends[i-1];
   assert(gap>=3000000 && gap<=3006000);
   assert(prepareAt[i]-ends[i-1]>=3000000);
   printf("{\"wpm\":%u,\"algo\":%u,\"scenario\":%u,\"gap_us\":%u,\"prepare_gap_us\":%u}\n",speeds[w],algo,sc,gap,prepareAt[i]-ends[i-1]);checks++;
  }
  checks++;
 }
 /* SIDE2 alone never transmits; a new SIDE1 after a qualified release works. */
 for(unsigned algo=0;algo<3;algo++){
  setup(1,18);whichAlgo=algo;keyboard(100000,KEY_SIDE2);keyboard(200000,APP_KEY_INVALID);exitAt=1000000;finish();assert(!quietN && !markN);checks++;
  setup(1,18);whichAlgo=algo;keyboard(100000,KEY_SIDE1);keyboard(200000,APP_KEY_INVALID);keyboard(500000,KEY_SIDE2);keyboard(600000,APP_KEY_INVALID);
  keyboard(30000000,KEY_SIDE1);keyboard(30100000,APP_KEY_INVALID);keyboard(30500000,KEY_SIDE2);keyboard(30600000,APP_KEY_INVALID);exitAt=60000000;
  finish();assert(quietN==2 && markN==146 && endN==2);checks++;
 }
 /* PTT during the pause retains the current selection and stops the loop. */
 setup(1,18);keyboard(100000,APP_KEY_DOWN);keyboard(150000,APP_KEY_INVALID);
 keyboard(200000,KEY_SIDE1);keyboard(300000,APP_KEY_INVALID);keyboard(20000000,APP_KEY_PTT);keyboard(20100000,APP_KEY_INVALID);exitAt=50000000;
 finish();assert(q.selected==1 && q.message==1 && markN==73+89 && quietN==2 && !q.repeat);checks++;
 initRepeat(60,1);scenario=6;target=1;finish();assert(markN==73 && quietN==1 && !q.repeat);checks++;
 initRepeat(60,1);scenario=7;target=1;api.tx_state=denyNext;exitAt=14000000;finish();assert(markN==73 && quietN==1 && !q.repeat);checks++;
 setup(1,18);keyboard(100000,KEY_SIDE1);keyboard(200000,APP_KEY_INVALID);keyboard(500000,APP_KEY_EXIT);keyboard(600000,APP_KEY_INVALID);exitAt=1500000;finish();assert(markN>0 && markN<73 && !q.repeat);checks++;
 /* No tight repeat loop for a personalized empty/unsupported first byte. */
 unsigned firstByte=MSG_MODELS+MSG_HEADER;uint8_t original=assets[firstByte];
 const uint8_t empty[]={0,' ','!'};
 for(unsigned i=0;i<3;i++){assets[firstByte]=empty[i];setup(1,18);keyboard(100000,KEY_SIDE1);keyboard(200000,APP_KEY_INVALID);exitAt=1000000;finish();assert(!markN && !quietN && !q.repeat);checks++;}
 assets[firstByte]=original;
 printf("{\"repeat_checks\":%u,\"passed\":true}\n",checks);return 0;
}
'''
code=(app/'test/out/functional.c').read_text().replace('int main(void)','int functional_main(void)').replace('static const uint8_t assets[]={','static uint8_t assets[]={')
source=p/'repeat-host.c';exe=p/'repeat-host.exe';source.write_text(code+driver)
run=subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-Wall','-Wextra','-ftrapv',str(source),'-o',str(exe)],capture_output=True,text=True)
(p/'repeat-build.log').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stderr
run=subprocess.run([str(exe)],capture_output=True,text=True);(p/'repeat-run.log').write_text(run.stdout+run.stderr)
assert run.returncode==0,run.stdout[-1500:]+run.stderr
rows=[json.loads(l) for l in run.stdout.splitlines()];(p/'repeat-results.json').write_text(json.dumps(rows,indent=2));print(rows[-1])
gaps=[r['gap_us'] for r in rows if 'gap_us' in r];print('RF pause range us:',min(gaps),max(gaps))
