# DEMIURGE art - Age4 (Industry age) texture synthesis.
#
# Runs inside Blender's Python (numpy + bpy for PNG writing via art/tools/dmtex.py).
# Every texture is generated from a fixed seed, so `build_age4.py --textures` rebuilds them bit for bit.
#
# Sets (1024 x 1024, PNG; D is sRGB, everything else linear):
#   T_Age4_Brick_{D,N,ORM}      red brick, English running bond, tiles every 1.8 m        (Structure)
#   T_Age4_BrickBuff_D           buff / yellow stock brick, shares Brick N and ORM          (Structure, culture C)
#   T_Age4_Slate_{D,N,ORM}      Welsh-style slate courses, tiles every 2.0 m              (Roof)
#   T_Age4_Stone_{D,N,ORM}      sandstone ashlar, tiles every 2.4 m                       (Structure of bridges)
#   T_Age4_Gravel_{D,N,ORM}     shale spoil / ballast, tiles every 4.0 m, neutral so MIs tint it (spoil, coal, ore)
#   T_Age4_Trim_{D,N,ORM,M}     the family trim sheet (Detail): 8 horizontal bands that tile in U
#                                in the lower half, unique tiles (windows, doors, clock...) in the upper half.
#                                M: R = night-window emissive mask, G = per-pane random (for lit fractions).
#
# The trim layout is shared with a4geo.py through TRIM below (UV rectangles in 0..1, v up).
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.normpath(os.path.join(HERE, '..', '..', 'tools'))
if TOOLS not in sys.path:
    sys.path.append(TOOLS)
import dmtex as dx  # noqa: E402

N = 1024

# metres covered by one texture tile (UV 0..1) for the tiling sets
TILE_M = {'brick': 1.8, 'slate': 2.0, 'stone': 2.4, 'gravel': 4.0}

# --------------------------------------------------------------------------- trim layout (shared with geometry)
# bands: name -> (v0, v1, metres along U per 1.0 of U)
BAND_PX = 64
BANDS = {
    'iron': (0, 8.0), 'timber': (1, 8.0), 'stone': (2, 8.0), 'rust': (3, 8.0),
    'glazing': (4, 8.0), 'trackbed': (5, 16.0), 'lead': (6, 8.0), 'terracotta': (7, 4.0),
}
# unique tiles: name -> pixel rect (x0, y0, x1, y1), y up from the bottom of the image
TILES_PX = {
    'sash6': (0, 512, 128, 768), 'sash2': (128, 512, 256, 768), 'factorywin': (256, 512, 384, 768),
    'door_green': (384, 512, 512, 768), 'door_red': (512, 512, 640, 768), 'loading': (640, 512, 768, 768),
    'shop': (768, 512, 896, 768), 'boarded': (896, 512, 1024, 768),
    'bigwin': (0, 768, 256, 1024), 'rubble': (256, 768, 512, 1024), 'lunette': (512, 768, 768, 1024),
    'clock': (768, 768, 896, 896), 'louvre': (768, 896, 896, 1024),
    'coal': (896, 768, 1024, 896), 'char': (896, 896, 1024, 1024),
}
INSET_PX = 3.0


def band_rect(name):
    k, metres = BANDS[name]
    v0 = (k * BAND_PX + INSET_PX) / N
    v1 = ((k + 1) * BAND_PX - INSET_PX) / N
    return v0, v1, metres


def tile_rect(name):
    x0, y0, x1, y1 = TILES_PX[name]
    return ((x0 + INSET_PX) / N, (y0 + INSET_PX) / N, (x1 - INSET_PX) / N, (y1 - INSET_PX) / N)


# --------------------------------------------------------------------------- helpers

def palette_pick(pal, r):
    """pal: list of (hex, weight); r: array 0..1 -> (..., 3) linear colours."""
    cols = np.stack([dx.hex_lin(h) for h, _ in pal])
    w = np.array([wt for _, wt in pal], np.float32)
    cdf = np.cumsum(w / w.sum())
    idx = np.searchsorted(cdf, np.clip(r, 0, 0.99999))
    return cols[idx]


def block_bond(T, W, H, ncol, nrow, offset=0.5, jitter_x=0.0):
    """Running bond blocks. Returns dict with ids, local fx, fy (m), block size lb, hb, edge distance."""
    lb, hb = W / ncol, H / nrow
    x = T.u * W
    y = T.v * H
    row = np.floor(y / hb).astype(np.int32)
    xs = x + (row % 2) * offset * lb
    colf = np.floor(xs / lb)
    col = (colf.astype(np.int32)) % ncol
    fx = xs - colf * lb
    fy = y - row * hb
    ids = (row % nrow) * ncol + col
    ex = np.minimum(fx, lb - fx)
    ey = np.minimum(fy, hb - fy)
    return dict(ids=ids, fx=fx, fy=fy, lb=lb, hb=hb, ex=ex, ey=ey, row=row, count=ncol * nrow)


def finish(name, outdir, D, H, R, AO=None, MT=None, metres=2.0, extra_ao=None, ao_strength=60.0, ao_radius=0.02):
    n = D.shape[0]
    nrm = dx.normal_from_height(H, metres, n)
    ao = dx.cavity_ao(H, n, ao_radius, metres, ao_strength) if AO is None else AO
    if extra_ao is not None:
        ao = ao * extra_ao
    mt = np.zeros_like(R) if MT is None else MT
    dx.save_png(os.path.join(outdir, 'T_Age4_%s_D.png' % name), D, srgb=True)
    dx.save_png(os.path.join(outdir, 'T_Age4_%s_N.png' % name), nrm, srgb=False)
    dx.save_png(os.path.join(outdir, 'T_Age4_%s_ORM.png' % name), np.stack([ao, R, mt], -1), srgb=False)
    print(dx.stats('T_Age4_%s_D' % name, D))


