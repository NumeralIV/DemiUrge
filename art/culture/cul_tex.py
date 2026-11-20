# DEMIURGE art/culture - procedural tileable texture sets for the three invented cultures.
#
# Runs inside Blender's Python (numpy). Uses the shared periodic-noise helpers in art/tools/dmtex.py
# (read-only). Every set is 1024^2, generated from a fixed seed, so a rebuild is bit-identical.
#
# Output per set (ASSETS.md naming): T_Culture_<Name>_D (sRGB albedo), _N (OpenGL normal, +Y),
# _ORM (linear: R occlusion, G roughness, B metallic) and, for the trim sheet only, _M (linear mask:
# R = night-window emissive, G = culture-accent tint mask, B = cloth/wind mask).
import os
import numpy as np
import dmtex as dt
from dmtex import hex_lin, mix3, sstep, blur, normal_from_height, cavity_ao

N = 1024

# metres covered by one tile (UV 0..1) - the mesh UVs use the same numbers (cul_geo.TILE)
TILE_M = {
    'PlasterA': 4.0, 'DomeA': 4.0, 'RubbleA': 4.0,
    'TimberB': 4.0, 'ShingleB': 4.0,
    'StoneC': 4.0, 'SlateC': 4.0, 'DryStoneC': 4.0,
    'Rock': 4.0, 'GardenB': 4.0,
    'Trim': 2.0,
}

# trim sheet: 8 horizontal bands of 128 px (band 0 at the bottom, UV v 0..0.125)
TRIM_BANDS = ['oak', 'tar', 'cobalt', 'saffron', 'rope', 'palestone', 'reed', 'window']


def _h01(a, b, k):
    """Integer hash of two int arrays -> float32 0..1 (no overflow)."""
    x = (a.astype(np.int64) * 73856093) ^ (b.astype(np.int64) * 19349663 + k * 83492791)
    x = (x ^ (x >> 13)) * 1274126177
    x = x ^ (x >> 16)
    return ((x & 0xffff).astype(np.float32) / 65535.0)


def _rough_noise(t, base, amp, px=8):
    return np.clip(base + amp * t.fbm(px, px, 4), 0.05, 1.0).astype(np.float32)


# --------------------------------------------------------------------------- culture A


def plaster_a(seed=101):
    """Smooth rose-ochre lime plaster with trowel undulation, hairline cracks, rain streaks, patches."""
    t = dt.Tex(N, seed)
    und = t.fbm(6, 6, 5)                                   # trowel waves
    grain = t.fbm(96, 96, 3)
    f1, f2, ids, _, _ = t.worley(7, 7, jitter=0.95)
    crack = 1.0 - sstep(0.0, 0.025, f2 - f1)               # thin lines along Voronoi edges
    crack *= sstep(0.3, 0.55, t.fbm(5, 5, 3) + 0.2) * 0.6  # only some edges cracked, faint
    patch = sstep(0.18, 0.32, t.fbm(3, 3, 4))               # repaired patches (re-plastered)
    streak = t.value(48, 3)                                 # vertical rain streaks (stretched along v)
    streak = sstep(0.1, 0.8, streak * 0.5 + 0.5 + 0.35 * t.fbm(24, 2, 2))
    h = und * 0.004 + grain * 0.0006 - crack * 0.0015 + patch * 0.0012
    base = hex_lin('#c48f6c')
    warm = hex_lin('#cc9776')
    rose = hex_lin('#b77a5f')
    dirt = hex_lin('#7d5644')
    m = sstep(-0.4, 0.5, t.fbm(4, 4, 5))
    col = mix3(rose, warm, m)
    col = mix3(col, base, 0.6 * np.ones_like(m))
    col = mix3(col, hex_lin('#cf9f80'), patch * 0.25)
    col = mix3(col, dirt, streak * 0.16 + crack * 0.35)
    col *= (1.0 + grain[..., None] * 0.06)
    ao = cavity_ao(h, N, 0.02, TILE_M['PlasterA'], 300.0)
    rough = _rough_noise(t, 0.86, 0.06) - patch * 0.04
    return col, h, ao, rough, None


