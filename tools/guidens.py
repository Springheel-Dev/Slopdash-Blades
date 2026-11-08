"""Gives the Blades Guide's visuals names of their own, so they load next to the
system's skin instead of over it.

XUI keeps one visual per name for all of xam: the Blades skin's "XuiButton",
"XuiScene"... replaced 17559's for every system screen (Download Profile came up
in 6717's look, oversized), and its stand-ins replaced the visuals xam's own
Guide frame draws (HUD_Bladedark). With every Blades visual renamed PREFIX+name,
and the Guide's screens pointing at those names, nothing is shared: the system
skin stays loaded as it is and ours is added.

A control with no Visual of its own takes the visual named after its class (or
the nearest base class that has one); those get the prefixed name explicitly,
resolved the same way against the Blades skin.

Used by tools/guidepkg.py (the screens) and for the skin package (rename_skin).
"""
PREFIX = "RD_"


def is_control(reader, cls):
    try:
        levels = reader.levels_base_first(cls)
    except Exception:
        return False
    return any(p.name == "Visual" for level in levels for p in level)


def class_chain(reader, cls):
    """cls, its custom bases (reader.extra), then the schema's bases, nearest first."""
    out = []
    while cls is not None:
        out.append(cls)
        if cls in reader.extra and reader.schema.chain(cls) is None:
            cls = reader.extra[cls][0]
        else:
            cls = reader.schema.bases.get(cls)
    return out


def point(reader, node, names):
    """Points a tree's controls at the prefixed visuals (names: the Blades skin's)."""
    p = node["props"]
    for k in ("TransFrom", "TransTo", "TransBackFrom", "TransBackTo"):    # scene transitions are visuals too
        if p.get(k) in names:
            p[k] = PREFIX + p[k]
    if is_control(reader, node["class"]):
        v = p.get("Visual")
        if isinstance(v, str) and v:
            if v in names:
                p["Visual"] = PREFIX + v
        else:
            for c in class_chain(reader, node["class"]):
                if c in names:
                    p["Visual"] = PREFIX + c
                    break
    for c in node["children"]:
        point(reader, c, names)


def visual_names(root):
    return {v["props"].get("Id") for v in root["children"] if v["class"] == "XuiVisual"}


def rename_skin(reader, root):
    """Renames a skin's visuals and points the controls inside them at the new names."""
    names = visual_names(root)
    for v in root["children"]:
        if v["class"] == "XuiVisual":
            v["props"]["Id"] = PREFIX + v["props"]["Id"]
        for c in v["children"]:
            point(reader, c, names)
    return names


if __name__ == "__main__":
    # python tools/guidens.py <Blades skin.xur, v5> <skin package with its images> <out skin.xzp>
    # The package keeps its images; skin.xur becomes the renamed skin (version 8),
    # and the system skin copy an earlier package carried (system.xur) is left out.
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import guideskin
    import xur
    import xur5to8
    import xzp
    schema = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xui_schema.txt")
    r = xur.Reader(open(sys.argv[1], "rb").read(), xur.Schema(schema), {"HUDScene": guideskin.HUD_SCENE})
    root = xur5to8.resolve(r, r.obj())
    names = rename_skin(r, root)
    pkg = open(sys.argv[2], "rb").read()
    files = [(n, pkg[o:o + s]) for n, o, s in xzp.entries(pkg) if n not in ("skin.xur", "system.xur")]
    files.insert(0, ("skin.xur", xur5to8.Converter(r).convert(root)))
    open(sys.argv[3], "wb").write(xzp.build(files, 3))
    print("%d visuals renamed %s*; %d files" % (len(names), PREFIX, len(files)))