# --------------------------------------------------------------------------- brick

BRICK_RED = [('#8e4a33', 4), ('#7a3d2b', 3), ('#9c5638', 2), ('#653226', 2), ('#a8623f', 1), ('#5a372f', 1),
             ('#83503b', 2)]
BRICK_BUFF = [('#b49c72', 4), ('#a38c64', 3), ('#c2aa80', 2), ('#8e7a58', 2), ('#9a7a5a', 1), ('#cbb48c', 1)]


def brick_maps(seed, pal):
    T = dx.Tex(N, seed)
    W = H = TILE_M['brick']
    b = block_bond(T, W, H, 8, 24)
    mortar = 0.010
    chip = T.fbm(48, 48, 4) * 0.0035 + T.fbm(160, 160, 2) * 0.0012
    d = np.minimum(b['ex'], b['ey']) - mortar * 0.5 + chip
    face = dx.sstep(-0.0005, 0.0025, d)
    r1 = T.cell_rand(b['ids'], b['count'], 1)
    r2 = T.cell_rand(b['ids'], b['count'], 2)
    r3 = T.cell_rand(b['ids'], b['count'], 3)
    grain = T.fbm(256, 256, 3)
    # height (metres)
    bulge = (1.0 - (2 * b['fx'] / b['lb'] - 1) ** 2) * (1.0 - (2 * b['fy'] / b['hb'] - 1) ** 2)
    Hh = face * (0.0055 + 0.0008 * r2 + 0.0006 * bulge) + grain * 0.00035 * face + (1 - face) * T.fbm(200, 200, 2) * 0.0006
    # pits on some bricks
    pits = np.clip(T.fbm(300, 300, 2) - 0.45, 0, 1) * 0.003
    Hh -= pits * face
    # colour
    col = palette_pick(pal, r1)
    col = col * (0.86 + 0.28 * r3)[..., None]
    # fire flashing: darker ends on some bricks
    flash = np.clip((r2 - 0.75) * 4, 0, 1) * dx.sstep(0.05, 0.0, np.minimum(b['fx'], b['lb'] - b['fx']))
    col = col * (1 - 0.35 * flash)[..., None]
    col = col * (0.92 + 0.16 * (grain * 0.5 + 0.5))[..., None]
    mortar_col = dx.hex_lin('#867c70') * (0.85 + 0.15 * T.fbm(64, 64, 3)[..., None])
    C = dx.mix3(mortar_col * np.ones_like(col), col, face)
    # soot: vertically elongated streaks, plus a soft overall grime
    streak = T.fbm(14, 2, 5) * 0.5 + 0.5
    grime = T.fbm(4, 4, 4) * 0.5 + 0.5
    soot = np.clip(streak * 0.6 + grime * 0.4 - 0.30, 0, 1)
    C = C * (1 - 0.55 * soot)[..., None]
    lum = (C @ np.array([0.2126, 0.7152, 0.0722], np.float32))[..., None]
    C = lum + (C - lum) * 0.85
    # light efflorescence on a few bricks
    eff = np.clip(T.fbm(32, 32, 4) - 0.55, 0, 1) * 2.0 * face
    C = dx.mix3(C, dx.hex_lin('#c9c0b0') * np.ones_like(C), eff * 0.35)
    R = np.clip(0.80 + 0.08 * grain - 0.05 * eff + (1 - face) * 0.1 + soot * 0.05, 0, 1)
    return C.astype(np.float32), Hh.astype(np.float32), R.astype(np.float32)


def make_brick(outdir):
    C, Hh, R = brick_maps(4101, BRICK_RED)
    finish('Brick', outdir, C, Hh, R, metres=TILE_M['brick'], ao_strength=120.0, ao_radius=0.01)
    C2, _, _ = brick_maps(4101, BRICK_BUFF)
    dx.save_png(os.path.join(outdir, 'T_Age4_BrickBuff_D.png'), C2, srgb=True)
    print(dx.stats('T_Age4_BrickBuff_D', C2))


# --------------------------------------------------------------------------- slate

def make_slate(outdir):
    T = dx.Tex(N, 4202)
    W = H = TILE_M['slate']
    b = block_bond(T, W, H, 7, 8)
    r1 = T.cell_rand(b['ids'], b['count'], 1)
    r2 = T.cell_rand(b['ids'], b['count'], 2)
    r3 = T.cell_rand(b['ids'], b['count'], 3)
    gap = dx.sstep(0.002, 0.0045, b['ex'] + T.fbm(64, 64, 3) * 0.0015)
    t = b['fy'] / b['hb']
    cleave = T.fbm(6, 400, 3)
    Hh = (1 - t) * (0.007 + 0.002 * r2) + (r3 - 0.5) * 0.0015 * (b['fx'] / b['lb'] - 0.5) + cleave * 0.0004
    Hh = Hh * gap
    # butt edge chips
    chip = np.clip(T.fbm(90, 90, 3) - 0.3, 0, 1) * dx.sstep(0.02, 0.0, b['fy']) * 0.004
    Hh -= chip
    pal = [('#3b4048', 5), ('#353a41', 3), ('#42464e', 2), ('#3d3a42', 2), ('#40443f', 1), ('#474a50', 1)]
    col = palette_pick(pal, r1) * (0.92 + 0.14 * r2)[..., None]
    col = col * (0.94 + 0.12 * (cleave * 0.5 + 0.5))[..., None]
    # lichen
    f1, _, _, _, _ = T.worley(90, 90, jitter=0.95)
    lich = dx.sstep(0.28, 0.12, f1) * dx.sstep(0.15, 0.45, T.fbm(8, 8, 4) * 0.5 + 0.5)
    col = dx.mix3(col, dx.hex_lin('#7d7a58') * np.ones_like(col), lich * 0.45)
    # wet/soot darkening toward the butt of each course, and grime
    soot = (1 - t) ** 3 * 0.25 + np.clip(T.fbm(5, 5, 4) * 0.5 + 0.2, 0, 1) * 0.18
    col = col * (1 - soot)[..., None]
    col = col * (0.35 + 0.65 * gap)[..., None]
    R = np.clip(0.58 + 0.12 * r3 + 0.25 * lich + (1 - gap) * 0.3, 0, 1)
    finish('Slate', outdir, col.astype(np.float32), Hh.astype(np.float32), R.astype(np.float32),
           metres=W, ao_strength=90.0, ao_radius=0.015)


