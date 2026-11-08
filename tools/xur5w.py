"""Writes a XUI binary scene in the 2005-6 format (XUIB version 5), the one
SlopDash's XUI runtime reads (it refuses tools/xur5to8.py's version 8:
8030000D), from a tree read by tools/xur.py.

    python tools/xur5w.py <xui_schema.txt> <in.xur> <out.xur>    (a round trip)

The layout is tools/xur.py's, written back. Values may be as the reader gave
them (table indices) or resolved (tools/xur5to8.resolve: ("V", x, y, z),
("Q", ...), ("C", bytes)); either way the tables are written again, the
reader's first and new entries after. A file with the header's optional block
(flags bit 0) gets its ten counts recomputed, worked out against 1888's
dashmain.xur: objects, stored values (each list element and nested block
counted), value sets (objects with values plus nested blocks), timeline key
values, timeline path bytes, timeline paths, keys, timelines, named frames,
and elements with children.
"""
import struct
import sys

import xur

BOOL, INT, UINT, FLOAT, STRING, COLOR, VECTOR, QUAT, OBJECT, CUSTOM = range(1, 11)


class Writer:
    def __init__(self, reader):
        self.r = reader
        d = reader.d
        self.strings = list(reader.strings)
        self.str_index = {}
        for i, s in enumerate(self.strings):
            self.str_index.setdefault(s, i + 1)

        def table(name):
            o, n = reader.sec.get(name, (0, 0))
            return bytearray(d[o:o + n])

        self.vect, self.quat, self.cust = table("VECT"), table("QUAT"), table("CUST")
        self.vect_index = {bytes(self.vect[i:i + 12]): i // 12 for i in range(len(self.vect) - 12, -1, -12)}
        self.quat_index = {bytes(self.quat[i:i + 16]): i // 16 for i in range(len(self.quat) - 16, -1, -16)}
        self.cust_index = {}
        magic, ver, self.flags, self.tool, size, nsec = struct.unpack(">4sIIHIH", d[:20])
        # Each property list's mask width in the file read (read it again, noting them).
        self.mask_width = {}
        widths = self.mask_width

        class Noting(xur.Reader):
            def block(self, props):
                x = self.d[self.pos]
                if x:
                    widths[tuple(p.name for p in props)] = x & 7
                return xur.Reader.block(self, props)

        Noting(d, reader.schema, reader.extra).obj()
        self.c = dict(objects=0, entries=0, sets=0, keyvals=0, pathbytes=0, paths=0, keys=0, timelines=0,
                      frames=0, parents=0)

    # -- values
    def string(self, s):
        if s is None:
            return 0
        if isinstance(s, int):
            return s
        if s.startswith("<str ") and s.endswith(">"):
            return int(s[5:-1])
        if s not in self.str_index:
            self.strings.append(s)
            self.str_index[s] = len(self.strings)
        return self.str_index[s]

    def vector(self, v):
        if not isinstance(v, tuple):
            return v
        b = struct.pack(">3f", *v[1:])
        if b not in self.vect_index:
            self.vect_index[b] = len(self.vect) // 12
            self.vect += b
        return self.vect_index[b]

    def quaternion(self, v):
        if not isinstance(v, tuple):
            return v
        b = struct.pack(">4f", *v[1:])
        if b not in self.quat_index:
            self.quat_index[b] = len(self.quat) // 16
            self.quat += b
        return self.quat_index[b]

    def custom(self, v):
        if not isinstance(v, tuple):
            return v
        if v[1] not in self.cust_index:
            # The reader's own data is found where it already is.
            at = bytes(self.cust).find(v[1])
            if at < 0:
                at = len(self.cust)
                self.cust += v[1]
            self.cust_index[v[1]] = at
        return self.cust_index[v[1]]

    def scalar(self, t, v):
        if t == STRING:
            return struct.pack(">H", self.string(v))
        if t == BOOL:
            return bytes([1 if v else 0])
        if t == INT:
            return struct.pack(">i", v)
        if t == FLOAT:
            return struct.pack(">f", v)
        if t == COLOR:
            return struct.pack(">I", int(v, 16) if isinstance(v, str) else v)
        if t == VECTOR:
            return struct.pack(">I", self.vector(v))
        if t == QUAT:
            return struct.pack(">I", self.quaternion(v))
        if t == CUSTOM:
            return struct.pack(">I", self.custom(v))
        if t == UINT:
            return struct.pack(">I", v)
        raise ValueError("type %d" % t)

    def block(self, props, values):
        """-> (bytes, its own entries, entries counting those nested) for one class
        level's (or compound's) properties: a nested block's own count is
        stored before it; an object's, everything counted, before its blocks."""
        bits, out, n, deep = [], bytearray(), 0, 0
        for i, p in enumerate(props):
            if p.name not in values:
                continue
            bits.append(i)
            v = values[p.name]
            if p.type == OBJECT:
                body, sub, subdeep = self.block(p.sub, v if isinstance(v, dict) else {})
                out += struct.pack(">H", sub) + (body if sub else b"")
                self.c["sets"] += 1
                n += 1
                deep += 1 + subdeep
            elif p.flags & 1:
                v = v if isinstance(v, list) else [v]
                out += bytes([len(v)])
                for e in v:
                    out += self.scalar(p.type, e)
                n += len(v)
                deep += len(v)
            else:
                out += self.scalar(p.type, v)
                n += 1
                deep += 1
        if not bits:
            return b"\0", 0, 0
        # The mask's width: as the file being written had it for this level
        # (1888's XuiElement level: 2 bytes for today's 17 properties, its
        # compiler's 16), else enough for the level's every property.
        mlen = self.mask_width.get(tuple(p.name for p in props), (len(props) + 7) // 8)
        mlen = max(mlen, bits[-1] // 8 + 1)
        mask = 0
        for i in bits:
            mask |= 1 << i
        if len(bits) > 32 or mlen > 7:
            raise ValueError("block too large: %d values, mask %d bytes" % (len(bits), mlen))
        head = bytes([((len(bits) - 1) << 3) | mlen]) + mask.to_bytes(mlen, "big")
        return head + bytes(out), n, deep

    # -- objects
    def obj(self, node):
        self.c["objects"] += 1
        flags = 0
        if node["props"]:
            flags |= 1
        if node["children"]:
            flags |= 2
            self.c["parents"] += 1
        raw = node.get("raw_timelines")
        if raw is not None:
            flags |= 4
        out = bytearray(struct.pack(">HB", self.string(node["class"]), flags))
        if flags & 1:
            # Its own values counted (a nested block as one); the header's
            # total counts every nested value too.
            body, total, everything = bytearray(), 0, 0
            for level in self.r.levels_base_first(node["class"]):
                b, n, deep = self.block(level, node["props"])
                body += b
                total += n
                everything += deep
            self.c["entries"] += everything
            self.c["sets"] += 1
            out += struct.pack(">H", total) + body
        if flags & 2:
            out += struct.pack(">I", len(node["children"]))
            for c in node["children"]:
                out += self.obj(c)
        if flags & 4:
            out += self.timeline_block(node, raw)
        return bytes(out)

    def timeline_block(self, node, raw):
        out = bytearray(struct.pack(">I", len(raw["frames"])))
        for name, time, cmd, target in raw["frames"]:
            out += struct.pack(">HIBH", self.string(name), time, cmd, self.string(target))
            self.c["frames"] += 1
        if not node["children"]:
            return bytes(out)
        out += struct.pack(">I", len(raw["timelines"]))
        for tl in raw["timelines"]:
            self.c["timelines"] += 1
            out += struct.pack(">HI", self.string(tl["target"]), len(tl["paths"]))
            for (n, level, i, subs, index), p in tl["paths"]:
                out += bytes([n, level, i] + list(subs))
                if n & 0x80:
                    out += struct.pack(">I", index)
                self.c["paths"] += 1
                self.c["pathbytes"] += n & 0x7F
            out += struct.pack(">I", len(tl["keys"]))
            for time, interp, ease, values in tl["keys"]:
                out += struct.pack(">IB", time, interp) + bytes(ease)
                for ((_, _, _, _, _), p), v in zip(tl["paths"], values):
                    out += self.scalar(p.type, v)
                self.c["keys"] += 1
                self.c["keyvals"] += len(values)
        return bytes(out)

    # -- the file
    def write(self, root):
        data = self.obj(root)
        strn = bytearray()
        for s in self.strings:
            strn += struct.pack(">H", len(s)) + s.encode("utf-16-be")
        sections = [(b"STRN", bytes(strn)), (b"VECT", bytes(self.vect)), (b"QUAT", bytes(self.quat)),
                    (b"CUST", bytes(self.cust)), (b"DATA", data)]
        sections = [(n, b) for n, b in sections if b or n in (b"STRN", b"DATA")]
        extra = b""
        if self.flags & 1:
            c = self.c
            extra = struct.pack(">10I", c["objects"], c["entries"], c["sets"], c["keyvals"], c["pathbytes"],
                                c["paths"], c["keys"], c["timelines"], c["frames"], c["parents"])
        table_at = 20 + len(extra)
        pos = table_at + 12 * len(sections)
        table, body = bytearray(), bytearray()
        for n, b in sections:
            table += struct.pack(">4sII", n, pos, len(b))
            body += b
            pos += len(b)
        total = pos
        head = b"XUIB" + struct.pack(">IIHIH", 5, self.flags, self.tool, total, len(sections))
        return head + extra + bytes(table) + bytes(body)


if __name__ == "__main__":
    import json
    import os
    import xurlearn
    here = os.path.dirname(os.path.abspath(__file__))
    extra = {}
    for name in ("blades1888_classes.json", "blades_classes.json"):
        for c, v in json.load(open(os.path.join(here, name))).items():
            extra.setdefault(c, (v["base"], xurlearn.props(v["types"])))
    data = open(sys.argv[2], "rb").read()
    r = xur.Reader(data, xur.Schema(sys.argv[1]), extra)
    root = r.obj()
    out = Writer(r).write(root)
    open(sys.argv[3], "wb").write(out)
    print("%d bytes (in %d), %s" % (len(out), len(data), "identical" if out == data else "different"))
