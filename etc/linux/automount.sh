#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Equuleus Works
#
# Automount for Linux boxes without a desktop (nobody else mounts USB/SATA/NVMe disks there).
#
#   curl -fsSL https://raw.githubusercontent.com/ponia2026/PoniaEmu/qt6/etc/linux/automount.sh | sh
#   sh automount.sh --uninstall
#
# Every partition with a known filesystem that is NOT on the disk holding the running system is mounted
# under /media/<label> as soon as it appears (and again at boot), and unmounted when the disk is removed.
# The system disk (the one holding /, /boot, /boot/efi or /boot/firmware) and swap are never touched.
#
# It installs: a udev rule, the systemd template service poniaemu-mount@.service and the helper
# /usr/local/lib/poniaemu/mount-disk. Needs root, systemd and udev.
set -eu

RULE=/etc/udev/rules.d/99-poniaemu-automount.rules
UNIT=/etc/systemd/system/poniaemu-mount@.service
HELPER=/usr/local/lib/poniaemu/mount-disk

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || { echo "Error: please run as root, or install sudo." >&2; exit 1; }
    SUDO="sudo"
fi
command -v systemctl >/dev/null 2>&1 || { echo "Error: systemd is needed." >&2; exit 1; }

if [ "${1:-}" = "--uninstall" ]; then
    for u in $(systemctl list-units --plain --no-legend 'poniaemu-mount@*' 2>/dev/null | awk '{print $1}'); do
        $SUDO systemctl stop "$u" || true
    done
    $SUDO rm -f "$RULE" "$UNIT" "$HELPER"
    $SUDO rmdir /usr/local/lib/poniaemu 2>/dev/null || true
    $SUDO systemctl daemon-reload
    $SUDO udevadm control --reload
    echo "Automount removed. Disks that are mounted now were unmounted."
    exit 0
fi

$SUDO mkdir -p "$(dirname "$HELPER")"

# ---- the helper -------------------------------------------------------------------------------
$SUDO tee "$HELPER" >/dev/null <<'HELPER_EOF'
#!/bin/sh
# Mount or unmount one partition. Called by poniaemu-mount@<name>.service: mount-disk start|stop <name>
set -u
ACTION=${1:-}; NAME=${2:-}
DEV="/dev/$NAME"
STATE=/run/poniaemu-mount
log() { logger -t poniaemu-mount -- "$*" 2>/dev/null || echo "poniaemu-mount: $*"; }

case "$ACTION" in
    start) ;;
    stop)
        if [ -f "$STATE/$NAME" ]; then
            MP="$(cat "$STATE/$NAME")"
            umount "$MP" 2>/dev/null || umount -l "$MP" 2>/dev/null || true
            rmdir "$MP" 2>/dev/null || true
            rm -f "$STATE/$NAME"
            log "unmounted $MP ($DEV)"
        fi
        exit 0 ;;
    *) echo "usage: $0 start|stop <name>" >&2; exit 2 ;;
esac

[ -b "$DEV" ] || exit 0
mkdir -p "$STATE"
findmnt -rn -S "$DEV" >/dev/null 2>&1 && exit 0          # already mounted by someone else

# Disks (not partitions) behind a device, following dm/lvm/crypt layers.
disks_of() { lsblk -sno NAME,TYPE "$1" 2>/dev/null | awk '$2 == "disk" {print $1}'; }

SYSTEM_DISKS=""
for m in / /boot /boot/efi /boot/firmware; do
    src="$(findmnt -no SOURCE "$m" 2>/dev/null | sed 's/\[.*\]//')"
    [ -n "$src" ] && SYSTEM_DISKS="$SYSTEM_DISKS $(disks_of "$src")"
done
for swap in $(swapon --noheadings --raw --show=NAME 2>/dev/null); do
    SYSTEM_DISKS="$SYSTEM_DISKS $(disks_of "$swap")"
done
for d in $(disks_of "$DEV"); do
    for s in $SYSTEM_DISKS; do
        if [ "$d" = "$s" ]; then log "skip $DEV: it is on the system disk $s"; exit 0; fi
    done
done

TYPE="$(blkid -s TYPE -o value "$DEV" 2>/dev/null)"
LABEL="$(blkid -s LABEL -o value "$DEV" 2>/dev/null)"
UUID="$(blkid -s UUID -o value "$DEV" 2>/dev/null)"
case "$TYPE" in
    ntfs|exfat|vfat|ext2|ext3|ext4|btrfs|xfs|f2fs|iso9660|udf) ;;
    *) log "skip $DEV: filesystem '$TYPE' is not handled"; exit 0 ;;
