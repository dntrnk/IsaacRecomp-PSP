"""Minimal SWF parser (just what the recomp needs): tags, sprites, AVM1 actions."""
import struct, zlib

class Reader:
    def __init__(s, d, p=0):
        s.d = d; s.p = p; s.bitbuf = 0; s.bitpos = 0
    def u8(s):
        v = s.d[s.p]; s.p += 1; s.bitpos = 0; return v
    def u16(s):
        v = struct.unpack_from('<H', s.d, s.p)[0]; s.p += 2; s.bitpos = 0; return v
    def s16(s):
        v = struct.unpack_from('<h', s.d, s.p)[0]; s.p += 2; s.bitpos = 0; return v
    def u32(s):
        v = struct.unpack_from('<I', s.d, s.p)[0]; s.p += 4; s.bitpos = 0; return v
    def cstr(s):
        e = s.d.index(0, s.p); v = s.d[s.p:e].decode('latin1'); s.p = e + 1; s.bitpos = 0; return v
    def align(s): s.bitpos = 0
    def ub(s, n):
        v = 0
        for _ in range(n):
            if s.bitpos == 0:
                s.bitbuf = s.d[s.p]; s.p += 1; s.bitpos = 8
            s.bitpos -= 1
            v = (v << 1) | ((s.bitbuf >> s.bitpos) & 1)
        return v
    def sb(s, n):
        v = s.ub(n)
        if n and v & (1 << (n - 1)): v -= 1 << n
        return v
    def rect(s):
        s.align(); n = s.ub(5); r = [s.sb(n) for _ in range(4)]; s.align(); return r
    def matrix(s):
        s.align(); a = d = 1.0; b = c = 0.0
        if s.ub(1):
            n = s.ub(5); a = s.sb(n) / 65536; d = s.sb(n) / 65536
        if s.ub(1):
            n = s.ub(5); b = s.sb(n) / 65536; c = s.sb(n) / 65536
        n = s.ub(5); tx = s.sb(n); ty = s.sb(n); s.align()
        return (a, b, c, d, tx, ty)
    def cxform(s, alpha):
        s.align(); ha = s.ub(1); hm = s.ub(1); n = s.ub(4)
        m = [256, 256, 256, 256]; a = [0, 0, 0, 0]; k = 4 if alpha else 3
        if hm: m[:k] = [s.sb(n) for _ in range(k)]
        if ha: a[:k] = [s.sb(n) for _ in range(k)]
        s.align(); return (m, a)

def load(path):
    d = open(path, 'rb').read()
    if d[:3] == b'CWS': d = d[:8] + zlib.decompress(d[8:])
    assert d[:3] == b'FWS'
    return bytearray(d)

def header(d):
    r = Reader(d, 8); rc = r.rect(); fr = r.u16() / 256; nf = r.u16()
    return rc, fr, nf, r.p

def tags(d, p, end):
    """Iterates (code, start_of_body, length)."""
    while p < end:
        h = struct.unpack_from('<H', d, p)[0]; p += 2
        t, l = h >> 6, h & 0x3f
        if l == 0x3f: l = struct.unpack_from('<I', d, p)[0]; p += 4
        yield t, p, l
        p += l
        if t == 0: break

ACTION_NAMES = {0x00:'End',0x04:'NextFrame',0x05:'PrevFrame',0x06:'Play',0x07:'Stop',0x08:'ToggleQuality',0x09:'StopSounds',
 0x0A:'Add',0x0B:'Subtract',0x0C:'Multiply',0x0D:'Divide',0x0E:'Equals',0x0F:'Less',0x10:'And',0x11:'Or',0x12:'Not',
 0x13:'StringEquals',0x14:'StringLength',0x15:'StringExtract',0x17:'Pop',0x18:'ToInteger',0x1C:'GetVariable',0x1D:'SetVariable',
 0x20:'SetTarget2',0x21:'StringAdd',0x22:'GetProperty',0x23:'SetProperty',0x24:'CloneSprite',0x25:'RemoveSprite',0x26:'Trace',
 0x27:'StartDrag',0x28:'EndDrag',0x29:'StringLess',0x2A:'Throw',0x2B:'CastOp',0x2C:'ImplementsOp',0x30:'RandomNumber',
 0x31:'MBStringLength',0x32:'CharToAscii',0x33:'AsciiToChar',0x34:'GetTime',0x35:'MBStringExtract',0x36:'MBCharToAscii',0x37:'MBAsciiToChar',
 0x3A:'Delete',0x3B:'Delete2',0x3C:'DefineLocal',0x3D:'CallFunction',0x3E:'Return',0x3F:'Modulo',0x40:'NewObject',0x41:'DefineLocal2',
 0x42:'InitArray',0x43:'InitObject',0x44:'TypeOf',0x45:'TargetPath',0x46:'Enumerate',0x47:'Add2',0x48:'Less2',0x49:'Equals2',
 0x4A:'ToNumber',0x4B:'ToString',0x4C:'PushDuplicate',0x4D:'StackSwap',0x4E:'GetMember',0x4F:'SetMember',0x50:'Increment',
 0x51:'Decrement',0x52:'CallMethod',0x53:'NewMethod',0x54:'InstanceOf',0x55:'Enumerate2',0x60:'BitAnd',0x61:'BitOr',0x62:'BitXor',
 0x63:'BitLShift',0x64:'BitRShift',0x65:'BitURShift',0x66:'StrictEquals',0x67:'Greater',0x68:'StringGreater',0x69:'Extends',
 0x81:'GotoFrame',0x83:'GetURL',0x87:'StoreRegister',0x88:'ConstantPool',0x8A:'WaitForFrame',0x8B:'SetTarget',0x8C:'GoToLabel',
 0x8D:'WaitForFrame2',0x8E:'DefineFunction2',0x8F:'Try',0x94:'With',0x96:'Push',0x99:'Jump',0x9A:'GetURL2',0x9B:'DefineFunction',
 0x9D:'If',0x9E:'Call',0x9F:'GotoFrame2'}