# --------------------------------------------------------------------------- stone ashlar

def make_stone(outdir):
    T = dx.Tex(N, 4303)
    W = H = TILE_M['stone']
    b = block_bond(T, W, H, 4, 8)
    r1 = T.cell_rand(b['ids'], b['count'], 1)
    r2 = T.cell_rand(b['ids'], b['count'], 2)
    d = np.minimum(b['ex'], b['ey']) - 0.004 + T.fbm(40, 40, 4) * 0.004
    face = dx.sstep(0.0, 0.006, d)
    tool = T.fbm(120, 120, 4)
    Hh = face * (0.008 + 0.004 * dx.sstep(0.0, 0.05, d) + tool * 0.0015 + r2 * 0.001)
    pal = [('#9a8b74', 4), ('#8d7f69', 3), ('#a69780', 2), ('#857563', 2), ('#7d7466', 1)]
    col = palette_pick(pal, r1) * (0.9 + 0.2 * r2)[..., None]
    col = col * (0.93 + 0.14 * (tool * 0.5 + 0.5))[..., None]
    grime = np.clip(T.fbm(10, 3, 5) * 0.6 + 0.25, 0, 1)
    col = col * (1 - 0.35 * grime)[..., None]
    joint = dx.hex_lin('#5e574d') * np.ones_like(col)
    col = dx.mix3(joint, col, face)
    R = np.clip(0.82 + 0.08 * tool, 0, 1)
    finish('Stone', outdir, col.astype(np.float32), Hh.astype(np.float32), R.astype(np.float32),
           metres=W, ao_strength=60.0, ao_radius=0.02)


# --------------------------------------------------------------------------- gravel / spoil

def gravel_maps(T, scale_cells=(36, 90, 220)):
    Hh = np.zeros((N, N), np.float32)
    col = np.zeros((N, N, 3), np.float32)
    pal = [('#504c49', 4), ('#433f3c', 3), ('#5d5853', 2), ('#38352f', 2), ('#66605a', 1), ('#4f4740', 1)]
    cover = np.zeros((N, N), np.float32)
    for k, g in enumerate(scale_cells):
        f1, f2, ids, _, _ = T.worley(g, g, jitter=0.95)
        stone = np.clip(1.0 - f1 / 0.62, 0, 1)
        edge = dx.sstep(0.0, 0.10, f2 - f1)
        dome = np.sqrt(stone) * edge
        r = T.cell_rand(ids, g * g, 10 + k)
        keep = (r > [0.45, 0.2, 0.0][k]).astype(np.float32)
        hh = dome * keep * (0.08, 0.035, 0.012)[k]
        c = palette_pick(pal, T.cell_rand(ids, g * g, 20 + k)) * (0.85 + 0.3 * r)[..., None]
        m = (hh > Hh).astype(np.float32)
        col = dx.mix3(col, c, m) if k else c
        Hh = np.maximum(Hh, hh)
        cover = np.maximum(cover, m * keep)
    fine = T.fbm(300, 300, 2)
    Hh += fine * 0.003
    col = col * (0.9 + 0.2 * (fine * 0.5 + 0.5))[..., None]
    return col, Hh


def make_gravel(outdir):
    T = dx.Tex(N, 4404)
    col, Hh = gravel_maps(T)
    R = np.clip(0.86 + 0.06 * T.fbm(64, 64, 3), 0, 1)
    finish('Gravel', outdir, col.astype(np.float32), Hh.astype(np.float32), R.astype(np.float32),
           metres=TILE_M['gravel'], ao_strength=25.0, ao_radius=0.05)


# --------------------------------------------------------------------------- trim sheet

class Canvas:
    def __init__(self, seed):
        self.T = dx.Tex(N, seed)
        self.D = np.zeros((N, N, 3), np.float32)
        self.H = np.zeros((N, N), np.float32)
        self.R = np.full((N, N), 0.8, np.float32)
        self.MT = np.zeros((N, N), np.float32)
        self.E = np.zeros((N, N), np.float32)
        self.G = np.zeros((N, N), np.float32)
        # shared noise fields (periodic over the whole sheet, so bands tile in U)
        T = self.T
        self.n_fine = T.fbm(256, 256, 3)
        self.n_mid = T.fbm(48, 48, 4)
        self.n_coarse = T.fbm(8, 8, 4)
        self.n_streak = T.fbm(64, 4, 4)       # stretched along V (vertical streaks)
        self.n_grain = T.fbm(4, 256, 4)       # stretched along U (timber grain)
        self.rng = np.random.default_rng(seed + 7)

    def sl(self, x0, y0, x1, y1):
        return (slice(y0, y1), slice(x0, x1))


