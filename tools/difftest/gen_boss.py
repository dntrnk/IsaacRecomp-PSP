import os, sys, random, re
# gen_boss.py <template> <seed> <boss ticks> [razor]: chapter by template, teleport into the boss room with that
# template, auto-aim + random movement, god mode; dumps balls every tick
tmpl=sys.argv[1]; seed=int(sys.argv[2]); per=int(sys.argv[3]); razor=sys.argv[4] if len(sys.argv)>4 else '0'
r=random.Random(seed)
alt={34:1,37:2,29:3,26:4,35:5,30:6,31:7,28:2,36:2,32:4,27:4,33:7,38:7,41:7,39:8,40:8,42:8}
m=re.match(r'^b(\d+)-',tmpl)
altchap=False
if m:
    n=int(m.group(1))
    if n<=8: chap=n; bosss=n
    else: chap=alt.get(n,8); bosss=n; altchap=True
elif tmpl.startswith('h'): f=int(tmpl[1:]); chap=min(8,max(1,2*f-1)) if f<5 else 4; bosss=22 if f==5 else f+8
elif tmpl.startswith('a'): n=int(tmpl[1:].split('-')[0]); chap=2*n-1 if n<=4 else 2*(n-4)-1; bosss=n+12
elif tmpl=='demon': chap=3; bosss=23
elif tmpl=='stan': chap=9; bosss=24
else: chap=8; bosss=None
start=os.environ.get('START_SCRIPT') or os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tests','start_game.txt')
ev=[(int(l.split()[0]),' '.join(l.split()[1:])) for l in open(start) if l.strip()]
t=1000
for i in range(min(chap,9)-1):
    ev.append((t,'eval call _root.a newstart 0')); t+=300
ev.append((t,'eval set _root.a.lasth 100000000'))
if altchap: ev.append((t,'eval set _root.altchap true'))
ev.append((t,'eval setat _root.levz _root.bossl %s'%tmpl))
if bosss is not None: ev.append((t,'eval set _root.bosss %d'%bosss))
ev.append((t,'eval copy _root.a.tar _root.bossl'))
ev.append((t,'eval set _root.a.telf 100000'))
ev.append((t,'eval set _root.a.fra 99999'))
ev.append((t+1,'eval _root.lev'))
ev.append((t+1,'eval _root.chaps'))
ev.append((t+3,'eval aim 1'))
keys=[87,65,83,68]; held={}
for tick in range(t+3,t+per):
    if tick%30==0:
        ev.append((tick,'eval set _root.a.lasth 100000000'))
        if razor!='0': ev.append((tick,'eval set _root.a.razor %s'%razor))
    for k in keys:
        if held.get(k,0)>0:
            held[k]-=1
            if held[k]==0: ev.append((tick,'up %d'%k))
        elif r.random()<0.03:
            held[k]=5+r.randrange(30); ev.append((tick,'down %d'%k))
    ev.append((tick,'eval balls'))
ev.append((t+per,'eval _root.bossd'))
ev.sort(key=lambda e:e[0])
for tk,c in ev: print(tk,c)
