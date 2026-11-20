# TreeLab: builds review contact sheets from shots/ue-treelab-*.png (Blender's numpy, no other dependencies).
#   blender.exe -b --factory-startup --python UnrealLab/TreeLab/Scripts/contact_sheet.py -- <out.jpg> <cols> <img1> <img2> ...
# Missing images become grey cells. Each cell is 480 x 270 (1/4 of 1920 x 1080, box filtered).
import os
import sys
import numpy as np
import bpy

argv = sys.argv[sys.argv.index('--') + 1:]
out, cols, files = argv[0], int(argv[1]), argv[2:]
CW, CH = 480, 270


def load(p):
    img = bpy.data.images.load(p, check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    a = px.reshape(h, w, 4)
    fy, fx = h // CH, w // CW
    a = a[:CH * fy, :CW * fx].reshape(CH, fy, CW, fx, 4).mean(axis=(1, 3))
    return a


rows = (len(files) + cols - 1) // cols
sheet = np.full((rows * CH, cols * CW, 4), 0.25, np.float32)
sheet[..., 3] = 1
for i, f in enumerate(files):
    r, c = i // cols, i % cols
    y1 = (rows - r) * CH            # row 0 at the top (pixel rows are bottom-up)
    if os.path.exists(f):
        sheet[y1 - CH:y1, c * CW:(c + 1) * CW] = load(f)
    sheet[y1 - CH:y1, c * CW:c * CW + 2] = 0.05
    sheet[y1 - 2:y1, c * CW:(c + 1) * CW] = 0.05
img = bpy.data.images.new('sheet', sheet.shape[1], sheet.shape[0], alpha=False)
img.pixels.foreach_set(sheet.ravel())
img.filepath_raw = out
img.file_format = 'JPEG'
bpy.context.scene.render.image_settings.quality = 90
img.save()
print('SHEET', out, sheet.shape)
