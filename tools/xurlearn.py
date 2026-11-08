"""Work out the layout of XUI classes a schema lacks, from the scenes that use them.

    python tools/xurlearn.py <xui_schema.txt> <out.json> <file.xur|package.xzp|resource.bin> ...

A .xur stores a class's own properties by position, so a stand-in class must
declare the same properties in the same order. For each unknown class this
finds the base class and the size of each property used (string = 2 bytes,
bool = 1, anything else = 4) from the first object of that class:
  - the object's stored property count must equal the base class's blocks
    plus the custom block exactly, and
  - the bytes after the object's properties must continue validly;
then whole files are re-decoded to choose between whatever survives.
Knowledge is shared across files and saved as JSON for tools/gen_classes.py.
"""
import itertools
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import xur  # noqa: E402

TYPES = [xur.STRING, xur.UINT, xur.BOOL]        # value sizes 2, 4, 1
MAX_BITS = 10


def read_xurs(path):
    """(name, bytes) for every .xur in a .xur / XUIZ package / extracted resource."""
    data = open(path, "rb").read()
    if data[:4] == b"XUIB":
        return [(os.path.basename(path), data)]
    if data[:4] != b"XUIZ":
        return []
    out = []
    _, _, _, hdr, count = struct.unpack(">IIIIH", data[4:22])
    p = 22
    for _ in range(count):
        size, off, nl = struct.unpack(">IIB", data[p:p + 9]); p += 9
        name = data[p:p + 2 * nl].decode("utf-16-be"); p += 2 * nl
        if name.lower().endswith(".xur"):
            out.append(("%s#%s" % (os.path.basename(path), name), data[22 + hdr + off:22 + hdr + off + size]))
    return out


def props(types):
    return [xur.Prop(i, "P%d" % i, t, 0) for i, t in enumerate(types)]


class Learner:
    def __init__(self, schema):
        self.schema = schema
        self.known = {}                 # class -> (base, [Prop])

    def reader(self, data):
        return xur.Reader(data, self.schema, dict(self.known))

    def parse(self, data):
        """(fully decoded?, furthest valid object start, error)."""
        r = self.reader(data)
        start, size = r.sec["DATA"]
        try:
            r.obj()
            if r.pos == start + size:
                return True, size, None
            return False, r.last_good - start, ValueError("stopped at %#x of %#x" % (r.pos - start, size))
        except (xur.Unknown, IndexError, ValueError, struct.error, KeyError) as e:
            return False, r.last_good - start, e

    def bases(self, cls):
        out = [c for c in self.schema.bases if self.schema.chain(c) and self.schema.props.get(c) is not None]
        return out + [c for c in self.known if c != cls]

    def local_candidates(self, data, at, cls):
        """Hypotheses (base, types) consistent with the object of class cls at offset `at`."""
        out = []
        for base in self.bases(cls):
            r = self.reader(data)
            r.extra[cls] = (base, [])
            r.pos = at
            try:
                r.u16(); flags = r.u8()
                if not flags & 1:
                    return [(base, [])]                 # no properties: any base will do; prefer first
                total = r.u16()
                std = 0
                for lv in r.levels_base_first(base):
                    before = len(r.d)
                    got = r.block(lv)
                    std += len(got)
                x = r.u8()
            except Exception:
                continue
            if x == 0:
                if total == std and self.continues(r, flags):
                    out.append((base, []))
                continue
            count, mlen = (x >> 3) + 1, x & 7
            if total != std + count or not 1 <= mlen <= 2:
                continue
            mask = int.from_bytes(r.d[r.pos:r.pos + mlen], "big")
            bits = [i for i in range(mlen * 8) if mask >> i & 1]
            if len(bits) != count or bits[-1] >= MAX_BITS:
                continue
            after_mask = r.pos + mlen
            for combo in itertools.product(TYPES, repeat=len(bits)):
                types = [xur.UINT] * (bits[-1] + 1)
                for b, t in zip(bits, combo):
                    types[b] = t
                r.pos = after_mask
                ok = True
                for b in bits:
                    try:
                        v = r.scalar(types[b])
                    except Exception:
                        ok = False
                        break
                    if types[b] == xur.STRING and isinstance(v, str) and v.startswith("<str"):
                        ok = False
                        break
                    if types[b] == xur.BOOL and v not in (True, False):
                        ok = False
                        break
                if ok and self.continues(r, flags):
                    out.append((base, types))
        return out

    @staticmethod
    def continues(r, flags):
        """Do the bytes after an object's properties look like a valid continuation?"""
        try:
            if flags & 2:
                n = r.u32()
                if not 0 < n < 2000:
                    return False
                ci, f = r.u16(), r.u8()
                return 0 < ci <= len(r.strings) and f <= 7
            if flags & 4:
                n = r.u32()
                return n < 2000
            if r.pos >= r.end:
                return True
            # next sibling, or the end of the parent (its frames count etc.): accept either
            ci, f = struct.unpack(">HB", r.d[r.pos:r.pos + 3])
            return (0 < ci <= len(r.strings) and f <= 7) or ci == 0
        except Exception:
            return False

    def learn(self, data, cls, at):
        cands = self.local_candidates(data, at, cls)
        if not cands:
            return None
        best = None
        for base, types in cands:
            self.known[cls] = (base, props(types))
            full, reach, err = self.parse(data)
            key = (full, reach, -len(types))
            if best is None or key > best[0]:
                best = (key, base, types, err)
        _, base, types, err = best
        self.known[cls] = (base, props(types))
        return base, types, err, len(cands)


