"""Builds the Blades (launch) Guide for the console's own Guide code (system
2.0.17559): blades1888.xzp, everything the 2.0.1888 Guide drew, in the format
17559's xam reads (XUIB version 8, tools/xur5to8.py). SlopGuide (plugin/)
hands xam these scenes in place of its own, so no 17559 art is shown:

  hudbkgnd.xur   1888 xam's Guide blade (the frame xam hosts every Guide
                 screen in), fitted to 17559's 852 x 480 Guide canvas. 17559
                 drives it with the same named frames (ClosedToHalf...). The
                 parts 17559's code looks up that 1888 never had (Legend,
                 GamerPic, Spinner, imgPlayTimerIcon) are there, off-screen.
                 1888's GamerTag is renamed Gamertag (17559's name), so xam
                 writes the gamertag into 1888's own vertical text.
  *.xur          1888 hud.xex's screens (MainMenuSignedIn/Out, Options...):
                 1888's own code isn't on the console, so their classes are
                 dropped to plain HUDScenes, and the buttons whose action
                 17559's Guide has take 17559's button class for it. Each
                 screen shows its own legend buttons (btnA..btnY), the scene's
                 legend text in them.
  skin.xur       1888's Guide skin (huduiskin), every visual renamed PREFIX+name
                 (tools/guidens.py) so it loads beside 17559's skin without
                 replacing it; the scenes above point at the renamed visuals.
  images, sounds 1888's own (hud, huduiskin, xam's shared images and sounds):
                 "sharedres://" paths become files in this package, so 17559's
                 shared art never shows.

The input scenes are 1888's, already renumbered for the current XuiControl
(tests/test_xur.cpp writes them: <package>#<file>.xur).

    python tools/guide1888.py <upgraded scenes dir> <1888 extracted modules dir> <out blades1888.xzp>

<1888 extracted modules dir> holds tools/xexres.py's output per module:
hud/hud.bin, huduiskin/skin.bin, xam/xam.bin, xam/shrdres.bin. The package
holds Microsoft's art: built from the user's own files, never committed.
"""
import copy
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import guidens  # noqa: E402
import xur  # noqa: E402
import xur5to8  # noqa: E402
import xurlearn  # noqa: E402
import xzp  # noqa: E402

guidens.PREFIX = "B8_"          # the 1888 skin's names (6717's used RD_)
SCHEMA = xur.Schema(os.path.join(HERE, "xui_schema.txt"))
LEARNED = json.load(open(os.path.join(HERE, "blades1888_classes.json")))
EXTRA = {c: (v["base"], xurlearn.props(v["types"])) for c, v in LEARNED.items()}

OLD_CANVAS = (1120.0, 770.0)
NEW_CANVAS = (852.0, 480.0)
# Where 2.0.1888's xam put the Guide on screen (its view transform, sub_818D0248
# in the 1888 xam disassembly, Assets/Guidelines .txt): a uniform scale of the
# render target's height / 770 (the constant at 8187125C is 1/770), x moved by
# -40 pixels on a widescreen display (-60 on 4:3: sub_818D0CF0, XGetVideoMode),
# y not moved. In 17559's Guide canvas (852 x 480, drawn at 1.5x on 720 lines):
FIT_XY = (480.0 / 770.0, 480.0 / 770.0)
FIT_AT = (-40.0 * 480.0 / 720.0, 0.0)
OFF = ("V", -4000.0, -4000.0, 0.0)          # off-screen
SHARED = "sharedres://"

# The 17559 Guide's own button classes, for 1888 buttons that do the same:
# behaviour only (they draw with the 1888 visuals the scene gives them).
BUTTON_CLASSES = {
    "btnQuickChat": "QuickChatNavButton",
    "btnSignInLive": "ConnectToLiveNavButton",
    "btnRecoverProfile": "RecoverGamerTagNavButton",
    "btnFriends": "FriendsNavButton",
    "btnMessages": "MessagesNavButton",
    "btnRecentPlayers": "FriendsNavButton",
    "btnY": "GuideDashCommandNavButton",        # "Xbox Dashboard" (the main screens only: screen())
}
# Legend keys: the hidden legend buttons answer their controller button.
VK_PAD = {"btnA": 0x5800, "btnB": 0x5801, "btnX": 0x5802, "btnY": 0x5803}
LEGEND_PROPS = ("P1", "P2", "P3", "P4")       # HUDScene: LegendA, B, X, Y
LEGEND_BUTTONS = {"btnA": "P1", "btnB": "P2", "btnX": "P3", "btnY": "P4"}


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


def local_paths(node, shared):
    """sharedres:// paths (properties and timeline keys) become package files."""
    def fix(v):
        if isinstance(v, str) and v.startswith(SHARED):
            shared.add(v[len(SHARED):])
            return v[len(SHARED):]
        if isinstance(v, dict):
            return {k: fix(x) for k, x in v.items()}
        if isinstance(v, list):
            return [fix(x) for x in v]
        return v
    node["props"] = fix(node["props"])
    raw = node.get("raw_timelines")
    if raw:
        for tl in raw["timelines"]:
            tl["keys"] = [(t, i, e, fix(vals)) for t, i, e, vals in tl["keys"]]
    for c in node["children"]:
        local_paths(c, shared)


# --- the frame -----------------------------------------------------------------
def node(cls, id_, w, h, **props):
    p = {"Id": id_, "Width": float(w), "Height": float(h)}
    p.update(props)
    return {"class": cls, "props": p, "children": []}


def frame(scenes, shared):
    r, root = read(os.path.join(scenes, "xam#hudbkgnd.xur"))
    scene = root["children"][0]
    assert scene["props"].get("Id") == "HUDRootScene"
    # 1888's legends were filled by 1888 xam's code; 17559's fills its own
    # Legend group instead. The screens show their own legends (screen()).
    drop = {"Legend_A", "Legend_B", "Legend_X", "Legend_Y"}
    scene["children"] = [c for c in scene["children"] if c["props"].get("Id") not in drop]
    raw = scene["raw_timelines"]
    raw["timelines"] = [t for t in raw["timelines"] if t["target"] not in drop]
    for c in scene["children"]:
        if c["props"].get("Id") == "GamerTag":
            c["props"]["Id"] = "Gamertag"
            c["props"]["Text"] = ""
        if c["props"].get("Id") == "DateTimeTextId":
            c["props"]["Text"] = ""
    for t in raw["timelines"]:
        if t["target"] == "GamerTag":
            t["target"] = "Gamertag"
    # The screens hosted here are 1888's OpenType 1 ones: 420-wide content for the
    # Half layout (teardown, hudbkgnd: graphic-base -397, blade 583.5, clock 402);
    # Full (blade 991) is for 855-wide ones. The console opens its Guide in Full,
    # so Full's named frames play Half's keyframes (teardown named_frames.csv:
    # ClosedToHalf 1-44, HalfToClosed 45-74, HalfToError 299-358, ErrorToHalf 359-418).
    times = {f[0]: f[1] for f in raw["frames"]}
    as_half = {"ClosedToFull": "ClosedToHalf", "EndClosedToFull": "EndClosedToHalf",
               "FullToClosed": "HalfToClosed", "EndFullToClosed": "EndHalfToClosed",
               "FullToError": "HalfToError", "EndFullToError": "EndHalfToError",
               "ErrorToFull": "ErrorToHalf", "EndErrorToFull": "EndErrorToHalf",
               # Half <-> Full: no move (both are Half here): the end of ClosedToHalf.
               "HalfToFull": "EndClosedToHalf", "EndHalfToFull": "EndClosedToHalf",
               "FullToHalf": "EndClosedToHalf", "EndFullToHalf": "EndClosedToHalf"}
    raw["frames"] = [(n, times[as_half[n]] if n in as_half else t, c, tg) for n, t, c, tg in raw["frames"]]
    # Half <-> Full: 17559 gives these real length (its frame: HalfToFull 35-74,
    # FullToHalf 75-98). Zero length (start and stop both at 1888's open pose)
    # may never report finishing, and the Guide then stayed "open" (0.29): one
    # frame into the open pose instead, ending where it was.
    pose = times["EndClosedToHalf"]
    for i, (n, t, c, tg) in enumerate(raw["frames"]):
        if n in ("HalfToFull", "FullToHalf"):
            raw["frames"][i] = (n, pose - 1, c, tg)
    # 1888's own Half <-> Full, under names of SlopGuide's: it plays them for
    # the one 855-wide screen, the keyboard (1888 vk's CKeyboardScene: P0 2, Full).
    for n in ("HalfToFull", "EndHalfToFull", "FullToHalf", "EndFullToHalf"):
        raw["frames"].append(("RG_" + n, times[n], 1 if n.startswith("End") else 0, None))
    raw["frames"].sort(key=lambda f: f[1])
    # What 17559's HUDBkgndScene looks up that 1888 never had: present, off-screen.
    legend = node("XuiGroup", "Legend", 560, 24, Position=OFF)
    for k in "YXBA":
        legend["children"].append(node("XuiText", k + "Text", 64, 18))
        legend["children"].append(node("XuiGroup", k + "Button", 22, 18))
    scene["children"] += [
        node("XuiImage", "imgPlayTimerIcon", 18, 18, Position=OFF),
        node("XuiButton", "GamerPic", 32, 32, Position=OFF, ClassOverride="GamerPicButton"),
        legend,
        node("XuiControl", "Spinner", 40, 40, Position=OFF),
    ]
    # Fitted to 17559's canvas: the canvas takes 17559's size; the scene keeps
    # 1888's and is scaled into it.
    root["props"]["Width"], root["props"]["Height"] = NEW_CANVAS
    scene["props"]["Scale"] = ("V", FIT_XY[0], FIT_XY[1], 1.0)
    scene["props"]["Position"] = ("V", FIT_AT[0], FIT_AT[1], 0.0)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- the screens ---------------------------------------------------------------
