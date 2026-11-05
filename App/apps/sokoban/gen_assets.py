#!/usr/bin/env python3
import os,sys
sys.dont_write_bytecode=True
sys.path.insert(0,os.path.join(os.path.dirname(__file__),'..'))
from app_assets import Assets
from app_art import Canvas
from levels import levels
puzzles=levels();data=bytes(c[i]|(c[i+1]<<4) for c,_ in puzzles for i in range(0,60,2))
a=Assets('SOKOBAN');a.const('LEVEL_COUNT',len(puzzles));a.const('LEVEL_BYTES',30);a.u8('LEVELS',data)
# Match the outlined arcade lettering used by the other built-in games.
cv=Canvas();cv.word('SOKOBAN',4)
cv.sprite(24,29,[0,0,72,62,62,72,0,0],2)
cv.sprite(54,29,[0,126,66,90,90,66,126,0],2)
for x in (44,76):
    cv.rect(x,35,x+5,36);cv.set(x+4,34);cv.set(x+4,37)
cv.rect(90,35,93,38)
a.raw('ART_TITLE',cv.pages());a.main()
