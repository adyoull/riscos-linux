#!/bin/bash
# Root file system for images 1.0.0019: the published 1.0.0018 generic
# image's root, with the newest packages (riscos-rom 5.31-18, riscos-support
# 1.0.0032-1, riscos-harddisc 1.0.0037-1, riscos-kernel-generic 7.2.0-4,
# riscos-kernel-pi 6.6.78-6) put in as dpkg would: files the new versions
# no longer have are removed, and the postinsts' work done here (with ROOT)
# instead of on the machine.
#   mkroot19.sh GEN18.IMG ROOT POOL
#   OFF=$((257 * 2048)) KERNELS=wdk2023 mkroot19.sh WDK18.IMG ROOT POOL
# (the Dev Kit image: its root starts at 257 MB, and it has only
# riscos-kernel-wdk2023, 7.2.0-4)
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -euo pipefail
IMG=$1; R=$2; POOL=$3
T=${ROLPKG:-$(cd "$(dirname "$0")/../../.." && pwd)/riscos-rolpkg}/tools  # riscos-rolpkg beside riscos-linux, or $ROLPKG
M=$(mktemp -d)
L=$(losetup -f --show -o $(( ${OFF:-557056} * 512 )) "$IMG")
mount -o ro "$L" "$M"
rm -rf "$R"; mkdir -p "$R"
cp -a "$M/." "$R/"
umount "$M"; losetup -d "$L"; rmdir "$M"

DEBS="$POOL/riscos-rom_5.31-18_arm64.deb $POOL/riscos-support_1.0.0032-1_arm64.deb
$POOL/riscos-harddisc_1.0.0037-1_all.deb"
for k in ${KERNELS:-generic pi}; do
  case $k in pi) v=6.6.78-6 ;; *) v=7.2.0-4 ;; esac
  DEBS="$DEBS $POOL/riscos-kernel-${k}_${v}_arm64.deb"
done
OLD=$(mktemp -d)
for d in $DEBS; do
  n=$(dpkg-deb -f "$d" Package)
  cp "$R/var/lib/dpkg/info/$n.list" "$OLD/$n.list"
done
python3 $T/preinstall.py "$R" $DEBS
# files and then directories the old versions had and the new ones don't
for d in $DEBS; do
  n=$(dpkg-deb -f "$d" Package)
  comm -23 <(sort "$OLD/$n.list") <(sort "$R/var/lib/dpkg/info/$n.list") | sort -r | while read -r f; do
    [ "$f" = /. ] && continue
    p="$R$f"
    if [ -L "$p" ] || [ -f "$p" ]; then rm -f "$p"; echo "removed $f"
    elif [ -d "$p" ]; then rmdir "$p" 2>/dev/null && echo "removed $f/" || true
    fi
  done
done
rm -rf "$OLD"

# riscos-support's postinst: OpenSSL's root certificates
mkdir -p "$R/usr/lib/ssl"
[ -e "$R/usr/lib/ssl/cert.pem" ] || [ -L "$R/usr/lib/ssl/cert.pem" ] || ln -s /etc/ssl/certs/ca-certificates.crt "$R/usr/lib/ssl/cert.pem"
[ -e "$R/usr/lib/ssl/certs" ] || [ -L "$R/usr/lib/ssl/certs" ] || ln -s /etc/ssl/certs "$R/usr/lib/ssl/certs"

# riscos-harddisc's postinst, run against ROOT: HardDisc4 belongs to riscos
grep '^/riscos/HardDisc4/' "$R/var/lib/dpkg/info/riscos-harddisc.list" | while read -r f; do
  [ -e "$R$f" ] || [ -L "$R$f" ] && chown -h 1000:1000 "$R$f"
done
P=$(mktemp)
dpkg-deb --ctrl-tarfile "$POOL/riscos-harddisc_1.0.0037-1_all.deb" | tar -xO ./postinst |
  sed -e '/dpkg-query -L riscos-harddisc/,/^  done$/d' \
      -e 's#^\(  *\)/usr/lib/riscos/\([a-z-]*\)$#\1"$R"/usr/lib/riscos/\2 "$R"#' \
      -e 's#\(tb\|tk\)="/riscos/#\1="$R/riscos/#' \
      -e 's#^\(  *\)fs=/var/lib/riscos#\1fs="$R"/var/lib/riscos#' \
      -e 's#mkdir -p /var/lib/riscos#mkdir -p "$R"/var/lib/riscos#' > "$P"
if grep -nE '(^|[ "=])/(riscos|var/lib/riscos|usr/lib/riscos)' "$P" | grep -v '\$R' | grep -v '^[0-9]*: *#' | grep -v "case"; then
  echo "postinst: a path not under ROOT (above)"; exit 1
fi
R="$R" sh "$P" configure; rm -f "$P"
echo "root done: $(du -sh "$R" | cut -f1)"
# /riscos and HardDisc4 itself belong to RISC OS (uid 1000), as on the Dev Kit
# image (tidiness: RISC OS could already write there)
chown 1000:1000 "$R/riscos" "$R/riscos/HardDisc4"
