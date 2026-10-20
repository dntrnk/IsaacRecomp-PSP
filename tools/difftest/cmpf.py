import sys,re
a=open(sys.argv[1]).read().splitlines(); b=open(sys.argv[2]).read().splitlines()
ign=set(sys.argv[4].split(',')) if len(sys.argv)>4 else set()
n=0; lim=int(sys.argv[3]) if len(sys.argv)>3 else 5
for i,(x,y) in enumerate(zip(a,b)):
    xs=x.split(' '); ys=y.split(' ')
    d=[(p,q) for p,q in zip(xs,ys) if p!=q and p.split('=')[0] not in ign]
    if d or len(xs)!=len(ys):
        print(i, ' '.join(xs[:4])); print('   ruffle/recomp:', d[:8]); n+=1
        if n>=lim: break
print('lines', len(a), len(b))
