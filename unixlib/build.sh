#!/bin/bash
# Build UnixLib with the RISC OS for Linux patch: riscos-unixlib at the
# commit in unixlib/UNIXLIB, plus unixlib/linux-port-pvops.patch, built
# with riscos-crossdev's compiler (RISCOS_TOOLCHAIN) by UnixLib's own
# build/build-unixlib.sh. Output: out/unixlib/libunixlib.a.
#
# Programs linked with it (-L out/unixlib before the toolchain's library)
# can use threads, and report faults, on the Linux port. On other RISC OS
# machines it behaves as the unpatched library.
#
# Environment:
#   UNIXLIB_SRC   an existing riscos-unixlib clone that has the commit
#                 (default: clone it into out/unixlib/riscos-unixlib)
#   GCC_SRC, GCCSDK_SRC   GCC 10.2.0 source and a riscos-gccsdk checkout,
#                 as build/build-unixlib.sh takes them; if both are set,
#                 nothing is downloaded for them (otherwise UnixLib's
#                 build/fetch-sources.sh fetches them from GitHub)
#   GCC_TARBALL   gcc-10.2.0.tar.gz you already have (checked by sha256)
#
# Needs git, autoconf2.69, automake 1.11, perl (riscos-crossdev 1.8.1's
# image has them).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TB=$(cd "$HERE/.." && pwd)
: "${OUT:=$TB/out/unixlib}"
: "${RISCOS_TOOLCHAIN:=${RCD_DIR:-/opt/riscos-crossdev}/toolchain/env}"
read -r _ url commit < <(grep -v '^#' "$HERE/UNIXLIB")
mkdir -p "$OUT"

src=${UNIXLIB_SRC:-$OUT/riscos-unixlib}
if [ ! -d "$src/.git" ]; then
  git clone -q --filter=blob:none --no-checkout "$url" "$src"
fi
git -C "$src" cat-file -e "$commit^{commit}" 2>/dev/null || git -C "$src" fetch -q origin "$commit"

# A clean copy of that commit, patched.
work=$OUT/work
rm -rf "$work"; mkdir -p "$work"
git -C "$src" archive "$commit" | tar -x -C "$work"
# (patch, not git apply: inside another git repository, such as this
# one's out/, git apply skips the files without a word.)
patch -d "$work" -p1 -s --no-backup-if-mismatch < "$HERE/linux-port-pvops.patch"
[ -f "$work/libunixlib/incl-local/internal/pvops.s" ] ||
  { echo "unixlib/build.sh: the patch didn't apply" >&2; exit 1; }
echo "unixlib: riscos-unixlib ${commit:0:7} + linux-port-pvops.patch"

if [ -z "${GCC_SRC:-}" ] || [ -z "${GCCSDK_SRC:-}" ]; then
  deps=$OUT/deps
  mkdir -p "$deps"
  [ -z "${GCC_TARBALL:-}" ] || cp "$GCC_TARBALL" "$deps/gcc-10.2.0.tar.gz"
  ( cd "$work" && SRC_DIR=$deps sh build/fetch-sources.sh )
  GCC_SRC=${GCC_SRC:-$deps/gcc-10.2.0}
  GCCSDK_SRC=${GCCSDK_SRC:-$deps/riscos-gccsdk}
fi

GCC_SRC=$GCC_SRC GCCSDK_SRC=$GCCSDK_SRC GCCSDK_ENV=$RISCOS_TOOLCHAIN \
  bash "$work/build/build-unixlib.sh"
cp "$work/build/work/build/.libs/libunixlib.a" "$OUT/libunixlib.a"
echo "$OUT/libunixlib.a"
