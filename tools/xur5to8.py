"""Converts a XUI binary scene from the 2005-6 format (XUIB version 5, what
the 2.0.1888/6717 systems use) to the one current systems read (version 8, as
xam.xex 17559 does: it refuses anything else). The layout is in tools/xur8.py.
Two of the header counts size tables, not just the pool: c2 the value sets (one
per object with values) and c4 the compound slots.

    python tools/xur5to8.py <xui_schema.txt> <in.xur> <out.xur> [--hud]

The scene is read with tools/xur.py, then written again: the same strings,
vectors, quaternions and custom data (their tables carry over), floats and
colours moved into tables of their own, every value as a compact number.
Property numbers stay as they are: a current system's classes have the old
properties first, in the same order, and newer ones after (checked against
xam 17559's own definitions). Nothing is shared between objects (flag 8) or
compounds; xam doesn't need it.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xur  # noqa: E402

BOOL, INT, UINT, FLOAT, STRING, COLOR, VECTOR, QUAT, OBJECT, CUSTOM = range(1, 11)


def cnum(v):
    """xam's compact number (0x817DF530)."""
    v &= 0xFFFFFFFF
    if v < 0xF0:
        return bytes([v])
    if v < 0xF00:                       # (a first byte of FFh means a u32 follows)
        return bytes([0xF0 | (v >> 8), v & 0xFF])
    return b"\xFF" + struct.pack(">I", v)


def resolve(reader, node):
    """Makes a tree from tools/xur.py self-contained, so trees from several files
    can be merged: vectors, quaternions and custom data become their values
    ("V"/"Q" tuples, "C" bytes) instead of indices into that file's tables, and
    the timelines' string indices become strings."""
    d = reader.d

    def table(name):
        o, n = reader.sec.get(name, (0, 0))
        return d[o:o + n]

    vect, quat, cust = table("VECT"), table("QUAT"), table("CUST")

    def one(t, v):
        if t == VECTOR and isinstance(v, int):
            return ("V",) + struct.unpack(">3f", vect[12 * v:12 * v + 12])
        if t == QUAT and isinstance(v, int):
            return ("Q",) + struct.unpack(">4f", quat[16 * v:16 * v + 16])
        if t == CUSTOM and isinstance(v, int):
            n = struct.unpack(">I", cust[v:v + 4])[0]
            return ("C", bytes(cust[v:v + 4 + n]))
        return v

    def values(props, vals):
        for p in props:
            if p.name not in vals:
                continue
            v = vals[p.name]
            if p.type == OBJECT:
                vals[p.name] = values(p.sub, dict(v)) if isinstance(v, dict) else v
            elif isinstance(v, list):
                vals[p.name] = [one(p.type, e) for e in v]
            else:
                vals[p.name] = one(p.type, v)
        return vals

    def walk(n):
        if n.get("resolved"):
            return
        for level in reader.levels_base_first(n["class"]):
            values(level, n["props"])
        raw = n.get("raw_timelines")
        if raw:
            raw["frames"] = [(reader.s(ni) if ni else None, t, c, reader.s(ti) if ti else None)
                             for ni, t, c, ti in raw["frames"]]
            for tl in raw["timelines"]:
                tl["target"] = reader.s(tl["target"]) if tl["target"] else None
                tl["keys"] = [(t, i, e, [one(p.type, v) for (_, p), v in zip(tl["paths"], vals)])
                              for t, i, e, vals in tl["keys"]]
        n["resolved"] = True
        for c in n["children"]:
            walk(c)
    walk(node)
    return node


