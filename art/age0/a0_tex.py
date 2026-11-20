# DEMIURGE art - Age0 (Stone Age) family: procedural trim sheets (numpy, runs inside Blender's Python).
#
# Three 1024^2 texture sets, one per material slot, each split into horizontal bands (trim sheet).
# A band tiles in U; geometry maps V inside the band (the generators in a0_geo.py know the layout).
# Rows are v-up (row 0 = UV v 0), which is what dmtex.save_png expects.
#
#   T_Age0_Structure  256 px/m, U tile 4 m      T_Age0_Roof  256 px/m, U tile 4 m
#     v 0.00-0.25  bark poles (grain along U)      v 0.00-0.50  thatch course (2 m; cut straw ends at v 0)
#     v 0.25-0.50  weathered split / stripped wood v 0.50-1.00  sewn hide panels (2 m)
#     v 0.50-0.75  wattle and daub (1 m tall)
#     v 0.75-1.00  packed earth with pebbles
#   T_Age0_Detail
#     v 0.000-0.500  lichened field stone, 128 px/m, U tile 8 m, 4 m tall (V = height above ground)
#     v 0.500-0.625  plain dark leather / fur (0.5 m)
#     v 0.625-0.750  painted hide, culture paint in M.G (0.5 m)
#     v 0.750-0.875  patches (no tiling): u 0-.25 glowing embers, .25-.5 cold char + ash, .5-1 dark interior
#     v 0.875-1.000  twisted cord / lashing (0.5 m)
#
# Per set: _D (sRGB albedo), _N (OpenGL normal, +Y up), _ORM (AO, roughness, metallic), _M (mask, linear):
#   M.R night emissive (doorways, smoke holes), M.G culture-colour paint, M.B moss / grime, M.A always-on fire.
import os
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import dmtex as dt  # noqa: E402

N = 1024


def _rows(a, v0, v1):
    return a[int(round(v0 * N)):int(round(v1 * N))]


def _put(dst, src, v0, v1):
    dst[int(round(v0 * N)):int(round(v1 * N))] = _rows(src, v0, v1)


def _metres(T, m):
    """x, y in metres over a canvas that spans m metres both ways."""
    return T.u * m, T.v * m


def hexs(h):
    return dt.hex_lin(h)


# --------------------------------------------------------------------------- Structure bands

def bark(seed):
    T = dt.Tex(N, seed)
    x, y = _metres(T, 4.0)
    warp = T.value(4, 16) * 0.012
    fis = T.ridged(12, 96, 4, u=T.u, v=T.v + warp)           # long fissures along U
    fine = T.fbm(64, 256, 3)
    plates = T.value(24, 48)
    h = 0.006 * fis + 0.0015 * fine + 0.002 * plates
    f1, f2, ids, _, _ = T.worley(10, 6, jitter=0.8, aspect=0.6)
    knot = dt.sstep(0.18, 0.0, f1) * (T.cell_rand(ids, 60) > 0.8)
    h -= 0.004 * knot
    dark, mid, light = hexs('#2a2018'), hexs('#4d3f31'), hexs('#76685a')
    a = dt.mix3(dark, mid, dt.sstep(0.45, 0.75, fis))
    a = dt.mix3(a, light, np.clip(dt.sstep(0.80, 0.95, fis) * (0.5 + 0.5 * plates), 0, 1))
    lich = dt.sstep(0.25, 0.5, T.fbm(8, 16, 4)) * dt.sstep(0.7, 0.85, fis)
    a = dt.mix3(a, hexs('#7d8068'), lich * 0.55)
    a = dt.mix3(a, hexs('#1d1611'), knot * 0.8)
    a *= (0.85 + 0.3 * T.value(6, 3)[..., None] * 0.5 + 0.15)
    rough = 0.82 + 0.12 * (1 - fis)
    return a, h, rough, lich * 0.6


