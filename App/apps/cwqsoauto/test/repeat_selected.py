"""Actual foreground repeat control: frozen selection, keys, RF and deadlines."""
from pathlib import Path
import subprocess,os,json
p=Path(__file__).resolve().parent;out=p/'out'
code=(out/'functional.c').read_text().replace('int main(void)','int old_main(void)').replace('static const uint8_t assets[]={','static uint8_t assets[]={')
code+=r'''
static unsigned target,scenario,delaySeconds,launchTime,firstOff,secondOn,lastOff,stopping;
static unsigned counts[16],sessions[2],checks;
static unsigned nextSelection(void){return (target+1)%16;}
static void probeGate(bool on){
 unsigned second=scenario==3?nextSelection():target;
 if(on){assert(quietN>=1 && quietN<=2);assert(q.message==(quietN==1?target:second));sessions[quietN-1]=q.message;
  if(quietN==2 && !secondOn)secondOn=simUs;}
 gate(on);
 if(!on){
  if(quietN==1 && markN==counts[target])firstOff=simUs;
  if(quietN==2 && markN==counts[target]+counts[second])lastOff=simUs;
 }
}
static void probeEnd(void){endtx();if(endN==2)exitAt=simUs+(delaySeconds+1)*1000000;}
static uint8_t selectedKeys(void){
 uint8_t k=getkey();if(simUs>=exitAt)return k;
 if(simUs>=100000 && simUs<100000+target*100000){
  if((simUs-100000)%100000<50000)return APP_KEY_DOWN;
  return APP_KEY_INVALID;
 }
 if(simUs>=launchTime && simUs<launchTime+100000)return KEY_SIDE1;
 if(scenario==5 && simUs>=launchTime+350000 && simUs<launchTime+550000)return APP_KEY_EXIT;
 if(firstOff){
  unsigned t=simUs-firstOff;
  if(scenario==1){exitAt=firstOff+(delaySeconds+1)*1000000;if(t>=300000 && t<500000)return KEY_SIDE2;}
  if(scenario==2 || scenario==3){if(t>=200000 && t<300000)return APP_KEY_DOWN;}
  if(scenario==3){if(t>=600000 && t<700000)return APP_KEY_PTT;}
  if(scenario==0 && t>=400000 && t<500000)return KEY_SIDE1;
 }
 if((scenario==0 || scenario==2) && quietN==2 && markN>counts[target]+1){
  if(!stopping)stopping=simUs;
  if(simUs-stopping<150000)return KEY_SIDE2;
 }
 return APP_KEY_INVALID;
}
static void testRepeat(unsigned selection,unsigned seconds,unsigned algo,unsigned kind){
 setup(1,18);whichAlgo=algo;config_t cfg={CFG_MAGIC,18,1,0,CAL_DEFAULT,700,300,1,(seconds<<4)|algo};memcpy(saved,&cfg,12);useSaved=true;
 target=selection;scenario=kind;delaySeconds=seconds;launchTime=200000+selection*100000;
 firstOff=secondOn=lastOff=stopping=0;sessions[0]=sessions[1]=99;
 api.get_key=selectedKeys;api.tx_carrier=probeGate;api.tx_end=probeEnd;
 cycleOrigin=0xF0000000u;exitAt=kind>=4?launchTime+3000000:140000000;
 uint8_t *record=assets+MSG_MODELS+selection*MSG_STRIDE;
 uint8_t backup[68];memcpy(backup,record,68);
 if(kind==4)denyTx=1;
 if(kind==6){event(launchTime+350000,P_DIT);event(launchTime+500000,0);}
 if(kind==7)memset(record+4,0,64);
 if(kind==8)record[4]=1; /* Unsupported first payload byte: no repeat loop. */
 finish();
 if(kind==0 || kind==2 || kind==3){
  unsigned second=kind==3?nextSelection():selection;
  assert(quietN==2 && endN==2 && markN==counts[selection]+counts[second]);
  assert(sessions[0]==selection && sessions[1]==second && !q.repeat && lastOff);
  if(kind!=3){unsigned gap=secondOn-firstOff;assert(gap>=seconds*1000000 && gap<=seconds*1000000+6000);}
  if(kind==2 || kind==3)assert(q.selected==nextSelection());
 }else if(kind==1){assert(quietN==1 && endN==1 && markN==counts[selection] && !q.repeat);}
 else if(kind==4 || kind==7 || kind==8){if(quietN||markN||q.repeat)fprintf(stderr,"selection=%u kind=%u sessions=%u marks=%u repeat=%u\n",selection,kind,quietN,markN,q.repeat);assert(!quietN && !markN && !q.repeat);}
 else {assert(quietN==1 && markN>0 && markN<counts[selection] && !q.repeat);}
 assert(!g.txReady && !g.keyDown && !rf);
 memcpy(record,backup,68);checks++;
}
int main(void){
 for(unsigned m=0;m<16;m++){setup(1,18);q.message=m;startAuto();runAuto();counts[m]=markN;assert(counts[m]);}
 for(unsigned m=0;m<16;m++){
  for(unsigned d=1;d<=15;d+=d==1?4:10)for(unsigned algo=0;algo<3;algo++)testRepeat(m,d,algo,0);
  for(unsigned kind=1;kind<=8;kind++)testRepeat(m,1,1,kind);
 }
 printf("{\"repeat_selected_cases\":%u,\"selections\":16,\"delays\":[1,5,15],\"passed\":true}\n",checks);
}
'''
c=out/'repeat-selected.c';exe=out/'repeat-selected.exe';c.write_text(code)
r=subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-Wall','-Wextra','-ftrapv',str(c),'-o',str(exe)],capture_output=True,text=True)
(out/'repeat-selected-build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(exe)],capture_output=True,text=True)
(out/'repeat-selected-run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
result=json.loads(r.stdout);(out/'repeat-selected.json').write_text(json.dumps(result,indent=2));print(result)
