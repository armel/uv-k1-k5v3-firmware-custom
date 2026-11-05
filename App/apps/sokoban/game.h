#ifndef SOKOBAN_GAME_H
#define SOKOBAN_GAME_H
#include <stdint.h>
#include <stdbool.h>
#define WALL 1u
#define GOAL 2u
#define BOX 4u
#define PLAYER 8u
#define CELLS 60u
#define UNDO 64u
typedef struct {uint8_t cell[CELLS],player,history[UNDO],head,count;uint32_t moves,pushes;} game_t;
static int neighbour(unsigned p,unsigned dir){
 if(dir>3 || p>=CELLS)return -1;
 if((dir==0 && p<10)||(dir==1 && p>=50)||(dir==2 && p%10==0)||(dir==3 && p%10==9))return -1;
 static const int8_t delta[]={-10,10,-1,1};return (int)p+delta[dir];
}
static bool game_won(const game_t *g){
 bool box=false;for(unsigned i=0;i<CELLS;i++)if(g->cell[i]&BOX){box=true;if(!(g->cell[i]&GOAL))return false;}return box;
}
static bool game_move(game_t *g,unsigned dir){
 int n=neighbour(g->player,dir);if(n<0 || (g->cell[n]&WALL))return false;
 bool push=g->cell[n]&BOX;
 if(push){int b=neighbour(n,dir);if(b<0 || (g->cell[b]&(WALL|BOX)))return false;g->cell[b]|=BOX;g->cell[n]&=~BOX;g->pushes++;}
 g->history[g->head]=dir|(push?4:0);g->head=(g->head+1)&(UNDO-1);if(g->count<UNDO)g->count++;
 g->player=n;g->moves++;return true;
}
static bool game_undo(game_t *g){
 if(!g->count)return false;
 g->head=(g->head+UNDO-1)&(UNDO-1);g->count--;unsigned action=g->history[g->head],dir=action&3;
 if(action&4){int b=neighbour(g->player,dir);g->cell[b]&=~BOX;g->cell[g->player]|=BOX;g->pushes--;}
 g->player=neighbour(g->player,dir^1);g->moves--;return true;
}
#endif
