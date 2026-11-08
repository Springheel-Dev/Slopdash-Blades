"""Reads XUI binary scenes in the format current systems use (XUIB version 8,
e.g. 17559's Guide), as xam.xex 17559 reads them (its reader at 0x817E0C10):

    python tools/xur8.py <xui_schema.txt> <file.xur>

Layout (big-endian):
  header: "XUIB", u32 8, u32 flags, u16 14, u32 file size, u16 section count,
          then 12 counts as compact numbers (see cnum) - they size one memory
          pool (dwords: 11*c0 + 3*(c1+c2+c3+c4+c9+c11) + 4*(2*c7+c6+c10) + c5 + c8);
          c8, c9, c10 are also the KEYP, KEYD and NAME entry counts; c2 sizes
          the table of value sets (every object with values takes a slot) and c4
          the compound slots (xam 0x817DFB68) - too small and xam corrupts memory
  section table: name, u32 offset (from the file start), u32 size
  STRN: u32 total characters (each string's length + 1), u16 count, then the
        strings, NUL-terminated UTF-8; referenced 1-based (0 = none)
  FLOT: floats; VECT: 3 floats; QUAT: 4 floats; COLR: u32 colours; CUST: raw
  KEYP: compact numbers (keyframe values); KEYD: keyframes; NAME: named frames
  DATA: the root object:
    class (string), u8 flags; 1: count of values, then one block per class from
    XuiElement down; 8: instead, the index of an earlier object's values;
    2: child count, children; 4: timelines.
    A block: a compact bit mask of the class's own properties present, then for
    each, a count if the property is indexed (flags & 1), then the values:
    bool a byte; int/uint a number; float FLOT index; string STRN index;
    colour COLR index; vector VECT index; quaternion QUAT index; custom CUST
    offset; compound: an index into the compound list (shared), and when new,
    its value count and a block of its own properties.
"""
import struct
import sys

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xur  # noqa: E402

BOOL, INT, UINT, FLOAT, STRING, COLOR, VECTOR, QUAT, OBJECT, CUSTOM = range(1, 11)
SECTIONS = ("STRN", "VECT", "QUAT", "CUST", "FLOT", "COLR", "KEYP", "KEYD", "NAME", "DATA")
# Keyframe interpolations and what follows their byte (xam's table at 0x8160B450).
EASE, VECT_EXTRA = (2,), (7, 10, 11, 12)


class Cursor:
    def __init__(self, d, pos, end):
        self.d, self.pos, self.end = d, pos, end

    def u8(self):
        v = self.d[self.pos]
        self.pos += 1
        return v

    def cnum(self):
        """xam 0x817DF530: < F0h one byte; F0h-FEh 12 bits in two; FFh then a u32."""
        b = self.u8()
        if b == 0xFF:
            v = struct.unpack(">I", self.d[self.pos:self.pos + 4])[0]
            self.pos += 4
            return v
        if b >= 0xF0:
            return ((b & 0x0F) << 8) | self.u8()
        return b


