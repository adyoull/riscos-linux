#!/bin/bash
# Download the RISC OS for Linux ROM image named in rom/ROM into out/rom/
# and check its sha256. Set ROL_ROM_FILE to use a copy you already have.
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
: "${OUT:=$HERE/../out/rom}"
read -r name sum url < <(grep -v '^#' "$HERE/ROM")
mkdir -p "$OUT"
if [ -n "${ROL_ROM_FILE:-}" ]; then
  cp "$ROL_ROM_FILE" "$OUT/$name.part"
elif [ ! -s "$OUT/$name" ]; then
  curl -fsSL --retry 3 -o "$OUT/$name.part" "$url"
fi
[ -f "$OUT/$name.part" ] && mv "$OUT/$name.part" "$OUT/$name"
echo "$sum  $OUT/$name" | sha256sum -c --quiet
chmod +x "$OUT/$name"
echo "$OUT/$name"
