# DEMIURGE art - Age2 (Iron Age) procedural texture sets, built on art/tools/dmtex.py.
#
# Every set is 1024^2, periodic (tiles seamlessly), seeded, and written as
#   T_Age2_<Set>_D.png (sRGB base colour), _N.png (OpenGL tangent normal), _ORM.png (linear AO/rough/metal)
# plus T_Age2_Trim_M.png (mask: R = night-window emissive).
# Tile sizes (metres covered by one 0..1 UV tile) are in TILE and are what the mesh UVs are scaled by.
import math
import os
import numpy as np
import dmtex as dt

N = 1024

# metres per UV tile (u, v) for the tileable sets; the trim sheet tiles in u only (TRIM_U metres per u)
TILE = {
    'Ashlar': 4.0, 'Rubble': 3.0, 'Limestone': 4.0,
    'Thatch': 3.0, 'Shingle': 2.0, 'TileClay': 2.0, 'TileStone': 2.0, 'Slate': 2.0,
}
TRIM_U = 2.0

# trim sheet bands: name -> (v0, v1) in 0..1 (v up). Inset by half a texel row band edge when sampling.
BANDS = {
    'timber':  (0 / 1024, 128 / 1024),
    'plank':   (128 / 1024, 256 / 1024),
    'window':  (256 / 1024, 384 / 1024),   # 16 cells of 64 x 128 px
    'cloth_o': (384 / 1024, 448 / 1024),
    'cloth_r': (448 / 1024, 512 / 1024),
    'cloth_i': (512 / 1024, 576 / 1024),
    'iron':    (576 / 1024, 608 / 1024),
    'bronze':  (608 / 1024, 672 / 1024),
    'plaster': (672 / 1024, 720 / 1024),
    'dressed': (720 / 1024, 768 / 1024),
    'hedge':   (768 / 1024, 832 / 1024),
    'water':   (832 / 1024, 864 / 1024),
    'goods':   (864 / 1024, 928 / 1024),
    'earth':   (928 / 1024, 960 / 1024),
    'char':    (960 / 1024, 1024 / 1024),
}
WINDOW_CELLS = 16


def L(h):
    return dt.hex_lin(h)


# --------------------------------------------------------------------------- block layouts


def row_blocks(T, nrows, lmin, lmax, hjit=0.25, seed=0):
    """Running-bond courses. Returns dict of (n,n) arrays: id, ex, ey (distance to nearest vertical /
    horizontal joint in tile units), lu, lv (0..1 inside block), bw, bh (block size, tile units)."""
    rng = np.random.default_rng(seed)
    n = T.n
    hs = 1.0 + (rng.random(nrows) - 0.5) * 2 * hjit
    hs = hs / hs.sum()
    rb = np.concatenate([[0.0], np.cumsum(hs)])
    u1 = (np.arange(n, dtype=np.float32) + 0.5) / n
    out = {k: np.zeros((n, n), np.float32) for k in ('ex', 'ey', 'lu', 'lv', 'bw', 'bh')}
    ids = np.zeros((n, n), np.int32)
    vv = (np.arange(n, dtype=np.float32) + 0.5) / n
    bid = 0
    for r in range(nrows):
        widths = []
        tot = 0.0
        while tot < 1.0:
            w = rng.uniform(lmin, lmax)
            widths.append(w)
            tot += w
        widths = np.array(widths) / tot
        cuts = np.concatenate([[0.0], np.cumsum(widths)])
        off = rng.random()
        up = (u1 - off) % 1.0
        k = np.clip(np.searchsorted(cuts, up, side='right') - 1, 0, len(widths) - 1)
        left = cuts[k]
        right = cuts[k + 1]
        ex = np.minimum(up - left, right - up)
        lu = (up - left) / (right - left)
        rows = np.where((vv >= rb[r]) & (vv < rb[r + 1]))[0]
        if len(rows) == 0:
            continue
        lv = (vv[rows] - rb[r]) / (rb[r + 1] - rb[r])
        ey = np.minimum(vv[rows] - rb[r], rb[r + 1] - vv[rows])
        out['ex'][rows, :] = ex[None, :]
        out['ey'][rows, :] = ey[:, None]
        out['lu'][rows, :] = lu[None, :]
        out['lv'][rows, :] = lv[:, None]
        out['bw'][rows, :] = (right - left)[None, :]
        out['bh'][rows, :] = hs[r]
        ids[rows, :] = (bid + k)[None, :]
        bid += len(widths)
    out['id'] = ids
    out['count'] = bid
    return out


