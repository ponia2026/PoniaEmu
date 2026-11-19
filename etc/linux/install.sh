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
#   AUTOMOUNT=1|0    install (default, 1) or skip (0) the automount of external disks, see automount.sh
#   SOUND_SERVER=1|0 install (default, 1) or skip (0) a PulseAudio server on a box that has none, see below
#   HDMI_VOLUME=N    set the HDMI output level to N percent on boards that have a gain control (default 80, 0 = leave it alone)
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
    say "Done. Your settings in ~/.config/poniaemu were left alone."
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
        # Newer Ubuntu releases rename some library packages with a "t64" suffix (libqt6core6 -> libqt6core6t64),
        # so for every name try the plain one first and then the t64 one; skip what this distribution does not have.
        pick_pkg() {
            for c in "$1" "$1t64"; do
                if apt-cache policy "$c" 2>/dev/null | grep -q "Candidate: [^(]"; then printf '%s' "$c"; return 0; fi
            done
            return 1
        }
        PKGS=""
        # libgles2/libegl1/libgbm1: the player draws through SDL on KMS/DRM and needs the GLES library; without it a game runs with sound but no picture
        for p in libsdl2-2.0-0 libgles2 libegl1 libgbm1 \
                 libqt6core6 libqt6gui6 libqt6network6 libqt6qml6 libqt6quick6 libqt6core5compat6 \
                 libqt6sql6 libqt6sql6-sqlite libqt6svg6 qt6-svg-plugins qt6-image-formats-plugins qt6-qpa-plugins qt6-wayland libxcb-cursor0 \
                 qml6-module-qtqml qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
                 qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtquick-timeline \
                 qml6-module-qtqml-workerscript qml6-module-qtqml-models qml6-module-qt-labs-qmlmodels \
                 qml6-module-qtmultimedia qml6-module-qt5compat-graphicaleffects; do
            if q="$(pick_pkg "$p")"; then PKGS="$PKGS $q"; else say "note: package $p is not available here, skipping."; fi
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

# Earlier test builds shipped the launcher entry, icons and metadata under the id org.ponia.PoniaFE; remove them when upgrading.
for f in "$PREFIX/share/applications/org.ponia.PoniaFE.desktop" "$PREFIX/share/metainfo/org.ponia.PoniaFE.metainfo.xml" \
         "$PREFIX"/share/icons/hicolor/*/apps/org.ponia.PoniaFE.png; do
    [ -e "$f" ] && $SUDO rm -f "$f"
done

# Mount every disk that is not the system disk under /media/<label>, when it is plugged in and at boot.
# The helper skips whatever is already mounted and never touches the disk that holds the system.
if [ "${AUTOMOUNT:-1}" = 1 ] && command -v systemctl >/dev/null 2>&1; then
    say "Installing the automount for external disks (remove it with: sh automount.sh --uninstall) ..."
    curl -fsSL "https://raw.githubusercontent.com/$REPO/qt6/etc/linux/automount.sh" | $SUDO sh || say "note: the automount could not be installed, skipping."
fi

# Amlogic boards: the HDMI audio output has a software gain that starts disabled (= full level, very loud on a TV).
# Find the TDM output that feeds HDMI, set it to HDMI_VOLUME percent and save the mixer state so it survives a reboot.
HV="${HDMI_VOLUME:-80}"
if [ "$HV" -gt 0 ] 2>/dev/null && command -v amixer >/dev/null 2>&1; then
    for card in $(awk '/^ *[0-9]+ \[/ {print $1}' /proc/asound/cards 2>/dev/null); do
        src="$($SUDO amixer -c "$card" cget name='TOHDMITX I2S SRC' 2>/dev/null | sed -n 's/.*: values=\([0-9]*\).*/\1/p')"
        [ -n "$src" ] || continue
        case "$src" in 0) tdm=A ;; 1) tdm=B ;; 2) tdm=C ;; *) continue ;; esac
        lvl=$((255 * HV / 100))
        for lane in 0 1 2 3; do
            $SUDO amixer -q -c "$card" cset name="TDMOUT_${tdm} Lane ${lane} Volume" "$lvl,$lvl" 2>/dev/null || true
        done
        $SUDO amixer -q -c "$card" cset name="TDMOUT_${tdm} Gain Enable Switch" on 2>/dev/null || true
        $SUDO alsactl store 2>/dev/null || true
        say "HDMI audio level set to ${HV}% (card $card, TDMOUT_${tdm})."
    done
fi

# The menus play sound through Qt Multimedia, which only talks to a PulseAudio-compatible server. A box without a desktop has
# none, so the frontend would be silent. PulseAudio refuses to run for root as a normal session service, so on such a box it is
# installed as a system-wide service that every local program (they all run as root there) connects to.
if [ "${SOUND_SERVER:-1}" = 1 ] && command -v systemctl >/dev/null 2>&1 \
   && ! systemctl is-active --quiet display-manager.service 2>/dev/null \
   && ! systemctl is-active --quiet pipewire-pulse.service 2>/dev/null \
   && ! command -v pipewire-pulse >/dev/null 2>&1 \
   && [ "$(id -u)" -eq 0 -o -n "$SUDO" ]; then
    if ! command -v pulseaudio >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1; then
        say "Installing PulseAudio (sound for the frontend menus) ..."
        $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -q pulseaudio pulseaudio-utils >/dev/null 2>&1 || say "note: PulseAudio could not be installed, skipping."
    fi
    if command -v pulseaudio >/dev/null 2>&1; then
        $SUDO sed -i 's/^load-module module-native-protocol-unix$/load-module module-native-protocol-unix auth-anonymous=1/' /etc/pulse/system.pa 2>/dev/null || true
        $SUDO mkdir -p /etc/pulse/client.conf.d
        $SUDO tee /etc/pulse/client.conf.d/10-poniaemu.conf >/dev/null <<'EOC'
# Programs on this box talk to the system-wide PulseAudio instance (see pulseaudio-system.service).
default-server = unix:/var/run/pulse/native
autospawn = no
EOC
        $SUDO tee /etc/systemd/system/pulseaudio-system.service >/dev/null <<'EOC'
[Unit]
Description=PulseAudio system-wide sound server (PoniaEmu)
After=sound.target

[Service]
ExecStart=/usr/bin/pulseaudio --system --realtime=no --disallow-exit --daemonize=no --log-target=journal
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOC
        $SUDO systemctl daemon-reload
        $SUDO systemctl enable --now pulseaudio-system.service >/dev/null 2>&1 || say "note: the sound server could not be started."
    fi
fi

command -v update-desktop-database >/dev/null 2>&1 && $SUDO update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
# Make sure every shared library is there; a missing one would only show up as an error when starting the program.
if command -v ldd >/dev/null 2>&1; then
    MISSING="$(ldd "$PREFIX/bin/poniaemu" 2>/dev/null | grep "not found" | awk '{print $1}' | tr '\n' ' ')"
    if [ -n "$MISSING" ]; then
        say ""
        say "PoniaEmu is installed, but these libraries are missing, so it cannot start yet:"
        say "  $MISSING"
        say "Install the packages that provide them (apt-cache search can help, for example: apt-cache search libqt6sql6) and run poniaemu again."
        exit 1
    fi
fi

say ""
say "Installed. Start it with:  poniaemu"
say "Games and BIOS files are not included, see the README for where to put them."
say ""
say "Start it from a desktop session, or on a console without a desktop with:  QT_QPA_PLATFORM=eglfs poniaemu"
