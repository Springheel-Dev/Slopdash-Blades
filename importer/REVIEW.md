# Reviewing the SlopDash Importer

This page is for anyone checking the importer before trusting it with their files and their console's USB stick. Each claim points to the code that makes it true.

## What it is made of

All of it is listed in one file, `importer/sources.py`, and the exe is built from that list (`build_exe.py`):

- **The importer**: `importer/slopdash_importer/` (the window, the command line, the file scan, the XEX decryption, the build steps and the USB copy).
- **SlopDash's tools**: the modules in `tools/` that read and convert the dashboard files, named in `sources.TOOL_MODULES`.
- **The scene upgrader**: `importer/native/` with SlopDash's C++ core (`src/core/xex`, `xur`, `xzp`), built with g++ into `upgrade_scenes.exe` (`build.sh`).
- **SlopDash's own pictures**: `plugin/art/*.png`, the Guide's screen recorder icons.
- **Two prebuilt files it installs**: `SlopDash.xex` and `SlopGuide.xex`, SlopDash's dashboard and Guide plugin, built from `src/` and `plugin/` with Microsoft's Xbox 360 SDK, which isn't redistributable. Treat them like any homebrew download.

It builds the Blades dashboard only, from two Xbox 360 dashboard versions' files: 2.0.1888 and 2.0.6717's `dash.xex`.

**Nothing of Microsoft's is included.** You bring the dashboard files. The importer recognises them by SHA-1 and size (`slopdash_importer/manifest.json`, hashes and descriptions only). The source does hold some facts about Microsoft's formats, stated here so nobody has to find them:

- **The XEX keys**: two AES keys, retail and XEX1, in `slopdash_importer/xexdecrypt.py` (the retail one also in `tools/xexres.py` and `src/core/xex.cpp`), used to decrypt the `.xex` files you bring. They are publicly documented, and other open-source tools such as the Xenia emulator and XexTool carry them.
- **Lists of the dashboards' scene class and property names and part names**: `tools/xui_schema*.txt`, `tools/*_classes.json`, `tools/gp17559_parts.json`, `src/core/xur_schema.inc`, `src/theme/blades1888_classes.inc`. These are names and types only, with no code, art or text.

## What it reads

- **The files and folders (or zips) you add.** It only reads them: `scan.py` opens them read-only and never writes, renames or deletes. Inside them it also looks into Xbox content packages, such as a system update's `su20076000_00000000`, with its own read-only reader (`stfspkg.py`). It only does that for files with no extension, no bigger than 64 MB, that start with a package signature.
- **How a file is recognised** (`scan.py`): first by size and SHA-1, as it comes off the console (the manifest's `original`) or as XexTool's `-e u` leaves it (`sha1`). Failing that, an `.xex` by what it holds: its program image, decrypted and decompressed, by size and SHA-1 (`image`). The same module comes as many different files (encrypted, decrypted, decompressed, each tool laying out its headers its own way), but they all hold the same image, so any of them is recognised, and nothing else is.
- **Encrypted `.xex` files are decrypted in memory** (`xexdecrypt.py`), as XexTool's `-e u` does: the file's own key unwrapped with the retail or XEX1 key, the body decrypted (AES-CBC). The key is confirmed by the first block's SHA-1 (LZX) or, for an uncompressed file, by its image matching the manifest. Your files on disk are never changed.
  As a tool on its own: `python -m slopdash_importer.xexdecrypt <in.xex> <out.xex>` (from `importer/`).
- **Your USB stick's `launch.ini`, to read it only.** `usb.read_launch_ini` reads it so the last screen can say what to change. The importer **never edits `launch.ini`**: it shows you the lines (`usb.launch_ini_advice`) and you make the change yourself.
- **The list of drives.** `usb.removable_drives` asks Windows for removable drives only (`GetDriveTypeW` = `DRIVE_REMOVABLE`), with their label, file system and free space. Fixed disks are never offered.

## What it writes

1. **A work folder.** The window uses `%TEMP%\SlopDashImporter`; the command line uses `--work`, which defaults to the same place. The importer marks a folder it makes with `.slopdash-importer-work` and **refuses to empty any folder that has files but not that mark** (`pipeline.clear_workspace`). A mistyped `--work` therefore can't delete your files. The folder holds your dashboard files decrypted. The window deletes it when you finish (`pipeline.remove_workspace`, marked folders only) unless you tick "Keep the build folder".
2. **Your USB stick, under `\Apps\SlopDash\` only** (`usb.copy_tree`). It adds files and overwrites files of the same name there. It deletes nothing, and it reads each file back and compares its SHA-1 after copying.

Nothing else is written: not the registry, not your files, not other drives.

## What it never does

- **No network.** Neither the importer nor any tool it bundles uses sockets, HTTP or any download library.
- **Runs no other program except its own scene upgrader** (`pipeline.Build.upgrade`, `subprocess.run([helper_path(), ...])`), built from `importer/native` and `src/core`.
- **Doesn't need the console**, and doesn't touch it.

## Building it yourself

From a clone, on Windows with [Git Bash](https://git-scm.com/downloads), MinGW g++ (for example [WinLibs](https://winlibs.com)) and [Python 3.13](https://www.python.org/downloads/) on the PATH:

```
git clone https://github.com/Springheel-Dev/Slopdash-Blades.git
cd Slopdash-Blades
./importer/build.sh
```

The exe lands in `importer/dist`. The release's own exe is built the same way by GitHub (`.github/workflows/importer-release.yml`), from the public source, when a release is published. It carries `SlopDash.xex` and `SlopGuide.xex`, which need Microsoft's Xbox 360 SDK to compile. `build.sh` takes them from:
1. a folder you give it: `./importer/build.sh <folder holding SlopDash.xex and SlopGuide.xex>`
2. a build from source, if it finds the SDK (`XEDK`, or `xdk` beside the repository)
3. otherwise, the release pinned in `importer/release.txt`, downloaded and checked against the SHA-256s there

`build.sh` installs the pinned versions in `requirements.txt` into `importer/.venv`, the versions the published exe was built with. PyInstaller doesn't make byte-identical files, so compare behaviour and the bundled files, not the exe's hash. To see what a built exe holds, unpack it with `pyi-archive_viewer` (part of PyInstaller).

To run it without the window, give the exe or the source the same arguments:

```
"SlopDash Importer.exe" <your files> --out <stick or folder> [--work <empty or new folder>]
python -m slopdash_importer.cli <your files> --out <folder>      (from importer/, from source)
```

From source that needs `cryptography`, plus `importer/build/upgrade_scenes.exe` for the scene step.