def dome_a(seed=102):
    """Lime-washed dome plaster: lighter rose-cream, brush strokes, dark run-off streaks down the slope."""
    t = dt.Tex(N, seed)
    brush = t.fbm(3, 40, 3)                                 # strokes running along u (around the dome)
    und = t.fbm(5, 5, 5)
    runs = sstep(0.25, 0.9, t.value(64, 2) * 0.5 + 0.5)      # run-off streaks along v (down the dome)
    lichen_f1, _, _, _, _ = t.worley(40, 40, jitter=1.0)
    lichen = (1.0 - sstep(0.0, 0.3, lichen_f1)) * sstep(0.35, 0.6, t.fbm(6, 6, 3) + 0.3) * 0.5
    peel = sstep(0.5, 0.56, t.fbm(10, 10, 5) + 0.12) * 0.7        # limewash flaking off to the plaster below
    h = und * 0.003 + brush * 0.0006 - peel * 0.0008
    wash = hex_lin('#cf9f80')
    under = hex_lin('#b4785a')
    col = mix3(wash, hex_lin('#dbb291'), sstep(-0.3, 0.6, t.fbm(4, 4, 4)))
    col = mix3(col, under, peel * 0.8)
    col = mix3(col, hex_lin('#8e6e58'), runs * 0.28)
    col = mix3(col, hex_lin('#8a8656'), lichen * 0.35)
    col *= (1.0 + brush[..., None] * 0.05)
    ao = cavity_ao(h, N, 0.02, TILE_M['DomeA'], 300.0)
    rough = _rough_noise(t, 0.82, 0.06) + runs * 0.05
    return col, h, ao, rough, None


def rubble_a(seed=103):
    """Red sandstone rubble masonry (terrace retaining walls, cisterns, the temple niche frame)."""
    t = dt.Tex(N, seed)
    wu = t.u + 0.04 * t.fbm(6, 6, 3)
    wv = t.v + 0.04 * t.fbm(6, 6, 3)
    f1, f2, ids, _, _ = t.worley(12, 20, u=wu, v=wv, jitter=0.8, aspect=12 / 20 * 1.9)
    edge = f2 - f1
    stone = sstep(0.02, 0.16, edge)
    bulge = np.sqrt(np.clip(edge, 0, 1)) * 0.03
    strata = t.fbm(2, 60, 3)                                # bedding lines in the sandstone
    h = bulge * stone + strata * 0.002 + t.fbm(64, 64, 3) * 0.002
    r = t.cell_rand(ids, 12 * 20)
    r2 = t.cell_rand(ids, 12 * 20, 7)
    c0 = hex_lin('#8a5440')
    c1 = hex_lin('#9e664d')
    c2 = hex_lin('#6c4232')
    col = mix3(mix3(c2, c0, sstep(0.0, 0.6, r)), c1, sstep(0.55, 1.0, r2))
    col = mix3(col, hex_lin('#6a4a3a'), (1.0 - stone) * 0.6)            # mortar / gaps
    col = mix3(col, hex_lin('#c48c68'), sstep(0.2, 0.7, strata) * 0.25 * stone)
    col *= (1.0 + t.fbm(80, 80, 3)[..., None] * 0.08)
    ao = cavity_ao(h, N, 0.05, TILE_M['RubbleA'], 60.0)
    rough = np.clip(0.9 + 0.05 * t.fbm(16, 16, 3), 0, 1)
    return col, h, ao, rough, None


# --------------------------------------------------------------------------- culture B


