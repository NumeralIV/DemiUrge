# DEMIURGE texture pilot - side-by-side BEFORE | AFTER sheets from render_pilot.py output (system Python + Pillow).
#   python art/textures-pilot/compose_shots.py [render_dir] [alt_dir]
# -> shots/texpilot-<40m|300m|close8m>-<golden|overcast>.png   (BEFORE procedural | AFTER Flow StoneB + ThatchB)
# -> shots/texpilot-alt-<view>-<light>.png                     (Flow B sets | Flow A sets), when alt_dir exists
import os
import sys

from PIL import Image, ImageDraw

from tile_proof import font

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'shots')
VIEWNAME = {'v40': '40m', 'v300': '300m', 'close8': 'close8m'}
VIEWTEXT = {'v40': '40 m three-quarter', 'v300': '300 m oblique (inset: centre 3x, nearest)', 'close8': 'close, 8 m'}


def tag(img, text, xy, size=26):
    d = ImageDraw.Draw(img)
    f = font(size)
    bb = d.textbbox(xy, text, font=f)
    d.rectangle((bb[0] - 8, bb[1] - 5, bb[2] + 8, bb[3] + 5), fill=(0, 0, 0))
    d.text(xy, text, font=f, fill=(255, 255, 255))


def inset(im):
    """3x nearest-neighbour crop of the image centre, pasted top-right under the label (the shrine is ~70 px wide
    at 300 m and sits in the centre)."""
    w, h = im.size
    cw, ch = w // 8, h // 8
    crop = im.crop((w // 2 - cw // 2, h // 2 - ch // 2, w // 2 + cw // 2, h // 2 + ch // 2))
    crop = crop.resize((cw * 3, ch * 3), Image.NEAREST)
    im = im.copy()
    x, y = w - crop.width - 8, 60
    ImageDraw.Draw(im).rectangle((x - 3, y - 3, x + crop.width + 2, y + crop.height + 2), fill=(255, 255, 255))
    im.paste(crop, (x, y))
    return im


def pair(a, b, la, lb, title, out):
    if a.size != b.size:
        b = b.resize(a.size)
    W, H = a.size
    sheet = Image.new('RGB', (W * 2 + 16, H + 56), (18, 18, 18))
    sheet.paste(a, (0, 56))
    sheet.paste(b, (W + 16, 56))
    tag(sheet, title, (12, 12), 28)
    tag(sheet, la, (14, 70))
    tag(sheet, lb, (W + 30, 70))
    sheet.save(out, optimize=True)
    print('SHOT', out)


def main():
    rd = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'renders')
    alt = sys.argv[2] if len(sys.argv) > 2 else os.path.join(rd, 'alt-A')
    for v in VIEWNAME:
        for l in ('golden', 'overcast'):
            pb, pa = os.path.join(rd, '%s-%s-before.png' % (v, l)), os.path.join(rd, '%s-%s-after.png' % (v, l))
            if not (os.path.exists(pb) and os.path.exists(pa)):
                continue
            a, b = Image.open(pb).convert('RGB'), Image.open(pa).convert('RGB')
            if v == 'v300':
                a, b = inset(a), inset(b)
            pair(a, b, 'BEFORE  procedural rubble / limestone / thatch',
                 'AFTER  Flow stone-b (walls, plinth) + thatch-b (roof)',
                 'Shrine texture pilot - %s - %s' % (VIEWTEXT[v], 'golden hour' if l == 'golden' else 'overcast'),
                 os.path.join(SHOTS, 'texpilot-%s-%s.png' % (VIEWNAME[v], l)))
            px = os.path.join(alt, '%s-%s-after.png' % (v, l))
            if os.path.exists(px):
                pair(b if v != 'v300' else b, Image.open(px).convert('RGB'),
                     'Flow stone-b + thatch-b (adopted)', 'Flow stone-a + thatch-a (alternative)',
                     'Shrine texture pilot - Flow A vs B - %s - %s' % (VIEWTEXT[v], l),
                     os.path.join(SHOTS, 'texpilot-alt-%s-%s.png' % (VIEWNAME[v], l)))


if __name__ == '__main__':
    main()
