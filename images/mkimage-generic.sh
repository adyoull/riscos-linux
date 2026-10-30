#!/bin/bash
# The generic AArch64 image (Debian 13 base): one MBR disk for 64-bit Raspberry Pis (Pi
# firmware, kernel8.img = rpi-6.6 with the VideoCore codec drivers), UEFI
# machines, and (new) the Pinebook (Allwinner A64) and Pinebook Pro
# (Rockchip RK3399), which start EFI/BOOT/BOOTAA64.EFI (mainline 7.2) from
# U-Boot (Ubuntu's u-boot-sunxi/u-boot-rockchip 2025.10-0ubuntu2.2,
# written into the gap before partition 1 by the user: see README).
# Partition 1 starts at 16 MiB to leave room for the RK3399's U-Boot.
# Root file system: the Dev Kit test build's + rol-fb (overlay, any DRM card) +
# TestBed.Codec (LinuxCodec, the VideoCore stand-ins).
set -euo pipefail
# Where things are (override from the environment):
#   TOP          this repository
#   WORK         a work directory (kernels built, the image, its FAT tree)
#   ROOTFS       the staged Debian root (see images/README.md)
#   MODOUT       the RISC OS modules built (make -C modules OUT=...)
#   LINUX        mainline Linux 7.2 with kernel/generic-7.2's patches
#   RPI          raspberrypi/linux (kernel/pi-6.6/BASE) and raspberrypi/firmware
#   UBOOTDEB     Ubuntu's u-boot-sunxi and u-boot-rockchip .debs, unpacked (x/)
#   WIFI         wifi/'s work directory (firmware and licences)
#   ROLPKG, ROLPKG_SITE   riscos-rolpkg and its built site
: "${TOP:=$(cd "$(dirname "$0")/.." && pwd)}" "${WORK:=/tmp/riscos-linux-build}"
: "${ROOTFS:=$WORK/root}" "${MODOUT:=$WORK/modules}" "${LINUX:=$WORK/linux}" "${RPI:=$WORK/raspberrypi}"
: "${UBOOTDEB:=$WORK/ubdeb}" "${WIFI:=$TOP/wifi/work}" "${ROLPKG:=$WORK/riscos-rolpkg}" "${ROLPKG_SITE:=$WORK/rolpkg-site}"
U=$WORK
K=$RPI/build/arch/arm64/boot
FW=$RPI/firmware
L=$TOP/modules/LinuxCodec
N=$MODOUT
UB=$UBOOTDEB/x/usr/lib/u-boot
D=$LINUX/arch/arm64/boot/dts
BOOT=$U/genboot; rm -rf "$BOOT"; mkdir -p "$BOOT/overlays" "$BOOT/EFI/BOOT" "$BOOT/dtb/allwinner" "$BOOT/dtb/rockchip" \
  "$BOOT/u-boot/pinebook" "$BOOT/u-boot/pinebook-pro"
cp $U/kernel8.codec.img "$BOOT/kernel8.img"
cp $U/Image.generic4 "$BOOT/EFI/BOOT/BOOTAA64.EFI"
for d in bcm2710-rpi-2-b bcm2710-rpi-3-b bcm2710-rpi-3-b-plus bcm2710-rpi-cm3 bcm2710-rpi-zero-2-w bcm2710-rpi-zero-2 \
         bcm2711-rpi-4-b bcm2711-rpi-400 bcm2711-rpi-cm4 bcm2711-rpi-cm4s \
         bcm2712-rpi-5-b bcm2712d0-rpi-5-b bcm2712-rpi-500 bcm2712-rpi-cm5-cm5io bcm2712-rpi-cm5-cm4io; do
  [ -f $K/dts/broadcom/$d.dtb ] && cp $K/dts/broadcom/$d.dtb "$BOOT/"
done
cp $K/dts/overlays/vc4-kms-v3d.dtbo $K/dts/overlays/vc4-kms-v3d-pi4.dtbo \
   $K/dts/overlays/vc4-kms-v3d-pi5.dtbo $K/dts/overlays/overlay_map.dtb "$BOOT/overlays/"
