# DEMIURGE ships - procedural trim sheets for the Ship family (runs inside Blender's Python).
#
# Three shared 1024x1024 trim sheets, one per material slot:
#   Hull   -> M_Ship_Structure   (hull planks, reed, painted steel, composite, culture stripe, char)
#   Canvas -> M_Ship_Roof        (sail cloth, striped sail, torn sail, superstructure, glazing)
#   Detail -> M_Ship_Detail      (containers, spars, rope, metal, crew, flag cloth, gold, glow, oars, cargo)
# Each sheet is a stack of horizontal bands. Every band tiles in U (meshes may repeat it along their length);
# V of a face always stays inside its band.
# Outputs per sheet: T_Ship_<Sheet>_D (sRGB), _N (OpenGL +Y), _ORM (AO, roughness, metallic), _M
# (R night-window emissive, G culture-tint mask, B constant emissive such as engine glow).
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', 'tools')))
import dmtex as dt  # noqa: E402

N = 1024

# band name -> (sheet, height px); listed bottom (v = 0) to top. Heights per sheet sum to 1024.
SHEETS = {
    'Hull': [('plank_tar', 112), ('plank_oiled', 112), ('deck', 96), ('reed', 112), ('steel_black', 112),
             ('steel_red', 80), ('steel_deck', 96), ('composite', 112), ('stripe', 64), ('char', 128)],
    'Canvas': [('sail', 224), ('sail_stripe', 160), ('sail_torn', 128), ('super_win', 192), ('paint_white', 96),
               ('char_roof', 96), ('glass', 128)],
    'Detail': [('containers', 192), ('spar', 112), ('rope', 64), ('metal', 96), ('crew', 64), ('flag', 112),
               ('gold', 96), ('glow', 64), ('oar', 96), ('cargo', 128)],
}
SHEET_SLOT = {'Hull': 0, 'Canvas': 1, 'Detail': 2}   # Structure, Roof, Detail


def band_table():
    """name -> (slot, y0 px, h px)."""
    out = {}
    for sheet, bands in SHEETS.items():
        y = 0
        for name, h in bands:
            out[name] = (SHEET_SLOT[sheet], y, h)
            y += h
        assert y == N, (sheet, y)
    return out


BANDS = band_table()
CONTAINER_SEGMENTS = 8          # containers band: 8 colour segments across U; segment 7 is culture-tinted


# --------------------------------------------------------------------------- helpers


class Band:
    """Views onto the sheet rows of one band. u: 0..1 across, vl: 0..1 up inside the band."""

    def __init__(self, T, y0, h):
        self.T, self.y0, self.h = T, y0, h
        self.sl = slice(y0, y0 + h)
        self.u = T.u[self.sl]
        self.v = T.v[self.sl]
        self.vl = (np.arange(h, dtype=np.float32)[:, None] + 0.5) / h * np.ones((1, N), np.float32)
        self.aspect = N / h   # one band height = 1/aspect of the tile width

    def noise(self, px, py, oct=4, gain=0.5):
        return self.T.fbm(px, py, oct, gain)[self.sl]

    def rng(self):
        return self.T.rng


def col(h):
    return dt.hex_lin(h)


def lines(x, count, width):
    """1 near integer multiples of 1/count (periodic), falloff over `width` (in units of one period)."""
    f = (x * count) % 1.0
    d = np.minimum(f, 1.0 - f)
    return np.clip(1.0 - d / max(width, 1e-6), 0.0, 1.0)


def bevel(x, count, width):
    """0 at seams rising to 1 inside, over width (period units)."""
    f = (x * count) % 1.0
    d = np.minimum(f, 1.0 - f)
    return dt.sstep(0.0, width, d)


# --------------------------------------------------------------------------- band painters
# each returns dict: rgb (h,N,3 linear), height (h,N metres), rough, metal, mR, mG, mB (h,N)


def _out(b, rgb, height, rough, metal=0.0, mR=0.0, mG=0.0, mB=0.0):
    z = np.zeros((b.h, N), np.float32)
    f = lambda a: (z + a).astype(np.float32)
    return dict(rgb=rgb.astype(np.float32), height=f(height), rough=f(rough), metal=f(metal), mR=f(mR), mG=f(mG), mB=f(mB))


