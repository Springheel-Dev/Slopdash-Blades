"""Builds marketplaceHub.xur: the Marketplace blade's content as the user's
screenshot of the 2007 Blades dashboard shows it (its online Marketplace,
which Xbox Live delivered: no dashboard file holds it). Six picture tiles on
the left (the store's categories), the banner top right, and three links
under it (Manage Repos, Active Downloads, File Manager).

    python tools/marketplace5.py <6717 blademp marketplaceSignedOut.xur> <out.xur>

Real parts only: 6717's own tile button (its skin's btn_BannerBtnToplevel:
a picture with its label along the bottom) and link button (btn_oneline_icon),
reached under the prefix SlopDash loads 6717's skin with (M6); the links'
icons are 1888's own for those actions (dashmark: ico_64x_PrepaidCard,
ico_64x_DownloadHistory, ico_64x_Memberships). The tile pictures and the
banner were Xbox Live's and aren't in any file: left empty.
The scene is 6717's signed-out Marketplace content (700 x 445, its back
panel) with its parts replaced; written in version 5 (tools/xur5w.py).
"""
import copy
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xur  # noqa: E402
import xur5to8  # noqa: E402
import xur5w  # noqa: E402
import xurlearn  # noqa: E402

SCHEMA = xur.Schema(os.path.join(HERE, "xui_schema.txt"))
EXTRA = {}
for name in ("blades_classes.json", "blades1888_classes.json"):
    for c, v in json.load(open(os.path.join(HERE, name))).items():
        EXTRA.setdefault(c, (v["base"], xurlearn.props(v["types"])))

PREFIX = "M6"
# The hub as a homebrew store: the tiles are
# ConsoleMods' repo categories (Aurora's Homebrew Store reads the same
# repos), with SlopDash's own themes in Themes' place; the links add a repo,
# show the downloads, and open a file manager. Icons: 1888's own (friends:
# ico_64x_addEditText; dashmark: ico_64x_DownloadHistory; memory:
# ico_64x_AllDevices).
# Each tile: its shelf (store::Shelf, the tile's id: btnTile<shelf>) and name.
# No Dashboards tile: their items are on Homebrew Apps.
TILES = ((0, "Homebrew Games"), (1, "Homebrew Apps"), (2, "Emulators"), (4, "SlopDash Themes"), (5, "Other"))
LINKS = (("btnAddRepo", "Manage Repos", "ico_64x_addEditText.png"),
         ("btnActiveDownloads", "Active Downloads", "ico_64x_DownloadHistory.png"),
         ("btnFileManager", "File Manager", "ico_64x_AllDevices.png"))
# The canvas is 700 x 445 (6717's Marketplace content), in two 345-wide columns.
COL, GAP = 345.0, 10.0
TILE_H, TILE_STEP = 69.0, 75.0             # as with six tiles
LINK_H = 72.0                               # 6717's btn_oneline_icon
BANNER_H = 445.0 - 3 * LINK_H - GAP         # the banner over the three links


def node(cls, id_, x, y, w, h, **props):
    p = {"Id": id_, "Width": float(w), "Height": float(h), "Position": ("V", float(x), float(y), 0.0)}
    p.update(props)
    return {"class": cls, "props": p, "children": [], "has_children": False}


def main(src=None, out=None):
    if src is None:             # (the command line; the importer passes them)
        src, out = sys.argv[1], sys.argv[2]
    r = xur.Reader(open(src, "rb").read(), SCHEMA, dict(EXTRA))
    root = xur5to8.resolve(r, r.obj())
    scene = root["children"][0]
    back = [c for c in scene["children"] if c["props"].get("Id") == "figBackground"]
    kids = [copy.deepcopy(c) for c in back]
    for k in kids:
        k["props"]["Visual"] = PREFIX + k["props"].get("Visual", "back panel")
    tile_ids = ["btnTile%d" % shelf for shelf, _ in TILES]
    link_ids = [l[0] for l in LINKS]
    first_beside = 3                            # the tile level with the first link
    # Right from the tiles goes on to the next blade, as on every other blade
    # (the thumbstick stopped at the links instead),
    # except from the tile level with the first link (SlopDash Themes): right
    # crosses to it. The links also come after the
    # last tile going down.
    for i, (shelf, text) in enumerate(TILES):
        nav = {"NavRight": link_ids[0]} if i == first_beside else {}
        if i:
            nav["NavUp"] = tile_ids[i - 1]
        nav["NavDown"] = tile_ids[i + 1] if i < len(TILES) - 1 else link_ids[0]
        kids.append(node("XuiNavButton", tile_ids[i], 0, i * TILE_STEP, COL, TILE_H,
                         Visual=PREFIX + "btn_BannerBtnToplevel", Text=text, **nav))
    # Each tile's highlight: 6717's banner button shows focus by brightening
    # the tile's picture, and the tiles have none yet, so the focused one
    # couldn't be seen. Over each tile, the
    # links' own button (6717's btn_oneline_icon), hidden: shown and played
    # in its Focus range on the focused tile (blades1888.cpp HubProc).
    for i, (shelf, _) in enumerate(TILES):
        kids.append(node("XuiControl", "rdTileHi%d" % shelf, 0, i * TILE_STEP, COL, TILE_H,
                         Visual=PREFIX + "btn_oneline_icon", Show=False, Enabled=False))
    # The banner: 6717's billboard slot, empty (Xbox Live filled it).
    kids.append(node("XuiScene", "scnBanner", COL + GAP, 0, COL, BANNER_H, Visual=PREFIX + "Metapane",
                     ))
    for i, (id_, text, icon) in enumerate(LINKS):
        nav = {"NavLeft": tile_ids[min(first_beside + i, len(TILES) - 1)]}
        nav["NavUp"] = link_ids[i - 1] if i else tile_ids[-1]
        if i < len(LINKS) - 1:
            nav["NavDown"] = link_ids[i + 1]
        kids.append(node("XuiNavButton", id_, COL + GAP, BANNER_H + GAP + i * LINK_H, COL, LINK_H,
                         Visual=PREFIX + "btn_oneline_icon", Text=text, ImagePath=icon, **nav))
    scene["children"] = kids
    scene["has_children"] = True
    scene["props"]["DefaultFocus"] = tile_ids[0]
    for k in ("P0", "P1", "P2"):
        scene["props"].pop(k, None)
    scene["class"] = "XuiScene"
    scene["props"]["ClassOverride"] = "SlopMarketplaceHub"       # SlopDash's (blades1888.cpp)
    scene.pop("raw_timelines", None)
    data = xur5w.Writer(r).write(root)
    open(out, "wb").write(data)
    print("%s: %d bytes, %d parts" % (out, len(data), len(kids)))


if __name__ == "__main__":
    main()
