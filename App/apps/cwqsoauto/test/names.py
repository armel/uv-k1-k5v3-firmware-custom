"""Exercise production draw() with all fixed slots, bounded and invalid names."""
from pathlib import Path
import subprocess,json,os
p=Path(__file__).resolve().parent;out=p/'out'
code=(out/'functional.c').read_text().replace('int main(void)','int previous_main(void)').replace('static const uint8_t assets[]={','static uint8_t assets[]={')
code+=r'''
static unsigned checks;
static void menuOnly(const char*s,uint8_t x,uint8_t y,bool st,bool fill){
 assert(!strcmp(s,"MENU") && x==2 && y==49 && !st && fill);
}
int main(void){
 for(unsigned n=0;n<16;n++) {
  uint8_t *slot=assets+T_MESSAGES+n*T_MESSAGES_STRIDE;
  for(unsigned length=0;length<=12;length++){
   memset(slot+3,0,13);memset(slot+3,'A',length);
   setup(1,18);q.selected=n;api.print_tiny=menuOnly;draw();
   assert(strlen(screen[2])==3+length && !memcmp(screen[2],slot,3+length));
   assert(!strcmp(screen[4],"RST 599") && !quietN && !markN);checks++;
  }
  for(unsigned value=0;value<256;value++){
   memset(slot+3,0,13);slot[3]=value;setup(1,18);q.selected=n;draw();
   assert(screen[2][3]==(!value?0:value>=32&&value<=126?value:'?'));checks++;
  }
  /* Missing terminator is bounded to 15 glyphs. Reader cannot overrun. */
  memset(slot+3,'Z',13);setup(1,18);q.selected=n;draw();assert(strlen(screen[2])==15);checks++;
 }
 printf("{\"name_display_cases\":%u,\"passed\":true}\n",checks);
}
'''
c=out/'names.c';exe=out/'names.exe';c.write_text(code)
r=subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-O1','-ftrapv',str(c),'-o',str(exe)],capture_output=True,text=True);assert r.returncode==0,r.stderr
r=subprocess.run([str(exe)],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
result=json.loads(r.stdout);(out/'names.json').write_text(json.dumps(result,indent=2));print(result)
