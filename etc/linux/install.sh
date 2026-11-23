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
#   AUTOMOUNT=1|0    install (1) or skip (0) the automount of external disks, see automount.sh; default: install it only when no desktop runs
#   APT_UPDATE=1|0   run apt-get update before installing the runtime packages (default, 1) or not (0)
#   SOUND_SERVER=1|0 install (default, 1) or skip (0) a PulseAudio server on a box that has none, see below
#   HDMI_VOLUME=N    set the HDMI output level to N percent on boards that have a gain control (default 80, 0 = leave it alone)
#   AUDIO_SETUP=1|0  1 = always run the HDMI sound setup (short beeps, you say which one you heard, see audiosetup.sh), 0 = do not install it;
#                    default: run it when a terminal is available and no desktop runs. It is installed as audiosetup so it can be run again.
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
    # The HDMI sound helper is not part of the package; take back what it configured too.
    if [ -f "$PREFIX/lib/poniaemu/audiosetup.sh" ]; then
        run $SUDO sh "$PREFIX/lib/poniaemu/audiosetup.sh" --reset || true
        run $SUDO rm -f "$PREFIX/lib/poniaemu/audiosetup.sh" "$PREFIX/bin/audiosetup"
    fi
    if [ -d /usr/local/share/fonts/poniaemu ]; then
        run $SUDO rm -rf /usr/local/share/fonts/poniaemu
        command -v fc-cache >/dev/null 2>&1 && run $SUDO fc-cache -f >/dev/null 2>&1 || true
    fi
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
        # A fresh install (or a container) has no package lists yet, and then every package looks "not available";
        # old lists can also point to packages that are gone. So refresh them first (APT_UPDATE=0 skips this).
        # libegl1 exists in every Debian/Ubuntu release, so if even that has no candidate the lists are empty and must be updated anyway.
        if [ "${APT_UPDATE:-1}" = 1 ] || ! apt-cache policy libegl1 2>/dev/null | grep -q "Candidate: [^(]"; then
            say "Updating the package lists (apt-get update) ..."
            run $SUDO apt-get update || say "note: apt-get update failed, check the network and the apt sources."
        fi
        PKGS=""
        # libgl1: the N64, PSP and Saturn cores link libGL.so.1; libbluetooth3/libudev1/libcurl4/libpugixml1v5: needed by the GameCube core (dolphin) from PoniaCore; alsa-utils: HDMI sound setup (amixer, speaker-test)
        # libgles2/libegl1/libgbm1: the player draws through SDL on KMS/DRM and needs the GLES library; without it a game runs with sound but no picture
        for p in libsdl2-2.0-0 libgles2 libegl1 libgbm1 libgl1 fontconfig \
                 libqt6core6 libqt6gui6 libqt6network6 libqt6qml6 libqt6quick6 libqt6core5compat6 \
                 libqt6sql6 libqt6sql6-sqlite libqt6svg6 qt6-svg-plugins qt6-image-formats-plugins qt6-qpa-plugins qt6-wayland libxcb-cursor0 \
                 qml6-module-qtqml qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
                 qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtquick-timeline \
                 qml6-module-qtqml-workerscript qml6-module-qtqml-models qml6-module-qt-labs-qmlmodels \
                 qml6-module-qtmultimedia qml6-module-qt5compat-graphicaleffects \
                 libbluetooth3 libudev1 libcurl4 libpugixml1v5 alsa-utils; do
            if q="$(pick_pkg "$p")"; then PKGS="$PKGS $q"; else say "note: package $p is not available here, skipping."; fi
        done
        if [ "${DRY_RUN:-0}" != 1 ] && ! apt-cache policy libqt6core6 libqt6core6t64 2>/dev/null | grep -q "Candidate: [^(]"; then
            say ""
            say "The Qt 6 packages are not in your package sources. On Ubuntu they are in the 'universe' component; enable it with:"
            say "    sudo add-apt-repository universe && sudo apt-get update"
            say "and run this script again. (Debian and Armbian need no extra step.)"
            exit 1
        fi
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

