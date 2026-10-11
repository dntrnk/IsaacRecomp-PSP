"""Applies the 1080p patch (without the 60fps smoothing, which the native runtime does on its own).
Usage: patch1080.py <root DoAction.as>   (in-place edit)  |  patch1080.py --bin in.swf out.swf"""
import sys, struct
sys.path.insert(0, __file__.rsplit('/', 1)[0])
if sys.argv[1] != '--bin':
    p = sys.argv[1]; s = open(p, newline='').read()
    def rep(old, new):
        global s
        assert s.count(old) == 1, (old, s.count(old)); s = s.replace(old, new)
    rep('fscommand("allowscale","false");', 'fscommand("allowscale","true");')
    rep('''      _root._yscale = _root._xscale;\r
      _root.a.mapd();\r
      _root._x = (1 - _root._xscale / 125) * 800 / 2;\r
      _root._y = (1 - _root._yscale / 125) * 600 / 2;\r''', '''      _root._xscale = 225;\r
      _root._yscale = _root._xscale;\r
      _root.a.mapd();\r
      _root._x = (1440 - 640 * _root._xscale / 100) / 2;\r
      _root._y = (1080 - 480 * _root._yscale / 100) / 2;\r''')
    rep('''old = new flash.display.BitmapData(640,480,true,0);\r
trg.attachBitmap(_root.old,1);\r''', '''old = new flash.display.BitmapData(1440,1080,true,0);\r
old.drawOrig = old.draw;\r
old.draw = function(src)\r
{\r
   this.drawOrig(src,new flash.geom.Matrix(2.25,0,0,2.25,0,0));\r
};\r
trg._xscale = trg._yscale = 100 / 2.25;\r
trg.attachBitmap(_root.old,1);\r''')
    rep('map = new flash.display.BitmapData(450,150,true,0);', 'map = new flash.display.BitmapData(640,260,true,0);')
    open(p, 'w', newline='').write(s)
else:
    from swf import load, header, tags
    d = load(sys.argv[2])
    # DoAction of sprite 6685: 3rd resolution tier (1100x800 @1.667 -> 1500x1116 @2.25)
    raw = struct.pack('<d', 1.667); enc = b'\x06' + raw[4:] + raw[:4]
    assert d.count(enc) == 1
    p167 = d.find(enc)
    p1100 = d.rfind(b'\x07' + struct.pack('<i', 1100), 0, p167)
    p800 = d.find(b'\x07' + struct.pack('<i', 800), p1100, p167)
    assert 0 < p167 - p1100 < 64 and p1100 < p800 < p167
    struct.pack_into('<i', d, p1100 + 1, 1500); struct.pack_into('<i', d, p800 + 1, 1116)
    raw = struct.pack('<d', 2.25); d[p167 + 1:p167 + 9] = raw[4:] + raw[:4]
    # header: 1440x1080 stage (20 twips/px). Rewrites the RECT with 16 bits/field.
    _, fr, nf, p = header(d)
    def bits(vals, n):
        s = format(n, '05b') + ''.join(format(v & ((1 << n) - 1), '0%db' % n) for v in vals)
        s += '0' * (-len(s) % 8); return bytes(int(s[i:i+8], 2) for i in range(0, len(s), 8))
    rect = bits([0, 1440 * 20, 0, 1080 * 20], 16)
    d = d[:8] + rect + d[p - 4:]
    struct.pack_into('<I', d, 4, len(d))
    open(sys.argv[3], 'wb').write(d)
