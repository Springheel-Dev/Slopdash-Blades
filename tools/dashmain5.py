"""Builds dashmain5.xzp: the launch dashboard's (2.0.1888) own tab strip,
dashmain.xur, with a fifth blade, the Marketplace, at the far left (the
user's choice: 1888's sizes, text and positions; 6717's Marketplace blade).

    python tools/dashmain5.py <upgraded 1888 dashmain.xur> <out dashmain5.xzp>

The input is 1888's dashmain.xur as SlopDash upgrades it (src/core/xur.cpp,
Upgrade1888). The output is a package holding the new dashmain.xur alone,
written in version 5 (tools/xur5w.py: SlopDash's XUI runtime refuses 8);
SlopDash loads it from the theme folder ahead of every other package named
dashmain (src/platform/resources.cpp), so the scene's images still come from
1888's own.

How the five blades are laid out (numbered as 6717's dashmain numbers them,
which SlopDash's code follows: Tab1 Marketplace, Tab2 Xbox Live .. Tab5
System, Tab6 the first-run setup; frames "1To2", "2Open"...):
  * Xbox Live .. System selected: everything as 1888 has it with that blade
    selected, moved one tab to the right (LEFT_SLOT, the spacing of 1888's
    left tabs), and the Marketplace's tab in the left slot that frees.
  * Marketplace selected: 1888's Xbox Live layout with every blade one place
    along (the Marketplace in Xbox Live's place, Xbox Live in Games'...) and
    System's tab in a new outermost right slot (1888's right spacing).
  * Every animation is 1888's own for the matching blade, moved or renamed
    the same way; its first and last keys are set to the resting layouts
    above, so nothing jumps between them.
New parts (none of 1888's file is lost): the Marketplace's label, its tab
shapes (left and right), its top piece, its colour parts (drawn with 6717's
skin, which SlopDash loads under the prefix M6: they're given it at run
time), its wallpaper and Tab1 (6717's Marketplace scene class, DashMPScene,
laid out as 1888's Xbox Live blade).
"""
import copy
import math
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xur  # noqa: E402
import xur5to8  # noqa: E402
import xur5w  # noqa: E402
import xurlearn  # noqa: E402
import xzp  # noqa: E402

SCHEMA = xur.Schema(os.path.join(HERE, "xui_schema.txt"))
EXTRA = {}
for name in ("blades1888_classes.json", "blades_classes.json"):
    for c, v in json.load(open(os.path.join(HERE, name))).items():
        EXTRA.setdefault(c, (v["base"], xurlearn.props(v["types"])))

# 1888's spacing: its left tab shapes sit at 25.6, 70.3, 114.1 (44.25 apart on
# average), its right ones at 804.6, 855.1, 905.1 (50.25), and the selected
# blade's parts step about 45 right for each tab to their left.
LEFT_SLOT = 44.25
RIGHT_SLOT = 50.25

# 1888's resting frames for blades 1..4 (Xbox Live .. System selected).
REST_FRAME = {1: 0, 2: 30, 3: 90, 4: 150}

# What stays where it is when the strip moves: the full-screen layers, the
# wings and edge highlights at the screen's sides, and the boot effects.
FIXED = {"Background", "black_cover", "white_cover", "WhiteBootCover", "bootGradient", "OOBE_background",
         "wing_left", "wing_rt_back", "wing_rt_front", "wing_left_front", "color_highlight_left",
         "color_highlight_rt", "bootshadowLT", "bootshadowRT", "bootburst1", "bootburst2", "bootburst3",
         "WingBootLeft", "WingBootRT", "blade_topshadow_left"}

# Each blade's own parts in 1888's file (1 Xbox Live .. 4 System); the
# Marketplace's (0) are new.
LABEL = {0: "blade_0_txt", 1: "blade_1_txt", 2: "blade_2_txt", 3: "blade_3_txt", 4: "blade_4_txt"}
TOP = {0: ["bladeMP_top_back", "bladeMP_top_jewel", "bladeMP_top_face "],
       1: ["blade_top_back", "blade_top_jewel", "blade_top_face "],
       2: ["blade2_top_back", "blade2_top_jewel1", "blade2_top_face "],
       3: ["blade3_top_back", "blade3_top_jewel", "blade3_top_face "],
       4: ["blade4_top_back", "blade4_top_jewel", "blade4_top_face "]}
PICTURE = {j: "Picture_blade%d" % j for j in range(5)}
OLD_TAB = {j: "Tab%d" % j for j in range(1, 6)}          # 1888: Tab1 Xbox Live .. Tab4 System, Tab5 setup
# The colour parts: shared by the blades, their visual says whose colour.
COLOUR = ["BG_color_1", "BG_color_2", "color_highlight_left", "color_highlight_rt"]
MP_COLOUR = {c: c + "_mp" for c in COLOUR}
# 6717's skin's Marketplace visuals, under the prefix SlopDash loads that skin
# with (M6): named with it (0.0.111 gave the parts the prefix at run time, and
# the timelines' own visual changes lost it: the blade drew grey).
MP_VISUAL = {"bgcolor": "M6blade_5_bgcolor", "highlight": "M6blade_5_highlight", "jewel": "M6blade_5_jewel"}


def read(path):
    r = xur.Reader(open(path, "rb").read(), SCHEMA, dict(EXTRA))
    root = r.obj()
    return r, xur5to8.resolve(r, root)


def find(node, id_):
    if node["props"].get("Id") == id_:
        return node
    for c in node["children"]:
        f = find(c, id_)
        if f:
            return f
    return None


def vec(v, dx=0.0, dy=0.0):
    return ("V", v[1] + dx, v[2] + dy, v[3])


class Track:
    """One element's timeline: its properties (path name -> raw path and Prop)
    and keys (time, interpolation, ease, {path name: value})."""

    def __init__(self):
        self.paths = {}
        self.order = []
        self.keys = []

    def add_path(self, name, raw):
        if name not in self.paths:
            self.paths[name] = raw
            self.order.append(name)


class Strip:
    def __init__(self, scene):
        self.scene = scene
        self.nodes = {c["props"].get("Id"): c for c in scene["children"]}
        raw = scene["raw_timelines"]
        self.tracks = {}
        for tl in raw["timelines"]:
            t = Track()
            names = []
            for (rawpath, p) in tl["paths"]:
                name = self.path_name(rawpath, p)
                t.add_path(name, (rawpath, p))
                names.append(name)
            for time, interp, ease, values in tl["keys"]:
                t.keys.append((time, interp, ease, dict(zip(names, values))))
            self.tracks[tl["target"]] = t

    @staticmethod
    def path_name(rawpath, p):
        n, level, i, subs, index = rawpath
        return "%d.%d.%s.%s:%s" % (level, i, ".".join(map(str, subs)), index, p.name)

    def prop_of(self, el, name):
        """A path's property name (its last part) for reading the element's own value."""
        return name.split(":", 1)[1]

    def value_at(self, el, frame):
        """{path name: value} for an element at a frame (its last key at or before
        it, else its own property values)."""
        t = self.tracks.get(el)
        out = {}
        if t:
            for name in t.order:
                v = None
                for time, interp, ease, vals in t.keys:
                    if time <= frame and name in vals:
                        v = vals[name]
                if v is None:
                    v = self.nodes[el]["props"].get(self.prop_of(el, name)) if el in self.nodes else None
                out[name] = v
        return out


