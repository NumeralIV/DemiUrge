# DEMIURGE art pipeline - contact sheet of images (texture review, render review).
#   blender.exe -b --factory-startup --python art/tools/contact_sheet.py -- <out.png> <cols> <cell_px> <img> [<img> ...]
# An image argument may be 'tile:<path>' (2x2 repeat, seam check) or 'crop:x0,y0,x1,y1:<path>' (pixel box).
# Images are resized (nearest-of-box) to cell_px wide keeping aspect; a 2x2 repeat is shown for textures when the
# argument is prefixed with "tile:" (checks seams).
import sys

import bpy
import numpy as np


def load(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)


def resize(a, w):
    h0, w0 = a.shape[:2]
    h = max(1, int(round(h0 * w / w0)))
    ys = (np.arange(h) * h0 / h).astype(np.int32)
    xs = (np.arange(w) * w0 / w).astype(np.int32)
    # box filter by averaging a few samples
    acc = np.zeros((h, w, 4), np.float32)
    k = max(1, min(4, w0 // w))
    for dy in range(k):
        for dx in range(k):
            acc += a[np.clip(ys + dy * h0 // (h * k), 0, h0 - 1)][:, np.clip(xs + dx * w0 // (w * k), 0, w0 - 1)]
    return acc / (k * k)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    out, cols, cell = argv[0], int(argv[1]), int(argv[2])
    tiles = []
    for p in argv[3:]:
        rep = p.startswith('tile:')
        if rep:
            p = p[5:]
        crop = None
        if p.startswith('crop:'):
            # crop:x0,y0,x1,y1:path  (pixels, top-left origin, as in an image viewer)
            _, box, p = p.split(':', 2)
            crop = [int(v) for v in box.split(',')]
        a = load(p)
        if crop:
            h = a.shape[0]
            x0, y0, x1, y1 = crop
            a = a[h - y1:h - y0, x0:x1]
        if rep:
            a = np.concatenate([np.concatenate([a, a], 1)] * 2, 0)
        tiles.append(resize(a, cell))
    ch = max(t.shape[0] for t in tiles)
    rows = (len(tiles) + cols - 1) // cols
    pad = 6
    W = cols * (cell + pad) + pad
    Hh = rows * (ch + pad) + pad
    sheet = np.full((Hh, W, 4), 0.1, np.float32)
    sheet[..., 3] = 1
    for i, t in enumerate(tiles):
        r, c = divmod(i, cols)
        y0 = Hh - pad - (r + 1) * (ch + pad) + pad + (ch - t.shape[0])
        x0 = pad + c * (cell + pad)
        a = t[..., 3:4]
        sheet[y0:y0 + t.shape[0], x0:x0 + t.shape[1], :3] = t[..., :3] * a + 0.1 * (1 - a)
    img = bpy.data.images.new('sheet', W, Hh, alpha=False)
    img.colorspace_settings.name = 'Non-Color'
    img.pixels.foreach_set(sheet.ravel())
    img.filepath_raw = out
    img.file_format = 'PNG'
    img.save()
    print('SHEET', out, W, Hh)


main()