def wood(seed):
    T = dt.Tex(N, seed)
    warp = T.value(3, 12) * 0.02
    grain = T.fbm(6, 160, 4, u=T.u, v=T.v + warp)
    fine = T.value(32, 512)
    crack = dt.sstep(0.025, 0.0, np.abs(T.value(2, 70, u=T.u, v=T.v + warp * 0.3))) * dt.sstep(0.55, 0.8, T.value(6, 24) * 0.5 + 0.5)
    h = 0.002 * grain + 0.0008 * fine - 0.004 * crack
    base = dt.mix3(hexs('#6a5a49'), hexs('#9a8a74'), np.clip(grain * 0.7 + 0.5, 0, 1))
    base = dt.mix3(base, hexs('#b3a58e'), dt.sstep(0.3, 0.7, fine) * 0.25)
    base = dt.mix3(base, hexs('#2c231b'), crack * 0.85)
    stain = dt.sstep(0.1, 0.6, T.fbm(4, 8, 4))
    base = dt.mix3(base, hexs('#4a3e33'), stain * 0.45)
    rough = 0.78 + 0.15 * crack + 0.05 * stain
    return base, h, rough, stain * 0.4


def wattle(seed):
    """Woven withies (horizontal, along U) round upright stakes every 0.33 m, smeared with daub."""
    T = dt.Tex(N, seed)
    x, y = _metres(T, 4.0)
    yb = y % 1.0                                         # each 1 m band is a wall height
    nst = 12                                             # stakes per 4 m
    sx = x * nst / 4.0
    sid = np.floor(sx)
    sf = sx - sid
    row = np.floor(yb / 0.05)
    phase = (row + sid) % 2                              # over / under
    rf = (yb / 0.05) % 1.0
    withy = np.sqrt(np.clip(1 - (2 * rf - 1) ** 2, 0, 1))
    bulge = np.where(phase > 0.5, np.sin(np.pi * sf), np.sin(np.pi * sf) * 0.25)
    hw = 0.012 * withy + 0.01 * bulge
    jitter = T.value(64, 64) * 0.002
    hw += jitter
    daubn = T.fbm(10, 10, 5) + 0.25 * T.fbm(40, 40, 3)
    cover = dt.sstep(-0.15, 0.05, daubn + 0.22 - 0.3 * dt.sstep(0.8, 1.0, yb))
    f1, f2, _, _, _ = T.worley(28, 28, jitter=0.9)
    crack = dt.sstep(0.02, 0.0, f2 - f1) * cover * dt.sstep(0.0, 0.4, T.fbm(8, 8, 3))
    hd = 0.02 + 0.006 * T.fbm(24, 24, 4) - 0.006 * crack
    h = np.maximum(hw, hd * cover + hw * (1 - cover) * 0.6)
    wcol = dt.mix3(hexs('#3e3024'), hexs('#76624a'), withy * (0.6 + 0.4 * (T.value(48, 300) * 0.5 + 0.5)))
    dcol = dt.mix3(hexs('#7a6248'), hexs('#a68d6a'), np.clip(T.fbm(16, 16, 4) * 0.8 + 0.5, 0, 1))
    a = dt.mix3(wcol, dcol, cover)
    a = dt.mix3(a, hexs('#3c2e22'), crack * 0.7)
    splash = dt.sstep(0.22, 0.0, yb) * (0.6 + 0.4 * T.value(30, 6))
    a = dt.mix3(a, hexs('#4a3a2b'), np.clip(splash, 0, 1) * 0.7)
    rough = 0.88 + 0.08 * cover
    return a, h, rough, np.clip(splash, 0, 1) * 0.5


def earth(seed):
    T = dt.Tex(N, seed)
    f1, f2, ids, _, _ = T.worley(90, 90, jitter=0.95)
    r = T.cell_rand(ids, 90 * 90)
    peb = dt.sstep(0.38, 0.2, f1) * (r > 0.86) * dt.sstep(-0.1, 0.3, T.fbm(8, 8, 3))
    base_n = T.fbm(12, 12, 6)
    h = 0.01 * base_n + 0.008 * peb + 0.002 * T.value(128, 128)
    a = dt.mix3(hexs('#4e3b2a'), hexs('#7a6248'), np.clip(base_n * 0.9 + 0.5, 0, 1))
    pc = dt.mix3(hexs('#5c564d'), hexs('#7f766a'), T.cell_rand(ids, 90 * 90, 3))
    a = dt.mix3(a, pc, peb)
    grass = dt.sstep(0.35, 0.6, T.fbm(6, 6, 5)) * dt.sstep(0.3, 0.7, T.value(200, 60) * 0.5 + 0.5)
    a = dt.mix3(a, hexs('#4f5a2e'), grass * 0.6)
    rough = 0.9 - 0.15 * peb
    return a, h, rough, grass