def _band_slices(cv, name):
    k = BANDS[name][0]
    y0, y1 = k * BAND_PX, (k + 1) * BAND_PX
    s = (slice(y0, y1), slice(0, N))
    ly = (np.arange(y1 - y0, dtype=np.float32) + 0.5)[:, None] / (y1 - y0) * np.ones((1, N), np.float32)
    lx = (np.arange(N, dtype=np.float32) + 0.5)[None, :] * np.ones((y1 - y0, 1), np.float32)   # pixels
    return s, lx, ly


def band_iron(cv):
    s, lx, ly = _band_slices(cv, 'iron')
    base = dx.hex_lin('#2c322e')
    var = cv.n_mid[s] * 0.5 + 0.5
    col = base * (0.85 + 0.3 * var)[..., None]
    rust = np.clip(cv.n_streak[s] * 0.8 + cv.n_mid[s] * 0.4 - 0.35, 0, 1)
    rust = np.maximum(rust, dx.sstep(0.06, 0.0, np.minimum(ly, 1 - ly)) * 0.6 * (cv.n_fine[s] * 0.5 + 0.5))
    col = dx.mix3(col, dx.hex_lin('#6b3a22') * np.ones_like(col), rust)
    # flanges and rivets
    flange = dx.sstep(0.10, 0.13, ly) * dx.sstep(0.90, 0.87, ly)
    hh = 0.004 * (1 - flange)
    for yc in (0.06, 0.94):
        d = np.sqrt(((lx % 12.0) - 6.0) ** 2 + ((ly - yc) * BAND_PX) ** 2)
        riv = np.clip(1 - d / 2.6, 0, 1)
        hh += np.sqrt(riv) * 0.004
    cv.D[s] = col
    cv.H[s] = hh + cv.n_fine[s] * 0.0003
    cv.R[s] = np.clip(0.5 + 0.35 * rust + 0.05 * cv.n_fine[s], 0, 1)
    cv.MT[s] = 0.25 * (1 - rust)


def band_timber(cv):
    s, lx, ly = _band_slices(cv, 'timber')
    nplank = 3
    p = np.floor(ly * nplank)
    fy = ly * nplank - p
    # end joints per plank
    off = np.array([0.0, 333.0, 701.0])[p.astype(np.int32)]
    seg = np.floor((lx + off) / 260.0)
    rid = (p * 13 + seg) % 17
    r = (np.sin(rid * 12.9898) * 43758.5453) % 1.0
    joint = dx.sstep(0.0, 0.08, np.minimum(fy, 1 - fy)) * dx.sstep(0.0, 3.0, np.minimum((lx + off) % 260.0, 260.0 - (lx + off) % 260.0))
    grain = cv.n_grain[s] * 0.5 + 0.5
    pal = np.stack([dx.hex_lin('#6e5d4b'), dx.hex_lin('#5a4a3c'), dx.hex_lin('#7a6a58')])
    col = pal[(r * 3).astype(np.int32) % 3] * (0.8 + 0.35 * grain)[..., None]
    col = col * (0.4 + 0.6 * joint)[..., None]
    cv.D[s] = col
    cv.H[s] = joint * 0.004 + grain * 0.0008
    cv.R[s] = 0.82 + 0.1 * grain


def band_stone(cv):
    s, lx, ly = _band_slices(cv, 'stone')
    seg = lx % 115.0
    j = dx.sstep(0.0, 2.5, np.minimum(seg, 115.0 - seg))
    edge = dx.sstep(0.0, 0.18, np.minimum(ly, 1 - ly))
    tool = cv.n_fine[s] * 0.5 + 0.5
    col = dx.hex_lin('#8f8069') * (0.86 + 0.2 * tool)[..., None]
    grime = np.clip(cv.n_streak[s] * 0.6 + 0.45 - ly * 0.4, 0, 1)
    col = col * (1 - 0.42 * grime)[..., None] * (0.5 + 0.5 * j)[..., None]
    cv.D[s] = col
    cv.H[s] = 0.006 * edge * j + tool * 0.0006
    cv.R[s] = 0.8 + 0.08 * tool


def band_rust(cv):
    s, lx, ly = _band_slices(cv, 'rust')
    a = cv.n_mid[s] * 0.5 + 0.5
    b = cv.n_fine[s] * 0.5 + 0.5
    col = dx.mix3(dx.hex_lin('#6a3b22') * np.ones((BAND_PX, N, 3), np.float32), dx.hex_lin('#3b2a22') * np.ones((BAND_PX, N, 3), np.float32), dx.sstep(0.4, 0.7, a))
    paint = dx.sstep(0.62, 0.66, cv.n_coarse[s] * 0.5 + 0.5 + b * 0.1)
    col = dx.mix3(col, dx.hex_lin('#33392f') * np.ones_like(col), paint)
    col = col * (0.85 + 0.3 * b)[..., None]
    cv.D[s] = col
    cv.H[s] = b * 0.0015 + paint * 0.0005
    cv.R[s] = 0.88 - paint * 0.25


