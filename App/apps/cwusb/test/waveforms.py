"""One host executable selects A then B through the production idle selector."""
from pathlib import Path
import ast,json,re,subprocess,os
a=Path(__file__).resolve().parent;r=a.parents[3];out=a/'out-waveforms';out.mkdir(exist_ok=True)
def literal(file,name):
    tree=ast.parse(file.read_text(encoding='utf-8'))
    return next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets))
def fn(s,n):
    p=re.search(r'^static [^\n]+\b'+n+r'\([^\n]*\)\n\{',s,re.M).start()
    return s[p:s.index('\n}',p)+2]
src=(r/'App/apps/cwkeyer/cwkeyer_app.c').read_text(encoding='utf-8')
api=(r/'App/apps/app_api.h').read_text(encoding='utf-8')
api=re.sub(r'_Static_assert\(.*?\);','/* ARM layout checked by original ARM build. */',api,flags=re.S)
apiPath=out/'host_api.h';apiPath.write_text(api,encoding='utf-8')
src=src.replace('#include "../app_api.h"','#include "'+apiPath.as_posix()+'"')
assets=r/'App/apps/cwusb/cwusb_assets.h'
src=src.replace('#include "cwkeyer_assets.h"','#include "'+assets.as_posix()+'"')
src='#include "'+assets.as_posix()+'"\n'+src
for macro,value in [('GPIOA_MODER','fakeModer'),('GPIOA_PUPDR','fakePupdr'),('GPIOA_IDR','input()'),('RCC_APBENR1','fakeRcc'),('SYST_LOAD','9999u'),('SYST_VAL','(9999u-simUs%10000u)')]:
    src=re.sub(r'^#define '+macro+r'\s+.*$','#define '+macro+' '+value,src,flags=re.M)