esac

# Mount point: /media/<label>, else the UUID, else the device name. Keep it a safe file name.
BASE="$(printf '%s' "${LABEL:-${UUID:-$NAME}}" | tr -c 'A-Za-z0-9._-' '_')"
MP="/media/$BASE"
if findmnt -rn -M "$MP" >/dev/null 2>&1; then MP="/media/$BASE-$NAME"; fi
mkdir -p "$MP"

have_fs() { grep -qw "$1" /proc/filesystems; }
case "$TYPE" in
    ntfs)
        if have_fs ntfs3; then T=ntfs3; else T=ntfs-3g; fi
        OPTS="rw,noatime,uid=0,gid=0,umask=022" ;;
    exfat)
        if have_fs exfat; then T=exfat; else T=exfat-fuse; fi
        OPTS="rw,noatime,uid=0,gid=0,umask=022" ;;
    vfat)
        T=vfat; OPTS="rw,noatime,uid=0,gid=0,umask=022,iocharset=utf8,flush" ;;
    iso9660|udf)
        T="$TYPE"; OPTS="ro" ;;
    *)
        T="$TYPE"; OPTS="rw,noatime" ;;
esac

if mount -t "$T" -o "$OPTS" "$DEV" "$MP" 2>/dev/null; then
    log "mounted $DEV ($TYPE) on $MP"
elif mount -t "$T" -o "ro${OPTS#rw}" "$DEV" "$MP" 2>/dev/null; then
    log "mounted $DEV ($TYPE) on $MP READ-ONLY (it could not be mounted read-write, maybe it was not removed cleanly)"
else
    log "could not mount $DEV ($TYPE) on $MP"
    rmdir "$MP" 2>/dev/null || true
    exit 0
fi
printf '%s\n' "$MP" > "$STATE/$NAME"
exit 0
HELPER_EOF
$SUDO chmod 755 "$HELPER"

# ---- the systemd unit -------------------------------------------------------------------------
$SUDO tee "$UNIT" >/dev/null <<'UNIT_EOF'
[Unit]
Description=Mount /dev/%I (PoniaEmu automount)
BindsTo=dev-%i.device
After=dev-%i.device

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/lib/poniaemu/mount-disk start %I
ExecStop=/usr/local/lib/poniaemu/mount-disk stop %I
UNIT_EOF

# ---- the udev rule ----------------------------------------------------------------------------
$SUDO tee "$RULE" >/dev/null <<'RULE_EOF'
# PoniaEmu automount: start poniaemu-mount@<name>.service for every partition with a filesystem.
# The helper decides what to mount and never touches the disk that holds the running system.
ACTION!="add|change", GOTO="poniaemu_automount_end"
SUBSYSTEM!="block", GOTO="poniaemu_automount_end"
KERNEL=="loop*|ram*|zram*|mmcblk*boot*|nbd*|fd*", GOTO="poniaemu_automount_end"
ENV{DEVTYPE}=="partition", ENV{ID_FS_USAGE}=="filesystem", TAG+="systemd", ENV{SYSTEMD_WANTS}+="poniaemu-mount@%k.service"
# A whole disk that is formatted without a partition table
ENV{DEVTYPE}=="disk", ENV{ID_PART_TABLE_TYPE}=="", ENV{ID_FS_USAGE}=="filesystem", TAG+="systemd", ENV{SYSTEMD_WANTS}+="poniaemu-mount@%k.service"
LABEL="poniaemu_automount_end"
RULE_EOF

$SUDO systemctl daemon-reload
$SUDO udevadm control --reload
# Handle the partitions that are already there. Start the service directly instead of replaying udev events,
# which would make every disk (including a slow or fragile USB stick) be probed once more.
for part in $(lsblk -nrpo NAME,TYPE | awk '$2 == "part" {print $1}'); do
    case "${part##*/}" in zram*|loop*|ram*|mmcblk*boot*) continue ;; esac
    $SUDO systemctl start "poniaemu-mount@${part##*/}.service" || true
done

echo "Automount installed. Disks outside the system disk are mounted under /media/<label>:"
findmnt -rn -o TARGET,SOURCE,FSTYPE | awk '$1 ~ "^/media/"' || true
echo "Remove it again with:  sh automount.sh --uninstall"
