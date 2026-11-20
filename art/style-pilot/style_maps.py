# DEMIURGE style pilot - texture sets for the five looks, built with the project's Flow recipe.
#   blender -b --factory-startup --python art/style-pilot/style_maps.py -- [--looks=PHOTO,P,S1,S2] [--only=rubble,..]
#                                                                        [--psrc] [--size=2048]
# Every candidate goes through art/textures-pilot/make_maps.py (seam healing, de-light, calibration, N/R/AO/ORM).
# This file only adds what the pilot recipe did not have:
#   * KINDS for the new material families (brick, plaster, tiles, wood, ground, grass) and a generic height map
#     for them (make_maps only knew stone and thatch);
#   * softer normals per look (stylised sets: 0.6 x the kind's mean tilt and a 2.5 px pre-blur);
#   * luma-only albedo calibration: every set gets the same mean brightness as the CURRENT procedural set of
#     that material, but keeps its own hue, so the comparison is about texture style, not exposure;
#   * look P: a processed-down copy of the photoreal library image (--psrc writes p-src/<mat>.png).
# Outputs: maps/<LOOK>/T_Style<LOOK>_<Mat>_<D|H|N|N_DX|R|AO|ORM>.png (+ 1k/), maps/<LOOK>/<Mat>.json
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'art', 'textures-pilot'))
import make_maps as mm  # noqa: E402

LIB = os.path.join(ROOT, 'art', 'textures-flow')
FLOW = os.path.join(HERE, 'flow')
PSRC = os.path.join(HERE, 'p-src')
MAPS = os.path.join(HERE, 'maps')

# ---------------------------------------------------------------------------------------------- new kinds
mm.KINDS.update({
    'brick':   dict(w=(0.14, 0.14), lam=0.0, rough=(0.96, 0.90, 0.82), relief_m=0.030, delit_cavity=0.30, tilt_deg=20.0),
    'plaster': dict(w=(0.20, 0.20), lam=0.0, rough=(0.95, 0.90, 0.85), relief_m=0.008, delit_cavity=0.10, tilt_deg=7.0),
    'tiles':   dict(w=(0.16, 0.16), lam=0.0, rough=(0.92, 0.80, 0.70), relief_m=0.040, delit_cavity=0.30, tilt_deg=24.0),
    'wood':    dict(w=(0.14, 0.10), lam=0.0, rough=(0.92, 0.82, 0.72), relief_m=0.010, delit_cavity=0.20, tilt_deg=12.0),
    'ground':  dict(w=(0.20, 0.20), lam=0.0, rough=(0.98, 0.94, 0.90), relief_m=0.020, delit_cavity=0.20, tilt_deg=14.0),
    'grass':   dict(w=(0.20, 0.20), lam=0.0, rough=(0.98, 0.92, 0.86), relief_m=0.030, delit_cavity=0.25, tilt_deg=22.0),
})
# weights of (large shapes, mid, fine) in the generic height map
HWEIGHTS = {'brick': (0.60, 0.30, 0.10), 'tiles': (0.60, 0.30, 0.10), 'plaster': (0.50, 0.35, 0.15),
            'wood': (0.30, 0.50, 0.20), 'ground': (0.40, 0.40, 0.20), 'grass': (0.30, 0.40, 0.30)}

_height_orig = mm.height_map
_normal_orig = mm.normal_map
STATE = dict(pre_sigma=1.0)


def height_generic(lin, kind, n):
    if kind in ('stone', 'thatch'):
        return _height_orig(lin, kind, n)
    L = mm.luma(lin)
    s = n / 2048.0
    Ln = L / (mm.gblur(L, 60 * s) + 1e-4)
    big = mm.gblur(Ln, 6 * s) - mm.gblur(Ln, 60 * s)      # bricks / tiles / boards up, joints and gaps down
    mid = mm.gblur(Ln, 2 * s) - mm.gblur(Ln, 20 * s)
    fine = Ln - mm.gblur(Ln, 3 * s)
    wb, wm, wf = HWEIGHTS[kind]
    h = wb * np.tanh(big * 3) + wm * np.tanh(mid * 4) + wf * np.tanh(fine * 6)
    h = (h - h.min()) / (h.max() - h.min() + 1e-6)
    return h.astype(np.float32)