def timber_b(seed=201):
    """Dark tarred timber planks running along u (decks, piles, walkways), silver weathering."""
    t = dt.Tex(N, seed)
    rows = 16                                               # 0.25 m planks on a 4 m tile
    pv = t.v * rows
    row = np.floor(pv).astype(np.int32)
    fv = pv - row
    # plank ends: each row has its own offset and length
    rr = np.random.default_rng(seed).random((rows, 4), dtype=np.float32)
    off = rr[row % rows, 0]
    seg_len = 0.25 + 0.25 * rr[row % rows, 1]               # in tile units (1-2 m)
    pu = (t.u + off) / seg_len
    seg = np.floor(pu)
    fu = pu - seg
    groove = np.minimum(sstep(0.0, 0.06, fv), sstep(0.0, 0.06, 1 - fv))
    endj = np.minimum(sstep(0.0, 0.01, fu), sstep(0.0, 0.01, 1 - fu))
    gr = t.fbm(4, 160, 3, u=t.u * 1.0, v=t.v)              # grain stretched along u
    knots_f1, _, _, _, _ = t.worley(10, 40, jitter=1.0)
    knot = 1.0 - sstep(0.0, 0.12, knots_f1)
    knot *= sstep(0.55, 0.7, t.fbm(8, 8, 2) * 0.5 + 0.5)
    h = groove * endj * 0.006 + gr * 0.0012 - knot * 0.001 + 0.002 * (rr[row % rows, 2] - 0.5)
    pr = (rr[row % rows, 3] * 0.6 + 0.4 * _h01(seg.astype(np.int64), row, 1047)).astype(np.float32)
    tar = hex_lin('#3a2f27')
    tar2 = hex_lin('#2a221c')
    silver = hex_lin('#77726a')
    col = mix3(tar2, tar, pr)
    weather = sstep(0.15, 0.7, t.fbm(5, 24, 4) * 0.5 + pr * 0.6 - 0.1)
    col = mix3(col, silver, weather * 0.4)
    col *= (1.0 + gr[..., None] * 0.18)
    col = mix3(col, hex_lin('#18130f'), (1 - groove * endj) * 0.85)
    col = mix3(col, hex_lin('#22301f'), sstep(0.35, 0.7, t.fbm(3, 3, 4)) * 0.18)   # algae
    ao = cavity_ao(h, N, 0.03, TILE_M['TimberB'], 300.0)
    rough = np.clip(0.62 + weather * 0.25 + 0.05 * gr, 0, 1)
    return col, h, ao, rough, None


