# DEMIURGE art - Age6 (Space Age) procedural texture sets.
#
# Runs inside Blender's Python (numpy). Uses the shared periodic-noise helpers in art/tools/dmtex.py.
# Every set is 1024^2, tileable, seeded, so `build_age6.py` rebuilds it bit for bit.
#
# Sets (T_Age6_<Set>_<D|N|ORM|M>):
#   Facade    7.2 m tile = 2 storeys x 3 bays of 2.4 m: cream panels, pale-gold sill band, white piers,
#             blue-grey glass with mullions and blinds.  M: R lit-window value (night emissive),
#             G body tint mask (culture tint), B accent mask (pale-gold band).
#   GreenRoof 8 m tile: grass, shrub beds, flowers (no paving, so hedges and trees can use any texel).
#   Tech      atlas (regions below, not tiling): solar cells, white metal, dark steel, gold, black,
#             signal orange, red lamp, light glazing.  M: R emissive, G tint mask, B accent mask.
#   Concrete  16 m tile: pale apron slabs with 4 m joints and stains.
#   Hull      6 m tile: white painted rocket skin with faint panel seams.
#   Cladding  12 m tile: pale ribbed metal panels with weathering streaks.
import os
import numpy as np
import dmtex as dt

N = 1024

# Tech atlas regions (u0, v0, u1, v1); shared with the geometry code.
TECH_REG = {
    'solar':   (0.0, 0.0, 0.5, 0.5),
    'white':   (0.5, 0.0, 1.0, 0.5),
    'steel':   (0.0, 0.5, 0.5, 1.0),
    'gold':    (0.5, 0.5, 0.75, 0.75),
    'black':   (0.75, 0.5, 1.0, 0.75),
    'orange':  (0.5, 0.75, 0.625, 1.0),
    'redlamp': (0.625, 0.75, 0.75, 1.0),
    'glass':   (0.75, 0.75, 1.0, 1.0),
}

FACADE_TILE = 7.2
ROOF_TILE = 8.0
CONCRETE_TILE = 16.0
HULL_TILE = 6.0
CLAD_TILE = 12.0


def _save_set(out, name, d, nrm, orm, m=None):
    p = os.path.join(out, 'T_Age6_%s_' % name)
    dt.save_png(p + 'D.png', d, srgb=True)
    dt.save_png(p + 'N.png', nrm, srgb=False)
    dt.save_png(p + 'ORM.png', orm, srgb=False)
    if m is not None:
        dt.save_png(p + 'M.png', m, srgb=False)
    return dt.stats(name, d)


