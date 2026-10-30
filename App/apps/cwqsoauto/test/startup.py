"""Production foreground tests: automatic entry release, no TIP hold gesture."""
from pathlib import Path
import json
import os
import subprocess

p = Path(__file__).resolve().parent
out = p / "out"
code = (out / "functional.c").read_text().replace("int main(void)", "int functional_main(void)")
code += r'''
static unsigned startupCases;
int main(void){
    const unsigned txKeys[]={APP_KEY_INVALID,APP_KEY_PTT,KEY_SIDE1,KEY_SIDE2};
    for(unsigned mode=0;mode<2;mode++)for(unsigned algo=0;algo<3;algo++)
    for(unsigned raw=0;raw<4;raw++)for(unsigned key=0;key<4;key++){
        /* Held indefinitely, or released after five seconds: neither is a request. */
        for(unsigned release=0;release<2;release++){
            setup(mode,18);whichAlgo=algo;event(0,raw);
            if(key)keyboard(0,txKeys[key]);
            if(release){event(5000000,0);keyboard(5000000,APP_KEY_INVALID);}
            exitAt=5500000;finish();
            assert(!quietN && !markN && !rf);startupCases++;
        }
        /* Only a new press after a qualified release starts the CQ. */
        setup(mode,18);whichAlgo=algo;event(0,raw);event(500000,0);
        if(key)keyboard(0,txKeys[key]);
        keyboard(500000,APP_KEY_INVALID);
        keyboard(600000,APP_KEY_PTT);keyboard(700000,APP_KEY_INVALID);
        exitAt=40000000;finish();
        assert(markN==73 && quietN==1 && endN==1 && firstMark>=600000 && firstMark<=605000);
        startupCases++;
    }
    /* First normal command: 100 ms after entry, no five-second wait. */
    for(unsigned mode=0;mode<2;mode++)for(unsigned key=1;key<4;key++){
        setup(mode,18);whichAlgo=0;
        keyboard(100000,txKeys[key]);keyboard(200000,APP_KEY_INVALID);
        if(key==2){keyboard(300000,KEY_SIDE2);keyboard(400000,APP_KEY_INVALID);}
        exitAt=40000000;finish();
        assert(markN==(key==3?0u:73u) && quietN==(key!=3) && endN==(key!=3));
        if(key!=3)assert(firstMark>=100000 && firstMark<=105000);else assert(!firstMark);startupCases++;
    }
    /* TIP/RING held on entry cannot become a manual mark on release. */
    for(unsigned mode=0;mode<2;mode++)for(unsigned algo=0;algo<3;algo++)
    for(unsigned raw=1;raw<4;raw++){
        setup(mode,18);whichAlgo=algo;event(0,raw);event(500000,0);
        event(600000,1);event(700000,0);exitAt=1500000;finish();
        assert(quietN==1 && markN && firstMark>=600000 && endN==1 && !rf);
        startupCases++;
    }
    /* In STRAIGHT, a mono plug's grounded RING is not a keying input. */
    setup(0,18);event(0,2);event(600000,3);event(700000,2);exitAt=1500000;finish();
    assert(markN==1 && quietN==1 && firstMark>=600000 && endN==1);startupCases++;
    /* Five ms idle does not qualify a held PTT; a later fresh edge does. */
    setup(1,18);keyboard(0,APP_KEY_PTT);keyboard(100000,APP_KEY_INVALID);
    keyboard(105000,APP_KEY_PTT);keyboard(200000,APP_KEY_INVALID);
    keyboard(300000,APP_KEY_PTT);keyboard(400000,APP_KEY_INVALID);
    exitAt=40000000;finish();assert(markN==73 && firstMark>=300000 && quietN==1);startupCases++;
    /* Wrapping the 48 MHz cycle counter does not bypass release qualification. */
    setup(1,18);cycleOrigin=0xffff0000u;
    keyboard(100000,APP_KEY_PTT);keyboard(200000,APP_KEY_INVALID);exitAt=40000000;
    finish();assert(markN==73 && firstMark>=100000 && quietN==1);startupCases++;
    /* API guard executes before accessing GPIO, radio or unavailable callbacks. */
    setup(1,18);api.api_size=367;app_main(&api);
    assert(!simUs && !quietN && !markN && !g.portOpen && !moder && !rcc);startupCases++;
    /* A persisted filtered closure cannot cross a new application entry. */
    setup(0,18);g.padRaw=g.padStable=3;g.padPolls=255;exitAt=600000;
    finish();assert(!markN && !quietN);startupCases++;
    printf("{\"startup_cases\":%u}\n",startupCases);
    return 0;
}
'''
source, exe = out / "startup.c", out / "startup.exe"
source.write_text(code)
subprocess.run([os.environ.get("HOST_CC", "gcc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                str(source), "-o", str(exe)], check=True)
run = subprocess.run([str(exe)], capture_output=True, text=True)
assert run.returncode == 0, run.stdout + run.stderr
result = json.loads(run.stdout)
(out / "startup.json").write_text(json.dumps(result, indent=2))
print(result)