def shingle_b(seed=202):
    """Silver-grey split wooden shingles in courses along u; v runs up the roof. Moss patches."""
    t = dt.Tex(N, seed)
    rows = 20                                               # 0.2 m exposed courses
    pv = t.v * rows + 0.04 * t.fbm(30, 4, 2)
    row = np.floor(pv).astype(np.int32)
    fv = pv - row
    rr = np.random.default_rng(seed).random((rows + 1, 64), dtype=np.float32)
    cells = 22                                              # ~0.18 m wide shingles
    pu = (t.u * cells + rr[row % rows, 0] * 5.0)
    jitter = (rr[row % rows, (np.floor(pu).astype(np.int32) % 60) + 2] - 0.5) * 0.5
    pu2 = pu + jitter * 0.5
    sid = np.floor(pu2).astype(np.int32)
    fu = pu2 - sid
    sr = _h01(sid, row, 40503)
    # ragged butt (lower edge) of every shingle; length differs a little per shingle
    butt = 0.06 * sr + 0.03 * np.abs(np.sin(fu * np.pi * 2.0))
    under = (fv < butt).astype(np.float32)                  # the course below shows through
    tv = np.clip((fv - butt) / (1 - butt), 0, 1)
    thick = (1.0 - tv) * 0.010 + 0.003
    gap = np.minimum(sstep(0.0, 0.06, fu), sstep(0.0, 0.06, 1 - fu))
    gr = t.fbm(60, 5, 3)                                    # grain runs along v (down the slope)
    split = sstep(0.6, 0.95, t.fbm(90, 3, 2) * 0.5 + 0.5)   # split lines along the grain
    h = (thick * (0.3 + 0.7 * gap) + gr * 0.0008 - split * 0.0008) * (1 - under) + under * 0.0005
    base = mix3(hex_lin('#5f6468'), hex_lin('#868b8f'), sr)
    base = mix3(base, hex_lin('#8d8374'), sstep(0.8, 1.0, sr) * 0.7)   # a few newer, browner shingles
    shadow = 1.0 - 0.5 * sstep(0.7, 1.0, fv) - 0.35 * under             # shadow cast by the course above
    col = base * shadow[..., None] * (1.0 + gr[..., None] * 0.18) * (1.0 - split[..., None] * 0.25)
    col = mix3(col, hex_lin('#262624'), (1.0 - gap) * 0.65 * (1 - under))
    moss_n = t.fbm(4, 4, 5) + 0.3 * t.fbm(20, 20, 3)
    moss = sstep(0.3, 0.5, moss_n) * sstep(0.35, 0.65, t.fbm(48, 48, 3) * 0.5 + 0.5 + 0.2 * (1 - tv)) * 0.8
    lich_f1, _, _, _, _ = t.worley(36, 36, jitter=1.0)
    lichen = (1.0 - sstep(0.04, 0.12, lich_f1)) * 0.5
    col = mix3(col, mix3(hex_lin('#3f4824'), hex_lin('#55602e'), sstep(0.3, 0.7, moss_n)), moss * 0.75)
    col = mix3(col, hex_lin('#b4b09a'), lichen * (1 - moss) * 0.4)
    h = h + moss * 0.002
    ao = cavity_ao(h, N, 0.04, TILE_M['ShingleB'], 120.0)
    rough = np.clip(0.8 + 0.1 * moss + 0.04 * gr, 0, 1)
    return col, h, ao, rough, None


# --------------------------------------------------------------------------- culture C


def stone_c(seed=301):
    """Pale dressed limestone in neat courses (the drum houses)."""
    t = dt.Tex(N, seed)
    rows = 11                                               # ~0.36 m courses
    pv = t.v * rows
    row = np.floor(pv).astype(np.int32)
    fv = pv - row
    rr = np.random.default_rng(seed).random((rows, 8), dtype=np.float32)
    nb = (5 + (rr[row % rows, 0] * 3).astype(np.int32))    # 5..7 blocks per course per tile
    pu = t.u * nb + rr[row % rows, 1] * 7
    bid = np.floor(pu).astype(np.int32)
    fu = pu - bid
    jw = 0.022
    joint = np.minimum(sstep(0.0, jw * 2, fv), sstep(0.0, jw * 2, 1 - fv))
    joint = np.minimum(joint, np.minimum(sstep(0.0, jw * nb / rows * 2.2, fu), sstep(0.0, jw * nb / rows * 2.2, 1 - fu)))
    br = _h01(bid, row, 15485)
    pillow = (np.sin(np.clip(fu, 0, 1) * np.pi) * np.sin(np.clip(fv, 0, 1) * np.pi)) ** 0.3
    chisel = t.fbm(120, 30, 2)
    h = joint * (0.006 + pillow * 0.003) + chisel * 0.0004 + t.fbm(20, 20, 3) * 0.001
    c0 = hex_lin('#c0b59d')
    c1 = hex_lin('#cec3aa')
    c2 = hex_lin('#a69a80')
    col = mix3(mix3(c2, c0, sstep(0.0, 0.5, br)), c1, sstep(0.5, 1.0, br))
    col = mix3(col, hex_lin('#cdb894'), sstep(0.3, 0.8, t.fbm(3, 3, 3)) * 0.3)
    lich_f1, _, _, _, _ = t.worley(30, 30, jitter=1.0)
    lichen = (1.0 - sstep(0.03, 0.14, lich_f1)) * sstep(0.2, 0.55, t.fbm(5, 5, 3) + 0.25)
    col = mix3(col, hex_lin('#c3a25c'), lichen * 0.35)
    col = mix3(col, hex_lin('#7f7a6c'), sstep(0.4, 0.9, t.value(40, 3) * 0.5 + 0.5) * 0.18)   # weather streaks
    col *= (1.0 + chisel[..., None] * 0.05)
    col = mix3(col, hex_lin('#7a7264'), (1 - joint) * 0.75)
    ao = cavity_ao(h, N, 0.03, TILE_M['StoneC'], 200.0)
    rough = np.clip(0.8 + 0.06 * t.fbm(12, 12, 3) + (1 - joint) * 0.1, 0, 1)
    return col, h, ao, rough, None