src=src.replace(fn(src,'irqSave'),'static uint32_t irqSave(void){return 0;}')
src=src.replace(fn(src,'irqRestore'),'static void irqRestore(uint32_t p){(void)p;}')
prefix='''#include <stdint.h>
static uint32_t simUs,fakeModer,fakePupdr,fakeRcc;
static uint32_t input(void);
static unsigned activeTestMode,requestAt,requestCount;
static void testRequest(void);
#define CW_KEYER_USB 1
#define CW_USB_IAMBIC 1
#define CURRENT_SIDE 0
#define HAVE_IAMBIC 0
#define SIM_ALGO activeTestMode
#define TARGET (activeTestMode==0?"FINAL_AUTO":activeTestMode==1?"FUSION_A":"FUSION_B")
'''
h='\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\nstruct event {unsigned t,p;};\nstatic struct event events[8];\nstatic unsigned eventCount,exitAt,reverseInput,nEdges,onTimes[32],offTimes[32];\nstatic char trace[33];\nstatic unsigned physical(void){\n unsigned p=0;\n for(unsigned i=0;i<eventCount;i++)if(simUs>=events[i].t)p=events[i].p;\n return reverseInput?((p>>1)|((p&1)<<1)):p;\n}\nstatic uint32_t input(void){\n unsigned p=physical();return 0xffffu&~((p&1?1u<<12:0)|(p&2?1u<<11:0));\n}\nstatic void noop(void){}\nstatic void delay(uint32_t ms){simUs+=ms*1000;assert(simUs<10000000);}\nstatic uint8_t key(void){\n simUs+=200;assert(simUs<10000000);\n if(simUs>=exitAt)return APP_KEY_EXIT;\n#if CURRENT_SIDE\n unsigned p=physical();return p&1?18u:p&2?17u:APP_KEY_INVALID;\n#else\n return APP_KEY_INVALID;\n#endif\n}\nstatic void carrier(bool on){\n if(on){assert(nEdges<32);onTimes[nEdges++]=simUs;}\n else{\n   assert(nEdges);offTimes[nEdges-1]=simUs;\n   unsigned d=(1200/g.wpm)*1000;\n   trace[nEdges-1]=simUs-onTimes[nEdges-1]>=2*d?\'-\':\'.\';trace[nEdges]=0;\n }\n}\nstatic uint8_t txState(void){return 0;}\nstatic void writeReg(uint8_t r,uint16_t v){(void)r;(void)v;}\nstatic void audio(bool b){(void)b;}\nstatic uint64_t divide(uint32_t n,uint32_t d){return ((uint64_t)(n%d)<<32)|(n/d);}\nstatic const app_api_t api={.get_key=key,.delay_ms=delay,.bk_write=writeReg,\n .tx_state=txState,.tx_carrier=carrier,.backlight_update=noop,.backlight_on=noop,\n .audio_path=audio,.uidivmod=divide};\nstatic void init(unsigned wpm){\n memset(&g,0,sizeof(g));A=&api;g.running=true;g.txReady=true;\n g.mode=MODE_PADDLE;g.wpm=wpm;g.breakInMs=1500;g.paBias=100;\n g.ticksPerMs=1000;g.lastVal=9999;\n#if HAVE_IAMBIC\n g.port=PORT_USB;g.keyer=SIM_ALGO;\n#endif\n simUs=eventCount=nEdges=reverseInput=0;exitAt=9000000;trace[0]=0;\n}\nstatic void event(unsigned t,unsigned p){events[eventCount++]=(struct event){t,p};}\nstatic void run(const char *label,const char *expected){\n runPaddle(paddles(key()));\n if(strcmp(trace,expected)){\n   fprintf(stderr,"FAIL %s %uWPM %s got %s expected %s\\n",TARGET,g.wpm,label,trace,expected);assert(0);\n }\n assert(!g.keyDown);\n unsigned d=(1200/g.wpm)*1000;\n for(unsigned i=0;i<nEdges && exitAt==9000000;i++){\n   unsigned mark=(trace[i]==\'-\'?3:1)*d+3000;\n   assert(offTimes[i]-onTimes[i]>=mark && offTimes[i]-onTimes[i]<=mark+400);\n }\n for(unsigned i=0;i+1<nEdges;i++){\n   unsigned period=(trace[i]==\'-\'?4:2)*d;\n   assert(onTimes[i+1]-onTimes[i]>=period && onTimes[i+1]-onTimes[i]<=period+1200);\n }\n printf("{\\"target\\":\\"%s\\",\\"wpm\\":%u,\\"case\\":\\"%s\\",\\"trace\\":\\"%s\\",\\"expected\\":\\"%s\\",\\"on_us\\":[",TARGET,g.wpm,label,trace,expected);\n for(unsigned i=0;i<nEdges;i++)printf("%s%u",i?",":"",onTimes[i]);\n printf("],\\"off_us\\":[");\n for(unsigned i=0;i<nEdges;i++)printf("%s%u",i?",":"",offTimes[i]);\n puts("]}");\n}\nint main(void){\n setvbuf(stdout,0,_IONBF,0);\n const unsigned speeds[]={5,18,60};\n const unsigned enabled=HAVE_IAMBIC || (!CURRENT_SIDE && CW_USB_IAMBIC);\n const unsigned b=enabled && SIM_ALGO==2,memory=enabled && SIM_ALGO!=0;\n for(unsigned i=0;i<3;i++){\n   unsigned w=speeds[i],d=(1200/w)*1000;\n   init(w);event(0,1);event(5*d,0);run("DIT held","...");\n   init(w);event(0,2);event(9*d,0);run("DAH held","---");\n   init(w);event(0,3);event(7*d,0);run("squeeze held",CURRENT_SIDE?"....":b?".-.-":".-.");\n   init(w);event(0,3);event(d/2,0);run("release during dit",b?".-":".");\n   init(w);event(0,2);event(d/4,3);event(d,0);run("release during dah",b?"-.":"-");\n   init(w);event(0,3);event(3*d/2,0);run("release during space",b?".-":".");\n   init(w);event(0,1);event(d/4,2);event(d/2,0);run("opposite tap during mark",memory?".-":".");\n   init(w);event(0,1);event(d/2,0);event(5*d/4,2);event(7*d/4,0);run("opposite tap during space",memory?".-":".");\n   init(w);event(0,1);event(d/2,0);event(5*d/4,3);event(7*d/4,0);run("squeeze only in space",".");\n   init(w);event(0,2);event(d,0);run("single dah released","-");\n   init(w);event(0,1);event(d/4,2);event(5*d/2,0);run("opposite still held at decision",".-");\n   init(w);reverseInput=1;g.reverse=true;event(0,2);event(d,0);run("reversed dah","-");\n   init(w);event(0,3);exitAt=d/2;run("EXIT during mark",".");assert(!g.running);\n }\n return 0;\n}\n'
h=h.replace('static void noop(void){}','''static void testRequest(void){
 if(!requestCount && simUs>=requestAt){
   const uint8_t before=g.iambic;
   const unsigned marks=nEdges;
   assert(g.txReady || (g.iambic&3));
   setIambicMode(activeTestMode==1?IAMBIC_B:IAMBIC_A);
   assert(g.iambic==before && nEdges==marks);requestCount++;
 }
}
static void noop(void){}''')
h=h.replace('simUs+=200;assert(simUs<10000000);','simUs+=200;assert(simUs<10000000);testRequest();')
h=h.replace('simUs+=ms*1000;assert(simUs<10000000);','simUs+=ms*1000;assert(simUs<10000000);testRequest();')
h=h.replace(' simUs=eventCount=nEdges=reverseInput=0;', ''' g.txReady=false;setIambicMode(activeTestMode==0?IAMBIC_AUTO:activeTestMode==1?IAMBIC_A:IAMBIC_B);g.txReady=true;g.armState=6;
 requestAt=0xffffffffu;requestCount=0;
 simUs=eventCount=nEdges=reverseInput=0;''')
