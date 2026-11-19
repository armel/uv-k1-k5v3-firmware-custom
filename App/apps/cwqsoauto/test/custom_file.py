"""Feed real web-generated nav16 assets into the production reader / RF harness."""
from pathlib import Path
import json,os,struct,subprocess,sys,zlib,re

p=Path(__file__).resolve().parent;out=p/'out'
assert len(sys.argv)>1,'Pass one or more web-generated nav16 APP paths'
code=(out/'functional.c').read_text().replace('int main(void)','int functional_main(void)')
code=code.replace('static const uint8_t assets[]={','static uint8_t assets[]={')
code+=r'''
int main(int argc,char **argv){
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    unsigned char header[64];assert(fread(header,1,64,file)==64);
    unsigned codeSize=header[8]|header[9]<<8|header[10]<<16|header[11]<<24;
    assert(!fseek(file,64+codeSize,SEEK_SET));
    assert(fread(assets,1,sizeof(assets),file)==sizeof(assets));assert(fgetc(file)==EOF);fclose(file);
    for(unsigned message=0;message<16;message++){
        setup(1,18);q.message=message;q.selected=message;strcpy(q.rst,"579");draw();
        printf("{\"message\":%u,\"preview\":\"%s\",\"title\":\"%s\"}\n",message,screen[3],screen[2]);
        char text[200];unsigned n=0;
        do{assert(n<sizeof(text));text[n]=nextCharacter();}while(text[n++]);
        printf("{\"message\":%u,\"text\":\"%s\"}\n",message,text);
        for(unsigned speed=5;speed<=60;speed+=speed==5?13:42){
            setup(1,speed);q.message=message;strcpy(q.rst,"579");startAuto();runAuto();
            assert(!rf && !g.txReady && !g.keyDown);
            printf("{\"message\":%u,\"speed\":%u,\"marks\":%u}\n",message,speed,markN);
        }
    }
    return 0;
}
'''
source,exe=out/'custom_file.c',out/'custom_file.exe';source.write_text(code)
subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-Wall','-Wextra',str(source),'-o',str(exe)],check=True)
patterns=['.-','-...','-.-.','-..','.','..-.','--.','....','..','.---','-.-','.-..','--','-.','---','.--.','--.-','.-.','...','-','..-','...-','.--','-..-','-.--','--..',
          '-----','.----','..---','...--','....-','.....','-....','--...','---..','----.','-..-.','..--..']
oracle=dict(zip('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?',patterns))
cases=0
for argument in sys.argv[1:]:
    file=Path(argument).resolve();binary=file.read_bytes();size=struct.unpack_from('<I',binary,8)[0]
    assert binary[20:36]==b'CW QSO Auto'.ljust(16,b'\0') and size==(p.parent/'cwqsoauto.bin').stat().st_size
    assets=binary[64+size:];assert len(assets)==(p.parent/'cwqsoauto_assets.bin').stat().st_size
    assert zlib.crc32(assets)&65535==struct.unpack_from('<H',binary,62)[0]
    texts=[assets[512+68*n+4:512+68*n+68].split(b'\0')[0].replace(b'\x02',b'579').decode('ascii') for n in range(16)]
    run=subprocess.run([str(exe),str(file)],capture_output=True,text=True)
    assert run.returncode==0,run.stdout+run.stderr
    for line in run.stdout.splitlines():
        row=json.loads(line);text=texts[row['message']]
        if 'preview' in row:
            raw=assets[512+68*row['message']+4:512+68*row['message']+68].split(b'\0')[0].replace(b'\x02',b'*').decode('ascii')
            expected=raw if len(raw)<=15 else raw[:14]+'>'
            namebase=int(re.search(r'#define T_MESSAGES (\d+)u',(p.parent/'cwqsoauto_assets.h').read_text())[1])
            at=namebase+row['message']*16
            title=assets[at:at+16].split(b'\0')[0].decode('ascii')
            assert row['preview']==expected and row['title']==title
        elif 'text' in row:assert row['text']==text
        else:assert row['marks']==sum(len(oracle[c]) for c in text if c!=' ')
        cases+=1
result=dict(files=len(sys.argv)-1,custom_file_cases=cases,passed=True)
(out/'custom-file.json').write_text(json.dumps(result,indent=2));print(result)