# --------------------------------------------------------------------------- Roof bands

def thatch(seed):
    """One course of bundled straw: strands run along V; v=0 (band bottom) is the cut, stubbly lower edge."""
    T = dt.Tex(N, seed)
    x, y = _metres(T, 4.0)
    yb = y % 2.0
    wu = T.u + T.value(6, 24) * 0.006
    bundles = 0.7 * T.value(28, 3, u=wu) + 0.3 * T.value(56, 5, u=wu)  # ~14 cm bundles
    strand = 0.6 * T.value(700, 6, u=wu) + 0.4 * T.value(1400, 12, u=wu)
    strand2 = T.value(350, 20, u=wu)
    sid = np.floor(wu * 900)
    endr = np.random.default_rng(seed + 7).random(1024 * 4, dtype=np.float32)[(sid.astype(np.int64)) % 4096]
    endy = 0.02 + 0.10 * endr + 0.03 * (bundles * 0.5 + 0.5)
    alive = dt.sstep(endy - 0.01, endy + 0.01, yb)
    h = (0.010 * strand + 0.006 * strand2 + 0.012 * bundles) * alive + (-0.02) * (1 - alive)
    h += 0.01 * dt.sstep(0.35, 0.05, yb) * alive          # thick lower lip
    # colour: weathered grey-brown outside, warmer gold deeper and in fresh bundles
    fresh = dt.sstep(0.0, 0.8, T.fbm(5, 3, 4) * 0.8 + bundles * 0.4)
    a = dt.mix3(hexs('#4f3f2e'), hexs('#8f7450'), np.clip(strand * 0.6 + 0.5 + 0.25 * bundles, 0, 1))
    a = dt.mix3(a, hexs('#a68655'), fresh * 0.5 * np.clip(strand2 * 0.5 + 0.6, 0, 1))
    grey = dt.sstep(0.2, 0.7, T.fbm(4, 8, 4) * 0.5 + 0.5)
    a = dt.mix3(a, hexs('#6c645a'), grey * 0.35)
    gap = (1 - alive)
    a = dt.mix3(a, hexs('#2a2016'), gap * 0.9)
    ends = dt.sstep(0.0, 0.08, yb - endy) * alive
    a = dt.mix3(hexs('#4d3d2c') * np.ones_like(a), a, 0.35 + 0.65 * ends)  # strand ends darker (end grain)
    moss = dt.sstep(0.45, 0.75, T.fbm(6, 4, 5) * 0.5 + 0.5) * dt.sstep(1.6, 0.3, yb)
    rough = 0.9 + 0.06 * gap
    return a, h, rough, moss