KEEP_CLASSES = {"HUDScene"}


MAIN_SCREENS = ("MainMenuSignedIn.xur", "MainMenuSignedOut.xur")
MAIN_ONLY = {"btnY"}


def adapt(n, main):
    p = n["props"]
    co = p.get("ClassOverride")
    if co and co not in KEEP_CLASSES and co not in BUTTON_CLASSES.values():
        del p["ClassOverride"]
    cid = p.get("Id")
    if cid in BUTTON_CLASSES and (main or cid not in MAIN_ONLY):
        p["ClassOverride"] = BUTTON_CLASSES[cid]
        if n["class"] == "XuiButton":
            n["class"] = "XuiNavButton"
    for c in n["children"]:
        adapt(c, main)


# Where the teardown's frame shows its legends in the Half layout (hudbkgnd
# keyframes: Legend_A (154, 650), Legend_B (140, 621), Legend_X (164, 650),
# Legend_Y (180, 621); 420 x 40, visuals legend_A..legend_Y), in a hosted
# screen's own units: the screen sits in AppHostElementId at (30, 15), scale 0.96.
HOST_AT, HOST_SCALE = (30.0, 15.0), 0.96
LEGEND_AT = {"btnA": (154.0, 650.0), "btnB": (140.0, 621.0), "btnX": (164.0, 650.0), "btnY": (180.0, 621.0)}
# The Full layout's (the same keyframes at EndHalfToFull, frame 119): A and B
# move right with the blade (589, 572); X and Y stay. For the Full screens
# (HUDScene P0 2: the keyboard, Save Gamer Profile, Edit Gamer Profile...),
# whose legends sat on the left under the Half positions.
LEGEND_AT_FULL = dict(LEGEND_AT, btnA=(589.0, 650.0), btnB=(572.0, 621.0))


def legend_at(scene_props):
    return LEGEND_AT_FULL if scene_props.get("P0") == 2 else LEGEND_AT
LEGEND_SIZE = (420.0, 40.0)
LEGEND_VISUAL = {"btnA": "legend_A", "btnB": "legend_B", "btnX": "legend_X", "btnY": "legend_Y"}


def own_legend(root, first_page=False):
    """The scene's legend text in its own legend buttons, shown where and as the
    teardown's frame shows its legends (above); the scene's legend properties go,
    so the console's own (off-screen) legend gets none. On a Guide's first page
    (first_page) B is only drawn: the press goes on to the Guide, which closes
    (a back button with nothing to go back to kept it open: 0.25)."""
    for scene in root["children"]:
        if scene["class"] != "HUDScene":
            continue
        sp = scene["props"]
        texts = {k: sp.pop(k) for k in LEGEND_PROPS if k in sp}
        for c in scene["children"]:
            cid = c["props"].get("Id")
            key = LEGEND_BUTTONS.get(cid)
            if key and texts.get(key):
                c["props"]["Show"] = True
                c["props"]["Text"] = texts[key]
                fx, fy = legend_at(sp)[cid]
                sp_x, sp_y = sp.get("Position", ("V", 0.0, 0.0, 0.0))[1:3]
                c["props"]["Position"] = ("V", (fx - HOST_AT[0]) / HOST_SCALE - sp_x,
                                          (fy - HOST_AT[1]) / HOST_SCALE - sp_y, 0.0)
                # The legend visuals' parts sit at fixed places for 420 x 40 (no
                # anchoring): the button keeps that size and is scaled out of the
                # host's 0.96, so it draws exactly as the frame's legend did.
                c["props"]["Width"], c["props"]["Height"] = LEGEND_SIZE
                c["props"]["Scale"] = ("V", 1.0 / HOST_SCALE, 1.0 / HOST_SCALE, 1.0)
                c["props"]["Visual"] = LEGEND_VISUAL[cid]
                if cid == "btnB" and first_page:
                    c["class"] = "XuiControl"
                    c["props"].pop("PressKey", None)
                    c["props"].pop("ClassOverride", None)
                if c["class"] in ("XuiButton", "XuiNavButton") and cid in VK_PAD:
                    c["props"]["PressKey"] = VK_PAD[cid]


def idle_player(player):
    """The mini player as 1888's code shows it with nothing playing (hud.xex
    913E96D8: volume mode off, so sliderVolume and imgVolume hidden; 913E9C28
    then 913E9D58: btnPlaylist reads hud string 28, the five controls disabled)."""
    for id_ in ("sliderVolume", "imgVolume"):
        find(player, id_)["props"]["Show"] = False
    find(player, "btnPlaylist")["props"]["Text"] = "Select Music"
    for id_ in ("btnPlay", "btnPrev", "btnNext", "btnPlaybackMode", "btnVolume"):
        find(player, id_)["props"]["Enabled"] = False


