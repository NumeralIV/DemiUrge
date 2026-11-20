# DEMIURGE tree families: procedural bark textures (albedo + DirectX normal), painted with numpy, tileable.
#
# One tile covers BARK_TILE[species] metres (u around the stem, v along it); the mesh generator writes bark UVs
# in tile units so the materials need no tiling parameters (BarkTileU = BarkTileV = 1).
import numpy as np
from . import imaging as im

SIZE = 1024
# metres covered by one texture tile (around, along)
BARK_TILE = {
    'broadleaf': (0.9, 1.4),
    'conifer': (0.8, 1.2),
    'palm': (1.0, 1.0),
    'acacia': (0.8, 1.2),
    'shrub': (0.35, 0.5),
}


def _mix(a, b, t):
    t = np.asarray(t, np.float32)[..., None]
    return a * (1 - t) + b * t


def _col(rgb255):
    return np.array(rgb255, np.float32) / 255.0


def paint(species, seed=7):
    """Returns (albedo [S,S,4] sRGB, normal [S,S,4] linear) for one species."""
    S = SIZE
    if species in ('broadleaf', 'shrub'):
        smooth = species == 'shrub'
        # vertical fissured ridges (oak / old beech mix), features elongated along v (= image y)
        n1 = im.fft_noise(S, S, seed, beta=2.2, ax=1.0, ay=6.0, kmax=180)
        n2 = im.fft_noise(S, S, seed + 1, beta=1.6, ax=1.0, ay=2.0, kmax=400)
        n3 = im.fft_noise(S, S, seed + 2, beta=1.2, kmax=500)
        ridge = 1.0 - np.abs(np.tanh(n1 * 0.9 + 0.35 * n2))
        if smooth:
            ridge = 0.6 + 0.4 * ridge
        hgt = ridge ** (1.6 if not smooth else 1.0) + 0.08 * n3
        fiss = im.smoothstep(0.35, 0.05, ridge)                 # deep cracks
        lichen = im.smoothstep(0.9, 1.6, im.fft_noise(S, S, seed + 3, beta=3.0, kmax=60)) * (0.35 if not smooth else 0.15)
        c_ridge = _col((128, 120, 106))
        c_mid = _col((92, 84, 72))
        c_fiss = _col((40, 33, 27))
        col = _mix(c_mid, c_ridge, im.smoothstep(0.4, 0.95, ridge + 0.1 * n3))
        col = _mix(col, c_fiss, fiss * 0.85)
        col = _mix(col, _col((112, 124, 92)), lichen)
        col *= (1.0 + 0.06 * n2)[..., None]
        strength = 9.0 if not smooth else 4.0
    elif species == 'conifer':
        # spruce: thin scaly plates, reddish grey-brown, cracks between plates
        f1, f2, cid = im.worley(S, S, 22, 30, seed, stretch=(1.0, 1.35))
        edge = f2 - f1
        plate = im.smoothstep(0.02, 0.22, edge)
        n2 = im.fft_noise(S, S, seed + 4, beta=1.8, kmax=300)
        hgt = plate * (0.7 + 0.3 * cid) + 0.06 * n2
        c_a = _col((118, 86, 66))
        c_b = _col((86, 70, 60))
        c_gap = _col((38, 28, 22))
        col = _mix(c_b, c_a, cid * 0.8 + 0.1 * n2)
        col = _mix(c_gap, col, plate)
        resin = im.smoothstep(1.8, 2.4, im.fft_noise(S, S, seed + 5, beta=2.6, kmax=80)) * 0.25
        col = _mix(col, _col((140, 120, 96)), resin)
        strength = 7.0
    elif species == 'palm':
        # coconut: leaf-scar rings (horizontal bands) + vertical fibres
        yy = (np.arange(S, dtype=np.float32) + 0.5) / S
        rings_per_tile = 7
        wob = im.fft_noise(S, S, seed + 6, beta=2.5, ax=4.0, ay=1.0, kmax=40) * 0.012
        ph = (yy[:, None] + wob) * rings_per_tile
        frac = ph - np.floor(ph)
        ring = im.smoothstep(0.0, 0.12, frac) * im.smoothstep(0.3, 0.16, frac)   # raised lip at each scar
        groove = im.smoothstep(0.06, 0.0, frac) + im.smoothstep(0.92, 1.0, frac)
        fib = im.fft_noise(S, S, seed + 7, beta=1.4, ax=1.0, ay=10.0, kmax=500)
        hgt = 0.6 * ring - 0.5 * groove + 0.07 * fib
        c_a = _col((150, 140, 124))
        c_b = _col((112, 102, 88))
        col = _mix(c_b, c_a, np.clip(0.5 + 0.35 * fib + 0.3 * ring, 0, 1))
        col = _mix(col, _col((70, 60, 50)), np.clip(groove, 0, 1) * 0.7)
        stain = im.smoothstep(0.8, 2.0, im.fft_noise(S, S, seed + 8, beta=3.0, kmax=50)) * 0.3
        col = _mix(col, _col((90, 92, 80)), stain)
        strength = 6.0
    elif species == 'acacia':
        # umbrella thorn: dark, rough, deep longitudinal fissures with rusty inner bark
        n1 = im.fft_noise(S, S, seed + 9, beta=2.0, ax=1.0, ay=4.0, kmax=220)
        n2 = im.fft_noise(S, S, seed + 10, beta=1.3, kmax=500)
        ridge = 1.0 - np.abs(np.tanh(n1 * 1.2))
        hgt = ridge ** 1.3 + 0.12 * n2
        fiss = im.smoothstep(0.3, 0.02, ridge)
        col = _mix(_col((78, 70, 64)), _col((112, 102, 92)), im.smoothstep(0.35, 0.95, ridge + 0.15 * n2))
        col = _mix(col, _col((96, 54, 34)), fiss * 0.8)
        strength = 8.0
    else:
        raise ValueError(species)
    alb = np.concatenate([np.clip(col, 0, 1), np.ones((S, S, 1), np.float32)], axis=2).astype(np.float32)
    nrm = im.height_to_normal(hgt.astype(np.float32), strength)
    return alb, nrm


def write_all(out_dir, log=print):
    import os
    os.makedirs(out_dir, exist_ok=True)
    for sp in ('broadleaf', 'conifer', 'palm', 'acacia', 'shrub'):
        alb, nrm = paint(sp)
        im.save_png(os.path.join(out_dir, 'T_Bark_%s.png' % sp.capitalize()), alb)
        im.save_png(os.path.join(out_dir, 'T_Bark_%s_N.png' % sp.capitalize()), nrm)
        log('bark %s: mean albedo %s' % (sp, np.round(alb[..., :3].reshape(-1, 3).mean(0) * 255).astype(int).tolist()))