def normal_soft(h, relief_m, tile_m, tilt_deg=None, pre_sigma=1.0):
    return _normal_orig(h, relief_m, tile_m, tilt_deg, pre_sigma=STATE['pre_sigma'])


mm.height_map = height_generic
mm.normal_map = normal_soft

# ---------------------------------------------------------------------------------------------- materials
# mat key -> (Name, kind, CURRENT procedural D used for the luma target, fixed target if no procedural set)
MATS = {
    'rubble':     ('Rubble',     'stone',   'art/textures/rubble/T_Rubble_BC.png', None),
    'thatch':     ('Thatch',     'thatch',  'art/age1/textures/T_Age1_Thatch_D.png', None),
    'mudbrick':   ('Mudbrick',   'brick',   'art/age1/textures/T_Age1_Mudbrick_D.png', None),
    'plaster':    ('Plaster',    'plaster', None, '#d8d0c2'),     # whitewash: lighter than the kit's clay render
    'terracotta': ('Terracotta', 'tiles',   None, '#a65a3c'),     # the kit has no tile roof
    'planks':     ('Planks',     'wood',    'art/textures/oak/T_Oak_BC.png', None),
    'path':       ('Path',       'ground',  'art/age1/textures/T_Age1_Soil_D.png', None),
    'grass':      ('Grass',      'grass',   'art/textures/turf/T_Turf_BC.png', None),
}
# PHOTO = the photoreal library image as generated (one of a/b per material, picked by eye), tile in metres
PHOTO = {
    'rubble': ('buildings/stone-rubble/stone-rubble-a.jpg', 2.0),
    'thatch': ('buildings/thatch-reed/thatch-reed-a.jpg', 1.2),
    'mudbrick': ('buildings/mudbrick/mudbrick-a.jpg', 2.0),
    'plaster': ('buildings/plaster-whitewash/plaster-whitewash-a.jpg', 3.0),
    'terracotta': ('buildings/roof-tiles-terracotta/roof-tiles-terracotta-a.jpg', 1.6),
    'planks': ('buildings/wood-planks/wood-planks-a.jpg', 1.6),
    'path': ('buildings/path-earth/path-earth-a.jpg', 2.5),
    'grass': ('terrain/grass-lush/grass-lush-a.jpg', 1.5),
}
P_SCALE = 1.35          # look P: features read 35 % larger when tiled
# S1 / S2: Flow downloads flow/<look>/<mat>-<a|b>.jpg; pick and tile size set after looking at each image
# Tile sizes from counting units per square (2026-11-20; only "a" was generated), slightly oversized on purpose
# (stylised = larger readable shapes):
#   S1 rubble ~10 stones across -> 2.6 m (26 cm stones); thatch 7 courses -> 1.6 m; mudbrick 14 courses -> 1.8 m
#      (13 cm courses); terracotta 10 tile columns -> 2.2 m; planks 7 boards -> 1.4 m (20 cm boards); path 3.0 m;
#      grass ~8 clumps -> 2.0 m
#   S2 rubble ~9 stones -> 2.6 m; thatch 4.5 courses -> 1.4 m; mudbrick 8 courses -> 1.4 m (big 17 cm courses);
#      terracotta 12 columns -> 2.4 m; planks 9 boards -> 1.6 m; path has an X of lighter ground across the whole
#      square (a repeat risk) -> 4.0 m to push the repeat past the plaza; grass 4 large clumps -> 1.6 m
STYL = {
    'S1': {'rubble': ('a', 2.6), 'thatch': ('a', 1.6), 'mudbrick': ('a', 1.8), 'plaster': ('a', 3.0),
           'terracotta': ('a', 2.2), 'planks': ('a', 1.4), 'path': ('a', 3.0), 'grass': ('a', 2.0)},
    'S2': {'rubble': ('a', 2.6), 'thatch': ('a', 1.4), 'mudbrick': ('a', 1.4), 'plaster': ('a', 3.0),
           'terracotta': ('a', 2.4), 'planks': ('a', 1.6), 'path': ('a', 4.0), 'grass': ('a', 1.6)},
}
# Extra de-light before make_maps for sources whose large light/dark shapes repeat visibly once tiled
# (flatten radius as a fraction of the image; written to prep/<look>-<mat>.png, which make_maps then reads):
#   S1 terracotta: a light diagonal streak through the tiles repeats once per tile -> ratio flatten (n/14).
#   S2 path: a light X across the whole square tiles into a diamond lattice. A ratio flatten (n/24) left a star
#   lattice with halos, so it gets a colour high-pass instead ('hp': everything larger than ~n/40 replaced by
#   the mean; pebbles and small marks kept). The image fails the reject rule and should be regenerated.
#   S1 / S2 grass: painted tufts with dark gaps turn into a regular carpet pattern at 80 m. S2 -> 'calm':
#   luminance spread around the mean x0.55 (hue and tuft shapes kept). S1's gaps are dark blue-green, which
#   'calm' lifted into bright teal, so S1 -> 'mix': colour pulled toward the mean colour, (c - mean) x0.55.
#   S1 / S2 planks: Flow drew horizontal boards; the library (and the shrine's posts and door frame UVs) expect
#   vertical boards, so on the posts they read as brickwork -> 'rot': turned 90 degrees.
PREFLAT = {('S2', 'path'): ('hp', 1 / 40.0), ('S1', 'terracotta'): ('flat', 1 / 14.0),
           ('S1', 'grass'): ('mix', 0.55), ('S2', 'grass'): ('calm', 0.55),
           ('S1', 'planks'): ('rot', 1), ('S2', 'planks'): ('rot', 1)}