h=h.replace(' assert(!g.keyDown);',' assert(!g.keyDown && !(g.iambic&15u));')
h=h.replace(' const unsigned enabled=', ' for(activeTestMode=0;activeTestMode<=2;activeTestMode++){\n const unsigned enabled=')
h=h.replace(' return 0;\n}', ''' }
 for(activeTestMode=0;activeTestMode<=2;activeTestMode++)for(unsigned i=0;i<3;i++){
   const unsigned speeds[]={5,18,60};unsigned w=speeds[i],d=(1200/w)*1000;
   for(unsigned window=0;window<3;window++){
     init(w);event(0,3);event(d/2,0);
     requestAt=window==0?1000:window==1?d/2:3*d/2;
     run(window==0?"change during ramp":window==1?"change during mark":"change during space",activeTestMode==2?".-":".");
     assert(requestCount==1);
     unsigned oldMode=g.iambic&48u;setIambicMode(oldMode==IAMBIC_B?IAMBIC_A:IAMBIC_B);
     assert((g.iambic&48u)==oldMode); /* still prepared after the sequence */
     g.txReady=false;setIambicMode(oldMode==IAMBIC_B?IAMBIC_A:IAMBIC_B);
     assert((g.iambic&48u)!=oldMode); /* foreground RX idle retry */
   }
 }
 memset(&g,0,sizeof(g));unsigned marks=nEdges;
 setIambicMode(IAMBIC_B);assert(g.iambic==IAMBIC_B && nEdges==marks);
 setIambicMode(IAMBIC_A);assert(g.iambic==IAMBIC_A && nEdges==marks);
 g.iambic=5;setIambicMode(IAMBIC_B);assert(g.iambic==5); /* active, even before txReady */
 return 0;
}''')
p=out/'fusion.c';p.write_text(prefix+src+h,encoding='utf-8');exe=out/'fusion.exe'
subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-Wall','-Wextra',str(p),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],text=True,capture_output=True)
if run.returncode: print(run.stdout[-1000:]);print(run.stderr);run.check_returncode()
rows=[json.loads(x) for x in run.stdout.splitlines()]
ref=json.loads((a/'waveform-reference.json').read_text())
for x in (x for x in rows if x['target']=='FINAL_AUTO'):
    candidates=[y for y in ref if y['target']=='PORT_AUTO' and y['wpm']==x['wpm'] and y['case']==x['case']]
    if not candidates: continue
    y=candidates[0]
    assert x['trace']==y['trace'] and x['on_us']==y['on_us'] and x['off_us']==y['off_us'],x
print('PASS AUTO: 39 historical traces/times identical, 9 busy-change cases; all three modes in SAME executable')
for mode in ['A','B']:
    expected={(x['wpm'],x['case']):x for x in ref if x['target']=='PORT_'+mode}
    actual=[x for x in rows if x['target']=='FUSION_'+mode]
    for x in actual:
        if (x['wpm'],x['case']) not in expected:continue
        before=expected[x['wpm'],x['case']]
        assert x['trace']==before['trace'] and x['on_us']==before['on_us'] and x['off_us']==before['off_us'],x
    print('PASS',mode,len(actual),'cases in SAME executable; 39 reference waveforms/times identical')
(out/'waveforms.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
print('PASS idle A/B selection, 27 active-change rejections and RX retries; no TX from selector')

