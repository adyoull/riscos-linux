#!/bin/bash
# Download RISC OS Open's HardDisc4 (named in harddisc/HD4), check its
# sha256, unpack it in RISC OS for Linux and set it up for the port, in
# out/harddisc/HardDisc4. Set ROL_HD4_FILE to use a copy you already have.
#
# Needs out/bin/qemu-arm-riscos and out/rom/RISCOS.IMG (make qemu rom).
#
# The set-up, as the Linux port's own build does it (Unix/LinuxSupport/
# common.mk in RISC_OS_Linux_Source), plus a step of ours:
#  - the filetypes, which IXFS keeps in extended attributes, become ",xxx"
#    name suffixes (xattr2comma.py), so the tree survives tar, git and
#    container layers.
# The CMOS RAM written while unpacking goes to !Boot.Loader.CMOS.
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TB=$(cd "$HERE/.." && pwd)
: "${OUT:=$TB/out/harddisc}"
mkdir -p "$OUT"
OUT=$(readlink -f "$OUT")
case $OUT in *[[:space:]\<\>\|\"\#\*\:\&\^\%\@\\.]*)
  echo "harddisc/fetch.sh: RISC OS can't be given this path: $OUT" >&2; exit 1 ;;
esac
: "${ROL_QEMU:=$TB/out/bin/qemu-arm-riscos}"
: "${ROL_ROM:=$TB/out/rom/RISCOS.IMG}"
read -r name sum url < <(grep -v '^#' "$HERE/HD4")

[ -x "$ROL_QEMU" ] || { echo "harddisc/fetch.sh: no $ROL_QEMU (make qemu)" >&2; exit 1; }
[ -f "$ROL_ROM" ] || { echo "harddisc/fetch.sh: no $ROL_ROM (make rom)" >&2; exit 1; }

mkdir -p "$OUT"
if [ -n "${ROL_HD4_FILE:-}" ]; then
  cp "$ROL_HD4_FILE" "$OUT/$name.part"
elif [ ! -s "$OUT/$name" ]; then
  curl -fsSL --retry 3 -o "$OUT/$name.part" "$url"
fi
[ -f "$OUT/$name.part" ] && mv "$OUT/$name.part" "$OUT/$name"
if ! echo "$sum  $OUT/$name" | sha256sum -c --quiet; then
  rm -f "$OUT/$name"
  echo "harddisc/fetch.sh: $name doesn't match harddisc/HD4 (removed; run again to download it)" >&2
  exit 1
fi

# Unpack: run the utility in RISC OS with the current directory set to an
# empty work directory, then leave RISC OS (exit_group) with status 0.
work=$(mktemp -d "$OUT/unpack.XXXXXX")
trap 'rm -rf "$work"' EXIT
cp "$OUT/$name" "$work/hd4,ffc"
cat > "$work/finish,fff" <<'EOF'
SYS "IXSupport_LinuxSyscall",0,,,,,,,248
EOF
ix=IXFS:\$$(printf '%s' "$work" | tr './' '/.')
RISC_OS_Alias_IXFSBoot="Dir $ix
/$ix.hd4
BASIC -quit $ix.finish" \
  timeout -k 5 300 "$ROL_QEMU" "$ROL_ROM" --abort-on-input --nvram "$work/CMOS" \
  </dev/null >"$work/log" 2>&1 || { cat "$work/log" >&2; echo "harddisc/fetch.sh: unpacking failed" >&2; exit 1; }
hd=$work/HardDisc4
[ -d "$hd/!Boot" ] || { cat "$work/log" >&2; echo "harddisc/fetch.sh: no HardDisc4 after unpacking" >&2; exit 1; }

python3 -I "$HERE/xattr2comma.py" "$hd"

boot=$hd/!Boot
rm -rf "$boot/Choices/Boot"
cp -a "$boot/RO520Hook/Boot" "$boot/Choices/Boot"
# HardDisc4, IXFS:$ (a folder labelled "IXFS:$"), USB discs and SD cards
# on the icon bar: !DiscIcon, from its own Tasks file, so a pinboard that
# replaces Pinboard's file, like ROD's Pinboard2, keeps them. (Not
# Pinboard's AddTinyDir: from RISC OS 5.31-16 that labels IXFS:$ just "$".)
printf '| DiscIcon: HardDisc4, IXFS:$, USB discs and SD cards on the icon bar\nX Filer_Run <IXFS$HardDisc4>.!Boot.Resources.!DiscIcon\n' > "$boot/Choices/Boot/Tasks/DiscIcon,feb"
extras=$HERE/extras
cp -a "$extras/!DiscIcon" "$boot/Resources/"
# !NetWatch: Inet$Resolvers follows Linux's network (a late link or
# Wi-Fi); !DiscIcon's !Run starts it
cp -a "$extras/!NetWatch" "$boot/Resources/"
cp -a "$extras/!ROLUpdate" "$boot/Resources/Configure/"
# root certificates for https (PackMan), and PackMan's sources with ours
cp -a "$extras/!CaCertificates" "$boot/Resources/"
mkdir -p "$boot/Choices/PackMan" && cp "$extras/Choices/PackMan/Sources,fff" "$boot/Choices/PackMan/"
# ARMEABISupport: GCCSDK's 1.05 (1.08 isn't stable on RISC OS on Linux) in
# !System.400.Modules, where PackMan's package puts it (riscos-harddisc's
# place-armeabisupport keeps it there on installed systems; a copy in 500
# or 310 would hide it or be hidden)
rm -f "$boot/Resources/!System/500/Modules/ARMEABISupport,ffa" "$boot/Resources/!System/310/Modules/ARMEABISupport,ffa"
mkdir -p "$boot/Resources/!System/400/Modules" "$hd/TestBed/Modules"
cp "$extras/Modules/ARMEABISupport,ffa" "$boot/Resources/!System/400/Modules/"
cp "$extras/Modules/ARMEABISupport,ffa" "$hd/TestBed/Modules/"
# MbufManager: RISC OS Open's 0.30 (MManagerBlob), which every RISC OS 5 ROM
# has (Omni's LanManFS and Access need it), and MbufRes for its Messages
# (a ROM's ResourceFS has them); PreDesk loads both if the ROM hasn't it
cp "$extras/Modules/MManager,ffa" "$extras/Modules/MbufRes,ffa" "$hd/TestBed/Modules/"
# the licences of what the image contains, and where the source is
# (riscos-linux's licences/; "." becomes "_" in RISC OS names)
lic=${LICENCES:-$HERE/../licences}
if [ -d "$lic" ]; then
  mkdir -p "$hd/Documents/Licences"
  for f in "$lic"/*; do b=$(basename "$f"); cp "$f" "$hd/Documents/Licences/${b//./_},fff"; done
  cp "$lic/SOURCE.txt" "$hd/Documents/Source,fff"
fi
mkdir -p "$hd/Utilities" && cp -a "$extras/!Updates" "$hd/Utilities/"
# the desktop's first mode: the screen's size, until one is saved in Configure
cp "$extras/Monitor,feb" "$boot/Choices/Boot/PreDesk/Configure/Monitor,feb"
# the monitor definition for virtual machines (UTM, QEMU): any size works
# in a window; common sizes up to 5120 x 2880 (64MB, from RISC OS 5.31-19)
mkdir -p "$boot/Resources/Configure/Monitors/Other"
cp "$extras/Monitors/Other/UTM,fff" "$boot/Resources/Configure/Monitors/Other/UTM,fff"
mkdir -p "$boot/Loader"
mv "$work/CMOS" "$boot/Loader/CMOS"

rm -rf "$OUT/HardDisc4"
mv "$hd" "$OUT/HardDisc4"
echo "$OUT/HardDisc4"
