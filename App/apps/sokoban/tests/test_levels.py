#!/usr/bin/env python3
import pathlib,subprocess,tempfile,sys
root=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
from levels import levels
puzzles=levels();assert len(puzzles)==24
with tempfile.TemporaryDirectory() as td:
    p=pathlib.Path(td);source=['#include <assert.h>','#include <string.h>','#include <stdio.h>','#include "game.h"','int main(void){']
    for index,(cells,solution) in enumerate(puzzles):
        assert sum(bool(c&8) for c in cells)==1 and sum(bool(c&4) for c in cells)==sum(bool(c&2) for c in cells)
        source+=['{game_t g={0},initial;', 'uint8_t c[]={'+','.join(map(str,cells))+'};','uint8_t s[]={'+','.join(map(str,solution))+'};',
                 'for(unsigned i=0;i<60;i++){g.cell[i]=c[i]&7;if(c[i]&8)g.player=i;}initial=g;assert(!game_won(&g));',
                 'for(unsigned i=0;i<sizeof s;i++)assert(game_move(&g,s[i]));assert(game_won(&g));',
                 'unsigned n=sizeof s<64?sizeof s:64;for(unsigned i=0;i<n;i++)assert(game_undo(&g));',
                 'for(unsigned i=sizeof s-n;i<sizeof s;i++)assert(game_move(&g,s[i]));assert(game_won(&g));',
                 '(void)initial;',f'printf("level {index+1:02d}: %u steps, %u pushes PASS\\n",(unsigned)g.moves,(unsigned)g.pushes);','}']
    source+=['}'];(p/'test.c').write_text('\n'.join(source))
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
