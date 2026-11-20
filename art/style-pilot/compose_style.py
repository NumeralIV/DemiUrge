# DEMIURGE style pilot - one-glance comparison sheets (system Python + Pillow only).
#   python art/style-pilot/compose_style.py [render_dir]
# -> shots/style-<s300|s80|c15>.png   columns CURRENT | PHOTOREAL | S1 | S2 | P; top row as rendered, bottom row
#                                     with the strategy grade (grade() below). Missing looks get a labelled blank.
# -> shots/style-swatches.png         the processed diffuse (D) of every material in every look, 2 x 2 tiled.
import json
import os
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'shots')
sys.path.insert(0, os.path.join(ROOT, 'art', 'textures-pilot'))
from tile_proof import font  # noqa: E402

LOOKS = [('CUR', 'CURRENT', 'procedural sets'), ('PHOTO', 'PHOTOREAL', 'Flow library as generated'),
         ('S1', 'S1', 'Flow, hand-painted prompt'), ('S2', 'S2', 'Flow, stylised-realism prompt'),
         ('P', 'P', 'photoreal processed down')]
VIEWS = {'s300': 'strategy camera, 300 m oblique', 's80': 'strategy camera, 80 m oblique',
         'c15': 'close, 15 m'}
MATS = ['Rubble', 'Thatch', 'Mudbrick', 'Plaster', 'Terracotta', 'Planks', 'Path', 'Grass']
PW, PH = 640, 400          # panel size on the sheet


def grade(im):
    """Light strategy-game grade: local contrast (clarity) from a wide unsharp mask, +14 % saturation, shadows
    lifted and cleaned (toe raised, a touch of cool removed from the darkest tones), highlights eased."""
    im = im.filter(ImageFilter.UnsharpMask(radius=30, percent=28, threshold=0))
    im = ImageEnhance.Color(im).enhance(1.14)
    def curve(v, lift):
        x = v / 255.0
        y = x + lift * (1 - x) ** 4 - 0.04 * x ** 6       # raise the toe, ease the shoulder
        return max(0, min(255, int(round(y * 255))))
    lut = [curve(v, 0.050) for v in range(256)] + [curve(v, 0.045) for v in range(256)] + \
          [curve(v, 0.030) for v in range(256)]
    return im.point(lut)


def tag(img, text, xy, size=22, fill=(0, 0, 0)):
    d = ImageDraw.Draw(img)
    f = font(size)
    bb = d.textbbox(xy, text, font=f)
    d.rectangle((bb[0] - 6, bb[1] - 4, bb[2] + 6, bb[3] + 4), fill=fill)
    d.text(xy, text, font=f, fill=(255, 255, 255))