def hide(seed):
    T = dt.Tex(N, seed)
    x, y = _metres(T, 4.0)
    yb = y % 2.0
    rowi = np.floor((yb + 0.12 * T.value(8, 4)) / 1.0)
    xs = (x + 0.61 * (rowi % 2) + 0.15 * T.value(4, 6)) / 1.33
    px = xs % 1.0
    py = ((yb + 0.12 * T.value(8, 4)) % 1.0)
    ex = np.minimum(px, 1 - px)
    ey = np.minimum(py, 1 - py)
    e = np.minimum(ex, ey)
    pid = (np.floor(xs) + 5 * rowi).astype(np.int64) % 16
    pr = np.random.default_rng(seed).random(16, dtype=np.float32)[pid]
    wr = T.ridged(10, 14, 4) * 0.7 + 0.3 * T.ridged(40, 30, 3)
    seam = dt.sstep(0.025, 0.0, e)
    st = dt.sstep(0.5, 0.9, np.sin(((x + yb) * 2 * np.pi * 25)) ) * dt.sstep(0.05, 0.03, e) * dt.sstep(0.015, 0.03, e)
    h = 0.004 * wr + 0.006 * seam - 0.003 * st
    a = dt.mix3(hexs('#7a5e42'), hexs('#a88d68'), np.clip(0.25 + 0.5 * pr + 0.35 * T.fbm(8, 8, 4), 0, 1))
    a = dt.mix3(a, hexs('#5a4330'), np.clip(dt.sstep(0.12, 0.0, e) * 0.45 + st * 0.4, 0, 1))
    stain = dt.sstep(0.2, 0.7, T.fbm(5, 5, 5) * 0.5 + 0.5)
    a = dt.mix3(a, hexs('#4e3d2e'), stain * 0.5)
    smoke = dt.sstep(1.2, 2.0, yb) * 0.35
    a = dt.mix3(a, hexs('#3a2f26'), smoke)
    a *= (0.92 + 0.12 * wr)[..., None]
    rough = 0.7 + 0.15 * wr
    return a, h, rough, stain * 0.3


# --------------------------------------------------------------------------- Detail bands

def stone(seed):
    """8 m canvas, 128 px/m. V = metres above ground inside the 4 m band (v 0..0.5)."""
    T = dt.Tex(N, seed)
    x, y = _metres(T, 8.0)
    yb = y % 4.0
    base = T.fbm(8, 8, 6)
    cr = T.ridged(6, 6, 5)
    crack = dt.sstep(0.84, 0.95, cr)
    grain = T.value(256, 256)
    h = 0.03 * base - 0.012 * crack + 0.002 * grain
    a = dt.mix3(hexs('#4f4c46'), hexs('#7a766c'), np.clip(base * 0.8 + 0.5, 0, 1))
    a = dt.mix3(a, hexs('#8c7e6a'), dt.sstep(0.0, 0.6, T.fbm(3, 3, 4)) * 0.4)
    f1, f2, ids, _, _ = T.worley(14, 14, jitter=1.0)
    lc = T.fbm(10, 10, 5)
    lich = dt.sstep(0.55, 0.2, f1 + 0.35 * T.fbm(60, 60, 3)) * dt.sstep(0.05, 0.35, lc)
    a = dt.mix3(a, hexs('#8e8f7a'), lich * 0.6)
    orange = dt.sstep(0.3, 0.12, f1 + 0.3 * T.fbm(80, 80, 2)) * (T.cell_rand(ids, 196) > 0.8)
    a = dt.mix3(a, hexs('#9a7a3c'), orange * 0.5)
    moss = np.clip(dt.sstep(0.55, 0.0, yb + 0.25 * T.fbm(10, 4, 4)) + dt.sstep(0.3, 0.6, lc) * 0.25, 0, 1)
    a = dt.mix3(a, hexs('#4d5233'), moss * 0.75)
    a = dt.mix3(a, hexs('#3c3833'), crack * 0.6)
    a = dt.mix3(a, hexs('#3f3528'), dt.sstep(0.15, 0.0, yb) * 0.6)
    rough = 0.82 + 0.1 * moss - 0.08 * lich
    return a, h, rough, moss