def per_id(ids, count, seed, lo=0.0, hi=1.0):
    r = np.random.default_rng(seed).random(count + 1).astype(np.float32)
    return (lo + (hi - lo) * r)[ids]


def finish(name, outdir, alb, h, metres, rough, metal=None, ao_r=0.05, ao_s=8.0, nstrength=1.0, mask=None):
    n = alb.shape[0]
    nrm = dt.normal_from_height(h * nstrength, metres, n)
    ao = dt.cavity_ao(h, n, ao_r, metres, ao_s)
    if metal is None:
        metal = np.zeros_like(rough)
    orm = np.stack([ao, np.clip(rough, 0.02, 1), np.clip(metal, 0, 1)], -1)
    base = os.path.join(outdir, 'T_Age2_%s' % name)
    dt.save_png(base + '_D.png', np.clip(alb, 0, 1))
    dt.save_png(base + '_N.png', nrm, srgb=False)
    dt.save_png(base + '_ORM.png', orm, srgb=False)
    if mask is not None:
        dt.save_png(base + '_M.png', mask, srgb=False)
    return dt.stats(name, alb)


def streaks(T, amount, px=48):
    """Vertical rain streaks: noise stretched along v."""
    s = T.fbm(px, 2, 4)
    return np.clip(0.5 + s * 1.6, 0, 1) * amount


# --------------------------------------------------------------------------- stone


def tex_ashlar(outdir, seed=21):
    """Grey fitted wall blocks (town walls, towers, gatehouse). 4 m tile, 7 courses."""
    T = dt.Tex(N, seed)
    m = TILE['Ashlar']
    b = row_blocks(T, 8, 0.4 / m, 1.7 / m, 0.38, seed + 1)
    mort = 0.012 / m
    bev = 0.06 / m
    chip = T.fbm(48, 48, 4) * (0.025 / m)
    e = np.minimum(b['ex'] * (1.0 + 0.25 * T.fbm(16, 16, 2)), b['ey']) + chip
    face = dt.sstep(mort * 0.5, mort * 0.5 + bev, e)
    pil = (np.sin(np.pi * b['lu']) * np.sin(np.pi * b['lv'])) ** 0.5
    chis = T.fbm(64, 64, 4)
    rnd = per_id(b['id'], b['count'], seed + 2)
    tilt = per_id(b['id'], b['count'], seed + 3, -1, 1)
    h = face * (0.025 + 0.012 * pil + 0.008 * tilt * (b['lu'] - 0.5)) + face * chis * 0.004
    h += dt.sstep(0.6, 1.0, T.fbm(8, 8, 3) * 0.5 + 0.5) * -0.004 * face   # spalled patches
    base = L('#85827b')
    warm = L('#8f8573')
    cool = L('#767878')
    tint = per_id(b['id'], b['count'], seed + 4)
    alb = dt.mix3(dt.mix3(cool, base, np.clip(tint * 2, 0, 1)), warm, np.clip(tint * 2 - 1, 0, 1))
    alb = alb * (0.8 + 0.26 * rnd)[..., None]
    alb = alb * (0.9 + 0.16 * chis)[..., None]
    alb = alb * (0.8 + 0.2 * dt.sstep(0.0, 0.08 / m, e))[..., None]     # dirt toward the edges
    lich = dt.sstep(0.55, 0.8, T.fbm(16, 16, 5) * 0.5 + 0.5) * face
    alb = dt.mix3(alb, L('#8f8e6c'), lich * 0.45)
    st = streaks(T, 1.0)
    alb = alb * (1.0 - 0.18 * st * (0.5 + 0.5 * T.fbm(4, 4, 2)))[..., None]
    alb = dt.mix3(L('#6a655c'), alb, face)        # joints
    rough = 0.88 + 0.08 * (1 - face) + 0.04 * chis
    return finish('Ashlar', outdir, alb, h, m, rough, ao_r=0.06, ao_s=14.0)