def slate_c(seed=302):
    """Dark blue-grey slate scales in courses (the cones); v runs up the slope."""
    t = dt.Tex(N, seed)
    rows = 22
    pv = t.v * rows
    row = np.floor(pv).astype(np.int32)
    fv = pv - row
    rr = np.random.default_rng(seed).random((rows, 4), dtype=np.float32)
    cells = 16
    pu = t.u * cells + (row % 2) * 0.5 + rr[row % rows, 0] * 0.15
    sid = np.floor(pu).astype(np.int32)
    fu = pu - sid
    sr = _h01(sid, row, 97)
    # rounded lower corners: a slate occupies the cell above a rounded butt line
    du = np.abs(fu - 0.5) * 2
    butt = 0.18 * np.clip((du - 0.55) / 0.45, 0, 1) ** 2
    inside = (fv > butt).astype(np.float32)
    tv = np.clip(fv, 0, 1)
    side = np.minimum(sstep(0.0, 0.03, fu), sstep(0.0, 0.03, 1 - fu))
    thick = (1 - tv) * 0.006 + 0.002
    cleave = t.fbm(30, 8, 3)
    h = (thick * side + cleave * 0.0007) * inside + (1 - inside) * 0.001
    c0 = hex_lin('#3c4048')
    c1 = hex_lin('#4a4f58')
    c2 = hex_lin('#33343b')
    col = mix3(mix3(c2, c0, sstep(0.0, 0.5, sr)), c1, sstep(0.55, 1.0, sr))
    col = mix3(col, hex_lin('#4a4352'), sstep(0.85, 1.0, sr) * 0.6)           # a few purple slates
    col = col * (0.82 + 0.18 * tv)[..., None] * (1.0 + cleave[..., None] * 0.12)
    col = mix3(col, hex_lin('#1b1c20'), (1 - side * inside) * 0.75)
    moss = sstep(0.28, 0.5, t.fbm(4, 4, 5)) * 0.7
    col = mix3(col, hex_lin('#59603a'), moss * (1 - tv) * 0.5)
    lich_f1, _, _, _, _ = t.worley(44, 44, jitter=1.0)
    lichen = (1.0 - sstep(0.03, 0.12, lich_f1)) * sstep(0.3, 0.6, t.fbm(6, 6, 3) + 0.25)
    col = mix3(col, hex_lin('#9a9a8a'), lichen * 0.45)
    ao = cavity_ao(h, N, 0.03, TILE_M['SlateC'], 200.0)
    rough = np.clip(0.55 + 0.15 * sr + 0.2 * moss + 0.05 * cleave, 0, 1)
    return col, h, ao, rough, None


