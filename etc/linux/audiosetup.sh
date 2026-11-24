#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# Finds out which HDMI audio route works on this board by playing short beeps and asking you which one you heard,
# then makes that route the default for the menus (PulseAudio) and for the games (ALSA).
#
# Whether HDMI audio comes out can only be judged by ear, so the candidates are played one after another: candidate N
# beeps N times. Make sure the TV is on, set to the HDMI input and not muted, and turn the volume to a normal level.
#
# Usage:
#   sh audiosetup.sh              test, ask which one you heard, apply it (needs a terminal)
#   sh audiosetup.sh --list       print the candidates
#   sh audiosetup.sh --test [N]   play all candidates, or only candidate N, and stop
#   sh audiosetup.sh --apply N    apply candidate N without asking
#   sh audiosetup.sh --reset      remove what this script configured (the saved choice and the boot service as well)
#   sh audiosetup.sh --reinstall  rewrite the boot files from the saved choice (/etc/poniaemu/audio.conf)
#
# The choice is saved by sound card name and put back at every boot by poniaemu-audio.service (--boot), which waits for the card,
# sets the mixer routing and only then lets PulseAudio start. This works for every chip: where no routing is needed it only waits for the card.
#
#   VOLUME=N    HDMI output level in percent on boards that have a gain control (default 80)
#   TEST_VOLUME=N   level used for the beeps (default 40)
set -u

VOLUME="${VOLUME:-80}"
TEST_VOLUME="${TEST_VOLUME:-40}"
ASOUND=/etc/asound.conf
PULSE_SYSTEM=/etc/pulse/system.pa
MARK_BEGIN="# >>> poniaemu audiosetup >>>"
MARK_END="# <<< poniaemu audiosetup <<<"

if [ "$(id -u)" = 0 ]; then SUDO=""; else SUDO="sudo"; fi
say() { printf '%s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { say "audiosetup: '$1' is missing (apt install alsa-utils)"; exit 1; }; }

# ---- candidates -------------------------------------------------------------------------------------------------
# One line per candidate: <card> <device> <tdm index or -1> <tdm letter or ->
# Boards with a "TOHDMITX I2S SRC" control (Amlogic) route HDMI audio from one of the I2S/TDM outputs: every output is
# tried, those that have a gain control (so the vendor sound card actually uses them) first, and every PCM device of the
# card, highest number first. Other boards: the devices whose name mentions HDMI first, then all the others.
#
# The device tree already says which outputs can feed HDMI: the sound card lists one DAI link per output, and the links that
# reach the HDMI transmitter (the "tohdmitx" node) carry a dai-format. HDMI audio needs "i2s"; an output wired as "dsp_a" is the
# one that produced a noisy right channel on a G12A box, so it is dropped when at least one "i2s" output exists.
dt_hdmi_taps() {   # prints "<letter> <dai-format>" for every I2S/TDM output wired to the HDMI transmitter; nothing if unknown
    dt=/proc/device-tree
    [ -d "$dt" ] || return 0
    node=""
    for c in $(find -L "$dt" -name compatible 2>/dev/null); do
        if tr '\0' ' ' < "$c" 2>/dev/null | grep -q 'tohdmitx'; then node="$(dirname "$c")"; break; fi
    done
    [ -n "$node" ] && [ -f "$node/phandle" ] || return 0
    ph="$(od -An -v -tx1 "$node/phandle" | tr -d ' \n')"
    for sound in "$dt"/sound*; do
        [ -d "$sound" ] || continue
        for link in "$sound"/dai-link-*; do
            [ -d "$link" ] || continue
            fmt=""; [ -f "$link/dai-format" ] && fmt="$(tr -d '\0' < "$link/dai-format")"
            for f in "$link"/sound-dai "$link"/codec*/sound-dai; do
                [ -f "$f" ] || continue
                # cells are big-endian 32-bit: <phandle index> pairs for a node with #sound-dai-cells = 1
                set -- $(od -An -v -tx1 "$f" | tr -d '\n' | sed 's/  */ /g')
                while [ $# -ge 8 ]; do
                    if [ "$1$2$3$4" = "$ph" ]; then
                        case "$8" in 00) l=A ;; 01) l=B ;; 02) l=C ;; *) l="" ;; esac
                        [ -n "$l" ] && say "$l ${fmt:-unknown}"
                    fi
                    shift 8
                done
            done
        done
    done
}

