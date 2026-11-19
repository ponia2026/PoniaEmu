#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# Print the file name for this build: PoniaEmu_v<version>-<build date>.<commit><suffix>.<extension>
# Example: PoniaEmu_v1.0.0-20261119.4f0ca498_v7a.apk
# It reads ponia_version.h, generated at build time (the same data as the About page).
# The commit is preceded by "." instead of parentheses: GitHub turns parentheses into dots when a release asset is uploaded, so the name would differ from the local one.
# Usage: apk-name.sh [build dir] [suffix] [extension]
#   suffix: _v8a for 64-bit Android, _v7a for 32-bit Android, _linux-x86_64 / _linux-arm64 / _linux-armhf for Linux (default: empty)
#   extension: apk by default, tar.gz for the Linux archives
#   the build dir defaults to ~/work/poniafe-android/build
BUILD=${1:-$HOME/work/poniafe-android/build}
H="$BUILD/generated/ponia_version.h"
[ -f "$H" ] || { echo "no $H" >&2; exit 1; }
get() { sed -n "s/^#define $1 \"\(.*\)\"$/\1/p" "$H"; }
echo "$(get PONIA_PRODUCT_NAME)_v$(get PONIA_VERSION)-$(get PONIA_BUILD_DATE).$(get PONIA_GIT_SHORT)${2}.${3:-apk}"
