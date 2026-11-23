#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# Test helper: download every core a Linux target uses into one folder, under the file names PoniaEmu asks a core mirror for,
# so test boxes on a slow connection fetch them from the local network instead (see core_mirror() in CoreInstaller.cpp).
#
#   sh etc/tools/core_mirror.sh <linux-arm64|linux-armhf|linux-x86_64> [folder]     (default folder: ~/poniaemu-mirror)
#   cd <folder> && python3 -m http.server 8090
#   on the test box:  echo http://<this machine>:8090 > ~/.config/poniaemu/core_mirror.txt    (delete the file to go back)
#
# Files that are already there are kept, so the script can be run again to add what is missing.
set -eu

TARGET=${1:?usage: core_mirror.sh <linux-arm64|linux-armhf|linux-x86_64> [folder]}
OUT=${2:-$HOME/poniaemu-mirror}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../..

case "$TARGET" in
    linux-arm64)  BB=aarch64 ;;
    linux-armhf)  BB=armv7-neon-hf ;;
    linux-x86_64) BB=x86_64 ;;
    *) echo "unsupported target: $TARGET (Linux targets only)" >&2; exit 2 ;;
esac

mkdir -p "$OUT"
LC=$(mktemp)
trap 'rm -f "$LC"' EXIT
g++ -std=c++17 -I "$SRC/src/libretro-host" -o "$LC" "$HERE/list_cores.cpp"

"$LC" "$TARGET" cores | while read -r core source lic kb; do
    if [ "$source" = poniacore ]; then
        file=${core}_libretro_${TARGET}.so.zip
        url=https://github.com/ponia2026/PoniaCore/releases/download/cores-latest/$file
    else
        name=$core
        [ "$name" = mamearcade ] && name=mame   # the Linux buildbot names the full MAME core "mame"
        file=${name}_libretro.so.zip
        url=https://buildbot.libretro.com/nightly/linux/$BB/latest/$file
    fi
    if [ -s "$OUT/$file" ]; then
        echo "have  $file"
    elif curl -fsSL -o "$OUT/$file.part" "$url" && mv "$OUT/$file.part" "$OUT/$file"; then
        echo "got   $file ($(du -h "$OUT/$file" | cut -f1))"
    else
        rm -f "$OUT/$file.part"
        echo "FAILED $file from $url"
    fi
done
echo "done: $OUT"