candidates() {
    taps="$(dt_hdmi_taps)"
    for card in $(awk '/^ *[0-9]+ \[/ {print $1}' /proc/asound/cards 2>/dev/null); do
        devs="$(aplay -l 2>/dev/null | sed -n "s/^card $card:.*device \([0-9]*\):.*/\1/p" | sort -rn)"
        [ -n "$devs" ] || continue
        items="$(amixer -c "$card" cget name='TOHDMITX I2S SRC' 2>/dev/null | sed -n "s/^ *; Item #\([0-9]*\) 'I2S \(.\)'.*/\1:\2/p")"
        if [ -n "$items" ]; then
            # Only the outputs the sound card really has are candidates: those with a source selector ("TDMOUT_x SRC SEL"),
            # else those with a gain control; if the card has neither, all of them.
            sel=""; gain=""
            for it in $items; do
                letter="${it#*:}"
                amixer -c "$card" cget name="TDMOUT_${letter} SRC SEL" >/dev/null 2>&1 && sel="$sel $it"
                amixer -c "$card" cget name="TDMOUT_${letter} Gain Enable Switch" >/dev/null 2>&1 && gain="$gain $it"
            done
            use="$sel"; [ -n "$use" ] || use="$gain"; [ -n "$use" ] || use="$items"
            # Device tree filter: keep only the outputs wired as i2s, if there is at least one of them that this card really has.
            if [ -n "$taps" ]; then
                good=""
                for it in $use; do
                    echo "$taps" | grep -q "^${it#*:} i2s\$" && good="$good $it"
                done
                [ -n "$good" ] && use="$good"
            fi
            # Order: I2S B, then C, then A (then anything else). Measured on a G12A box (S905L3A): through I2S A the HDMI right channel is
            # pure noise (the left channel is fine, so the beeps are still heard and every candidate looks like it works), through B and C
            # both channels are clean. B is the output the mainline kernel uses for HDMI.
            ordered=""
            for pref in B C A; do
                for it in $use; do [ "${it#*:}" = "$pref" ] && ordered="$ordered $it"; done
            done
            for it in $use; do
                case " $ordered " in *" $it "*) ;; *) ordered="$ordered $it" ;; esac
            done
            use="$ordered"
            for it in $use; do
                for dev in $devs; do say "$card $dev ${it%%:*} ${it#*:}"; done
            done
        else
            # Other boards: rank every playback device by evidence that it is an HDMI/DisplayPort output.
            #   +3 the device name mentions HDMI / DisplayPort     +2 the ALSA description of the device does
            #   +2 the card has an ELD with a connected monitor (card-level, so it only matters between cards)
            # Ties go to the lower device number. The ranking only decides the order of the beeps; the user still confirms by ear.
            cid="$(aplay -l 2>/dev/null | sed -n "s/^card $card: \([^ ]*\) .*/\1/p" | head -1)"
            eld=0
            for f in /proc/asound/card"$card"/eld#*; do
                [ -e "$f" ] || continue
                if grep -q '^monitor_name[[:space:]]\+[^[:space:]]' "$f" 2>/dev/null; then eld=1
                elif awk '/^monitor_present/ {mp=$2} /^eld_valid/ {ev=$2} END {exit !(mp==1 && ev==1)}' "$f" 2>/dev/null; then eld=1; fi
            done
            for dev in $devs; do
                s=0
                aplay -l 2>/dev/null | grep -i "^card $card:.*device $dev:" | grep -qiE 'hdmi|displayport' && s=$((s + 3))
                aplay -L 2>/dev/null | awk -v pat="CARD=$cid,DEV=$dev" '
                    { if ((p1 ~ pat || p2 ~ pat) && $0 ~ /HDMI|DisplayPort/) f = 1; p2 = p1; p1 = $0 } END { exit !f }' && s=$((s + 2))
                [ "$eld" = 1 ] && s=$((s + 2))
                [ -n "${AUDIOSETUP_DEBUG:-}" ] && echo "# card $card device $dev score $s (eld=$eld)" >&2
                echo "$s $dev"
            done | sort -k1,1rn -k2,2n | while read -r s dev; do say "$card $dev -1 -"; done
        fi
    done
}

# Is a TV/monitor connected to an HDMI or DisplayPort connector? 0 = yes, 1 = connectors exist but none is connected, 2 = unknown.
tv_state() {
    seen=0
    for d in /sys/class/drm/card*-HDMI* /sys/class/drm/card*-DP*; do
        [ -r "$d/status" ] || continue
        seen=1
        [ "$(cat "$d/status" 2>/dev/null)" = connected ] && return 0
    done
    [ "$seen" = 1 ] && return 1
    return 2
}