def drystone_c(seed=303):
    """Dry-stone wall: packed irregular pale grey stones with deep dark gaps (ring walls, folds)."""
    t = dt.Tex(N, seed)
    wu = t.u + 0.03 * t.fbm(8, 8, 3)
    wv = t.v + 0.03 * t.fbm(8, 8, 3)
    f1, f2, ids, _, _ = t.worley(10, 28, u=wu, v=wv, jitter=0.8, aspect=10 / 28 * 2.6)
    edge = f2 - f1
    stone = sstep(0.04, 0.14, edge)
    bulge = np.sqrt(np.clip(edge, 0, 1)) * 0.035
    h = bulge * stone + t.fbm(64, 64, 3) * 0.002
    r = t.cell_rand(ids, 10 * 28)
    r2 = t.cell_rand(ids, 10 * 28, 3)
    col = mix3(mix3(hex_lin('#8d897f'), hex_lin('#b0aa9c'), r), hex_lin('#9b8e78'), sstep(0.6, 1.0, r2))
    lich_f1, _, _, _, _ = t.worley(36, 36, jitter=1.0)
    lichen = (1.0 - sstep(0.04, 0.16, lich_f1)) * sstep(0.2, 0.5, t.fbm(5, 5, 3) + 0.2)
    col = mix3(col, hex_lin('#c5b98d'), lichen * 0.4)
    col = mix3(col, hex_lin('#59633a'), sstep(0.25, 0.5, t.fbm(3, 3, 4)) * 0.25 * stone)  # moss
    col = mix3(col, hex_lin('#1e1d1a'), (1.0 - stone) * 0.92)
    col *= (1.0 + t.fbm(90, 90, 3)[..., None] * 0.1)
    ao = cavity_ao(h, N, 0.06, TILE_M['DryStoneC'], 40.0)
    rough = np.clip(0.88 + 0.06 * t.fbm(10, 10, 3), 0, 1)
    return col, h, ao, rough, None


# --------------------------------------------------------------------------- shared trim sheet


def trim(seed=401):
    """8 bands x 128 px, each periodic in u (2 m per tile). See TRIM_BANDS."""
    t = dt.Tex(N, seed)
    bh = N // 8
    col = np.zeros((N, N, 3), np.float32)
    h = np.zeros((N, N), np.float32)
    rough = np.zeros((N, N), np.float32)
    metal = np.zeros((N, N), np.float32)
    mask = np.zeros((N, N, 3), np.float32)
    lv = (t.v * 8) % 1.0                                    # 0..1 inside each band
    band = np.floor(t.v * 8).astype(np.int32)

    gr_u = t.fbm(6, 200, 4)                                 # grain along u
    f_n = t.fbm(16, 16, 3)
    edge_dark = sstep(0.0, 0.08, lv) * sstep(0.0, 0.08, 1 - lv)
    # 0 oak
    oak = mix3(hex_lin('#4a3626'), hex_lin('#6e5440'), sstep(-0.4, 0.5, gr_u))
    oak = mix3(oak, hex_lin('#8a8478'), sstep(0.2, 0.7, t.fbm(4, 8, 3)) * 0.35)     # silvered
    # 1 tar
    tar = mix3(hex_lin('#251e18'), hex_lin('#3d342b'), sstep(-0.3, 0.5, gr_u))
    # 2 cobalt paint, worn to plaster at the edges and in chips
    chips = sstep(0.25, 0.4, t.fbm(20, 20, 4) + 0.35 * (1 - edge_dark))
    cob = mix3(hex_lin('#2f4fa8'), hex_lin('#26408a'), sstep(-0.4, 0.4, f_n))
    cob = mix3(cob, hex_lin('#c99a78'), chips * 0.85)
    # 3 saffron woven cloth
    wx = np.sin(t.u * 2 * np.pi * 220) * np.sin(t.v * 2 * np.pi * 220 * 1.0)
    saf = mix3(hex_lin('#c88f2e'), hex_lin('#e0a848'), sstep(-0.5, 0.5, t.fbm(8, 8, 3))) * (1.0 + 0.12 * wx[..., None])
    # 4 rope: diagonal twist
    tw = np.sin((t.u * 2 * 60 + lv * 3.0) * np.pi * 2)
    rope = mix3(hex_lin('#5a4329'), hex_lin('#7d6440'), tw * 0.5 + 0.5)
    # 5 pale carved stone
    pst = mix3(hex_lin('#c9bfa6'), hex_lin('#ddd3bd'), sstep(-0.4, 0.5, t.fbm(10, 10, 4)))
    # 6 woven reed panel (basket weave)
    wu = (t.u * 2 * 24) % 1.0
    wvv = (lv * 3) % 1.0
    cellp = ((np.floor(t.u * 2 * 24) + np.floor(lv * 3)) % 2).astype(np.float32)
    strand = np.where(cellp > 0.5, np.sin(wvv * np.pi * 6), np.sin(wu * np.pi * 6))
    reed = mix3(hex_lin('#4a3624'), hex_lin('#6a5236'), strand * 0.5 + 0.5)
    # 7 window: deep recess, shutter frame, warm-lit glass mask
    wfu = (t.u * 2 * 4) % 1.0                               # 0.5 m window modules along u
    inside = (sstep(0.12, 0.16, wfu) * sstep(0.12, 0.16, 1 - wfu) * sstep(0.12, 0.18, lv) * sstep(0.12, 0.18, 1 - lv))
    mull = 1.0 - (1.0 - sstep(0.0, 0.03, np.abs(wfu - 0.5))) * inside
    win = mix3(hex_lin('#3c2e22'), hex_lin('#141110'), inside * mull)

    stack = [oak, tar, cob, saf, rope, pst, reed, win]
    hs = [gr_u * 0.0015, gr_u * 0.0008, -chips * 0.0006, wx * 0.0006, tw * 0.003, f_n * 0.0012,
          strand * 0.003, -inside * 0.01]
    rs = [0.72 + 0.08 * gr_u, 0.55 + 0.1 * gr_u, 0.6 + chips * 0.25, 0.92 + 0 * gr_u, 0.9 + 0 * gr_u,
          0.8 + 0.06 * f_n, 0.85 + 0 * gr_u, 0.6 - inside * 0.35]
    for b in range(8):
        sel = band == b
        col[sel] = stack[b][sel]
        h[sel] = hs[b][sel]
        rough[sel] = np.clip(rs[b], 0, 1)[sel]
    col *= (0.82 + 0.18 * edge_dark)[..., None]
    mask[..., 0] = np.where(band == 7, inside * mull, 0.0)        # night window emissive
    mask[..., 1] = np.where((band == 2) | (band == 3), 1.0, 0.0)  # culture accent tint mask
    mask[..., 2] = np.where(band == 3, 1.0, 0.0)                  # cloth (wind sway) mask
    ao = cavity_ao(h, N, 0.02, TILE_M['Trim'], 300.0)
    return col, h, ao, rough, (mask, metal)