def tex_rubble(outdir, seed=31):
    """Rubble stone in clay-lime mortar (houses). 3 m tile, stones ~0.3 x 0.2 m."""
    T = dt.Tex(N, seed)
    m = TILE['Rubble']
    gx, gy = 11, 16
    wu = T.fbm(6, 6, 3) * 0.012
    wv = T.fbm(6, 6, 3) * 0.012
    f1, f2, idc, cu, cv = T.worley(gx, gy, T.u + wu, T.v + wv, 0.85, gy / gx)
    edge = f2 - f1
    face = dt.sstep(0.06, 0.32, edge)
    rnd = T.cell_rand(idc, gx * gy, 1)
    rnd2 = T.cell_rand(idc, gx * gy, 2)
    det = T.fbm(80, 80, 4)
    h = face * (0.03 + 0.03 * rnd) * (0.6 + 0.4 * dt.sstep(0.0, 0.6, edge)) + det * 0.004 * face
    stone = dt.mix3(L('#6f6455'), L('#a3937a'), rnd)
    stone = dt.mix3(stone, L('#7d7466'), rnd2 * 0.5)
    stone = stone * (0.9 + 0.16 * det)[..., None]
    mortar = L('#9b8a6c') * (0.85 + 0.2 * T.fbm(40, 40, 3))[..., None]
    alb = dt.mix3(mortar, stone, face)
    alb = alb * (1.0 - 0.14 * streaks(T, 1.0, 40))[..., None]
    moss = dt.sstep(0.6, 0.85, T.fbm(10, 10, 5) * 0.5 + 0.5) * (1 - face * 0.5)
    alb = dt.mix3(alb, L('#5f6640'), moss * 0.5)
    rough = 0.86 + 0.1 * (1 - face)
    return finish('Rubble', outdir, alb, h, m, rough, ao_r=0.04, ao_s=18.0)


def tex_limestone(outdir, seed=41):
    """Pale dressed limestone (temple, academy, market). 4 m tile, 6 courses of long blocks."""
    T = dt.Tex(N, seed)
    m = TILE['Limestone']
    b = row_blocks(T, 6, 1.0 / m, 2.4 / m, 0.18, seed + 1)
    mort = 0.008 / m
    bev = 0.02 / m
    e = np.minimum(b['ex'], b['ey'])
    face = dt.sstep(mort * 0.5, mort * 0.5 + bev, e)
    rnd = per_id(b['id'], b['count'], seed + 2)
    fine = T.fbm(96, 96, 4)
    h = face * (0.012 + 0.002 * rnd) + face * fine * 0.0015
    alb = dt.mix3(L('#a69879'), L('#bfb296'), rnd)
    alb = alb * (0.95 + 0.07 * fine)[..., None]
    blot = T.fbm(6, 6, 4) * 0.5 + 0.5
    alb = alb * (0.92 + 0.1 * blot)[..., None]
    alb = alb * (1.0 - 0.12 * streaks(T, 1.0, 56) * dt.sstep(0.3, 0.8, blot))[..., None]
    alb = dt.mix3(L('#9a8e76'), alb, 0.55 + 0.45 * face)
    rough = 0.72 + 0.12 * (1 - face) + 0.06 * fine
    return finish('Limestone', outdir, alb, h, m, rough, ao_r=0.03, ao_s=20.0)


# --------------------------------------------------------------------------- roofs