tv_warn() {
    tv_state
    [ $? -eq 1 ] && say "WARNING: no TV is detected on the HDMI connector. Connect it and turn it on, or nothing will be heard."
    return 0
}

# ---- mixer ------------------------------------------------------------------------------------------------------
# Route HDMI audio to the chosen TDM output and set its level. Only does something on boards with the Amlogic controls.
route() {   # card device tdm_index tdm_letter percent
    [ "$3" -ge 0 ] 2>/dev/null || return 0
    lvl=$(( $5 * 255 / 100 ))
    # The sound card is wired in software: the playback device N is the FRDDR_<A+N> fifo, which must be connected to the TDM output
    # ("SINK 1 SEL" = OUT <tdm index>, "SRC 1 EN" on), and the TDM output must take its input from that fifo ("SRC SEL" = IN N).
    # Without this the device cannot even be opened ("no backend DAIs enabled" in the kernel log).
    fifo="$(printf 'ABC' | cut -c$(($2 + 1)))"
    for f in A B C; do
        for n in 1 2 3; do $SUDO amixer -q -c "$1" cset name="FRDDR_$f SRC $n EN Switch" off 2>/dev/null; done
    done
    if [ -n "$fifo" ]; then
        $SUDO amixer -q -c "$1" cset name="FRDDR_$fifo SINK 1 SEL" "OUT $3" 2>/dev/null
        $SUDO amixer -q -c "$1" cset name="FRDDR_$fifo SRC 1 EN Switch" on 2>/dev/null
        $SUDO amixer -q -c "$1" cset name="TDMOUT_$4 SRC SEL" "IN $2" 2>/dev/null
    fi
    $SUDO amixer -q -c "$1" cset name='TOHDMITX I2S SRC' "$3" 2>/dev/null
    $SUDO amixer -q -c "$1" cset name='TOHDMITX Switch' on 2>/dev/null
    for lane in 0 1 2 3; do
        $SUDO amixer -q -c "$1" cset name="TDMOUT_$4 Lane $lane Volume" "$lvl,$lvl" 2>/dev/null
    done
    $SUDO amixer -q -c "$1" cset name="TDMOUT_$4 Gain Enable Switch" on 2>/dev/null
    return 0
}

# ---- test tones -------------------------------------------------------------------------------------------------
# The beeps are one continuous raw stream (S16_LE, 48 kHz, stereo) per candidate: a long silent start, then N beeps. One stream matters:
# an HDMI link needs a second or two after the device is opened before sound comes out, so beeps played as separate short
# commands (each reopening the device) would be cut off or missed. Only aplay is needed, no tone generator.
TONE_DIR=""
make_tone() {
    TONE_DIR="$(mktemp -d)"
    trap 'rm -rf "$TONE_DIR"' EXIT
    # one period of a 750 Hz sine at 48 kHz = 64 samples, as octal escapes for printf
    printf "$(awk 'BEGIN { pi = 3.14159265358979
        for (i = 0; i < 64; i++) { v = int(9000 * sin(2 * pi * i / 64)); if (v < 0) v += 65536
            lo = v % 256; hi = int(v / 256); printf "\\%03o\\%03o\\%03o\\%03o", lo, hi, lo, hi } }')" > "$TONE_DIR/period"
    : > "$TONE_DIR/beep"
    n=0
    while [ "$n" -lt 300 ]; do cat "$TONE_DIR/period" >> "$TONE_DIR/beep"; n=$((n + 1)); done   # 300 periods = 0.4 s
}

stream() {   # count: 3 s of silence, <count> beeps (0.4 s each, 0.4 s apart), 1 s of silence
    head -c 576000 /dev/zero
    n=0
    while [ "$n" -lt "$1" ]; do
        cat "$TONE_DIR/beep"
        head -c 76800 /dev/zero
        n=$((n + 1))
    done
    head -c 192000 /dev/zero
}

play_candidate() {   # index card device tdm_index tdm_letter
    say "  candidate $1: card $2 device $3${5:+ (I2S $5)} - $1 beep(s)"
    route "$2" "$3" "$4" "$5" "$TEST_VOLUME"
    stream "$1" | aplay -q -D "hw:$2,$3" -t raw -f S16_LE -r 48000 -c 2 \
        || say "    (could not play on card $2 device $3: busy or not usable)"
}