def band_glazing(cv):
    s, lx, ly = _band_slices(cv, 'glazing')
    bar_u = np.minimum(lx % 64.0, 64.0 - lx % 64.0)
    bar = np.maximum(dx.sstep(3.0, 1.5, bar_u), dx.sstep(0.07, 0.04, np.minimum(ly, 1 - ly)))
    pane = np.floor(lx / 64.0)
    r = (np.sin(pane * 78.233 + 1.3) * 43758.5453) % 1.0
    dirt = np.clip(cv.n_streak[s] * 0.5 + 0.35 + (1 - ly) * 0.25, 0, 1)
    glass = dx.hex_lin('#26313a') * (0.75 + 0.5 * r)[..., None]
    glass = dx.mix3(glass, dx.hex_lin('#5a5a52') * np.ones_like(glass), dirt * 0.45)
    broken = (r > 0.93).astype(np.float32)
    glass = glass * (1 - 0.7 * broken)[..., None]
    col = dx.mix3(glass, dx.hex_lin('#4a4f4d') * np.ones_like(glass), bar)
    cv.D[s] = col
    cv.H[s] = bar * 0.006
    cv.R[s] = 0.12 + 0.45 * dirt * (1 - bar) + 0.5 * bar
    cv.MT[s] = 0.0
    cv.E[s] = (1 - bar) * 0.55 * (1 - broken)
    cv.G[s] = r


def band_trackbed(cv):
    s, lx, ly = _band_slices(cv, 'trackbed')
    T = cv.T
    col = np.zeros((BAND_PX, N, 3), np.float32)
    g = T.fbm(512, 512, 2)[s]
    f1, f2, ids, _, _ = T.worley(256, 256, jitter=0.95)
    stone = np.sqrt(np.clip(1 - f1[s] / 0.6, 0, 1)) * dx.sstep(0, 0.1, (f2 - f1)[s])
    r = T.cell_rand(ids, 256 * 256, 31)[s]
    col = palette_pick([('#6a645c', 3), ('#58534d', 2), ('#7a7368', 1)], r) * (0.6 + 0.5 * stone)[..., None]
    period = N / 20.0
    su = np.minimum(lx % period, period - lx % period)
    sleeper = dx.sstep(8.5, 7.0, su) * dx.sstep(0.12, 0.16, ly) * dx.sstep(0.88, 0.84, ly)
    wood = dx.hex_lin('#3d3129') * (0.8 + 0.3 * (cv.n_grain[s] * 0.5 + 0.5))[..., None]
    col = dx.mix3(col, wood, sleeper)
    cv.D[s] = col
    cv.H[s] = stone * 0.01 * (1 - sleeper) + sleeper * 0.012 + g * 0.001
    cv.R[s] = 0.9 - 0.05 * sleeper


def band_lead(cv):
    s, lx, ly = _band_slices(cv, 'lead')
    w = cv.n_mid[s] * 0.5 + 0.5
    col = dx.hex_lin('#55595c') * (0.85 + 0.25 * w)[..., None]
    roll = dx.sstep(0.0, 0.15, np.minimum(ly, 1 - ly))
    cv.D[s] = col * (0.8 + 0.2 * roll)[..., None]
    cv.H[s] = roll * 0.004 + w * 0.0008
    cv.R[s] = 0.55 + 0.1 * w
    cv.MT[s] = 0.3


def band_terracotta(cv):
    s, lx, ly = _band_slices(cv, 'terracotta')
    w = cv.n_mid[s] * 0.5 + 0.5
    col = dx.hex_lin('#a4593a') * (0.82 + 0.3 * w)[..., None]
    soot = dx.sstep(0.55, 1.0, ly) * 0.6
    col = col * (1 - soot)[..., None]
    rings = np.sin(ly * 40.0) * 0.5 + 0.5
    cv.D[s] = col
    cv.H[s] = rings * 0.001 + w * 0.0006
    cv.R[s] = 0.78


def _tile_slices(name):
    x0, y0, x1, y1 = TILES_PX[name]
    s = (slice(y0, y1), slice(x0, x1))
    h, w = y1 - y0, x1 - x0
    ly = ((np.arange(h, dtype=np.float32) + 0.5) / h)[:, None] * np.ones((1, w), np.float32)
    lx = ((np.arange(w, dtype=np.float32) + 0.5) / w)[None, :] * np.ones((h, 1), np.float32)
    return s, lx, ly, w, h


def _rect(lx, ly, x0, y0, x1, y1, soft=0.004):
    return dx.sstep(x0 - soft, x0 + soft, lx) * dx.sstep(x1 + soft, x1 - soft, lx) * dx.sstep(y0 - soft, y0 + soft, ly) * dx.sstep(y1 + soft, y1 - soft, ly)


def _glass(cv, s, lx, ly, pane_id):
    r = (np.sin(pane_id * 12.9898 + 4.1) * 43758.5453) % 1.0
    sky = dx.hex_lin('#3a4652') * (0.6 + 0.7 * ly)[..., None]   # sky reflection gradient
    inner = dx.hex_lin('#1a1d20')
    refl = 0.35 + 0.4 * r
    col = inner * (1 - refl)[..., None] + sky * refl[..., None]
    curtain = dx.sstep(0.82, 0.86, np.abs(lx * 2 - 1)) * (r > 0.5)
    col = dx.mix3(col, dx.hex_lin('#9b8b70') * np.ones_like(col), curtain * 0.5)
    return col, r


