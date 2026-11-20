# Independent check: mean brightness of wall / roof / plinth regions in render panels (system Python + Pillow).
#   python art/textures-pilot/check/measure_ab.py <dir> [variant ...]
# Regions are given for 960 x 720 panels and scaled to the panel size. Values: mean of the display image
# converted back with the sRGB curve (relative only; AgX makes it non-linear in scene light).
import os
import sys

from PIL import Image

REG = {'v40': {'wall_front': (490, 390, 580, 500), 'roof_front': (330, 180, 470, 240), 'plinth_side': (600, 520, 760, 580)},
       'close8': {'wall_front': (440, 330, 600, 520), 'roof': (100, 120, 500, 200), 'wall_right': (720, 320, 820, 520)}}
LUT = [(v / 255 / 12.92 if v / 255 <= 0.04045 else ((v / 255 + 0.055) / 1.055) ** 2.4) for v in range(256)]


def lum(path, box):
    im = Image.open(path).convert('RGB')
    s = im.width / 960.0
    c = im.crop(tuple(int(v * s) for v in box))
    r, g, b = c.split()
    n = c.width * c.height
    tot = 0.0
    for ch, w in ((r, 0.2126), (g, 0.7152), (b, 0.0722)):
        h = ch.histogram()
        tot += w * sum(LUT[i] * h[i] for i in range(256))
    return tot / n


def main():
    d = sys.argv[1]
    variants = sys.argv[2:] or ['after', 'after_flatN', 'after_procN', 'after_procR']
    for v, regs in REG.items():
        for light in ('golden', 'overcast'):
            pb = os.path.join(d, '%s-%s-before.png' % (v, light))
            if not os.path.exists(pb):
                continue
            for k, box in regs.items():
                b = lum(pb, box)
                row = ['%-7s %-8s %-11s before %.4f' % (v, light, k, b)]
                for var in variants:
                    p = os.path.join(d, '%s-%s-%s.png' % (v, light, var))
                    if os.path.exists(p):
                        row.append('%s %.2f' % (var, lum(p, box) / b))
                print('  '.join(row))


main()
