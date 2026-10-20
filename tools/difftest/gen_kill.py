import os, sys, random
# gen_kill.py <type> <seed> <ticks>: start the game, spawn enemy <type>, god mode, auto-aim at it (kills it: death
# behaviour, drops, splits), random WASD movement, dump balls each tick
t=int(sys.argv[1]); seed=int(sys.argv[2]); n=int(sys.argv[3])
r=random.Random(seed)
lines=[l.strip() for l in open(os.environ.get('START_SCRIPT') or os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tests','start_game.txt')) if l.strip()]
ev=[(1000,'eval set _root.a.lasth 100000000'),(1000,'eval call _root.a create 320 220 0 0 0 0 %d'%t),(1001,'eval aim 1')]
held={}; keys=[87,65,83,68]
for tick in range(1001,1000+n):
    if tick%30==0: ev.append((tick,'eval set _root.a.lasth 100000000'))
    for k in keys:
        if held.get(k,0)>0:
            held[k]-=1
            if held[k]==0: ev.append((tick,'up %d'%k))
        elif r.random()<0.03:
            held[k]=5+r.randrange(30); ev.append((tick,'down %d'%k))
    ev.append((tick,'eval balls'))
out=[(int(l.split()[0]),' '.join(l.split()[1:])) for l in lines]+ev
out.sort(key=lambda e:e[0])
for tk,c in out: print(tk,c)
