#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# 印出这次编译对应的 APK 文件名：PoniaEmu_v<版本>-<编译日期>(<提交号>).apk
# 读的是编译时产生的 ponia_version.h（与「关于」页同一份数据）。
# 用法: apk-name.sh [build 目录] [后缀]   默认 ~/work/poniafe-android/build；32 位包传 _v7a
BUILD=${1:-$HOME/work/poniafe-android/build}
H="$BUILD/generated/ponia_version.h"
[ -f "$H" ] || { echo "no $H" >&2; exit 1; }
get() { sed -n "s/^#define $1 \"\(.*\)\"$/\1/p" "$H"; }
echo "$(get PONIA_PRODUCT_NAME)_v$(get PONIA_VERSION)-$(get PONIA_BUILD_DATE)($(get PONIA_GIT_SHORT))${2}.apk"