def planks(b, strakes, base, var, rough, seed_off=0, plank_len=0.33, tar=0.0):
    rng = b.rng()
    vl, u = b.vl, b.u
    s = np.floor(vl * strakes).astype(np.int32)
    offs = rng.random(strakes + 1).astype(np.float32)
    # butt joints along each strake
    up = (u / plank_len + offs[s]) % 1.0
    butt = np.minimum(up, 1.0 - up) * plank_len * b.aspect * strakes   # distance in strake heights
    seam_v = bevel(vl, strakes, 0.10)
    seam_u = dt.sstep(0.0, 0.06, butt)
    seam = seam_v * seam_u
    grain = b.noise(6, 48 * 1, 4) * 0.6 + b.noise(2, 160, 3) * 0.4
    tone = rng.random(strakes + 1).astype(np.float32)[s] - 0.5
    pl_id = np.floor(u / plank_len + offs[s]).astype(np.int32)
    tone2 = (np.sin(pl_id * 12.9898 + s * 78.233) * 43758.5453) % 1.0 - 0.5
    blot = b.noise(5, 3, 4)
    lum = 1.0 + var * (tone * 0.9 + tone2 * 0.6 + grain * 0.8 + blot * 0.5)
    rgb = col(base)[None, None, :] * lum[..., None]
    caulk = 1.0 - seam
    rgb = rgb * (1.0 - 0.75 * caulk[..., None])
    # weathering: salt bleach near top of each strake + general grime
    salt = np.clip(b.noise(3, 2, 3) * 1.5 + 0.1, 0, 1) * 0.12
    rgb = dt.mix3(rgb, col('#9a948a'), salt * (1 - tar * 0.6))
    height = seam * 0.006 + grain * 0.0012
    rough_a = rough + 0.08 * blot - 0.1 * tar * (grain > 0.1)
    return _out(b, rgb, height, rough_a)


def p_plank_tar(b):
    return planks(b, 6, '#30261c', 0.35, 0.58, tar=1.0)


def p_plank_oiled(b):
    return planks(b, 5, '#6c4a2e', 0.32, 0.62)


def p_deck(b):
    d = planks(b, 8, '#8a6f50', 0.30, 0.78, plank_len=0.22)
    # nail heads
    rng = b.rng()
    vl, u = b.vl, b.u
    s = (vl * 8) % 1.0
    nails = (lines(u, 64, 0.03) * (np.abs(s - 0.5) < 0.12)).astype(np.float32)
    d['rgb'] = d['rgb'] * (1 - 0.5 * nails[..., None])
    return d


def p_reed(b):
    vl, u = b.vl, b.u
    nb = 3
    t = (vl * nb) % 1.0
    bundle = np.sqrt(np.clip(1 - (2 * t - 1) ** 2, 0, 1))           # round cross-section
    stripes = b.noise(3, 220, 2) * 0.7 + b.noise(2, 90, 3) * 0.3
    lash = lines(u + 0.013 * np.floor(vl * nb), 9, 0.035)
    base = col('#a88a5a')
    lum = 0.78 + 0.32 * bundle + 0.30 * stripes
    rgb = base[None, None, :] * lum[..., None]
    dry = np.clip(b.noise(4, 4, 3) * 1.4, 0, 1)
    rgb = dt.mix3(rgb, col('#cdb07a')[None, None, :] * lum[..., None], dry * 0.5)
    rgb = dt.mix3(rgb, col('#4e3a24'), lash * 0.9)
    height = bundle * 0.03 + stripes * 0.002 + lash * 0.012
    rough = 0.88 - 0.05 * lash
    return _out(b, rgb, height, rough)


