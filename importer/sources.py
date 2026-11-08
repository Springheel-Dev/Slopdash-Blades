"""Everything the SlopDash Importer is made from, in one place: build_exe.py
builds the exe from these. Paths are relative to the SlopDash repository's root.

None of it is Microsoft's: the user brings the dashboard files, and the
importer recognises them by the hashes in slopdash_importer/manifest.json.
"""

# The importer itself.
IMPORTER = ["importer/" + p for p in (
    "README.md", "REVIEW.md", "requirements.txt", "build.sh", "release.txt", "build_exe.py",
    "run_importer.py", "sources.py",
    "slopdash_importer/__init__.py", "slopdash_importer/__main__.py", "slopdash_importer/cli.py",
    "slopdash_importer/gui.py", "slopdash_importer/manifest.json", "slopdash_importer/pipeline.py",
    "slopdash_importer/scan.py", "slopdash_importer/stfspkg.py", "slopdash_importer/usb.py",
    "slopdash_importer/xexdecrypt.py",
    "native/scenes.cpp", "native/scenes.h", "native/upgrade_scenes.cpp")]

# SlopDash's tools the pipeline imports inside its steps (PyInstaller can't
# see those), each a module in tools/.
TOOL_MODULES = ("xexres", "xzp", "xur", "xur5to8", "xur5w", "xur8", "xurlearn", "guidens", "guideskin",
                "guide1888", "dashmain5", "marketplace5", "themepak")

# What the tools read beside themselves: lists of the dashboards' class and
# property names and part names (no code, art or text of Microsoft's).
TOOL_DATA = ("xui_schema.txt", "xui_schema_17559.txt", "blades1888_classes.json", "blades_classes.json",
             "gp17559_parts.json")

# The scene upgrader (build.sh, g++): SlopDash's own core.
CORE = ("src/core/xex.cpp", "src/core/xex.h", "src/core/xur.cpp", "src/core/xur.h", "src/core/xur_schema.inc",
        "src/core/xzp.cpp", "src/core/xzp.h", "src/theme/blades1888_classes.inc")

# SlopDash's own pictures (the Guide's screen recorder), packed in.
ART = ["plugin/art/" + n for n in ("recRecord.png", "recStop.png", "recShot.png", "recStream.png",
                                   "recSettings.png")]
