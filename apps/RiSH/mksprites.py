#!/usr/bin/env python3
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
"""mksprites.py OUT DPI: RiSH's !Sprites file (32bpp, 1bpp masks) for
screens of DPI dots per inch (90 or 180): "!rish" and "sm!rish", a
terminal window, drawn here. mksprites.py preview OUT.png shows them."""
import struct, sys
from PIL import Image, ImageDraw

def draw(n):
    """A terminal window: a RISC OS title bar, a dark screen with a green
    prompt, a block cursor and two lines of output."""
    s = 8
    N = n * s
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    small = n < 30
    x0, y0, x1, y1 = N*0.03, N*0.10, N*0.97, N*0.92
    r = N*0.07
    edge = (40, 44, 52, 255)
    # shadow, frame, screen
    d.rounded_rectangle([x0 + N*0.02, y0 + N*0.03, x1, y1 + N*0.02], radius=r, fill=(0, 0, 0, 70))
    d.rounded_rectangle([x0, y0, x1 - N*0.02, y1 - N*0.01], radius=r, fill=edge)
    ix0, ix1 = x0 + N*0.035, x1 - N*0.055
    bar = y0 + N*(0.19 if small else 0.17)
    # title bar: RISC OS's cream, with its close box
    d.rounded_rectangle([ix0, y0 + N*0.035, ix1, bar + r], radius=r*0.6, fill=(238, 232, 176, 255))
    d.rectangle([ix0, bar - N*0.01, ix1, bar + r], fill=edge)
    d.rectangle([ix0, bar, ix1, y1 - N*0.045], fill=(14, 18, 24, 255))
    if not small:
        c = y0 + N*0.035 + (bar - y0 - N*0.035) / 2
        d.ellipse([ix0 + N*0.035, c - N*0.035, ix0 + N*0.105, c + N*0.035], fill=(200, 80, 70, 255))
    g = (70, 230, 120, 255)
    w = max(1, int(N * (0.085 if small else 0.065)))
    top = bar + N*(0.10 if small else 0.09)
    h = N*(0.26 if small else 0.20)
    px = ix0 + N*0.07
    # the prompt ">" and a block cursor
    d.line([(px, top), (px + h*0.62, top + h/2), (px, top + h)], fill=g, width=w, joint='curve')
    d.rectangle([px + h*0.95, top + h*0.05, px + h*0.95 + N*0.13, top + h*0.95], fill=(235, 235, 235, 255))
    if not small:
        # output lines below
        yy = top + h + N*0.085
        d.rectangle([px, yy, px + N*0.52, yy + N*0.045], fill=(110, 150, 210, 255))
        d.rectangle([px, yy + N*0.105, px + N*0.34, yy + N*0.15], fill=(150, 156, 166, 255))
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

if sys.argv[1] == 'preview':
    sheet = Image.new('RGBA', (560, 300), (221, 221, 221, 255))
    x = 10
    for n, z in ((68, 4), (34, 4), (34, 1), (17, 4), (17, 1)):
        i = draw(n).resize((n * z, n * z), Image.NEAREST)
        sheet.paste(i, (x, 10), i)
        x += n * z + 12
    sheet.save(sys.argv[2])
    sys.exit(0)
out, dpi = sys.argv[1], int(sys.argv[2])
k = dpi // 90
sprs = sprite('!rish', draw(34 * k), dpi) + sprite('sm!rish', draw(17 * k), dpi)
open(out, 'wb').write(struct.pack('<3I', 2, 16, 16 + len(sprs)) + sprs)
