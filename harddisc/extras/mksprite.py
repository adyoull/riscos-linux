#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
"""mksprite.py OUT NAME SIZE: a RISC OS sprite file with one 32bpp sprite
(180 dpi, 1bpp mask) of a mouse with its wheel and up/down arrows, drawn
here: the scroll wheel plug-in's icon."""
import struct, sys
from PIL import Image, ImageDraw

def draw(n):
    s = 4  # supersample
    im = Image.new('RGBA', (n * s, n * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    N = n * s
    # the mouse
    d.rounded_rectangle([N*0.22, N*0.18, N*0.70, N*0.94], radius=N*0.22, fill=(236, 236, 236, 255), outline=(60, 60, 60, 255), width=max(1, N//40))
    d.line([N*0.22, N*0.48, N*0.70, N*0.48], fill=(60, 60, 60, 255), width=max(1, N//50))
    d.line([N*0.46, N*0.18, N*0.46, N*0.48], fill=(60, 60, 60, 255), width=max(1, N//50))
    # the wheel
    d.rounded_rectangle([N*0.41, N*0.24, N*0.51, N*0.42], radius=N*0.04, fill=(40, 90, 200, 255))
    # arrows
    c = (200, 40, 40, 255)
    d.polygon([(N*0.84, N*0.06), (N*0.74, N*0.22), (N*0.94, N*0.22)], fill=c)
    d.rectangle([N*0.815, N*0.22, N*0.865, N*0.40], fill=c)
    d.polygon([(N*0.84, N*0.70), (N*0.74, N*0.54), (N*0.94, N*0.54)], fill=c)
    d.rectangle([N*0.815, N*0.36, N*0.865, N*0.54], fill=c)
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
