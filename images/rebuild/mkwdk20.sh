#!/bin/bash
# The Windows Dev Kit 2023 image 1.0.0020 (the 1.0.0018 layout): GPT, the
# EFI partition (256 MB) with the Dev Kit's 7.2 kernel, which loads
# blackrock.dtb, and the root (3072 MB, PARTUUID
# 5249534f-534c-494e-5558-000000000003).
#   mkwdk20.sh WDK19.IMG ROOT OUT.IMG
# EFI partition: the 1.0.0019 image's files, with the kernel and device
# tree from ROOT's riscos-kernel-wdk2023, README.txt and the licences
# refreshed. ROOT: made by mkroot20.sh (OFF=$((257 * 2048))).
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -euo pipefail
OLD=$1; R=$2; IMG=$3
U=$(cd "$(dirname "$0")/.." && pwd)
ESPD=$(mktemp -d)
mcopy -s -n -i "$OLD@@$((2048 * 512))" '::/*' "$ESPD/"
cp -a "$R/usr/lib/riscos/boot-wdk2023/." "$ESPD/"
cp $U/README-WDK2023.txt "$ESPD/README.txt"
rm -rf "$ESPD/LICENCES"; mkdir -p "$ESPD/LICENCES"
cp $U/../licences/* "$ESPD/LICENCES/"
cp $U/../licences/SOURCE.txt "$ESPD/SOURCE.txt"
ESP_MB=256; ROOT_MB=3072
rm -f "$IMG"; truncate -s $(( 1 + ESP_MB + ROOT_MB + 1 ))M "$IMG"
sgdisk -o -n 1:1M:+${ESP_MB}M -t 1:ef00 -c 1:EFI -n 2:0:+${ROOT_MB}M -t 2:8300 -c 2:riscos -u 2:5249534f-534c-494e-5558-000000000003 "$IMG" >/dev/null
ESP=$(mktemp -u).fat
mkfs.vfat -F 32 -C -n EFI "$ESP" $((ESP_MB * 1024)) >/dev/null
mcopy -s -i "$ESP" "$ESPD"/* ::/
dd if="$ESP" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$ESP"; rm -rf "$ESPD"
R5=$(mktemp -u).ext4; mkfs.ext4 -q -F -L riscos -d "$R" "$R5" ${ROOT_MB}M
dd if="$R5" of="$IMG" bs=1M seek=$((1 + ESP_MB)) conv=notrunc,sparse status=none; rm -f "$R5"
e2fsck -fn "$IMG?offset=$(( (1 + ESP_MB) * 1048576 ))" 2>&1 | tail -1
sgdisk -p "$IMG" | tail -3
ls -lh "$IMG"; du -h "$IMG"