def steel(b, base, rough, rows=3, cols=6, rust=0.25, seed=0):
    vl, u = b.vl, b.u
    r = np.floor(vl * rows).astype(np.int32)
    off = (np.sin(r * 3.1 + seed) * 0.5 + 0.5) * 0.5
    seam_h = lines(vl, rows, 0.03)
    seam_v = lines(u + off / cols, cols, 0.004 * b.aspect)
    weld = np.maximum(seam_h, seam_v)
    blot = b.noise(4, 3, 4)
    dent = b.noise(12, 4, 3)
    rgb = col(base)[None, None, :] * (1 + 0.18 * blot + 0.06 * dent)[..., None]
    # rust streaks run down from horizontal seams
    streak_n = np.clip(b.noise(64, 2, 2) * 2.0, 0, 1)
    below = ((vl * rows) % 1.0)
    streak = streak_n * np.clip(1.0 - below * 1.6, 0, 1) * rust
    rgb = dt.mix3(rgb, col('#5a3220'), streak)
    rgb = rgb * (1 - 0.35 * weld[..., None])
    height = weld * 0.003 + dent * 0.002
    return _out(b, rgb, height, rough + 0.1 * blot + 0.1 * streak)


def p_steel_black(b):
    return steel(b, '#1f2023', 0.5, rust=0.35)


def p_steel_red(b):
    d = steel(b, '#7a2a1e', 0.72, rows=2, cols=5, rust=0.0, seed=2)
    algae = np.clip(b.vl - 0.75, 0, 1) * 4 * np.clip(b.noise(8, 3, 3) + 0.6, 0, 1)
    d['rgb'] = dt.mix3(d['rgb'], col('#3b3a26'), np.clip(algae, 0, 0.8))
    return d


def p_steel_deck(b):
    d = steel(b, '#4b524d', 0.82, rows=2, cols=4, rust=0.1, seed=4)
    scuff = np.clip(b.noise(20, 20, 3) * 2, 0, 1)
    d['rgb'] = dt.mix3(d['rgb'], col('#6a6f68'), scuff * 0.4)
    return d


def p_composite(b):
    vl, u = b.vl, b.u
    seam = np.maximum(lines(vl, 2, 0.012), lines(u + 0.5 * (np.floor(vl * 2) % 2) / 5, 5, 0.0025 * b.aspect))
    blot = b.noise(3, 2, 4)
    rgb = col('#e9e7e0')[None, None, :] * (1 + 0.05 * blot)[..., None]
    rgb = rgb * (1 - 0.45 * seam[..., None])
    streak = np.clip(b.noise(48, 2, 2) * 1.5, 0, 1) * 0.08
    rgb = dt.mix3(rgb, col('#9a968c'), streak)
    return _out(b, rgb, seam * -0.002, 0.32 + 0.06 * blot)


def p_stripe(b):
    wear = np.clip(b.noise(10, 3, 4) * 2.2 - 0.6, 0, 1)
    rgb = col('#ece8e0')[None, None, :] * (1 + 0.06 * b.noise(4, 2, 3))[..., None]
    rgb = dt.mix3(rgb, col('#8f8a80'), wear * 0.5)
    edge = lines(b.vl, 1, 0.06)
    rgb = rgb * (1 - 0.4 * edge[..., None])
    return _out(b, rgb, edge * -0.002, 0.5 + 0.2 * wear, mG=1.0 - 0.5 * wear)


def p_char(b):
    f1, f2, ids, _, _ = b.T.worley(24, 6)
    f1, f2 = f1[b.sl], f2[b.sl]
    crack = 1.0 - dt.sstep(0.0, 0.06, f2 - f1)
    ember = np.clip(b.noise(6, 3, 4) * 2.0, 0, 1)
    rgb = col('#1b1613')[None, None, :] * (1 + 0.5 * b.noise(16, 6, 3))[..., None]
    rgb = dt.mix3(rgb, col('#4a2d1b'), ember * 0.6)
    rgb = rgb * (1 - 0.6 * crack[..., None])
    return _out(b, rgb, -crack * 0.006 + b.noise(20, 8, 3) * 0.002, 0.93)