def leather_paint(seed):
    """4 m canvas: rows for v .5-.625 plain leather, .625-.75 painted hide (paint mask in G)."""
    T = dt.Tex(N, seed)
    x, y = _metres(T, 4.0)
    wr = T.ridged(12, 12, 4)
    h = 0.004 * wr + 0.002 * T.value(80, 80)
    # plain leather / fur
    fur = T.value(400, 60)
    la = dt.mix3(hexs('#3e2c1e'), hexs('#6b5038'), np.clip(0.5 + 0.4 * T.fbm(10, 10, 4) + 0.2 * fur, 0, 1))
    # painted hide: chevrons and dots in a horizontal strip
    yl = (y - 2.5) / 0.5                                  # 0..1 inside the painted band rows
    chev = np.abs(((x * 4.0) % 1.0) - 0.5) * 2.0          # triangle wave, 0.25 m period
    zig = np.abs(yl - 0.3 - 0.25 * chev)
    # a solid field of culture paint with unpainted chevron and dot lines (the colour must carry at distance)
    lines = dt.sstep(0.035, 0.02, zig)
    dots = dt.sstep(0.035, 0.025, np.hypot((((x * 4.0) % 1.0) - 0.5) * 0.25, (yl - 0.78) * 0.5))
    border = dt.sstep(0.05, 0.035, np.minimum(yl, 1.0 - yl))
    paint = np.clip(1.0 - lines - dots - border, 0, 1)
    paint *= dt.sstep(-0.45, -0.1, T.fbm(20, 20, 4))       # worn patches
    ha = dt.mix3(hexs('#9c805e'), hexs('#c0a782'), np.clip(0.5 + 0.4 * T.fbm(8, 8, 4), 0, 1))
    ha = dt.mix3(ha, hexs('#d8d2c6'), paint)                # paint is a light neutral: the culture tint multiplies it
    in_paint = (T.v >= 0.625).astype(np.float32)
    a = la * (1 - in_paint)[..., None] + ha * in_paint[..., None]
    a *= (0.9 + 0.15 * wr)[..., None]
    rough = 0.72 + 0.12 * wr - 0.1 * paint * in_paint
    return a, h, rough, paint * in_paint


def patches(seed):
    """Non-tiling patches in rows v .75-.875."""
    T = dt.Tex(N, seed)
    f1, f2, ids, _, _ = T.worley(160, 160, jitter=0.95)
    coal = dt.sstep(0.0, 0.25, f2 - f1)
    hot = dt.sstep(0.06, 0.0, f2 - f1) * dt.sstep(0.15, 0.45, T.fbm(8, 8, 4))
    ash = dt.sstep(0.1, 0.45, T.fbm(20, 20, 4))
    u = T.u
    emb = (u < 0.25).astype(np.float32)
    cold = ((u >= 0.25) & (u < 0.5)).astype(np.float32)
    inter = (u >= 0.5).astype(np.float32)
    a_emb = dt.mix3(hexs('#16110e'), hexs('#2b2420'), coal)
    a_emb = dt.mix3(a_emb, hexs('#c2541c'), hot)
    a_emb = dt.mix3(a_emb, hexs('#7b746c'), ash * 0.4)
    a_cold = dt.mix3(hexs('#141210'), hexs('#2e2a26'), coal)
    ash2 = dt.sstep(-0.3, 0.3, T.fbm(10, 10, 5))
    a_cold = dt.mix3(a_cold, hexs('#8a857e'), np.clip(ash2 * 0.85 + ash * 0.2, 0, 1))
    a_int = dt.mix3(hexs('#0c0a08'), hexs('#1c1610'), np.clip(T.fbm(6, 6, 4) * 0.5 + 0.5, 0, 1))
    a = a_emb * emb[..., None] + a_cold * cold[..., None] + a_int * inter[..., None]
    h = 0.01 * coal * (emb + cold)
    rough = 0.9 * (emb + cold) + 0.95 * inter
    night = inter
    fire = hot * emb
    return a, h, rough, night, fire


def rope(seed):
    T = dt.Tex(N, seed)
    x, y = _metres(T, 4.0)
    tw = ((x * 30.0 + y * 60.0) % 1.0)
    prof = np.sin(np.pi * tw)
    fib = T.value(600, 120)
    h = 0.003 * prof + 0.0005 * fib
    a = dt.mix3(hexs('#3d2f20'), hexs('#8a7253'), np.clip(prof * 0.8 + 0.2 * fib, 0, 1))
    rough = 0.85 + 0.0 * prof
    return a, h, rough


# --------------------------------------------------------------------------- assembly

def _finish(a, h, rough, m_span, ao_strength=60.0, ao_radius=0.01):
    n = dt.normal_from_height(h, m_span, N)
    ao = dt.cavity_ao(h, N, ao_radius, m_span, ao_strength)
    return a, n, ao, rough


