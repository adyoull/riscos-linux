#!/bin/bash
# tests/sound-out.sh: sound out of RISC OS for Linux. Runs
# tests/programs/soundout.bas with the sound saved as a WAV file
# (ROL_SOUND_WAV) and checks it: a 440 Hz note in the left channel only,
# about 0.5 s long, then a second of left -4096, right +4096 (a linear
# handler's), then a scheduled note in both channels. Prints PASS or what
# went wrong.
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TB=$(cd "$HERE/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
out=$(ROL_SOUND_WAV=$tmp/out.wav "$TB/bin/rol-run" -t 90 \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.soundout/bas' 2>&1)
rc=$?
if [ $rc != 0 ] || ! grep -q '^rates 8$' <<<"$out" || ! grep -q '(playing)' <<<"$out"; then
  echo "rol-run status $rc: $(tail -4 <<<"$out" | tr '\n' '|')"; exit 1
fi
python3 -I - "$tmp/out.wav" <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1])
rate, n = w.getframerate(), w.getnframes()
raw = w.readframes(n)
s = struct.unpack('<%dh' % (2 * n), raw)
L, R = s[0::2], s[1::2]
blk = rate // 10
kinds = []
for i in range(0, n - blk, blk):
    l, r = L[i:i + blk], R[i:i + blk]
    rl = math.sqrt(sum(x * x for x in l) / blk); rr = math.sqrt(sum(x * x for x in r) / blk)
    ml, mr = sum(l) / blk, sum(r) / blk
    if rl < 50 and rr < 50: kinds.append('.')
    elif rr < 50 and abs(ml) < 500: kinds.append('T')            # tone, left only
    elif abs(ml + 4096) < 2 and abs(mr - 4096) < 2: kinds.append('D')
    elif rl > 500 and rr > 500 and abs(ml) < 500 and abs(mr) < 500: kinds.append('C')
    else: kinds.append('?')
k = ''.join(kinds)
problems = []
t0 = k.find('T')
if t0 < 0: problems.append('no left-only note')
else:
    seg = L[t0 * blk + blk:t0 * blk + 3 * blk]          # 0.2 s inside the note
    def power(f):
        c = s_ = 0.0
        for j, x in enumerate(seg):
            a = 2 * math.pi * f * j / rate; c += x * math.cos(a); s_ += x * math.sin(a)
        return c * c + s_ * s_
    p = {f: power(f) for f in (220, 330, 440, 550, 660)}
    if max(p, key=p.get) != 440: problems.append('note not 440 Hz: %r' % sorted(p, key=p.get)[-1])
    nt = k.count('T')
    if not 4 <= nt <= 7: problems.append('note %d tenths long' % nt)
nd = k.count('D')
if not 9 <= nd <= 11: problems.append('linear handler output %d tenths long' % nd)
d1 = k.rfind('D')
if 'C' not in k[d1:]: problems.append('no scheduled note after the handler')
if k.count('?') > 4: problems.append('unexpected sound')   # (the starts and ends of the handler's sound and the last note)
print('PASS' if not problems else '; '.join(problems) + ' [' + k + ']')
PY
