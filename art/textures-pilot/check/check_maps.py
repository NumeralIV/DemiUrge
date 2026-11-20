# Independent check of the texture pilot maps (Blender's Python: numpy; bpy only to decode PNGs). Read-only.
#   blender -b --factory-startup --python art/textures-pilot/check/check_maps.py -- [--out=<dir>] [Name ...]
# Prints CHECK lines (JSON) per set:
#  - normal_vs_height: correlation of the stored normal's x / y with the slope of the stored height
#    (OpenGL: x = -dh/du, y = -dh/dv with v up). +1 = consistent, -1 = inverted.
#  - photo_light: best-fit direction of a light baked into the diffuse, from regressing the diffuse's
#    band-pass luminance on the normal's x / y. A clear fit from one side means the derived relief agrees (or,
#    with the opposite sign, disagrees) with the shading the photo shows.
#  - thatch course profile: mean height around each detected butt shadow line (rows above / below).
#  - stripes: low-frequency row / column banding in N, R and D (max deviation of row means / column means
#    after removing the tile mean), and where it is.
#  - procedural set reference numbers for the same checks.
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PILOT = os.path.dirname(HERE)
sys.path.insert(0, PILOT)
import make_maps as mm   # noqa: E402  (the pilot's own filters, so the check uses the same blur)

ROOT = os.path.abspath(os.path.join(PILOT, '..', '..'))


def load(path):
    """-> float32 (H, W, C) 0..1 as stored, row 0 = top."""
    import bpy
    img = bpy.data.images.load(path)
    img.colorspace_settings.name = 'Non-Color'
    w, h = img.size
    c = img.channels
    px = np.empty(w * h * c, np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, c)[::-1].copy()


def corr(a, b):
    a = a - a.mean()
    b = b - b.mean()
    return float((a * b).sum() / math.sqrt((a * a).sum() * (b * b).sum() + 1e-12))


def slopes(h):
    du = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5        # dh/du (u right)
    drow = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5      # dh/drow (row down)
    return du, -drow                                          # dh/du, dh/dv (v up)


def banding(a):
    """max |row mean - mean| and |col mean - mean| relative to the image's std, plus the row/col where it is."""
    a = a.astype(np.float64)
    s = a.std() + 1e-9
    r = a.mean(1) - a.mean()
    c = a.mean(0) - a.mean()
    return dict(row_max=round(float(np.abs(r).max() / s), 3), row_at=int(np.abs(r).argmax()),
                col_max=round(float(np.abs(c).max() / s), 3), col_at=int(np.abs(c).argmax()),
                row_std=round(float(r.std() / s), 3), col_std=round(float(c.std() / s), 3))


def light_fit(D, N):
    """Regress band-pass luminance (5..60 px) on the normal's tangent components. Returns the fitted light's
    tangent direction (degrees, 0 = +u right, 90 = +v up) and the fraction of variance explained."""
    L = mm.luma(D if D.shape[-1] == 3 else D[..., :3])
    s = L.shape[0] / 2048.0
    bp = mm.gblur(L, 2 * s) - mm.gblur(L, 60 * s)
    nx = N[..., 0] * 2 - 1
    ny = N[..., 1] * 2 - 1
    nx = mm.gblur(nx, 2 * s) - mm.gblur(nx, 60 * s)
    ny = mm.gblur(ny, 2 * s) - mm.gblur(ny, 60 * s)
    X = np.stack([nx.ravel(), ny.ravel()], 1).astype(np.float64)
    y = bp.ravel().astype(np.float64)
    coef, *_ = np.linalg.lstsq(X, y, rcond=None)
    r2 = 1 - ((y - X @ coef) ** 2).sum() / ((y - y.mean()) ** 2).sum()
    return dict(dir_deg=round(math.degrees(math.atan2(coef[1], coef[0])), 1), r2=round(float(r2), 3),
                coef=[round(float(c), 4) for c in coef])


def course_profile(D, H, n):
    """Thatch: find butt shadow lines (strong bright-over-dark steps) and average H (and luminance) in rows
    relative to them. Offsets in px at 2048, negative = above (toward the ridge, image top)."""
    lin = mm.srgb_to_lin(D[..., :3])
    b = mm.butt_lines(lin, n)
    s = n / 2048.0
    # local maxima down each column
    pk = (b > np.roll(b, 1, 0)) & (b >= np.roll(b, -1, 0)) & (b > 0.55)
    ys, xs = np.nonzero(pk)
    offs = list(range(-60, 61, 6))
    L = mm.luma(lin)
    Ln = L / (mm.gblur(L, 60 * s) + 1e-4)
    prof_h, prof_l = [], []
    for o in offs:
        yy = (ys + int(o * s)) % n
        prof_h.append(round(float(H[yy, xs].mean()), 3))
        prof_l.append(round(float(Ln[yy, xs].mean()), 3))
    return dict(peaks=int(len(ys)), offsets=offs, h=prof_h, lum=prof_l)


