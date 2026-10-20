import os, sys, random
# gen_room.py <seed> <rooms "a,b,c"> <ticks per room> [chapter skips]: teleports through rooms with random input, god mode
seed=int(sys.argv[1]); rooms=[int(x) for x in sys.argv[2].split(',')]; per=int(sys.argv[3]); skips=int(sys.argv[4]) if len(sys.argv)>4 else 0
r=random.Random(seed)
ev=[(int(l.split()[0]),' '.join(l.split()[1:])) for l in open(os.environ.get('START_SCRIPT') or os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tests','start_game.txt')) if l.strip()]
t=1000
for i in range(skips):
    ev.append((t,'eval call _root.a newstart 0')); t+=300
ev.append((t,'eval set _root.a.lasth 100000000'))
keys=[87,65,83,68,37,38,39,40]
for room in rooms:
    ev.append((t,'eval set _root.a.tar %d'%room))
    ev.append((t,'eval set _root.a.telf 100000'))
    ev.append((t,'eval set _root.a.fra 99999'))
    ev.append((t+1,'eval _root.lev'))
    held={}
    for tick in range(t+2,t+per):
        if tick%30==0: ev.append((tick,'eval set _root.a.lasth 100000000'))
        for k in keys:
            if held.get(k,0)>0:
                held[k]-=1
                if held[k]==0: ev.append((tick,'up %d'%k))
            elif r.random()<0.04:
                held[k]=5+r.randrange(40); ev.append((tick,'down %d'%k))
        ev.append((tick,'eval balls'))
    for k,v in held.items():
        if v>0: ev.append((t+per,'up %d'%k))
    t+=per+5
ev.sort(key=lambda e:e[0])
for tk,c in ev: print(tk,c)