cp $FW/boot/start4.elf $FW/boot/fixup4.dat $FW/boot3/bootcode.bin $FW/boot3/start.elf $FW/boot3/fixup.dat "$BOOT/"
# the 7.2 kernel's device trees, where U-Boot's UEFI looks for them (dtb/<fdtfile>)
cp $D/allwinner/sun50i-a64-pinebook.dtb "$BOOT/dtb/allwinner/"
cp $D/rockchip/rk3399-pinebook-pro.dtb "$BOOT/dtb/rockchip/"
cp $UB/pinebook/u-boot-sunxi-with-spl.bin "$BOOT/u-boot/pinebook/"
cp $UB/pinebook-pro-rk3399/u-boot-rockchip.bin $UB/pinebook-pro-rk3399/u-boot-rockchip-spi.bin "$BOOT/u-boot/pinebook-pro/"
zcat $UBOOTDEB/x/usr/share/doc/u-boot-sunxi/copyright.gz 2>/dev/null > "$BOOT/u-boot/COPYING" || cp $UBOOTDEB/x/usr/share/doc/u-boot-sunxi/copyright "$BOOT/u-boot/COPYING"
cp $TOP/images/README-Generic.txt "$BOOT/README.txt"
# the licences, and where the source is
mkdir -p "$BOOT/LICENCES"; cp $TOP/licences/* "$BOOT/LICENCES/"; cp $TOP/licences/SOURCE.txt "$BOOT/SOURCE.txt"
cp $WORK/lic72/* "$BOOT/"; cp $WIFI/lic/LICENCE.cypress $WIFI/lic/LICENCE.wpa_supplicant $WIFI/lic/LICENCE.libnl "$BOOT/"
DISKID=5249534f
cat > "$BOOT/config.txt" <<'CFG'
# RISC OS on Linux (riscos-linux-testbed) for a 64-bit Raspberry Pi:
# 3/3B+/CM3, Zero 2 W, 4/400/CM4, 5/500/CM5. The kernel has every driver
# built in (no modules). (UEFI machines, the Pinebook and the Pinebook Pro
# start EFI/BOOT/BOOTAA64.EFI instead.)
arm_64bit=1
kernel=kernel8.img
dtoverlay=vc4-kms-v3d
max_framebuffers=2
disable_overscan=1
disable_splash=1
dtparam=audio=on
[pi4]
arm_boost=1
[all]
CFG
echo "console=serial0,115200 console=tty1 root=PARTUUID=$DISKID-02 rootfstype=ext4 rw rootwait loglevel=3 fbcon=nodefer mac80211_hwsim.radios=0" > "$BOOT/cmdline.txt"
IMG=$U/generic5.img
START_MB=16; BOOT_MB=256; ROOT_MB=3072
rm -f $IMG
truncate -s $(( START_MB + BOOT_MB + ROOT_MB ))M $IMG
python3 $U/mbr.py $IMG $DISKID $BOOT_MB $ROOT_MB $START_MB
FAT=$U/genfat.img; rm -f $FAT
mkfs.vfat -F 32 -C -n RISCOS $FAT $((BOOT_MB * 1024)) >/dev/null
mcopy -s -i $FAT "$BOOT"/* ::/
dd if=$FAT of=$IMG bs=1M seek=$START_MB conv=notrunc status=none
rm -f $FAT
# the root file system: Debian 13 (trixie) slim, arm64, the official build
# (debuerreotype), with RISC OS for Linux in /opt/riscos and /riscos and our
# init as /sbin/init: staged in $ROOTFS (see notes)
R5=$U/root5.ext4; rm -f $R5
mkfs.ext4 -q -F -L riscos -d $ROOTFS $R5 ${ROOT_MB}M
dd if=$R5 of=$IMG bs=1M seek=$((START_MB + BOOT_MB)) conv=notrunc,sparse status=none
rm -f $R5
RO="$IMG?offset=$(( (START_MB + BOOT_MB) * 1048576 ))"
e2fsck -fn "$RO" 2>&1 | tail -1
ls -lh $IMG; du -h $IMG
