#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../game.h"
int main(void){
 game_t g={0};g.player=22;g.cell[24]=BOX;g.cell[25]=GOAL;
 assert(game_move(&g,3) && g.player==23 && g.moves==1 && !g.pushes);
 assert(game_move(&g,3) && g.cell[25]==(BOX|GOAL) && game_won(&g));
 assert(game_undo(&g) && g.player==23 && g.cell[24]==BOX && g.cell[25]==GOAL && g.moves==1 && !g.pushes);
 assert(game_undo(&g) && g.player==22 && g.moves==0);assert(!game_undo(&g));
 g.cell[23]=WALL;assert(!game_move(&g,3) && !g.moves);g.cell[23]=BOX;assert(!game_move(&g,3));
 memset(&g,0,sizeof g);g.player=9;assert(!game_move(&g,3));g.player=10;assert(!game_move(&g,2));g.player=0;assert(!game_move(&g,0));g.player=59;assert(!game_move(&g,1));
 memset(&g,0,sizeof g);g.player=22;
 for(unsigned i=0;i<100;i++)assert(game_move(&g,i&1?2:3));
 for(unsigned i=0;i<64;i++)assert(game_undo(&g));assert(!game_undo(&g) && g.moves==36 && g.player==22);
 puts("movement, pushes, goals, blocking, edge bounds and undo ring PASS");
}