def p_sail(b, stripes=False, torn=False):
    vl, u = b.vl, b.u
    cloths = 8
    seam = lines(u, cloths, 0.02)
    weave = b.noise(4, 64, 2) * 0.5 + b.noise(96, 8, 2) * 0.5
    foot = np.clip(1.0 - vl * 3.0, 0, 1)
    reef = lines(vl - 0.82, 1, 0.012) * (lines(u, 48, 0.12))
    base = col('#efe6cf')
    rgb = base[None, None, :] * (1 + 0.05 * weave)[..., None]
    mG = np.zeros_like(u)
    if stripes:
        tint = (np.floor(u * cloths) % 2 == 0).astype(np.float32)
        rgb = dt.mix3(rgb, col('#f3f1ec')[None, None, :] * (1 + 0.05 * weave)[..., None], tint)
        mG = tint * (1.0 - 0.6 * seam)
    grime = foot * 0.22 + np.clip(b.noise(3, 3, 4) * 0.6, 0, 1) * 0.12
    rgb = dt.mix3(rgb, col('#8b7d63'), grime)
    if torn:
        rgb = rgb * col('#b7ab90')[None, None, :] / base[None, None, :]
        scorch = np.clip(b.noise(5, 4, 5) * 2.5 - 0.5, 0, 1)
        rgb = dt.mix3(rgb, col('#2b2119'), scorch)
        grime = grime + scorch
    rgb = rgb * (1 - 0.18 * seam[..., None])
    edge = np.clip(1 - np.minimum(vl, 1 - vl) * 30, 0, 1)
    rgb = rgb * (1 - 0.25 * edge[..., None])
    return _out(b, rgb, seam * 0.002 + weave * 0.0005 + reef * 0.002, 0.88 + 0.05 * grime, mG=mG)


def p_super_win(b):
    """One storey of a white deckhouse: a row of windows (about 0.9 x 1.0 m every 1.9 m), deck ledge at the foot."""
    vl, u = b.vl, b.u
    base = col('#e4e1d9')
    rgb = base[None, None, :] * (1 + 0.04 * b.noise(6, 3, 3))[..., None]
    nwin = 8
    fu = (u * nwin) % 1.0
    win = ((np.abs(vl - 0.56) < 0.19) & (np.abs(fu - 0.5) < 0.24)).astype(np.float32)
    frame = ((np.abs(vl - 0.56) < 0.215) & (np.abs(fu - 0.5) < 0.265)).astype(np.float32) - win
    wid = np.floor(u * nwin)
    lit = ((np.sin(wid * 7.13 + 1.7) * 0.5 + 0.5) > 0.3).astype(np.float32)
    ledge = (vl < 0.06).astype(np.float32)
    streak = np.clip(b.noise(64, 2, 2) * 2, 0, 1) * 0.18 * (1 - win) * np.clip(0.6 - vl, 0, 1) * 1.6
    rgb = dt.mix3(rgb, col('#7a6a58'), streak)
    rgb = dt.mix3(rgb, col('#9d9a92'), ledge * 0.8)
    rgb = rgb * (1 - 0.25 * frame[..., None])
    glass = col('#1a252e')[None, None, :] * (1 + 0.5 * vl - 0.25)[..., None]
    rgb = dt.mix3(rgb, glass, win)
    return _out(b, rgb, -win * 0.03 + ledge * 0.01 + frame * 0.004, 0.45 - 0.37 * win, metal=0.0, mR=win * lit)


def p_paint_white(b):
    rgb = col('#e0dcd2')[None, None, :] * (1 + 0.05 * b.noise(4, 3, 3))[..., None]
    streak = np.clip(b.noise(64, 2, 2) * 2, 0, 1) * 0.12
    rgb = dt.mix3(rgb, col('#6e6458'), streak)
    return _out(b, rgb, b.noise(16, 8, 2) * 0.0005, 0.45 + 0.1 * streak)



def p_char_roof(b):
    blis = np.clip(b.noise(24, 8, 3) * 2.0, 0, 1)
    rgb = col('#1e1a17')[None, None, :] * (1 + 0.4 * b.noise(8, 4, 3))[..., None]
    rgb = dt.mix3(rgb, col('#4a2a1a'), blis * 0.5)
    return _out(b, rgb, blis * 0.002, 0.9)


def p_glass(b):
    vl, u = b.vl, b.u
    mull = np.maximum(lines(u, 12, 0.04), lines(vl, 1, 0.05))
    rgb = col('#13202c')[None, None, :] * (1 + 0.25 * (1 - vl))[..., None]
    rgb = dt.mix3(rgb, col('#cfccc3'), mull)
    lit = ((np.sin(np.floor(u * 12) * 5.71) * 0.5 + 0.5) > 0.25).astype(np.float32) * (1 - mull)
    return _out(b, rgb, mull * 0.003, 0.08 + 0.4 * mull, metal=0.2 * (1 - mull), mR=lit)