def _window(cv, name, cols, rows, frame_hex, frame_w=0.06, bar_w=0.022, meeting=True):
    s, lx, ly, w, h = _tile_slices(name)
    fwx = frame_w
    fwy = frame_w * w / h
    inner = _rect(lx, ly, fwx, fwy, 1 - fwx, 1 - fwy, 0.002)
    gx = (lx - fwx) / (1 - 2 * fwx)
    gy = (ly - fwy) / (1 - 2 * fwy)
    if meeting:
        sash = (gy > 0.5).astype(np.float32)
        gyy = np.where(gy > 0.5, (gy - 0.5) * 2, gy * 2)
        rr = rows
    else:
        sash = np.zeros_like(gy)
        gyy = gy
        rr = rows
    cx = np.floor(np.clip(gx, 0, 0.9999) * cols)
    cyy = np.floor(np.clip(gyy, 0, 0.9999) * rr)
    bx = np.minimum((gx * cols) % 1.0, 1 - (gx * cols) % 1.0) / cols
    by = np.minimum((gyy * rr) % 1.0, 1 - (gyy * rr) % 1.0) / rr
    bar = np.maximum(dx.sstep(bar_w * 0.6, bar_w * 0.3, bx), dx.sstep(bar_w * 0.6 * w / h * 2, bar_w * 0.3 * w / h * 2, by))
    if meeting:
        bar = np.maximum(bar, dx.sstep(0.03, 0.015, np.abs(gy - 0.5)))
    pane_id = cx + cyy * 7 + sash * 50 + (sum(ord(ch) for ch in name) % 97)
    glass, r = _glass(cv, s, lx, ly, pane_id)
    frame = dx.hex_lin(frame_hex) * np.ones_like(glass)
    paint = 1 - inner * (1 - bar)
    col = dx.mix3(glass, frame, paint)
    cv.D[s] = col
    cv.H[s] = paint * 0.012 + (1 - inner) * 0.006
    cv.R[s] = np.where(paint > 0.5, 0.55, 0.08)
    cv.E[s] = (1 - paint)
    cv.G[s] = r


def _door(cv, name, hex_col, fan=True):
    s, lx, ly, w, h = _tile_slices(name)
    fr = 0.07
    frame = 1 - _rect(lx, ly, fr, 0.0, 1 - fr, 1 - fr * w / h, 0.002)
    fan_m = _rect(lx, ly, fr, 0.80, 1 - fr, 1 - fr * w / h, 0.002) if fan else np.zeros_like(lx)
    transom = _rect(lx, ly, fr, 0.77, 1 - fr, 0.80, 0.002)
    leaf = (1 - frame) * (1 - fan_m) * (1 - transom)
    panels = np.zeros_like(lx)
    for (x0, x1) in ((0.16, 0.46), (0.54, 0.84)):
        for (y0, y1) in ((0.06, 0.34), (0.40, 0.72)):
            panels = np.maximum(panels, _rect(lx, ly, x0, y0, x1, y1, 0.01))
    col = dx.hex_lin(hex_col) * (0.9 + 0.15 * (cv.n_fine[s] * 0.5 + 0.5))[..., None]
    col = col * (0.8 + 0.2 * panels)[..., None]
    glass, r = _glass(cv, s, lx, ly, np.floor(lx * 4))
    frame_c = dx.hex_lin('#d6cfbf') * np.ones_like(col)
    out = dx.mix3(col, frame_c, frame + transom)
    out = dx.mix3(out, glass, fan_m)
    knob = np.clip(1 - np.sqrt(((lx - 0.82) * w) ** 2 + ((ly - 0.4) * h) ** 2) / 3.0, 0, 1)
    out = dx.mix3(out, dx.hex_lin('#b08d3a') * np.ones_like(out), knob)
    cv.D[s] = out
    cv.H[s] = (frame + transom) * 0.012 + panels * 0.004 + leaf * 0.002
    cv.R[s] = np.where(fan_m > 0.5, 0.08, 0.5)
    cv.MT[s] = knob
    cv.E[s] = fan_m
    cv.G[s] = r


def _loading(cv):
    s, lx, ly, w, h = _tile_slices('loading')
    planks = np.minimum((lx * 9) % 1.0, 1 - (lx * 9) % 1.0)
    pj = dx.sstep(0.0, 0.12, planks)
    grain = cv.n_streak[s] * 0.5 + 0.5
    col = dx.hex_lin('#5a3a2c') * (0.75 + 0.4 * grain)[..., None] * (0.6 + 0.4 * pj)[..., None]
    mid = dx.sstep(0.012, 0.006, np.abs(lx - 0.5))
    straps = np.zeros_like(lx)
    for yc in (0.15, 0.5, 0.85):
        straps = np.maximum(straps, dx.sstep(0.025, 0.015, np.abs(ly - yc)) * dx.sstep(0.47, 0.44, np.abs(lx - 0.5)))
    col = dx.mix3(col, dx.hex_lin('#2a2a28') * np.ones_like(col), np.maximum(straps, mid))
    frame = 1 - _rect(lx, ly, 0.04, 0.0, 0.96, 0.97, 0.002)
    col = dx.mix3(col, dx.hex_lin('#4a3a30') * np.ones_like(col), frame)
    cv.D[s] = col
    cv.H[s] = pj * 0.003 + straps * 0.004 + frame * 0.01
    cv.R[s] = 0.8 - straps * 0.2
    cv.MT[s] = straps * 0.5


def _shop(cv):
    s, lx, ly, w, h = _tile_slices('shop')
    riser = (ly < 0.22).astype(np.float32)
    fr = 0.06
    frame = 1 - _rect(lx, ly, fr, 0.22, 1 - fr, 0.97, 0.002)
    mull = dx.sstep(0.015, 0.008, np.abs(lx - 0.5)) * (ly > 0.22)
    transom = dx.sstep(0.012, 0.006, np.abs(ly - 0.80))
    glass, r = _glass(cv, s, lx, ly, np.floor(lx * 2) + (ly > 0.8) * 3)
    wood = dx.hex_lin('#2f3e34') * np.ones_like(glass)
    paint = np.clip(frame + mull + transom + riser, 0, 1)
    col = dx.mix3(glass, wood, paint)
    cv.D[s] = col
    cv.H[s] = paint * 0.01
    cv.R[s] = np.where(paint > 0.5, 0.5, 0.06)
    cv.E[s] = (1 - paint)
    cv.G[s] = r