def facade(out):
    T = dt.Tex(N, 6101)
    S = FACADE_TILE
    x = T.u * S
    y = T.v * S
    ys = y % 3.6
    storey = (y // 3.6).astype(np.int32)
    xs = x % 2.4
    bay = (x // 2.4).astype(np.int32)

    pier = (xs < 0.2) | (xs > 2.2)
    panel = ((ys < 1.15) | (ys > 3.42)) & ~pier
    gold = (ys > 1.05) & (ys < 1.15) & ~pier
    glass = ~panel & ~pier
    mull = glass & (np.abs(xs - 1.2) < 0.035)
    trans = glass & (np.abs(ys - 3.02) < 0.03)
    frame = mull | trans
    pane_glass = glass & ~frame

    side = (xs > 1.2).astype(np.int32)
    pid = storey * 6 + bay * 2 + side           # 12 panes per tile
    rng = np.random.default_rng(611)
    p_lit = rng.random(12)
    p_bri = rng.random(12)
    p_blind = rng.random(12)
    p_tone = rng.random(12)

    n1 = T.fbm(8, 8, 5)
    n2 = T.fbm(64, 64, 3)
    streak = T.fbm(48, 3, 4)                    # vertical streaks (stretched in v)

    # --- albedo
    c_panel = dt.hex_lin('#e9e2d2')
    c_pier = dt.hex_lin('#f0ebdf')
    c_gold = dt.hex_lin('#c6a564')
    c_glass_lo = dt.hex_lin('#3a4a55')
    c_glass_hi = dt.hex_lin('#93a7b3')
    c_blind = dt.hex_lin('#a59c8b')
    c_frame = dt.hex_lin('#dcd8cf')

    rgb = np.zeros((N, N, 3), np.float32)
    pv = 1.0 + 0.05 * n1 + 0.02 * n2
    # panel joints every 1.2 m and a reveal mid-band
    joint = (np.abs(((x + 0.6) % 1.2) - 0.6) < 0.012) & panel & (ys < 1.05)
    reveal = (np.abs(ys - 0.42) < 0.01) & panel
    grime = 1.0 - 0.07 * dt.sstep(0.0, 0.6, streak * 0.5 + 0.5) * (ys < 1.05) * (1 - ys / 1.05)
    pan = c_panel * (pv * grime)[..., None]
    pan = np.where((joint | reveal)[..., None], pan * 0.72, pan)
    rgb = np.where(panel[..., None], pan, rgb)
    rgb = np.where(pier[..., None], c_pier * (pv * (1.0 - 0.04 * np.clip(streak, 0, 1)))[..., None], rgb)
    rgb = np.where(gold[..., None], c_gold * (1.0 + 0.04 * n2)[..., None], rgb)
    # glass: sky-reflection gradient toward the head of the window, per-pane variation, blinds
    t = np.clip((ys - 1.15) / 2.27, 0, 1)
    gl = dt.mix3(c_glass_lo, c_glass_hi, (t ** 1.6) * (0.55 + 0.45 * p_bri[pid]))
    gl = gl * (0.85 + 0.3 * p_tone[pid])[..., None]
    bl_h = np.where(p_blind[pid] > 0.55, (p_blind[pid] - 0.55) * 2.0 * 2.2, 0.0)
    blinds = pane_glass & (ys > 3.42 - bl_h)
    gl = np.where(blinds[..., None], dt.mix3(gl, c_blind, np.full(gl.shape[:2], 0.45, np.float32)), gl)
    rgb = np.where(pane_glass[..., None], gl, rgb)
    rgb = np.where(frame[..., None], c_frame * pv[..., None], rgb)

    # --- height (m)
    h = np.zeros((N, N), np.float32)
    h = np.where(panel, 0.05, h)
    h = np.where(gold, 0.065, h)
    h = np.where(pier, 0.15, h)
    h = np.where(frame, 0.03, h)
    h = np.where(joint | reveal, 0.035, h)
    h = h + 0.0015 * n2
    hb = dt.blur(h, 1)
    nrm = dt.normal_from_height(hb, S, N)
    ao = dt.cavity_ao(hb, N, 0.08, S, 18.0)

    rough = np.full((N, N), 0.55, np.float32) + 0.06 * n1
    rough = np.where(pier, 0.5, rough)
    rough = np.where(gold, 0.3, rough)
    rough = np.where(pane_glass, 0.05 + 0.04 * p_tone[pid], rough)
    rough = np.where(frame, 0.4, rough)
    metal = np.where(gold, 1.0, 0.0).astype(np.float32)
    metal = np.where(frame, 0.6, metal)
    orm = np.stack([ao, np.clip(rough, 0.02, 1), metal], -1)

    lit = np.where(p_lit[pid] > 0.55, 0.25 + 0.75 * p_bri[pid], 0.0)
    lit = np.where(blinds, lit * 0.7, lit)
    m = np.stack([np.where(pane_glass, lit, 0.0),
                  np.where(panel | pier, 1.0, 0.0),
                  np.where(gold, 1.0, 0.0)], -1).astype(np.float32)
    return _save_set(out, 'Facade', rgb, nrm, orm, m)


def green_roof(out):
    T = dt.Tex(N, 6202)
    S = ROOF_TILE
    n1 = T.fbm(6, 6, 5)
    n2 = T.fbm(40, 40, 3)
    n3 = T.ridged(24, 24, 3)
    wu = T.u + 0.035 * T.fbm(5, 5, 3)
    wv = T.v + 0.035 * T.fbm(5, 5, 3)
    f1, f2, idc, cu, cv = T.worley(9, 9, u=wu, v=wv, jitter=0.95)
    f1 = f1 * (1.0 + 0.35 * T.fbm(20, 20, 3))
    cr = T.cell_rand(idc, 81)
    cr2 = T.cell_rand(idc, 81, 7)
    path = np.zeros((N, N), bool)          # no paving: foliage meshes sample this tile anywhere
    edge = np.zeros((N, N), bool)
    shrub = (f1 < 0.38 + 0.08 * n1) & (cr < 0.62) 
    flowers = (cr2 > 0.86) & (f1 < 0.3) & ~path & (n2 > 0.15)

    c_grass_a = dt.hex_lin('#4d6531')
    c_grass_b = dt.hex_lin('#69793d')
    c_shrub_a = dt.hex_lin('#2a4322')
    c_shrub_b = dt.hex_lin('#4f6c33')
    c_path = dt.hex_lin('#cbc2b0')
    c_soil = dt.hex_lin('#4c3e2d')
    c_fl1 = dt.hex_lin('#d9c46a')
    c_fl2 = dt.hex_lin('#c7849a')

    g = dt.mix3(c_grass_a, c_grass_b, np.clip(n1 * 0.9 + 0.5, 0, 1))
    g = g * (0.9 + 0.2 * n2)[..., None]
    sh = dt.mix3(c_shrub_a, c_shrub_b, np.clip(n3 * 1.3 - 0.35 + 0.3 * cr, 0, 1))
    rgb = np.where(shrub[..., None], sh, g)
    ring = (f1 > 0.38 + 0.08 * n1) & (f1 < 0.44 + 0.08 * n1) & (cr < 0.62)
    rgb = np.where((ring & ~path)[..., None], dt.mix3(rgb, c_soil, np.full(n1.shape, 0.5, np.float32)), rgb)
    fl = np.where((cr2 > 0.93)[..., None], c_fl2, c_fl1)
    rgb = np.where((flowers & (n2 > 0.32))[..., None], fl, rgb)
    tiles = (np.abs(((T.u * S) % 0.6) - 0.3) > 0.285)
    pv = c_path * (0.95 + 0.06 * n2)[..., None]
    pv = np.where(tiles[..., None], pv * 0.8, pv)
    rgb = np.where(path[..., None], pv, rgb)
    rgb = np.where(edge[..., None], dt.hex_lin('#9d9686') * (0.95 + 0.05 * n2)[..., None], rgb)

    h = 0.02 * n2
    h = np.where(shrub, 0.12 + 0.28 * np.clip(1.0 - f1 / 0.45, 0, 1) * (0.6 + 0.4 * n3), h)
    h = np.where(path, -0.02, h)
    h = np.where(edge, 0.06, h)
    hb = dt.blur(h, 1)
    nrm = dt.normal_from_height(hb, S, N)
    ao = dt.cavity_ao(hb, N, 0.3, S, 6.0)
    rough = np.where(path | edge, 0.72, 0.9 + 0.05 * n1).astype(np.float32)
    orm = np.stack([ao, np.clip(rough, 0, 1), np.zeros_like(ao)], -1)
    return _save_set(out, 'GreenRoof', rgb, nrm, orm)


def tech(out):
    T = dt.Tex(N, 6303)
    u, v = T.u, T.v
    n2 = T.fbm(64, 64, 3)
    n1 = T.fbm(8, 8, 4)
    rgb = np.zeros((N, N, 3), np.float32)
    h = np.zeros((N, N), np.float32)
    rough = np.zeros((N, N), np.float32)
    metal = np.zeros((N, N), np.float32)
    em = np.zeros((N, N), np.float32)
    tint = np.zeros((N, N), np.float32)
    acc = np.zeros((N, N), np.float32)

    def reg(name):
        u0, v0, u1, v1 = TECH_REG[name]
        return (u >= u0) & (u < u1) & (v >= v0) & (v < v1), (u - u0) / (u1 - u0), (v - v0) / (v1 - v0)

    # solar module: 6 x 10 cells with a frame
    m, lu, lv = reg('solar')
    frame = (lu < 0.025) | (lu > 0.975) | (lv < 0.02) | (lv > 0.98)
    cu = (lu - 0.025) / 0.95 * 6
    cv = (lv - 0.02) / 0.96 * 10
    grid = ((np.abs(cu - np.round(cu)) < 0.045) | (np.abs(cv - np.round(cv)) < 0.03)) & ~frame
    bus = (np.abs(((cu * 3) % 1.0) - 0.5) < 0.03) & ~frame & ~grid
    cell = ~frame & ~grid
    c_cell = dt.mix3(dt.hex_lin('#142446'), dt.hex_lin('#2a4570'), np.clip(lv * 0.6 + 0.2 * n1 + 0.2, 0, 1))
    sol = np.where(cell[..., None], c_cell, dt.hex_lin('#b9c0c7'))
    sol = np.where(bus[..., None], dt.hex_lin('#7d8ea3'), sol)
    rgb = np.where(m[..., None], sol, rgb)
    h = np.where(m, np.where(frame, 0.012, np.where(cell, 0.0, 0.002)), h)
    rough = np.where(m, np.where(cell, 0.08 + 0.03 * n2, 0.35), rough)
    metal = np.where(m & ~cell, 1.0, metal)

    def flat(name, hexc, r, mt, var=0.04, e=0.0, ti=0.0, ac=0.0):
        nonlocal rgb, h, rough, metal, em, tint, acc
        mm, _, _ = reg(name)
        rgb = np.where(mm[..., None], dt.hex_lin(hexc) * (1.0 + var * n2 + var * 0.7 * n1)[..., None], rgb)
        rough = np.where(mm, r + 0.05 * n1, rough)
        metal = np.where(mm, mt, metal)
        em = np.where(mm, e, em)
        tint = np.where(mm, ti, tint)
        acc = np.where(mm, ac, acc)
        h = np.where(mm, 0.002 * n2, h)

    flat('white', '#e7e8e3', 0.35, 0.0, 0.03, ti=1.0)
    flat('steel', '#3b3e42', 0.45, 0.85, 0.08)
    flat('gold', '#c7a660', 0.28, 1.0, 0.04, ac=1.0)
    flat('black', '#141516', 0.6, 0.3, 0.05)
    flat('orange', '#d0542a', 0.5, 0.0, 0.04)
    flat('redlamp', '#e0281c', 0.4, 0.0, 0.02, e=1.0)
    flat('glass', '#8fb3c6', 0.07, 0.0, 0.03, e=0.55)
    nrm = dt.normal_from_height(dt.blur(h, 1), 2.0, N)
    ao = np.ones((N, N), np.float32)
    orm = np.stack([ao, np.clip(rough, 0.02, 1), metal], -1)
    mk = np.stack([em, tint, acc], -1).astype(np.float32)
    return _save_set(out, 'Tech', rgb, nrm, orm, mk)


def concrete(out):
    T = dt.Tex(N, 6404)
    S = CONCRETE_TILE
    x, y = T.u * S, T.v * S
    n1 = T.fbm(4, 4, 5)
    n2 = T.fbm(96, 96, 3)
    n3 = T.fbm(12, 12, 4)
    sx = (x // 4).astype(np.int32)
    sy = (y // 4).astype(np.int32)
    sid = sy * 4 + sx
    rs = np.random.default_rng(641).random(16)
    joint = (np.abs(((x + 2) % 4) - 2) > 1.985) | (np.abs(((y + 2) % 4) - 2) > 1.985)
    base = dt.hex_lin('#c5c0b5')
    tone = 0.93 + 0.1 * rs[sid] + 0.05 * n1 + 0.03 * n2
    stain = np.clip(n3 * 1.6 - 0.35, 0, 1) * 0.18
    rgb = base * (tone * (1.0 - stain))[..., None]
    rgb = np.where(joint[..., None], rgb * 0.55, rgb)
    h = 0.002 * n2 + np.where(joint, -0.015, 0.0) + 0.001 * rs[sid]
    nrm = dt.normal_from_height(dt.blur(h, 1), S, N)
    ao = dt.cavity_ao(h, N, 0.05, S, 30.0)
    rough = np.clip(0.78 + 0.08 * n1 - 0.1 * stain, 0, 1)
    orm = np.stack([ao, rough, np.zeros_like(ao)], -1)
    return _save_set(out, 'Concrete', rgb, nrm, orm)


def hull(out):
    T = dt.Tex(N, 6505)
    S = HULL_TILE
    x, y = T.u * S, T.v * S
    n1 = T.fbm(6, 6, 4)
    n2 = T.fbm(80, 80, 3)
    ring = np.abs(((y + 0.75) % 1.5) - 0.75) > 0.742
    row = (y // 1.5).astype(np.int32)
    vx = (x + (row % 2) * 1.0) % 2.0
    vseam = (vx < 0.008) | (vx > 1.992)
    seam = ring | vseam
    rgb = dt.hex_lin('#ecece7') * (1.0 + 0.025 * n1 + 0.012 * n2)[..., None]
    rgb = np.where(seam[..., None], rgb * 0.82, rgb)
    h = np.where(seam, -0.003, 0.0) + 0.0004 * n2
    nrm = dt.normal_from_height(dt.blur(h, 1), S, N)
    ao = np.where(seam, 0.85, 1.0).astype(np.float32)
    rough = (0.32 + 0.05 * n1).astype(np.float32)
    orm = np.stack([ao, rough, np.zeros_like(ao)], -1)
    return _save_set(out, 'Hull', rgb, nrm, orm)


def cladding(out):
    T = dt.Tex(N, 6606)
    S = CLAD_TILE
    x, y = T.u * S, T.v * S
    n1 = T.fbm(6, 6, 4)
    n2 = T.fbm(64, 64, 3)
    streak = T.fbm(40, 2, 4)
    rib = np.abs(((x % 0.6) / 0.6) - 0.5) * 2.0          # 0 centre of rib .. 1 valley
    rib_h = 0.04 * (1.0 - dt.sstep(0.35, 0.65, rib))
    seam = np.abs(((y + 2) % 4) - 2) > 1.985
    panel_id = ((y // 4).astype(np.int32) * 3 + (x // 4).astype(np.int32)) % 9
    rp = np.random.default_rng(661).random(9)
    base = dt.hex_lin('#bfc3c3')
    tone = 0.94 + 0.08 * rp[panel_id] + 0.03 * n1
    weather = 1.0 - 0.1 * np.clip(streak * 1.5 + 0.2, 0, 1)
    rgb = base * (tone * weather * (0.94 + 0.06 * (1 - rib)))[..., None]
    rgb = np.where(seam[..., None], rgb * 0.6, rgb)
    h = rib_h + np.where(seam, -0.01, 0.0) + 0.0005 * n2
    nrm = dt.normal_from_height(dt.blur(h, 1), S, N)
    ao = dt.cavity_ao(h, N, 0.15, S, 25.0)
    rough = np.clip(0.42 + 0.08 * n1 + 0.08 * (1 - weather), 0, 1).astype(np.float32)
    metal = np.full((N, N), 0.65, np.float32)
    orm = np.stack([ao, rough, metal], -1)
    return _save_set(out, 'Cladding', rgb, nrm, orm)


def build_all(out):
    os.makedirs(out, exist_ok=True)
    return [facade(out), green_roof(out), tech(out), concrete(out), hull(out), cladding(out)]
