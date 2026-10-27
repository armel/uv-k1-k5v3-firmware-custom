
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
int main(void){
 setbuf(stdout,NULL);
 setup(1,18);exitAt=600000;finish();assert(!quietN);cases++;
 for(unsigned message=0;message<5;message++) for(unsigned speed=5;speed<=60;speed++){
  setup(1,speed);q.message=message<3?message:message+11;cycleOrigin=0xffff0000u;start();runAuto();
  const unsigned marks[]={73,89,16,13,6};
  assert(quietN==1 && endN==1 && markN==marks[message] && nEdges==2*markN);
  assert(!rf && !g.txReady && q.release && !q.ready);
  printf("{\"speed\":%u,\"message\":%u,\"rst\":\"%s\",\"edges\":[",speed,message,q.rst);
  for(unsigned i=0;i<nEdges;i++){printf("%s%u",i?",":"",edgeTime[i]);}puts("]}");cases++;
 }
 /* All 1,000 three-digit values, including every intermediate/zero digit. */
 for(unsigned rst=0;rst<1000;rst++){
  setup(1,18);handleUiKeyDirection(APP_KEY_DOWN,-1);assert(q.selected==1);
  handleUiKeyDirection(rst/100,0);handleUiKeyDirection((rst/10)%10,0);handleUiKeyDirection(rst%10,0);
  char expected[80],actual[80];sprintf(expected,"%03u",rst);assert(!strcmp(q.rst,expected));
  q.message=1;unsigned i=0;do{assert(i<sizeof(actual));actual[i]=(char)nextCharacter();}while(actual[i++]);
  sprintf(expected,"DE F4XXX UR %s %s NAME SEB HW?",q.rst,q.rst);assert(!strcmp(actual,expected) && !quietN);
  draw();sprintf(expected,"RST %03u",rst);assert(!strcmp(screen[4],expected));cases++;
 }
 const char *reports[]={"000","079","995","957","579","999","111","359","456","589"};
 for(unsigned r=0;r<10;r++)for(unsigned w=5;w<=60;w+=55){
  setup(1,w);q.message=1;strcpy(q.rst,reports[r]);start();runAuto();
  assert(markN==89 && endN==1 && !rf);
  printf("{\"speed\":%u,\"message\":1,\"rst\":\"%s\",\"edges\":[",w,q.rst);
  for(unsigned i=0;i<nEdges;i++){printf("%s%u",i?",":"",edgeTime[i]);}puts("]}");cases++;
 }
 setup(1,18);handleUiKeyDirection(5,0);assert(!strcmp(q.rst,"995"));
 handleUiKeyDirection(7,0);assert(!strcmp(q.rst,"957"));handleUiKeyDirection(9,0);assert(!strcmp(q.rst,"579"));cases++;
 for(unsigned digit=0;digit<10;digit++){
  setup(1,18);unsigned oldAlgo=g.iambic;bool oldSpeaker=g.speaker;
  handleUiKeyDirection(digit,0);assert(q.rst[2]==(char)('0'+digit) && !q.settings && !q.selected);
  assert(g.iambic==oldAlgo && g.speaker==oldSpeaker && !quietN);cases++;
 }
 setup(1,18);handleUiKeyDirection(APP_KEY_UP,1);assert(q.selected==15 && !g.tuneOffset10Hz);
 handleUiKeyDirection(APP_KEY_DOWN,-1);assert(q.selected==0);handleUiKeyDirection(APP_KEY_DOWN,-1);assert(q.selected==1);
 handleUiKeyDirection(APP_KEY_DOWN,-1);assert(q.selected==2);handleUiKeyDirection(APP_KEY_UP,1);assert(q.selected==1);
 handleUiKeyDirection(APP_KEY_UP,1);assert(!q.selected && !g.tuneOffset10Hz && !quietN);cases++;
 setup(1,18);draw();assert(!strcmp(screen[0],"433650000 Hz") && !headerText[0]);
 assert(!strcmp(screen[2],"01 CQ / APPEL") && !strcmp(screen[4],"RST 599") && !screen[3][0] && !screen[5][0] && !screen[6][0]);cases++;
 /* Spatial QSO navigation must ignore the resident SET_NAV value direction. */
 for(int direction=-1;direction<=1;direction+=2){
  setup(1,18);handleUiKeyDirection(APP_KEY_DOWN,(int8_t)direction);assert(q.selected==1 && !g.tuneOffset10Hz);cases++;
  setup(1,18);q.selected=2;handleUiKeyDirection(APP_KEY_UP,(int8_t)direction);assert(q.selected==1 && !g.tuneOffset10Hz);cases++;
 }
 const uint32_t frequencies[]={1800000u,43365000u,130000000u};
 for(unsigned f=0;f<3;f++)for(int offset=-5000;offset<=5000;offset+=5000){
  setup(1,18);radioFrequency=frequencies[f];g.tuneOffset10Hz=(int16_t)offset;draw();
  char expected[32];sprintf(expected,"%llu Hz",(unsigned long long)(frequencies[f]+offset)*10u);
  assert(!strcmp(screen[0],expected));cases++;
 }
 for(unsigned selection=0;selection<3;selection++){
  setup(1,18);q.selected=selection;draw();
  const char *labels[]={"01 CQ / APPEL","02 REPONSE/RST","03 73 / FIN"};assert(!strcmp(screen[2],labels[selection]) && !headerText[0]);
  assert(!screen[3][0] && !strcmp(screen[4],"RST 599") && !screen[5][0] && !screen[6][0]);
  q.message=14;start();draw();assert(!strcmp(headerText,"TX MSG 15") && !strcmp(screen[2],labels[selection]));
  stopAuto();draw();assert(!strcmp(screen[2],labels[selection]));cases++;
 }
 for(unsigned mode=0;mode<2;mode++)for(unsigned speed=5;speed<=60;speed+=55)for(int cal=-50;cal<=50;cal+=50){
  setup(mode,speed);q.settings=true;g.calSteps=cal;g.rxPitchHz=900;g.breakInMs=1500;g.reverse=true;draw();
  char expected[32];sprintf(expected,"2 WPM %u",speed);assert(!strcmp(capsules[0],mode?"1 PADDLE":"1 STRAIGHT") && !strcmp(capsules[1],expected));
  assert(!strcmp(capsules[2],"3 TONE 900") && !strcmp(capsules[3],"4 BK 1500") && !strcmp(capsules[4],"5 DA-DI"));
  sprintf(expected,"6 CAL %d",cal);assert(!strcmp(capsules[5],expected));cases++;
 }
 setup(1,18);handleUiKeyDirection(APP_KEY_MENU,0);assert(q.settings);
 handleUiKeyDirection(APP_KEY_UP,1);assert(g.tuneOffset10Hz==10);handleUiKeyDirection(APP_KEY_DOWN,-1);assert(!g.tuneOffset10Hz);
 handleUiKeyDirection(APP_KEY_2,0);assert(g.wpm==19 && !strcmp(q.rst,"599"));handleUiKeyDirection(APP_KEY_F,0);
 handleUiKeyDirection(APP_KEY_2,0);assert(g.wpm==18 && !g.fArm);handleUiKeyDirection(APP_KEY_8,0);assert(g.iambic==IAMBIC_A);
 handleUiKeyDirection(APP_KEY_0,0);assert(!g.speaker && !strcmp(q.rst,"599"));
 handleUiKeyDirection(APP_KEY_EXIT,0);assert(!q.settings && g.running);draw();assert(!strcmp(screen[4],"RST 599") && !screen[3][0] && !screen[5][0] && !screen[6][0] && !capsules[0][0] && !capsules[5][0]);cases++;
 for(unsigned selection=0;selection<3;selection++){
  setup(1,18);idleEntry();if(selection){keyboard(5400000,APP_KEY_DOWN);keyboard(5500000,APP_KEY_INVALID);}
  if(selection==2){keyboard(5600000,APP_KEY_DOWN);keyboard(5700000,APP_KEY_INVALID);}
  keyboard(5800000,APP_KEY_PTT);keyboard(5900000,APP_KEY_INVALID);exitAt=45000000;finish();
  const unsigned marks[]={73,89,16};assert(q.selected==selection && q.message==selection && markN==marks[selection] && quietN==1 && endN==1);cases++;
 }
 const unsigned txKeys[]={APP_KEY_PTT,KEY_SIDE1,KEY_SIDE2};
 for(unsigned keyIndex=0;keyIndex<1;keyIndex++){
  unsigned key=txKeys[keyIndex],count=keyIndex==0?73:keyIndex==1?13:6;
  for(unsigned mode=0;mode<2;mode++){
   setup(mode,18);keyboard(0,key);keyboard(6000000,APP_KEY_INVALID);exitAt=6500000;finish();assert(!quietN);cases++;
   setup(mode,18);idleEntry();keyboard(1000000,key);keyboard(40000000,APP_KEY_INVALID);exitAt=41000000;finish();assert(quietN==1 && markN==count);cases++;
   setup(mode,18);idleEntry();keyboard(5210000,key);keyboard(5400000,APP_KEY_INVALID);exitAt=40000000;finish();assert(quietN==1 && markN==count);cases++;
   setup(mode,18);idleEntry();keyboard(5400000,key);keyboard(40000000,APP_KEY_INVALID);exitAt=41000000;finish();assert(quietN==1 && endN==1 && markN==count && !q.selected);cases++;
   setup(mode,18);idleEntry();keyboard(5400000,key);keyboard(40000000,APP_KEY_INVALID);keyboard(40050000,key);keyboard(40150000,APP_KEY_INVALID);exitAt=70000000;finish();assert(quietN==2 && endN==2 && markN==2*count);cases++;
   setup(mode,18);idleEntry();keyboard(5400000,key);keyboard(40000000,APP_KEY_INVALID);keyboard(40005000,key);keyboard(45000000,APP_KEY_INVALID);exitAt=46000000;finish();assert(quietN==1 && markN==count);cases++;
  }
  setup(1,18);idleEntry();keyboard(5400000,APP_KEY_MENU);keyboard(5500000,APP_KEY_INVALID);
  keyboard(5600000,key);keyboard(5700000,APP_KEY_INVALID);exitAt=6500000;finish();assert(!quietN && !q.selected);cases++;
  setup(1,18);idleEntry();event(5400000,1);event(5800000,0);keyboard(5500000,key);keyboard(7000000,APP_KEY_INVALID);exitAt=7400000;finish();assert(!autoLabels && quietN==1 && markN<10);cases++;
 }
 for(unsigned key=0;key<=APP_KEY_F;key++){
  if(key==APP_KEY_EXIT)continue;
  setup(1,18);idleEntry();keyboard(5400000,key);keyboard(5500000,APP_KEY_INVALID);exitAt=6000000;finish();assert(!quietN && !markN);cases++;
 }
 setup(1,18);idleEntry();keyboard(5400000,5);keyboard(5500000,APP_KEY_INVALID);
 keyboard(5600000,7);keyboard(5700000,APP_KEY_INVALID);keyboard(5800000,9);keyboard(5900000,APP_KEY_INVALID);
 keyboard(6000000,APP_KEY_DOWN);keyboard(6100000,APP_KEY_INVALID);keyboard(6200000,APP_KEY_PTT);keyboard(6300000,APP_KEY_INVALID);
 exitAt=45000000;finish();assert(!strcmp(q.rst,"579") && q.message==1 && markN==89 && quietN==1 && endN==1);
 printf("{\"speed\":18,\"message\":1,\"rst\":\"579\",\"edges\":[");
 for(unsigned i=0;i<nEdges;i++){printf("%s%u",i?",":"",edgeTime[i]);}puts("]}");cases++;
 for(unsigned message=0;message<3;message++)for(unsigned cancel=0;cancel<3;cancel++){
  setup(1,18);idleEntry();if(message==1 || message==2){keyboard(5300000,APP_KEY_DOWN);keyboard(5350000,APP_KEY_INVALID);}
  if(message==2){keyboard(5400000,APP_KEY_DOWN);keyboard(5450000,APP_KEY_INVALID);}
  keyboard(5600000,message==3?KEY_SIDE1:message==4?KEY_SIDE2:APP_KEY_PTT);keyboard(5700000,APP_KEY_INVALID);
  if(!cancel){keyboard(6100000,APP_KEY_EXIT);keyboard(6200000,APP_KEY_INVALID);}else{event(6100000,cancel);event(6200000,0);}
  exitAt=7000000;finish();const unsigned marks[]={73,89,16,13,6};assert(markN>0 && markN<marks[message] && quietN==1 && endN==1 && edgeTime[nEdges-1]<=6106000);cases++;
 }
 for(unsigned message=0;message<5;message++){
  setup(1,18);q.message=message<3?message:message+11;denyTx=1;start();runAuto();assert(!quietN && !markN && !g.txReady);cases++;
  setup(1,18);q.message=message<3?message:message+11;denyAfter=5;start();runAuto();assert(markN==5 && quietN==1 && endN==1 && !rf);cases++;
 }
 /* All PTT transitions below execute the real foreground loop. */
 for(unsigned mode=0;mode<2;mode++){
  setup(mode,18);exitAt=600000;finish();assert(!quietN);cases++;
  setup(mode,18);keyboard(0,APP_KEY_PTT);keyboard(6000000,APP_KEY_INVALID);exitAt=6500000;finish();assert(!quietN);cases++;
  setup(mode,18);idleEntry();exitAt=6000000;finish();assert(!quietN);cases++;
  setup(mode,18);event(0,1);exitAt=6500000;finish();assert(!quietN);cases++;
  setup(mode,18);event(0,1);event(5200000,0);keyboard(1000000,APP_KEY_PTT);keyboard(6000000,APP_KEY_INVALID);exitAt=6500000;finish();assert(!quietN);cases++;
  setup(mode,18);idleEntry();keyboard(5210000,APP_KEY_PTT);keyboard(5400000,APP_KEY_INVALID);exitAt=40000000;finish();assert(markN==73 && quietN==1);cases++;
  setup(mode,18);idleEntry();keyboard(5400000,APP_KEY_PTT);keyboard(5500000,APP_KEY_INVALID);exitAt=40000000;finish();assert(markN==73 && quietN==1 && endN==1 && autoLabels);assert(firstMark<=5405000);cases++;
  setup(mode,18);idleEntry();keyboard(5400000,APP_KEY_PTT);keyboard(40000000,APP_KEY_INVALID);exitAt=41000000;finish();assert(markN==73 && quietN==1 && endN==1);cases++;
  setup(mode,18);idleEntry();keyboard(5400000,APP_KEY_PTT);keyboard(40000000,APP_KEY_INVALID);keyboard(40050000,APP_KEY_PTT);keyboard(40150000,APP_KEY_INVALID);exitAt=70000000;finish();assert(markN==146 && quietN==2 && endN==2);cases++;
  /* A brief second press during CQ is ignored rather than queued. */
  setup(mode,18);idleEntry();keyboard(5400000,APP_KEY_PTT);keyboard(5500000,APP_KEY_INVALID);keyboard(6000000,APP_KEY_PTT);keyboard(6100000,APP_KEY_INVALID);exitAt=40000000;finish();assert(markN==73 && quietN==1);cases++;
  /* A 5ms release after completion must not re-enable held PTT. */
  setup(mode,18);idleEntry();keyboard(5400000,APP_KEY_PTT);keyboard(40000000,APP_KEY_INVALID);keyboard(40005000,APP_KEY_PTT);keyboard(45000000,APP_KEY_INVALID);exitAt=46000000;finish();assert(markN==73 && quietN==1);cases++;
  /* First PTT can follow the startup release immediately (no second wait). */
  setup(mode,18);idleEntry();keyboard(5230000,APP_KEY_PTT);keyboard(5330000,APP_KEY_INVALID);exitAt=40000000;finish();assert(markN==73 && quietN==1 && firstMark<=5235000);cases++;
  /* Former UI cannot launch CQ, with input available. */
  setup(mode,18);idleEntry();keyboard(5400000,APP_KEY_7);keyboard(5500000,APP_KEY_INVALID);keyboard(5600000,APP_KEY_MENU);keyboard(5700000,APP_KEY_INVALID);exitAt=6500000;finish();assert(!quietN);cases++;
 }
 /* Cancellation requires release of both PTT and paddle/keypad controls. */
 for(unsigned abortMode=0;abortMode<3;abortMode++){
  setup(1,18);idleEntry();keyboard(5400000,APP_KEY_PTT);
  if(abortMode==0){keyboard(6100000,APP_KEY_EXIT);keyboard(6200000,APP_KEY_INVALID);}
  else {event(6100000,abortMode==1?1:2);event(6200000,0);keyboard(6300000,APP_KEY_INVALID);}
  exitAt=6600000;finish();assert(markN>0 && markN<73 && quietN==1 && endN==1);assert(edgeTime[nEdges-1]<=6106000);cases++;
 }
 /* PTT remains held through a paddle cancellation: no restart and no manual mark. */
 setup(1,18);idleEntry();event(6100000,1);event(6200000,0);keyboard(5400000,APP_KEY_PTT);keyboard(40000000,APP_KEY_INVALID);exitAt=41000000;finish();assert(markN<73 && quietN==1 && endN==1);cases++;
 setup(1,18);idleEntry();keyboard(5400000,APP_KEY_PTT);keyboard(6100000,APP_KEY_EXIT);keyboard(6200000,APP_KEY_PTT);keyboard(40000000,APP_KEY_INVALID);exitAt=41000000;finish();assert(markN<73 && quietN==1 && endN==1);cases++;
 setup(1,18);idleEntry();event(6100000,1);event(15000000,0);keyboard(5400000,APP_KEY_PTT);keyboard(5500000,APP_KEY_INVALID);exitAt=16000000;finish();assert(markN<73 && quietN==1 && endN==1);cases++;
 setup(1,18);idleEntry();event(6100000,1);event(7000000,0);keyboard(5400000,APP_KEY_PTT);keyboard(5500000,APP_KEY_INVALID);keyboard(6500000,APP_KEY_PTT);keyboard(8000000,APP_KEY_INVALID);exitAt=9000000;finish();assert(markN<73 && quietN==1 && endN==1);cases++;
 setup(0,18);api.api_size=364;idleEntry();keyboard(5400000,APP_KEY_PTT);keyboard(5500000,APP_KEY_INVALID);exitAt=6500000;finish();assert(!quietN && !markN);cases++;
 /* Cancel, release, press again: a second CQ starts normally. */
 setup(1,18);idleEntry();event(6100000,1);event(6200000,0);keyboard(5400000,APP_KEY_PTT);keyboard(6300000,APP_KEY_INVALID);keyboard(6400000,APP_KEY_PTT);keyboard(6500000,APP_KEY_INVALID);exitAt=40000000;finish();assert(markN>73 && quietN==2 && endN==2);cases++;
 /* Cancel, release, then fresh manual contact. */
 setup(1,18);idleEntry();event(6100000,1);event(6200000,0);event(6400000,1);event(6500000,0);keyboard(5400000,APP_KEY_PTT);keyboard(6300000,APP_KEY_INVALID);exitAt=7400000;finish();assert(quietN==2 && endN==2 && edgeTime[nEdges-2]>=6400000);cases++;
 /* Manual STRAIGHT remains manual, including mark-duration protection. */
 setup(0,18);idleEntry();event(5400000,1);event(5500000,0);exitAt=6500000;finish();assert(markN==1 && quietN==1);cases++;
 setup(0,18);idleEntry();event(5400000,1);event(20500000,0);exitAt=21000000;finish();assert(markN==1 && quietN==1);cases++;
 /* PTT observed inside runPaddle must not become a deferred CQ. */
 setup(1,18);idleEntry();event(5400000,1);event(5800000,0);keyboard(5500000,APP_KEY_PTT);keyboard(7000000,APP_KEY_INVALID);exitAt=7400000;finish();assert(!autoLabels && quietN==1 && markN<10);cases++;
 /* An overlapping held PTT cannot turn a STRAIGHT carrier into CQ on release. */
 setup(0,18);idleEntry();event(5400000,1);event(5800000,0);keyboard(5500000,APP_KEY_PTT);keyboard(7000000,APP_KEY_INVALID);exitAt=7400000;finish();assert(!autoLabels && quietN==1 && markN==1);cases++;
 for(unsigned algo=0;algo<3;algo++){
  setup(1,18);g.iambic=algo<<4;g.wpm=37;g.calSteps=-11;g.reverse=true;g.speaker=false;g.rxPitchHz=850;g.breakInMs=1450;
  saveConfig();config_t c;memcpy(&c,saved,12);assert(c.wpm==37 && c.calSteps==-11 && c.reverse && !c.speaker && c.rxPitchHz==850 && c.breakInMs==1450 && c.reservedPort==algo);
  useSaved=true;g.wpm=5;loadConfig();
  assert(g.wpm==37 && g.calSteps==-11 && g.reverse && !g.speaker && g.rxPitchHz==850 && g.breakInMs==1450 && (g.iambic>>4)==algo);cases++;
 }
 printf("{\"passed_cases\":%u}\n",cases);return 0;
}
