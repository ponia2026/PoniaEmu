#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# PoniaEmu installer for Linux.
#
#   curl -fsSL https://raw.githubusercontent.com/ponia2026/PoniaEmu/qt6/etc/linux/install.sh | sh
#
# It picks the package that matches your system (x86_64, arm64 or armhf), checks its SHA-256,
# installs the Qt 6 runtime packages with apt (Debian, Ubuntu, Armbian and friends) and unpacks
# PoniaEmu into /usr/local.
#
# Options (environment variables):
#   VERSION=v1.0.0   install this release instead of the latest one
#   PREFIX=/opt/x    install somewhere else (default /usr/local)
#   NO_DEPS=1        do not touch the system packages
#   DRY_RUN=1        only print what would be done
# Remove it again with:  sh install.sh --uninstall
set -eu

REPO="ponia2026/PoniaEmu"
PREFIX="${PREFIX:-/usr/local}"
VERSION="${VERSION:-latest}"
MANIFEST="$PREFIX/share/poniaemu/manifest.txt"

say()  { printf '%s\n' "$*"; }
die()  { printf 'Error: %s\n' "$*" >&2; exit 1; }
run()  { if [ "${DRY_RUN:-0}" = 1 ]; then say "[dry-run] $*"; else "$@"; fi; }

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || die "please run as root, or install sudo."
    SUDO="sudo"
fi

# ---- uninstall -------------------------------------------------------------------------------
if [ "${1:-}" = "--uninstall" ]; then
    [ -f "$MANIFEST" ] || die "nothing to remove: $MANIFEST not found."
    say "Removing PoniaEmu from $PREFIX ..."
    while IFS= read -r f; do
        case "$f" in */) ;; *) run $SUDO rm -f "$PREFIX/$f" ;; esac
    done < "$MANIFEST"
    run $SUDO rm -f "$MANIFEST"
    say "Done. Your settings in ~/.config/poniafe were left alone."
    exit 0
fi

# ---- which package? --------------------------------------------------------------------------
# Many TV boxes run a 64-bit kernel with a 32-bit system, so ask the package system first.
if command -v dpkg >/dev/null 2>&1; then
    case "$(dpkg --print-architecture)" in
        amd64) ARCH=x86_64 ;;
        arm64) ARCH=arm64 ;;
        armhf) ARCH=armhf ;;
        *) die "unsupported architecture: $(dpkg --print-architecture)" ;;
    esac
else
    case "$(uname -m)" in
        x86_64|amd64) ARCH=x86_64 ;;
        aarch64|arm64) ARCH=arm64 ;;
        armv7*|armhf) ARCH=armhf ;;
        *) die "unsupported architecture: $(uname -m)" ;;
    esac
fi

# Package names carry the version, date and commit (PoniaEmu_v1.0.0-20261119.4f0ca498_linux-arm64.tar.gz),
# so look the exact name up in the release instead of guessing it.
if [ "$VERSION" = latest ]; then
    API="https://api.github.com/repos/$REPO/releases/latest"
else
    API="https://api.github.com/repos/$REPO/releases/tags/$VERSION"
fi
say "PoniaEmu ($VERSION) for $ARCH -> $PREFIX"

# ---- runtime packages ------------------------------------------------------------------------
if [ "${NO_DEPS:-0}" != 1 ]; then
    if command -v apt-get >/dev/null 2>&1; then
        # Only ask for packages this distribution actually has.
        PKGS=""
        for p in libsdl2-2.0-0 libqt6svg6 qt6-qpa-plugins qt6-wayland \
                 qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
                 qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtquick-timeline \
                 qml6-module-qtqml-workerscript qml6-module-qtqml-models qml6-module-qt-labs-qmlmodels \
                 qml6-module-qtmultimedia qml6-module-qt5compat-graphicaleffects; do
            if apt-cache show "$p" >/dev/null 2>&1; then PKGS="$PKGS $p"; else say "note: package $p is not available here, skipping."; fi
        done
        run $SUDO apt-get install -y $PKGS
    else
        say "note: no apt found. Please install Qt 6.4+ (Quick, Controls, Multimedia, Qt5Compat, Svg) and SDL2 yourself."
    fi
fi

# ---- download, verify, unpack ----------------------------------------------------------------
command -v curl >/dev/null 2>&1 || die "curl is needed."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ "${DRY_RUN:-0}" = 1 ]; then
    say "[dry-run] would look up the *_linux-$ARCH.tar.gz package in $API"
    say "[dry-run] would download it with SHA256SUMS.txt, unpack into $PREFIX and write $MANIFEST"
    exit 0
fi

URL="$(curl -fsSL "$API" | grep -o "https://[^\"]*_linux-$ARCH\\.tar\\.gz" | head -1 || true)"
[ -n "$URL" ] || die "no Linux package for $ARCH found in release '$VERSION'."
FILE="${URL##*/}"
BASE="${URL%/*}"
say "Downloading $FILE ..."
curl -fL --retry 3 -o "$TMP/$FILE" "$BASE/$FILE"
curl -fsSL --retry 3 -o "$TMP/SHA256SUMS.txt" "$BASE/SHA256SUMS.txt"
( cd "$TMP" && grep " $FILE\$" SHA256SUMS.txt | sha256sum -c - ) || die "checksum mismatch, not installing."

tar -tzf "$TMP/$FILE" > "$TMP/manifest.txt"
$SUDO mkdir -p "$PREFIX"
$SUDO tar -xzf "$TMP/$FILE" -C "$PREFIX"
$SUDO mkdir -p "$(dirname "$MANIFEST")"
$SUDO cp "$TMP/manifest.txt" "$MANIFEST"

# Package 1.0.0 shipped the launcher entry, icons and metadata under the id org.ponia.PoniaFE; remove them when upgrading.
for f in "$PREFIX/share/applications/org.ponia.PoniaFE.desktop" "$PREFIX/share/metainfo/org.ponia.PoniaFE.metainfo.xml" \
         "$PREFIX"/share/icons/hicolor/*/apps/org.ponia.PoniaFE.png; do
    [ -e "$f" ] && $SUDO rm -f "$f"
done

command -v update-desktop-database >/dev/null 2>&1 && $SUDO update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
say ""
say "Installed. Start it with:  poniaemu"
say "Games and BIOS files are not included, see the README for where to put them."