def _boarded(cv):
    s, lx, ly, w, h = _tile_slices('boarded')
    b = np.minimum((ly * 9) % 1.0, 1 - (ly * 9) % 1.0)
    pj = dx.sstep(0.0, 0.15, b)
    grain = cv.n_grain[s] * 0.5 + 0.5
    col = dx.hex_lin('#5e5246') * (0.7 + 0.4 * grain)[..., None] * (0.55 + 0.45 * pj)[..., None]
    frame = 1 - _rect(lx, ly, 0.06, 0.03, 0.94, 0.97, 0.002)
    col = dx.mix3(col, dx.hex_lin('#3c3a36') * np.ones_like(col), frame)
    cv.D[s] = col
    cv.H[s] = pj * 0.004 + frame * 0.01
    cv.R[s] = 0.88


def _bigwin(cv):
    s, lx, ly, w, h = _tile_slices('bigwin')
    cols, rows = 6, 6
    fr = 0.04
    inner = _rect(lx, ly, fr, fr, 1 - fr, 1 - fr, 0.002)
    gx = (lx - fr) / (1 - 2 * fr)
    gy = (ly - fr) / (1 - 2 * fr)
    bx = np.minimum((gx * cols) % 1.0, 1 - (gx * cols) % 1.0)
    by = np.minimum((gy * rows) % 1.0, 1 - (gy * rows) % 1.0)
    bar = np.maximum(dx.sstep(0.07, 0.035, bx), dx.sstep(0.07, 0.035, by))
    pane = np.floor(np.clip(gx, 0, 0.999) * cols) + 7 * np.floor(np.clip(gy, 0, 0.999) * rows)
    glass, r = _glass(cv, s, lx, ly, pane)
    white = (r > 0.85).astype(np.float32)   # whitewashed / replaced panes
    glass = dx.mix3(glass, dx.hex_lin('#8f9088') * np.ones_like(glass), white * 0.8)
    paint = 1 - inner * (1 - bar)
    col = dx.mix3(glass, dx.hex_lin('#2c2f2e') * np.ones_like(glass), paint)
    cv.D[s] = col
    cv.H[s] = paint * 0.008
    cv.R[s] = np.where(paint > 0.5, 0.6, 0.12)
    cv.E[s] = (1 - paint) * (1 - 0.5 * white)
    cv.G[s] = r


def _rubble(cv):
    s, lx, ly, w, h = _tile_slices('rubble')
    T = dx.Tex(256, 4505)
    f1, f2, ids, _, _ = T.worley(14, 14, jitter=0.95)
    r = T.cell_rand(ids, 14 * 14, 1)
    r2 = T.cell_rand(ids, 14 * 14, 2)
    chunk = np.sqrt(np.clip(1 - f1 / 0.7, 0, 1)) * dx.sstep(0.0, 0.08, f2 - f1)
    g1, g2, gid, _, _ = T.worley(40, 40, jitter=0.95)
    small = np.sqrt(np.clip(1 - g1 / 0.7, 0, 1)) * dx.sstep(0.0, 0.1, g2 - g1)
    pal = [('#8a4a33', 4), ('#6c3a2a', 3), ('#8f877a', 2), ('#4a4440', 2), ('#5a4a3c', 1), ('#a39276', 1)]
    c1 = palette_pick(pal, r)
    c2 = palette_pick(pal, T.cell_rand(gid, 40 * 40, 3))
    col = dx.mix3(c2, c1, (chunk > small * 0.6).astype(np.float32))
    dust = np.clip(T.fbm(6, 6, 4) * 0.6 + 0.2, 0, 1)
    col = dx.mix3(col, dx.hex_lin('#6a625a') * np.ones_like(col), 0.35 + dust * 0.4)
    col = col * (0.55 + 0.35 * r2)[..., None]
    cv.D[s] = col
    cv.H[s] = np.maximum(chunk * 0.06, small * 0.025)
    cv.R[s] = 0.9


def _lunette(cv):
    s, lx, ly, w, h = _tile_slices('lunette')
    x = lx * 2 - 1
    y = ly
    rad = np.sqrt(x * x + y * y)
    ang = np.arctan2(y, x)
    inside = dx.sstep(1.0, 0.97, rad)
    radial = np.minimum((ang / (math.pi / 12)) % 1.0, 1 - (ang / (math.pi / 12)) % 1.0)
    bars = np.maximum(dx.sstep(0.06, 0.03, radial) * (rad > 0.3), dx.sstep(0.02, 0.01, np.abs(rad - 0.62)))
    bars = np.maximum(bars, dx.sstep(0.02, 0.01, np.abs(rad - 0.3)))
    bars = np.maximum(bars, dx.sstep(0.035, 0.0, 1.0 - rad) * 3)
    bars = np.maximum(bars, dx.sstep(0.012, 0.006, np.abs(y - 0.28)))
    bars = np.clip(bars, 0, 1)
    pane = np.floor(ang / (math.pi / 12)) + 30 * np.floor(rad / 0.31)
    glass, r = _glass(cv, s, lx, ly, pane)
    col = dx.mix3(glass, dx.hex_lin('#3c403e') * np.ones_like(glass), bars)
    col = dx.mix3(dx.hex_lin('#3c403e') * np.ones_like(col), col, inside)
    cv.D[s] = col
    cv.H[s] = bars * 0.01
    cv.R[s] = np.where(bars > 0.5, 0.55, 0.1)
    cv.E[s] = inside * (1 - bars) * 0.7
    cv.G[s] = r


