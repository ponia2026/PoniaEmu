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
#   sh audiosetup.sh --reset      remove what this script configured
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
candidates() {
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
            for it in $use; do
                for dev in $devs; do say "$card $dev ${it%%:*} ${it#*:}"; done
            done
        else
            hdmi=""; other=""
            for dev in $devs; do
                if aplay -l 2>/dev/null | grep -i "^card $card:.*device $dev:" | grep -qi hdmi; then
                    hdmi="$hdmi $dev"
                else
                    other="$other $dev"
                fi
            done
            for dev in $hdmi $other; do say "$card $dev -1 -"; done
        fi
    done
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
    say "Playing the candidates. Listen on the TV: candidate N beeps N times (after about 3 seconds of silence)."
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

reset() {
    strip_block "$ASOUND"
    strip_block "$PULSE_SYSTEM"
    [ -s "$ASOUND" ] || $SUDO rm -f "$ASOUND"
    if [ -f "$PULSE_DROPIN" ]; then
        $SUDO rm -f "$PULSE_DROPIN"
        $SUDO rmdir "$(dirname "$PULSE_DROPIN")" 2>/dev/null || true
        $SUDO systemctl daemon-reload 2>/dev/null || true
    fi
    say "Removed the audio settings of this script."
}

apply() {   # index
    line="$(candidates | sed -n "${1}p")"
    [ -n "$line" ] || { say "audiosetup: there is no candidate $1"; return 1; }
    set -- $line
    card=$1; dev=$2; tdm=$3; letter=$4
    route "$card" "$dev" "$tdm" "$letter" "$VOLUME"
    $SUDO alsactl store 2>/dev/null || true

    # ALSA default: through dmix, so PulseAudio (menus) and the player (games) can use the device at the same time.
    strip_block "$ASOUND"
    {
        printf '%s\n' "$MARK_BEGIN"
        cat <<EOF
pcm.ponia_hdmi {
    type dmix
    ipc_key 5978
    ipc_perm 0666
    slave { pcm "hw:$card,$dev"  rate 48000  channels 2  format S16_LE  period_size 1024  buffer_size 8192 }
}
pcm.!default { type plug  slave.pcm "ponia_hdmi" }
ctl.!default { type hw  card $card }
EOF
        printf '%s\n' "$MARK_END"
    } | $SUDO tee -a "$ASOUND" >/dev/null

    # PulseAudio (system mode on boards without a desktop): an output on top of the same ALSA default.
    if [ -f "$PULSE_SYSTEM" ]; then
        strip_block "$PULSE_SYSTEM"
        {
            printf '%s\n' "$MARK_BEGIN"
            printf '%s\n' "load-module module-alsa-sink device=default sink_name=ponia_hdmi tsched=0"
            printf '%s\n' "set-default-sink ponia_hdmi"
            printf '%s\n' "$MARK_END"
        } | $SUDO tee -a "$PULSE_SYSTEM" >/dev/null
        # At boot PulseAudio can start before the mixer routing is restored; then the HDMI output cannot be opened, only a null
        # output remains, and the player (which prefers PulseAudio) plays into nothing. Start it after the sound state is restored,
        # and restore it once more right before it starts.
        if command -v systemctl >/dev/null 2>&1 && [ -f /etc/systemd/system/pulseaudio-system.service ]; then
            $SUDO mkdir -p "$(dirname "$PULSE_DROPIN")"
            printf '%s\n' "[Unit]" "After=sound.target alsa-restore.service alsa-state.service" "[Service]" \
                "ExecStartPre=-/usr/sbin/alsactl restore" | $SUDO tee "$PULSE_DROPIN" >/dev/null
            $SUDO systemctl daemon-reload 2>/dev/null || true
        fi
        $SUDO systemctl restart pulseaudio-system.service 2>/dev/null || $SUDO systemctl restart pulseaudio 2>/dev/null || true
    fi
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
    "")
        count="$(candidates | wc -l)"
        [ "$count" -gt 0 ] || { say "audiosetup: no sound device found."; exit 1; }
        [ -r /dev/tty ] && [ -w /dev/tty ] || { say "audiosetup: no terminal to ask on; use --test and --apply N."; exit 1; }
        say "HDMI audio setup: $count candidate(s). Turn the TV on, select the HDMI input and set a normal volume."
        say "Stop any running game first: the test uses the sound device and changes the HDMI route."
        printf 'Press Enter to start the beeps (s = skip this setup): ' >/dev/tty
        read -r ready </dev/tty || exit 1
        [ "$ready" = s ] && { say "Skipped. You can run it later with: audiosetup"; exit 0; }
        while :; do
            run_tests
            say "Several candidates can work: the sound card has more than one playback device that reaches the same HDMI output."
            say "If you heard more than one, any of them is fine: type the lowest number you heard."
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
    *) say "usage: $0 [--list | --test [N] | --apply N | --reset]"; exit 2 ;;
esac
