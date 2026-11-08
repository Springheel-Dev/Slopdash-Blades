"""SlopDash theme packs (.pak, "SDPK"; src/core/pak.h): one file holding what
a theme reads from the user's dashboard files, taken out of their .xex modules
here so the console reads one file and decrypts nothing.

Blades' pack (blades1888.pak) holds:
  <module>/<resource>   every resource package of each 1888 module given
                        (dash, xam, hud, huduiskin, signin, ...), unchanged:
                        SlopDash upgrades the scenes as it loads them
  6717/<resource>       6717's dash.xex's dashmain, blademp and dashuisk
  dashmain5.xzp         the five-blade tab strip (tools/dashmain5.py)
  xenonclatin.xtt       1888's font, if given

    python tools/themepak.py blades <1888 folder> <6717 dash.xex> <dashmain5.xzp> <out.pak>
    python tools/themepak.py list <file.pak>
"""
import contextlib
import io
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import xexres  # noqa: E402

MAGIC = b"SDPK"
VERSION = 1
ALIGN = 16

# The 1888 modules Blades reads (src/platform/resources.cpp: dash and xam, then kModules).
MODULES_1888 = ("dash", "xam", "hud", "huduiskin", "signin", "createprofile", "gamerprofile", "friends",
                "voicemail", "quickchat", "feedback", "vk", "deviceselector", "minimediaplayer", "marketplace",
                "updater")
PACKAGES_6717 = ("dashmain", "blademp", "dashuisk")


def write(entries):
    """A pack's bytes from [(name, bytes)], in that order."""
    table = b""
    for name, _ in entries:
        n = name.encode("ascii")
        if len(n) > 255:
            raise ValueError("pack entry name too long: %s" % name)
        table += bytes([len(n)]) + n + b"\0" * 8      # (offset and size filled in below)
    pos = 12 + len(table)
    out, fixed = bytearray(), bytearray()
    places = []
    for _, data in entries:
        pos += -pos % ALIGN
        places.append((pos, len(data)))
        pos += len(data)
    for (name, _), (off, size) in zip(entries, places):
        n = name.encode("ascii")
        fixed += bytes([len(n)]) + n + struct.pack(">II", off, size)
    out += MAGIC + struct.pack(">II", VERSION, len(entries)) + fixed
    for (_, data), (off, _) in zip(entries, places):
        out += b"\0" * (off - len(out)) + data
    return bytes(out)


def read(data):
    """[(name, offset, size)] of a pack; ValueError if it isn't one this reads."""
    if data[:4] != MAGIC:
        raise ValueError("not a SlopDash theme pack")
    version, count = struct.unpack(">II", data[4:12])
    if version != VERSION:
        raise ValueError("theme pack version %d (this reads %d)" % (version, VERSION))
    p, out = 12, []
    for _ in range(count):
        n = data[p]
        name = data[p + 1:p + 1 + n].decode("ascii")
        off, size = struct.unpack(">II", data[p + 1 + n:p + 9 + n])
        if off + size > len(data):
            raise ValueError("theme pack's %s runs past its end" % name)
        out.append((name, off, size))
        p += 9 + n
    return out


def resources(xex_bytes):
    """[(resource name, bytes)] of an .xex, in its order."""
    with contextlib.redirect_stdout(io.StringIO()):
        image, _, res = xexres.xex_image(xex_bytes)
    return [(name, bytes(image[off:off + size])) for name, off, size in res]


def blades(modules, dash6717, dashmain5, font=None):
    """blades1888.pak's bytes. modules: {1888 module name ("dash"...): its .xex's
    bytes}, the ones given; dash6717: 6717's dash.xex; dashmain5: the tab strip."""
    entries = []
    for m in MODULES_1888:
        if m in modules:
            entries += [("%s/%s" % (m, name), data) for name, data in resources(modules[m])]
    have = dict(resources(dash6717))
    for name in PACKAGES_6717:
        if name not in have:
            raise ValueError("6717's dash.xex has no %s" % name)
        entries.append(("6717/" + name, have[name]))
    entries.append(("dashmain5.xzp", dashmain5))
    if font is not None:
        entries.append(("xenonclatin.xtt", font))
    return write(entries)


def main(args):
    if args[:1] == ["list"] and len(args) == 2:
        data = open(args[1], "rb").read()
        for name, off, size in read(data):
            print("%10d  %s" % (size, name))
        print("%d entries, %.1f MB" % (len(read(data)), len(data) / 1e6))
    elif args[:1] == ["blades"] and len(args) == 5:
        folder, d6717, five, out = args[1:]
        modules = {m: open(os.path.join(folder, m + ".xex"), "rb").read()
                   for m in MODULES_1888 if os.path.exists(os.path.join(folder, m + ".xex"))}
        font = os.path.join(folder, "xenonclatin.xtt")
        data = blades(modules, open(d6717, "rb").read(), open(five, "rb").read(),
                      open(font, "rb").read() if os.path.exists(font) else None)
        open(out, "wb").write(data)
        print("%s: %d entries, %.1f MB" % (out, len(read(data)), len(data) / 1e6))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
