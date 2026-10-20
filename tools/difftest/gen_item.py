import os, sys, random
# gen_item.py <item | p<col>> <seed> <ticks>: (p<col>: a pill 1-6 / card 7-28, 70-74 / trinket 29-61 instead, used with Q)
# start the game, god mode, an item pedestal under the player (picked up), then
# waves of enemies shot with auto-aim, random movement, the active item used every 90 ticks (charged), bombs;
# dumps balls every tick and the player's properties / _root.ups every 30 ticks
arg=sys.argv[1]; pick=arg.startswith('p'); it=int(arg.lstrip('p')); seed=int(sys.argv[2].lstrip('p')); n=int(sys.argv[3])
r=random.Random(seed)
here=os.path.dirname(os.path.abspath(__file__))
lines=[l.strip() for l in open(os.environ.get('START_SCRIPT') or os.path.join(here,'..','..','tests','start_game.txt')) if l.strip()]
ev=[(1000,'eval set _root.a.lasth 100000000'),(1000,'eval call _root.a create 320 300 0 0 0 0 %r'%((5.07+it*1e-9) if pick else (5.1+it/10000.0)))]
if pick: ev+=[(1090,'down 81'),(1093,'up 81')]   # use the pill / card (trinkets are just held)
for t in range(1001,1061):
    ev.append((t,'eval set _root.a.player.xp 320')); ev.append((t,'eval set _root.a.player.yp 300'))
waves=[[(10,120,160),(14,520,160),(11,320,420)],[(12,140,400),(16,500,420),(10,320,160)],[(25,120,160),(10,520,400),(14,320,160)]]
for w,base in enumerate(range(1060,1000+n,300)):
    for (tp,x,y) in waves[w%len(waves)]: ev.append((base,'eval call _root.a create %d %d 0 0 0 0 %d'%(x,y,tp)))
ev.append((1061,'eval aim 1'))
held={}; keys=[87,65,83,68]
for tick in range(1061,1000+n):
    if tick%30==0:
        ev.append((tick,'eval set _root.a.lasth 100000000'))
        ev.append((tick,'eval props _root.a.player'))
    if tick%150==0: ev.append((tick,'eval props _root.ups'))
    if tick%90==45: ev.append((tick,'eval set _root.itc 1')); ev.append((tick,'down 32'))
    if tick%90==47: ev.append((tick,'up 32'))
    if tick%240==100: ev.append((tick,'down 69'))
    if tick%240==102: ev.append((tick,'up 69'))
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
