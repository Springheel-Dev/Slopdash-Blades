"""Reader for XUI binary scenes (.xur, "XUIB" version 5).

The format was worked out against the XDK's own compiler (xui2bin.exe) and
a schema dumped from the XUI runtime on the console (xui_schema.txt).

    python tools/xur.py <xui_schema.txt> <file.xur> [--unknown Class=Base ...]

Properties are stored by position, so every class in an object's chain must be
known. A class the schema lacks (the dashboard's own) is reported with its raw
property block, which is how the custom classes' layouts are worked out.
"""
import struct
import sys

BOOL, INT, UINT, FLOAT, STRING, COLOR, VECTOR, QUAT, OBJECT, CUSTOM = range(1, 11)
SIZES = {BOOL: 1, INT: 4, UINT: 4, FLOAT: 4, STRING: 2, COLOR: 4, VECTOR: 4, QUAT: 4, CUSTOM: 4}


class Prop:
    def __init__(self, pid, name, type_, flags):
        self.id, self.name, self.type, self.flags = pid, name, type_, flags
        self.sub = []                       # compound properties


class Schema:
    """xui_schema.txt as dumped by SlopDash on the console."""

    def __init__(self, path):
        self.bases = {}                     # class -> parent class or None
        self.props = {}                     # class -> [Prop] (its own, in order)
        stack = []
        cls = None
        for line in open(path, encoding="latin-1"):
            line = line.rstrip("\r\n")
            if line.startswith("class "):
                if "not registered" in line:
                    cls = None
                    continue
                chain = [c.strip() for c in line[6:].split(":")]
                cls = chain[0]
                for a, b in zip(chain, chain[1:] + [None]):
                    self.bases.setdefault(a, b)
                self.props.setdefault(cls, [])
                stack = []
                continue
            if cls is None or not line.strip():
                continue
            depth = (len(line) - len(line.lstrip(" "))) // 2
            f = line.strip().split("\t")
            kv = dict(x.split("=", 1) for x in f[2:] if "=" in x)
            p = Prop(int(f[0]), f[1], int(kv["type"]), int(kv["flags"], 16 if kv["flags"].startswith("0x") else 10))
            stack = stack[:depth - 1]
            if depth == 1:
                self.props[cls].append(p)
            else:
                stack[-1].sub.append(p)
            stack.append(p)

    def chain(self, cls):
        """Classes from XuiElement down to cls, or None if any is unknown."""
        out = []
        while cls is not None:
            if cls not in self.bases:
                return None
            out.append(cls)
            cls = self.bases[cls]
        return out[::-1]


class Unknown(Exception):
    pass