def p_containers(b):
    vl, u = b.vl, b.u
    seg = np.floor(u * CONTAINER_SEGMENTS).astype(np.int32) % CONTAINER_SEGMENTS
    pal = np.stack([col(h) for h in ('#8a3828', '#2d4a6e', '#ad8636', '#3e6644', '#87898a', '#d4d2ca', '#6a3e2a', '#e6e4dc')])
    rgb = pal[seg]
    su = (u * CONTAINER_SEGMENTS) % 1.0
    ribs = np.sin(su * math.pi * 2 * 22) * 0.5 + 0.5
    frame = np.clip(1 - np.minimum(su, 1 - su) * 40, 0, 1)
    frame = np.maximum(frame, np.clip(1 - np.minimum(vl, 1 - vl) * 18, 0, 1))
    blot = b.noise(6, 3, 4)
    rgb = rgb * (1 + 0.12 * blot + 0.10 * (ribs - 0.5))[..., None]
    rust = np.clip(b.noise(80, 3, 2) * 2 - 0.3, 0, 1) * 0.25 * (1 - vl)
    rgb = dt.mix3(rgb, col('#4f2c1c'), rust)
    rgb = rgb * (1 - 0.35 * frame[..., None])
    mG = (seg == CONTAINER_SEGMENTS - 1).astype(np.float32) * (1 - frame * 0.7)
    return _out(b, rgb, ribs * 0.008 * (1 - frame) + frame * 0.004, 0.62 + 0.1 * blot, metal=0.1, mG=mG)


def p_spar(b):
    grain = b.noise(3, 70, 3) * 0.6 + b.noise(1, 200, 2) * 0.4
    rgb = col('#5b4532')[None, None, :] * (1 + 0.35 * grain + 0.15 * b.noise(6, 4, 3))[..., None]
    return _out(b, rgb, grain * 0.001, 0.72)


def p_rope(b):
    tw = np.sin((b.u * 160 + b.vl * 3) * math.pi * 2) * 0.5 + 0.5
    rgb = col('#4a3a28')[None, None, :] * (0.75 + 0.45 * tw)[..., None]
    return _out(b, rgb, tw * 0.002, 0.9)


def p_metal(b):
    n = b.noise(8, 4, 4)
    rgb = col('#2f3236')[None, None, :] * (1 + 0.25 * n)[..., None]
    rust = np.clip(b.noise(20, 6, 3) * 2 - 0.4, 0, 1) * 0.3
    rgb = dt.mix3(rgb, col('#5a3524'), rust)
    return _out(b, rgb, n * 0.001, 0.45 + 0.2 * rust, metal=0.75 - 0.6 * rust)


def p_crew(b):
    n = b.noise(12, 6, 3)
    rgb = col('#2a221a')[None, None, :] * (1 + 0.3 * n)[..., None]
    # a lighter band for faces/skin at the top of the band (heads map there)
    skin = (b.vl > 0.82).astype(np.float32)
    rgb = dt.mix3(rgb, col('#6a4a34'), skin)
    return _out(b, rgb, n * 0.001, 0.9)


def p_flag(b):
    weave = b.noise(128, 16, 2) * 0.5 + b.noise(6, 3, 3) * 0.5
    rgb = col('#f1efe9')[None, None, :] * (1 + 0.07 * weave)[..., None]
    edge = np.clip(1 - np.minimum(b.vl, 1 - b.vl) * 25, 0, 1)
    rgb = rgb * (1 - 0.15 * edge[..., None])
    return _out(b, rgb, weave * 0.0006, 0.86, mG=1.0 - 0.3 * edge)


def p_gold(b):
    brush = b.noise(2, 120, 2)
    rgb = col('#c9a35a')[None, None, :] * (1 + 0.1 * brush)[..., None]
    return _out(b, rgb, brush * 0.0004, 0.3 + 0.08 * brush, metal=1.0)


