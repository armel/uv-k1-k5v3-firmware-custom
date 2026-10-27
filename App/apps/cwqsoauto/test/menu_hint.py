"""Check the production MENU hint position and restriction to the QSO screen."""
from pathlib import Path
import json
import os
import subprocess

p = Path(__file__).resolve().parent
out = p / "out"
code = (out / "functional.c").read_text().replace("int main(void)", "int functional_main(void)")
code += r'''
static unsigned hintCalls,menuCases;
static void checkedHint(const char *s,uint8_t x,uint8_t y,bool st,bool fill){
    if(x==2){assert(q.selected>=3 && !strcmp(s,"*=RST >=SUITE") && y==49 && !st && fill);return;}
    assert(!strcmp(s,"MENU") && x==110 && y==49 && !st && fill);
    assert(x+strlen(s)*4<=128 && y+6<=56);hintCalls++;
}
int main(void){
    const char *reports[]={"000","599","999"};
    for(unsigned selection=0;selection<16;selection++)for(unsigned state=0;state<17;state++)
    for(unsigned rst=0;rst<3;rst++){
        setup(1,18);api.print_tiny=checkedHint;q.selected=selection;
        strcpy(q.rst,reports[rst]);q.active=state!=0;q.message=state?state-1:0;
        hintCalls=0;draw();assert(hintCalls==1 && !quietN && !markN);
        if(selection<3)assert(!screen[3][0]);
        assert(!screen[5][0] && !screen[6][0]);menuCases++;
    }
    for(unsigned mode=0;mode<2;mode++)for(unsigned algo=0;algo<3;algo++){
        setup(mode,18);api.print_tiny=checkedHint;q.settings=true;g.iambic=algo<<4;
        hintCalls=0;draw();assert(!hintCalls && !quietN && !markN);menuCases++;
    }
    printf("{\"menu_hint_cases\":%u}\n",menuCases);return 0;
}
'''
source, exe = out / "menu_hint.c", out / "menu_hint.exe"
source.write_text(code)
subprocess.run([os.environ.get("HOST_CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                str(source), "-o", str(exe)], check=True)
run = subprocess.run([str(exe)], capture_output=True, text=True)
assert run.returncode == 0, run.stdout + run.stderr
result = json.loads(run.stdout)
(out / "menu_hint.json").write_text(json.dumps(result, indent=2))
print(result)