# The stylised paths came out brick-red (#724324) and fought the roofs and walls: their albedo target takes the
# CURRENT soil's hue instead of their own (make_maps then moves the cast half-way to it).
HUE_FROM_CURRENT = {('S1', 'path'), ('S2', 'path')}
SOFT = {'PHOTO': (1.0, 1.0), 'P': (0.65, 2.5), 'S1': (0.6, 2.5), 'S2': (0.6, 2.5)}   # (tilt factor, pre-blur px)


def mean_lin(path_or_arr):
    a = mm.load_image(path_or_arr) if isinstance(path_or_arr, str) else path_or_arr
    return mm.srgb_to_lin(a).reshape(-1, 3).mean(0)


def luma_target(src_path, mat, cur_hue=False):
    """Hex target with the CURRENT set's mean luma and the source image's own mean hue (or, with cur_hue, the
    CURRENT set's hue)."""
    name, kind, cur, fixed = MATS[mat]
    tl = mm.hex_lin(fixed) if fixed else mean_lin(os.path.join(ROOT, cur))
    sm = tl if cur_hue else mean_lin(src_path)
    t = sm * float((tl @ mm.LUMA) / (sm @ mm.LUMA))
    if t.max() > 0.98:
        t = t * (0.98 / t.max())
    return mm.lin_hex(t)

# ---------------------------------------------------------------------------------------------- look P


def guided(I, p, r, eps):
    """He et al. guided filter with a Gaussian window (sigma r px), guide I (H, W), input p (H, W)."""
    m = lambda a: mm.gblur(a, r, (False, False))
    mI, mp = m(I), m(p)
    cov = m(I * p) - mI * mp
    var = m(I * I) - mI * mI
    a = cov / (var + eps)
    b = mp - a * mI
    return m(a) * I + m(b)


def make_p(mat, size=2048):
    """Processed-down photoreal (v2, 2026-11-20; v1 was too close to PHOTO to be a separate option):
    edge-preserving smoothing (three guided-filter passes, guide = luma), only 15 % of the removed fine detail
    added back, saturation x1.25, luminance spread x0.80 around the mean with the darkest crevices lifted."""
    src = os.path.join(LIB, PHOTO[mat][0])
    a = mm.load_image(src)
    if a.shape[0] != size:
        a = np.clip(mm.fft_resize(a, size, size), 0, 1)
    s = size / 2048.0
    base = a.copy()
    for r, eps in ((18 * s, 0.020), (10 * s, 0.012), (5 * s, 0.006)):
        L = base @ mm.LUMA
        base = np.stack([guided(L, base[..., c], r, eps) for c in range(3)], -1)
    out = base + 0.15 * (a - base)
    L = out @ mm.LUMA
    out = L[..., None] + (out - L[..., None]) * 1.25                     # saturation
    Lm = float(L.mean())
    L2 = Lm + (L - Lm) * 0.80
    L2 = L2 + 0.10 * np.clip(0.25 - L2, 0, None)                         # lift the darkest crevices
    out = out * (L2 / (L + 1e-4))[..., None]
    out = np.clip(out, 0, 1)
    p = os.path.join(PSRC, '%s.png' % mat)
    mm.save_png(p, out)
    print('STYLE_PSRC', p)
    return p

