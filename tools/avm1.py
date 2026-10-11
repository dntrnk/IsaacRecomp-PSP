"""AVM1 disassembly into an instruction list with the constant pool resolved."""
import struct
from swf import ACTION_NAMES

def cstr(d, q):
    e = d.index(0, q); return d[q:e].decode('latin1'), e + 1

class Ins:
    __slots__ = ('pos', 'op', 'name', 'args', 'end', 'body', 'body_range')
    def __repr__(s): return '%x %s %r' % (s.pos, s.name, s.args)

def decode(d, start, end, pool=None):
    """Decodes [start,end). Returns (Ins list, final pool). DefineFunction returns args as
    (name, params, flags, regcount, body_start, body_end). The body is NOT skipped: it stays inline, as in the bytecode."""
    out = []; a = start
    while a < end:
        c = d[a]; l = struct.unpack_from('<H', d, a + 1)[0] if c >= 0x80 else 0
        x = Ins(); x.pos = a; x.op = c; x.name = ACTION_NAMES.get(c, 'Unknown%02x' % c)
        q = a + 3; e = a + 3 + l if c >= 0x80 else a + 1
        x.end = e
        if c == 0x88:
            n = struct.unpack_from('<H', d, q)[0]; q += 2; pool = []
            for _ in range(n):
                s, q = cstr(d, q); pool.append(s)
            x.args = list(pool)
        elif c == 0x96:
            vals = []
            while q < e:
                ty = d[q]; q += 1
                if ty == 0: s, q = cstr(d, q); vals.append(('str', s))
                elif ty == 1: vals.append(('num', struct.unpack_from('<f', d, q)[0])); q += 4
                elif ty == 2: vals.append(('null', None))
                elif ty == 3: vals.append(('undef', None))
                elif ty == 4: vals.append(('reg', d[q])); q += 1
                elif ty == 5: vals.append(('bool', bool(d[q]))); q += 1
                elif ty == 6:
                    raw = bytes(d[q + 4:q + 8] + d[q:q + 4]); vals.append(('num', struct.unpack('<d', raw)[0])); q += 8
                elif ty == 7: vals.append(('num', struct.unpack_from('<i', d, q)[0])); q += 4
                elif ty == 8: vals.append(('str', pool[d[q]])); q += 1
                elif ty == 9: vals.append(('str', pool[struct.unpack_from('<H', d, q)[0]])); q += 2
                else: raise Exception('push type %d at %x' % (ty, q))
            x.args = vals
        elif c in (0x99, 0x9D):
            x.args = e + struct.unpack_from('<h', d, q)[0]
        elif c == 0x9B:
            nm, q = cstr(d, q); n = struct.unpack_from('<H', d, q)[0]; q += 2; ps = []
            for _ in range(n):
                p, q = cstr(d, q); ps.append((0, p))
            cs = struct.unpack_from('<H', d, q)[0]
            x.args = (nm, ps, 0, 0, e, e + cs)
        elif c == 0x8E:
            nm, q = cstr(d, q); n = struct.unpack_from('<H', d, q)[0]; rc = d[q + 2]
            fl = struct.unpack_from('<H', d, q + 3)[0]; q += 5; ps = []
            for _ in range(n):
                r = d[q]; p, q = cstr(d, q + 1); ps.append((r, p))
            cs = struct.unpack_from('<H', d, q)[0]
            x.args = (nm, ps, fl, rc, e, e + cs)
        elif c == 0x87: x.args = d[q]
        elif c == 0x81: x.args = struct.unpack_from('<H', d, q)[0]
        elif c == 0x8C: x.args = cstr(d, q)[0]
        elif c == 0x83:
            u, q2 = cstr(d, q); t, _ = cstr(d, q2); x.args = (u, t)
        elif c == 0x9F:
            fl = d[q]; x.args = (fl & 1, d[q + 1] | d[q + 2] << 8 if fl & 2 else 0)
        elif c == 0x9A: x.args = d[q]
        else: x.args = None
        out.append(x); a = e
    return out, pool