def tex_thatch(outdir, seed=51):
    """Reed / straw thatch, grain down the slope (v). 3 m tile, courses every 0.3 m."""
    T = dt.Tex(N, seed)
    m = TILE['Thatch']
    warp = T.fbm(8, 4, 3) * 0.02
    strands = T.value(420, 7, T.u + warp, T.v) * 0.5 + T.value(840, 14, T.u + warp, T.v) * 0.35 + T.value(210, 4, T.u, T.v) * 0.3
    courses = 10
    c = (T.v * courses + T.fbm(12, 3, 2) * 0.35) % 1.0
    lump = T.fbm(10, 6, 4)
    fine = T.value(1024, 40, T.u + warp, T.v) * 0.6 + T.value(512, 20, T.u + warp, T.v) * 0.4
    h = 0.006 * c ** 0.6 + strands * 0.012 + fine * 0.006 + lump * 0.03
    base = dt.mix3(L('#56483a'), L('#86704f'), np.clip(0.5 + strands * 0.6 + fine * 0.5, 0, 1))
    base = base * (0.86 + 0.14 * c ** 0.7)[..., None] * (0.85 + 0.25 * (lump * 0.5 + 0.5))[..., None]
    grey = T.fbm(5, 5, 4) * 0.5 + 0.5
    base = dt.mix3(base, L('#69645a'), dt.sstep(0.3, 0.8, grey) * 0.6)
    moss = dt.sstep(0.62, 0.85, T.fbm(7, 7, 5) * 0.5 + 0.5)
    base = dt.mix3(base, L('#4f5532'), moss * 0.6)
    rough = 0.93 + 0.05 * strands
    return finish('Thatch', outdir, base, h, m, rough, ao_r=0.05, ao_s=10.0)


def tex_shingle(outdir, seed=61):
    """Dark split-wood shingles. 2 m tile, 8 rows; butts face down-slope (low v)."""
    T = dt.Tex(N, seed)
    m = TILE['Shingle']
    b = row_blocks(T, 8, 0.12 / m, 0.3 / m, 0.06, seed + 1)
    gap = dt.sstep(0.002, 0.006, b['ex'])
    lv = b['lv']
    rnd = per_id(b['id'], b['count'], seed + 2)
    grain = T.value(300, 10) * 0.6 + T.value(600, 20) * 0.4
    h = gap * (0.022 * (1 - lv) + 0.004 * rnd + grain * 0.0015)
    alb = dt.mix3(L('#3f342b'), L('#6e6457'), rnd)
    alb = alb * (0.85 + 0.25 * grain)[..., None]
    alb = alb * (0.7 + 0.3 * dt.sstep(0.0, 0.25, lv))[..., None]   # shadowed under the butt above
    moss = dt.sstep(0.62, 0.86, T.fbm(8, 8, 5) * 0.5 + 0.5)
    alb = dt.mix3(alb, L('#575a38'), moss * 0.5)
    alb = alb * (0.35 + 0.65 * gap)[..., None]
    rough = 0.82 + 0.1 * grain
    return finish('Shingle', outdir, alb, h, m, rough, ao_r=0.03, ao_s=24.0)


def tex_slate(outdir, seed=71):
    """Blue-grey slates (academy). 2 m tile, 10 rows."""
    T = dt.Tex(N, seed)
    m = TILE['Slate']
    b = row_blocks(T, 10, 0.24 / m, 0.36 / m, 0.04, seed + 1)
    gap = dt.sstep(0.002, 0.005, b['ex'])
    lv = b['lv']
    rnd = per_id(b['id'], b['count'], seed + 2)
    fine = T.fbm(120, 120, 3)
    h = gap * (0.012 * (1 - lv) + 0.003 * rnd + fine * 0.001)
    alb = dt.mix3(L('#3e4043'), L('#5c5f62'), rnd)
    alb = alb * (0.9 + 0.12 * fine)[..., None]
    alb = alb * (0.75 + 0.25 * dt.sstep(0.0, 0.2, lv))[..., None]
    rust = dt.sstep(0.7, 0.9, T.fbm(9, 9, 5) * 0.5 + 0.5)
    alb = dt.mix3(alb, L('#7a6a52'), rust * 0.35)
    alb = alb * (0.4 + 0.6 * gap)[..., None]
    rough = 0.74 + 0.12 * fine + 0.1 * (1 - gap)
    return finish('Slate', outdir, alb, h, m, rough, ao_r=0.03, ao_s=24.0)


