"""Bounded asset preview: initial/custom text, RST, boundaries and no TX mutation."""
from pathlib import Path
import json,os,subprocess

p=Path(__file__).resolve().parent;out=p/'out'
code=(out/'functional.c').read_text().replace('int main(void)','int functional_main(void)')
code=code.replace('static const uint8_t assets[]={','static uint8_t assets[]={')
code+=r'''
static unsigned previewCases,legendCalls,menuCalls;
static uint8_t original[sizeof(assets)];
static void previewTiny(const char *s,uint8_t x,uint8_t y,bool st,bool fill){
    assert(y==49 && !st && fill);
    assert(x==2 && !strcmp(s,"MENU"));menuCalls++;
    assert(x+4*strlen(s)<=128);
}
static void check(unsigned selection){
    q.selected=selection;api.print_tiny=previewTiny;legendCalls=menuCalls=0;
    __typeof__(q) savedQ;memcpy(&savedQ,&q,sizeof(q));
    draw();assert(!memcmp(&q,&savedQ,sizeof(q)) && !quietN && !markN);
    assert(!strcmp(screen[4],"RST 599") && menuCalls==1);
    {
        assert(!legendCalls);
        char title[16];memcpy(title,assets+T_MESSAGES+selection*T_MESSAGES_STRIDE,16);title[15]=0;
        assert(!strcmp(screen[2],title));
        const uint8_t *stored=assets+MSG_MODELS+selection*MSG_STRIDE+MSG_HEADER;
        unsigned length=0;while(length<64 && stored[length])length++;
        unsigned shown=length>15?15:length;char expected[16];
        for(unsigned i=0;i<shown;i++)expected[i]=stored[i]==RST_TOKEN?'*':stored[i];
        if(length>15)expected[14]='>';
        expected[shown]=0;
        assert(!strcmp(screen[3],expected) && strlen(screen[3])<=15);
    }
    previewCases++;
}
int main(void){
    memcpy(original,assets,sizeof(assets));
    for(unsigned selection=0;selection<16;selection++){
        setup(1,18);check(selection);
    }
    for(unsigned selection=0;selection<16;selection++){
        uint8_t *stored=assets+MSG_MODELS+selection*MSG_STRIDE+MSG_HEADER;
        for(unsigned length=0;length<=64;length++){
            memcpy(assets,original,sizeof(assets));memset(stored,0,64);memset(stored,'E',length);
            setup(1,18);q.message=14;q.cursor=37;q.callAt=2;q.active=true;
            check(selection);
            for(unsigned i=0;i<64;i++)assert(stored[i]==(i<length?'E':0));
        }
        for(unsigned token=0;token<64;token++){
            memcpy(assets,original,sizeof(assets));memset(stored,'E',64);stored[token]=RST_TOKEN;
            setup(1,18);check(selection);assert(stored[token]==RST_TOKEN);
        }
        memcpy(assets,original,sizeof(assets));memset(stored,0,64);
        memcpy(stored,"DE F5ABC \2 QTH",14);setup(1,18);check(selection);
        memcpy(assets,original,sizeof(assets));memset(stored,RST_TOKEN,64);setup(1,18);check(selection);
        assert(stored[0]==RST_TOKEN && stored[63]==RST_TOKEN);
    }
    printf("{\"preview_cases\":%u,\"max_visible_chars\":15}\n",previewCases);return 0;
}
'''
source,exe=out/'preview.c',out/'preview.exe';source.write_text(code)
subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-Wall','-Wextra',str(source),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True)
assert run.returncode==0,run.stdout+run.stderr
result=json.loads(run.stdout);(out/'preview.json').write_text(json.dumps(result,indent=2));print(result)
