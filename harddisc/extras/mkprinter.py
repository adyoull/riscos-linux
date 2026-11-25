#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
"""mkprinter.py OUT NAME SIZE: a RISC OS sprite file with one 32bpp sprite
(180 dpi, 1bpp mask) of a printer with a page coming out, drawn here: the
Linux printing plug-in's icon (the sprite file format as mksprite.py)."""
import struct, sys
from PIL import Image, ImageDraw
sys.path.insert(0, __import__('os').path.dirname(__file__))

def draw(n):
    s = 4
    N = n * s
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    w = max(1, N // 40)
    # the page going in, at the back
    d.rectangle([N*0.28, N*0.08, N*0.72, N*0.40], fill=(250, 250, 250, 255), outline=(80, 80, 80, 255), width=w)
    # the printer
    d.rounded_rectangle([N*0.08, N*0.34, N*0.92, N*0.72], radius=N*0.06, fill=(200, 200, 205, 255), outline=(60, 60, 60, 255), width=w)
    d.rectangle([N*0.16, N*0.42, N*0.30, N*0.48], fill=(60, 170, 60, 255))
    # the page coming out, printed
    d.rectangle([N*0.22, N*0.62, N*0.78, N*0.94], fill=(255, 255, 255, 255), outline=(80, 80, 80, 255), width=w)
    for i, c in enumerate([(40, 90, 200, 255), (200, 40, 40, 255), (40, 40, 40, 255)]):
        y = N * (0.70 + i * 0.07)
        d.rectangle([N*0.30, y, N*0.70 - i * N*0.08, y + N*0.03], fill=c)
    return im.resize((n, n), Image.LANCZOS)

def sprite(name, im):
    w, h = im.size
    px = im.load()
    img = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            img += bytes((r, g, b, 0))
    mrow = (w + 31) // 32 * 4
    mask = bytearray()
    for y in range(h):
        row = bytearray(mrow)
        for x in range(w):
            if px[x, y][3] >= 128:
                row[x // 8] |= 1 << (x % 8)
        mask += row
    mode = (6 << 27) | (180 << 14) | (180 << 1) | 1
    hdr = 44
    size = hdr + len(img) + len(mask)
    return struct.pack('<I12s7I', size, name.encode().ljust(12, b'\0'), w - 1, h - 1, 0, 31,
                       hdr, hdr + len(img), mode) + img + mask

out, name, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
spr = sprite(name, draw(n))
open(out, 'wb').write(struct.pack('<3I', 1, 16, 16 + len(spr)) + spr)
