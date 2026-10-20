import os, sys, random
# gen_en.py <type> <seed> <ticks> [x y]: start the game, spawn enemy <type>, random movement + shooting, dump balls each tick
t=int(sys.argv[1]); seed=int(sys.argv[2]); n=int(sys.argv[3])
x=float(sys.argv[4]) if len(sys.argv)>4 else 320; y=float(sys.argv[5]) if len(sys.argv)>5 else 220
r=random.Random(seed)
lines=[l.strip() for l in open(os.environ.get('START_SCRIPT') or os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tests','start_game.txt')) if l.strip()]
ev=[]
ev.append((1000,'eval call _root.a create %g %g 0 0 0 0 %d'%(x,y,t)))
held={}
keys=[87,65,83,68,37,38,39,40]
for tick in range(1001,1000+n):
    for k in keys:
        if held.get(k,0)>0:
            held[k]-=1
            if held[k]==0: ev.append((tick,'up %d'%k))
        elif r.random()<0.04:
            held[k]=5+r.randrange(40); ev.append((tick,'down %d'%k))
    ev.append((tick,'eval balls'))
for k,v in held.items():
    if v>0: ev.append((1000+n,'up %d'%k))
out=[(int(l.split()[0]),' '.join(l.split()[1:])) for l in lines]+ev
out.sort(key=lambda e:e[0])
for tk,c in out: print(tk,c)