def _barrel(T, cols, rows, cover_frac, seed, pale=False):
    u = T.u * cols
    v = T.v * rows
    ci = np.floor(u).astype(np.int32)
    fu = u - ci
    # each column = pan (concave, wide) + cover (convex, narrow) at the column seam
    d = np.abs(fu - 0.0)
    d = np.minimum(d, 1.0 - fu)                     # distance to the seam (0..0.5)
    cov = np.clip(1.0 - d / cover_frac, 0, 1)
    cover = np.sqrt(np.clip(1 - (d / cover_frac) ** 2, 0, 1)) * (d < cover_frac)
    pan = 0.25 * (1 - np.cos(np.pi * 2 * fu))   # low in the middle of the pan... flipped below
    pan = 0.35 - 0.35 * np.sin(np.pi * np.clip((fu - cover_frac * 0.5) / (1 - cover_frac), 0, 1))
    ri_off = (np.floor(v + 0.5 * (ci % 2) * 0) ).astype(np.int32)
    fv = v - np.floor(v)
    step = 1.0 - fv                                # each row thicker at its lower edge
    h = np.maximum(pan * 0.6, cover) * 0.05 + step * 0.012
    tid = (ri_off * cols + ci) * 2 + (cov > 0).astype(np.int32)
    return h, tid, cover, fv, cov


def tex_tileclay(outdir, seed=81):
    """Clay pan-and-cover roof tiles (market colonnade, gatehouse, towers). 2 m tile."""
    T = dt.Tex(N, seed)
    m = TILE['TileClay']
    cols, rows = 8, 5
    h, tid, cover, fv, cov = _barrel(T, cols, rows, 0.28, seed)
    cnt = cols * rows * 2 + 2
    rnd = per_id(np.clip(tid, 0, cnt - 1), cnt, seed + 1)
    fine = T.fbm(100, 100, 3)
    alb = dt.mix3(L('#7e432c'), L('#a8653f'), rnd)
    alb = dt.mix3(alb, L('#5a3324'), (rnd > 0.88).astype(np.float32) * 0.6)
    alb = alb * (0.88 + 0.14 * fine)[..., None]
    alb = alb * (0.65 + 0.35 * dt.sstep(0.0, 0.18, fv))[..., None]
    alb = alb * (0.75 + 0.25 * np.clip(cover + (1 - cov) * 0.9, 0, 1))[..., None]
    lich = dt.sstep(0.68, 0.88, T.fbm(10, 10, 5) * 0.5 + 0.5)
    alb = dt.mix3(alb, L('#a9a68e'), lich * 0.4)
    h = h + fine * 0.001
    rough = 0.78 + 0.1 * fine
    return finish('TileClay', outdir, alb, h, m, rough, ao_r=0.04, ao_s=18.0)