run_tests() {   # [only index]
    only="${1:-0}"
    tv_warn
    say "Playing the candidates. Listen on the TV: candidate N beeps N times (after about 3 seconds of silence). A clean beep has no hiss behind it."
    make_tone
    i=0
    candidates | while read -r card dev tdm letter; do
        i=$((i + 1))
        [ "$i" -le 9 ] || break
        if [ "$only" -ne 0 ] && [ "$only" -ne "$i" ]; then continue; fi
        play_candidate "$i" "$card" "$dev" "$tdm" "$letter"
    done
}

# ---- apply / reset ----------------------------------------------------------------------------------------------
strip_block() {   # file
    [ -f "$1" ] || return 0
    $SUDO sed -i "/^$MARK_BEGIN\$/,/^$MARK_END\$/d" "$1"
}

PULSE_DROPIN=/etc/systemd/system/pulseaudio-system.service.d/poniaemu-audio.conf
CONF_DIR=/etc/poniaemu
CONF=$CONF_DIR/audio.conf
ROUTE_UNIT=/etc/systemd/system/poniaemu-audio.service

# The choice is kept in $CONF (card NAME, not number: the number changes when a USB sound device is plugged in) and put back at every boot
# by poniaemu-audio.service, which waits for the sound card, sets the mixer routing and only then lets PulseAudio start. Nothing depends on
# the saved mixer state or on the order the drivers happen to load in.
card_index_of() { sed -n "s/^ *\([0-9][0-9]*\) \[$1 *\].*/\1/p" /proc/asound/cards 2>/dev/null | head -1; }
card_id_of()    { sed -n "s/^ *$1 \[\([^] ]*\) *\].*/\1/p" /proc/asound/cards 2>/dev/null | head -1; }

load_conf() {
    CARD_ID=""; DEVICE=""; TDM_INDEX=-1; TDM_LETTER=-; SAVED_VOLUME=$VOLUME
    [ -r "$CONF" ] && . "$CONF"
    [ -n "${CARD_ID:-}" ] && [ -n "${DEVICE:-}" ]
}

# Boot: wait for the card, route it, and check the routing really stuck. Exit status 1 = it did not (shows up in the journal).
boot_route() {
    load_conf || { say "poniaemu-audio: nothing configured, nothing to do."; return 0; }
    n=0; card=""
    while [ "$n" -lt 40 ]; do
        card="$(card_index_of "$CARD_ID")"
        [ -n "$card" ] && break
        n=$((n + 1)); sleep 1
    done
    [ -n "$card" ] || { say "poniaemu-audio: sound card '$CARD_ID' did not appear within 40 s."; return 1; }
    if [ "$TDM_INDEX" -ge 0 ] 2>/dev/null; then
        n=0; got=""
        while [ "$n" -lt 10 ]; do
            route "$card" "$DEVICE" "$TDM_INDEX" "$TDM_LETTER" "${SAVED_VOLUME:-$VOLUME}"
            got="$(amixer -c "$card" cget name='TOHDMITX I2S SRC' 2>/dev/null | sed -n 's/^ *: values=//p')"
            [ "$got" = "$TDM_INDEX" ] && break
            n=$((n + 1)); sleep 1
        done
        [ "$got" = "$TDM_INDEX" ] || { say "poniaemu-audio: could not set the HDMI route on card $CARD_ID."; return 1; }
    fi
    say "poniaemu-audio: card $CARD_ID (number $card) ready, device $DEVICE${TDM_LETTER:+ (I2S $TDM_LETTER)}."
}

# Safety net after PulseAudio started: make sure the output exists and is the default one (retry for a few seconds).
pulse_wait() {
    n=0
    while [ "$n" -lt 15 ]; do
        pactl list short sinks 2>/dev/null | grep -q 'ponia_hdmi' || pactl load-module module-alsa-sink device=ponia_hdmi sink_name=ponia_hdmi tsched=0 >/dev/null 2>&1
        if pactl list short sinks 2>/dev/null | grep -q 'ponia_hdmi'; then pactl set-default-sink ponia_hdmi >/dev/null 2>&1; return 0; fi
        n=$((n + 1)); sleep 1
    done
    say "poniaemu-audio: the PulseAudio output ponia_hdmi could not be created."
    return 1
}