class Reader:
    def __init__(self, data, schema, extra=None):
        self.d = data
        self.schema = schema
        self.extra = extra or {}            # custom class -> base class, plus its props
        magic, ver, flags, tool, size, nsec = struct.unpack(">4sIIHIH", data[:20])
        if magic != b"XUIB":
            raise ValueError("not XUIB")
        p = 20 + (40 if flags & 1 else 0)
        self.sec = {}
        for i in range(nsec):
            n, o, s = struct.unpack(">4sII", data[p + 12 * i:p + 12 * i + 12])
            self.sec[n.decode()] = (o, s)
        o, s = self.sec["STRN"]
        self.strings, q = [], o
        while q < o + s:
            n = struct.unpack(">H", data[q:q + 2])[0]
            self.strings.append(data[q + 2:q + 2 + 2 * n].decode("utf-16-be"))
            q += 2 + 2 * n
        self.pos, self.end = self.sec["DATA"]
        self.end += self.pos
        self.unknown = []                   # (class, offset, raw bytes)
        self.last_good = self.pos

    # -- primitives
    def u8(self):
        v = self.d[self.pos]; self.pos += 1; return v

    def u16(self):
        v = struct.unpack(">H", self.d[self.pos:self.pos + 2])[0]; self.pos += 2; return v

    def u32(self):
        v = struct.unpack(">I", self.d[self.pos:self.pos + 4])[0]; self.pos += 4; return v

    def s(self, i):
        return self.strings[i - 1] if 0 < i <= len(self.strings) else "<str %d>" % i

    # -- properties
    def block(self, props):
        x = self.u8()
        if x == 0:
            return {}
        mlen, count = x & 7, (x >> 3) + 1
        mask = int.from_bytes(self.d[self.pos:self.pos + mlen], "big"); self.pos += mlen
        out = {}
        bits = [i for i in range(mlen * 8) if mask >> i & 1]
        if len(bits) != count:
            raise ValueError("mask %x doesn't match count %d at %#x" % (mask, count, self.pos))
        for i in bits:
            if i >= len(props):
                raise Unknown("property bit %d beyond %d known" % (i, len(props)))
            p = props[i]
            out[p.name] = self.value(p)
        return out

    def value(self, p):
        if p.type == OBJECT:
            n = self.u16()
            return self.block(p.sub) if n else {}
        if p.flags & 1:                     # indexed: a count, then that many values
            n = self.u8()
            return [self.scalar(p.type) for _ in range(n)]
        return self.scalar(p.type)

    def scalar(self, t):
        if t == STRING:
            return self.s(self.u16())
        if t == BOOL:
            return bool(self.u8())
        if t in (INT,):
            return struct.unpack(">i", struct.pack(">I", self.u32()))[0]
        if t == FLOAT:
            return struct.unpack(">f", struct.pack(">I", self.u32()))[0]
        if t == COLOR:
            return "0x%08x" % self.u32()
        if t in (VECTOR, QUAT, CUSTOM, UINT):
            return self.u32()
        raise ValueError("type %d" % t)

    # -- objects
    def obj(self, depth=0, out=None):
        start = self.pos
        ci = self.u16()
        cls = self.s(ci)
        flags = self.u8()
        if not (0 < ci <= len(self.strings)) or flags > 7:
            raise ValueError("garbage object at %#x" % start)
        self.last_good = start
        node = {"class": cls, "props": {}, "children": [], "at": start, "has_children": bool(flags & 2)}
        if flags & 1:
            self.u16()                      # total property count (all classes)
            name = cls
            while self.schema.chain(name) is None and name in self.extra:
                name = self.extra[name][0]
            chain = self.schema.chain(name)
            if chain is None:
                e = Unknown("class %s at %#x" % (cls, start))
                e.cls, e.at = cls, start
                raise e
            for props in self.levels_base_first(cls):
                try:
                    node["props"].update(self.block(props))
                except Unknown as e:
                    # A custom class used with more properties than learned so far.
                    if props is not self.schema.props.get(cls) and not hasattr(e, "cls"):
                        owner = [c for c, (b, pr) in self.extra.items() if pr is props]
                        e.cls, e.at, e.extend = (owner[0] if owner else cls), start, True
                    raise
        if flags & 2:
            for _ in range(self.u32()):
                node["children"].append(self.obj(depth + 1))
        if flags & 4:
            node["frames"], node["timelines"] = self.timelines(node)
        return node

    def levels_base_first(self, cls):
        """Own-property lists from XuiElement down to cls (custom classes may stack)."""
        custom = []
        while self.schema.chain(cls) is None and cls in self.extra:
            custom.append(self.extra[cls][1])
            cls = self.extra[cls][0]
        chain = self.schema.chain(cls)
        if chain is None:
            raise Unknown("class %s" % cls)
        return [self.schema.props.get(c, []) for c in chain] + custom[::-1]

    def class_props(self, cls):
        return self.levels_base_first(cls)[::-1]

    def _old_class_props(self, cls):
        """[own props of cls, of its parent, ...] (level 0 = cls itself)."""
        name = cls
        levels = []
        if self.schema.chain(cls) is None and cls in self.extra:
            levels.append(self.extra[cls][1])
            name = self.extra[cls][0]
        chain = self.schema.chain(name)
        if chain is None:
            raise Unknown("class %s (timeline target)" % cls)
        return levels + [self.schema.props.get(c, []) for c in reversed(chain)]

    def timelines(self, node):
        # Named frames: name, time, command (0 none, 1 stop, 2+ goto-style with a target).
        frames = []
        raw = {"frames": [], "timelines": []}      # as stored (string indices etc.), for converting
        node["raw_timelines"] = raw
        for _ in range(self.u32()):
            ni = self.u16()
            name, time, cmd = self.s(ni), self.u32(), self.u8()
            target = None
            if True:                        # every frame carries a target string (0 = none)
                t = self.u16()
                target = self.s(t) if t else None
            frames.append((name, time, cmd, target))
            raw["frames"].append((ni, time, cmd, t))
        ids = {}

        # Targets are looked up nearest first: direct children, then deeper.
        level = node["children"]
        while level:
            nxt = []
            for c in level:
                ids.setdefault(c["props"].get("Id"), c)
                nxt += c["children"]
            level = nxt
        timelines = []
        # Timelines animate children, so the count is only stored for an element with children.
        for _ in range(self.u32() if node["has_children"] else 0):
            ti = self.u16()
            target = self.s(ti) if ti else ""
            el = ids.get(target) if ti else node        # index 0: the object itself
            if el is None:
                raise Unknown("timeline target %s not found at %#x" % (target, self.pos))
            levels = self.class_props(el["class"])
            props = []
            rawpaths = []
            for _ in range(self.u32()):
                n = self.u8()
                level, i = self.u8(), self.u8()
                rawpath = [n, level, i, [], None]
                if level >= len(levels) or i >= len(levels[level]):
                    raise Unknown("timeline on %s (%s): level %d prop %d not in schema, at %#x"
                                  % (target, el["class"], level, i, self.pos))
                p = levels[level][i]
                path = [p.name]
                indexed, n = n & 0x80, n & 0x7F  # bit 7: a u32 array index follows the path
                for _ in range(n - 1):
                    j = self.u8()
                    rawpath[3].append(j)
                    if j >= len(p.sub):
                        raise Unknown("timeline on %s (%s): %s has no sub-property %d (path so far %s), at %#x"
                                      % (target, el["class"], p.name, j, path, self.pos))
                    p = p.sub[j]
                    path.append(p.name)
                if indexed:
                    rawpath[4] = self.u32()
                    path[-1] += "[%d]" % rawpath[4]
                props.append((".".join(path), p))
                rawpaths.append((rawpath, p))
            keys = []
            rawkeys = []
            for _ in range(self.u32()):
                time, interp, e1, e2, e3 = self.u32(), self.u8(), self.u8(), self.u8(), self.u8()
                values = [self.scalar(p.type) for _, p in props]
                keys.append((time, interp, values))
                rawkeys.append((time, interp, (e1, e2, e3), values))
            timelines.append((target, [n for n, _ in props], keys))
            raw["timelines"].append({"target": ti, "paths": rawpaths, "keys": rawkeys})
        return frames, timelines


def dump(node, depth=0):
    p = node["props"]
    print("%s%s %s %s" % ("  " * depth, node["class"], p.get("Id", ""),
                         {k: v for k, v in p.items() if k != "Id"}))
    for c in node["children"]:
        dump(c, depth + 1)


if __name__ == "__main__":
    schema = Schema(sys.argv[1])
    r = Reader(open(sys.argv[2], "rb").read(), schema)
    root = r.obj()
    dump(root)
    print("consumed %d of %d DATA bytes" % (r.pos - r.sec["DATA"][0], r.sec["DATA"][1]))