def tex_tilestone(outdir, seed=91):
    """Pale stone roof slabs with narrow cover strips (temple). 2 m tile."""
    T = dt.Tex(N, seed)
    m = TILE['TileStone']
    cols, rows = 4, 3
    u = T.u * cols
    v = T.v * rows
    ci = np.floor(u).astype(np.int32)
    fu = u - ci
    fv = v - np.floor(v)
    ri = np.floor(v).astype(np.int32)
    d = np.minimum(fu, 1 - fu)
    cw = 0.09
    cover = np.sqrt(np.clip(1 - (d / cw) ** 2, 0, 1)) * (d < cw)
    h = cover * 0.04 + (1 - fv) * 0.01
    rnd = per_id((ri % rows) * cols + ci % cols, rows * cols, seed + 1)
    fine = T.fbm(90, 90, 4)
    alb = dt.mix3(L('#b9ab8e'), L('#d3c7ad'), rnd)
    alb = alb * (0.92 + 0.1 * fine)[..., None]
    alb = alb * (0.8 + 0.2 * dt.sstep(0.0, 0.12, fv))[..., None]
    alb = alb * (0.85 + 0.15 * (1 - (d < cw * 1.2) * (1 - cover)))[..., None]
    stain = dt.sstep(0.55, 0.9, T.fbm(6, 6, 5) * 0.5 + 0.5)
    alb = dt.mix3(alb, L('#8f866f'), stain * 0.35)
    rough = 0.7 + 0.1 * fine
    return finish('TileStone', outdir, alb, h + fine * 0.001, m, rough, ao_r=0.04, ao_s=16.0)


# --------------------------------------------------------------------------- trim sheet


