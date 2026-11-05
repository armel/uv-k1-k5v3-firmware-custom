/* Sokoban overlay — Apache-2.0, BI8SYN. */
#include <stddef.h>
#include "../app_api.h"
#include "game.h"
#include "sokoban_assets.h"
void *memset(void *p,int c,size_t n){uint8_t *b=p;while(n--)*b++=c;return p;}
void *memcpy(void *p,const void *q,size_t n){uint8_t *d=p;const uint8_t *s=q;while(n--)*d++=*s++;return p;}
static struct {const app_api_t *api;game_t game;uint8_t level,previous;bool confirm,won_pending,wait_release,title,title_on;uint32_t confirm_at,repeat_at,won_at,title_at;} g;
#define A (g.api)
static void value(char *p,uint32_t n,unsigned digits){while(digits){p[--digits]='0'+n%10;n/=10;}}
static bool load(void){
 uint8_t packed[LEVEL_BYTES];if(A->asset_read(LEVELS+g.level*LEVEL_BYTES,packed,sizeof packed)!=sizeof packed)return false;
 memset(&g.game,0,sizeof g.game);g.confirm=false;g.won_pending=false;
 for(unsigned i=0;i<CELLS;i++){uint8_t tile=(packed[i/2]>>((i&1)*4))&15;g.game.cell[i]=tile&7;if(tile&PLAYER)g.game.player=i;}
 return true;
}
static void draw(void){
 if(g.title){
  A->display_clear();A->status_clear();
  if(A->asset_read(ART_TITLE,A->status_line,128)!=128 || A->asset_read(ART_TITLE+128,A->fb[0],896)!=896){
   A->display_clear();A->status_clear();A->print_tiny("SOKOBAN",50,4,false,true);
  }
  if(g.title_on)A->print_bold("PRESS MENU",0,127,6);
  A->blit_status();A->blit_full();return;
 }
 A->display_clear();A->status_clear();A->print_inverse("SOKOBAN",2,0,true,true,30);
 char level[]="L01/24";value(level+1,g.level+1,2);value(level+4,LEVEL_COUNT,2);A->print_tiny(level,38,1,true,true);A->draw_battery();
 /* Eight column bytes per 8x8 tile, least significant bit at the top. */
 static const uint8_t wall[]={255,85,255,85,255,85,255,85};
 static const uint8_t box[]={0,126,66,90,90,66,126,0};
 static const uint8_t placed[]={0,126,126,102,102,126,126,0};
 static const uint8_t player[]={0,0,72,62,62,72,0,0};
 for(unsigned i=0;i<CELLS;i++){
  uint8_t t=g.game.cell[i],*dst=A->fb[i/10]+(i%10)*8;
  for(unsigned x=0;x<8;x++){
   uint8_t b=0;if(t&WALL)b=wall[x];else if(t&BOX)b=(t&GOAL)?placed[x]:box[x];else if(i==g.game.player)b=player[x];else if((t&GOAL) && (x==3 || x==4))b=24;
   dst[x]=b;
  }
 }
 unsigned total=0,done=0;for(unsigned i=0;i<CELLS;i++){if(g.game.cell[i]&BOX){total++;if(g.game.cell[i]&GOAL)done++;}}
 char goals[]="0/0";goals[0]='0'+done;goals[2]='0'+total;
 A->print_tiny("BOX",86,1,false,true);A->print_tiny(goals,86,8,false,true);
 char m[]="00000",p[]="00000";value(m,g.game.moves,5);value(p,g.game.pushes,5);
 A->print_tiny("MOV",86,18,false,true);A->print_tiny(m,86,25,false,true);
 A->print_tiny("PUSH",86,34,false,true);A->print_tiny(p,86,41,false,true);
 if(game_won(&g.game))A->print_tiny("WON AUTO:NEXT *UNDO",2,49,false,true);
 else if(g.confirm)A->print_tiny("0 AGAIN TO RESTART",2,49,false,true);
 else A->print_tiny("2/4/6/8 MOVE *UNDO 0RESET 3/9LV",2,49,false,true);
 A->blit_status();A->blit_full();
}
static int direction(uint8_t key){return key==2?0:key==8?1:key==4?2:key==6?3:-1;}
#ifndef SOKOBAN_HOST_TEST
__attribute__((section(".text.entry"),used))
#endif
void app_main(const app_api_t *api){
 memset(&g,0,sizeof g);g.api=api;A->audio_path(false);uint8_t saved;A->cfg_load(&saved,1);g.level=saved<LEVEL_COUNT?saved:0;uint8_t original=g.level;
 A->backlight_on();A->battery_sample();
 if(!load()){A->display_clear();A->print_tiny("LEVEL ASSET ERROR",2,1,false,true);A->blit_full();return;}
 g.title=true;g.title_on=true;g.title_at=A->ticks_ms();g.previous=A->get_key();draw();uint32_t last_battery=A->ticks_ms();g.repeat_at=last_battery+300;bool running=true;
 while(running){
  A->delay_ms(40);uint32_t now=A->ticks_ms();uint8_t key=A->get_key();bool changed=false;
  if(key==APP_KEY_SAVER){g.previous=APP_KEY_INVALID;A->backlight_update();continue;}
  if(key==APP_KEY_WAKE){g.previous=APP_KEY_INVALID;draw();continue;}
  if(g.confirm && now-g.confirm_at>=2000){g.confirm=false;changed=true;}
  bool fresh=key!=g.previous;int dir=direction(key);
  if(g.wait_release){
   if(key==APP_KEY_INVALID)g.wait_release=false;
   else if(key!=APP_KEY_EXIT && key!=APP_KEY_PTT){fresh=false;dir=-1;}
  }
  if(fresh)g.repeat_at=now+300;
  bool repeat=dir>=0 && !fresh && (int32_t)(now-g.repeat_at)>=0;
  if(repeat)g.repeat_at=now+120;
  if(key<APP_KEY_INVALID && (fresh || repeat)){
   if(key==APP_KEY_EXIT || key==APP_KEY_PTT){running=false;break;}
   if(g.title){
    if(fresh && key==APP_KEY_MENU){g.title=false;g.wait_release=true;changed=true;}
   }
   else if(key!=APP_KEY_0 && g.confirm){g.confirm=false;changed=true;}
   if(!g.title && dir>=0 && !game_won(&g.game))changed|=game_move(&g.game,dir);
   else if(!g.title && fresh && key==APP_KEY_STAR)changed|=game_undo(&g.game);
   else if(!g.title && fresh && key==APP_KEY_0){if(g.confirm){if(!load())break;}else{g.confirm=true;g.confirm_at=now;}changed=true;}
   else if(!g.title && fresh && (key==APP_KEY_3 || key==APP_KEY_9 || (key==APP_KEY_MENU && game_won(&g.game)))){
    uint8_t old=g.level;
    if(key==APP_KEY_3)g.level=(g.level+LEVEL_COUNT-1)%LEVEL_COUNT;else g.level=(g.level+1)%LEVEL_COUNT;
    if(!load()){g.level=old;break;}
    g.wait_release=key!=APP_KEY_INVALID;changed=true;
   }
  }
  if(game_won(&g.game)){
   if(!g.won_pending){g.won_pending=true;g.won_at=now;}
   if(now-g.won_at>=1500){
    uint8_t old=g.level;g.level=(g.level+1)%LEVEL_COUNT;
    if(!load()){g.level=old;break;}
    g.wait_release=key!=APP_KEY_INVALID;changed=true;
   }
  }else g.won_pending=false;
  if(g.title && now-g.title_at>=500){g.title_at=now;g.title_on=!g.title_on;changed=true;}
  if(now-last_battery>=5000){A->battery_sample();last_battery=now;changed=true;}
  g.previous=key;A->backlight_update();if(changed)draw();
 }
 A->audio_path(false);
 for(unsigned released=0;released<3;){released=A->get_key()==APP_KEY_INVALID?released+1:0;A->backlight_update();A->delay_ms(10);}
 if(g.level!=original)A->cfg_save(&g.level,1);
}
