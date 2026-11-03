#!/bin/bash
# Build the Linux port's ROM image on a Raspberry Pi with RISC OS Open's DDE.
#
#   nightly/build-on-pi.sh            (DDE in ../DDE, i.e. ../DDE/AcornC.C++)
#   DDE=/path/to/DDE nightly/build-on-pi.sh
#
# The build runs the DDE inside the Linux port of RISC OS (the bootstrap
# image Unix/RISCOS.IMG), natively on the Pi's 32-bit ARM support.  It
# refuses to run anywhere else, as the DDE is licensed for the Pi it's
# installed on and must not be run under emulation.
#
# Needs: git gcc make bubblewrap libseccomp-dev libattr1-dev libsdl2-dev attr
# Result: Built/RISCOS.IMG (and Build2/Linux/RiscOS/Images/rom)
#
# Copyright (c) 2026 Andrew Youll. Apache License 2.0.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
DDE=${DDE:-../DDE}

model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)
if [[ "$model" != Raspberry\ Pi* ]]; then
  echo "This isn't a Raspberry Pi ($model): not running the DDE here." >&2
  exit 1
fi
[[ -d "$DDE/AcornC.C++" ]] || { echo "Put the DDE at $DDE (so that $DDE/AcornC.C++ exists), or set DDE=..." >&2; exit 1; }

missing=()
for c in gcc make bwrap setfattr pkg-config; do command -v $c >/dev/null || missing+=($c); done
for p in libseccomp sdl2; do pkg-config --exists $p 2>/dev/null || missing+=(lib$p-dev); done
[[ -e /usr/include/attr/attributes.h || -e /usr/include/sys/xattr.h ]] || missing+=(libattr1-dev)
if (( ${#missing[@]} )); then
  echo "Missing: ${missing[*]}" >&2
  echo "sudo apt install git gcc make bubblewrap attr pkg-config libseccomp-dev libattr1-dev libsdl2-dev" >&2
  exit 1
fi

# The build must run natively: arm_test exits 0 when this kernel runs 32-bit
# ARM code and lets the port intercept SWIs.
set +e
Unix/LinuxSupport/arm_test
r=$?
set -e
if (( r != 0 )); then
  echo "This Pi can't run the Linux port natively (arm_test returned $r)." >&2
  [[ $r == 7 ]] && echo 'Try: sudo sysctl vm.mmap_min_addr=12288' >&2
  [[ $r == 126 ]] && echo 'No 32-bit ARM support: on a Pi 5 use the 4K-page kernel (kernel=kernel8.img in config.txt).' >&2
  exit 1
fi

echo "Building on: $model ($(uname -r)), $(git describe --always --dirty)"
time make ACORN_CPP="$(realpath "$DDE")/AcornC.C++" TARGET=Linux METHOD=Linux "$@" check
ls -l Built/RISCOS.IMG
