
static unsigned cases;
static void setup(unsigned mode,unsigned wpm){
 useSaved=false;init(mode);A=&api;g.mode=mode;g.wpm=wpm;g.running=true;
 g.rxPitchHz=700;g.breakInMs=250;g.speaker=true;g.ticksPerMs=48000;strcpy(q.rst,"599");
}
static void keyboard(unsigned t,uint8_t key){assert(nKeys<32);keys[nKeys].t=t;keys[nKeys++].key=key;}
static void start(void){startAuto();}
static void runAuto(void){unsigned limit=simUs+100000000;while(q.active){assert(simUs<limit);simUs+=200;serviceAuto();serviceOneMs();}}
static void finish(void){app_main(&api);assert(!g.txReady && !g.keyDown && !g.portOpen && !rf);}
static void idleEntry(void){ /* Startup qualification is exercised by app_main. */ }

static unsigned checks;
static void config(unsigned mode,unsigned algo,unsigned seconds){
 setup(mode,18);whichAlgo=algo;config_t c={CFG_MAGIC,18,mode,0,CAL_DEFAULT,700,300,1,(seconds<<4)|algo};memcpy(saved,&c,sizeof(c));useSaved=true;
}
static char repeatLabel[32];
static void box(const char*s,uint8_t x,uint8_t y,bool st,bool fill,uint8_t end){inverse(s,x,y,st,fill,end);if(!st && y==2 && x==2){strcpy(repeatLabel,s);assert(end==59 && x+4*strlen(s)<=end);}}
static void menu(void){keyboard(100000,APP_KEY_MENU);keyboard(200000,APP_KEY_INVALID);}
static uint8_t unblock(void){uint8_t k=getkey();if(simUs>=600000)denyTx=0;return k;}
static unsigned seconds,stopAt,firstEnd,secondStart,kind;
static void gateRepeat(bool on){bool was=rf;if(on && quietN==2 && markN==73)secondStart=simUs;gate(on);if(was&&!on&&markN==73)firstEnd=simUs;}
static void finishRepeat(void){endtx();if(stopAt)exitAt=simUs+(seconds+1)*1000000;}
static uint8_t repeatKey(void){
 uint8_t k=getkey();if(simUs>=exitAt)return k;
 if(kind==0){if(!stopAt&&quietN==2&&markN>=78)stopAt=simUs;if(stopAt&&simUs-stopAt<200000)return KEY_SIDE2;}
 else if(firstEnd){
  unsigned t=simUs-firstEnd;
  if(t>=500000&&t<600000)return APP_KEY_MENU;
  unsigned pressed=kind==1?999000:1200000;
  if(t>=pressed&&t<1600000)return APP_KEY_PTT;
  if(t>=4000000)exitAt=simUs;
 }
 if(simUs>=100000&&simUs<200000)return KEY_SIDE1;return APP_KEY_INVALID;
}
int main(void){
 setbuf(stdout,NULL);
 /* All packed bytes, old valid bytes, bounds, and restart round-trip. */
 for(unsigned raw=0;raw<256;raw++){
  config(1,0,0);saved[11]=raw;loadConfig();
  unsigned want=raw>>4;if(!want)want=5;unsigned algo=(raw&15)<=2?raw&15:0;
  assert(g.repeatSeconds==want && g.iambic==(algo<<4));saveConfig();uint8_t persisted[12];memcpy(persisted,saved,12);
  config(1,0,0);memcpy(saved,persisted,12);loadConfig();assert(g.repeatSeconds==want && g.iambic==(algo<<4));checks++;
 }
 config(1,2,0);saved[0]=0;loadConfig();assert(g.repeatSeconds==5 && g.iambic==IAMBIC_AUTO);checks++;
 for(unsigned mode=0;mode<2;mode++)for(unsigned algo=0;algo<3;algo++){
  config(mode,algo,5);loadConfig();q.settings=true;api.print_inverse=box;
  for(unsigned i=0;i<30;i++)handleUiKeyDirection(APP_KEY_UP,1);assert(g.repeatSeconds==15);draw();assert(!strcmp(repeatLabel,"REPEAT: 15s"));
  for(unsigned i=0;i<30;i++)handleUiKeyDirection(APP_KEY_DOWN,-1);assert(g.repeatSeconds==1);draw();assert(!strcmp(repeatLabel,"REPEAT: 1s"));assert(g.mode==mode && g.iambic==(algo<<4));checks++;
  config(mode,algo,5);menu();keyboard(400000,APP_KEY_PTT);keyboard(800000,APP_KEY_INVALID);exitAt=1500000;finish();
  assert(markN==1 && quietN==1 && endN==1 && edgeTime[0]>=400000 && edgeTime[0]<406000 && edgeTime[1]>=800000 && edgeTime[1]<807000);checks++;
  config(mode,algo,5);denyTx=1;menu();keyboard(400000,APP_KEY_PTT);keyboard(800000,APP_KEY_INVALID);exitAt=1500000;finish();assert(!quietN && !markN);checks++;
  config(mode,algo,5);denyTx=1;api.get_key=unblock;menu();keyboard(400000,APP_KEY_PTT);keyboard(800000,APP_KEY_INVALID);keyboard(1100000,APP_KEY_PTT);keyboard(1200000,APP_KEY_INVALID);exitAt=1800000;finish();assert(markN==1 && quietN==1 && firstMark>=1100000);checks++;
  config(mode,algo,5);menu();keyboard(400000,APP_KEY_PTT);keyboard(600000,APP_KEY_MENU);keyboard(700000,APP_KEY_PTT);keyboard(900000,APP_KEY_INVALID);exitAt=1500000;finish();assert(quietN==1 && markN==1 && !q.settings);checks++;
  config(mode,algo,5);menu();keyboard(300000,APP_KEY_UP);keyboard(350000,APP_KEY_INVALID);keyboard(400000,APP_KEY_PTT);keyboard(600000,APP_KEY_INVALID);keyboard(700000,APP_KEY_MENU);keyboard(800000,APP_KEY_INVALID);keyboard(1000000,APP_KEY_PTT);keyboard(1100000,APP_KEY_INVALID);exitAt=30000000;finish();assert(markN==74 && quietN==2 && (saved[11]>>4)==6);checks++;
 }
 config(1,1,5);menu();keyboard(400000,APP_KEY_PTT);keyboard(15400000,APP_KEY_INVALID);exitAt=17000000;finish();assert(markN==1 && quietN==1 && edgeTime[1]<15400000);checks++;
 for(unsigned d=1;d<=15;d+=d==1?4:10)for(unsigned algo=0;algo<3;algo++){
  config(1,algo,d);seconds=d;kind=stopAt=firstEnd=secondStart=0;cycleOrigin=0xF0000000u;api.get_key=repeatKey;api.tx_carrier=gateRepeat;api.tx_end=finishRepeat;exitAt=140000000;finish();
  assert(markN==146 && quietN==2 && endN==2 && !q.repeat);unsigned gap=secondStart-firstEnd;assert(gap>=d*1000000 && gap<=d*1000000+6000);
  printf("{\"delay_s\":%u,\"algo\":%u,\"rf_gap_us\":%u}\n",d,algo,gap);checks++;
 }
 /* PTT wins over pause expiry, and can take over an auto mark in SETTINGS. */
 for(kind=1;kind<=2;kind++){
  config(1,1,1);seconds=1;stopAt=firstEnd=secondStart=0;api.get_key=repeatKey;api.tx_carrier=gateRepeat;api.tx_end=finishRepeat;exitAt=140000000;finish();assert(!q.repeat && markN>=74 && markN<146 && quietN==2);checks++;
 }
 printf("{\"settings_ptt_checks\":%u,\"passed\":true}\n",checks);return 0;
}