def main():
    schema = xur.Schema(sys.argv[1])
    out = sys.argv[2]
    files = []
    for path in sys.argv[3:]:
        files += read_xurs(path)
    L = Learner(schema)
    if os.path.exists(out):
        for c, v in json.load(open(out)).items():
            L.known[c] = (v["base"], props(v["types"]))
    for _ in range(200):
        progress = False
        for name, data in files:
            full, reach, err = L.parse(data)
            cls = getattr(err, "cls", None)
            if full or cls is None:
                continue
            if cls in L.known:
                if not getattr(err, "extend", False):
                    continue
                old = L.known.pop(cls)          # learn again from this (wider) instance
                print("re-learning %s (was %s %d props)" % (cls, old[0], len(old[1])))
            res = L.learn(data, cls, err.at)
            if res is None:
                print("no hypothesis fits %s (%s at %#x)" % (cls, name, err.at))
                L.known[cls] = ("?", [])
                continue
            base, types, e2, n = res
            names = {xur.STRING: "str", xur.UINT: "u32", xur.BOOL: "bool"}
            print("%-30s : %-18s [%s]  (%d fits; %s)" % (cls, base, ", ".join(names[t] for t in types), n, name))
            progress = True
        if not progress:
            break
    # ClassOverride: XUI creates the named class in place of the element's own, so
    # it must derive from that class. It stores nothing of its own in the file.
    overrides = {}
    for name, data in files:
        r = L.reader(data)
        try:
            root = r.obj()
        except Exception:
            continue
        stack = [root]
        while stack:
            n = stack.pop()
            stack += n["children"]
            o = n["props"].get("ClassOverride")
            if o and o != n["class"]:
                overrides.setdefault(o, set()).add(n["class"])
    for o, bases in sorted(overrides.items()):
        if o in L.known or schema.chain(o):
            continue
        if len(bases) > 1:
            print("override %s is used on several classes %s; using %s" % (o, sorted(bases), sorted(bases)[0]))
        L.known[o] = (sorted(bases)[0], [])
    print("%d class overrides" % len([o for o in overrides if L.known.get(o, ("", [1]))[1] == []]))

    ok = [n for n, d in files if L.parse(d)[0]]
    print("\n%d of %d scenes decode completely" % (len(ok), len(files)))
    for name, data in files:
        full, reach, err = L.parse(data)
        if not full:
            print("  FAIL %-45s %s" % (name, err))
    json.dump({c: {"base": b, "types": [p.type for p in pr]} for c, (b, pr) in sorted(L.known.items())
               if b != "?"}, open(out, "w"), indent=1)


if __name__ == "__main__":
    main()
