#!/usr/bin/env bash
# Builds SlopDash.xex with the Xbox 360 XDK's own compiler (no Visual Studio).
#
#   ./build.sh            (Git Bash on Windows)
#
# XEDK: the XDK folder; defaults to xdk beside the repository.
# Output: build/SlopDash.xex. On the console it is Usb0:\Apps\SlopDash\SlopDash.xex
# (launch.ini's Default).
set -euo pipefail
cd "$(dirname "$0")"

XEDK="${XEDK:-$(cd "$(dirname "$0")/.." && pwd)/xdk}"
[ -f "$XEDK/bin/win32/cl.exe" ] || { echo "XDK not found at XEDK=$XEDK" >&2; exit 1; }
BIN="$XEDK/bin/win32"
w() { cygpath -w "$1"; }
INC="$(w "$XEDK/include/xbox")"
# The C/C++ standard library headers live in the Tech Preview compiler folder.
CRT="$(w "$XEDK/TechPreview/Jul12Compiler/include/xbox")"

SOURCES=(src/platform/main.cpp src/platform/log.cpp src/platform/stand_ins.cpp src/platform/resources.cpp src/theme/blades.cpp src/theme/blades1888.cpp src/core/xex.cpp src/core/xzp.cpp src/core/pak.cpp src/core/xma.cpp src/core/sevenzip.cpp src/core/xur.cpp src/core/stfs.cpp src/core/xdbf.cpp src/platform/achievements.cpp src/core/png.cpp src/core/xpr.cpp src/core/god.cpp src/core/library.cpp src/platform/scanner.cpp src/platform/net.cpp src/platform/https.cpp src/platform/store.cpp src/platform/console.cpp src/platform/pictures.cpp src/platform/trainers.cpp src/platform/retroachievements.cpp src/core/trainer.cpp src/core/json.cpp src/platform/fetcher.cpp src/core/http.cpp src/core/catalog.cpp src/platform/launcher.cpp src/platform/profiles.cpp src/platform/xconfig.cpp src/platform/themes.cpp src/platform/settings.cpp src/platform/library_settings.cpp src/platform/blade_colours.cpp src/platform/music.cpp src/core/musiclib.cpp src/platform/videos.cpp src/platform/movie.cpp src/platform/storage.cpp src/platform/shotcache.cpp)
mkdir -p build/obj

# -W4 -WX: all warnings as errors. -GR- -EHsc: no RTTI, C++ exceptions only.
# -MT: static CRT. -FI: the XDK platform header (the IDE normally injects it).
# local/ (never committed): the maintainer's own additions, built in when there.
LOCALDEF=()
[ -d local ] && [ -z "${SLOPDASH_PUBLIC:-}" ] && LOCALDEF=(-D SLOPDASH_LOCAL)
objs=()
for src in "${SOURCES[@]}"; do
    obj="build/obj/$(basename "${src%.*}").obj"
    INCLUDE="$INC;$CRT" "$BIN/cl.exe" -nologo -c -W4 -WX -Ox -MT -GR- -EHsc -TP \
        -D _XBOX -D NDEBUG "${LOCALDEF[@]}" -I src -I third_party/bearssl/inc -I third_party/lzma -D __BIG_ENDIAN__ \
        -FI"$INC\\xbox_intellisense_platform.h" -Fo"$obj" "$src"
    objs+=("$obj")
done

# The LZMA SDK's decoder (third_party/lzma, Igor Pavlov, public domain, 24.09):
# the store's .7z downloads. Built once into build/lzma.lib. Big-endian told
# outright (it detects only GCC's markers); PPMd on.
if [ ! -f build/lzma.lib ]; then
    mkdir -p build/lzma
    lzobjs=()
    for src in third_party/lzma/*.c; do
        obj="build/lzma/$(basename "${src%.c}").obj"
        INCLUDE="$INC;$CRT" "$BIN/cl.exe" -nologo -c -W3 -Ox -MT -TC -D inline=__inline -D __BIG_ENDIAN__ \
            -D Z7_PPMD_SUPPORT -D _XBOX -D NDEBUG -I third_party/lzma \
            -FI"$INC\\xbox_intellisense_platform.h" -Fo"$obj" "$src" | grep -v "^[A-Za-z0-9_]*.c$" || true
        lzobjs+=("$obj")
    done
    "$BIN/lib.exe" -nologo -OUT:build/lzma.lib "${lzobjs[@]}"
fi

# BearSSL (third_party/bearssl, MIT): the store's HTTPS. Built once into
# build/bearssl.lib (delete it to rebuild). Its platform detection is turned
# off: the console has no OS random or clock source it knows (SlopDash seeds
# it and sets the time itself), and none of its x86 / POWER8 code applies.
if [ ! -f build/bearssl.lib ]; then
    mkdir -p build/bearssl
    BR_DEFS=(-D BR_USE_URANDOM=0 -D BR_USE_WIN32_RAND=0 -D BR_USE_UNIX_TIME=0 -D BR_USE_WIN32_TIME=0
             -D BR_RDRAND=0 -D BR_AES_X86NI=0 -D BR_SSE2=0 -D BR_POWER8=0 -D BR_INT128=0 -D BR_UMUL128=0
             -D BR_LE_UNALIGNED=0 -D BR_BE_UNALIGNED=0 -D BR_64=1)
    brobjs=()
    while IFS= read -r src; do
        rel="${src#third_party/bearssl/src/}"
        obj="build/bearssl/${rel//\//_}"
        obj="${obj%.c}.obj"
        INCLUDE="$INC;$CRT" "$BIN/cl.exe" -nologo -c -W1 -Ox -MT -TC -D inline=__inline -D _XBOX -D NDEBUG "${BR_DEFS[@]}" \
            -I third_party/bearssl/inc -I third_party/bearssl/src \
            -FI"$INC\\xbox_intellisense_platform.h" -Fo"$obj" "$src" | grep -v "^[a-z0-9_]*.c$" || true
        brobjs+=("$obj")
    done < <(find third_party/bearssl/src -name '*.c' | sort)
    "$BIN/lib.exe" -nologo -OUT:build/bearssl.lib "${brobjs[@]}"
fi

# -XEX:NO: a plain PE; imagexex turns it into the .xex below.
LIB="$(w "$XEDK/lib/xbox")" "$BIN/link.exe" -nologo -RELEASE -OPT:REF -XEX:NO \
    -MAP:build/SlopDash.map -OUT:build/SlopDash.exe "${objs[@]}" build/bearssl.lib build/lzma.lib \
    xapilib.lib xboxkrnl.lib d3d9.lib d3dx9.lib xgraphics.lib xinput2.lib xmcore.lib \
    xuirun.lib xuirender.lib xonline.lib xav.lib xnet.lib xaudio2.lib xmedia2.lib xmahal.lib x3daudio.lib libcpMT.lib

"$BIN/imagexex.exe" -nologo -config:SlopDash.xex.xml -out:build/SlopDash.xex build/SlopDash.exe
ls -l build/SlopDash.xex
