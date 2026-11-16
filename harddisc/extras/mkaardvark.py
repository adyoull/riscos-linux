#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
"""mkaardvark.py DIR [SHEET.png]: !AArdvarkLoader's icon, drawn here, as
RISC OS sprite files DIR/!Sprites and DIR/!Sprites22 (34 pixels, 90 dpi)
and DIR/!Sprites11 (68 pixels, 180 dpi): an aardvark, with the small
version for the Filer's small icons. A sprite's name is 12 characters at
most, so the Filer's is the application's name cut to that:
"!aardvarkloa" (and "sm!aardvarkl"). 32 bits a pixel with a 1 bit mask,
as mkbattery.py's."""
import struct, sys
from PIL import Image, ImageDraw

def draw(n):
    s = 8
    N = n * s
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    line = (70, 45, 30, 255)
    body = (196, 150, 110, 255)
    shade = (166, 120, 84, 255)
    pink = (232, 176, 160, 255)
    lw = max(s, N // 42)
    def E(x0, y0, x1, y1, **k): d.ellipse([N * x0, N * y0, N * x1, N * y1], **k)
    def P(pts, **k): d.polygon([(N * x, N * y) for x, y in pts], **k)
    # the tail: thick at the rump, tapering to the ground at the left
    P([(0.30, 0.50), (0.02, 0.80), (0.07, 0.84), (0.36, 0.66)], fill=body, outline=line, width=lw)
    # far legs
    for x in (0.34, 0.62):
        d.rounded_rectangle([N * x, N * 0.62, N * (x + 0.10), N * 0.90], radius=N * 0.03, fill=shade, outline=line, width=lw)
    # the body: a high arched back
    E(0.20, 0.30, 0.76, 0.76, fill=body, outline=line, width=lw)
    # near legs
    for x in (0.26, 0.54):
        d.rounded_rectangle([N * x, N * 0.62, N * (x + 0.11), N * 0.92], radius=N * 0.03, fill=body, outline=line, width=lw)
    # neck and head, then the long snout down to the ground
    P([(0.62, 0.36), (0.84, 0.30), (0.90, 0.44), (0.70, 0.60)], fill=body, outline=line, width=lw)
    P([(0.80, 0.34), (0.90, 0.40), (0.985, 0.74), (0.93, 0.78), (0.78, 0.50)], fill=body, outline=line, width=lw)
    E(0.915, 0.71, 0.995, 0.80, fill=pink, outline=line, width=lw)
    # hide the joins inside the outline
    E(0.24, 0.34, 0.72, 0.72, fill=body)
    P([(0.66, 0.42), (0.83, 0.345), (0.875, 0.44), (0.72, 0.56)], fill=body)
    P([(0.815, 0.39), (0.885, 0.43), (0.955, 0.72), (0.93, 0.735), (0.80, 0.49)], fill=body)
    # the ears: tall, like a rabbit's
    P([(0.70, 0.36), (0.66, 0.06), (0.76, 0.10), (0.79, 0.34)], fill=pink, outline=line, width=lw)
    P([(0.78, 0.34), (0.80, 0.04), (0.89, 0.10), (0.87, 0.33)], fill=body, outline=line, width=lw)
    # the eye
    E(0.835, 0.405, 0.875, 0.445, fill=line)
    return im.resize((n, n), Image.LANCZOS)

def sprite(name, im, dpi):
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
    mode = (6 << 27) | (dpi << 14) | (dpi << 1) | 1
    hdr = 44
    size = hdr + len(img) + len(mask)
    return struct.pack('<I12s7I', size, name.encode().ljust(12, b'\0'), w - 1, h - 1, 0, 31,
                       hdr, hdr + len(img), mode) + img + mask

def file(path, n, dpi):
    sprs = sprite('!aardvarkloa', draw(n), dpi) + sprite('sm!aardvarkl', draw(n // 2), dpi)
    open(path, 'wb').write(struct.pack('<3I', 2, 16, 16 + len(sprs)) + sprs)

out = sys.argv[1]
file(out + '/!Sprites,ff9', 34, 90)
file(out + '/!Sprites22,ff9', 34, 90)
file(out + '/!Sprites11,ff9', 68, 180)
if len(sys.argv) > 2:  # a picture, to look at
    sheet = Image.new('RGBA', (560, 280), (221, 221, 221, 255))
    x = 8
    for n in (272, 68, 34, 17):
        im = draw(n)
        sheet.paste(im, (x, 4), im)
        x += n + 12
    sheet.save(sys.argv[2])
