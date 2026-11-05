#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SOKOBAN_HOST_TEST
#include "../sokoban_app.c"
static uint32_t now;static unsigned frames,saves,scenario;static bool won_seen;static unsigned prompts;static FILE *assets;
static uint8_t fb[7][128],status[128];
static uint8_t key(void){
 if(scenario==20){if(now==480)assert(prompts==1);if(now==560)assert(frames==2 && prompts==1);if(now==1080)assert(frames==3 && prompts==2);if(now==1120)return 13;return 19;}
 if(scenario==18 || scenario==19){if(now==40)return scenario==18?APP_KEY_EXIT:APP_KEY_PTT;return 19;}
 if(scenario==15){if(now==40)return 10;if(now==120)return 13;return 19;}
 if(scenario==16){if(now>=40 && now<400)return 10;if(now==440)return 13;return 19;}
 if(scenario==17){if(now==40)return APP_KEY_SAVER;if(now==80)return APP_KEY_WAKE;if(now==120)return 13;return 19;}
 if(scenario==14){if(now==40)return 6;if(now==120)return 13;return 19;}
 if(now<80)return now==40?APP_KEY_MENU:APP_KEY_INVALID;
 uint32_t t=now-80;
 if(scenario>=11 && scenario<=13){
  if(t==1600)assert(g.level==0 && game_won(&g.game));
  if(t==1680 && scenario!=12)assert(g.level==1 && !g.game.moves);
  if(t==40 || t==120)return 6;
  if(scenario==12 && t==1640)return 14;
  if(scenario==13 && t>=1480 && t<1880)return 6;
  if(t==2120)return 13;
  return 19;
 }
 if(scenario==4){if(t<200)return 6;if(t==240)return 13;return 19;}
 if(scenario==5){if(t==40 || t==2040)return 0;if(t==2120)return 13;return 19;}
 if(scenario==6){if(t==40)return APP_KEY_SAVER;if(t==120)return APP_KEY_WAKE;if(t==200)return 13;return 19;}
 if(scenario==7){if(t==40)return 9;return 19;}
 if(scenario==8){if(t==40)return 9;if(t==120)return 13;return 19;}
 if(scenario==9){if(t==40)return 13;return 19;}
 if(scenario==10){if(t==40)return APP_KEY_PTT;return 19;}
 if(scenario==3){if(t==40)return 0;if(t==120)return 5;if(t>=200 && t<280)return 13;return 19;}
 if(scenario==1){if(t>=40 && t<700)return 6;if(t>=800 && t<880)return 13;return 19;}
 if(t>=760 && t<840)return 13;
 switch(t){case 40:case 120:case 280:return 6;case 200:return 14;case 360:return 10;case 440:return 9;case 520:return 3;case 600:case 680:return 0;default:return 19;}
}
static void delay(uint32_t n){now+=n;assert(now<3200);}
static uint32_t ticks(void){return now;}
static void nothing(void){}
static void audio(bool on){assert(!on);}
static void cfg_load(uint8_t *p,uint8_t n){assert(n==1);*p=(scenario==8 || scenario==18)?23:scenario==9?255:0;}
static void cfg_save(const uint8_t *p,uint8_t n){assert(n==1 && *p==(scenario==8?0:1));saves++;}
static uint16_t asset(uint16_t off,void *p,uint16_t n){assert(off+n<=24*30+1024);if((scenario==15 && off>=720) || scenario==2 || (scenario==7 && off>0 && off<720))return 0;assert(!fseek(assets,off,SEEK_SET));return fread(p,1,n,assets);}
static void tiny(const char *s,uint8_t x,uint8_t y,bool st,bool fill){(void)fill;assert(x+strlen(s)*4<=128 && y+6<=(st?8:56));}
static void inverse(const char *s,uint8_t x,uint8_t row,bool st,bool fill,uint8_t end){assert(end==x+strlen(s)*4 && x>=2 && end<128);tiny(s,x,row*8+1,st,fill);}
static void bold(const char *s,uint8_t start,uint8_t end,uint8_t line){assert(!strcmp(s,"PRESS MENU") && start==0 && end==127 && line==6);prompts++;}
static void blit(void){frames++;if(game_won(&g.game))won_seen=true;}
int main(int argc,char **argv){assert(argc==2);assets=fopen(argv[1],"rb");assert(assets);
 app_api_t api={.fb=fb,.status_line=status,.display_clear=nothing,.status_clear=nothing,.draw_battery=nothing,.battery_sample=nothing,.print_tiny=tiny,.print_bold=bold,.print_inverse=inverse,.blit_full=blit,.blit_status=nothing,.get_key=key,.delay_ms=delay,.ticks_ms=ticks,.cfg_load=cfg_load,.cfg_save=cfg_save,.asset_read=asset,.audio_path=audio,.backlight_on=nothing,.backlight_update=nothing};
 for(scenario=0;scenario<21;scenario++){now=frames=saves=prompts=0;won_seen=false;app_main(&api);assert(frames>0);
  if(scenario==20)assert(g.title && !saves);
  if(scenario==18)assert(g.title && g.level==23 && !saves);
  if(scenario==19)assert(g.title && !saves);
  if(scenario==15 || scenario==16)assert(!g.title && !g.game.moves && !saves);
  if(scenario==17)assert(g.title && frames==2 && !saves);
  if(scenario==14)assert(g.title && !g.game.moves && !saves);
  if(scenario==0)assert(won_seen && g.level==1 && !g.game.moves && saves==1);
  if(scenario==1)assert(won_seen && g.level==0 && g.game.moves==2 && !saves);
  if(scenario==2)assert(!saves);
  if(scenario==3)assert(frames>=3 && !g.confirm && !saves);
  if(scenario==4)assert(!g.game.moves && !saves);
  if(scenario==5)assert(g.confirm && g.confirm_at==2120 && !saves);
  if(scenario==6)assert(frames==3 && !g.game.moves && !saves);
  if(scenario==7)assert(g.level==0 && !g.game.moves && !saves);
  if(scenario==8)assert(g.level==0 && saves==1);
  if(scenario==9 || scenario==10)assert(g.level==0 && !saves);
  if(scenario==11 || scenario==13)assert(won_seen && g.level==1 && !g.game.moves && saves==1);
  if(scenario==12)assert(g.level==0 && g.game.moves==1 && !game_won(&g.game) && !saves);
 }
 fclose(assets);puts("app movement, win, undo, levels, reset, repeat, bounds, persistence and asset errors PASS");
}