class Converter:
    def __init__(self, reader):
        self.r = reader
        self.strings = list(reader.strings)
        self.vect, self.vect_index = [], {}
        self.quat, self.quat_index = [], {}
        self.cust, self.cust_index = bytearray(), {}
        self.flot, self.flot_index = [], {}
        self.colr, self.colr_index = [], {}
        self.compounds = 0
        self.keyp, self.keyd, self.names = [], bytearray(), bytearray()
        self.n_keyd = self.n_names = 0
        # pool sizes (see xur8.py): objects, 12-byte and 4-byte allocations, timelines
        self.objects = 0
        self.with_props = 0             # objects with values: slots in xam's shared-values table
        self.entries12 = 0
        self.entries4 = 0
        self.timelines = 0

    # -- values
    def string_index(self, s):
        if s is None:
            return 0
        if s.startswith("<str ") and s.endswith(">"):
            return int(s[5:-1])         # how tools/xur.py shows an index it has no string for (0: none)
        if s not in self.strings:
            self.strings.append(s)      # (a merged scene's or a changed class's)
        return self.strings.index(s) + 1

    def table_index(self, table, index, data):
        if data not in index:
            index[data] = len(table)
            table.append(data)
        return index[data]

    def float_index(self, f):
        bits = struct.pack(">f", f)
        if bits not in self.flot_index:
            self.flot_index[bits] = len(self.flot)
            self.flot.append(bits)
        return self.flot_index[bits]

    def colour_index(self, c):
        v = int(c, 16) if isinstance(c, str) else c
        if v not in self.colr_index:
            self.colr_index[v] = len(self.colr)
            self.colr.append(v)
        return self.colr_index[v]

    def scalar(self, t, v):
        if t == BOOL:
            return bytes([1 if v else 0])
        if t in (INT, UINT):
            return cnum(v)
        if t == FLOAT:
            return cnum(self.float_index(v))
        if t == STRING:
            return cnum(self.string_index(v))
        if t == COLOR:
            return cnum(self.colour_index(v))
        if t == VECTOR and isinstance(v, tuple):
            return cnum(self.table_index(self.vect, self.vect_index, struct.pack(">3f", *v[1:])))
        if t == QUAT and isinstance(v, tuple):
            return cnum(self.table_index(self.quat, self.quat_index, struct.pack(">4f", *v[1:])))
        if t == CUSTOM and isinstance(v, tuple):
            at = self.cust_index.get(v[1])
            if at is None:
                at = self.cust_index[v[1]] = len(self.cust)
                self.cust += v[1]
            return cnum(at)
        if t in (VECTOR, QUAT, CUSTOM):
            return cnum(v)              # (unresolved: v5 table indices / offsets as they are)
        raise ValueError("type %d" % t)

    def value(self, p, v):
        if p.type == OBJECT:
            i = self.compounds
            self.compounds += 1
            body, n = self.block(p.sub, v)
            self.entries12 += n
            return cnum(i) + cnum(n) + body
        return self.scalar(p.type, v)

    def block(self, props, values):
        """-> (bytes, value entries) for one class's properties."""
        mask, out, n = 0, bytearray(), 0
        for i, p in enumerate(props):
            if p.name not in values:
                continue
            mask |= 1 << i
            v = values[p.name]
            if p.flags & 1:
                v = v if isinstance(v, list) else [v]
                out += cnum(len(v))
                for e in v:
                    out += self.value(p, e)
                n += len(v)
            else:
                out += self.value(p, v)
                n += 1
        return cnum(mask) + out, n

    # -- objects
    def obj(self, node):
        self.objects += 1
        out = bytearray()
        cls = node["class"]
        flags = 0
        props = bytearray()
        count = 0
        if node["props"]:
            flags |= 1
            self.with_props += 1
            for level in self.r.levels_base_first(cls):
                b, n = self.block(level, node["props"])
                props += b
                count += n
            self.entries12 += count
        if node["children"]:
            flags |= 2
        raw = node.get("raw_timelines")
        if raw is not None:
            flags |= 4
        out += cnum(self.string_index(cls)) + bytes([flags])
        if flags & 1:
            out += cnum(count) + props
        if flags & 2:
            out += cnum(len(node["children"]))
            for c in node["children"]:
                out += self.obj(c)
        if flags & 4:
            out += self.timeline_block(node, raw)
        return bytes(out)

    def timeline_block(self, node, raw):
        out = bytearray()
        out += cnum(len(raw["frames"]))
        if raw["frames"]:
            out += cnum(self.n_names)
            for ni, time, cmd, ti in raw["frames"]:
                ni = ni if isinstance(ni, int) else self.string_index(ni)
                ti = ti if isinstance(ti, int) else self.string_index(ti)
                self.names += cnum(ni) + cnum(time) + bytes([cmd])
                if cmd >= 2:
                    self.names += cnum(ti)
                self.n_names += 1
        if node["children"]:
            out += cnum(len(raw["timelines"]))
            for tl in raw["timelines"]:
                self.timelines += 1
                target = tl["target"]
                target = target if isinstance(target, int) else self.string_index(target)
                out += cnum(target) + cnum(len(tl["paths"]))
                self.entries12 += len(tl["paths"])          # per-property records
                self.entries4 += len(tl["paths"]) * 2
                for (n, level, i, subs, index), p in tl["paths"]:
                    out += bytes([n])
                    if n & 0x7F:
                        out += bytes([level, i] + subs)
                    if n & 0x80:
                        out += cnum(index)
                    self.entries4 += (n & 0x7F) + 1
                out += cnum(len(tl["keys"])) + cnum(self.n_keyd)
                for time, interp, ease, values in tl["keys"]:
                    self.keyd += cnum(time) + bytes([interp & 0x3F])
                    if interp == 2:
                        self.keyd += bytes(ease)
                    elif interp in (7, 10, 11, 12):
                        raise ValueError("interpolation %d has no version 5 form" % interp)
                    self.keyd += cnum(len(self.keyp))
                    self.n_keyd += 1
                    for ((_, _, _, _, _), p), v in zip(tl["paths"], values):
                        self.keyp.append(self.scalar(p.type, v))
        return bytes(out)

    # -- the file
    def convert(self, root):
        data = self.obj(root)
        # The tables this conversion built (resolve() made the values self-contained).
        vect = b"".join(self.vect)
        quat = b"".join(self.quat)
        cust = bytes(self.cust)
        strn = bytearray(struct.pack(">IH", sum(len(t) + 1 for t in self.strings), len(self.strings)))
        for t in self.strings:
            strn += t.encode("utf-8") + b"\0"
        sections = [
            (b"STRN", bytes(strn)),
            (b"VECT", vect),
            (b"QUAT", quat),
            (b"CUST", cust),
            (b"FLOT", b"".join(self.flot)),
            (b"COLR", b"".join(struct.pack(">I", c) for c in self.colr)),
            (b"KEYP", b"".join(self.keyp)),
            (b"KEYD", bytes(self.keyd)),
            (b"NAME", bytes(self.names)),
            (b"DATA", data),
        ]
        sections = [(n, b) for n, b in sections if b or n in (b"STRN", b"DATA")]
        # The pool (xur8.py): exact KEYP/KEYD/NAME counts, the rest with room to spare.
        # c2 and c4 also size xam's table of value sets (one per object with values,
        # which flag 8 can refer back to) and of compounds (0x817DFB68): both must
        # hold every slot, or xam writes through a null table (0.0.6's crash).
        counts = [0] * 12
        counts[0] = self.objects + 8
        counts[1] = self.entries12 + 64
        counts[2] = self.with_props
        counts[4] = self.compounds
        counts[5] = self.entries4 + 256
        counts[7] = self.timelines + 8
        counts[8] = len(self.keyp)
        counts[9] = self.n_keyd
        counts[10] = self.n_names
        counts[11] = 64
        head_counts = b"".join(cnum(c) for c in counts)
        table_at = 20 + len(head_counts)
        pos = table_at + 12 * len(sections)
        table, body = bytearray(), bytearray()
        for n, b in sections:
            table += struct.pack(">4sII", n, pos, len(b))
            body += b
            pos += len(b)
        total = table_at + len(table) + len(body)
        head = b"XUIB" + struct.pack(">IIHIH", 8, 0, 14, total, len(sections))
        return head + head_counts + bytes(table) + bytes(body)


def convert(data, schema, extra=None):
    r = xur.Reader(data, schema, extra)
    root = r.obj()
    o, s = r.sec["DATA"]
    if r.pos != o + s:
        raise ValueError("version 5 DATA read to %#x of %#x" % (r.pos, o + s))
    return Converter(r).convert(resolve(r, root))


if __name__ == "__main__":
    extra = {}
    if "--hud" in sys.argv:
        import guideskin
        extra = {"HUDScene": guideskin.HUD_SCENE}
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = convert(open(args[1], "rb").read(), xur.Schema(args[0]), extra)
    open(args[2], "wb").write(out)
    print("version 8: %d bytes" % len(out))
