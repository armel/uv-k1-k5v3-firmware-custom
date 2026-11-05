"""Original deterministic Sokoban puzzles, constructed by reversing legal pushes."""
import random
W,H=10,6
DELTA=(-10,10,-1,1)
def levels():
    cells=[1]*60
    for p in (22,23,24,25):cells[p]=0
    cells[22]=8;cells[24]=4;cells[25]=2
    result=[(cells,[3,3])];seen=set()
    rng=random.Random(20261013)
    while len(result)<24:
        floor={y*10+x for y in range(1,5) for x in range(1,9)}
        floor-=set(rng.sample(sorted(floor),2+len(result)//6))
        nbox=2 if len(result)<9 else 3
        goals=set(rng.sample(sorted(floor),nbox));boxes=set(goals)
        player=rng.choice(sorted(floor-boxes));path=[];states={(player,tuple(sorted(boxes))):0}
        for step in range(500):
            d=rng.randrange(4);n=player+DELTA[d]
            if n not in floor or n in boxes:continue
            behind=player-DELTA[d]
            if behind in boxes and rng.randrange(3)!=0:
                boxes.remove(behind);boxes.add(player)
            player=n;path.append(d^1)
            state=(player,tuple(sorted(boxes)))
            if state in states:
                path=path[:states[state]];states={s:i for s,i in states.items() if i<=len(path)}
            else:states[state]=len(path)
        if boxes==goals or len(path)<12+len(result):continue
        cells=[1]*60
        for p in floor:cells[p]=0
        for p in goals:cells[p]|=2
        for p in boxes:cells[p]|=4
        cells[player]|=8
        signature=bytes(cells)
        if signature in seen:continue
        seen.add(signature);result.append((cells,list(reversed(path))))
    return result
