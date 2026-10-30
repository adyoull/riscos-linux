#!/bin/bash
# Build qemu-arm-riscos: QEMU 8.2.2 linux-user (ARM) with riscos-crossdev's
# alignment-trap patch and qemu-8.2.2-riscos-linux.patch, so it can run the
# Linux port of RISC OS on a non-ARM Linux. Installed as $OUT/qemu-arm-riscos.
# Needs: build-essential ninja-build python3-venv libglib2.0-dev, and meson
# 1.2.3 (pip install meson==1.2.3: the Ubuntu "+ds" tarball has no wheels).
#   qemu/build.sh            (RCD_DIR = the riscos-crossdev checkout)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
: "${RCD_DIR:?set RCD_DIR to the riscos-crossdev directory}"
: "${OUT:=$HERE/../out/bin}"
: "${DL:=$HERE/../dl}"
: "${WORK:=$HERE/work}"
TARBALL=qemu_8.2.2+ds.orig.tar.xz
SUM=5e692dd20950a819ff6550cd3974e00319a93f47c0a15f808c58df1a9e4d2478
URL=https://archive.ubuntu.com/ubuntu/pool/main/q/qemu/$TARBALL
mkdir -p "$DL" "$WORK" "$OUT"
for d in "$DL" "$RCD_DIR/toolchain/dl"; do
  [ -s "$d/$TARBALL" ] && { cp -n "$d/$TARBALL" "$DL/" 2>/dev/null || true; break; }
done
[ -s "$DL/$TARBALL" ] || curl -fsSL --retry 3 -o "$DL/$TARBALL" "$URL"
echo "$SUM  $DL/$TARBALL" | sha256sum -c --quiet
python3 -c 'import mesonbuild' 2>/dev/null || command -v meson >/dev/null ||
  { echo "meson not found: pip install meson==1.2.3" >&2; exit 1; }
cd "$WORK"
rm -rf qemu-8.2.2 && tar xf "$DL/$TARBALL"
cd qemu-8.2.2
patch -p1 -s < "$RCD_DIR/qemu/qemu-8.2.2-align-trap.patch"
patch -p1 -s < "$HERE/qemu-8.2.2-riscos-linux.patch"
mkdir build && cd build
# -O3 and link-time optimisation: 10-20% faster (ROBench; 0.6)
../configure --target-list=arm-linux-user --disable-system --disable-docs \
  --disable-tools --disable-werror --disable-install-blobs \
  --enable-lto --extra-cflags=-O3 >/dev/null
ninja qemu-arm >/dev/null
cp qemu-arm "$OUT/qemu-arm-riscos"
echo "$OUT/qemu-arm-riscos (qemu/work can be deleted)"
