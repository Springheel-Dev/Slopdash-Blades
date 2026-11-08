"""Builds "SlopDash Importer.exe": one portable file (PyInstaller --onefile),
holding the importer, SlopDash's own tools (tools/), the scene upgrader
(build/upgrade_scenes.exe, built by build.sh), the recorder icons
(plugin/art) and the SlopDash release it installs (SlopDash.xex,
SlopGuide.xex). No Microsoft files: the user brings those.

    importer/.venv/Scripts/python importer/build_exe.py [release folder]

The release folder defaults to ImporterAssets/release.
"""
import os
import sys

import PyInstaller.__main__

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TOOLS = os.path.join(REPO, "tools")
RELEASE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "ImporterAssets", "release")
SEP = os.pathsep

sys.path.insert(0, HERE)
import sources  # noqa: E402   (the one list of what goes in)

HIDDEN = sources.TOOL_MODULES
TOOL_DATA = sources.TOOL_DATA

args = [os.path.join(HERE, "run_importer.py"), "--onefile", "--noconsole", "--clean",
        "--name", "SlopDash Importer",
        "--distpath", os.path.join(HERE, "dist"), "--workpath", os.path.join(HERE, "build", "pyinstaller"),
        "--specpath", os.path.join(HERE, "build"),
        "--paths", HERE, "--paths", TOOLS,
        "--add-binary", os.path.join(HERE, "build", "upgrade_scenes.exe") + SEP + ".",
        "--add-data", os.path.join(HERE, "slopdash_importer", "manifest.json") + SEP + "slopdash_importer"]
for name in TOOL_DATA:
    args += ["--add-data", os.path.join(TOOLS, name) + SEP + "."]
for path in sources.ART:
    args += ["--add-data", os.path.join(REPO, *path.split("/")) + SEP + "art"]
for name in ("SlopDash.xex", "SlopGuide.xex"):
    args += ["--add-data", os.path.join(RELEASE, name) + SEP + "release"]
for m in HIDDEN:
    args += ["--hidden-import", m]
PyInstaller.__main__.run(args)
