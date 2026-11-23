#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# Fill in the release notes template (etc/release/notes.md.in) and check them against the files that are going to be attached.
#
#   sh etc/release/make_notes.sh <tag> <stamp> [dist-folder] > notes.md
#
#   tag     release tag, for example v1.0.0 (the notes show the version without the "v")
#   stamp   the date.commit part of the file names, for example 20261123.56902eb2
#   dist    optional: folder holding the files to attach; every file name the notes mention must exist there
set -eu

TAG=${1:?usage: make_notes.sh <tag> <stamp> [dist-folder]}
STAMP=${2:?usage: make_notes.sh <tag> <stamp> [dist-folder]}
DIST=${3:-}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(sed -e "s/@TAG@/$TAG/g" -e "s/@STAMP@/$STAMP/g" -e "s/@VERSION@/${TAG#v}/g" "$HERE/notes.md.in")

if printf '%s\n' "$OUT" | grep -q '@[A-Z]*@'; then
    echo "make_notes.sh: unfilled placeholder left in the template" >&2
    exit 1
fi

if [ -n "$DIST" ]; then
    missing=0
    for name in $(printf '%s\n' "$OUT" | grep -o 'PoniaEmu_[A-Za-z0-9._-]*\.\(apk\|tar\.gz\)' | sort -u); do
        [ -f "$DIST/$name" ] || { echo "make_notes.sh: the notes mention $name, which is not in $DIST" >&2; missing=1; }
    done
    [ "$missing" = 0 ] || exit 1
fi

printf '%s\n' "$OUT"
