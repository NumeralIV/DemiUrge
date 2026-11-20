# DEMIURGE style pilot - the test scene: the village shrine at the centre of a small Bronze-age block
# (8 houses from art/age1 on a ring, an earth plaza and paths, grass, a few broadleaf trees for scale).
#   blender -b --factory-startup --python art/style-pilot/build_scene.py
# Writes art/style-pilot/scene/style_village.blend and scene/path_mask.png. Geometry is the shipped LOD0
# meshes, untouched; only materials change between looks (render_style.py).
import math
import os
import random

import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SCENE_DIR = os.path.join(HERE, 'scene')
SHRINE = os.path.join(ROOT, 'art', 'shrine', 'shrine.blend')
AGE1 = os.path.join(ROOT, 'art', 'age1', 'source', 'age1.blend')
TREES = os.path.join(ROOT, 'art', 'trees', 'source', 'broadleaf.blend')
MASK_M = 100.0          # the path mask covers +-50 m around the shrine
MASK_N = 1024

# ring of houses: (kit house, wall variant, roof variant). Variants show the other two materials of the pilot
# (whitewashed plaster walls, terracotta tile roofs) the way culture instances would.
HOUSES = [('A', 'mud', 'thatch'), ('B', 'plaster', 'thatch'), ('D', 'mud', 'tiles'), ('C', 'mud', 'flat'),
          ('A', 'mud', 'tiles'), ('B', 'mud', 'thatch'), ('D', 'plaster', 'thatch'), ('C', 'plaster', 'flat')]


def append_objects(path, names):
    with bpy.data.libraries.load(path, link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n in names]
    base = os.path.dirname(path)
    out = []
    for o in dst.objects:
        if o is None:
            continue
        o.parent = None
        bpy.context.scene.collection.objects.link(o)
        out.append(o)
    for im in bpy.data.images:          # make every texture path absolute against its source .blend
        fp = im.filepath
        if fp and not os.path.exists(bpy.path.abspath(fp)) and fp.startswith('//'):
            alt = os.path.normpath(os.path.join(base, fp[2:].replace('\\', '/')))
            if os.path.exists(alt):
                im.filepath = alt
    return out


