#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
"""rol-sndcap.py FIFO OUT.wav: save the sound LinuxAudio sends to FIFO as a
WAV file (16-bit stereo, at the first buffer's sample rate; buffers at
another rate are resampled to it, nearest sample). LinuxAudio's stream:
each buffer is a header (b"RSND", rate in Hz, byte count: three
little-endian 32-bit words) and then its samples (signed 16-bit, left then
right). It reads in real time, as a sound card would (LinuxAudio keeps
about 50 ms ahead of its reader), so the WAV's time line is the program's.
Ends when the writer closes the FIFO; the WAV header is rewritten
after every buffer, so the file is good even if this is killed."""
import struct, sys, time

fifo, out = sys.argv[1], sys.argv[2]
w = open(out, 'wb')
rate = None
frames = 0

def header():
    r = rate or 48000
    w.seek(0)
    w.write(b'RIFF' + struct.pack('<I', 36 + frames * 4) + b'WAVEfmt ' +
            struct.pack('<IHHIIHH', 16, 1, 2, r, r * 4, 4, 16) +
            b'data' + struct.pack('<I', frames * 4))
    w.seek(0, 2)

header()
w.flush()
t0 = None
with open(fifo, 'rb') as f:
    while True:
        h = f.read(12)
        if len(h) < 12:
            break
        magic, r, n = struct.unpack('<4sII', h)
        if magic != b'RSND':
            sys.exit('rol-sndcap: bad stream')
        data = f.read(n)
        if len(data) < n:
            break
        if rate is None:
            rate = r
        if r != rate and r:
            src = len(data) // 4
            dst = src * rate // r
            data = b''.join(data[4 * (i * r // rate):4 * (i * r // rate) + 4] for i in range(dst))
        w.write(data)
        frames += len(data) // 4
        if t0 is None:
            t0 = time.monotonic()
        ahead = t0 + frames / rate - time.monotonic()
        if ahead > 0:
            time.sleep(ahead)
        header()
        w.flush()
w.close()
