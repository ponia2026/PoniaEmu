#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# 印出这次编译对应的文件名：PoniaEmu_v<版本>-<编译日期>.<提交号><后缀>.<扩展名>
# 例：PoniaEmu_v1.0.0-20261119.4f0ca498_v7a.apk
# 读的是编译时产生的 ponia_version.h（与「关于」页同一份数据）。
# 提交号前用「.」而不用括号：GitHub 上传 Release 时会把括号改成点，名字会跟本机不一致。
# 用法: apk-name.sh [build 目录] [后缀] [扩展名]
#   后缀：64 位安卓传 _v8a，32 位安卓传 _v7a，Linux 传 _linux-x86_64 / _linux-arm64 / _linux-armhf（默认空）
#   扩展名：默认 apk，Linux 压缩包传 tar.gz
#   build 目录默认 ~/work/poniafe-android/build
BUILD=${1:-$HOME/work/poniafe-android/build}
H="$BUILD/generated/ponia_version.h"
[ -f "$H" ] || { echo "no $H" >&2; exit 1; }
get() { sed -n "s/^#define $1 \"\(.*\)\"$/\1/p" "$H"; }
echo "$(get PONIA_PRODUCT_NAME)_v$(get PONIA_VERSION)-$(get PONIA_BUILD_DATE).$(get PONIA_GIT_SHORT)${2}.${3:-apk}"
