# Makes src/jpegdata.h: the 640x480 test photo for gfx.jpeg (needs Pillow).
from PIL import Image, ImageDraw, ImageFilter
import random, math
random.seed(7)
W, H = 640, 480
im = Image.new('RGB', (W, H)); px = im.load()
for y in range(H):
    for x in range(W):
        if y < H * 0.55:
            r = int(90 + 60 * y / H); g = int(140 + 50 * y / H); b = int(220 - 30 * y / H)
        else:
            t = (y - H * 0.55) / (H * 0.45)
            r = int(70 + 50 * t + 20 * math.sin(x / 37)); g = int(110 + 40 * t + 15 * math.cos(x / 23)); b = int(50 + 20 * t)
        n = random.randint(-14, 14)
        px[x, y] = tuple(max(0, min(255, c + n)) for c in (r, g, b))
d = ImageDraw.Draw(im)
for i in range(40):
    x = random.randint(0, W); y = random.randint(int(H * 0.5), H); s = random.randint(10, 60)
    d.ellipse([x - s, y - s * 2, x + s, y], fill=(random.randint(20, 60), random.randint(70, 130), random.randint(20, 50)))
d.ellipse([480, 40, 560, 120], fill=(255, 240, 180))
for i in range(12):
    x = random.randint(0, W - 120); y = random.randint(int(H * 0.45), int(H * 0.7))
    d.rectangle([x, y, x + random.randint(30, 110), y + random.randint(30, 80)], fill=(random.randint(120, 200), random.randint(90, 150), random.randint(80, 130)))
im = im.filter(ImageFilter.GaussianBlur(0.7))
im.save('robench.jpg', 'JPEG', quality=85, progressive=False, optimize=False, subsampling=2)
data = open('robench.jpg', 'rb').read()
with open('jpegdata.h', 'w') as f:
    f.write('/* 640x480 baseline JPEG (quality 85, 4:2:0) made by mkjpeg.py */\n#define JPEG_W %d\n#define JPEG_H %d\nstatic const unsigned char jpeg_data[%d] __attribute__ ((aligned (4))) = {\n' % (W, H, len(data)))
    for i in range(0, len(data), 16):
        f.write('  ' + ','.join(str(b) for b in data[i:i + 16]) + ',\n')
    f.write('};\n')