def p_glow(b):
    rgb = col('#8fd0ff')[None, None, :] * (0.85 + 0.15 * np.sin(b.vl * math.pi))[..., None]
    return _out(b, rgb, 0.0, 0.2, mB=1.0)


def p_oar(b):
    grain = b.noise(3, 90, 3)
    rgb = col('#8b6b47')[None, None, :] * (1 + 0.25 * grain + 0.1 * b.noise(5, 3, 3))[..., None]
    return _out(b, rgb, grain * 0.001, 0.7)


def p_cargo(b):
    vl, u = b.vl, b.u
    f1, f2, ids, _, _ = b.T.worley(12, 2)
    f1, f2, ids = f1[b.sl], f2[b.sl], ids[b.sl]
    r = b.T.cell_rand(ids, 12 * 2)
    pal = np.stack([col('#a5814f'), col('#8f6a3e'), col('#9a5a3a'), col('#b49a6a')])
    rgb = pal[(r * 4).astype(np.int32) % 4]
    bulge = np.clip(1 - f1 * 1.6, 0, 1)
    weave = b.noise(80, 40, 2)
    rgb = rgb * (0.7 + 0.4 * bulge + 0.1 * weave)[..., None]
    edge = 1.0 - dt.sstep(0.0, 0.08, f2 - f1)
    rgb = rgb * (1 - 0.5 * edge[..., None])
    return _out(b, rgb, bulge * 0.02 + weave * 0.001, 0.9)


PAINTERS = {
    'plank_tar': p_plank_tar, 'plank_oiled': p_plank_oiled, 'deck': p_deck, 'reed': p_reed,
    'steel_black': p_steel_black, 'steel_red': p_steel_red, 'steel_deck': p_steel_deck, 'composite': p_composite,
    'stripe': p_stripe, 'char': p_char,
    'sail': p_sail, 'sail_stripe': lambda b: p_sail(b, stripes=True), 'sail_torn': lambda b: p_sail(b, torn=True),
    'super_win': p_super_win, 'paint_white': p_paint_white, 'char_roof': p_char_roof, 'glass': p_glass,
    'containers': p_containers, 'spar': p_spar, 'rope': p_rope, 'metal': p_metal, 'crew': p_crew, 'flag': p_flag,
    'gold': p_gold, 'glow': p_glow, 'oar': p_oar, 'cargo': p_cargo,
}


def build_sheet(sheet, out_dir, seed):
    T = dt.Tex(N, seed)
    rgb = np.zeros((N, N, 3), np.float32)
    height = np.zeros((N, N), np.float32)
    rough = np.zeros((N, N), np.float32)
    metal = np.zeros((N, N), np.float32)
    m = np.zeros((N, N, 3), np.float32)
    y = 0
    log = []
    for name, h in SHEETS[sheet]:
        b = Band(T, y, h)
        d = PAINTERS[name](b)
        rgb[b.sl] = d['rgb']
        height[b.sl] = d['height']
        rough[b.sl] = d['rough']
        metal[b.sl] = d['metal']
        m[b.sl] = np.stack([d['mR'], d['mG'], d['mB']], -1)
        log.append(dt.stats('  %s/%s' % (sheet, name), d['rgb']))
        y += h
    # normals: treat the sheet as 4 m across; blur seams between bands is unnecessary (heights are small)
    nrm = dt.normal_from_height(height, 4.0, N)
    ao = dt.cavity_ao(height, N, 0.03, 4.0, 60.0)
    orm = np.stack([ao, np.clip(rough, 0.04, 1.0), np.clip(metal, 0, 1)], -1)
    base = os.path.join(out_dir, 'T_Ship_%s' % sheet)
    dt.save_png(base + '_D.png', np.clip(rgb, 0, 1), srgb=True)
    dt.save_png(base + '_N.png', nrm, srgb=False)
    dt.save_png(base + '_ORM.png', orm, srgb=False)
    dt.save_png(base + '_M.png', np.clip(m, 0, 1), srgb=False)
    return log


def build_all(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    log = []
    for i, sheet in enumerate(SHEETS):
        log += build_sheet(sheet, out_dir, 7100 + i * 13)
    return log