reset() {
    strip_block "$ASOUND"
    strip_block "$PULSE_SYSTEM"
    [ -s "$ASOUND" ] || $SUDO rm -f "$ASOUND"
    if [ -f "$ROUTE_UNIT" ]; then
        $SUDO systemctl disable poniaemu-audio.service >/dev/null 2>&1 || true
        $SUDO rm -f "$ROUTE_UNIT"
    fi
    if [ -f "$PULSE_DROPIN" ]; then
        $SUDO rm -f "$PULSE_DROPIN"
        $SUDO rmdir "$(dirname "$PULSE_DROPIN")" 2>/dev/null || true
    fi
    $SUDO rm -f "$CONF"; $SUDO rmdir "$CONF_DIR" 2>/dev/null || true
    $SUDO systemctl daemon-reload 2>/dev/null || true
    say "Removed the audio settings of this script."
}

# Write everything that makes the saved choice come back at every boot. Safe to run again (the installer does after an upgrade).
install_boot() {
    load_conf || { say "audiosetup: no saved choice in $CONF."; return 1; }
    SELF="$(readlink -f "$0" 2>/dev/null || echo "$0")"
    case "$SELF" in
        */lib/poniaemu/audiosetup.sh) ;;
        *) $SUDO mkdir -p /usr/local/lib/poniaemu /usr/local/bin
           $SUDO cp "$SELF" /usr/local/lib/poniaemu/audiosetup.sh && $SUDO chmod 755 /usr/local/lib/poniaemu/audiosetup.sh
           $SUDO ln -sf /usr/local/lib/poniaemu/audiosetup.sh /usr/local/bin/audiosetup
           SELF=/usr/local/lib/poniaemu/audiosetup.sh ;;
    esac

    # ALSA default: through dmix, so PulseAudio (menus) and the player (games) can use the device at the same time.
    strip_block "$ASOUND"
    {
        printf '%s\n' "$MARK_BEGIN"
        cat <<EOF
pcm.ponia_hdmi {
    type dmix
    ipc_key 5978
    ipc_perm 0666
    slave { pcm "hw:$CARD_ID,$DEVICE"  rate 48000  channels 2  format S16_LE  period_size 1024  buffer_size 8192 }
}
pcm.!default { type plug  slave.pcm "ponia_hdmi" }
ctl.!default { type hw  card $CARD_ID }
EOF
        printf '%s\n' "$MARK_END"
    } | $SUDO tee -a "$ASOUND" >/dev/null

    # The routing service runs before PulseAudio; it is harmless on boards that need no routing (it only waits for the card).
    $SUDO tee "$ROUTE_UNIT" >/dev/null <<EOF
[Unit]
Description=PoniaEmu HDMI audio route
After=sound.target alsa-restore.service alsa-state.service systemd-udev-settle.service
Before=pulseaudio-system.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh $SELF --boot

[Install]
WantedBy=multi-user.target
EOF
    $SUDO systemctl daemon-reload 2>/dev/null || true
    $SUDO systemctl enable poniaemu-audio.service >/dev/null 2>&1 || true

    # PulseAudio (system mode on boards without a desktop): the output is created from system.pa; the drop-in orders it after the route
    # and adds a retry in case the output could not be created at start.
    if [ -f "$PULSE_SYSTEM" ]; then
        strip_block "$PULSE_SYSTEM"
        {
            printf '%s\n' "$MARK_BEGIN"
            printf '%s\n' "load-module module-alsa-sink device=ponia_hdmi sink_name=ponia_hdmi tsched=0"
            printf '%s\n' "set-default-sink ponia_hdmi"
            printf '%s\n' "$MARK_END"
        } | $SUDO tee -a "$PULSE_SYSTEM" >/dev/null
        if command -v systemctl >/dev/null 2>&1 && [ -f /etc/systemd/system/pulseaudio-system.service ]; then
            $SUDO mkdir -p "$(dirname "$PULSE_DROPIN")"
            printf '%s\n' "[Unit]" "Wants=poniaemu-audio.service" "After=sound.target poniaemu-audio.service" "[Service]" \
                "ExecStartPost=-/bin/sh $SELF --pulse-wait" | $SUDO tee "$PULSE_DROPIN" >/dev/null
            $SUDO systemctl daemon-reload 2>/dev/null || true
        fi
    fi
}

