#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# 下载「随 APK 内建」的 Libretro 核心，放进 Android 项目的 libs/arm64-v8a/，文件名要叫 lib<核心>_libretro.so
# （APK 里的原生库必须是 lib*.so；前端与播放器都会找 lib 前缀的版本，找得到就不再询问下载）。
#
# 内建原则：①授权可商用（ponia_cores.h 里 noncommercial=false）②体积小。压缩后合计约 10 MB，APK 总大小仍在 40 MB 以下。
# 不内建、首次启动才下载：非商用授权的(snes9x、genesis_plus_gx、fbneo、mame2003_plus)、很大的(flycast 17 MB、mamearcade 257 MB)。
#
# 用法: fetch-bundled-cores.sh [输出目录]   默认 src/app/platform/android/libs/arm64-v8a
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/../../src/app/platform/android/libs/arm64-v8a}
ABI=${ABI:-arm64-v8a}   # 32 位用 ABI=armeabi-v7a（输出目录要另外指定）
BASE=https://buildbot.libretro.com/nightly/android/latest/${ABI}
mkdir -p "$OUT"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# 「下载名:APK 内名称」——mupen64plus_next 的 Android 版叫 mupen64plus_next_gles3
for pair in fceumm gambatte mgba melonds mednafen_pce_fast pcsx_rearmed stella a5200 prosystem retro8 \
            yabasanshiro applewin dosbox_pure mupen64plus_next_gles3:mupen64plus_next; do
    dl=${pair%%:*}; id=${pair##*:}
    curl -fsSL -m 300 -o "$TMP/$dl.zip" "$BASE/${dl}_libretro_android.so.zip"
    unzip -oq "$TMP/$dl.zip" -d "$TMP"
    mv "$TMP/${dl}_libretro_android.so" "$OUT/lib${id}_libretro.so"
    echo "lib${id}_libretro.so  $(wc -c < "$OUT/lib${id}_libretro.so") bytes"
done

# GameCube(Dolphin 核心) 需要的 Sys 资料夹（GPL，压缩后约 3 MB）：放进 APK assets/packs/，播放器第一次跑 GC 时解到 system 目录
ASSETS=${2:-$HERE/../../src/app/platform/android/assets/packs}
mkdir -p "$ASSETS"
curl -fsSL -m 300 -o "$ASSETS/Dolphin.zip" https://buildbot.libretro.com/assets/system/Dolphin.zip
echo "assets/packs/Dolphin.zip  $(wc -c < "$ASSETS/Dolphin.zip") bytes"

# ---- fake08（Pico-8 主力核心，buildbot 没有 Android 版，需自行编译）----
# 源码 https://github.com/jtothebell/fake-08（MIT）。NDK r25：
#   cd platform/libretro/jni && ndk-build APP_ABI=arm64-v8a APP_PLATFORM=android-23
# 产物 libs/arm64-v8a/libfake08.so（约 1.7 MB）→ 复制为 src/app/platform/android/libs/arm64-v8a/libfake08_libretro.so
# 为什么不用 retro8：retro8 不支援 pxa 压缩卡带（Islander 等会黑屏）。
