#!/bin/bash
# tests/desktop-wheel.sh: the scroll wheel, end to end in the desktop.
# A Filer window on a directory of 200 files; the pointer over it; six
# wheel steps down sent as rol-kvm key numbers &1F1 (as the UTM image's
# rol-fb sends them), which KVMWheel turns into PointerV WheelChange and
# WindowScroll (RISC OS Open's module, which starts its own task with the
# desktop) into scrolling. Passes if the window scrolled (the two
# screenshots differ) and RISC OS saw the six steps.
# Prints PASS or FAIL: REASON. Needs out/harddisc and out/modules.
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TB=$(cd "$HERE/.." && pwd)
w=$(mktemp -d)
trap 'exec 3>&- 2>/dev/null; rm -rf "$w"' EXIT
mkdir "$w/files"
for i in $(seq 100 299); do : > "$w/files/file$i"; done
ix() { echo "IXFS:\$$(echo "$1" | tr '/.' './')"; }   # a Linux path as RISC OS's
cat > "$w/drive" <<B
DIM b% 256
SYS "Wimp_Initialise",310,&4B534154,"WheelTest",0
REM rol-run loads modules after the desktop has started, so WindowScroll's
REM task isn't started by Service_StartWimp (as it is in the UTM image)
SYS "Wimp_StartTask","Desktop_WindowScroll"
SYS "XOS_CLI","Filer_OpenDir $(ix "$w/files") 200 300 600 400 -SmallIcons"
T%=TIME:SYS "OS_Pointer",2 TO ,y0%
REPEAT
SYS "Wimp_PollIdle",0,b%,TIME+50
UNTIL TIME>T%+4500
SYS "OS_Pointer",2 TO ,y1%
F%=OPENOUT("$(ix "$w/alt")"):BPUT#F%,STR\$(y1%-y0%):CLOSE#F%
SYS "Wimp_CloseDown"
B
mkfifo "$w/kvm"
ROL_KVM_CONTROL=$w/kvm "$TB/bin/rol-run" -t 90 -w 60 -g 1024x768 \
  -m "$TB/out/modules/KVMWheel,ffa" -m "$TB/out/modules/WindowScroll,ffa" -s "$w/end.png" \
  -c "BASIC -quit $(ix "$w/drive")" > "$w/out" 2>&1 &
pid=$!
exec 3>"$w/kvm"
sleep 25
# the Filer window: 200,300 to 800,700 in OS units, bottom left origin;
# (the Filer puts it at about x 100-420, y 545-768 in pixels from the top)
echo "mouse 250 650 0" >&3; sleep 1; echo "shot $w/a.png" >&3; sleep 2
for i in 1 2 3 4 5 6; do echo "key 497 1" >&3; echo "key 497 0" >&3; sleep 0.3; done
sleep 3; echo "shot $w/b.png" >&3; sleep 2
wait $pid
alt=$(cat "$w/alt" 2>/dev/null)
if [ ! -s "$w/a.png" ] || [ ! -s "$w/b.png" ]; then
  echo "FAIL: no screenshots"; sed 's/^/      | /' "$w/out" | tail -5
elif [ "$alt" != "-6" ]; then
  echo "FAIL: RISC OS saw wheel movement '$alt' (wanted -6)"
elif cmp -s "$w/a.png" "$w/b.png"; then
  echo "FAIL: the window didn't scroll"
else
  echo "PASS"
fi
