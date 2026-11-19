#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# Repack an APK produced by `ninja poniafe_make_apk`: drop the dead space left behind by Gradle's
# incremental packaging (a 74 MB file can hold only ~26 MB of real data), re-align and re-sign it.
#
# Usage: repack-apk.sh <in.apk> <out.apk> [keystore] [build-tools dir]
# The keystore defaults to ~/.android/debug.keystore (password "android", debug signing only).
set -e
IN=$1; OUT=$2
KS=${3:-$HOME/.android/debug.keystore}
BT=${4:-$ANDROID_HOME/build-tools/33.0.2}
[ -n "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 in.apk out.apk [keystore] [build-tools]"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir "$TMP/x"
(cd "$TMP/x" && unzip -q "$IN" && rm -rf META-INF)
# Only the real top-level entries, so that nothing stale is carried over.
(cd "$TMP/x" && zip -q -r -X "$TMP/unsigned.apk" AndroidManifest.xml classes*.dex resources.arsc res assets lib)
"$BT/zipalign" -f -p 4 "$TMP/unsigned.apk" "$TMP/aligned.apk"
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out "$OUT" "$TMP/aligned.apk"
"$BT/apksigner" verify "$OUT"
ls -l "$OUT"
