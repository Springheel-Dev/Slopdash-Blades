"""The SlopDash Importer's build: the user's files (scan.Found) and the SlopDash
release in, the USB stick's \\Apps\\SlopDash folder out, in a workspace:

  1. SlopDash.xex, SlopGuide.xex (the release)
  2. the scenes upgraded (upgrade_scenes.exe, SlopDash's own core/xur)
  3. the Marketplace blade's content (tools/marketplace5.py, from 6717's)
  4. the five-blade tab strip, dashmain5.xzp (tools/dashmain5.py)
  5. the Blades theme pack, Themes\\blades1888.pak (tools/themepak.py): the 1888
     modules' and 6717's packages, the tab strip and the font, in one file
  6. the 1888 Guide, Guide\\blades1888.xzp (tools/guide1888.py)
  7. every file's SHA-1 listed (stage.json), for the copy to check against

The tools are SlopDash's own (tools/), run in this process; their output is the
same as building by hand (the importer's test: ImporterAssets).
"""
import contextlib
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile

FROZEN = getattr(sys, "frozen", False)
BUNDLE = getattr(sys, "_MEIPASS", None)
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
if not FROZEN:
    sys.path.insert(0, os.path.join(REPO, "tools"))

import xexres  # noqa: E402
import xzp  # noqa: E402


def bundled(*parts):
    return os.path.join(BUNDLE, *parts) if FROZEN else None


def helper_path():
    return bundled("upgrade_scenes.exe") or os.path.join(REPO, "importer", "build", "upgrade_scenes.exe")


def art_dir():
    return bundled("art") or os.path.join(REPO, "plugin", "art")


def release_dir():
    """The SlopDash release the importer carries (its own copy)."""
    return bundled("release") or os.path.join(REPO, "ImporterAssets", "release")


# The modules the Guide is built from, and the packages guide1888 reads from each.
GUIDE_MODULES = ("hud", "huduiskin", "xam", "vk", "deviceselector")
SCENE_MODULES = ("createprofile", "xam", "gamerprofile", "hud", "deviceselector", "signin", "huduiskin", "vk")


class BuildError(Exception):
    pass


WORK_MARK = ".slopdash-importer-work"
# The importer's own work folder (the window's, and the command line's default).
DEFAULT_WORK = os.path.join(tempfile.gettempdir(), "SlopDashImporter")


def ours(work):
    """The importer's own folder: marked, or its fixed default (which builds
    from before the mark left unmarked: refusing it locked those users out,
    2026-11-05)."""
    same = os.path.normcase(os.path.abspath(work)) == os.path.normcase(os.path.abspath(DEFAULT_WORK))
    return same or os.path.isfile(os.path.join(work, WORK_MARK))


def clear_workspace(work):
    """Empties the work folder for a new build, making it if it isn't there.
    Only the importer's own folder (ours) or an empty one is emptied: the
    command line's --work can name any folder, and deleting one the importer
    didn't make would delete someone's files. Raises BuildError for anything
    else."""
    if os.path.exists(work) and not os.path.isdir(work):
        raise BuildError("%s is a file, not a folder" % work)
    if os.path.isdir(work) and os.listdir(work):
        if not ours(work):
            raise BuildError("%s isn't a SlopDash Importer work folder (it holds files and no %s): "
                             "choose an empty or new folder" % (work, WORK_MARK))
        shutil.rmtree(work)
    os.makedirs(work, exist_ok=True)
    open(os.path.join(work, WORK_MARK), "w").close()


def remove_workspace(work):
    """The work folder deleted at the end (the window's Finish), only if the
    importer made it."""
    if os.path.isdir(work) and ours(work):
        shutil.rmtree(work, ignore_errors=True)