# --------------------------------------------------------------------------- shared rock, garden


def rock(seed=501):
    """Neutral weathered sandstone rock with bedding strata and lichen; tinted per culture by the MI
    (red canyon rock for A, grey moor stone for C)."""
    t = dt.Tex(N, seed)
    wv = t.v + 0.05 * t.fbm(3, 3, 4)
    strata = t.fbm(2, 24, 4, v=wv)
    beds = np.sin((wv * 14 + 0.6 * t.fbm(4, 4, 3)) * np.pi * 2) * 0.5 + 0.5
    f1, f2, ids, _, _ = t.worley(6, 10, jitter=1.0, aspect=0.9)
    cracks = 1.0 - sstep(0.0, 0.035, f2 - f1)
    rid = t.ridged(5, 5, 5)
    h = rid * 0.05 + strata * 0.02 + beds * 0.012 - cracks * 0.02 + t.fbm(60, 60, 3) * 0.003
    col = mix3(hex_lin('#8f8170'), hex_lin('#b3a38c'), sstep(-0.4, 0.5, strata))
    col = mix3(col, hex_lin('#6f6355'), beds * 0.3)
    col = mix3(col, hex_lin('#3e362e'), cracks * 0.7)
    lich_f1, _, _, _, _ = t.worley(28, 28, jitter=1.0)
    lichen = (1.0 - sstep(0.02, 0.16, lich_f1)) * sstep(0.25, 0.55, t.fbm(5, 5, 3) + 0.25)
    col = mix3(col, hex_lin('#b8ad7e'), lichen * 0.35)
    col *= (1.0 + t.fbm(90, 90, 3)[..., None] * 0.1)
    ao = cavity_ao(h, N, 0.08, 4.0, 20.0)
    rough = np.clip(0.88 + 0.06 * t.fbm(10, 10, 3), 0, 1)
    return col, h, ao, rough, None


