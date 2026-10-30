#!/bin/bash
# Get BASICVFP (BBC BASIC VI for VFP, RISC OS Open's) and other modules for
# the test bed from RISC OS Open's Raspberry Pi ROM: download the ROM zip
# named in basicvfp/PIROM, check its sha256, take the modules listed there
# out of the ROM image's module chain, check those too, and write them:
# out/modules/BASICVFP,ffa and SharedSound,ffa, and
# out/pirom/VFPSupport-ROOL,ffa (ROOL's VFPSupport: a test compares its
# elementary functions with ours).
# Set ROL_PIROM_FILE to use a copy of the zip you already have.
#
# Why: the port's ROM has no BASICVFP, but its *BASIC64 runs BASICVFP
# whenever VFPSupport is there (as on a Pi), and BASICVFP is the main user
# of VFPSupport_ElementaryFunctions. rol-run loads it when it's there. The
# port's ROM has SoundDMA, but not SharedSound (several programs' sound at
# once), which sound players use.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TB=$(cd "$HERE/.." && pwd)
: "${OUT:=$TB/out/modules}"
: "${DL:=$TB/out/pirom}"
{ read -r zip sum url; cat > "$DL/modules.list"; } < <(grep -v '^#' "$HERE/PIROM")
mkdir -p "$OUT" "$DL"
if [ -n "${ROL_PIROM_FILE:-}" ]; then
  cp "$ROL_PIROM_FILE" "$DL/$zip.part"
elif [ ! -s "$DL/$zip" ]; then
  curl -fsSL --retry 3 -o "$DL/$zip.part" "$url"
fi
[ -f "$DL/$zip.part" ] && mv "$DL/$zip.part" "$DL/$zip"
if ! echo "$sum  $DL/$zip" | sha256sum -c --quiet; then
  rm -f "$DL/$zip"
  echo "basicvfp/fetch.sh: $zip doesn't match basicvfp/PIROM (removed; run again to download it)" >&2
  exit 1
fi
python3 -I - "$DL/$zip" "$DL/modules.list" "$OUT" "$DL" <<'PY'
import hashlib, struct, sys, zipfile
rom = zipfile.ZipFile(sys.argv[1]).read("RISCOS.IMG")
u = lambda o: struct.unpack_from("<I", rom, o)[0]
# The ROM module chain: each module is preceded by its length + 4; 0 ends
# it. Find it by looking for a long run of modules with sensible titles.
for p in range(0, len(rom) - 8, 4):
    q, mods = p, []
    while (len(mods) < 200 and q + 64 <= len(rom) and 32 < u(q) < 0x100000
           and not u(q) & 3 and q + u(q) <= len(rom)):
        start, length = q + 4, u(q) - 4
        t = u(start + 16)
        title = rom[start + t:start + t + 32].split(b"\0")[0] if 0 < t < length else b""
        if not title or not all(32 < c < 127 for c in title):
            break
        mods.append((title.decode(), start, length))
        q += u(q)
    if len(mods) > 30 and q + 4 <= len(rom) and u(q) == 0:
        break
else:
    sys.exit("basicvfp/fetch.sh: no module chain in the ROM image")
found = {t: rom[s:s + l] for t, s, l in mods}
for line in open(sys.argv[2]):
    if not line.strip():
        continue
    title, want, where = line.split()
    if title not in found:
        sys.exit(f"basicvfp/fetch.sh: no {title} in the ROM image")
    if hashlib.sha256(found[title]).hexdigest() != want:
        sys.exit(f"basicvfp/fetch.sh: the {title} taken out isn't the one in basicvfp/PIROM")
    out = f"{sys.argv[3]}/{title},ffa" if where == "modules" else f"{sys.argv[4]}/{title}-ROOL,ffa"
    open(out, "wb").write(found[title])
    print(out)
PY
