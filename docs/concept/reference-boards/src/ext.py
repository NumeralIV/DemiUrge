"""Builds the T-020 extension sheets (ages-300m, cultures-300m, do-and-dont) and contact-sheet.jpg.
No text is drawn; panel order and meaning are listed in index.md.  Run from the repo root (Pillow)."""
import os
from PIL import Image, ImageDraw
H = os.path.dirname(os.path.abspath(__file__)); O = os.path.join(H, '..'); BG = (11, 15, 23)
def fit(im, w, h):                      # centre-crop to w:h, then resize
    W, Hh = im.size; r = w / h
    if W / Hh > r: nw = int(Hh * r); im = im.crop(((W - nw) // 2, 0, (W - nw) // 2 + nw, Hh))
    else: nh = int(W / r); im = im.crop((0, (Hh - nh) // 2, W, (Hh - nh) // 2 + nh))
    return im.resize((w, h), Image.LANCZOS)
src = lambda n: Image.open(os.path.join(H, n)).convert('RGB')
hero = lambda n: Image.open(os.path.join(O, f'hero-{n}.png')).convert('RGB')

# ages-300m and cultures-300m: panels in one row, each with its own measured 5-swatch palette and hex codes
import numpy as np
from PIL import ImageFont
FONT = ImageFont.truetype(os.path.join(O, '..', '..', '..', 'assets', 'fonts', 'inter', 'Inter-Regular.ttf'), 17)
def pal5(im):
    q = im.resize((256, 192)).quantize(colors=5, method=Image.Quantize.MEDIANCUT)
    cols = q.getpalette()[:15]; cnt = sorted(q.getcolors(), reverse=True)
    c = [tuple(cols[i * 3:i * 3 + 3]) for n, i in cnt]
    return sorted(c, key=lambda t: 0.2126 * t[0] + 0.7152 * t[1] + 0.0722 * t[2])
PALS = {}
def strip(panels, w, h, out):
    s = Image.new('RGB', (len(panels) * w, h + 96), BG); d = ImageDraw.Draw(s)
    for i, (key, im) in enumerate(panels):
        s.paste(fit(im, w - 4, h - 4), (i * w + 2, 2)); cols = pal5(fit(im, w, h)); PALS[key] = ['#%02x%02x%02x' % c for c in cols]
        sw = (w - 4) / 5
        for j, c in enumerate(cols):
            x = int(i * w + 2 + j * sw); d.rectangle([x, h + 2, int(x + sw) - 1, h + 92], fill=c)
            Y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
            d.text((x + 8, h + 66), '#%02x%02x%02x' % c, font=FONT, fill=(20, 20, 20) if Y > 120 else (242, 239, 232))
    s.save(os.path.join(O, out), optimize=True)
ages = [('stone', src('age-0-stone.jpg')), ('bronze', src('age-1-bronze.jpg')), ('iron', hero('buildings-300m')), ('sail', src('age-3-sail.jpg')),
        ('industry', src('age-4-industry.jpg')), ('atomic', src('age-5-atomic.jpg')), ('space', src('age-6-space.jpg'))]
strip(ages, 512, 384, 'ages-300m.png')
strip([('cliff-terrace', src('culture-a.jpg')), ('lagoon-weavers', src('culture-b.jpg')), ('ring-builders', src('culture-c.jpg'))], 640, 480, 'cultures-300m.png')
import json; json.dump(PALS, open('/workspace/rb-ext-pal.json', 'w'), indent=1)

# do-and-dont: 4 pairs (left = want, green bar; right = failure, red bar)
t40 = hero('terrain-40m')
tile = t40.crop((60, 500, 316, 644))                 # 256x144 patch of grass and path
tiled = Image.new('RGB', (1280, 720))
for ty in range(5):
    for tx in range(5): tiled.paste(tile, (tx * 256, ty * 144))
pairs = [(hero('buildings-40m'), src('dont-grit.jpg')), (hero('buildings-40m'), src('dont-toon.jpg')),
         (hero('buildings-300m'), src('dont-beige.jpg')), (t40.crop((0, 360, 1280, 720)), tiled)]
s = Image.new('RGB', (1920, 1124), BG); d = ImageDraw.Draw(s)
for i, (a, b) in enumerate(pairs):
    x0 = (i % 2) * 960; y0 = (i // 2) * 562
    for j, im in enumerate((a, b)):
        x = x0 + j * 478 + 4; s.paste(fit(im, 472, 534), (x, y0 + 4))
        d.rectangle([x, y0 + 542, x + 471, y0 + 553], fill=(88, 170, 96) if j == 0 else (200, 70, 60))
s.save(os.path.join(O, 'do-and-dont.png'), optimize=True)

# contact sheet: nine boards + night, then the extension sheets
names = ['buildings-40m', 'buildings-300m', 'buildings-2km', 'terrain-40m', 'terrain-300m', 'terrain-2km',
         'towns-40m', 'towns-300m', 'towns-2km', 'night-300m', 'ages-300m', 'cultures-300m', 'do-and-dont', 'greyscale-check']
cs = Image.new('RGB', (3 * 960 + 32, 5 * 540 + 48), BG)
for i, n in enumerate(names):
    im = Image.open(os.path.join(O, n + '.png')).convert('RGB'); im.thumbnail((960, 540), Image.LANCZOS)
    x = (i % 3) * 968 + 8 + (960 - im.width) // 2; y = (i // 3) * 548 + 8 + (540 - im.height) // 2; cs.paste(im, (x, y))
cs.save(os.path.join(O, 'contact-sheet.jpg'), quality=86, optimize=True)
print('ok')
