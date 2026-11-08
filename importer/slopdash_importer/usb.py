"""The USB stick: the removable drives Windows has, the copy (every file read
back and checked), and what to put in DashLaunch's launch.ini, which the
importer reads but never writes."""
import ctypes
import hashlib
import os
import re
import shutil
import string

DEFAULT_LINE = r"Default = Usb:\Apps\SlopDash\SlopDash.xex"
GUIDE_PATH = r"Usb:\Apps\SlopDash\SlopGuide.xex"


class Drive:
    def __init__(self, root, label, fs, free, total):
        self.root, self.label, self.fs, self.free, self.total = root, label, fs, free, total

    def __str__(self):
        return "%s  %s  (%s, %.1f GB free of %.1f GB)" % (self.root[:2], self.label or "No label", self.fs,
                                                          self.free / 1e9, self.total / 1e9)


def removable_drives():
    """The removable drives (USB sticks): never fixed disks."""
    if os.name != "nt":
        return []
    k = ctypes.windll.kernel32
    drives = []
    mask = k.GetLogicalDrives()
    for i, letter in enumerate(string.ascii_uppercase):
        if not mask & (1 << i):
            continue
        root = letter + ":\\"
        if k.GetDriveTypeW(root) != 2:              # DRIVE_REMOVABLE
            continue
        label = ctypes.create_unicode_buffer(261)
        fs = ctypes.create_unicode_buffer(261)
        if not k.GetVolumeInformationW(root, label, 261, None, None, None, fs, 261):
            continue                                # (no medium)
        free, total = ctypes.c_ulonglong(), ctypes.c_ulonglong()
        k.GetDiskFreeSpaceExW(root, ctypes.byref(free), ctypes.byref(total), None)
        drives.append(Drive(root, label.value, fs.value, free.value, total.value))
    return drives


def problems(drive, needed):
    """Why the stick won't do ([] if it will): the console reads FAT32 sticks."""
    out = []
    if drive.fs.upper() != "FAT32":
        out.append("It's %s: the Xbox 360 needs FAT32." % drive.fs)
    if drive.free < needed:
        out.append("It has %.0f MB free: %.0f MB is needed." % (drive.free / 1e6, needed / 1e6))
    return out


def tree_size(folder):
    return sum(os.path.getsize(os.path.join(d, n)) for d, _, ns in os.walk(folder) for n in ns)


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def copy_tree(stage, target, log=print, progress=None):
    """Everything under stage copied to target (the stick's root), each file
    read back and compared. How many files. Raises OSError on a mismatch."""
    files = [(d, n) for d, _, ns in os.walk(stage) for n in ns]
    for i, (d, n) in enumerate(files):
        src = os.path.join(d, n)
        rel = os.path.relpath(src, stage)
        dst = os.path.join(target, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        if sha1(src) != sha1(dst):
            raise OSError("%s didn't copy correctly" % rel)
        if progress:
            progress(i + 1, len(files), rel)
        log("    " + rel)
    return len(files)


# --- what kind of softmod stick it is ------------------------------------------------
# BadUpdate (Grimdoomer) with the ABadAvatar save or a game trigger: the stick
# holds BadUpdatePayload\default.xex, the payload, XeUnshackle (which loads
# DashLaunch: its launch.ini goes at the stick's root) or FreeMyXe (no
# DashLaunch). Peer Pressure (Grimdoomer, persistent): its installer reads
# BadUpdatePayload\SoftmodExtras\SoftmodExtras.ini; it doesn't run DashLaunch
# or its plugins, and boots without the stick. (consolemods.org Bad Update,
# github.com/Byrom90/XeUnshackle, github.com/grimdoomer/Xbox360PeerPressure.)
BADUPDATE, PEER_PRESSURE, UNKNOWN = "badupdate", "peerpressure", "unknown"


def stick_kind(root):
    payload = os.path.join(root, "BadUpdatePayload")
    if os.path.exists(os.path.join(payload, "SoftmodExtras", "SoftmodExtras.ini")):
        return PEER_PRESSURE
    if os.path.exists(os.path.join(payload, "default.xex")):
        return BADUPDATE
    return UNKNOWN


def stick_note(root):
    """A line about the stick for the USB step (None if there's nothing to say)."""
    kind = stick_kind(root)
    if kind == PEER_PRESSURE:
        return ("This is a Peer Pressure stick. Peer Pressure doesn't run DashLaunch plugins, so SlopDash will run "
                "as your dashboard but SlopGuide won't: no 1888 Guide, Trainer panel or screenshots.")
    if kind == UNKNOWN:
        return ("This stick has no BadUpdatePayload folder. SlopDash will still be copied, but your console starts "
                "it from a BadUpdate or ABadAvatar stick (with XeUnshackle).")
    return None


def read_launch_ini(root):
    path = os.path.join(root, "launch.ini")
    if not os.path.exists(path):
        return None
    return open(path, "rb").read().decode("latin-1")


def launch_ini_advice(root):
    """What to change in launch.ini on the stick, in words, for the user to do."""
    if stick_kind(root) == PEER_PRESSURE:
        return ("Peer Pressure doesn't use launch.ini. In its Softmod Settings app, on the Dashboard tab, set "
                "the homebrew dashboard to:\n\n  Usb:\\Apps\\SlopDash\\SlopDash.xex\n\n"
                "Keep this stick plugged in: SlopDash and its themes are on it.")
    text = read_launch_ini(root)
    if text is None:
        return ("There's no launch.ini here yet. XeUnshackle comes with one: copy it to the stick's root, "
                "then set these two lines in it (or make a new text file called launch.ini with just these):\n\n"
                "  [Paths]\n  %s\n\n  [Plugins]\n  plugin1 = %s\n" % (DEFAULT_LINE, GUIDE_PATH))
    lines = []
    # (each match on its own line: an empty "Default =" mustn't take the next one)
    m = re.search(r"(?im)^[ \t]*Default[ \t]*=[ \t]*([^\r\n]*)", text)
    now = m.group(1).strip() if m else ""
    if now.lower() == r"usb:\apps\slopdash\slopdash.xex":
        lines.append("Default already starts SlopDash.")
    elif now:
        lines.append("In [Paths], Default starts %s now. Change that line to:\n  %s" % (now, DEFAULT_LINE))
    elif m:
        lines.append("In [Paths], Default is empty. Change it to:\n  %s" % DEFAULT_LINE)
    else:
        lines.append("In [Paths], add:\n  %s" % DEFAULT_LINE)
    plugins = {int(n): v.strip() for n, v in re.findall(r"(?im)^[ \t]*plugin(\d+)[ \t]*=[ \t]*([^\r\n]*)", text)}
    older = [n for n, v in sorted(plugins.items())
             if re.search(r"(?i)(SlopGuide|RetroGuide)\.xex$", v) and v.lower() != GUIDE_PATH.lower()]
    if any(v.lower() == GUIDE_PATH.lower() for v in plugins.values()):
        lines.append("SlopGuide is already a plugin.")
    elif older:
        lines.append("In [Plugins], plugin%d is an older copy of SlopGuide (%s). Change that line to:\n"
                     "  plugin%d = %s" % (older[0], plugins[older[0]], older[0], GUIDE_PATH))
    else:
        used = [n for n, v in plugins.items() if v]
        free = [n for n in range(1, 6) if n not in used]
        if free:
            lines.append("In [Plugins], add:\n  plugin%d = %s" % (free[0], GUIDE_PATH))
        else:
            lines.append("In [Plugins], all five plugin lines are used: replace one with:\n  plugin5 = %s"
                         % GUIDE_PATH)
    return "\n\n".join(lines)
