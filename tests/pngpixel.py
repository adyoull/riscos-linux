#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
"""pngpixel.py FILE X Y...: print the colour (RRGGBB) of each pixel X,Y
(from the top left) of an 8-bit RGB or RGBA PNG with no interlacing,
such as rol-kvm writes. Also prints the size first, as WxH."""
import struct
import sys
import zlib


def main(path, coords):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat = 8, b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and ctype in (2, 6) and inter == 0, "unsupported PNG"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    print(f"{w}x{h}")
    for x, y in coords:
        p = rows[y][x * bpp:x * bpp + 3]
        print(f"{p[0]:02x}{p[1]:02x}{p[2]:02x}")


if __name__ == "__main__":
    a = [int(v) for v in sys.argv[2:]]
    main(sys.argv[1], list(zip(a[0::2], a[1::2])))
