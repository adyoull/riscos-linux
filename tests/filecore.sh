#!/bin/bash
# tests/filecore.sh: FileCore discs end to end, as SCSIFS sees a USB stick
# on RISC OS on Linux. Needs a ROM with SCSISwitch, SCSIFS and SCSIFiler
# (5.31-10 or later) and the SCSILinux module (out/modules).
#
# A blank 64 MB disc image becomes a SCSI disc through SCSILinux
# (SCSILinux$Test). HForm formats it through SCSIFS; then a 200 KB file
# of random bytes and a directory (HForm itself) are copied on and back,
# *Verify checks the disc, and a second boot reads the file again. Every
# copy must come back identical (contents and file types).
#
#   ROL_ROM=... RCD_DIR=... tests/filecore.sh
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TB=$(cd "$HERE/.." && pwd)
W=$(mktemp -d /tmp/rol-filecore-XXXXXX)
trap 'rm -rf "$W"' EXIT
IMG=$W/disc.img
MOD='IXFS:$'$(echo "$TB/out/modules/SCSILinux" | tr / .)
HF='IXFS:$'$(echo "$TB/out/harddisc/HardDisc4/Utilities/Caution/!HForm" | tr / .)
RW='IXFS:$'$(echo "$W" | tr / .)
truncate -s 64M "$IMG"
head -c 200000 /dev/urandom > "$W/rand"
fail=0

run () {	# run Obey file $1 (its lines on stdin)
  cat > "$W/$1,feb"
  timeout 300 "$TB/bin/rol-run" -c "Obey $RW.$1" 2>&1 | tr -d '\r'
}

# 1: format with HForm
run format <<E
Set SCSILinux\$Test $IMG
RMLoad $MOD
Set HForm\$Autoreply Defaults,NOprompts,NOadddefects,Q01:I,Q02:N,BootOpt:N,BigDir:Y,Q03:Y,Q04:N,Q05:Y,Q06:Y,OldOrNewMap:N,Q07:A
Set HForm\$Dir $HF
Run <HForm\$Dir>.!RunImage
Echo HForm status <HForm\$EndStatus>
E
[ $? = 0 ] || fail=1

# 2: copy on and back, verify
run copy <<E
Set SCSILinux\$Test $IMG
RMLoad $MOD
SCSILinux
CDir SCSI::0.\$.Dir1
Copy $RW.rand SCSI::0.\$.Dir1.Rand ~CF~V
Copy $HF SCSI::0.\$.HForm R~CF~V
Copy SCSI::0.\$.Dir1.Rand $RW.back1 ~CF~V
Copy SCSI::0.\$.HForm $RW.hfback R~CF~V
Dir SCSI::0.\$
Verify
Dir $RW
E
[ $? = 0 ] || fail=1

# 3: a fresh boot reads it again
run reread <<E
Set SCSILinux\$Test $IMG
RMLoad $MOD
SCSILinux
Copy SCSI::0.\$.Dir1.Rand $RW.back2 ~CF~V
E
[ $? = 0 ] || fail=1

for f in back1 back2; do
  if cmp -s "$W/rand" "$W/$f"; then echo "ok    $f: identical"; else echo "FAIL  $f differs or is missing"; fail=1; fi
done
python3 - "$TB/out/harddisc/HardDisc4/Utilities/Caution/!HForm" "$W/hfback" <<'E' || fail=1
import os, re, sys
A, B = sys.argv[1], sys.argv[2]
n = bad = 0
for root, ds, fs in os.walk(A):
    for f in fs:
        m = re.match(r'(.*),([0-9a-f]{3})$', f)
        name, ft = (m.group(1), m.group(2)) if m else (f, None)
        rel = os.path.relpath(os.path.join(root, f), A)
        pb = os.path.join(B, os.path.dirname(rel), name)
        try:
            same = open(os.path.join(A, rel), 'rb').read() == open(pb, 'rb').read()
            x = os.getxattr(pb, 'user.RISC_OS.LoadExec')
            t = '%03x' % ((int.from_bytes(x[:4], 'little') >> 8) & 0xfff)
        except OSError:
            same, t = False, None
        n += 1
        if not same or (ft and t != ft):
            bad += 1
            print('FAIL  ', rel)
print('ok    directory: %d files, %d different' % (n, bad) if not bad else 'FAIL  directory: %d of %d different' % (bad, n))
sys.exit(1 if bad or not n else 0)
E
echo "$([ $fail = 0 ] && echo PASS || echo FAIL)"
exit $fail
