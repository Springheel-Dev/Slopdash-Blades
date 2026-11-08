import struct, sys, os, collections
def entries(d):
    if d[:4] != b"XUIZ": return None
    ver, total, _, hdr, count = struct.unpack(">IIIIH", d[4:22])
    p = 22; out = []
    for _ in range(count):
        size, off, nl = struct.unpack(">IIB", d[p:p+9]); p += 9
        if ver >= 3:                    # newer packages (system 17559): 8-bit names
            name = d[p:p+nl].decode("latin-1"); p += nl
        else:
            name = d[p:p+2*nl].decode("utf-16-be"); p += 2*nl
        out.append((name, off, size))
    base = 22 + hdr
    return [(n, base + o, s) for n, o, s in out]
def build(files, ver=1):
    """An XUIZ from [(name, bytes)], laid out as Microsoft's are: a header, the
    directory, then the files back to back. Version 1 (the 2005-6 system) has
    UTF-16 names; version 3 (current systems, e.g. 17559's huduiskin) 8-bit."""
    assert ver in (1, 3)
    directory = b""
    off = 0
    for name, data in files:
        enc = name.encode("utf-16-be") if ver < 3 else name.encode("latin-1")
        directory += struct.pack(">IIB", len(data), off, len(name)) + enc
        off += len(data)
    total = 22 + len(directory) + off
    head = b"XUIZ" + struct.pack(">IIIIH", ver, total, 0, len(directory), len(files))
    return head + directory + b"".join(d for _, d in files)


def replace(d, changes, ver=None):
    """A copy of XUIZ d with some files' contents replaced: {name: bytes}, in its
    own version unless another is given."""
    files = [(n, changes.get(n, d[o:o + s])) for n, o, s in entries(d)]
    return build(files, ver or struct.unpack(">I", d[4:8])[0])


if __name__ == "__main__":
    for f in sys.argv[1:]:
        d = open(f, "rb").read(); es = entries(d)
        if es is None: continue
        exts = collections.Counter(os.path.splitext(n)[1].lower() for n, _, _ in es)
        print(os.path.basename(f), len(es), dict(exts), "| first data:", d[es[0][1]:es[0][1]+4])
