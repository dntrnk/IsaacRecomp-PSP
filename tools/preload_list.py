"""Merges lists of seen graphics (written to the file given by ISAAC_LEARN_OUT when running in test mode) into
the preload list compiled into the executable (rt/preload_<game>.inc).
  python3 tools/preload_list.py rt/preload_<game>.inc list1.lst list2.lst ...
Line format: id kind abs variant q100 (q100: scale in quarter-octaves x100, relative to 720 px height
unless abs=1). One entry is kept per graphic and per distinct scale in handheld (720p) and docked (1080p) mode."""
import math, sys

out, ins = sys.argv[1], sys.argv[2:]
WINQ_1080 = 4 * math.log2(1080 / 720)
seen, keys = set(), []
for path in ins:
    for line in open(path):
        p = line.split()
        if len(p) != 5 or line.startswith('#'):
            continue
        i, kind, ab, var, q = map(int, p)
        q /= 100.0
        b720 = math.ceil(q - 1e-6)
        b1080 = b720 if ab else math.ceil(q + WINQ_1080 - 1e-6)
        k = (i, kind, ab, var, b720, b1080)
        if k in seen:
            continue
        seen.add(k)
        keys.append((i, kind, ab, var, int(round(q * 100))))
keys.sort()
with open(out, 'w') as f:
    f.write('// Graphics preloaded at startup (generated with tools/preload_list.py from play sessions).\n')
    f.write('// id, kind, abs, variant, scale (quarter-octaves x100; relative to 720 px height unless abs)\n')
    for i, kind, ab, var, q in keys:
        f.write('{%d, %d, %s, %d, %d},\n' % (i, kind, 'true' if ab else 'false', var, q))
print(len(keys), 'graphics')