# Each blade's own whoosh, as 1888 has it: its switch sounds go by the blade
# you arrive at (1888 dashmain Blade_Sounds: 1To2 dash_BladeSwitch_2, 2To1 _1,
# 2To3 _3, 3To2 _2, 3To4 _4, 4To3 _3), Xbox Live _1 .. System _4. Copied by
# analogy they came out one along. The
# Marketplace has none of its own (1888 had four blades; 6717's five share the
# same four, one per gap): arriving there plays 6717's for that move, _1.
ARRIVAL_SOUND = {1: "dash_BladeSwitch_1.xma", 2: "dash_BladeSwitch_1.xma", 3: "dash_BladeSwitch_2.xma",
                 4: "dash_BladeSwitch_3.xma", 5: "dash_BladeSwitch_4.xma"}


def arrival_sound(time, frames, value):
    """A Blade_Sounds key's file: in an "<a>To<b>" switch, blade b's whoosh."""
    if not (isinstance(value, str) and "BladeSwitch" in value):
        return value
    seg = None
    for name, t, _, _ in frames:
        if t <= time and "To" in name and not name.endswith("End"):
            seg = name
    if not seg:
        return value
    try:
        to = int(seg.split("To")[1])
    except ValueError:
        return value
    return ARRIVAL_SOUND.get(to, value)


