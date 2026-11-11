#!/bin/sh
# build.sh [OUT]: builds !RiSH into OUT (default ../../out/apps) with the
# riscos-crossdev toolchain (RISCOS_TOOLCHAIN).
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -e
cd "$(dirname "$0")"
T=${RISCOS_TOOLCHAIN:-/opt/riscos-crossdev/toolchain/env}/bin
out=${1:-../../out/apps}
app="$out/!RiSH"
rm -rf "$app"
mkdir -p "$app" "$out/obj"
"$T/arm-riscos-gnueabihf-gcc" -O2 -mfpu=vfpv3 -mfloat-abi=hard -Wall -Wextra -Werror -Wno-stringop-truncation -static -o "$out/obj/rish.elf" rish.c
"$T/arm-riscos-gnueabihf-strip" "$out/obj/rish.elf"
"$T/elf2aif" -e "$out/obj/rish.elf" "$app/!RunImage,ff8" >/dev/null
cp 'app/!Boot,feb' 'app/!Run,feb' 'app/!Help,fff' 'app/LICENSE,fff' 'app/UnixLib-COPYING,fff' "$app/"
python3 mksprites.py "$app/!Sprites,ff9" 90
python3 mksprites.py "$app/!Sprites11,ff9" 180
ls -l "$app"
