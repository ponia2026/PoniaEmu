#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# Download the Libretro cores that are bundled with the APK into the Android project's libs/arm64-v8a/. The file names must be lib<core>_libretro.so
# (native libraries inside an APK must be named lib*.so; the frontend and the player both look for the lib-prefixed name and, when found, do not ask to download).
#
# Bundling rules: (1) the license allows commercial use (noncommercial=false in ponia_cores.h), (2) small size. About 10 MB compressed in total; the APK stays below 40 MB.
# Not bundled, downloaded on first launch: cores with a non-commercial license (snes9x, genesis_plus_gx, fbneo, mame2003_plus) and very large ones (flycast 17 MB, mamearcade 257 MB).
#
# Usage: fetch-bundled-cores.sh [output dir]   default: src/app/platform/android/libs/arm64-v8a
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/../../src/app/platform/android/libs/arm64-v8a}
ABI=${ABI:-arm64-v8a}   # use ABI=armeabi-v7a for 32-bit (the output dir must then be given separately)
BASE=https://buildbot.libretro.com/nightly/android/latest/${ABI}
mkdir -p "$OUT"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# Which cores does this ABI use? Ask the same tables the program uses (src/libretro-host/ponia_cores.h) through list_cores, so a core
# that no system needs on this ABI (for example Dolphin on 32-bit ARM) is not packed. The candidates below are only what MAY be packed.
case "$ABI" in
    arm64-v8a) PONIA_TARGET=android-v8a ;;
    armeabi-v7a) PONIA_TARGET=android-v7a ;;
    *) echo "unknown ABI $ABI" >&2; exit 1 ;;
esac
g++ -std=c++17 -I "$HERE/../../src/libretro-host" -o "$TMP/list_cores" "$HERE/../tools/list_cores.cpp"
USED=" $("$TMP/list_cores" "$PONIA_TARGET" cores | cut -d' ' -f1 | tr '\n' ' ') "
echo "cores used on $PONIA_TARGET:$USED"

# "download name:name inside the APK" - the Android build of mupen64plus_next is called mupen64plus_next_gles3
# retro8 is the PICO-8 fallback core: no system maps to it, so it is always packed.
for pair in fceumm gambatte mgba melonds mednafen_pce_fast pcsx_rearmed stella a5200 prosystem retro8 \
            yabasanshiro applewin dosbox_pure mupen64plus_next_gles3:mupen64plus_next; do
    dl=${pair%%:*}; id=${pair##*:}
    case "$USED" in
        *" $id "*) ;;
        *) if [ "$id" != retro8 ]; then echo "skip $id: not used on $PONIA_TARGET"; continue; fi ;;
    esac
    curl -fsSL -m 300 -o "$TMP/$dl.zip" "$BASE/${dl}_libretro_android.so.zip"
    unzip -oq "$TMP/$dl.zip" -d "$TMP"
    mv "$TMP/${dl}_libretro_android.so" "$OUT/lib${id}_libretro.so"
    echo "lib${id}_libretro.so  $(wc -c < "$OUT/lib${id}_libretro.so") bytes"
done

# Sys folder needed by the GameCube (Dolphin core) (GPL, about 3 MB compressed): put into the APK's assets/packs/; the player unpacks it into the system directory the first time GameCube is run
ASSETS=${2:-$HERE/../../src/app/platform/android/assets/packs}
mkdir -p "$ASSETS"
case "$USED" in
    *" dolphin "*)
        curl -fsSL -m 300 -o "$ASSETS/Dolphin.zip" https://buildbot.libretro.com/assets/system/Dolphin.zip
        echo "assets/packs/Dolphin.zip  $(wc -c < "$ASSETS/Dolphin.zip") bytes" ;;
    *) echo "skip assets/packs/Dolphin.zip: Dolphin is not used on $PONIA_TARGET" ;;
esac

# ---- fake08 (the main PICO-8 core; the buildbot has no Android build, so it must be built by yourself) ----
# Source: https://github.com/jtothebell/fake-08 (MIT). NDK r25:
#   cd platform/libretro/jni && ndk-build APP_ABI=arm64-v8a APP_PLATFORM=android-23
# Output libs/arm64-v8a/libfake08.so (about 1.7 MB) -> copy it to src/app/platform/android/libs/arm64-v8a/libfake08_libretro.so
# Why not retro8: retro8 does not support compressed (pxa) cartridges (Islander and others show a black screen).