# ---------------------------------------------------------------------------------------------- driver


def job(look, mat, size):
    name, kind, _, _ = MATS[mat]
    if look == 'PHOTO':
        src, tile = os.path.join(LIB, PHOTO[mat][0]), PHOTO[mat][1]
    elif look == 'P':
        src, tile = os.path.join(PSRC, '%s.png' % mat), round(PHOTO[mat][1] * P_SCALE, 2)
        if not os.path.exists(src):
            src = make_p(mat, size)
    else:
        pick, tile = STYL[look][mat]
        # flow/<S1|S2>/<mat>-<a|b>.jpg (+ .provenance.json, tools/flow_provenance.py ingest); old flat name kept
        src = os.path.join(FLOW, look, '%s-%s.jpg' % (mat, pick))
        if not os.path.exists(src):
            src = os.path.join(FLOW, '%s-%s-%s.jpg' % (look.lower(), mat, pick))
    if not os.path.exists(src):
        print('STYLE_SKIP missing', src)
        return None
    orig_src = src
    if (look, mat) in PREFLAT:
        a = mm.load_image(src)
        k = min(a.shape[:2])
        lin = mm.srgb_to_lin(a[:k, :k])
        mode, frac = PREFLAT[(look, mat)]
        if mode == 'hp':
            lin = lin - mm.gblur(lin, k * frac, (False, False)) + lin.reshape(-1, 3).mean(0)
        elif mode == 'rot':
            lin = np.ascontiguousarray(np.rot90(lin, int(frac)))
        elif mode == 'mix':
            m = lin.reshape(-1, 3).mean(0)
            lin = m + (lin - m) * frac
        elif mode == 'calm':
            L = mm.luma(lin)
            Lm = float(L.mean())
            lin = lin * ((Lm + (L - Lm) * frac) / (L + 1e-5))[..., None]
        else:
            lin = mm.flatten(lin, k * frac, (False, False), 0.6, 1.6)
        lin = np.clip(lin, 0, 1)
        src = os.path.join(HERE, 'prep', '%s-%s.png' % (look, mat))
        mm.save_png(src, mm.lin_to_srgb(lin))
        print('STYLE_PREFLAT', src)
    tf, ps = SOFT[look]
    base = dict(mm.KINDS[kind])
    mm.KINDS[kind] = dict(base, tilt_deg=base['tilt_deg'] * tf)
    STATE['pre_sigma'] = ps
    try:
        tgt = luma_target(src, mat, (look, mat) in HUE_FROM_CURRENT)
        rep = mm.process(src, name, kind, tile, tgt, size, None, os.path.join(MAPS, look), 'T_Style%s_' % look)
    finally:
        mm.KINDS[kind] = base
    rep['look'] = look
    rep['source_file'] = os.path.relpath(orig_src, ROOT).replace('\\', '/')
    if src != orig_src:
        rep['preflatten'] = [PREFLAT[(look, mat)][0], round(PREFLAT[(look, mat)][1], 4)]
    rep['tilt_factor'], rep['pre_blur_px'] = tf, ps
    if (look, mat) in HUE_FROM_CURRENT:
        rep['hue_from_current'] = True
    with open(os.path.join(MAPS, look, '%s.json' % name), 'w') as f:
        json.dump(rep, f, indent=1)
    return rep


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    fl = dict(a[2:].split('=', 1) for a in argv if a.startswith('--') and '=' in a)
    looks = fl.get('looks', 'PHOTO,P,S1,S2').split(',')
    only = fl.get('only', ','.join(MATS)).split(',')
    size = int(fl.get('size', 2048))
    if '--psrc' in argv:
        for m in only:
            make_p(m, size)
    summary = []
    for look in looks:
        for m in only:
            r = job(look, m, size)
            if r:
                summary.append((look, m, r['tile_m'], r['albedo_mean'], r['normal_tilt_deg'], r['seconds']))
    for s in summary:
        print('STYLE_DONE', *s)


if __name__ == '__main__':
    main()