# The program registers its fonts itself, but a system without any CJK font (a minimal Debian, a fresh Ubuntu) can show boxes for the
# language names in the first menus. A font in the system font directory is found by every fallback lookup at any time, so also put the
# Noto Sans CJK fonts there.
FONT_SRC="$PREFIX/share/poniaemu/themes/macaron/assets/fonts"
FONT_DIR="/usr/local/share/fonts/poniaemu"
if [ -d "$FONT_SRC" ] && ls "$FONT_SRC"/NotoSansCJK-*.otf >/dev/null 2>&1; then
    $SUDO mkdir -p "$FONT_DIR"
    $SUDO cp "$FONT_SRC"/NotoSansCJK-*.otf "$FONT_DIR/"
    command -v fc-cache >/dev/null 2>&1 && $SUDO fc-cache -f "$FONT_DIR" >/dev/null 2>&1 || true
fi

# Earlier test builds shipped the launcher entry, icons and metadata under the id org.ponia.PoniaFE; remove them when upgrading.
for f in "$PREFIX/share/applications/org.ponia.PoniaFE.desktop" "$PREFIX/share/metainfo/org.ponia.PoniaFE.metainfo.xml" \
         "$PREFIX"/share/icons/hicolor/*/apps/org.ponia.PoniaFE.png; do
    [ -e "$f" ] && $SUDO rm -f "$f"
done

# Mount every disk that is not the system disk under /media/<label>, when it is plugged in and at boot.
# The helper skips whatever is already mounted and never touches the disk that holds the system.
# A desktop (GNOME, KDE ...) mounts disks for the logged-in user by itself, and a second mount owned by root would only get in
# the way, so by default this is installed only on a box without a desktop session; AUTOMOUNT=1 forces it.
AM="${AUTOMOUNT:-auto}"
if [ "$AM" = auto ]; then
    if command -v systemctl >/dev/null 2>&1 && ! systemctl is-active --quiet display-manager.service 2>/dev/null; then AM=1; else AM=0; fi
fi
if [ "$AM" = 1 ] && command -v systemctl >/dev/null 2>&1; then
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

# Which HDMI sound route works depends on the board and can only be judged by ear: the helper plays short beeps (candidate N beeps
# N times), asks which one you heard first and makes that route the default for the menus and the games. It is always installed
# (as audiosetup) and is run here when a terminal is available and no desktop runs, or when AUDIO_SETUP=1.
AS="${AUDIO_SETUP:-auto}"
if [ "$AS" != 0 ] && [ "${DRY_RUN:-0}" != 1 ]; then
    $SUDO mkdir -p "$PREFIX/lib/poniaemu"
    if curl -fsSL "https://raw.githubusercontent.com/$REPO/qt6/etc/linux/audiosetup.sh" | $SUDO tee "$PREFIX/lib/poniaemu/audiosetup.sh" >/dev/null; then
        $SUDO ln -sf "$PREFIX/lib/poniaemu/audiosetup.sh" "$PREFIX/bin/audiosetup"
        $SUDO chmod 755 "$PREFIX/lib/poniaemu/audiosetup.sh"
        if [ "$AS" = auto ]; then
            if ( : </dev/tty ) 2>/dev/null && ! systemctl is-active --quiet display-manager.service 2>/dev/null; then AS=1; else AS=0; fi
        fi
        if [ "$AS" = 1 ]; then
            say ""
            say "HDMI sound setup (you can skip it and run it later with: audiosetup)"
            $SUDO sh "$PREFIX/lib/poniaemu/audiosetup.sh" || say "note: the HDMI sound setup did not finish; run audiosetup again later."
        else
            say "To set up the HDMI sound later, run: audiosetup"
        fi
    else
        say "note: the HDMI sound helper could not be downloaded, skipping."
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
say "It works from a desktop session and on a plain console without a desktop (it picks the screen output by itself)."
say "Games and BIOS files are not included, see the README for where to put them."