def iter_actions(d, a, end):
    """Iterates flat actions (pos, code, len_body). Does not descend into functions (the body follows inline)."""
    while a < end:
        c = d[a]
        if c == 0: yield a, 0, 0; a += 1; continue
        if c < 0x80: yield a, c, 0; a += 1
        else:
            l = struct.unpack_from('<H', d, a + 1)[0]; yield a, c, l; a += 3 + l

def action_blocks(d):
    """Returns a list of action blocks: (kind, owner, frame, start, end, extra)."""
    out = []
    rc, fr, nf, p = header(d)
    def walk(p, end, owner):
        frame = 0
        for t, b, l in tags(d, p, end):
            if t == 1: frame += 1
            elif t == 12: out.append(('frame', owner, frame, b, b + l, None))
            elif t == 59: out.append(('init', struct.unpack_from('<H', d, b)[0], 0, b + 2, b + l, None))
            elif t == 39:
                sid = struct.unpack_from('<H', d, b)[0]
                walk(b + 4, b + l, sid)
            elif t == 26 or t == 70:
                for ev in clip_actions(d, t, b, l):
                    out.append(('clip', owner, frame, ev[1], ev[2], ev[0]))
            elif t == 34:
                bid = struct.unpack_from('<H', d, b)[0]
                for cond, s, e in button2_actions(d, b, l):
                    out.append(('button', bid, 0, s, e, cond))
    walk(p, len(d), 0)
    return out

def parse_place(d, t, b, l):
    r = Reader(d, b); o = {}
    if t == 4:
        o['id'] = r.u16(); o['depth'] = r.u16(); o['matrix'] = r.matrix()
        if r.p < b + l: o['cx'] = r.cxform(False)
        o['move'] = False; return o
    fl = r.u8(); fl2 = r.u8() if t == 70 else 0
    o['depth'] = r.u16(); o['move'] = bool(fl & 1)
    if t == 70 and (fl2 & 8 or (fl2 & 16 and fl & 2)): o['classname'] = r.cstr()
    if fl & 2: o['id'] = r.u16()
    if fl & 4: o['matrix'] = r.matrix()
    if fl & 8: o['cx'] = r.cxform(True)
    if fl & 16: o['ratio'] = r.u16()
    if fl & 32: o['name'] = r.cstr()
    if fl & 64: o['clipdepth'] = r.u16()
    if t == 70:
        if fl2 & 1:
            n = r.u8(); o['filters'] = n
            o['filters_pos'] = r.p
            # filters are not parsed here (see the C++ side)
            return o
        if fl2 & 2: o['blend'] = r.u8()
        if fl2 & 4: o['cache'] = r.u8()
    if fl & 128: o['clipactions'] = True
    return o

def clip_actions(d, t, b, l):
    r = Reader(d, b)
    fl = r.u8(); fl2 = r.u8() if t == 70 else 0
    if not fl & 128: return []
    r.u16()
    if t == 70 and (fl2 & 8 or (fl2 & 16 and fl & 2)): r.cstr()
    if fl & 2: r.u16()
    if fl & 4: r.matrix()
    if fl & 8: r.cxform(True)
    if fl & 16: r.u16()
    if fl & 32: r.cstr()
    if fl & 64: r.u16()
    if t == 70:
        if fl2 & 1: return [('FILTERS-UNPARSED', 0, 0)]
        if fl2 & 2: r.u8()
        if fl2 & 4: r.u8()
    r.u16(); r.u32()  # reserved, all flags (swf6+: 32 bits)
    out = []
    while True:
        ev = r.u32()
        if ev == 0: break
        sz = r.u32(); s = r.p; key = None
        if ev & (1 << 17): key = r.u8()
        out.append((ev | ((key or 0) << 32), r.p, s + sz))
        r.p = s + sz
    return out

def button2_actions(d, b, l):
    r = Reader(d, b); r.u16(); r.u8(); off = r.u16()
    out = []
    if off == 0: return out
    p = b + 3 + off
    while True:
        sz = struct.unpack_from('<H', d, p)[0]; cond = struct.unpack_from('<H', d, p + 2)[0]
        end = p + sz if sz else b + l
        out.append((cond, p + 4, end))
        if sz == 0: break
        p += sz
    return out
