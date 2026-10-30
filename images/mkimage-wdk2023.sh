#!/bin/bash
# The Windows Dev Kit 2023 image: the Dev Kit test build's GPT disk and EFI
# partition (its 7.2 kernel, which loads blackrock.dtb), with the generic
# image's Debian root ($ROOTFS) except the generic and Pi
# kernel packages, and riscos-kernel-wdk2023 preinstalled instead.
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
export ROOTFS
R=$ROOTFS; U=$WORK; ST=$WORK/wdkstash
S=$ROLPKG_SITE
rm -rf $ST; mkdir -p $ST/info; cp -p $R/var/lib/dpkg/status $ST/status
for k in generic pi; do mv $R/var/lib/dpkg/info/riscos-kernel-$k.* $ST/info/; mv $R/usr/lib/riscos/boot-$k $ST/; done
restore () {
  rm -rf $R/usr/lib/riscos/boot-wdk2023 $R/var/lib/dpkg/info/riscos-kernel-wdk2023.*
  mv $ST/boot-generic $ST/boot-pi $R/usr/lib/riscos/
  mv $ST/info/* $R/var/lib/dpkg/info/
  cp -p $ST/status $R/var/lib/dpkg/status
}
trap restore EXIT
python3 - <<'PY'
import re
import os; p=os.environ['ROOTFS']+'/var/lib/dpkg/status'; s=open(p).read()
st=[x for x in s.strip('\n').split('\n\n') if not re.search(r'^Package: riscos-kernel-(generic|pi)$',x,re.M)]
open(p,'w').write('\n\n'.join(st)+'\n')
PY
$ROLPKG/tools/preinstall.py $R $S/pool/riscos-kernel-wdk2023_*.deb
chown -R 1000:1000 $R/riscos
IMG=$U/wdk8.img; rm -f $IMG; cp --sparse=always $U/b25.img $IMG
fallocate -p -o $((526336*512)) -l $((3072*1048576)) $IMG
R5=$U/rootw.ext4; rm -f $R5; mkfs.ext4 -q -F -L riscos -d $R $R5 3072M
dd if=$R5 of=$IMG bs=512 seek=526336 conv=notrunc,sparse status=none; rm -f $R5
e2fsck -fn "$IMG?offset=$((526336*512))" 2>&1 | tail -1
# the licences, and where the source is, on the EFI partition
mmd -D s -i "$IMG@@1M" ::LICENCES 2>/dev/null || true
mcopy -o -i "$IMG@@1M" $TOP/licences/* ::LICENCES/
mcopy -o -i "$IMG@@1M" $TOP/licences/SOURCE.txt ::SOURCE.txt
mcopy -o -i "$IMG@@1M" $TOP/images/README-WDK2023.txt ::README.txt
ls -lh $IMG; du -h $IMG
