"""Builds the T-020 reference boards' data: detail-crop boxes, the measured palette of each hero and the
greyscale-check samples. Writes boards.js (read by board.html) and greyscale-check.png.
Run from the repo root: python docs/concept/reference-boards/src/boards.py   (numpy, scipy, Pillow)
"""
import json, os
import numpy as np
from PIL import Image, ImageDraw
from scipy.cluster.vq import kmeans2

HERE = os.path.dirname(os.path.abspath(__file__)); OUT = os.path.join(HERE, '..')
# crops: (x, y) top-left of a 320x180 box in the 1280x720 hero, shown at 2x; then what it proves (for index.md)
# samples: category -> (x, y) centre of a 24x24 box in the hero, for the greyscale check
B = {
 'buildings-40m': dict(crops=[(170, 170), (300, 370), (760, 220)],
   samples={'roof (thatch)': (375, 290), 'wall (whitewash)': (200, 450), 'ground (grass)': (1000, 560), 'path': (690, 640), 'vegetation (trees)': (640, 120)}),
 'buildings-300m': dict(crops=[(660, 0), (380, 170), (700, 470)],
   samples={'roof (thatch)': (559, 240), 'roof (shingle)': (316, 489), 'wall (whitewash)': (601, 451), 'ground (lane)': (520, 640), 'vegetation': (1050, 60)}),
 'buildings-2km': dict(crops=[(740, 200), (560, 330), (850, 430)],
   samples={'roof field (town)': (870, 320), 'wall (ring)': (1000, 395), 'ground (field)': (450, 420), 'vegetation (forest)': (220, 560), 'water (sky reflection)': (560, 600)}),
 'terrain-40m': dict(crops=[(120, 470), (240, 320), (840, 470)],
   samples={'grass': (300, 600), 'path': (690, 560), 'rock': (320, 430), 'wheat': (90, 360), 'water': (1000, 640)}),
 'terrain-300m': dict(crops=[(420, 520), (300, 150), (960, 380)],
   samples={'meadow': (500, 450), 'forest': (110, 380), 'field (gold)': (330, 280), 'river': (770, 490), 'beach': (520, 595)}),
 'terrain-2km': dict(crops=[(650, 30), (380, 330), (480, 500)],
   samples={'snow (shade side)': (800, 50), 'rock ridge': (900, 150), 'forest': (300, 520), 'field (gold)': (470, 390), 'meadow': (560, 470), 'river': (690, 470)}),
 'towns-40m': dict(crops=[(380, 400), (680, 370), (940, 230)],
   samples={'roof (thatch)': (170, 450), 'roof (shingle)': (870, 300), 'wall (whitewash)': (210, 590), 'ground (lane)': (520, 520), 'vegetation': (60, 600)}),
 'towns-300m': dict(crops=[(470, 370), (640, 40), (950, 470)],
   samples={'roof (thatch)': (480, 285), 'roof (shingle)': (545, 400), 'wall (ring)': (760, 440), 'ground (lane)': (620, 390), 'vegetation (meadow)': (300, 420), 'field (gold)': (150, 220)}),
 'towns-2km': dict(crops=[(600, 480), (620, 250), (330, 200)],
   samples={'roofs (iron town)': (770, 330), 'roofs (harbour)': (750, 550), 'ground (meadow)': (560, 450), 'field (gold)': (960, 470), 'forest': (250, 470), 'water': (620, 650)}),
   'night-300m': dict(crops=[(500, 290), (680, 20), (380, 420)],
   samples={'lamplit square': (625, 352), 'temple (lit)': (768, 110), 'field': (125, 336), 'forest': (62, 656), 'river': (125, 516)}),
}

def lum(rgb):          # relative luminance Y (sRGB -> linear, Rec. 709 weights), 0..1
    c = rgb / 255.0; c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return c @ np.array([0.2126, 0.7152, 0.0722])

def palette(img, k=10, seed=7):
    a = np.asarray(img.resize((320, 180), Image.LANCZOS)).reshape(-1, 3).astype(float)
    np.random.seed(seed)
    cent, lab = kmeans2(a, k, minit='++', seed=seed)
    out = []
    for i in range(k):
        m = a[lab == i]
        if len(m) < 30: continue
        med = np.median(m, 0); out.append((float(lum(med)), int(len(m)), '#%02x%02x%02x' % tuple(int(v) for v in med)))
    out.sort()
    return [dict(hex=h, share=round(n / len(a) * 100, 1), Y=round(y, 3)) for y, n, h in out]

data, grey_rows = {}, []
for name, d in B.items():
    im = Image.open(os.path.join(OUT, f'hero-{name}.png')).convert('RGB'); a = np.asarray(im).astype(float)
    samp = {}
    for cat, (x, y) in d['samples'].items():
        p = np.median(a[y - 12:y + 12, x - 12:x + 12].reshape(-1, 3), 0)
        samp[cat] = dict(hex='#%02x%02x%02x' % tuple(int(v) for v in p), Y=round(float(lum(p)), 3))
    data[name] = dict(crops=d['crops'], palette=palette(im), samples=samp)
    g = im.convert('L').convert('RGB'); dr = ImageDraw.Draw(g)
    for cat, (x, y) in d['samples'].items(): dr.rectangle([x - 12, y - 12, x + 12, y + 12], outline=(255, 60, 60), width=3)
    for (x, y) in d['crops']: dr.rectangle([x, y, x + 320, y + 180], outline=(60, 200, 255), width=2)
    grey_rows.append(g.resize((640, 360), Image.LANCZOS))
open(os.path.join(HERE, 'boards.js'), 'w').write('const BOARDS = ' + json.dumps(data, indent=1) + ';\n')
sheet = Image.new('RGB', (1920, 1080), (11, 15, 23))
for i, g in enumerate(grey_rows[:9]): sheet.paste(g, ((i % 3) * 640, (i // 3) * 360))
sheet.save(os.path.join(OUT, 'greyscale-check.png'), optimize=True)
json.dump(data, open('/workspace/rb-data.json', 'w'), indent=1)
print('ok')
