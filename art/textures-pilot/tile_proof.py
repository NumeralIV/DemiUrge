# DEMIURGE texture pilot - 4 x 4 tiling proofs (system Python + Pillow).
#   python art/textures-pilot/tile_proof.py [Name ...]          (default: the four pilot sets)
# shots/texpilot-tile-<Name>-D.png     raw Flow image tiled 4 x 4 (left) vs the processed diffuse tiled 4 x 4 (right)
# shots/texpilot-tile-<Name>-maps.png  every map of the set tiled 4 x 4: D, H, N, R, AO, ORM
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(HERE, 'out')
SHOTS = os.path.join(ROOT, 'shots')
SRC = {'StoneA': 'flow/stone-a.jpg', 'StoneB': 'flow/stone-b.jpg', 'ThatchA': 'flow/thatch-a.jpg',
       'ThatchB': 'flow/thatch-b.jpg'}


def font(sz):
    for f in ('arialbd.ttf', 'arial.ttf', 'DejaVuSans-Bold.ttf'):
        try:
            return ImageFont.truetype(f, sz)
        except OSError:
            pass
    return ImageFont.load_default()


def tile4(im, cell):
    im = im.convert('RGB').resize((cell, cell), Image.LANCZOS)
    t = Image.new('RGB', (cell * 4, cell * 4))
    for i in range(4):
        for j in range(4):
            t.paste(im, (i * cell, j * cell))
    return t


def label(img, text, xy=(14, 10)):
    d = ImageDraw.Draw(img)
    f = font(30)
    x, y = xy
    bb = d.textbbox((x, y), text, font=f)
    d.rectangle((bb[0] - 8, bb[1] - 6, bb[2] + 8, bb[3] + 6), fill=(0, 0, 0))
    d.text((x, y), text, font=f, fill=(255, 255, 255))


def main():
    names = sys.argv[1:] or list(SRC)
    os.makedirs(SHOTS, exist_ok=True)
    for n in names:
        raw = Image.open(os.path.join(HERE, SRC[n]))
        d = Image.open(os.path.join(OUT, 'T_Shrine_%s_D.png' % n))
        a, b = tile4(raw, 320), tile4(d, 320)
        sheet = Image.new('RGB', (a.width * 2 + 16, a.height), (20, 20, 20))
        sheet.paste(a, (0, 0))
        sheet.paste(b, (a.width + 16, 0))
        label(sheet, '%s  raw Flow image, 4 x 4' % n)
        label(sheet, '%s  T_Shrine_%s_D (healed, de-lit), 4 x 4' % (n, n), (a.width + 30, 10))
        sheet.save(os.path.join(SHOTS, 'texpilot-tile-%s-D.png' % n), optimize=True)
        cells = []
        for k in ('D', 'H', 'N', 'R', 'AO', 'ORM'):
            im = Image.open(os.path.join(OUT, 'T_Shrine_%s_%s.png' % (n, k)))
            if im.mode == 'I;16' or im.mode == 'I':
                im = im.point(lambda v: v / 256).convert('L')
            t = tile4(im, 192)
            label(t, '%s  %s' % (n, k))
            cells.append(t)
        cw = cells[0].width
        sheet = Image.new('RGB', (cw * 3 + 32, cw * 2 + 16), (20, 20, 20))
        for i, t in enumerate(cells):
            sheet.paste(t, ((i % 3) * (cw + 16), (i // 3) * (cw + 16)))
        sheet.save(os.path.join(SHOTS, 'texpilot-tile-%s-maps.png' % n), optimize=True)
        print('TILE_PROOF', n)


if __name__ == '__main__':
    main()
