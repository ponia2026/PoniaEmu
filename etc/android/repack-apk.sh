#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# Repack an APK produced by `ninja poniaemu_make_apk`: drop the dead space left behind by Gradle's
# incremental packaging (a 74 MB file can hold only ~26 MB of real data), re-align and re-sign it.
#
# Usage: repack-apk.sh <in.apk> <out.apk> [keystore] [build-tools dir]
# Use absolute paths: the script changes directory.
# The keystore defaults to ~/.android/debug.keystore (debug signing only).
# Signing secrets come from the environment, so they never show up in a process list or a log:
#   KS_PASS    keystore password (default "android")
#   KEY_PASS   key password (default: same as KS_PASS)
#   KEY_ALIAS  key alias (default: the first key in the keystore)
set -e
IN=$1; OUT=$2
KS=${3:-$HOME/.android/debug.keystore}
BT=${4:-$ANDROID_HOME/build-tools/33.0.2}
[ -n "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 in.apk out.apk [keystore] [build-tools]"; exit 1; }
KS_PASS=${KS_PASS:-android}
KEY_PASS=${KEY_PASS:-$KS_PASS}
export KS_PASS KEY_PASS

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir "$TMP/x"
(cd "$TMP/x" && unzip -q "$IN" && rm -rf META-INF)
# Only the real top-level entries, so that nothing stale is carried over.
(cd "$TMP/x" && zip -q -r -X "$TMP/unsigned.apk" AndroidManifest.xml classes*.dex resources.arsc res assets lib)
"$BT/zipalign" -f -p 4 "$TMP/unsigned.apk" "$TMP/aligned.apk"
if [ -n "$KEY_ALIAS" ]; then
    "$BT/apksigner" sign --ks "$KS" --ks-key-alias "$KEY_ALIAS" --ks-pass env:KS_PASS --key-pass env:KEY_PASS --out "$OUT" "$TMP/aligned.apk"
else
    "$BT/apksigner" sign --ks "$KS" --ks-pass env:KS_PASS --key-pass env:KEY_PASS --out "$OUT" "$TMP/aligned.apk"
fi
"$BT/apksigner" verify "$OUT"
ls -l "$OUT"