apply() {   # index
    line="$(candidates | sed -n "${1}p")"
    [ -n "$line" ] || { say "audiosetup: there is no candidate $1"; return 1; }
    set -- $line
    card=$1; dev=$2; tdm=$3; letter=$4
    route "$card" "$dev" "$tdm" "$letter" "$VOLUME"
    $SUDO alsactl store 2>/dev/null || true

    # Save the choice by card NAME and set up what restores it at every boot (see install_boot).
    cid="$(card_id_of "$card")"
    [ -n "$cid" ] || { say "audiosetup: cannot find the name of card $card."; return 1; }
    $SUDO mkdir -p "$CONF_DIR"
    printf 'CARD_ID=%s\nDEVICE=%s\nTDM_INDEX=%s\nTDM_LETTER=%s\nSAVED_VOLUME=%s\n' "$cid" "$dev" "$tdm" "$letter" "$VOLUME" | $SUDO tee "$CONF" >/dev/null
    install_boot || return 1
    $SUDO systemctl restart poniaemu-audio.service 2>/dev/null || true
    if [ -f "$PULSE_SYSTEM" ]; then
        $SUDO systemctl restart pulseaudio-system.service 2>/dev/null || $SUDO systemctl restart pulseaudio 2>/dev/null || true
        sleep 2
        if command -v pactl >/dev/null 2>&1; then
            pactl list short sinks 2>/dev/null | grep -q ponia_hdmi && say "PulseAudio output ponia_hdmi is up." \
                || say "note: the PulseAudio output ponia_hdmi is not up yet; it is retried at every boot (journalctl -u pulseaudio-system)."
        fi
    fi
    # Keep a record of what was detected and what the user picked: samples from different boards show later how far the detection can be trusted.
    tv_state; tvs=$?
    {
        printf '%s model=[%s] picked=card%s/dev%s/tdm%s/%s taps=[%s] tv=%s candidates=[%s]\n' "$(date '+%F %T')" \
            "$(tr -d '\0' < /proc/device-tree/model 2>/dev/null)" "$card" "$dev" "$tdm" "$letter" \
            "$(dt_hdmi_taps | tr '\n' ',')" "$tvs" "$(candidates | tr '\n' ';')"
    } | $SUDO tee -a /var/log/poniaemu-audiosetup.log >/dev/null 2>&1 || true
    say "HDMI audio set to card $card, device $dev${letter:+ (I2S $letter)} at ${VOLUME}%."
}

# ---- main -------------------------------------------------------------------------------------------------------
need aplay; need amixer; need awk

case "${1:-}" in
    --list)
        candidates | { i=0; while read -r card dev tdm letter; do i=$((i + 1)); say "$i: card $card device $dev tdm=$tdm ${letter:+I2S $letter}"; done; }
        ;;
    --test) run_tests "${2:-0}" ;;
    --apply) [ -n "${2:-}" ] || { say "usage: $0 --apply N"; exit 2; }; apply "$2" ;;
    --reset) reset ;;
    --boot) boot_route ;;                 # used by poniaemu-audio.service at every boot
    --pulse-wait) pulse_wait ;;           # used by pulseaudio-system.service after it started
    --reinstall) install_boot ;;          # rewrite the boot files from the saved choice (the installer does this after an upgrade)
    "")
        count="$(candidates | wc -l)"
        if [ "$count" -eq 0 ]; then
            say "audiosetup: no sound device found: the kernel has no sound card on this board."
            say "  On a board whose device tree does not enable the HDMI audio (some Rockchip boards) there is nothing to set up until it is enabled."
            exit 1
        fi
        [ -r /dev/tty ] && [ -w /dev/tty ] || { say "audiosetup: no terminal to ask on; use --test and --apply N."; exit 1; }
        say "HDMI audio setup: $count candidate(s). Turn the TV on, select the HDMI input and set a normal volume."
        say "Stop any running game first: the test uses the sound device and changes the HDMI route."
        tv_warn
        printf 'Press Enter to start the beeps (s = skip this setup): ' >/dev/tty
        read -r ready </dev/tty || exit 1
        [ "$ready" = s ] && { say "Skipped. You can run it later with: audiosetup"; exit 0; }
        while :; do
            run_tests
            say "Several candidates can work: the sound card has more than one playback device that reaches the same HDMI output."
            say "If you heard more than one, any of them is fine: type the lowest number that sounded clean. A candidate with hiss or crackle on it is not good: skip it."
            printf 'Number you heard (0 = none, r = play again, q = quit): ' >/dev/tty
            read -r answer </dev/tty || exit 1
            case "$answer" in
                q) exit 0 ;;
                r) continue ;;
                0) say "Nothing was heard. Check the TV (input, volume, mute) and the cable, then run this again." ; exit 1 ;;
                *[!0-9]*|"") say "Please type a number." ;;
                *) apply "$answer" && exit 0 ;;
            esac
        done
        ;;
    *) say "usage: $0 [--list | --test [N] | --apply N | --reset | --reinstall]"; exit 2 ;;
esac
