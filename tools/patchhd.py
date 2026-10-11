"""High-resolution patch with a configurable scale (generalizes patch1080.py).
  patchhd.py --scale S <root DoAction.as>        (in-place edit of the FFDec-exported script)
  patchhd.py --scale S --bin in.swf out.swf      (stage header and, if S > 1.667, room layers)
S = 2.25 -> 1440x1080 (PC, isaac1080.swf); S = 1.5 -> 960x720 (Switch, isaac720.swf)."""
import sys, struct
sys.path.insert(0, __file__.rsplit('/', 1)[0])
args = sys.argv[1:]
S = float(args[args.index('--scale') + 1]); del args[args.index('--scale'):args.index('--scale') + 2]
W, H = int(round(640 * S)), int(round(480 * S))
def num(x): return ('%g' % x)
if args[0] != '--bin':
    p = args[0]; s = open(p, newline='').read()
    def rep(old, new):
        global s
        assert s.count(old) == 1, (old, s.count(old)); s = s.replace(old, new)
    rep('fscommand("allowscale","false");', 'fscommand("allowscale","true");')
    rep('''      _root._yscale = _root._xscale;\r
      _root.a.mapd();\r
      _root._x = (1 - _root._xscale / 125) * 800 / 2;\r
      _root._y = (1 - _root._yscale / 125) * 600 / 2;\r''', '''      _root._xscale = %s;\r
      _root._yscale = _root._xscale;\r
      _root.a.mapd();\r
      _root._x = (%d - 640 * _root._xscale / 100) / 2;\r
      _root._y = (%d - 480 * _root._yscale / 100) / 2;\r''' % (num(S * 100), W, H))
    rep('''old = new flash.display.BitmapData(640,480,true,0);\r
trg.attachBitmap(_root.old,1);\r''', '''old = new flash.display.BitmapData(%d,%d,true,0);\r
old.drawOrig = old.draw;\r
old.draw = function(src)\r
{\r
   this.drawOrig(src,new flash.geom.Matrix(%s,0,0,%s,0,0));\r
};\r
trg._xscale = trg._yscale = 100 / %s;\r
trg.attachBitmap(_root.old,1);\r''' % (W, H, num(S), num(S), num(S)))
    if S > 1.667:   # the minimap is drawn at _root's scale
        rep('map = new flash.display.BitmapData(450,150,true,0);', 'map = new flash.display.BitmapData(640,260,true,0);')
    open(p, 'w', newline='').write(s)
else:
    from swf import load, header
    d = load(args[1])
    if S > 1.667:
        # DoAction of sprite 6685: 3rd resolution tier (1100x800 @1.667 -> layers at scale S)
        raw = struct.pack('<d', 1.667); enc = b'\x06' + raw[4:] + raw[:4]
        assert d.count(enc) == 1
        p167 = d.find(enc)
        p1100 = d.rfind(b'\x07' + struct.pack('<i', 1100), 0, p167)
        p800 = d.find(b'\x07' + struct.pack('<i', 800), p1100, p167)
        assert 0 < p167 - p1100 < 64 and p1100 < p800 < p167
        gx, gy = int(round(1500 * S / 2.25)), int(round(1116 * S / 2.25))
        struct.pack_into('<i', d, p1100 + 1, gx); struct.pack_into('<i', d, p800 + 1, gy)
        raw = struct.pack('<d', S); d[p167 + 1:p167 + 9] = raw[4:] + raw[:4]
    # header: W x H stage (20 twips/px). Rewrites the RECT with 16 bits/field.
    _, fr, nf, p = header(d)
    def bits(vals, n):
        s = format(n, '05b') + ''.join(format(v & ((1 << n) - 1), '0%db' % n) for v in vals)
        s += '0' * (-len(s) % 8); return bytes(int(s[i:i+8], 2) for i in range(0, len(s), 8))
    rect = bits([0, W * 20, 0, H * 20], 16)
    d = d[:8] + rect + d[p - 4:]
    struct.pack_into('<I', d, 4, len(d))
    open(args[2], 'wb').write(d)
