"""All 16 selections, circular input, real PTT/SIDE foreground and Morse oracle."""
from pathlib import Path
import json, os, subprocess

p=Path(__file__).resolve().parent
out=p/'out'
code=(out/'functional.c').read_text().replace('int main(void)','int functional_main(void)')
code+=r'''
static unsigned navCases;
static unsigned countMessage(unsigned message){
    setup(1,18);q.message=message;startAuto();runAuto();return markN;
}
static void navigate(unsigned selection){
    for(unsigned i=0;i<selection;i++){
        keyboard(200000+i*100000,APP_KEY_DOWN);
        keyboard(250000+i*100000,APP_KEY_INVALID);
    }
}
int main(void){
    for(int direction=-1;direction<=1;direction+=2){
        setup(1,18);
        for(unsigned n=0;n<64;n++){
            handleUiKeyDirection(APP_KEY_DOWN,direction);
            assert(q.selected==(n+1)%16 && !quietN && !q.settings);navCases++;
        }
        for(unsigned n=0;n<64;n++){
            handleUiKeyDirection(APP_KEY_UP,direction);
            assert(q.selected==(63-n)%16 && !quietN && !q.settings);navCases++;
        }
    }
    for(unsigned selection=0;selection<16;selection++){
        unsigned expectedMarks=countMessage(selection);
        setup(1,18);navigate(selection);
        unsigned time=200000+selection*100000;
        keyboard(time,APP_KEY_PTT);keyboard(time+40000000,APP_KEY_INVALID);
        exitAt=time+41000000;finish();
        assert(q.selected==selection && q.message==selection && markN==expectedMarks);
        assert(quietN==1 && endN==1 && !rf);navCases++;
        setup(1,18);q.selected=selection;draw();
        char label[20];sprintf(label,"%02u ",selection+1);
        assert(!strncmp(screen[2],label,strlen(label)) && !strcmp(screen[4],"RST 599"));
        assert(screen[3][0]);
        assert(!screen[5][0] && !screen[6][0] && !quietN);navCases++;
        for(unsigned speed=5;speed<=60;speed++){
            setup(1,speed);q.message=selection;startAuto();runAuto();
            assert(markN==expectedMarks && endN==1 && !rf && !g.txReady);
            printf("{\"message\":%u,\"speed\":%u,\"edges\":[",selection,speed);
            for(unsigned n=0;n<nEdges;n++)printf("%s%u",n?",":"",edgeTime[n]);
            puts("]}");navCases++;
        }
    }
    printf("{\"navigation_cases\":%u}\n",navCases);return 0;
}
'''
source,exe=out/'navigation16.c',out/'navigation16.exe'
source.write_text(code)
subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-Wall','-Wextra',str(source),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True)
assert run.returncode==0,run.stdout[-3000:]+run.stderr
rows=[json.loads(line) for line in run.stdout.splitlines()]
alphabet='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?'
patterns=['.-','-...','-.-.','-..','.','..-.','--.','....','..','.---','-.-','.-..','--','-.','---','.--.','--.-','.-.','...','-','..-','...-','.--','-..-','-.--','--..',
          '-----','.----','..---','...--','....-','.....','-....','--...','---..','----.','-..-.','..--..']
oracle=dict(zip(alphabet,patterns))
messages=['CQ CQ CQ DE F4XXX F4XXX K','DE F4XXX UR 599 599 NAME SEB HW?','73 TU E E']+[f'MSG {i}' for i in range(4,15)]+['AGN?','?']
for row in rows[:-1]:
    text=messages[row['message']];expected=[]
    for i,c in enumerate(text):
        if c==' ':continue
        for j,element in enumerate(oracle[c]):
            gap=1 if j+1<len(oracle[c]) else 7 if i+1<len(text) and text[i+1]==' ' else 3
            expected.append((3 if element=='-' else 1,gap))
    edges=row['edges'];dot=(1200//row['speed'])*1000
    assert len(edges)==2*len(expected)
    for i,(mark,gap) in enumerate(expected):
        assert mark*dot+3000<=edges[2*i+1]-edges[2*i]<=mark*dot+4400
        if i+1<len(expected):assert gap*dot-3000<=edges[2*i+2]-edges[2*i+1]<=gap*dot-1600
result={**rows[-1], 'message_speed_oracles':len(rows)-1, 'passed':True}
(out/'navigation16.json').write_text(json.dumps(result,indent=2))
print(result)
