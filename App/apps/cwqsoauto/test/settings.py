"""Exercise production drawing, mode changes and equal-width settings cells."""
from pathlib import Path
import json
import os
import subprocess

p = Path(__file__).resolve().parent
out = p / "out"
code = (out / "functional.c").read_text().replace("int main(void)", "int functional_main(void)")
code += r'''
static unsigned smallCalls,cellCalls,settingsCases,modeCalls,cellMask;
static char modeText[16];
static void checkedTiny(const char *s,uint8_t x,uint8_t y,bool st,bool fill){
    (void)s;(void)x;(void)y;(void)st;(void)fill;
    assert(!"SETTINGS uses highlighted smallest-font cells");
}
static void checkedInverse(const char *s,uint8_t x,uint8_t line,bool st,bool fill,uint8_t end){
    if(!st){
        assert(fill && x+strlen(s)*4<=end);
        if(line==2){
            assert(x==2 && end==59 && !strcmp(s,"UP/DN FREQ F:-"));smallCalls++;
        }else if(line==3){
            assert(x==2 && end==3+strlen(s)*4);
            snprintf(modeText,sizeof(modeText),"%s",s);modeCalls++;
        }else{
            assert(line>=4 && line<=6 && (x==2 || x==66) && end==x+59);
            unsigned bit=1u<<((line-4)*2+(x==66));assert(!(cellMask&bit));
            cellMask|=bit;cellCalls++;
        }
    }
    inverse(s,x,line,st,fill,end);
}
static void checkSettings(void){
    const unsigned mode=g.mode?1+(g.iambic>>4):0;
    const char *names[]={"","8 AUTO","8 IAMB A","8 IAMB B"};
    smallCalls=cellCalls=modeCalls=cellMask=0;modeText[0]=0;
    api.print_tiny=checkedTiny;api.print_inverse=checkedInverse;
    draw();assert(!strcmp(modeText,names[mode]) && !screen[3][0]);
    assert(modeCalls==(unsigned)(g.mode==MODE_PADDLE));
    assert(smallCalls==1 && cellCalls==6 && cellMask==63 && !quietN && !markN);
    settingsCases++;
}
int main(void){
    for(unsigned mode=0;mode<2;mode++)for(unsigned algo=0;algo<3;algo++)
    for(unsigned speaker=0;speaker<2;speaker++)for(unsigned w=5;w<=60;w++)
    for(int cal=-50;cal<=50;cal+=50){
        setup(mode,w);q.settings=true;g.iambic=algo<<4;g.speaker=speaker;
        g.calSteps=cal;g.rxPitchHz=900;g.breakInMs=1500;g.reverse=true;
        checkSettings();
    }
    setup(1,18);q.settings=true;
    for(unsigned i=0;i<9;i++){
        checkSettings();unsigned before=g.iambic;
        handleUiKeyDirection(APP_KEY_8,0);
        assert(g.iambic==((before+16)%48));
    }
    checkSettings();bool speaker=g.speaker;
    handleUiKeyDirection(APP_KEY_0,0);assert(g.speaker!=speaker);checkSettings();
    handleUiKeyDirection(APP_KEY_1,0);assert(g.mode==MODE_STRAIGHT);checkSettings();
    handleUiKeyDirection(APP_KEY_1,0);assert(g.mode==MODE_PADDLE);checkSettings();
    printf("{\"settings_cases\":%u}\n",settingsCases);
    return 0;
}
'''
source, exe = out / "settings.c", out / "settings.exe"
source.write_text(code)
subprocess.run([os.environ.get("HOST_CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                str(source), "-o", str(exe)], check=True)
run = subprocess.run([str(exe)], capture_output=True, text=True)
assert run.returncode == 0, run.stdout + run.stderr
result = json.loads(run.stdout)
(out / "settings.json").write_text(json.dumps(result, indent=2))
print(result)
