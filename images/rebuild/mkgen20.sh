#!/bin/bash
# Generic AArch64 image 1.0.0020 (the 1.0.0018 layout): MBR, FAT (RISCOS,
# 256 MB at 16 MB) and the ext4 root (3072 MB).
#   mkgen20.sh GEN19.IMG ROOT OUT.IMG
# FAT: the 1.0.0019 image's files, with the kernels, device trees and
# firmware from ROOT's riscos-kernel-pi and riscos-kernel-generic (as
# Updates copies them), README.txt and the licences refreshed. ROOT: made
# by mkroot20.sh.
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -euo pipefail
OLD=$1; R=$2; IMG=$3
U=$(cd "$(dirname "$0")/.." && pwd)
BOOT=$(mktemp -d)
mcopy -s -n -i "$OLD@@$((32768 * 512))" '::/*' "$BOOT/"
cp -a "$R/usr/lib/riscos/boot-pi/." "$BOOT/"
cp -a "$R/usr/lib/riscos/boot-generic/." "$BOOT/"
cp $U/README-Generic.txt "$BOOT/README.txt"
rm -rf "$BOOT/LICENCES"; mkdir -p "$BOOT/LICENCES"; cp $U/../licences/* "$BOOT/LICENCES/"; cp $U/../licences/SOURCE.txt "$BOOT/SOURCE.txt"
DISKID=5249534f
START_MB=16; BOOT_MB=256; ROOT_MB=3072
rm -f "$IMG"
truncate -s $(( START_MB + BOOT_MB + ROOT_MB ))M "$IMG"
python3 $U/mbr.py "$IMG" $DISKID $BOOT_MB $ROOT_MB $START_MB
FAT=$(mktemp -u).fat
mkfs.vfat -F 32 -C -n RISCOS "$FAT" $((BOOT_MB * 1024)) >/dev/null
mcopy -s -i "$FAT" "$BOOT"/* ::/
dd if="$FAT" of="$IMG" bs=1M seek=$START_MB conv=notrunc status=none
rm -f "$FAT"; rm -rf "$BOOT"
R5=$(mktemp -u).ext4
mkfs.ext4 -q -F -L riscos -d "$R" "$R5" ${ROOT_MB}M
dd if="$R5" of="$IMG" bs=1M seek=$((START_MB + BOOT_MB)) conv=notrunc,sparse status=none
rm -f "$R5"
e2fsck -fn "$IMG?offset=$(( (START_MB + BOOT_MB) * 1048576 ))" 2>&1 | tail -1
ls -lh "$IMG"; du -h "$IMG"
