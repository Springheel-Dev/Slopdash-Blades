#!/usr/bin/env bash
# Builds the SlopDash Importer: the scene upgrader (SlopDash's own core, g++,
# no XDK), then the portable exe (dist/SlopDash Importer.exe).
#
#   ./importer/build.sh [folder holding SlopDash.xex and SlopGuide.xex]
#
# Needs Git Bash, MinGW g++ and Python 3.13 on the PATH (Windows).
#
# The exe carries SlopDash.xex and SlopGuide.xex. They come from, in order:
#   1. the folder given
#   2. a build here, if the Xbox 360 SDK is found (XEDK, or xdk beside the repository)
#   3. the release pinned in release.txt, downloaded and checked against its SHA-256s
set -euo pipefail
RELEASE="${1:+$(cd "$1" && pwd)}"         # (the folder given, before the cd below)
cd "$(dirname "$0")"
mkdir -p build
CORE="native/scenes.cpp ../src/core/xex.cpp ../src/core/xur.cpp ../src/core/xzp.cpp"
g++ -std=c++11 -O2 -static -I../src native/upgrade_scenes.cpp $CORE -o build/upgrade_scenes.exe

# --- SlopDash.xex and SlopGuide.xex -----------------------------------------------
XDK="${XEDK:-$(cd ../.. && pwd)/xdk}"
if [ -n "$RELEASE" ]; then
    echo "SlopDash build: $RELEASE"
elif [ -d "$XDK/bin" ]; then
    echo "SlopDash build: from source, with the Xbox 360 SDK at $XDK"
    (cd .. && XEDK="$XDK" bash build.sh)
    (cd ../plugin && XEDK="$XDK" bash build.sh)
    RELEASE=build/release
    mkdir -p "$RELEASE"
    cp ../build/SlopDash.xex ../plugin/build/SlopGuide.xex "$RELEASE"/
else
    TAG=$(awk '$1 == "tag" { print $2 }' release.txt)
    REPO=$(awk '$1 == "repo" { print $2 }' release.txt)
    echo "SlopDash build: no Xbox 360 SDK, so the release $TAG's (release.txt)"
    RELEASE=build/release-$TAG
    mkdir -p "$RELEASE"
    for f in SlopDash.xex SlopGuide.xex; do
        [ -f "$RELEASE/$f" ] && continue
        curl -fsSL -o "$RELEASE/$f" "https://github.com/$REPO/releases/download/$TAG/$f" ||
            { command -v gh >/dev/null && gh release download "$TAG" --repo "$REPO" --pattern "$f" --dir "$RELEASE"; } ||
            { echo "couldn't download $f from $REPO's release $TAG"; exit 1; }
    done
    # Each file checked against the SHA-256 release.txt pins.
    awk '$1 == "sha256" { print $2 "  " $3 }' release.txt | (cd "$RELEASE" && sha256sum -c -) ||
        { echo "the downloaded files don't match release.txt: not used"; rm -rf "$RELEASE"; exit 1; }
fi

# The pinned versions (requirements.txt): the versions the published exe was built with.
[ -d .venv ] || { python -m venv .venv && .venv/Scripts/pip install -q -r requirements.txt; }
.venv/Scripts/python build_exe.py "$(cd "$RELEASE" && (pwd -W 2>/dev/null || pwd))"
ls -l "dist/SlopDash Importer.exe"