def main(src=None, out=None):
    if src is None:             # (the command line; the importer passes them)
        src, out = sys.argv[1], sys.argv[2]
    r, root = read(src)
    scene = root["children"][0]
    s = Strip(scene)

    # ---- the new elements: copies of 1888's own, renamed --------------------------
    children = scene["children"]

    def clone(from_id, new_id, before=None, after=None, **props):
        n = copy.deepcopy(s.nodes[from_id])
        n["props"]["Id"] = new_id
        n["props"].update(props)
        n.pop("raw_timelines", None)
        at = [c["props"].get("Id") for c in children]
        if before:
            children.insert(at.index(before), n)
        else:
            children.insert(at.index(after) + 1, n)
        s.nodes[new_id] = n
        if from_id in s.tracks:          # same class: the source's property paths fit
            t = Track()
            for name in s.tracks[from_id].order:
                t.add_path(name, s.tracks[from_id].paths[name])
            s.tracks[new_id] = t
        return n

    clone("blade_1_grey_Left", "blade_0_grey_left", before="blade_1_grey_Left")
    # The new right tab is Xbox Live's (shown only with the Marketplace
    # selected): the nearest right tab, so drawn over the others, as 1888's
    # nearest (Games', blade_1_grey_rt) is.
    clone("blade_3_grey_rt", "blade_4_grey_rt", before="blade_1_txt")
    # (turning about the nearest tab's pivot, as Games' does: it's the nearest)
    for p in ("Pivot", "Rotation"):
        s.nodes["blade_4_grey_rt"]["props"][p] = s.nodes["blade_1_grey_rt"]["props"][p]
    clone("blade_1_txt", "blade_0_txt", after="blade_1_txt", Text="marketplace")
    # In 1888's order, back, jewel, face (each after the one before: inserted
    # all after blade4's, they came out reversed and the back hid the jewel,
    # so the Marketplace's selected tab drew grey, no blade).
    after = "blade4_top_face "
    for old, new in zip(TOP[1], TOP[0]):
        clone(old, new, after=after)
        after = new
    s.nodes["bladeMP_top_jewel"]["props"]["Visual"] = MP_VISUAL["jewel"]
    for c in COLOUR:
        clone(c, MP_COLOUR[c], after=c)
    for c in COLOUR:
        kind = "bgcolor" if c.startswith("BG") else "highlight"
        s.nodes[MP_COLOUR[c]]["props"]["Visual"] = MP_VISUAL[kind]
    clone("Picture_blade1", "Picture_blade0", before="Picture_blade1")

    # The tabs: 1888's Tab1..Tab5 become Tab2..Tab6; Tab1 is the Marketplace's,
    # a copy of the Xbox Live blade's (Tab1) with 6717's Marketplace scene class.
    rename = {OLD_TAB[j]: "Tab%d" % (j + 1) for j in range(1, 6)}
    tab_mp = copy.deepcopy(s.nodes["Tab1"])
    for old, new in sorted(rename.items(), key=lambda kv: -int(kv[0][3:])):
        s.nodes[old]["props"]["Id"] = new
    tab_mp["props"]["Id"] = "Tab1"
    tab_mp["props"]["Opacity"] = 0.0
    tab_mp["props"]["Show"] = False
    blade = tab_mp["children"][0]
    blade["props"]["Id"] = "scMarketplace"
    blade["props"]["ClassOverride"] = "DashMPScene"
    header = find(blade, "txt_Header") or find(blade, "labHeader")
    if header:
        header["props"]["Text"] = "Marketplace"
    # (its X legend stays Sign Out, as the Xbox Live blade's and the user's
    # screenshot of the Marketplace have it)
    at = [c["props"].get("Id") for c in children]
    children.insert(at.index("Tab2") + 1, tab_mp)        # 1888's order: Tab5 .. Tab1, then the sounds
    s.nodes = {c["props"].get("Id"): c for c in children}
    tracks = {}
    for k, t in s.tracks.items():
        tracks[rename.get(k, k)] = t
    s.tracks = tracks
    t1 = Track()
    for name in s.tracks["Tab2"].order:
        t1.add_path(name, s.tracks["Tab2"].paths[name])
    s.tracks["Tab1"] = t1
    TAB = {0: "Tab1", 1: "Tab2", 2: "Tab3", 3: "Tab4", 4: "Tab5"}

    # The blades' contents sit in their tabs at fixed places (1888's tabs only
    # fade in and out): Xbox Live .. System's move one tab right with their
    # blades; the Marketplace's stays where Xbox Live's was.
    for n in range(2, 6):
        for c in s.nodes["Tab%d" % n]["children"]:
            p = c["props"].get("Position")
            c["props"]["Position"] = vec(p, LEFT_SLOT) if isinstance(p, tuple) else ("V", LEFT_SLOT, 0.0, 0.0)

    # System has no right tabs: its right edge is the outer frame (wing_rt,
    # which stays put), and its B / A legend, moved with the rest, ran into it
    #. Those two stay where 1888 has them on the screen.
    for leg in ("legend_a", "legend_b"):
        n = find(s.nodes["Tab5"], leg)
        p = n["props"].get("Position") if n else None
        if isinstance(p, tuple):
            n["props"]["Position"] = vec(p, -LEFT_SLOT)

    sp = scene["props"]
    sp["TabCount"] = 6
    sp["DefaultFocus"] = "Tab2"

    # ---- resting layouts ----------------------------------------------------------
    old_rest = {j: {el: s.value_at(el, f) for el in s.tracks} for j, f in REST_FRAME.items()}

    def moved(el, vals, dx):
        if el in FIXED or not dx:
            return dict(vals)
        out = {}
        for name, v in vals.items():
            out[name] = vec(v, dx) if name.endswith(":Position") and isinstance(v, tuple) else v
        return out

    def shown(vals, show):
        out = dict(vals)
        for name in out:
            if name.endswith(":Show"):
                out[name] = show
            if name.endswith(":Opacity") and not show:
                out[name] = 0.0 if name.endswith(":Opacity") and False else out[name]
        return out

    def hidden_like(el, j):
        return shown(old_rest[j].get(el, {}), False)

    # Where the Marketplace's label sits as the leftmost tab, for 1..4 tabs on
    # the left: 1888's Xbox Live label there (1, 2, 3), the next one on (4).
    l1 = {k: old_rest[k]["blade_1_txt"] for k in (2, 3, 4)}
    l1[5] = {n: (vec(v, 0, 2 * (l1[4][n][2] - l1[3][n][2]) / 2) if n.endswith(":Position") else v)
             for n, v in l1[4].items()}
    l1[5] = {n: (("V", v[1], l1[4][n][2] + (l1[4][n][2] - l1[3][n][2]), v[3]) if n.endswith(":Position") else v)
             for n, v in l1[4].items()}

    def rest(n):
        """{element: {path: value}} with blade n selected (1 Marketplace .. 5 System)."""
        out = {}
        if n >= 2:
            j = n - 1
            for el in s.tracks:
                if el in old_rest[j]:
                    out[el] = moved(el, old_rest[j][el], LEFT_SLOT)
            out["blade_0_txt"] = dict(l1[n])
            # 1888's left tab shapes turn 0.2 degrees more for each tab further
            # from the selected blade (about a pivot far to the left, -2750,
            # 385): the cascade. The Marketplace's continues it (it sat level
            # with Xbox Live's, above the cascade).
            mp = dict(shown(old_rest[2]["blade_1_grey_Left"], True))
            angle = math.radians(0.2 * (n - 2)) / 2
            for nm in mp:
                if nm.endswith(":Rotation"):
                    mp[nm] = ("Q", 0.0, 0.0, math.sin(angle), math.cos(angle))
                elif nm.endswith(":Pivot"):
                    mp[nm] = ("V", -2750.0, 385.0, 0.0)
            out["blade_0_grey_left"] = mp
            out["blade_4_grey_rt"] = moved("blade_4_grey_rt", hidden_like("blade_3_grey_rt", 1), RIGHT_SLOT)
            for el in TOP[0]:
                out[el] = hidden_like(TOP[1][TOP[0].index(el)], 2)
            for c in COLOUR:
                out[MP_COLOUR[c]] = shown(moved(c, old_rest[j][c], LEFT_SLOT), False)
            out["Picture_blade0"] = shown(old_rest[2]["Picture_blade1"], False)
            out["Tab1"] = {nm: (0.0 if nm.endswith(":Opacity") else False if nm.endswith(":Show") else v)
                           for nm, v in old_rest[1]["Tab2"].items()}
            return out
        # The Marketplace selected: 1888's Xbox Live layout, every blade one along.
        base = old_rest[1]
        for el in s.tracks:
            if el in base:
                out[el] = dict(base[el])
        for m in range(0, 4):                    # new blade m (0 Marketplace .. 3 Media) <- 1888's m+1
            out[LABEL[m]] = dict(base[LABEL[m + 1]])
            for a, b in zip(TOP[m], TOP[m + 1]):
                out[a] = {nm: base[b].get(nm, base.get(a, {}).get(nm)) for nm in s.tracks[a].order}
            out[PICTURE[m]] = dict(base[PICTURE[m + 1]])
            out[TAB[m]] = dict(base[TAB[m + 1]]) if TAB[m + 1] in base else out.get(TAB[m])
        # System: its tab in a new outermost right slot, the rest of it hidden.
        sysl = dict(base["blade_4_txt"])
        for nm, v in sysl.items():
            if nm.endswith(":Position"):
                g, m3 = base["blade_4_txt"][nm], base["blade_3_txt"][nm]
                sysl[nm] = ("V", 2 * g[1] - m3[1], 2 * g[2] - m3[2], g[3])
        out[LABEL[4]] = sysl
        for el in TOP[4]:
            out[el] = shown(base[el], False)
        out[PICTURE[4]] = shown(base[PICTURE[4]], False)
        out[TAB[4]] = {nm: (0.0 if nm.endswith(":Opacity") else False if nm.endswith(":Show") else v)
                       for nm, v in base[TAB[4]].items()}
        out["blade_4_grey_rt"] = shown(moved("blade_4_grey_rt", base["blade_3_grey_rt"], RIGHT_SLOT), True)
        out["blade_0_grey_left"] = shown(base["blade_1_grey_Left"], False)
        # Its colours: Xbox Live's in 1888's layout are the Marketplace's here.
        for c in COLOUR:
            vals = base[c]
            vis = [nm for nm in vals if nm.endswith(":Visual")]
            live = vis and str(vals[vis[0]]).startswith("blade_1_")
            out[c] = shown(vals, False) if live else dict(vals)
            out[MP_COLOUR[c]] = shown(vals, bool(live))
            if vis:
                out[MP_COLOUR[c]][vis[0]] = MP_VISUAL["bgcolor" if c.startswith("BG") else "highlight"]
        return out

    REST = {n: rest(n) for n in range(1, 6)}

    # The right tabs keep one place each in every layout, as 1888's do (its
    # right tabs never move; only their labels rise as they near the selected
    # blade). The Marketplace's layout (1888's Xbox Live one, every blade one
    # along) had them up to 10 points off the others' (1888's moved one tab
    # right), and its tab shapes in other order: they jumped at the start of
    # every Marketplace / Xbox Live switch. Games', Media's and System's tabs stay
    # where the other layouts have them; Xbox Live's sits one tab nearer.
    def px(vals):
        return [v for nm, v in vals.items() if nm.endswith(":Position")][0][1]

    def at_x(vals, x):
        return {nm: (("V", x, v[2], v[3]) if nm.endswith(":Position") else v) for nm, v in vals.items()}
    rt_x = {k: px(REST[2]["blade_%d_grey_rt" % k]) for k in (1, 2, 3)}
    rt_x[4] = rt_x[1] - (rt_x[2] - rt_x[1])
    base1 = old_rest[1]
    RT_ALONG = {}                    # mine <- 1888's in the Marketplace's layout, dx
    for k, src in ((4, 1), (1, 2), (2, 3), (3, 3)):
        el, s_el = "blade_%d_grey_rt" % k, "blade_%d_grey_rt" % src
        RT_ALONG[el] = (s_el, rt_x[k] - px(base1[s_el]))
        REST[1][el] = shown(at_x(base1[s_el], rt_x[k]), True)
    for n in range(2, 6):
        REST[n]["blade_4_grey_rt"] = shown(at_x(REST[n]["blade_4_grey_rt"], rt_x[4]), False)
    # Their labels likewise: one x each on the right (Xbox Live's one tab nearer).
    lab_x = {m: px(REST[2][LABEL[m]]) for m in (2, 3, 4)}
    # Xbox Live's, in the nearest right tab, as 1888 places the nearest right
    # tab's label (Games', with Xbox Live selected) from its tab: 106.8 points.
    # (One label's step further left, as before, put it 110 from its tab: the
    # widest gap to the blade's inner edge of any label, 21 pixels against
    # 12-17.)
    lab_x[1] = rt_x[4] + (lab_x[2] - rt_x[1])
    LABEL_DX = {LABEL[m]: lab_x[m] - px(REST[1][LABEL[m]]) for m in (1, 2, 3, 4)}
    for m in (1, 2, 3, 4):
        REST[1][LABEL[m]] = at_x(REST[1][LABEL[m]], lab_x[m])
    # The right tab shapes turn 0.2 degrees more for each tab further from the
    # selected blade (1888's: Games' 0.2, Media's 0.4, System's 0.6 with Xbox
    # Live selected, about pivots far to the right, so a turn moves a tab's
    # edge about 6 points). The Marketplace's layout copied 1888's turns one
    # along: Xbox Live's tab sat at 0.6, System's at 0.6, and System's snapped
    # back 0.2 as the Marketplace / Xbox Live switch ended.
    RT_BLADE = {"blade_4_grey_rt": 2, "blade_1_grey_rt": 3, "blade_2_grey_rt": 4, "blade_3_grey_rt": 5}
    ROT = [nm for nm in s.tracks["blade_2_grey_rt"].order if nm.endswith(":Rotation")][0]
    TURN = {d: s.nodes["blade_%d_grey_rt" % d]["props"]["Rotation"] for d in (1, 2, 3)}

    def turned(q, degrees):
        a = 2 * math.atan2(q[3], q[4]) + math.radians(degrees)
        return ("Q", 0.0, 0.0, math.sin(a / 2), math.cos(a / 2))
    TURN[4] = turned(TURN[3], -0.2)
    for n in REST:
        for el, blade in RT_BLADE.items():
            REST[n][el] = dict(REST[n][el])
            REST[n][el][ROT] = TURN[min(max(blade - n, 1), 4)]

    # The right edge's shadow: where the others have it (1888's stays put
    # while there are right tabs).
    REST[1]["blade_topshadow_rt"] = dict(REST[2]["blade_topshadow_rt"])
    for n in REST:                      # the Marketplace's jewel is always its own colour
        for nm in REST[n].get("bladeMP_top_jewel", {}):
            if nm.endswith(":Visual"):
                REST[n]["bladeMP_top_jewel"][nm] = MP_VISUAL["jewel"]

    # ---- the animations ---------------------------------------------------------------
    # (new name, its end's name, 1888's range, mode, resting layout at the start,
    # at the end); mode: ("move", n) = 1888's own for blade n-1, one tab right;
    # ("along",) = 1888's Xbox Live one with every blade one along.
    old_frames = {a: b for a, b, *_ in scene["raw_timelines"]["frames"]}

    def rng(a, b):
        return (old_frames[a], old_frames[b])

    segs = [("Default", None, (0, 0), ("move", 2), 2, 2)]
    segs.append(("1To2", "1To2End", rng("1To2", "1To2End"), ("along",), 1, 2))
    segs.append(("2To1", "2To1End", rng("2To1", "2To1End"), ("along",), 2, 1))
    for n in (2, 3, 4):
        j = n - 1
        segs.append(("%dTo%d" % (n, n + 1), "%dTo%dEnd" % (n, n + 1),
                     rng("%dTo%d" % (j, j + 1), "%dTo%dEnd" % (j, j + 1)), ("move", n), n, n + 1))
        segs.append(("%dTo%d" % (n + 1, n), "%dTo%dEnd" % (n + 1, n),
                     rng("%dTo%d" % (j + 1, j), "%dTo%dEnd" % (j + 1, j)), ("move", n + 1), n + 1, n))
    for kind, rest_at in (("Open", "start"), ("Close", "end"), ("OpenFull", None), ("CloseFull", None),
                          ("Blink", "both")):
        for n in range(1, 6):
            j = max(n - 1, 1)
            a, b = "%d%s" % (j, kind), "%d%sEnd" % (j, kind)
            mode = ("along",) if n == 1 else ("move", n)
            st = n if rest_at in ("start", "both") else None
            en = n if rest_at in ("end", "both") else None
            segs.append(("%d%s" % (n, kind), "%d%sEnd" % (n, kind), rng(a, b), mode, st, en))
    for a, b, n in (("BootLive", "EndBootLive", 2), ("BootGames", "EndBootGames", 3),
                    ("BootOOBE", "EndBootOOBE", None), ("OOBEDone", "EndOOBEDone", 2),
                    ("BootLiveCmd", "EndBootLiveCmd", 2), ("BootGamesCmd", "EndBootGamesCmd", 3),
                    ("BootMediaCmd", "EndBootMediaCmd", 4), ("BootSystemCmd", "EndBootSystemCmd", 5),
                    ("ReturnGames", "EndReturnGames", 3), ("ReturnMedia", "EndReturnMedia", 4),
                    ("BootSystem", "EndBootSystem", 5)):
        segs.append((a, b, rng(a, b), ("move", n or 2), None, n))

    out_tracks = {el: Track() for el in s.tracks}
    for el, t in s.tracks.items():
        for name in t.order:
            out_tracks[el].add_path(name, t.paths[name])
    for el in RT_BLADE:
        out_tracks[el].add_path(ROT, s.tracks["blade_2_grey_rt"].paths[ROT])
    frames = []
    t_new = 0

    # The Marketplace's tab while another blade is selected is the leftmost tab:
    # it does what 1888's leftmost one (Xbox Live's) does, one slot to its left
    # (no move), in the same animation; with Xbox Live itself selected (1888 had
    # no tab on its left) in Games' matching one, timed to fit. Blade switches
    # leave it at rest (its start and end are pinned).
    LEFTMOST = {"blade_0_txt": "blade_1_txt", "blade_0_grey_left": "blade_1_grey_Left"}
    ANALOG = {"2Open": "2Open", "2Close": "2Close", "2OpenFull": "2OpenFull", "2CloseFull": "2CloseFull",
              "2Blink": "2Blink", "BootLive": "BootGames", "OOBEDone": "BootGames", "BootLiveCmd": "BootGamesCmd"}
    END_OF = {a: b for (a, b) in zip([f[0] for f in scene["raw_timelines"]["frames"]][:-1],
                                     [f[0] for f in scene["raw_timelines"]["frames"]][1:])}
    # With the Marketplace selected, System's tab is outermost on the right: it
    # does what 1888's outermost right tab (System's) does, one slot further out.
    sys_label_at = REST[1]["blade_4_txt"]
    sys_off = None
    for nm, v in sys_label_at.items():
        if nm.endswith(":Position"):
            o = old_rest[1]["blade_4_txt"][nm]
            sys_off = (v[1] - o[1], v[2] - o[2])

    def source_of(el, mode, name=None, a=0, b=0):
        """(1888 element, its range, offset (dx, dy)) whose keys this one takes,
        or (None, ...) for none."""
        if mode[0] == "move":
            if el in LEFTMOST:
                # A switch between Xbox Live .. System: the Marketplace's tab
                # rests where 1888's Xbox Live tab rests with the same blade
                # selected (its label: l1; its shape: 25.6, turned 0.2 degrees
                # a tab), so it moves as that one does in 1888's same-numbered
                # switch, unmoved (0.0.112 glided straight between rests, out
                # of the cascade while the blades moved).
                # 4To5 / 5To4 have no 1888 counterpart: pinned rests.
                if "To" in name and name in old_frames and END_OF.get(name):
                    return LEFTMOST[el], old_frames[name], old_frames[END_OF[name]], (0.0, 0.0)
                if mode[1] >= 3 and "To" not in name and name != "Default":
                    return LEFTMOST[el], a, b, (0.0, 0.0)
                if name in ANALOG:
                    a2 = old_frames[ANALOG[name]]
                    b2 = old_frames[END_OF[ANALOG[name]]]
                    return LEFTMOST[el], a2, b2, (0.0, 0.0)
                return None, a, b, None
            if el in ("blade_4_grey_rt", "Picture_blade0", "Tab1") or el in TOP[0] or el in MP_COLOUR.values():
                return None, a, b, None
            return el, a, b, None
        if el == LABEL[4] and sys_off:
            return "blade_4_txt", a, b, sys_off
        if el in RT_ALONG:
            return RT_ALONG[el][0], a, b, (RT_ALONG[el][1], 0.0)
        for m in range(0, 4):
            if el == LABEL[m]:
                return LABEL[m + 1], a, b, None
            if el in TOP[m]:
                return TOP[m + 1][TOP[m].index(el)], a, b, None
            if el == PICTURE[m]:
                return PICTURE[m + 1], a, b, None
            if el == TAB[m]:
                return TAB[m + 1], a, b, None
        if el in MP_COLOUR.values():
            return [c for c in COLOUR if MP_COLOUR[c] == el][0], a, b, None
        # The Marketplace's grey tab: Xbox Live's in 1888's switch (shown
        # until the selected tab's shape covers it, frame 9). Pinned, it stayed
        # under the sliding blade to the end: a double edge, then a single one
        # at rest.
        if el == "blade_0_grey_left":
            return "blade_1_grey_Left", a, b, (0.0, 0.0)
        # (the other left tab shapes: none shows with the Marketplace or Xbox
        # Live selected; following 1888's own, Xbox Live's came in under the
        # Marketplace's at the end of Marketplace -> Xbox Live: a double edge)
        if el in ("blade_1_grey_Left", "blade_2_grey_left", "blade_3_grey_left"):
            return None, a, b, None
        if el in (PICTURE[4], TAB[4]) or el in TOP[4]:
            return None, a, b, None
        return el, a, b, None

    def _unused(el, mode):
        if mode[0] == "move":
            if el in ("blade_0_txt", "blade_0_grey_left", "blade_4_grey_rt", "Picture_blade0", "Tab1") or \
                    el in TOP[0] or el in MP_COLOUR.values():
                return None
            return el
        for m in range(0, 4):
            if el == LABEL[m]:
                return LABEL[m + 1]
            if el in TOP[m]:
                return TOP[m + 1][TOP[m].index(el)]
            if el == PICTURE[m]:
                return PICTURE[m + 1]
            if el == TAB[m]:
                return TAB[m + 1]
        if el in MP_COLOUR.values():
            return [c for c in COLOUR if MP_COLOUR[c] == el][0]
        if el in (LABEL[4], PICTURE[4], TAB[4], "blade_0_grey_left", "blade_4_grey_rt") or el in TOP[4]:
            return None
        return el

    # The blade masks the contents it passes over: the
    # sliding blade's leading edge is 1888's three "tween" shapes (its bend
    # going right, straight, bending left). A blade's content is clipped
    # (ClipChildren) to the left of that edge: coming in, the old blade's
    # content goes as the new blade crosses it, not faded across the screen;
    # going back, the uncovered blade's content shows where the blade has left.
    # (each shape's solid grey begins this far right of its place: 1888's skin,
    # base1 in blade_rt_bend_tween 93.9, blade_straight_tween's group 72.8,
    # blade_left_bend_tween's mirrored group 253.5 - 164.8)
    TWEENS = {"right_bend_blade_tween": 93.9, "straight_blade_tween": 72.8, "left_bend_blade_tween": 88.7}
    WIDTH = ("2.1..None:Width", s.tracks["WingBootLeft"].paths["2.1..None:Width"])
    FULL_W = 1120.0
    for n in range(1, 6):
        s.nodes[TAB[n - 1]]["props"]["ClipChildren"] = True
        s.nodes[TAB[n - 1]]["props"].setdefault("Width", FULL_W)

    def track_at(t, f):
        """(x, shown) of a track at absolute frame f, from its keys so far."""
        pos = [nm for nm in t.order if nm.endswith(":Position")]
        sh = [nm for nm in t.order if nm.endswith(":Show")]
        held, prev = {}, None
        for k in t.keys:
            if k[0] > f:
                if prev is not None and prev[1] != 1 and pos and pos[0] in k[3] and pos[0] in held:
                    u = (f - prev[0]) / float(k[0] - prev[0])
                    x0, x1 = held[pos[0]][1], k[3][pos[0]][1]
                    return x0 + (x1 - x0) * u, held.get(sh[0], True) if sh else True
                break
            held.update({nm: v for nm, v in k[3].items() if v is not None})
            prev = k
        x = held[pos[0]][1] if pos and pos[0] in held and isinstance(held[pos[0]], tuple) else None
        return x, (held.get(sh[0], True) if sh else True)

    def landed(keys, pnm):
        """The frame from which a part's position no longer changes (0: never moves)."""
        pos = [(k[0], k[3][pnm]) for k in keys if isinstance(k[3].get(pnm), tuple)]
        if not pos:
            return 0
        last, at = pos[-1][1], pos[-1][0]
        for time, v in reversed(pos):
            if abs(v[1] - last[1]) > 0.05 or abs(v[2] - last[2]) > 0.05:
                break
            at = time
        return at

    def key_at(keys, f):
        """A key at frame f, where the keys either side would put it."""
        if any(k[0] == f for k in keys):
            return
        i = max(j for j, k in enumerate(keys) if k[0] < f)
        a = keys[i]
        b = keys[i + 1] if i + 1 < len(keys) else None
        u = (f - a[0]) / float(b[0] - a[0]) if b else 0.0
        vals = {}
        for nm, v in a[3].items():
            w = b[3].get(nm) if b and a[1] != 1 else None
            if isinstance(v, tuple) and isinstance(w, tuple) and v[0] == w[0] == "V":
                vals[nm] = ("V",) + tuple(x + (y - x) * u for x, y in zip(v[1:], w[1:]))
            elif isinstance(v, float) and isinstance(w, float):
                vals[nm] = v + (w - v) * u
            else:
                vals[nm] = v
        keys.insert(i + 1, [f, a[1], a[2], vals])

    def mask_switch(name, t0, length):
        n_from, n_to = (int(x) for x in name.split("To"))
        edge = []
        for f in range(length + 1):
            xs = []
            for el, pad in TWEENS.items():
                x, shown = track_at(out_tracks[el], t0 + f)
                if shown and x is not None:
                    xs.append(x + pad)
            edge.append(min(xs) if xs else None)
        moving = [f for f in range(length + 1) if edge[f] is not None]
        if not moving:
            return
        last = moving[-1]
        forward = n_to > n_from
        for el, which in ((TAB[n_from - 1], "out"), (TAB[n_to - 1], "in")):
            t = out_tracks[el]
            t.add_path(*WIDTH)
            op = [nm for nm in t.order if nm.endswith(":Opacity")]
            sh = [nm for nm in t.order if nm.endswith(":Show")]
            t.keys = [k for k in t.keys if not (t0 <= k[0] <= t0 + length)]

            def key(f, interp, opacity, show, width):
                v = {WIDTH[0]: width}
                for nm in op:
                    v[nm] = opacity
                for nm in sh:
                    v[nm] = show
                t.keys.append((t0 + f, interp, (0, 0, 0), v))
            if forward and which == "out":
                # (until the blade's edge comes in, the whole of it)
                first = moving[0]
                if first > 0:
                    key(0, 0, 1.0, True, FULL_W)
                for f in range(first, last + 1):
                    key(f, 0, 1.0, True, max(0.0, edge[f] if edge[f] is not None else 0.0))
                key(last + 1, 1, 0.0, False, FULL_W)
                key(length, 1, 0.0, False, FULL_W)
            elif forward:
                # the new blade's content: 1888's, in over the last four frames
                key(0, 1, 0.0, False, FULL_W)
                key(max(length - 4, last + 1), 0, 0.0, True, FULL_W)
                key(length, 1, 1.0, True, FULL_W)
            elif which == "out":
                # going back, the blade leaving takes its content with it (1888:
                # gone at once)
                key(0, 1, 0.0, False, FULL_W)
                key(length, 1, 0.0, False, FULL_W)
            else:
                for f in range(0, last + 1):
                    key(f, 0, 1.0, True, max(0.0, edge[f] if edge[f] is not None else FULL_W))
                key(last + 1, 1, 1.0, True, FULL_W)
                key(length, 1, 1.0, True, FULL_W)
            t.keys.sort(key=lambda k: k[0])

    for name, end_name, (a, b), mode, st, en in segs:
        length = b - a
        # A screen opened over the blades (1888: "<n>Open" .. "<n>CloseFull"):
        # only the selected blade's tab shows, at the far left, the same for
        # every blade, and the screen isn't moved: 1888's keys as they are
        # (the resting ends are pinned to the moved layout; 0.0.111 moved them
        # all, and the tab sat over the screen's list).
        opened = name[1:] in ("Open", "Close", "OpenFull", "CloseFull")
        frames.append((name, t_new, 1 if end_name is None else 0, None))
        if end_name:
            frames.append((end_name, t_new + length, 1, None))
        for el, t in out_tracks.items():
            src, a2, b2, offset = source_of(el, mode, name, a, b)
            keys = []
            # Another animation's keys are timed to fit this one.
            scale = (float(length) / (b2 - a2)) if (b2 - a2) else 0.0
            if src and src in s.tracks:
                for time, interp, ease, vals in s.tracks[src].keys:
                    if a2 <= time <= b2:
                        keys.append([int(round((time - a2) * scale)), interp, ease, dict(vals)])
            # The layout this segment starts and ends in, where it's a resting one.
            start = REST[st][el] if st and el in REST[st] else None
            end = REST[en][el] if en and el in REST[en] else None
            # 1888's own first and last keys say how the animation begins and
            # ends (the old blade's content hidden at once, the new label's
            # starting place...): they stand; the resting layout only fills in
            # what they don't have (0.0.112 overrode them: the old content
            # stayed up until the end, then the new snapped in, and labels
            # overlapped it).
            own_start = any(k[0] == 0 for k in keys)
            own_end = any(k[0] == length for k in keys)
            if not keys or keys[0][0] != 0:
                v = s.value_at(src, a2) if src and src in s.tracks else {}
                keys.insert(0, [0, 0, (0, 0, 0), dict(v)])
            if keys[-1][0] != length and length:
                v = s.value_at(src, b2) if src and src in s.tracks else {}
                keys.append([length, 0, (0, 0, 0), dict(v)])
            # (keys squeezed onto the same frame: the last one stands)
            dedup = []
            for k in keys:
                if dedup and dedup[-1][0] == k[0]:
                    dedup[-1] = k
                else:
                    dedup.append(k)
            keys = dedup
            # Moved one tab right ("move"), by its own offset, or the Marketplace's
            # colours ("along").
            for k in keys:
                if offset is not None:
                    k[3] = {nm: (vec(v, offset[0], offset[1]) if nm.endswith(":Position") and isinstance(v, tuple)
                                 else v) for nm, v in k[3].items()}
                elif mode[0] == "move":
                    if not opened:
                        k[3] = moved(el, k[3], LEFT_SLOT)
                elif el in MP_COLOUR.values() or el in COLOUR:
                    vis = [nm for nm in k[3] if nm.endswith(":Visual")]
                    if vis:
                        live = str(k[3][vis[0]]).startswith("blade_1_")
                        if el in COLOUR and live:
                            k[3] = shown(k[3], False)
                        elif el in COLOUR:
                            # every other blade's colour one along too (Games'
                            # green in 1888's animation is Xbox Live's orange here)
                            v = str(k[3][vis[0]])
                            if v.startswith("blade_") and v[6:7].isdigit() and int(v[6]) >= 2:
                                k[3][vis[0]] = "blade_%d%s" % (int(v[6]) - 1, v[7:])
                        if el in MP_COLOUR.values():
                            k[3] = shown(k[3], bool(live))
                            k[3][vis[0]] = MP_VISUAL["bgcolor" if el.startswith("BG") else "highlight"]
                # (System's tab one further out than 1888's: one turn more)
                if mode[0] == "along" and el == "blade_3_grey_rt" and isinstance(k[3].get(ROT), tuple):
                    k[3][ROT] = turned(k[3][ROT], -0.2)
                if mode[0] == "along" and el in LABEL_DX and offset is None:
                    k[3] = {nm: (vec(v, LABEL_DX[el]) if nm.endswith(":Position") and isinstance(v, tuple)
                                 and v[1] > 600 else v) for nm, v in k[3].items()}
                if el == "bladeMP_top_jewel":
                    for nm in list(k[3]):
                        if nm.endswith(":Visual"):
                            k[3][nm] = MP_VISUAL["jewel"]
                for nm in list(k[3]):
                    if nm.endswith(":Visual") and isinstance(k[3][nm], str) and mode[0] == "along" \
                            and el not in MP_COLOUR.values() and el not in COLOUR and el != "bladeMP_top_jewel":
                        v = k[3][nm]
                        if v.startswith("blade_") and v[6:7].isdigit():
                            d = int(v[6]) - 1
                            k[3][nm] = MP_VISUAL["jewel"] if d == 0 else "blade_%d%s" % (d, v[7:])
            # Resting layouts pinned at the ends; parts with no keys of their own
            # hold the resting values throughout.
            # A part 1888 animates keeps 1888's own values at the ends too (what
            # it shows at that frame, value_at above): the resting layout pinned
            # over them made Games' colour layer snap at Media's start (0.0.113).
            if src and src in s.tracks and mode[0] == "move" and offset is None:
                start = end = None
            # The Marketplace / Xbox Live switches are 1888's Xbox Live / Games
            # one, whose Xbox Live layout is a few points off the one the other
            # switches use (1888's moved one tab right): Xbox Live's panel
            # jumped 5 points at the start of the Games switch. Their keys are
            # eased onto that layout across the switch.
            # The screens opened over the blades likewise: their frames are
            # 1888's own, unmoved, so a Close ended in 1888's four-blade layout
            # (the right tabs and the panel's edge 44 points left of the rest:
            # the description ran over the Media tab after the Game Library).
            # Eased onto the resting layout across the
            # Close, off it across the Open.
            easing = (mode[0] == "along" and "To" in name) or (opened and name[1:] in ("Open", "Close"))
            # (an opened screen's layout is 1888's, unmoved: up to a tab and a
            # half off the rest on the right, more for Media's colour strip)
            limit = 12 if mode[0] == "along" and not opened else 250

            def eased(end_n):
                return bool(easing and length and el not in TAB.values() and end_n and el in REST[end_n]
                            and not (mode[0] == "along" and not opened and end_n != 2))

            def pin(k, vals, end_n):
                # (a place the easing below brings it to is left to that: pinned
                # on the last key alone, the part landed, then crept onto it)
                for nm, v in vals.items():
                    have = k[3].get(nm)
                    if nm.endswith(":Position") and eased(end_n) and isinstance(have, tuple) \
                            and isinstance(v, tuple) and abs(v[1] - have[1]) <= limit and abs(v[2] - have[2]) <= limit:
                        continue
                    k[3][nm] = v
            if start is not None:
                if own_start:
                    for nm, v in start.items():
                        keys[0][3].setdefault(nm, v)
                else:
                    pin(keys[0], start, st)
            if end is not None:
                if own_end:
                    for nm, v in end.items():
                        keys[-1][3].setdefault(nm, v)
                else:
                    pin(keys[-1], end, en)
            # The left tab shapes in a switch between Xbox Live .. System: 1888's
            # switch with the same blades shows one fewer left tab, and the one
            # it brings in it keeps hidden, then snaps in at frame 10. Here that
            # tab is already there: it held, then jumped. They slide from rest
            # to rest over the
            # switch's movement (frames 0 to 9) instead, as 1888's tabs do.
            if "To" in name and mode[0] == "move" and el.endswith(("_grey_Left", "_grey_left")) \
                    and el != "blade_0_grey_left":
                n_from, n_to = (int(x) for x in name.split("To"))
                def is_shown(v):
                    return all(x for nm, x in v.items() if nm.endswith(":Show"))
                # (a tab becoming the selected blade keeps 1888's keys: hidden
                # under the selected shape at once; slid, it showed a double edge)
                if el in REST[n_from] and el in REST[n_to] and is_shown(REST[n_from][el])                         and is_shown(REST[n_to][el]):
                    keys = [[0, 0, (0, 0, 0), dict(REST[n_from][el])],
                            [min(9, length), 1, (0, 0, 0), dict(REST[n_to][el])],
                            [length, 1, (0, 0, 0), dict(REST[n_to][el])]]
            # The right edge's shadow: 1888's Xbox Live to Games switch moves it
            # out a tab and holds it there, then snaps it back at frame 9 (its
            # others leave it alone): it popped at the start of Games' switch.
            # From rest to rest over the movement, in every switch.
            if "To" in name and el == "blade_topshadow_rt" and "End" not in name:
                n_from, n_to = (int(x) for x in name.split("To"))
                keys = [[0, 0, (0, 0, 0), dict(REST[n_from][el])],
                        [min(9, length), 1, (0, 0, 0), dict(REST[n_to][el])],
                        [length, 1, (0, 0, 0), dict(REST[n_to][el])]]
            if easing and length and el not in TAB.values():
                for end_n, at_end in ((en, True), (st, False)):
                    if not eased(end_n):
                        continue
                    want = [v for nm, v in REST[end_n][el].items() if nm.endswith(":Position")]
                    k_end = keys[-1] if at_end else keys[0]
                    have = [(nm, v) for nm, v in k_end[3].items() if nm.endswith(":Position")]
                    if not want or not have or not isinstance(have[0][1], tuple):
                        continue
                    pnm = have[0][0]
                    dx, dy = want[0][1] - have[0][1][1], want[0][2] - have[0][1][2]
                    if abs(dx) > limit or abs(dy) > limit or (abs(dx) < 0.05 and abs(dy) < 0.05):
                        continue
                    # Over the part's own movement, done as it lands: across
                    # the whole switch it crept on after landing (Xbox Live's
                    # label 2 points, its picture 3, the shine 4: the user's
                    # report). A part that doesn't move: until its own last
                    # key (an Open's left tabs land together, frame 29), else
                    # over the blades' movement (frames 0 to 9).
                    span = landed(keys, pnm)
                    if not span:
                        inner = [k[0] for k in keys if 0 < k[0] < length]
                        span = inner[-1] if inner else min(9, length)
                        key_at(keys, span)
                    for k in keys:
                        u = min(1.0, k[0] / float(span))
                        w = u if at_end else 1.0 - u
                        v = k[3].get(pnm)
                        if isinstance(v, tuple):
                            k[3] = dict(k[3])
                            k[3][pnm] = ("V", v[1] + dx * w, v[2] + dy * w, v[3])
            # And the rest of the resting layout (what shows, its colours, its
            # turn) at a Close's end and an Open's start: unpinned since
            # 0.0.113, a Close left the opened screen's stand-in tab up and the
            # tabs where the screen had them.
            if opened and name[1:] in ("Open", "Close"):
                rest_n = st if name[1:] == "Open" else en
                if rest_n and el in REST[rest_n] and keys:
                    k = keys[0] if name[1:] == "Open" else keys[-1]
                    k[3] = dict(k[3])
                    for nm, v in REST[rest_n][el].items():
                        if not nm.endswith(":Position") or el in TAB.values():
                            k[3][nm] = v
                        elif nm not in k[3]:
                            k[3][nm] = v
            # The selected blade's label while a screen opens or closes over the
            # blades: gone while it slides, out over 3 frames, back over the last 4
            # once it has (nearly) landed.
            if opened and name[1:] in ("Open", "Close") and el == LABEL[int(name[0]) - 1] and length > 8:
                op = [nm for nm in out_tracks[el].order if nm.endswith(":Opacity")]
                pos = [nm for nm in out_tracks[el].order if nm.endswith(":Position")]
                if op and pos:
                    back = landed(keys, pos[0]) or length
                    back = min(max(back, 3) + 4, length) if name[1:] == "Open" else length
                    pts = [(0, 1.0), (3, 0.0), (back - 4, 0.0), (back, 1.0)]
                    for f, _ in pts:
                        key_at(keys, f)

                    def opacity(f):
                        for (f0, v0), (f1, v1) in zip(pts, pts[1:]):
                            if f0 <= f <= f1:
                                return v0 + (v1 - v0) * (f - f0) / float(f1 - f0) if f1 > f0 else v1
                        return 1.0
                    for k in keys:
                        k[3] = dict(k[3])
                        k[3][op[0]] = opacity(k[0])
            # Media <-> System has no 1888 switch for the Marketplace's tab to
            # follow: pinned at both ends it crept the whole switch long, after
            # the other tabs had stopped. Over the switch's movement (frames 0
            # to 9), as they do.
            if name in ("4To5", "5To4") and el in LEFTMOST:
                n_from, n_to = (int(x) for x in name.split("To"))
                keys = [[0, 0, (0, 0, 0), dict(REST[n_from][el])],
                        [min(9, length), 1, (0, 0, 0), dict(REST[n_to][el])],
                        [length, 1, (0, 0, 0), dict(REST[n_to][el])]]
            # A blade switch's contents, as the System switch did: the old
            # blade's fades out across the switch, the new one
            # fades in over its last four frames (1888's own), the rest stay hidden.
            if "To" in name and el in TAB.values():
                n_from, n_to = (int(x) for x in name.split("To"))
                op = [nm for nm in t.order if nm.endswith(":Opacity")]
                sh = [nm for nm in t.order if nm.endswith(":Show")]

                def tab_key(time, interp, opacity, show):
                    v = {}
                    for nm in op:
                        v[nm] = opacity
                    for nm in sh:
                        v[nm] = show
                    return [time, interp, (0, 0, 0), v]
                if el == TAB[n_from - 1]:
                    # (out over the switch's movement, 1888's first nine frames:
                    # across the whole switch it lingered)
                    keys = [tab_key(0, 0, 1.0, True), tab_key(min(9, length), 1, 0.0, False),
                            tab_key(length, 1, 0.0, False)]
                elif el == TAB[n_to - 1]:
                    keys = [tab_key(0, 1, 0.0, False), tab_key(max(length - 4, 1), 0, 0.0, True),
                            tab_key(length, 1, 1.0, True)]
                else:
                    keys = [tab_key(0, 1, 0.0, False), tab_key(length, 1, 0.0, False)]
            if not src or src not in s.tracks:
                fill = start or end or (REST[2].get(el) if mode[0] == "move" else REST[1].get(el)) or {}
                for k in keys:
                    for nm, v in fill.items():
                        k[3].setdefault(nm, v)
            # (a right tab following 1888's nearest, Games', which never turns:
            # its turn. Unset, it held whatever the switch before had left.)
            if el in RT_BLADE:
                for k in keys:
                    k[3].setdefault(ROT, TURN[1])
            for time, interp, ease, vals in keys:
                for nm in vals:
                    if nm not in t.paths:
                        # (a path the source has that this element's own timeline didn't)
                        t.add_path(nm, s.tracks[src].paths[nm])
                t.keys.append((t_new + time, interp, ease, vals))
        if "To" in name and length and name[0].isdigit():
            mask_switch(name, t_new, length)
        t_new += length + 1

    # ---- written back -------------------------------------------------------------------
    raw = scene["raw_timelines"]
    raw["frames"] = frames
    tls = []
    for el in [c["props"].get("Id") for c in children]:
        if el not in out_tracks or not out_tracks[el].keys:
            continue
        t = out_tracks[el]
        paths = [t.paths[nm] for nm in t.order]
        keys = []
        held = {}
        for time, interp, ease, vals in t.keys:
            held.update({k: v for k, v in vals.items() if v is not None})
            values = []
            for nm in t.order:
                v = held.get(nm)
                if v is None:
                    v = s.nodes[el]["props"].get(nm.split(":", 1)[1]) if el in s.nodes else None
                if v is None:
                    p = t.paths[nm][1]
                    v = {1: False, 2: 0, 3: 0, 4: 0.0, 5: "", 6: "0x00000000", 7: ("V", 0.0, 0.0, 0.0),
                         8: ("Q", 0.0, 0.0, 0.0, 1.0)}.get(p.type, 0)
                held[nm] = v
                values.append(v)
            if el == "Blade_Sounds":
                values = [arrival_sound(time, frames, v) if nm.endswith(":File") else v
                          for nm, v in zip(t.order, values)]
            keys.append((time, interp, tuple(ease) if not isinstance(ease, tuple) else ease, values))
        tls.append({"target": el, "paths": paths, "keys": keys})
    raw["timelines"] = tls

    data = xur5w.Writer(r).write(root)
    files = [("dashmain.xur", data)]
    # The Marketplace's content (tools/marketplace5.py), when built beside it.
    hub = os.path.join(os.path.dirname(os.path.abspath(out)), "marketplaceHub.xur")
    if os.path.exists(hub):
        files.append(("marketplaceHub.xur", open(hub, "rb").read()))
    open(out, "wb").write(xzp.build(files, 1))
    print("%s: %d bytes; %d named frames, %d timelines, %d keys, last frame %d" % (
        out, os.path.getsize(out), len(frames), len(tls), sum(len(t["keys"]) for t in tls), t_new - 1))


if __name__ == "__main__":
    main()