class Reader:
    def __init__(self, data, schema, extra=None):
        self.d = data
        self.schema = schema
        self.extra = extra or {}
        magic, ver, flags, tool, size, nsec = struct.unpack(">4sIIHIH", data[:20])
        if magic != b"XUIB" or ver != 8 or tool != 14:
            raise ValueError("not XUIB 8/14: %r %d %d" % (magic, ver, tool))
        if size != len(data):
            raise ValueError("size field %d, file %d" % (size, len(data)))
        c = Cursor(data, 20, len(data))
        self.counts = [c.cnum() for _ in range(12)]
        self.sec = {}
        for i in range(nsec):
            n, o, s = struct.unpack(">4sII", data[c.pos + 12 * i:c.pos + 12 * i + 12])
            self.sec[n.decode()] = (o, s)
        self.strings = self.read_strn()
        self.flot = self.table("FLOT", ">f")
        self.vect = self.table("VECT", ">3f")
        self.quat = self.table("QUAT", ">4f")
        self.colr = self.table("COLR", ">I")
        o, s = self.sec.get("CUST", (0, 0))
        self.cust = data[o:o + s]
        self.keyp = self.read_keyp()
        self.keyd = self.read_keyd()
        self.names = self.read_name()
        self.compounds = {}
        self.shared = []

    def table(self, name, fmt):
        o, s = self.sec.get(name, (0, 0))
        n = struct.calcsize(fmt)
        if s % n:
            raise ValueError("%s size %d not a multiple of %d" % (name, s, n))
        return [struct.unpack(fmt, self.d[o + i:o + i + n]) for i in range(0, s, n)]

    def read_strn(self):
        o, s = self.sec["STRN"]
        total, count = struct.unpack(">IH", self.d[o:o + 6])
        out, p, chars = [], o + 6, 0
        for _ in range(count):
            e = self.d.index(b"\0", p)
            t = self.d[p:e].decode("utf-8")
            out.append(t)
            chars += len(t) + 1
            p = e + 1
        if chars != total:
            raise ValueError("STRN total %d, strings %d" % (total, chars))
        return out

    def s(self, i):
        return self.strings[i - 1] if i else None

    def read_keyp(self):
        o, s = self.sec.get("KEYP", (0, 0))
        c = Cursor(self.d, o, o + s)
        return [c.cnum() for _ in range(self.counts[8])]

    def read_keyd(self):
        o, s = self.sec.get("KEYD", (0, 0))
        c = Cursor(self.d, o, o + s)
        out = []
        for _ in range(self.counts[9]):
            frame = c.cnum()
            b = c.u8()
            interp, hi = b & 0x3F, b >> 6
            extra = None
            if interp in EASE:
                extra = (c.u8(), c.u8(), c.u8())
            elif interp in VECT_EXTRA:
                extra = self.vect[c.cnum()]
            out.append({"frame": frame, "interp": interp, "hi": hi, "extra": extra, "keyp": c.cnum()})
        return out

    def read_name(self):
        o, s = self.sec.get("NAME", (0, 0))
        c = Cursor(self.d, o, o + s)
        out = []
        for _ in range(self.counts[10]):
            name, frame, cmd = self.s(c.cnum()), c.cnum(), c.u8()
            target = self.s(c.cnum()) if cmd >= 2 else None
            out.append((name, frame, cmd, target))
        return out

    # -- objects
    def levels(self, cls):
        r = xur.Reader.__new__(xur.Reader)
        r.schema, r.extra = self.schema, self.extra
        return r.levels_base_first(cls)

    def value(self, c, p):
        t = p.type
        if t == BOOL:
            return bool(c.u8())
        if t == INT:
            v = c.cnum()
            return v - (1 << 32) if v & 0x80000000 else v
        if t == UINT:
            return c.cnum()
        if t == FLOAT:
            return self.flot[c.cnum()][0]
        if t == STRING:
            return self.s(c.cnum())
        if t == COLOR:
            return "0x%08x" % self.colr[c.cnum()][0]
        if t == VECTOR:
            return self.vect[c.cnum()]
        if t == QUAT:
            return self.quat[c.cnum()]
        if t == CUSTOM:
            return ("cust", c.cnum())
        if t == OBJECT:
            i = c.cnum()                      # a slot in the compound list: filled, or new here
            if i in self.compounds:
                return self.compounds[i]
            c.cnum()                          # its value count
            v = self.block(c, p.sub)
            self.compounds[i] = v
            return v
        raise ValueError("type %d" % t)

    def block(self, c, props):
        mask = c.cnum()
        out = {}
        for i, p in enumerate(props):
            if not mask >> i & 1:
                continue
            if p.flags & 1:
                out[p.name] = [self.value(c, p) for _ in range(c.cnum())]
            else:
                out[p.name] = self.value(c, p)
        if mask >> len(props):
            raise xur.Unknown("mask %x beyond %d properties" % (mask, len(props)))
        return out

    def obj(self, c):
        at = c.pos
        cls = self.s(c.cnum())
        flags = c.u8()
        node = {"class": cls, "props": {}, "children": [], "at": at, "flags": flags}
        if flags & 1:
            c.cnum()                          # the value count
            for props in self.levels(cls):
                node["props"].update(self.block(c, props))
            self.shared.append(node["props"])
        elif flags & 8:
            node["props"] = self.shared[c.cnum()]
        if flags & 2:
            node["children"] = [self.obj(c) for _ in range(c.cnum())]
        if flags & 4:
            node["frames"], node["timelines"] = self.timelines(c, node)
        return node

    def timelines(self, c, node):
        frames = []
        nf = c.cnum()
        if nf:
            start = c.cnum()
            frames = self.names[start:start + nf]
        tls = []
        if node["children"]:
            for _ in range(c.cnum()):
                target = self.s(c.cnum())
                paths = []
                for _ in range(c.cnum()):
                    b = c.u8()
                    n, indexed = b & 0x7F, b & 0x80
                    path = [c.u8(), c.u8()] + [c.u8() for _ in range(n - 1)] if n else []
                    if indexed:
                        path.append(("index", c.cnum()))
                    paths.append(path)
                nk, ks = c.cnum(), c.cnum()
                keys = self.keyd[ks:ks + nk]
                tls.append({"target": target, "paths": paths, "keys": keys})
        return frames, tls

    def root(self):
        o, s = self.sec["DATA"]
        c = Cursor(self.d, o, o + s)
        node = self.obj(c)
        return node, c.pos - o, s


def dump(node, depth=0):
    p = node["props"]
    print("%s%s %s %s" % ("  " * depth, node["class"], p.get("Id", ""),
                         {k: v for k, v in p.items() if k != "Id"}))
    for c in node["children"]:
        dump(c, depth + 1)


if __name__ == "__main__":
    schema = xur.Schema(sys.argv[1])
    extra = {}
    if len(sys.argv) > 3 and sys.argv[3] == "--hud":
        import guideskin
        extra = {"HUDScene": guideskin.HUD_SCENE}
    r = Reader(open(sys.argv[2], "rb").read(), schema, extra)
    root, used, size = r.root()
    dump(root)
    print("counts", r.counts)
    print("tables: %d strings, %d floats, %d vectors, %d quats, %d colours, %d cust bytes, %d keyp, %d keyd, %d names"
          % (len(r.strings), len(r.flot), len(r.vect), len(r.quat), len(r.colr), len(r.cust), len(r.keyp),
             len(r.keyd), len(r.names)))
    print("DATA read %d of %d bytes" % (used, size))