def build_all(outdir, log=print):
    os.makedirs(outdir, exist_ok=True)
    zero = np.zeros((N, N), np.float32)

    # Structure ----------------------------------------------------------------
    D = np.zeros((N, N, 3), np.float32); NN = np.zeros((N, N, 3), np.float32)
    ORM = np.zeros((N, N, 3), np.float32); M = np.zeros((N, N, 3), np.float32); MA = np.zeros((N, N), np.float32)
    for fn, v0, v1, sd in ((bark, 0.0, 0.25, 11), (wood, 0.25, 0.5, 12), (wattle, 0.5, 0.75, 13), (earth, 0.75, 1.0, 14)):
        a, h, r, moss = fn(sd)
        a, n, ao, r = _finish(a, h, r, 4.0)
        _put(D, a, v0, v1); _put(NN, n, v0, v1)
        _put(ORM, np.stack([ao, r, zero], -1), v0, v1)
        _put(M, np.stack([zero, zero, moss], -1), v0, v1)
    _save('Structure', outdir, D, NN, ORM, M, MA, log)

    # Roof ---------------------------------------------------------------------
    D[:] = 0; NN[:] = 0; ORM[:] = 0; M[:] = 0
    for fn, v0, v1, sd in ((thatch, 0.0, 0.5, 21), (hide, 0.5, 1.0, 22)):
        a, h, r, moss = fn(sd)
        a, n, ao, r = _finish(a, h, r, 4.0, ao_strength=40.0, ao_radius=0.02)
        _put(D, a, v0, v1); _put(NN, n, v0, v1)
        _put(ORM, np.stack([ao, r, zero], -1), v0, v1)
        _put(M, np.stack([zero, zero, moss], -1), v0, v1)
    _save('Roof', outdir, D, NN, ORM, M, MA, log)

    # Detail -------------------------------------------------------------------
    D[:] = 0; NN[:] = 0; ORM[:] = 0; M[:] = 0
    a, h, r, moss = stone(31)
    a, n, ao, r = _finish(a, h, r, 8.0, ao_strength=25.0, ao_radius=0.04)
    _put(D, a, 0.0, 0.5); _put(NN, n, 0.0, 0.5); _put(ORM, np.stack([ao, r, zero], -1), 0.0, 0.5)
    _put(M, np.stack([zero, zero, moss], -1), 0.0, 0.5)
    a, h, r, paint = leather_paint(32)
    a, n, ao, r = _finish(a, h, r, 4.0)
    _put(D, a, 0.5, 0.75); _put(NN, n, 0.5, 0.75); _put(ORM, np.stack([ao, r, zero], -1), 0.5, 0.75)
    _put(M, np.stack([zero, paint, zero], -1), 0.5, 0.75)
    a, h, r, night, fire = patches(33)
    a, n, ao, r = _finish(a, h, r, 4.0)
    _put(D, a, 0.75, 0.875); _put(NN, n, 0.75, 0.875); _put(ORM, np.stack([ao, r, zero], -1), 0.75, 0.875)
    _put(M, np.stack([night, zero, zero], -1), 0.75, 0.875); _put(MA, fire, 0.75, 0.875)
    a, h, r = rope(34)
    a, n, ao, r = _finish(a, h, r, 4.0)
    _put(D, a, 0.875, 1.0); _put(NN, n, 0.875, 1.0); _put(ORM, np.stack([ao, r, zero], -1), 0.875, 1.0)
    _save('Detail', outdir, D, NN, ORM, M, MA, log)


def _save(name, outdir, D, NN, ORM, M, MA, log):
    p = lambda s: os.path.join(outdir, 'T_Age0_%s_%s.png' % (name, s))
    dt.save_png(p('D'), D, srgb=True)
    dt.save_png(p('N'), NN, srgb=False)
    dt.save_png(p('ORM'), ORM, srgb=False)
    dt.save_png(p('M'), M, alpha=MA, srgb=False)
    log(dt.stats('T_Age0_%s_D' % name, D))