class Build:
    def __init__(self, found, workspace, log=print, step=None, release=None):
        self.found = found
        self.work = workspace
        self.log = log
        self.step_cb = step or (lambda i, n, text: None)
        self.release = release or release_dir()
        self.stage = os.path.join(workspace, "stage")
        self.root = os.path.join(self.stage, "Apps", "SlopDash")
        self.blades = os.path.join(self.root, "Themes", "blades1888.pak")

    # --- small helpers -----------------------------------------------------------
    def data(self, set_, name):
        f = self.found.get("%s/%s" % (set_, name))
        if not f:
            raise BuildError("missing %s %s" % (set_, name))
        return f.read()

    def has(self, set_, name):
        return "%s/%s" % (set_, name) in self.found

    def write(self, path, data):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)

    def quiet(self, fn, *a, **k):
        """A tool run with its printing kept for the log."""
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            r = fn(*a, **k)
        for line in out.getvalue().splitlines():
            if line.strip():
                self.log("    " + line)
        return r

    def packages(self, xex_bytes):
        """{package name: bytes} of an .xex's resources."""
        image, load, res = self.quiet(xexres.xex_image, xex_bytes)
        return {name: image[off:off + size] for name, off, size in res}

    # --- the steps ---------------------------------------------------------------
    def run(self):
        steps = [("Getting SlopDash ready", self.release_files),
                 ("Upgrading the 2005 scenes", self.upgrade),
                 ("Building the Marketplace blade", self.marketplace),
                 ("Building the five-blade tab strip", self.tab_strip),
                 ("Packing the Blades theme", self.pack_blades),
                 ("Building the 1888 Guide", self.guide),
                 ("Listing what was built", self.listing)]
        clear_workspace(self.work)
        os.makedirs(self.root)
        for i, (text, fn) in enumerate(steps):
            self.step_cb(i, len(steps), text)
            self.log("%d/%d %s" % (i + 1, len(steps), text))
            fn()
        self.step_cb(len(steps), len(steps), "Done")
        return self.stage

    def release_files(self):
        for name in ("SlopDash.xex", "SlopGuide.xex"):
            src = os.path.join(self.release, name)
            if not os.path.exists(src):
                raise BuildError("the SlopDash release has no %s (%s)" % (name, self.release))
            shutil.copyfile(src, os.path.join(self.root, name))
            self.log("    %s" % name)

    def upgrade(self):
        mods = os.path.join(self.work, "modules")
        scenes = os.path.join(self.work, "scenes")
        dash_scenes = os.path.join(self.work, "scenes_dash")
        for d in (mods, scenes, dash_scenes):
            os.makedirs(d, exist_ok=True)
        inputs = []
        for m in SCENE_MODULES:
            path = os.path.join(mods, m + ".xex")
            self.write(path, self.data("1888", m + ".xex"))
            inputs.append(path)
        dash = os.path.join(mods, "dash.xex")
        self.write(dash, self.data("1888", "dash.xex"))
        for out, files in ((scenes, inputs), (dash_scenes, [dash])):
            r = subprocess.run([helper_path(), out] + files, capture_output=True, text=True,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            for line in r.stdout.splitlines():
                self.log("    " + line)
            if r.returncode:
                raise BuildError("the scenes couldn't be upgraded:\n" + r.stdout + r.stderr)
        # The Guide's modules' packages, as guide1888 reads them: <module>\<package>.bin.
        for m in GUIDE_MODULES:
            for name, blob in self.packages(self.data("1888", m + ".xex")).items():
                self.write(os.path.join(mods, m, name + ".bin"), blob)

    def marketplace(self):
        import marketplace5
        blademp = self.packages(self.data("6717", "dash.xex")).get("blademp")
        if blademp is None:
            raise BuildError("6717's dash.xex has no Marketplace blade (blademp)")
        ents = {n.lower(): blademp[o:o + s] for n, o, s in xzp.entries(blademp)}
        src = ents.get("marketplacesignedout.xur")
        if src is None:
            raise BuildError("6717's Marketplace blade has no marketplaceSignedOut.xur")
        tab = os.path.join(self.work, "tabstrip")
        self.write(os.path.join(tab, "marketplaceSignedOut.xur"), src)
        self.quiet(marketplace5.main, os.path.join(tab, "marketplaceSignedOut.xur"),
                   os.path.join(tab, "marketplaceHub.xur"))

    def tab_strip(self):
        import dashmain5
        tab = os.path.join(self.work, "tabstrip")
        out = os.path.join(tab, "dashmain5.xzp")      # (marketplaceHub.xur beside it goes in too)
        self.quiet(dashmain5.main, os.path.join(self.work, "scenes_dash", "dashmain#dashmain.xur"), out)

    def pack_blades(self):
        import themepak
        modules = {}
        for m in themepak.MODULES_1888:
            if self.has("1888", m + ".xex"):
                modules[m] = self.data("1888", m + ".xex")
            else:
                self.log("    1888 %s.xex: not given (some screens won't open)" % m)
        font = self.data("1888", "xenonclatin.xtt") if self.has("1888", "xenonclatin.xtt") else None
        five = open(os.path.join(self.work, "tabstrip", "dashmain5.xzp"), "rb").read()
        data = self.quiet(themepak.blades, modules, self.data("6717", "dash.xex"), five, font)
        self.write(self.blades, data)
        self.log("    blades1888.pak: %d entries, %.1f MB" % (len(themepak.read(data)), len(data) / 1e6))

    def guide(self):
        import guide1888
        pkg = self.quiet(guide1888.build, os.path.join(self.work, "scenes"), os.path.join(self.work, "modules"),
                         art=art_dir())
        self.write(os.path.join(self.root, "Guide", "blades1888.xzp"), pkg)

    def listing(self):
        files = {}
        for folder, _, names in os.walk(self.stage):
            for n in names:
                p = os.path.join(folder, n)
                rel = os.path.relpath(p, self.stage).replace(os.sep, "\\")
                if rel == "stage.json":
                    continue
                h = hashlib.sha1(open(p, "rb").read()).hexdigest()
                files[rel] = {"size": os.path.getsize(p), "sha1": h}
        json.dump(files, open(os.path.join(self.work, "stage.json"), "w"), indent=1)
        self.log("    %d files, %.1f MB" % (len(files), sum(f["size"] for f in files.values()) / 1e6))
