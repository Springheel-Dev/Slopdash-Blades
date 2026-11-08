#!/usr/bin/env bash
# Builds SlopGuide.xex (the DashLaunch plugin for the system Guide's skin)
# with the XDK's own compiler, as ../build.sh does for SlopDash.
set -euo pipefail
cd "$(dirname "$0")"

XEDK="${XEDK:-$(cd "$(dirname "$0")/../.." && pwd)/xdk}"
[ -f "$XEDK/bin/win32/cl.exe" ] || { echo "XDK not found at XEDK=$XEDK" >&2; exit 1; }
BIN="$XEDK/bin/win32"
w() { cygpath -w "$1"; }
INC="$(w "$XEDK/include/xbox")"
CRT="$(w "$XEDK/TechPreview/Jul12Compiler/include/xbox")"
mkdir -p build
# ../local/ (never committed): the maintainer's own additions, built in when there.
LOCALDEF=()
[ -d ../local ] && [ -z "${SLOPDASH_PUBLIC:-}" ] && LOCALDEF=(-D SLOPDASH_LOCAL)

INCLUDE="$INC;$CRT" "$BIN/cl.exe" -nologo -c -W4 -WX -Ox -MT -GR- -EHsc -TP \
    -D _XBOX -D NDEBUG "${LOCALDEF[@]}" -I ../src \
    -FI"$INC\xbox_intellisense_platform.h" -Fobuild/SlopGuide.obj SlopGuide.cpp

# A DLL with the CRT's DLL entry point (which calls DllMain).
LIB="$(w "$XEDK/lib/xbox")" "$BIN/link.exe" -nologo -RELEASE -OPT:REF -DLL \
    -ENTRY:_DllMainCRTStartup -XEX:NO -ALIGN:128,65536 -MAP:build/SlopGuide.map \
    -OUT:build/SlopGuide.exe build/SlopGuide.obj xboxkrnl.lib xapilib.lib xav.lib

"$BIN/imagexex.exe" -nologo -config:SlopGuide.xex.xml -out:build/SlopGuide.xex build/SlopGuide.exe
ls -l build/SlopGuide.xex

# SlopGuide loads at 0x81E80000 and SlopDash at 0x82000000: an image of
# 0x180000 or more overlaps it and SlopDash can't start ("the game cannot be
# loaded": 0.90-0.92's recorder buffers, 2026-10-18). Refused here.
python - build/SlopGuide.exe <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
pe = struct.unpack("<I", d[0x3C:0x40])[0]
size = struct.unpack("<I", d[pe + 0x50:pe + 0x54])[0]
limit = 0x82000000 - 0x81E80000
print("SlopGuide image: 0x%X bytes (limit 0x%X)" % (size, limit))
if size >= limit:
    sys.exit("SlopGuide's image would overlap SlopDash's load address: not built")
EOF