def inset(im):
    """3x crop of the centre (the village) pasted top-right, for the 300 m view."""
    w, h = im.size
    cw, ch = w // 6, h // 6
    crop = im.crop((w // 2 - cw // 2, h // 2 - ch // 2, w // 2 + cw // 2, h // 2 + ch // 2))
    crop = crop.resize((cw * 2, ch * 2), Image.LANCZOS)
    im = im.copy()
    x, y = w - crop.width - 6, h - crop.height - 6
    ImageDraw.Draw(im).rectangle((x - 2, y - 2, x + crop.width + 1, y + crop.height + 1), fill=(255, 255, 255))
    im.paste(crop, (x, y))
    return im


def blank(text):
    im = Image.new('RGB', (PW, PH), (44, 44, 44))
    d = ImageDraw.Draw(im)
    f = font(20)
    for i, line in enumerate(text.split('\n')):
        bb = d.textbbox((0, 0), line, font=f)
        d.text(((PW - bb[2]) // 2, PH // 2 - 30 + i * 28), line, font=f, fill=(190, 190, 190))
    return im


def sheet(view, rd):
    gap, top, lab = 10, 52, 30
    cols = len(LOOKS)
    W = cols * PW + (cols + 1) * gap + 90
    H = top + 2 * (PH + lab) + 3 * gap
    s = Image.new('RGB', (W, H), (18, 18, 18))
    tag(s, 'DEMIURGE style pilot  -  shrine + Bronze-age block  -  %s  -  golden hour' % VIEWS[view], (12, 12), 24)
    found = 0
    for ci, (key, name, sub) in enumerate(LOOKS):
        x = 90 + gap + ci * (PW + gap)
        p = os.path.join(rd, '%s-%s.png' % (view, key))
        if os.path.exists(p):
            im = Image.open(p).convert('RGB')
            found += 1
            if view == 's300':
                im = inset(im)
            raw, gr = im.resize((PW, PH), Image.LANCZOS), grade(im).resize((PW, PH), Image.LANCZOS)
        else:
            raw = gr = blank('%s not rendered' % name)
        for ri, pic in enumerate((raw, gr)):
            y = top + ri * (PH + lab + gap) + lab
            s.paste(pic, (x, y))
            if ri == 0:
                d = ImageDraw.Draw(s)
                d.text((x + 2, top + 2), name, font=font(22), fill=(255, 220, 140))
                d.text((x + 8 + d.textbbox((0, 0), name, font=font(22))[2], top + 6), sub, font=font(16),
                       fill=(200, 200, 200))
    d = ImageDraw.Draw(s)
    for ri, t in enumerate(('as\nrendered', 'strategy\ngrade')):
        y = top + ri * (PH + lab + gap) + lab + PH // 2 - 24
        d.multiline_text((10, y), t, font=font(18), fill=(255, 220, 140), spacing=6)
    out = os.path.join(SHOTS, 'style-%s.png' % view)
    s.save(out, optimize=True)
    print('SHOT', out, found, 'looks')


def swatches():
    cell, gap = 200, 6
    W = 150 + len(MATS) * (cell + gap)
    H = 40 + len(LOOKS) * (cell + gap)
    s = Image.new('RGB', (W, H), (18, 18, 18))
    d = ImageDraw.Draw(s)
    for mi, m in enumerate(MATS):
        d.text((150 + mi * (cell + gap) + 4, 10), m, font=font(18), fill=(255, 220, 140))
    for li, (key, name, sub) in enumerate(LOOKS):
        y = 40 + li * (cell + gap)
        d.text((8, y + cell // 2 - 12), name, font=font(20), fill=(255, 220, 140))
        for mi, m in enumerate(MATS):
            x = 150 + mi * (cell + gap)
            p = cur_d(m) if key == 'CUR' else os.path.join(HERE, 'maps', key, '1k', 'T_Style%s_%s_D.png' % (key, m))
            if p and os.path.exists(p):
                t = Image.open(p).convert('RGB').resize((cell // 2, cell // 2), Image.LANCZOS)
                for i in range(2):
                    for j in range(2):
                        s.paste(t, (x + i * cell // 2, y + j * cell // 2))
            else:
                s.paste(Image.new('RGB', (cell, cell), (44, 44, 44)), (x, y))
                d.text((x + 10, y + cell // 2 - 8), 'n/a' if key == 'CUR' else 'pending', font=font(16),
                       fill=(170, 170, 170))
    out = os.path.join(SHOTS, 'style-swatches.png')
    s.save(out, optimize=True)
    print('SHOT', out)


CROP = (0.266, 0.160, 0.734, 0.728)     # shrine front in the c15 view, as fractions of the frame (4:3 at 16:10)


def crop_sheet(rd, title):
    """Shrine wall crop of the close view for every look, from renders/hi/ (1920 x 1200) when present."""
    cw, ch, gap, top = 480, 360, 10, 44
    W = len(LOOKS) * (cw + gap) + gap
    H = top + 2 * (ch + gap) + 4
    s = Image.new('RGB', (W, H), (18, 18, 18))
    tag(s, title, (12, 10), 20)
    for ci, (key, name, sub) in enumerate(LOOKS):
        x = gap + ci * (cw + gap)
        p = os.path.join(rd, 'hi', 'c15-%s.png' % key)
        if not os.path.exists(p):
            p = os.path.join(rd, 'c15-%s.png' % key)
        if os.path.exists(p):
            im = Image.open(p).convert('RGB')
            w, h = im.size
            box = (int(CROP[0] * w), int(CROP[1] * h), int(CROP[2] * w), int(CROP[3] * h))
            raw = im.crop(box).resize((cw, ch), Image.LANCZOS)
            gr = grade(im).crop(box).resize((cw, ch), Image.LANCZOS)
        else:
            raw = gr = Image.new('RGB', (cw, ch), (44, 44, 44))
        for ri, pic in enumerate((raw, gr)):
            y = top + ri * (ch + gap)
            s.paste(pic, (x, y))
            d = ImageDraw.Draw(s)
            lab = name if ri == 0 else name + ' + grade'
            d.text((x + 7, y + 5), lab, font=font(18), fill=(0, 0, 0))
            d.text((x + 6, y + 4), lab, font=font(18), fill=(255, 220, 140))
    out = os.path.join(SHOTS, 'style-c15-crop.png')
    s.save(out, optimize=True)
    print('SHOT', out)


def cur_d(m):
    rel = {'Rubble': 'art/textures/rubble/T_Rubble_BC.png', 'Thatch': 'art/age1/textures/T_Age1_Thatch_D.png',
           'Mudbrick': 'art/age1/textures/T_Age1_Mudbrick_D.png', 'Plaster': 'art/age1/textures/T_Age1_Render_D.png',
           'Planks': 'art/textures/oak/T_Oak_BC.png', 'Path': 'art/age1/textures/T_Age1_Soil_D.png',
           'Grass': 'art/textures/turf/T_Turf_BC.png'}.get(m)
    return os.path.join(ROOT, rel) if rel else None


def main():
    rd = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'renders')
    for v in VIEWS:
        sheet(v, rd)
    swatches()
    crop_sheet(rd, 'DEMIURGE style pilot  -  shrine wall at 15 m, crop (1920 x 1200 render)  -  top: as rendered, '
               'bottom: strategy grade')


if __name__ == '__main__':
    main()