def garden_b(seed=601):
    """Floating-garden beds: rows of leafy vegetables on dark wet soil (u along the rows)."""
    t = dt.Tex(N, seed)
    rows = 10
    pv = t.v * rows
    fv = pv - np.floor(pv)
    rowmask = sstep(0.15, 0.3, fv) * sstep(0.15, 0.3, 1 - fv)
    f1, f2, ids, _, _ = t.worley(48, rows * 2, jitter=0.7, aspect=48 / (rows * 2))
    leaf = (1.0 - sstep(0.15, 0.55, f1)) * rowmask
    r = t.cell_rand(ids, 48 * rows * 2)
    veg = mix3(hex_lin('#3f6a24'), hex_lin('#7aa040'), r)
    veg = mix3(veg, hex_lin('#2f5a3a'), sstep(0.8, 1.0, r))           # a few blue-green cabbages
    veg = mix3(veg, hex_lin('#a3482c'), sstep(0.93, 1.0, t.cell_rand(ids, 48 * rows * 2, 5)) * 0.8)  # red chard
    soil = mix3(hex_lin('#2c2219'), hex_lin('#46372a'), sstep(-0.4, 0.5, t.fbm(30, 30, 3)))
    col = mix3(soil, veg, sstep(0.05, 0.3, leaf))
    col *= (1.0 + 0.25 * t.fbm(80, 80, 3)[..., None] * leaf[..., None])
    h = leaf * 0.06 + t.fbm(40, 40, 3) * 0.004
    ao = cavity_ao(h, N, 0.05, 4.0, 40.0)
    rough = np.clip(0.75 + 0.15 * (1 - leaf), 0, 1)
    return col, h, ao, rough, None


SETS = {
    'PlasterA': plaster_a, 'DomeA': dome_a, 'RubbleA': rubble_a,
    'TimberB': timber_b, 'ShingleB': shingle_b,
    'StoneC': stone_c, 'SlateC': slate_c, 'DryStoneC': drystone_c,
    'Rock': rock, 'GardenB': garden_b,
    'Trim': trim,
}

# mean albedo targets from the concept index / shrine sheet (checked in stats)
TARGET_HEX = {'PlasterA': '#c48f6c', 'DomeA': '#cf9f80', 'RubbleA': '#8a5440', 'TimberB': '#3a2f27',
              'ShingleB': '#8f949a', 'StoneC': '#cdb894', 'SlateC': '#3c4048', 'DryStoneC': '#9a9488'}


def build_all(out_dir, only=None, log=print):
    os.makedirs(out_dir, exist_ok=True)
    files = {}
    for name, fn in SETS.items():
        if only and name not in only:
            continue
        col, h, ao, rough, extra = fn()
        col = np.clip(col, 0, 1).astype(np.float32)
        nrm = normal_from_height(h.astype(np.float32), TILE_M[name], N)
        metal = np.zeros((N, N), np.float32)
        mask = None
        if extra is not None:
            mask, metal = extra
        orm = np.stack([ao, np.clip(rough, 0, 1), metal], axis=-1).astype(np.float32)
        base = os.path.join(out_dir, 'T_Culture_%s' % name)
        dt.save_png(base + '_D.png', col, srgb=True)
        dt.save_png(base + '_N.png', nrm, srgb=False)
        dt.save_png(base + '_ORM.png', orm, srgb=False)
        fl = {'D': base + '_D.png', 'N': base + '_N.png', 'ORM': base + '_ORM.png'}
        if mask is not None:
            dt.save_png(base + '_M.png', mask, srgb=False)
            fl['M'] = base + '_M.png'
        files[name] = fl
        log(dt.stats(name, col, TARGET_HEX.get(name)))
    return files