def tex_trim(outdir, seed=101):
    T = dt.Tex(N, seed)
    n = N
    alb = np.zeros((n, n, 3), np.float32)
    h = np.zeros((n, n), np.float32)
    rough = np.full((n, n), 0.8, np.float32)
    metal = np.zeros((n, n), np.float32)
    mask = np.zeros((n, n, 3), np.float32)
    u = T.u

    def band(name):
        v0, v1 = BANDS[name]
        r0, r1 = int(round(v0 * n)), int(round(v1 * n))
        lv = (T.v[r0:r1] - v0) / (v1 - v0)
        return slice(r0, r1), lv

    grain_a = T.value(200, 600) * 0.5 + T.value(400, 1200) * 0.3      # along u (rows)
    grain_v = T.value(160, 4) * 0.5 + T.value(320, 8) * 0.3           # along v

    # timber: oak beam, grain along u
    s, lv = band('timber')
    g = grain_a[s]
    knot = dt.sstep(0.75, 0.95, T.fbm(24, 6, 3)[s] * 0.5 + 0.5)
    edge = dt.sstep(0.0, 0.08, np.minimum(lv, 1 - lv))
    a = dt.mix3(L('#3d2c1e'), L('#6e543d'), np.clip(0.5 + g, 0, 1))
    a = dt.mix3(a, L('#7d766a'), dt.sstep(0.3, 0.9, T.fbm(6, 3, 3)[s] * 0.5 + 0.5) * 0.45)  # silvered
    a = a * (1 - 0.4 * knot)[..., None] * (0.75 + 0.25 * edge)[..., None]
    alb[s] = a
    h[s] = g * 0.004 + edge * 0.01 - knot * 0.002
    rough[s] = 0.78 + 0.1 * g

    # planks: vertical boards (door, hoarding, gates); seams every 1/16 u (0.125 m), iron straps
    s, lv = band('plank')
    bu = (u[s] * 16) % 1.0
    seam = dt.sstep(0.0, 0.06, np.minimum(bu, 1 - bu))
    bid = np.floor(u[s] * 16).astype(np.int32)
    br = per_id(bid % 16, 16, seed + 5)
    g = grain_v[s]
    a = dt.mix3(L('#3b2b1f'), L('#5e4835'), np.clip(br * 0.8 + g * 0.6, 0, 1))
    strap = ((np.abs(lv - 0.22) < 0.05) | (np.abs(lv - 0.78) < 0.05)).astype(np.float32)
    a = dt.mix3(a, L('#2a2623'), strap * 0.9)
    alb[s] = a * (0.4 + 0.6 * seam)[..., None]
    h[s] = seam * 0.006 + g * 0.002 + strap * 0.004
    rough[s] = 0.8 - strap * 0.25
    metal[s] = strap * 0.6

    # windows: 16 cells (64 x 128 px): timber frame, dark interior with a mullion; mask = glass/opening
    s, lv = band('window')
    cu = (u[s] * WINDOW_CELLS) % 1.0
    ci = np.floor(u[s] * WINDOW_CELLS).astype(np.int32)
    fr = 0.13
    inner = (cu > fr) & (cu < 1 - fr) & (lv > fr * 0.5) & (lv < 1 - fr * 0.5)
    mull = (np.abs(cu - 0.5) < 0.035) | (np.abs(lv - 0.55) < 0.02)
    open_ = inner & ~mull
    shutter = per_id(ci % WINDOW_CELLS, WINDOW_CELLS, seed + 7) > 0.5
    glow = np.clip(1.0 - np.abs(lv - 0.45) * 1.2, 0, 1)
    a = np.zeros(cu.shape + (3,), np.float32)
    frame = dt.mix3(L('#3c2c1f'), L('#5a4532'), np.clip(0.5 + grain_v[s], 0, 1))
    a[:] = frame
    interior = dt.mix3(L('#0f0c09'), L('#2b2016'), glow * 0.6)
    a = np.where(open_[..., None], interior, a)
    # half-open shutter leaf on one side of some cells
    leaf = shutter & (cu > 1 - 0.32) & inner
    a = np.where(leaf[..., None], frame * 0.9, a)
    alb[s] = a
    h[s] = (~open_).astype(np.float32) * 0.01 + leaf * 0.005
    rough[s] = np.where(open_, 0.5, 0.8)
    mask[s, :, 0] = (open_ & ~leaf).astype(np.float32) * (0.6 + 0.4 * glow)

    # cloth stripes (awnings): stripes along v (vary with u), 0.25 m wide
    for nm, col in (('cloth_o', '#c08a2c'), ('cloth_r', '#8e2b20'), ('cloth_i', '#24375e')):
        s, lv = band(nm)
        st = ((u[s] * TRIM_U / 0.25) % 2.0) < 1.0
        weave = T.value(512, 64)[s] * 0.5 + 0.5
        sag = 0.85 + 0.15 * np.sin(np.pi * lv)
        a = np.where(st[..., None], L(col), L('#d8ccb0'))
        a = a * (0.9 + 0.1 * weave)[..., None] * sag[..., None]
        dirt = dt.sstep(0.4, 0.9, T.fbm(8, 2, 3)[s] * 0.5 + 0.5)
        a = dt.mix3(a, a * 0.7, dirt * 0.4)
        alb[s] = a
        h[s] = weave * 0.001
        rough[s] = 0.92

    # iron
    s, lv = band('iron')
    rust = dt.sstep(0.4, 0.8, T.fbm(30, 4, 4)[s] * 0.5 + 0.5)
    alb[s] = dt.mix3(L('#2e2c2a'), L('#6b4228'), rust)
    rough[s] = 0.55 + 0.35 * rust
    metal[s] = 0.85 * (1 - rust)
    h[s] = rust * 0.002

    # bronze verdigris (dome, armillary, fittings)
    s, lv = band('bronze')
    ver = T.fbm(40, 10, 5)[s] * 0.5 + 0.5
    st2 = streaks(T, 1.0, 60)[s]
    a = dt.mix3(L('#4f8a74'), L('#6fa48a'), ver)
    a = dt.mix3(a, L('#3b5a48'), st2 * 0.5)
    a = dt.mix3(a, L('#7a5a32'), dt.sstep(0.85, 0.97, ver) * 0.7)
    alb[s] = a
    rough[s] = 0.55 + 0.2 * ver
    metal[s] = 0.15 + 0.3 * dt.sstep(0.85, 0.97, ver)
    h[s] = ver * 0.002

    # lime plaster
    s, lv = band('plaster')
    p = T.fbm(30, 6, 5)[s]
    a = L('#d2c5a8') * (0.9 + 0.12 * p)[..., None]
    a = dt.mix3(a, L('#a89a7e'), dt.sstep(0.3, 0.9, (1 - lv) * (0.5 + 0.5 * T.fbm(10, 2, 3)[s])) * 0.5)
    alb[s] = a
    rough[s] = 0.9
    h[s] = p * 0.003

    # dressed pale stone (column shafts, lintels): smooth, faint horizontal drum joints every 1/4 band
    s, lv = band('dressed')
    p = T.fbm(40, 4, 5)[s]
    a = L('#c2b498') * (0.9 + 0.1 * p)[..., None]
    jt = np.abs(((lv * 4.0) % 1.0) - 0.5) > 0.47
    a = np.where(jt[..., None], a * 0.82, a)
    a = a * (0.85 + 0.15 * lv)[..., None]
    alb[s] = a
    rough[s] = 0.7
    h[s] = p * 0.002 - jt * 0.002

    # hedge / foliage
    s, lv = band('hedge')
    f1, f2, idc, _, _ = T.worley(96, 6)
    leaf = (f1[s] < 0.45).astype(np.float32)
    fr2 = T.fbm(64, 4, 4)[s] * 0.5 + 0.5
    alb[s] = dt.mix3(L('#22341a'), L('#4f6a2c'), fr2) * (0.6 + 0.4 * (1 - f1[s]))[..., None]
    rough[s] = 0.85
    h[s] = (1 - f1[s]) * 0.02

    # water
    s, lv = band('water')
    w = T.fbm(40, 4, 4)[s] * 0.5 + 0.5
    alb[s] = dt.mix3(L('#1f3a3a'), L('#3a5c58'), w)
    rough[s] = 0.08
    h[s] = w * 0.002

    # goods: produce, pots, sacks in colourful blobs
    s, lv = band('goods')
    f1, f2, idc, _, _ = T.worley(64, 4)
    rr = T.cell_rand(idc, 64 * 4, 9)[s]
    pal = np.stack([L('#8e2b20'), L('#c08a2c'), L('#5d7a2a'), L('#a0562c'), L('#b9a77c'), L('#6a4a7a'), L('#7c3a22')], 0)
    k = np.clip((rr * len(pal)).astype(np.int32), 0, len(pal) - 1)
    a = pal[k] * (0.6 + 0.4 * (1 - f1[s]))[..., None]
    a = np.where((f1[s] > 0.6)[..., None], L('#2a2017'), a)
    alb[s] = a
    rough[s] = 0.7
    h[s] = (1 - f1[s]) * 0.03

    # earth / gravel paths
    s, lv = band('earth')
    e = T.fbm(80, 4, 5)[s] * 0.5 + 0.5
    alb[s] = dt.mix3(L('#6e5b42'), L('#9a8566'), e)
    rough[s] = 0.95
    h[s] = e * 0.004

    # charred timber
    s, lv = band('char')
    c1, c2, idc, _, _ = T.worley(48, 4, aspect=0.5)
    crack = dt.sstep(0.0, 0.08, c2[s] - c1[s])
    alb[s] = dt.mix3(L('#0d0b0a'), L('#2b2622'), (T.fbm(60, 8, 4)[s] * 0.5 + 0.5)) * (0.3 + 0.7 * crack)[..., None]
    rough[s] = 0.95
    h[s] = crack * 0.01

    return finish('Trim', outdir, alb, h, TRIM_U, rough, metal, ao_r=0.02, ao_s=20.0, mask=mask)


ALL = {
    'Ashlar': tex_ashlar, 'Rubble': tex_rubble, 'Limestone': tex_limestone, 'Thatch': tex_thatch,
    'Shingle': tex_shingle, 'Slate': tex_slate, 'TileClay': tex_tileclay, 'TileStone': tex_tilestone,
    'Trim': tex_trim,
}


def build_all(outdir, only=None):
    os.makedirs(outdir, exist_ok=True)
    msgs = []
    for k, f in ALL.items():
        if only and k not in only:
            continue
        msgs.append(f(outdir))
    return msgs