def path_mask():
    """Earth plaza around the shrine, a ring lane, a spur to every door and one road out to the south-west.
    Soft edges with a little noise; 0 at the border so the ground is grass outside the mask."""
    n = MASK_N
    xs = (np.arange(n) + 0.5) / n * MASK_M - MASK_M / 2
    X, Y = np.meshgrid(xs, -xs)                         # row 0 = +Y (north)
    R = np.sqrt(X * X + Y * Y)
    d = np.full_like(R, 1e9)
    d = np.minimum(d, R - 8.5)                           # plaza
    d = np.minimum(d, np.abs(R - 15.0) - 1.4)            # ring lane, 2.8 m wide
    def seg(p, q, w):
        p, q = np.array(p), np.array(q)
        v = q - p
        t = np.clip(((X - p[0]) * v[0] + (Y - p[1]) * v[1]) / (v @ v), 0, 1)
        return np.sqrt((X - p[0] - t * v[0]) ** 2 + (Y - p[1] - t * v[1]) ** 2) - w / 2
    for i, (ang, rad) in enumerate(house_slots()):
        a = math.radians(ang)
        d = np.minimum(d, seg((15 * math.cos(a), 15 * math.sin(a)), ((rad - 3.2) * math.cos(a), (rad - 3.2) * math.sin(a)), 1.8))
    d = np.minimum(d, seg((0, -15), (-5, -48), 3.2))   # road out
    rng = np.random.default_rng(7)
    noise = rng.standard_normal((n // 16, n // 16)).astype(np.float32)
    noise = np.kron(noise, np.ones((16, 16), np.float32))
    k = np.exp(-np.linspace(-2, 2, 33) ** 2)
    k /= k.sum()
    for ax in (0, 1):
        noise = np.apply_along_axis(lambda r: np.convolve(np.pad(r, 16, mode='wrap'), k, 'valid'), ax, noise)
    noise = noise / (noise.std() + 1e-6)
    d = d + 0.45 * noise
    m = np.clip(0.5 - d / 1.2, 0, 1)
    m *= np.clip((MASK_M / 2 - np.maximum(np.abs(X), np.abs(Y))) / 3.0, 0, 1)
    print('STYLE_MASK', float(np.nanmin(m)), float(np.nanmax(m)), float(np.nanmean(m)), float(np.isnan(noise).mean()))
    import sys
    sys.path.insert(0, os.path.join(ROOT, 'art', 'textures-pilot'))
    import make_maps as mm
    p = os.path.join(SCENE_DIR, 'path_mask.png')
    mm.save_png(p, m.astype(np.float32))
    img = bpy.data.images.load(p)
    img.colorspace_settings.name = 'Non-Color'
    return img


def house_slots():
    rng = random.Random(11)
    out = []
    for i in range(len(HOUSES)):
        ang = -90 + 22.5 + i * 45 + rng.uniform(-6, 6)      # leaves the south (road) open between two houses
        out.append((ang, 22.5 + rng.uniform(-1.0, 1.5)))
    return out


def main():
    os.makedirs(SCENE_DIR, exist_ok=True)
    sc = bpy.context.scene
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    # shrine, intact LOD0
    with bpy.data.libraries.load(SHRINE, link=False) as (src, dst):
        names = list(src.objects)
    shrine = append_objects(SHRINE, [n for n in names if n.startswith('intact.') and n.endswith('.LOD0')])
    for o in shrine:
        o['style_part'] = 'shrine'
    # houses
    kinds = sorted(set(h[0] for h in HOUSES))
    srcs = {k: append_objects(AGE1, ['SM_Age1_House_%s_Intact_LOD0' % k])[0] for k in kinds}
    used = {k: 0 for k in kinds}
    for (k, wall, roof), (ang, rad) in zip(HOUSES, house_slots()):
        o = srcs[k] if used[k] == 0 else srcs[k].copy()
        if used[k]:
            sc.collection.objects.link(o)
        used[k] += 1
        a = math.radians(ang)
        o.location = (rad * math.cos(a), rad * math.sin(a), 0.0)
        o.rotation_euler = (0, 0, a - math.radians(90) + random.Random(ang).uniform(-0.12, 0.12))
        o['style_part'] = 'house'
        o['style_wall'] = wall
        o['style_roof'] = roof
    # trees for scale (identical in every look)
    try:
        tnames = ['SM_Tree_Broadleaf_%d_LOD1' % i for i in range(4)]
        trees = append_objects(TREES, tnames)
        rng = random.Random(5)
        placed = 0
        while placed < 34:
            r, a = rng.uniform(36, 120), rng.uniform(0, 2 * math.pi)
            x, y = r * math.cos(a), r * math.sin(a)
            if -60 < y < -12 and -30 < x < 6:      # keep the road and the camera side of the road clear
                continue
            t = rng.choice(trees).copy()
            t.data = t.data            # linked duplicate
            sc.collection.objects.link(t)
            t.location = (x, y, 0)
            t.rotation_euler = (0, 0, rng.uniform(0, 6.28))
            s = rng.uniform(0.75, 1.1)
            t.scale = (s, s, s)
            t['style_part'] = 'tree'
            placed += 1
        for t in trees:
            bpy.data.objects.remove(t)
    except Exception as e:                          # trees are context only
        print('STYLE_TREES_SKIPPED', e)
    # ground
    me = bpy.data.meshes.new('StyleGround')
    s = 1500.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new('StyleGround', me)
    sc.collection.objects.link(g)
    g['style_part'] = 'ground'
    path_mask()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SCENE_DIR, 'style_village.blend'))
    print('STYLE_SCENE', [(o.name, tuple(round(v, 1) for v in o.location)) for o in bpy.data.objects if o.get('style_part') == 'house'])


main()