def screen(scenes, name, shared):
    r, root = read(os.path.join(scenes, "hud#" + name))
    # 1888's code filled these slots when the main screen opened.
    for slot, sub in (("scnCommunity", "CommunityBar.xur"), ("scnMusic", "MiniMediaPlayer.xur")):
        target = find(root, slot)
        if target is None:
            continue
        _, subroot = read(os.path.join(scenes, "hud#" + sub))
        target["children"] = subroot["children"]
        if sub == "MiniMediaPlayer.xur":
            idle_player(target)
    if name == "Options.xur":
        recorder_row(root)
        plugins_row(root)
        achievements_row(root)
    if name in ("MainMenuSignedIn.xur", "MainMenuSignedOut.xur"):
        recorder_panel(scenes, root)
    if name == "MainMenuSignedOut.xur":
        no_recover(root)
    adapt(root, name in MAIN_SCREENS)
    own_legend(root, name in MAIN_SCREENS)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- The screen recorder (SlopGuide's: plugin/Recorder.inc) --------------------------
# A recorder for the screen, with a Guide section to run it. Personal Settings gets a row for it above Shut Down (as its own rows:
# 45 apart); the row opens Screen Recorder, made of 1888's own parts:
# Personal Settings' heading, legends and row button, and Voice's wrapping
# text and one-line body label. Its button and status line are SlopGuide's
# (Guide1888.inc RecorderScreen).
def recorder_row(root):
    scene = root["children"][0]
    kids = {c["props"].get("Id"): c for c in scene["children"]}
    off, last = kids["btnTurnOff"], kids["btnPersonalization"]
    row = copy.deepcopy(off)
    y = off["props"]["Position"][2]
    row["props"].update(Id="btnRecorder", Text="Screen Recorder", NavUp="btnPersonalization", NavDown="btnTurnOff")
    p = off["props"]["Position"]
    off["props"]["Position"] = ("V", p[1], y + 45.0, p[3])
    off["props"]["NavUp"] = "btnRecorder"
    last["props"]["NavDown"] = "btnRecorder"
    scene["children"].append(row)


def recorder_panel(scenes, root):
    """The Guide main menu's Screen Recorder panel (on
    the Guide itself, in the XBOX 360 banner's place at the bottom - the
    banner, btnGameBanner, stays under it for when the panel's hidden:
    Personal Settings > Screen Recorder > Show on Guide). The music panel's
    own parts: its pane (btn_MetaPane_static) and its song button
    (btn_single) as Record and Stop - 1888's Guide has no record or stop
    pictures - and a one-line label (Voice's Label_Body_1line) for the state.
    Which one shows: SlopGuide (Guide1888.inc RecorderPanel)."""
    _, music = read(os.path.join(scenes, "hud#MiniMediaPlayer.xur"))
    _, voice = read(os.path.join(scenes, "hud#OptionsVoice.xur"))
    mkids = {c["props"].get("Id"): c for c in music["children"][0]["children"]}
    vkids = {c["props"].get("Id"): c for c in voice["children"][0]["children"]}
    # The user's layout, from their mockup (2026-10-20), back to front: an
    # outer frame (the music panel's own, btn_MetaPane_static: its outline and
    # shadow) round the label and an inner frame (the same), the inner one
    # holding four tiles that fill its width; the label top left, small. The
    # panel's origin is the outer frame's corner.
    def pane(x, y, w, h, id_):
        p = copy.deepcopy(mkids["paneSongs"])
        p["props"].update(Id=id_, Position=("V", x, y, 0.0), Width=w, Height=h)
        return p
    outer = pane(0.0, 0.0, REC_OUTER[0], REC_OUTER[1], "paneRecorder")
    ix, iy = REC_INNER[0], REC_INNER[1]
    inner = pane(ix, iy, REC_INNER[2], REC_INNER[3], "paneRecorderButtons")
    # Four icon buttons (icons drawn in 1888's style, tools/recorder_icons.py:
    # 1888 has no record, stop or stream picture), in the music panel's
    # transport row container; their visual is recTile (recorder_visuals).
    row = copy.deepcopy(mkids["sceneControlsContainer"])
    template = next(c for c in row["children"] if c["props"].get("Id") == "btnPrev")
    buttons = []
    # (Screenshot added after Stop: in-game screenshots.)
    ids = ("btnRecStart", "btnRecStop", "btnRecShot", "btnRecStream", "btnRecSettings")
    icons = ("recRecord.png", "recStop.png", "recShot.png", "recStream.png", "recSettings.png")
    n = len(ids)
    for i, (bid, icon) in enumerate(zip(ids, icons)):
        b = copy.deepcopy(template)
        b["props"].update(Id=bid, ImagePath=icon, Position=("V", (REC_TILE_W + REC_GAP) * i, 0.0, 0.0),
                          Width=REC_TILE_W, Height=REC_TILE_H, Visual="recTile",
                          NavLeft=ids[(i - 1) % n], NavRight=ids[(i + 1) % n])
        buttons.append(b)
    row["children"] = buttons
    row["props"].update(Id="sceneRecorderButtons", Position=("V", ix + REC_ROW[0], iy + REC_ROW[1], 0.0),
                        Width=n * REC_TILE_W + (n - 1) * REC_GAP, Height=REC_TILE_H + 6.0)
    row["props"]["DefaultFocus"] = "btnRecStart"   # (the panel's focus goes to the row, the row's to Record:
    row["props"].pop("NavDown", None)               #  named past the row, Record couldn't be reached, 0.97)
    # Above the tiles, the highlighted one's name (SlopGuide RecorderScreen).
    state = copy.deepcopy(vkids["LabelSubHeader2"])
    state["props"].update(Id="txtRecordState", Text="Screen Recorder",
                          Position=("V", ix + REC_LABEL[0], iy + REC_LABEL[1], 0.0),
                          Width=REC_INNER[2] - 2 * REC_LABEL[0], Height=32.0, Visual="recLabel")
    scene = root["children"][0]
    banner = next(c for c in scene["children"] if c["props"].get("Id") == "btnGameBanner")
    panel = node("XuiScene", "scnRecorderPanel", REC_OUTER[0], REC_OUTER[1], Position=banner["props"]["Position"],
                 NavUp="scnMusic", DefaultFocus="sceneRecorderButtons")
    panel["children"] = [outer, inner, row, state]
    panel["has_children"] = True
    banner["props"]["Show"] = False
    for c in scene["children"]:
        if c["props"].get("Id") == "scnMusic":
            c["props"]["NavDown"] = "scnRecorderPanel"
    scene["children"].append(panel)
    place_recorder(scene)


# The recorder panel's layout, in scene units: the outer frame's size; the inner
# frame (x, y, width, height), the tile row (x, y in the inner frame), the gap
# between tiles, the label (x, y in the inner frame); the outer frame's corner
# from the music panel's. The outer frame is the music panel's width, on its
# left edge. Measured on the console (2026-10-20): the panes and tiles draw on
# their boxes; a pane under about 50 high draws a dark notch mid-side; the
# label's text (15 points) started 4.5 right of its box, its capitals 8 down,
# its baseline 23; at 20 points, about 5, 10 and 28. So: the inner frame 7 in
# from the outer on three sides and 7 under the label's baseline, the tiles 7
# in from the inner frame all round, the label's text on the tiles' left edge.
REC_OUTER = (420.0, 94.0)
REC_INNER = (7.0, 35.0, 406.0, 52.0)
REC_ROW = (7.0, 7.0)
REC_GAP = 4.0
REC_LABEL = (2.0, -37.0)
REC_UNDER_MUSIC = (0.0, 112.0)


def place_recorder(scene):
    """Under the music panel (both menus), as in the mockup."""
    kids = {c["props"].get("Id"): c for c in scene["children"]}
    at = kids["scnMusic"]["props"]["Position"]
    kids["scnRecorderPanel"]["props"]["Position"] = ("V", at[1] + REC_UNDER_MUSIC[0], at[2] + REC_UNDER_MUSIC[1], 0.0)


def no_recover(root):
    """The signed-out main menu without Recover Gamertag (an Xbox Live
    service, gone), its other parts spread evenly down
    the blade between Sign In's top and the bottom panel's bottom."""
    scene = root["children"][0]
    kids = {c["props"].get("Id"): c for c in scene["children"]}
    kids["btnRecoverProfile"]["props"].update(Show=False, Enabled=False, Position=OFF)
    # Pulled up into Recover Gamertag's place with 1888's own steps between them
    # (its Personal Settings sat 5 into the row above it, 346 under 277 + 74;
    # the music panel 56 under it, the banner 109 under that). Spread out
    # evenly, they floated.
    def move(i, y):
        at = kids[i]["props"]["Position"]
        kids[i]["props"]["Position"] = ("V", at[1], y, at[3])
    new = kids["btnNewProfile"]["props"]
    options = new["Position"][2] + new["Height"] - 5.0
    move("btnOptions", options)
    move("scnMusic", options + 56.0)
    place_recorder(scene)
    kids["btnNewProfile"]["props"]["NavDown"] = "btnOptions"
    kids["btnOptions"]["props"]["NavUp"] = "btnNewProfile"


def streaming_screen(scenes, shared):
    """Screen Recorder's Stream (the Guide panel's third button): the streaming
    services to link, as Personal Settings' rows (Options.xur), each Not
    Connected until linking is built (SlopGuide: Coming Soon)."""
    r, root = read(os.path.join(scenes, "hud#Options.xur"))
    _, voice = read(os.path.join(scenes, "hud#OptionsVoice.xur"))
    scene = root["children"][0]
    vkids = {c["props"].get("Id"): c for c in voice["children"][0]["children"]}
    kids = {c["props"].get("Id"): c for c in scene["children"]}
    keep = [kids["btnA"], kids["btnB"], kids["labelHeading"]]
    kids["labelHeading"]["props"]["Text"] = "Streaming"
    services = (("btnStreamTwitch", "Twitch"), ("btnStreamYouTube", "YouTube"), ("btnStreamKick", "Kick"))
    ids = [i for i, _ in services]
    for n, (bid, name) in enumerate(services):
        row = copy.deepcopy(kids["btnOnlineStatus"])
        row["props"].update(Id=bid, Text=name + ": Not Connected", Position=("V", 144.0, 195.0 + 45.0 * n, 0.0))
        row["props"].pop("NavDown", None)
        row["props"].pop("NavUp", None)
        if n:
            row["props"]["NavUp"] = ids[n - 1]
        if n < len(ids) - 1:
            row["props"]["NavDown"] = ids[n + 1]
        keep.append(row)
    about = copy.deepcopy(vkids["LabelSubHeader1"])
    about["props"].update(Id="txtStreamAbout", Text="Link an account to stream from the Guide's Screen Recorder.")
    state = copy.deepcopy(vkids["LabelSubHeader2"])
    state["props"].update(Id="txtStreamState", Text="", Position=("V", 144.0, 340.0, 0.0))
    keep += [about, state]
    scene["children"] = keep
    scene["props"]["Id"] = "scnStreaming"
    scene["props"].pop("ClassOverride", None)
    scene["props"]["DefaultFocus"] = ids[0]
    adapt(root, False)
    own_legend(root, False)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


def recorder_screen(scenes, shared):
    r, root = read(os.path.join(scenes, "hud#Options.xur"))
    _, voice = read(os.path.join(scenes, "hud#OptionsVoice.xur"))
    scene = root["children"][0]
    vkids = {c["props"].get("Id"): c for c in voice["children"][0]["children"]}
    keep = []
    for c in scene["children"]:
        i = c["props"].get("Id")
        if i in ("btnA", "btnB"):
            keep.append(c)
        elif i == "labelHeading":
            c["props"]["Text"] = "Screen Recorder"
            keep.append(c)
        elif i == "btnOnlineStatus":
            c["props"].update(Id="btnRecShow", Text="Show on Guide: On")
            c["props"].pop("NavDown", None)
            c["props"]["Position"] = ("V", 144.0, 195.0, 0.0)
            keep.append(c)
    about = copy.deepcopy(vkids["LabelSubHeader1"])
    about["props"].update(Id="txtRecordAbout",
                          Text="Record and Stop are on the Guide. Recordings are saved to Media > Recordings.")
    keep += [about]
    scene["children"] = keep
    scene["props"]["Id"] = "scnRecorder"
    scene["props"].pop("ClassOverride", None)       # (no 17559 class: SlopGuide runs it)
    scene["props"]["DefaultFocus"] = "btnRecShow"
    adapt(root, False)
    own_legend(root, False)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- Plugins (SlopGuide's: Plugins.inc, Guide1888.inc PluginsScreen) -------------
# Finer control of the plugins being run than editing launch.ini.
# Personal Settings gets a row for it under Screen Recorder (above Shut Down,
# 45 below); its screen is Screen Recorder's own make, with rows 45 apart:
# Performance (all off), Debug (all on), Debug Tools, then a row per plugin
# (SlopGuide words them, and hides the ones it doesn't need).
PLUGIN_ROWS = 6


def plugins_row(root):
    scene = root["children"][0]
    kids = {c["props"].get("Id"): c for c in scene["children"]}
    off, rec = kids["btnTurnOff"], kids["btnRecorder"]
    row = copy.deepcopy(rec)
    p = off["props"]["Position"]
    row["props"].update(Id="btnPlugins", Text="Plugins", NavUp="btnRecorder", NavDown="btnTurnOff",
                        Position=p)
    off["props"]["Position"] = ("V", p[1], p[2] + 45.0, p[3])
    off["props"]["NavUp"] = "btnPlugins"
    rec["props"]["NavDown"] = "btnPlugins"
    scene["children"].append(row)


def achievements_row(root):
    """Personal Settings > Achievements: above Shut Down,
    as Plugins' row. SlopGuide opens the console's achievements for the Guide's
    player there (XShowAchievementsUI; the scenes: achievements() below)."""
    scene = root["children"][0]
    kids = {c["props"].get("Id"): c for c in scene["children"]}
    off, above = kids["btnTurnOff"], kids["btnPlugins"]
    row = copy.deepcopy(above)
    p = off["props"]["Position"]
    row["props"].update(Id="btnAchievements", Text="Achievements", NavUp="btnPlugins", NavDown="btnTurnOff",
                        Position=p)
    off["props"]["Position"] = ("V", p[1], p[2] + 45.0, p[3])
    off["props"]["NavUp"] = "btnAchievements"
    above["props"]["NavDown"] = "btnAchievements"
    scene["children"].append(row)


def plugins_screen(scenes, shared):
    r, root = read(os.path.join(scenes, "hud#Options.xur"))
    scene = root["children"][0]
    keep, button = [], None
    for c in scene["children"]:
        i = c["props"].get("Id")
        if i in ("btnA", "btnB"):
            keep.append(c)
        elif i == "labelHeading":
            c["props"]["Text"] = "Plugins"
            keep.append(c)
        elif i == "btnOnlineStatus":
            button = c
    rows = [("btnAllOff", "Performance: All Off"), ("btnAllOn", "Debug: All On"),
            ("btnDebugTools", "Debug Tools: Off")]
    rows += [("btnPlugin%d" % n, "") for n in range(PLUGIN_ROWS)]
    for n, (rid, text) in enumerate(rows):
        b = copy.deepcopy(button)
        b["props"].update(Id=rid, Text=text, Position=("V", 144.0, 150.0 + 45.0 * n, 0.0))
        b["props"].pop("NavUp", None)
        b["props"].pop("NavDown", None)
        if n:
            b["props"]["NavUp"] = rows[n - 1][0]
        if n + 1 < len(rows):
            b["props"]["NavDown"] = rows[n + 1][0]
        keep.append(b)
    scene["children"] = keep
    scene["props"]["Id"] = "scnPlugins"
    scene["props"].pop("ClassOverride", None)       # (no 17559 class: SlopGuide runs it)
    scene["props"]["DefaultFocus"] = "btnAllOff"
    adapt(root, False)
    own_legend(root, False)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- Trainer (SlopGuide's: Trainers.inc, Guide1888.inc TrainerScreen) -------------
# Start + Back in a game opens the Guide on this screen: 1888's
# Notifications screen's own parts (its heading, its tick box, its line of
# text), a tick box a cheat, 37 apart from under the line to above the legends.
CHEAT_ROWS = 12


def trainer_screen(scenes, shared):
    r, root = read(os.path.join(scenes, "hud#OptionsNotifications.xur"))
    scene = root["children"][0]
    kids = {c["props"].get("Id"): c for c in scene["children"]}
    keep = [kids[i] for i in ("artPanel", "legend_a", "btnB") if i in kids]
    heading = kids["labelHeading"]
    heading["props"]["Text"] = "Trainer"
    keep.append(heading)
    line = kids["XuiLabel1"]
    line["props"].update(Id="txtTrainerName", Text="", Height=40.0)
    keep.append(line)
    box = kids["chkShow"]
    rows = ["chkCheat%d" % n for n in range(CHEAT_ROWS)]
    for n, rid in enumerate(rows):
        b = copy.deepcopy(box)
        b["props"].update(Id=rid, Text="", Height=36.0, Position=("V", 144.0, 150.0 + 37.0 * n, 0.0))
        b["props"].pop("NavUp", None)
        b["props"].pop("NavDown", None)
        if n:
            b["props"]["NavUp"] = rows[n - 1]
        if n + 1 < len(rows):
            b["props"]["NavDown"] = rows[n + 1]
        keep.append(b)
    scene["children"] = keep
    scene["props"]["Id"] = "scnTrainer"
    scene["props"].pop("ClassOverride", None)       # (no 17559 class: SlopGuide runs it)
    scene["props"]["DefaultFocus"] = rows[0]
    adapt(root, False)
    own_legend(root, False)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- Sign In (signin.xex) -----------------------------------------------------------
# The console's own sign-in (17559's signin.xex) creates the same three scenes
# 1888's did: signin1.xur (the Guide page, its Scene_Pane0 empty), then
# UserList.xur (or UserListSinglePane.xur) and status.xur in that pane. SlopGuide
# hands it 1888's (SI_*.xur here), so the console's code lists the profiles and
# signs in; their classes stay for it (UserListScene, UserList, StatusScene,
# SelectedProfile: the same names in both). 1888's page classes (SignIn,
# LogonPane) go: 17559's page has none.
SIGNIN_KEEP = {"UserListScene", "UserList", "StatusScene", "SelectedProfile"}


def keep_classes(n, keep):
    """adapt() for the sign-in scenes: their own classes stay."""
    saved = {}

    def take(x, path):
        co = x["props"].get("ClassOverride")
        if co in keep:
            saved[path] = co
        for i, c in enumerate(x["children"]):
            take(c, path + (i,))

    def put(x, path):
        if path in saved:
            x["props"]["ClassOverride"] = saved[path]
        for i, c in enumerate(x["children"]):
            put(c, path + (i,))

    take(n, ())
    adapt(n, False)
    put(n, ())


def signin_page(scenes, shared):
    """signin1.xur: 1888's Sign In page (header "Sign In", Scene_Pane0 for the
    console's code to fill). Its root takes 17559's id (1888's was spelt
    Scene_Sigin). Legends as signin.xex set them: A Select, B Back, X "Recover
    Gamertag", Y "Create New Profile" (its strings 40 and 5)."""
    r, root = read(os.path.join(scenes, "signin#signin1.xur"))
    page = root["children"][0]
    page["props"]["Id"] = "Scene_Signin"
    page["props"]["P3"] = "Recover Gamertag"
    page["props"]["P4"] = "Create New Profile"
    page["children"] += [node("XuiButton", "btnA", 420, 40), node("XuiBackButton", "btnB", 420, 40),
                         node("XuiNavButton", "btnX", 420, 40), node("XuiNavButton", "btnY", 420, 40)]
    adapt(root, False)
    own_legend(root, True)      # the console opens its sign-in as the Guide's first page
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


def signin_part(scenes, name, shared):
    """UserList.xur / status.xur: 1888's, their classes kept. status gets the
    one part 17559's code looks up that 1888's never had (txtDataCenterMode),
    hidden."""
    r, root = read(os.path.join(scenes, "signin#" + name))
    if name == "status.xur":
        root["children"][0]["children"].append(
            node("XuiLabel", "txtDataCenterMode", 272, 64, Show=False))
    keep_classes(root, SIGNIN_KEEP)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- Create Profile (createprofile.xex) ---------------------------------------------
# 17559's createprofile.xex makes the same three scenes as 1888's, with the same
# classes and parts (CreateGamerProfile.xur: GamerTagScene, nextButton,
# gamerTagText; AddXboxLive.xur: AddXBoxLiveScene, waitCursorControl,
# statusText, backButton; Congratulations.xur). SlopGuide hands it 1888's
# (CP_*.xur), classes kept for its code; their legends drawn as the frame did
# (display only: the scenes' own buttons take the presses).
CREATEPROFILE = ("CreateGamerProfile.xur", "AddXboxLive.xur", "Congratulations.xur")


def keep_all_classes(n):
    """adapt() for a module the console runs itself: every class stays."""
    saved = []

    def take(x):
        saved.append(x["props"].get("ClassOverride"))
        for c in x["children"]:
            take(c)

    def put(x, it):
        co = next(it)
        if co:
            x["props"]["ClassOverride"] = co
        for c in x["children"]:
            put(c, it)

    take(n)
    adapt(n, False)
    put(n, iter(saved))


def display_legends(root):
    """The page's legends (HUDScene P1..P4) as drawn-only controls."""
    for scene in root["children"]:
        if scene["class"] != "HUDScene":
            continue
        have = {c["props"].get("Id") for c in scene["children"]}
        for cid, key in LEGEND_BUTTONS.items():
            if scene["props"].get(key) and cid not in have:
                scene["children"].append(node("XuiControl", cid, 420, 40))
    own_legend(root)


DEVICESELECTOR = ("DeviceSelector.xur", "DeviceFull.xur")
# Parts 17559's device selector has that 1888's didn't (17559 deviceselector.xex,
# media#DeviceSelector.xur): its code may look them up, so they're there, hidden.
DS_EXTRA = {"DeviceSelector.xur": (("XuiLabel", "txtNoDevices", 420, 219, "XuiLabelCenterJustify"),
                                   ("XuiLabel", "ProgressAnimation", 40, 40, "Loading_Small"))}


def deviceselector(scenes, name, shared):
    """1888's device selector (deviceselector.xex, media#), for the console's
    own: the same classes (DeviceSelectorScene, DeviceList), which 17559's code
    drives; its legends shown as the Guide shows every screen's."""
    r, root = read(os.path.join(scenes, "media#" + name))
    keep_all_classes(root)
    scene = root["children"][0]
    have = {c["props"].get("Id") for c in scene["children"]}
    for cls, id_, w, h, visual in DS_EXTRA.get(name, ()):
        if id_ not in have:
            scene["children"].append(node(cls, id_, w, h, Show=False, Visual=visual))
    display_legends(root)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


def find_id(n, id_):
    for c in n["children"]:
        if c["props"].get("Id") == id_:
            return c
        f = find_id(c, id_)
        if f:
            return f
    return None


def createprofile(scenes, name, shared):
    r, root = read(os.path.join(scenes, "cp#" + name))
    keep_all_classes(root)
    # Save Gamer Profile's "Customize Profile" opens Edit Gamer Profile: named
    # as that screen's header.
    edit = find_id(root, "editProfileButton") if name == "Congratulations.xur" else None
    if edit:
        edit["props"]["Text"] = "Edit Gamer Profile"
        # Its card: Zone in Gamerscore's place, no Achievements (zone_row).
        zone_row(root["children"][0], "titlesPlayedLabel", ["GSTextnumber", "GSIcon", "GSText"], "GSTextnumber",
                 789.0, 200.0, ["achievementsLabel", "achievementsnumber"])
    display_legends(root)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- The console's gamer profile screens (gamerprofile.xex: gp#) ------------------
# Same scene names and classes as 1888's (Edit Profile is 808_editProfile.xur,
# class EditProfileScene, in both). 17559's have parts 1888's never had (Avatar,
# Name, Location, Bio, the personal card): its code may look them up, so
# they're there, hidden and off-screen. So 1888's screen shows, and the avatar
# editor (a 17559 title, which left SlopDash for the 17559 dashboard: the user,
# 2026-10-15) can't be reached. 17559's scenes: Assets/17559/gp (from the
# console's flash).
GAMERPROFILE = ("808_editProfile.xur", "807_EditGamerTile.xur", "809_ChangeGamerTile.xur",
                "SelectGamerzone.xur", "GamerZoneInfo.xur")
SCHEMA_17559 = xur.Schema(os.path.join(HERE, "xui_schema_17559.txt"))
# 17559's scenes' outline (tools/gp17559_parts.py writes it from them): each
# part's Id, class and ClassOverride, as a tree. add_missing needs no more, so
# the Guide builds without 17559's own files.
GP_17559_PARTS = os.path.join(HERE, "gp17559_parts.json")


def outline17559(name):
    """A scene of the outline, in the shape read() gives (class, props, children)."""
    def node_of(o):
        props = {k: o[k] for k in ("Id", "ClassOverride") if k in o}
        return {"class": o["class"], "props": props, "children": [node_of(c) for c in o.get("children", [])]}
    return node_of(json.load(open(GP_17559_PARTS))[name])


def read17559(path):
    import guideskin
    import xur8
    extra = {"HUDScene": guideskin.HUD_SCENE,
             "XuiGamerCard": ("XuiControl", [xur.Prop(0, "P0", xur.STRING, 0), xur.Prop(1, "P1", xur.STRING, 0)])}
    return xur8.Reader(open(path, "rb").read(), SCHEMA_17559, extra).root()[0]


FIGURE_POINTS = []      # an 1888 figure's outline, for stand-in figures (gamerprofile)


def first_points(n):
    for c in n["children"]:
        if c["class"] == "XuiFigure" and c["props"].get("Points"):
            return c["props"]["Points"]
        p = first_points(c)
        if p:
            return p
    return None


def add_missing(mine, theirs):
    """Every part of theirs (by Id, under the same parent) that mine lacks, as a
    hidden, off-screen stand-in of the same class."""
    have = {c["props"].get("Id"): c for c in mine["children"] if c["props"].get("Id")}
    for c in theirs["children"]:
        cid = c["props"].get("Id")
        if not cid:
            continue
        if cid in have:
            add_missing(have[cid], c)
            continue
        props = {"Position": OFF, "Show": False}
        if c["props"].get("ClassOverride"):
            props["ClassOverride"] = c["props"]["ClassOverride"]
        if c["class"] in ("XuiButton", "XuiNavButton"):
            props["Enabled"] = False
        # A figure needs its outline: one without points crashed the
        # dashboard (xam 17559 8178313C, reading the null points while
        # scaling them: GamerCardBackground on Edit Gamer Profile and Change
        # Gamer Picture). 1888's own outline, borrowed.
        if c["class"] == "XuiFigure" and FIGURE_POINTS:
            props["Points"] = FIGURE_POINTS[0]
        stand_in = node(c["class"] if c["class"] in SCHEMA.bases or c["class"] in EXTRA else "XuiControl", cid, 40, 40, **props)
        add_missing(stand_in, c)
        mine["children"].append(stand_in)


def offline_online_rows(root):
    """Every profile gets 1888's online rows (SlopGuide GpOnlineRows: Motto and
    Gamer Zone for offline profiles too). Their Gamertag
    row renames the profile there (Profile Name's slot): labelled so. Privacy
    Settings is Xbox Live's, and 17559 never looks it up: hidden."""
    g = find_id(root, "onlineButtonGroup")
    if not g:
        return
    tag, privacy = find_id(g, "gamerTagButton"), find_id(g, "privacySettingsButton")
    if tag:
        tag["props"]["Text"] = "Profile Name"
        tag["props"].pop("NavDown", None)
    if privacy:
        privacy["props"].update(Show=False, Enabled=False, Position=OFF)
        privacy["props"].pop("NavUp", None)


def show_motto(root):
    """The profile's motto, which 17559's code writes into labMotto (a hidden
    stand-in here, so a saved motto showed nowhere):
    shown under 1888's gamer card, as a plain label like Save Gamer Profile's
    text (no visual: the skin's default). The card draws its Status panel
    below its own 134 high box, down to y 464 (the console's screen,
    2026-10-17: the motto at the box's foot sat on "Status"): under that."""
    lab = find_id(root, "labMotto")
    card = find_id(root, "gamerCard")
    if not lab or not card:
        return
    cx, cy = card["props"]["Position"][1], card["props"]["Position"][2]
    lab["props"].update(Show=True, Position=("V", cx + 6.0, 472.0, 0.0),
                        Width=card["props"]["Width"] - 12.0, Height=37.0)


def add_done(root):
    """Edit Gamer Profile's Done (Save Gamer Profile has
    one, this screen had only B): a row under each button group's last, as
    that row (its visual, size, spacing), going where B goes (SlopGuide)."""
    for group in ("offlineButtonGroup", "onlineButtonGroup"):
        g = find_id(root, group)
        if not g or find_id(g, "doneButton"):
            continue
        rows = [c for c in g["children"] if c["class"] == "XuiButton" and c["props"].get("Show", True) and
                c["props"].get("Position") != OFF]      # (not the stand-ins: add_missing)
        last = rows[-1]
        y = last["props"]["Position"][2] if last["props"].get("Position") else 0.0
        step = 45.0
        done = copy.deepcopy(last)
        done["props"]["Id"] = "doneButton"
        done["props"]["Text"] = "Done"
        done["props"]["Position"] = ("V", 0.0, y + step, 0.0)
        done["props"]["NavUp"] = last["props"]["Id"]
        done["props"].pop("NavDown", None)
        last["props"]["NavDown"] = "doneButton"
        g["children"].append(done)
        g["props"]["Height"] = max(g["props"].get("Height", 0.0), y + step + done["props"].get("Height", 47.0))


def gamerprofile(scenes, name, shared):
    if not FIGURE_POINTS:
        FIGURE_POINTS.append(first_points(read(os.path.join(scenes, "gp#809_ChangeGamerTile.xur"))[1]))
    r, root = read(os.path.join(scenes, "gp#" + name))
    keep_all_classes(root)
    theirs = outline17559(name)
    for mine_scene, their_scene in zip(root["children"], theirs["children"]):
        add_missing(mine_scene, their_scene)
    if name == "808_editProfile.xur":
        offline_online_rows(root)
        add_done(root)
        show_motto(root)
    display_legends(root)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- Achievements (gamerprofile.xex) ----------------------------------------------
# The Guide's achievements. The console's own
# gamerprofile code runs them (it reads the signed-in profile itself, a game's
# fresh unlocks included); SlopGuide hands it 1888's scenes, as for Edit Gamer
# Profile. 17559's scene -> 1888's:
#   820_GameShowcaseMe   games played      -> 1888's 820 (the same classes)
#   802_Achievements     one game's       -> 1888's 821-2 (17559's is a grid; its
#                                            parts renamed onto 1888's list)
#   828_AchievDetails    one achievement  -> 1888's 828 (its picture part made the
#                                            image presenter 17559's code fills)
#   804_LoadAchievements loading          -> 1888's 820 frame, heading only
#   GamerCardScene       your profile     -> 1888's Edit Gamer Profile (808): its
#                                            card, its rows as 17559's (View Games,
#                                            Edit Profile...). The Guide's gamer
#                                            card opens it (XShowGamerCardUI).
# Every part 17559's code looks up is there (add_missing: 17559's outline,
# tools/gp17559_parts.json).
ACHIEVEMENTS = {"GamerCardScene.xur": "808_editProfile.xur",
                "820_GameShowcaseMe.xur": "820_GameShowcaseMe.xur",
                "802_Achievements.xur": "821-2_GameAchievStatsMe.xur",
                "828_AchievDetails.xur": "828_AchievDetails.xur",
                "804_LoadAchievements.xur": "820_GameShowcaseMe.xur"}


def rename(root, old, new, **props):
    n = find_id(root, old)
    if n is not None:
        n["props"]["Id"] = new
        n["props"].update(props)
    return n


def achievements_802(root):
    """1888's achievement list (821-2) as 17559's 802: the scene and list take
    17559's names and classes; its labels for the focused achievement (name,
    gamerscore, date, description) go in a strip under the list, as 17559
    shows them above its grid."""
    scene = root["children"][0]
    scene["props"].update(Id="SceneMain", ClassOverride="CAchievementsGridScene")
    lst = rename(root, "achievementList", "AchievementList", ClassOverride="CAchievementsGridList")
    # 17559's list gives each achievement its picture and nothing else (17559
    # showed a grid of them, the focused one's words above): 1888's two-line
    # rows came out blank, the picture in the gamerscore's place (the console,
    # 2026-11-05). A grid of pictures, as Change Gamer Picture's, the focused
    # one's words in the strip under it (1888's dashboard did the same).
    lst["props"].update(Height=418.0, Visual="List_tileGrid")   # (to the line at 520)
    # 1888's placeholders ("<game title>", "<#> of <total #> Unlocked") showed
    # where 17559's code writes nothing (a title without achievements), and
    # the count ran under the clock (the console, 2026-11-05): empty, and the
    # count ends at x 860, left of the clock.
    rename(root, "headerText", "labGameTitle", Text="", Width=400.0)
    rename(root, "achievementCountText", "labCountText", Position=("V", 560.0, 44.0, 0.0), Width=300.0, Text="")
    rename(root, "noAchievementsText", "labEmpty")
    rename(root, "backButton", "legend_B")
    y = find_id(root, "yButton")
    if y is not None:
        scene["children"].remove(y)
    head = find_id(root, "labGameTitle")
    count = find_id(root, "labCountText")
    strip = (("labAchievementName", 144.0, 527.0, 560.0, 40.0, True),
             ("labPoints", 720.0, 527.0, 279.0, 40.0, False),
             ("labAchievementDescription", 144.0, 567.0, 560.0, 80.0, False),
             ("labAcquiredDate", 720.0, 567.0, 279.0, 40.0, False))
    for lid, x, yy, w, h, heading in strip:
        lab = copy.deepcopy(head if heading else count)
        lab["props"].update(Id=lid, Text="", Position=("V", x, yy, 0.0), Width=w, Height=h)
        scene["children"].append(lab)


def achievements_828(root):
    """1888's picture part (a brush control) as the image presenter 17559's
    code puts the achievement's picture in, where 1888's was."""
    # 17559's gamerscore text has the G in it: 1888's own G picture beside
    # it made two (the console, 2026-11-05), so it goes and the text takes
    # its room. The heading: the game's title, SlopGuide's (AchDetailsMade).
    scene = root["children"][0]
    g = find_id(root, "XuiImage1")
    if g is not None:
        scene["children"].remove(g)
    cred = find_id(root, "credText")
    if cred is not None:
        cred["props"]["Width"] = 170.0
    head = find_id(root, "headerText")
    if head is not None:
        head["props"]["Text"] = ""
    pic = find_id(root, "achievementImage")
    if pic is not None:
        pic["class"] = "XuiImagePresenter"
        pic["props"].pop("ClassOverride", None)
        pic["props"].pop("Visual", None)


GAMERCARD_ROWS = {
    "meOfflineButtonGroup": (("viewGamesButton", "View Games"), ("editProfileButton", "Edit Profile"),
                             ("gamerPreferencesButton", "Game Defaults"),
                             ("signInPreferencesButton", "Sign-in Preferences"), ("addLiveButton", "Join Xbox Live"),
                             ("fitnessButton", "Manage Exercise Info")),
    "meOnlineButtonGroup": (("viewGamesButton", "View Games"), ("editProfileButton", "Edit Profile"),
                            ("viewReputationButton", "View Rep"), ("gamerPreferencesButton", "Game Defaults"),
                            ("signInPreferencesButton", "Sign-in Preferences"),
                            ("accountSecurityButton", "Account Security"), ("fitnessButton", "Manage Exercise Info"))}


def gamercard_scene(root):
    """17559's GamerCardScene (your profile: View Games, Edit Profile...) in 1888's
    Edit Gamer Profile: its heading, card and rows (45 apart, as 808's), each
    group of rows as 17559's group, with 17559's buttons in 17559's order."""
    scene = root["children"][0]
    scene["props"].update(Id="gamerCardScene", ClassOverride="GamerCardUIScene")
    # Focus as 17559's: none on the scene (its code focuses the group it
    # shows), each group on View Games. 808's (offlineButtonGroup,
    # gamerTileButton) named parts this screen hasn't: nothing had focus and A
    # went to the Guide's root (the console, 2026-11-05).
    scene["props"].pop("DefaultFocus", None)
    rename(root, "txt_Header", "headerText", Text="Profile")
    wait = find_id(root, "waitCursorControl")       # (808's: 17559's code here never hides it)
    if wait is not None:
        scene["children"].remove(wait)
    groups = {"onlineButtonGroup": "meOnlineButtonGroup", "offlineButtonGroup": "meOfflineButtonGroup"}
    for old, new in groups.items():
        g = find_id(root, old)
        template = g["children"][0]
        rows = GAMERCARD_ROWS[new]
        kids = []
        for n, (rid, text) in enumerate(rows):
            b = copy.deepcopy(template)
            b["props"].update(Id=rid, Text=text, Position=("V", 0.0, 45.0 * n, 0.0))
            b["props"].pop("NavUp", None)
            b["props"].pop("NavDown", None)
            if n:
                b["props"]["NavUp"] = rows[n - 1][0]
            if n + 1 < len(rows):
                b["props"]["NavDown"] = rows[n + 1][0]
            kids.append(b)
        g["children"] = kids
        g["props"].update(Id=new, Height=45.0 * len(rows) + 2.0, DefaultFocus="viewGamesButton")


def achievements_804(root):
    """Loading: 1888's games frame with its heading, nothing to fill."""
    scene = root["children"][0]
    scene["props"].update(Id="SceneMain", ClassOverride="LoadAchievementsScene")
    scene["props"].pop("DefaultFocus", None)        # (820's gameList: not here)
    keep = []
    for c in scene["children"]:
        i = c["props"].get("Id")
        if i == "headerText":
            c["props"]["Text"] = "Achievements"
            keep.append(c)
        elif i == "backButton":
            c["props"]["Id"] = "legend_B"
            keep.append(c)
        elif c["class"] == "XuiFigure":
            keep.append(c)
    scene["children"] = keep


def achievements(scenes, theirs, shared):
    if not FIGURE_POINTS:
        FIGURE_POINTS.append(first_points(read(os.path.join(scenes, "gp#809_ChangeGamerTile.xur"))[1]))
    r, root = read(os.path.join(scenes, "gp#" + ACHIEVEMENTS[theirs]))
    keep_all_classes(root)
    if theirs == "GamerCardScene.xur":
        gamercard_scene(root)
    elif theirs == "802_Achievements.xur":
        achievements_802(root)
    elif theirs == "828_AchievDetails.xur":
        achievements_828(root)
    elif theirs == "804_LoadAchievements.xur":
        achievements_804(root)
    outline = outline17559(theirs)
    for mine_scene, their_scene in zip(root["children"], outline["children"]):
        add_missing(mine_scene, their_scene)
    display_legends(root)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- The keyboard (vk.xex) -------------------------------------------------------
# The console's keyboard (17559 vk.xex: KeyboardMain.xur, a HUDScene of class
# CKeyboardScene with a KeyboardPlaceholder, into which its code loads
# KeyboardBase.xur, class KeyboardControl) drew 17559's in the 1888 Guide: its
# text tiny, its right column outside the blade (Create Gamer Profile's gamertag).
# SlopGuide hands it these: 1888's keyboard (vk#Keyboard.xur),
# its layout and art, with every part 17559's code looks up (Assets/17559/vk,
# from the console's flash): its keys are KeysGroup\Key.<row>_<column>, 5 x 10,
# and the specials keep their names. 1888's 4 x 10 grid takes rows 0-3 (its
# letters 7 wide, then the digits 3 wide); row 4 is off-screen (SlopGuide gives
# 17559's pages 1888's characters, row 4 empty). The Japanese parts (KeysGroupJP,
# CandidateListGroup) are there, hidden.
VK_GRID = (("A", "B", "C", "D", "E", "F", "G", "1", "2", "3"),
           ("H", "I", "J", "K", "L", "M", "N", "4", "5", "6"),
           ("O", "P", "Q", "R", "S", "T", "U", "7", "8", "9"),
           ("V", "W", "X", "Y", "Z", "-", "@", "_", "0", "."))
VK_SPECIALS = ("Key.Left", "Key.Prev", "Key.Caps", "Key.Right", "Key.Next", "Key.OK", "Key.Spc", "Key.BS")
VK_LEGEND = {"legend_a": "btnA", "legend_b": "btnB", "legend_x": "btnX", "legend_y": "btnY"}


def vk_ids():
    """1888's key names to 17559's (Key.A -> Key.0_0 ...)."""
    m = {}
    for r, row in enumerate(VK_GRID):
        for c, k in enumerate(row):
            m["Key." + k] = "Key.%d_%d" % (r, c)
    return m


def vk_legends(scene, texts):
    """The keyboard's legends as every Guide screen's (own_legend), its buttons
    keeping their names and keys (17559's code uses legend_x and legend_y)."""
    for c in scene["children"]:
        cid = c["props"].get("Id")
        alias = VK_LEGEND.get(cid)
        text = texts.get(LEGEND_BUTTONS.get(alias, ""))
        if not alias or not text:
            continue
        p = c["props"]
        p["Show"], p["Text"], p["Visual"] = True, text, LEGEND_VISUAL[alias]
        fx, fy = LEGEND_AT_FULL[alias]          # (the keyboard is a Full screen)
        p["Position"] = ("V", (fx - HOST_AT[0]) / HOST_SCALE, (fy - HOST_AT[1]) / HOST_SCALE, 0.0)
        p["Width"], p["Height"] = LEGEND_SIZE
        p["Scale"] = ("V", 1.0 / HOST_SCALE, 1.0 / HOST_SCALE, 1.0)


def vk_main(scenes, shared):
    r, root = read(os.path.join(scenes, "vk#Keyboard.xur"))
    scene = root["children"][0]
    sp = scene["props"]
    texts = {k: sp.pop(k) for k in LEGEND_PROPS if k in sp}
    keep = [c for c in scene["children"] if c["props"].get("Id") in VK_LEGEND or c["props"].get("Id") == "Text_Header"]
    sp["Id"], sp["ClassOverride"], sp["DefaultFocus"] = "XuiHUDKeyboardScene", "CKeyboardScene", "KeyboardPlaceholder"
    scene["children"] = keep + [node("XuiScene", "KeyboardPlaceholder", *OLD_CANVAS)]
    vk_legends(scene, texts)
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


def vk_base(scenes, shared):
    r, root = read(os.path.join(scenes, "vk#Keyboard.xur"))
    old = root["children"][0]
    by = {c["props"].get("Id"): c for c in old["children"]}
    ids = vk_ids()

    def nav(p):
        for k in ("NavLeft", "NavRight", "NavUp", "NavDown"):
            if p.get(k) in ids:
                p[k] = ids[p[k]]

    def key(src, id_, **extra):
        k = copy.deepcopy(src)
        k["props"]["Id"] = id_
        k["props"]["ClassOverride"] = "VKXuiButton"
        nav(k["props"])
        k["props"].update(extra)
        return k

    def hidden(cls, id_, **props):
        return node(cls, id_, 47, 47, Position=OFF, Show=False, **props)

    def vkbutton(id_):
        return hidden("XuiButton", id_, ClassOverride="VKXuiButton", Visual="btn_KbrdChar")

    keys = node("XuiGroup", "KeysGroup", *OLD_CANVAS)
    keys["children"].append(vkbutton("Key.Hidden"))
    for r_ in range(5):
        for c in range(10):
            id_ = "Key.%d_%d" % (r_, c)
            keys["children"].append(key(by["Key." + VK_GRID[r_][c]], id_) if r_ < len(VK_GRID) else vkbutton(id_))
    keys["children"] += [vkbutton("Key.JComma"), vkbutton("Key.JPeriod")]
    keys["children"] += [key(by[id_], id_) for id_ in VK_SPECIALS]
    sep = by["AlphaSeparator"]
    kana = copy.deepcopy(sep)
    kana["props"].update(Id="KanaSeparator", Show=False)
    keys["children"] += [sep, kana]

    def edit(id_):
        e = copy.deepcopy(by["XuiEdit1"])
        e["props"]["Id"] = id_
        nav(e["props"])
        return e
    fmt = edit("FormattedText")
    for k in ("NavUp", "NavDown"):
        fmt["props"].pop(k, None)
    text2 = copy.deepcopy(by["XuiText2"])
    text2["props"]["Show"] = False
    shader = copy.deepcopy(sep)
    shader["props"].update(Id="ShaderFigure", Show=False, Position=OFF)

    cand = node("XuiGroup", "CandidateListGroup", *OLD_CANVAS, Show=False)
    cand["children"] = [hidden("XuiList", "CandidateList", ClassOverride="ClauseList")] + \
        [vkbutton(i) for i in ("Key.Shorten", "Key.PrevClause", "Key.Unused", "Key.Extend", "Key.NextClause",
                               "Key.Determine")] + [hidden("XuiLabel", "NumberOfList")]
    modal = node("XuiGroup", "grpModalKey", 47, 47, Position=OFF)
    modal["children"] = [vkbutton("Key.ModalMenu_" + s) for s in ("right", "left", "top", "bottom", "Center")]
    jp = node("XuiGroup", "KeysGroupJP", *OLD_CANVAS, Show=False)
    jp["children"] = [hidden("XuiLabel", "labUsage"), vkbutton("Key.Hidden")] + \
        [vkbutton("Key.%d_%d" % (a, b)) for a in range(4) for b in range(3)] + \
        [vkbutton(i) for i in ("Key.Left", "Key.Prev", "Key.Modifier", "Key.Right", "Key.Next", "Key.OK")] + \
        [hidden("XuiLabel", "NumberOfList"), hidden("XuiList", "SuggestList", ClassOverride="ClauseList"),
         vkbutton("Key.Spc"), vkbutton("Key.BS"), modal]

    scene = node("XuiScene", "XuiKeyboardScene", *OLD_CANVAS, ClassOverride="KeyboardControl",
                 DefaultFocus="KeysGroup\\Key.0_0", TransTo="TransTo")
    legends = []
    for id_ in ("legend_y", "legend_b", "legend_x", "legend_a"):
        lg = copy.deepcopy(by[id_])
        lg["props"].update(Show=False, Position=OFF)
        lg["props"].pop("Text", None)
        legends.append(lg)
    legends[1]["props"]["PressKey"] = 0     # (17559's base: its B goes on to the scene)
    scene["children"] = legends + [shader, fmt, edit("XuiEdit1"), copy.deepcopy(by["XuiText1"]), text2, cand,
                                   keys, jp, hidden("XuiImage", "KanaModeRomaji"), hidden("XuiImage", "KanaModeKana")]
    root["children"] = [scene]
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- xam's other scenes: notifications and the gamer card --------------------------
def notify(scenes, shared):
    """1888's notification pop-up; 17559's NotifyPopupScene has the same parts."""
    r, root = read(os.path.join(scenes, "xam#notify.xur"))
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


def zone_row(parent, label_like, gs_parts, value_like, value_at, value_width, hide):
    """A card's Gamerscore row as its Zone row (a new
    profile's gamerscore and achievements say nothing; the zone does): the
    Gamerscore parts and the Achievements row hidden; a "Zone" label made as
    the card's other row labels (label_like, at the Gamerscore row's height),
    and its value "zoneValue" as the card's numbers (value_like), filled in by
    SlopGuide (CardZoneInto)."""
    kids = {c["props"].get("Id"): c for c in parent["children"]}
    y = kids[gs_parts[0]]["props"]["Position"][2]
    for i in gs_parts + hide:
        kids[i]["props"]["Show"] = False
    label = copy.deepcopy(kids[label_like])
    label["props"].update(Id="zoneLabel", Text="Zone")
    p = label["props"]["Position"]
    label["props"]["Position"] = ("V", p[1], y, p[3])
    value = copy.deepcopy(kids[value_like])
    value["props"].update(Id="zoneValue", Text="", Width=float(value_width))
    q = value["props"]["Position"]
    value["props"]["Position"] = ("V", value_at, y, q[3])
    value["props"].pop("Show", None)
    parent["children"] += [label, value]


def gamercard(scenes, shared):
    """1888's gamer card, for 17559's GamerCardScene code to fill. That code
    requires two parts 1888's card never had (extendedScene's richPresenceText
    and consoleTypeImage): there, hidden."""
    r, root = read(os.path.join(scenes, "gamercrd#GamerCard.xur"))
    ext = find(root, "extendedScene")
    ext["children"] += [
        node("XuiLabel", "richPresenceText", 251, 44, Show=False),
        node("XuiImage", "consoleTypeImage", 95, 95, Show=False),
        node("XuiImage", "gameImage", 42, 42, Show=False),
    ]
    # The offline card (Edit Gamer Profile's): Zone in Gamerscore's place.
    off = find(root, "offlineBaseScene")
    zone_row(off, "titlesPlayedLabel", ["credText", "GSIcon", "GSText"], "credText", 125.0, 286.0,
             ["achievementsText", "achievementsLabel"])
    local_paths(root, shared)
    guidens.point(r, root, SKIN_NAMES)
    return xur5to8.Converter(r).convert(root)


# --- the skin --------------------------------------------------------------------
SKIN_NAMES = set()


# The console's message boxes as 1888's: 17559's xam names a box's visual
# "XuiMessageBox%d" (buttons + 1), which SlopGuide changes to "B8_MessageBox%d"
# (plugin/Guide1888.inc UseBladeBoxes); these are 1888's own XuiMessageBox2-4
# under that name. SlopGuide hosts them on 1888's error blade (HostBox).
BOX_VISUALS = ("XuiMessageBox2", "XuiMessageBox3", "XuiMessageBox4")


def skin(scenes, shared):
    r, root = read(os.path.join(scenes, "skin#skin.xur"))
    SKIN_NAMES.update(guidens.rename_skin(r, root))
    for v in list(root["children"]):
        name = v["props"].get("Id", "")
        if v["class"] == "XuiVisual" and name == guidens.PREFIX + "scr_Notification":
            # The toast's text, centred top to bottom in its border. 1888's
            # toasts were always two lines ("Gamertag / is online") filling
            # this 60-high box (no TextStyle: top-aligned); 17559's are one
            # line, which sat against the top edge.
            for c in v["children"]:
                if c["props"].get("Id") == "TextPresenter":
                    c["props"]["TextStyle"] = 0x1000           # XUI_FONT_STYLE_VERTICAL_CENTER
                    # Centred on the pill (61 high), not 3 above it: 1888's box
                    # is at y -3, so centred text sat high, against the top
                    # border.
                    c["props"]["Position"] = ("V", c["props"]["Position"][1], 0.5, 0.0)
        if v["class"] == "XuiVisual" and name[len(guidens.PREFIX):] in BOX_VISUALS:
            box = copy.deepcopy(v)
            box["props"]["Id"] = guidens.PREFIX + name[len(guidens.PREFIX) + 3:]      # B8_MessageBox3
            root["children"].append(box)
            SKIN_NAMES.add(box["props"]["Id"])
    recorder_visuals(root)
    local_paths(root, shared)
    return xur5to8.Converter(r).convert(root)


# The recorder panel's tiles fill the panel's width, which the music panel's
# fixed-size tile can't: the Select Music
# bar (btn_single, which stretches) with an icon in it. Its label: 1888's plain
# label at Select Music's size (20 points).
# Five tiles since Screenshot (2026-10-22): (406 - 2 x 7 - 4 x 4) / 5 wide.
REC_TILE_W, REC_TILE_H, REC_ICON = 75.2, 38.0, 38.0
REC_LABEL_PT = 20.0
REC_FADE = True         # (the icon dims with the tile, as the bar's text does)


def recorder_visuals(root):
    vis = {v["props"].get("Id"): v for v in root["children"] if v["class"] == "XuiVisual"}
    pre = guidens.PREFIX
    tile = copy.deepcopy(vis[pre + "btn_single"])
    tile["props"].update(Id=pre + "recTile", Width=REC_TILE_W, Height=REC_TILE_H)
    dw, dh = REC_TILE_W - 420.0, REC_TILE_H - 42.0
    for c in tile["children"]:
        p = c["props"]
        a = p.get("Anchor", 0)                  # 1 left, 2 top, 4 right, 8 bottom
        if "Width" in p and a & 5 == 5:
            p["Width"] += dw
        if "Height" in p and a & 10 == 10:
            p["Height"] += dh
        elif a & 8 and "Position" in p:
            at = p["Position"]
            p["Position"] = ("V", at[1], at[2] + dh, at[3])
    # The icon, from the music tile (its presenter), centred; dimmed as the
    # bar's text is when the tile's disabled (Stop, not recording).
    music = vis[pre + "xuiButtonImageCenteredMusic"]
    icon = copy.deepcopy(next(c for c in music["children"] if c["props"].get("Id") == "XuiImagePresenter1"))
    icon["props"].update(Width=REC_ICON, Height=REC_ICON, Position=("V", (REC_TILE_W - REC_ICON) / 2.0,
                                                                     (REC_TILE_H - REC_ICON) / 2.0, 0.0))
    icon["props"].pop("Pivot", None)
    tile["children"].append(icon)
    text = next(t for t in tile["raw_timelines"]["timelines"] if t["target"] == "XuiTextPresenter1")
    if not REC_FADE:
        text = None
    fade = copy.deepcopy(text) if text else None
    if fade:
        fade["target"] = "XuiImagePresenter1"
        fade["paths"] = fade["paths"][:1]           # (opacity)
        fade["keys"] = [(t, i, e, [1.0 if v[0] >= 0.5 else 0.25]) for t, i, e, v in text["keys"]]
        tile["raw_timelines"]["timelines"].append(fade)
    label = copy.deepcopy(vis[pre + "XuiLabel"])
    label["props"]["Id"] = pre + "recLabel"
    for c in label["children"]:
        if c["class"] == "XuiTextPresenter":
            c["props"]["PointSize"] = REC_LABEL_PT
    root["children"] += [tile, label]
    SKIN_NAMES.update(("recTile", "recLabel"))


def package_files(path):
    d = open(path, "rb").read()
    return [(n, d[o:o + s]) for n, o, s in xzp.entries(d)]


ZONE_ICONS = ("ico_32x_Zone_RR.png", "ico_32x_Zone_Pro.png", "ico_32x_Zone_Family.png",
              "ico_32x_Zone_Underground.png")


def build(scenes, modules, art=None):
    """The package's bytes. art: the recorder icons' folder (plugin/art when
    not given)."""
    files = []
    shared = set()
    # The gamer zones' icons: 17559's code asks for them by these names
    # (gamerprofile 90103118..90103200: sharedres://ico_32x_Zone_*.png) and got
    # 17559's art; 1888's own (its xam shrdres.bin), which SlopGuide answers
    # with (the zone list's pictures).
    shared.update(ZONE_ICONS)
    skin_data = skin(scenes, shared)              # first: the screens point at its names
    files.append(("skin.xur", skin_data))
    files.append(("hudbkgnd.xur", frame(scenes, shared)))
    files.append(("notify.xur", notify(scenes, shared)))
    files.append(("GamerCard.xur", gamercard(scenes, shared)))
    files.append(("SI_signin1.xur", signin_page(scenes, shared)))
    files.append(("SI_UserList.xur", signin_part(scenes, "UserList.xur", shared)))
    files.append(("SI_status.xur", signin_part(scenes, "status.xur", shared)))
    for name in CREATEPROFILE:
        files.append(("CP_" + name, createprofile(scenes, name, shared)))
    for name in DEVICESELECTOR:
        files.append(("DS_" + name, deviceselector(scenes, name, shared)))
    for name in GAMERPROFILE:
        files.append(("GP_" + name, gamerprofile(scenes, name, shared)))
    for name in ACHIEVEMENTS:
        files.append(("GP_" + name, achievements(scenes, name, shared)))
    files.append(("VK_KeyboardMain.xur", vk_main(scenes, shared)))
    files.append(("VK_KeyboardBase.xur", vk_base(scenes, shared)))
    # the keyboard's own pictures (LB, RT, Caps, Done...: 1888 vk.xex's)
    for n, data in package_files(os.path.join(modules, "vk", "vk.bin")):
        if n.lower().endswith(".png") and "\\" not in n:
            files.append((n, data))
    # deviceselector's own icons (1888's: the hard drive and the memory unit)
    for n, data in package_files(os.path.join(modules, "deviceselector", "media.bin")):
        if n.lower().endswith(".png") and "\\" not in n:
            files.append((n, data))
    for name in sorted(os.listdir(scenes)):
        if name.startswith("hud#") and name.endswith(".xur"):
            files.append((name[4:], screen(scenes, name[4:], shared)))
    files.append(("Recorder.xur", recorder_screen(scenes, shared)))
    files.append(("Streaming.xur", streaming_screen(scenes, shared)))
    files.append(("Plugins.xur", plugins_screen(scenes, shared)))
    files.append(("Trainer.xur", trainer_screen(scenes, shared)))
    # The recorder panel's icons (tools/recorder_icons.py, kept in plugin/art).
    art = art or os.path.join(HERE, "..", "plugin", "art")
    for name in ("recRecord.png", "recStop.png", "recShot.png", "recStream.png", "recSettings.png"):
        files.append((name, open(os.path.join(art, name), "rb").read()))
    have = {n.lower() for n, _ in files}
    # Their art: hud's and the skin's own files, then xam's (sounds, images).
    for pkg in (os.path.join(modules, "hud", "hud.bin"), os.path.join(modules, "huduiskin", "skin.bin"),
                os.path.join(modules, "xam", "xam.bin"), os.path.join(modules, "xam", "gamercrd.bin")):
        for n, data in package_files(pkg):
            low = n.lower()
            if low.endswith((".xur", ".xus")) or "\\" in n or low in have:
                continue
            files.append((n, data))
            have.add(low)
    shrd = {n.lower(): (n, data) for n, data in package_files(os.path.join(modules, "xam", "shrdres.bin"))}
    # 1888's gamer pictures (64_<picture key>.png): SlopGuide shows the
    # profile's own on the gamer card, by the key it reads from the profile.
    shared.update(n for n, _ in shrd.values() if n.lower().startswith("64_fffe07d1"))
    for name in sorted(shared):
        if name.lower() in have:
            continue
        if name.lower() not in shrd:
            print("  missing shared file %s" % name)
            continue
        files.append(shrd[name.lower()])
        have.add(name.lower())
    for n, data in files:
        print("  %-34s %7d" % (n, len(data)))
    return xzp.build(files, 3)


if __name__ == "__main__":
    pkg = build(sys.argv[1], sys.argv[2])
    open(sys.argv[3], "wb").write(pkg)
    print("%s: %d bytes, %d visuals renamed %s*" % (sys.argv[3], len(pkg), len(SKIN_NAMES), guidens.PREFIX))