def check_set(name, out_dir, kind):
    p = lambda k: os.path.join(out_dir, 'T_Shrine_%s_%s.png' % (name, k))
    D = load(p('D'))[..., :3]
    H = load(p('H'))[..., 0]
    N = load(p('N'))[..., :3]
    NDX = load(p('N_DX'))[..., :3]
    R = load(p('R'))[..., 0]
    n = H.shape[0]
    du, dv = slopes(H)
    nx, ny = N[..., 0] * 2 - 1, N[..., 1] * 2 - 1
    res = dict(name=name,
               normal_vs_height=dict(x=round(corr(nx, -du), 3), y=round(corr(ny, -dv), 3),
                                     dx_green_is_flipped=round(corr(NDX[..., 1] * 2 - 1, -ny), 3)),
               normal_tilt_deg_mean=round(float(np.degrees(np.arccos(np.clip(N[..., 2] * 2 - 1, -1, 1))).mean()), 2),
               photo_light=light_fit(mm.srgb_to_lin(D), N),
               banding=dict(N_x=banding(nx), N_y=banding(ny), R=banding(R), D=banding(mm.luma(mm.srgb_to_lin(D))),
                            H=banding(H)),
               lowfreq_N=round(float(np.sqrt(mm.gblur(nx, n / 40) ** 2 + mm.gblur(ny, n / 40) ** 2).mean()), 4),
               R_stats=dict(mean=round(float(R.mean()), 3), std=round(float(R.std()), 3),
                            p5=round(float(np.percentile(R, 5)), 3), p95=round(float(np.percentile(R, 95)), 3)),
               D_lin_mean=mm.lin_hex(mm.srgb_to_lin(D).reshape(-1, 3).mean(0)),
               D_srgb_mean_luma=round(float(mm.luma(D).mean()), 4),
               D_lin_luma_std_rel=round(float(mm.luma(mm.srgb_to_lin(D)).std() / mm.luma(mm.srgb_to_lin(D)).mean()), 3))
    if kind == 'thatch':
        res['course'] = course_profile(D, H, n)
    print('CHECK', json.dumps(res))
    return res


def check_procedural(setname):
    """Same numbers for an art/textures procedural set (v-up rows in the PNG are stored top-down like any PNG)."""
    d = os.path.join(ROOT, 'art', 'textures', setname.lower())
    T = setname.capitalize()
    D = load(os.path.join(d, 'T_%s_BC.png' % T))[..., :3]
    N = load(os.path.join(d, 'T_%s_N.png' % T))[..., :3]
    ORM = load(os.path.join(d, 'T_%s_ORM.png' % T))[..., :3]
    nx, ny = N[..., 0] * 2 - 1, N[..., 1] * 2 - 1
    R = ORM[..., 1]
    res = dict(name='procedural_' + setname,
               normal_tilt_deg_mean=round(float(np.degrees(np.arccos(np.clip(N[..., 2] * 2 - 1, -1, 1))).mean()), 2),
               photo_light=light_fit(mm.srgb_to_lin(D), N),
               lowfreq_N=round(float(np.sqrt(mm.gblur(nx, D.shape[0] / 40) ** 2 + mm.gblur(ny, D.shape[0] / 40) ** 2).mean()), 4),
               R_stats=dict(mean=round(float(R.mean()), 3), std=round(float(R.std()), 3),
                            p5=round(float(np.percentile(R, 5)), 3), p95=round(float(np.percentile(R, 95)), 3)),
               D_lin_mean=mm.lin_hex(mm.srgb_to_lin(D).reshape(-1, 3).mean(0)),
               D_srgb_mean_luma=round(float(mm.luma(D).mean()), 4),
               D_lin_luma_std_rel=round(float(mm.luma(mm.srgb_to_lin(D)).std() / mm.luma(mm.srgb_to_lin(D)).mean()), 3))
    print('CHECK', json.dumps(res))


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    flags = dict(a[2:].split('=', 1) for a in argv if a.startswith('--') and '=' in a)
    names = [a for a in argv if not a.startswith('--')] or ['StoneA', 'StoneB', 'ThatchA', 'ThatchB']
    out_dir = flags.get('out', os.path.join(PILOT, 'out'))
    for nm in names:
        check_set(nm, out_dir, 'thatch' if nm.startswith('Thatch') else 'stone')
    if '--procedural' in argv:
        for s in ('rubble', 'thatch', 'limestone'):
            check_procedural(s)


main()