def _clock(cv):
    s, lx, ly, w, h = _tile_slices('clock')
    x = lx * 2 - 1
    y = ly * 2 - 1
    rr = np.sqrt(x * x + y * y)
    ang = np.arctan2(y, x)
    dial = dx.sstep(0.86, 0.84, rr)
    ring = dx.sstep(0.03, 0.01, np.abs(rr - 0.85)) + dx.sstep(0.94, 0.92, rr) * (1 - dx.sstep(0.88, 0.86, rr))
    tick = dx.sstep(0.08, 0.04, np.minimum((ang / (math.pi / 6)) % 1.0, 1 - (ang / (math.pi / 6)) % 1.0)) * dx.sstep(0.66, 0.70, rr) * dx.sstep(0.82, 0.78, rr)

    def hand(a, l, wdt):
        dxv, dyv = math.cos(a), math.sin(a)
        t = np.clip(x * dxv + y * dyv, 0, l)
        d = np.sqrt((x - t * dxv) ** 2 + (y - t * dyv) ** 2)
        return dx.sstep(wdt, wdt * 0.5, d)
    hands = np.maximum(hand(math.radians(60), 0.45, 0.05), hand(math.radians(160), 0.68, 0.035))
    ink = np.clip(ring + tick + hands, 0, 1)
    face = dx.hex_lin('#e4dccb')
    col = dx.mix3(face * np.ones((h, w, 3), np.float32), dx.hex_lin('#15161a') * np.ones((h, w, 3), np.float32), ink)
    col = dx.mix3(dx.hex_lin('#3a3d3a') * np.ones_like(col), col, np.clip(dial + ring, 0, 1))
    cv.D[s] = col
    cv.H[s] = ink * 0.003 + (1 - dial) * 0.008
    cv.R[s] = 0.4
    cv.E[s] = dial * (1 - ink) * 0.8
    cv.G[s] = 0.0


def _louvre(cv):
    s, lx, ly, w, h = _tile_slices('louvre')
    sl = (ly * 10) % 1.0
    col = dx.hex_lin('#4f4438') * (0.4 + 0.6 * sl)[..., None]
    frame = 1 - _rect(lx, ly, 0.08, 0.06, 0.92, 0.94, 0.002)
    col = dx.mix3(col, dx.hex_lin('#5e5246') * np.ones_like(col), frame)
    cv.D[s] = col
    cv.H[s] = sl * 0.012 + frame * 0.015
    cv.R[s] = 0.85


def _coal(cv):
    s, lx, ly, w, h = _tile_slices('coal')
    T = dx.Tex(128, 4606)
    f1, f2, ids, _, _ = T.worley(10, 10, jitter=0.95)
    lump = np.sqrt(np.clip(1 - f1 / 0.7, 0, 1)) * dx.sstep(0, 0.1, f2 - f1)
    r = T.cell_rand(ids, 100, 1)
    col = dx.hex_lin('#1c1b1b') * (0.7 + 0.6 * r)[..., None] * (0.5 + 0.6 * lump)[..., None]
    cv.D[s] = col
    cv.H[s] = lump * 0.05
    cv.R[s] = 0.45 + 0.3 * (1 - lump)


def _char(cv):
    s, lx, ly, w, h = _tile_slices('char')
    cr = np.minimum((ly * 14 + cv.n_mid[s] * 0.6) % 1.0, 1 - (ly * 14 + cv.n_mid[s] * 0.6) % 1.0)
    crack = dx.sstep(0.08, 0.02, cr)
    col = dx.hex_lin('#1e1a17') * (0.7 + 0.5 * (cv.n_fine[s] * 0.5 + 0.5))[..., None]
    col = dx.mix3(col, dx.hex_lin('#0c0b0a') * np.ones_like(col), crack)
    cv.D[s] = col
    cv.H[s] = (1 - crack) * 0.004
    cv.R[s] = 0.92


def make_trim(outdir):
    cv = Canvas(4707)
    for f in (band_iron, band_timber, band_stone, band_rust, band_glazing, band_trackbed, band_lead, band_terracotta):
        f(cv)
    _window(cv, 'sash6', 3, 2, '#d9d2c2')
    _window(cv, 'sash2', 1, 1, '#d9d2c2')
    _window(cv, 'factorywin', 4, 8, '#2c2f2e', frame_w=0.05, bar_w=0.03, meeting=False)
    _door(cv, 'door_green', '#2f4a3a')
    _door(cv, 'door_red', '#5e2321')
    _loading(cv)
    _shop(cv)
    _boarded(cv)
    _bigwin(cv)
    _rubble(cv)
    _lunette(cv)
    _clock(cv)
    _louvre(cv)
    _coal(cv)
    _char(cv)
    metres = 8.0
    nrm = dx.normal_from_height(cv.H, metres, N)
    ao = dx.cavity_ao(cv.H, N, 0.03, metres, 40.0)
    dx.save_png(os.path.join(outdir, 'T_Age4_Trim_D.png'), cv.D, srgb=True)
    dx.save_png(os.path.join(outdir, 'T_Age4_Trim_N.png'), nrm, srgb=False)
    dx.save_png(os.path.join(outdir, 'T_Age4_Trim_ORM.png'), np.stack([ao, np.clip(cv.R, 0, 1), np.clip(cv.MT, 0, 1)], -1), srgb=False)
    dx.save_png(os.path.join(outdir, 'T_Age4_Trim_M.png'), np.stack([np.clip(cv.E, 0, 1), np.clip(cv.G, 0, 1), np.zeros_like(cv.E)], -1), srgb=False)
    print(dx.stats('T_Age4_Trim_D', cv.D))


def make_all(outdir):
    os.makedirs(outdir, exist_ok=True)
    make_brick(outdir)
    make_slate(outdir)
    make_stone(outdir)
    make_gravel(outdir)
    make_trim(outdir)
