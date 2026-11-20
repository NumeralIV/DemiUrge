# DEMIURGE art - wonders-a: procedural PBR texture sets (numpy, runs inside Blender's Python).
# Uses art/tools/dmtex.py (periodic noise). Arrays are [v, u] with v up.
# Output per set: T_Wonder_<Set>_D.png (sRGB), _N.png (OpenGL +Y), _ORM.png (R AO, G roughness, B metallic),
# and for the detail atlas also _M.png (R night emissive mask, G culture-tint mask, B flame mask).
import math
import os

import numpy as np

import dmtex as dt
from dmtex import hex_lin, sstep, mix3, blur


def _write(folder, name, bc, height, rough, metal, metres, n, ao_mult=None, mask=None, ao_strength=6.0):
    nrm = dt.normal_from_height(height, metres, n)
    ao = dt.cavity_ao(height, n, 0.05 * metres / 8.0 + 0.02, metres, ao_strength)
    if ao_mult is not None:
        ao = ao * ao_mult
    orm = np.stack([ao, np.clip(rough, 0.03, 1), np.clip(metal, 0, 1)], axis=-1)
    # bake a little of the AO into the albedo too (EEVEE preview + distance read), as most of our kit does
    bc_out = bc * (0.55 + 0.45 * ao)[..., None]
    dt.save_png(os.path.join(folder, 'T_Wonder_%s_D.png' % name), bc_out, srgb=True)
    dt.save_png(os.path.join(folder, 'T_Wonder_%s_N.png' % name), nrm, srgb=False)
    dt.save_png(os.path.join(folder, 'T_Wonder_%s_ORM.png' % name), orm, srgb=False)
    if mask is not None:
        dt.save_png(os.path.join(folder, 'T_Wonder_%s_M.png' % name), mask, srgb=False)
    return dt.stats(name, bc_out)


def _tint(base_hex, var, amount):
    """base colour times a per-texel brightness/chroma wobble (var in -1..1)."""
    c = hex_lin(base_hex)
    return c[None, None, :] * (1.0 + amount * var)[..., None]


# ============================================================================ Hearth (wonder 0)

def hearth_bank(n, folder):
    """Bank trim: U tiles every 16 m around the ring, V spans the bank profile once (foot .. crest .. foot)."""
    T = dt.Tex(n, 1001)
    u, v = T.u, T.v
    wob = T.fbm(8, 4, 4)
    f1, f2, ids, _, _ = T.worley(56, 36, jitter=1.0)
    edge = f2 - f1
    srand = T.cell_rand(ids, 56 * 36)
    srand2 = T.cell_rand(ids, 56 * 36, 7)
    srand3 = T.cell_rand(ids, 56 * 36, 13)
    # verifier fix: the tight single-size Voronoi read as crazy paving. Now loose river stones of two sizes,
    # rounded and set in earth (about a third of the small cells empty), so the flank reads as a piled bank.
    present = sstep(0.32, 0.4, srand3)
    stone = sstep(0.12, 0.34, edge) * present
    g1, g2, gids, _, _ = T.worley(22, 14, jitter=1.0)
    gedge = g2 - g1
    gpresent = sstep(0.45, 0.55, T.cell_rand(gids, 22 * 14, 3))
    gstone = sstep(0.1, 0.3, gedge) * gpresent
    big = gstone > stone
    edge = np.where(big, gedge * 1.2, edge)
    srand = np.where(big, T.cell_rand(gids, 22 * 14, 5), srand)
    srand2 = np.where(big, T.cell_rand(gids, 22 * 14, 9), srand2)
    stone = np.maximum(stone, gstone)
    present = np.maximum(present, gpresent * (gstone > 0.01))
    # bands across the profile: river-stone revetment on both flanks, grass shoulders, scorched crest
    vv = v + 0.06 * wob
    stonez = (1 - sstep(0.22, 0.36, vv)) + sstep(0.64, 0.78, vv)
    stonez = np.clip(stonez, 0, 1) * (0.7 + 0.3 * sstep(-0.3, 0.3, T.fbm(16, 10, 3)))
    crest = np.exp(-((vv - 0.5) / 0.06) ** 2)
    grassz = np.clip(1 - stonez * 0.85 - crest, 0, 1)
    earth = _tint('#5a4630', T.fbm(32, 20, 4), 0.3)
    earth = mix3(earth, hex_lin('#4a3a2a'), sstep(0.0, 0.5, T.fbm(64, 40, 3)) * 0.5)
    tuft = T.fbm(160, 100, 3)
    grass = mix3(hex_lin('#5f7a3a'), hex_lin('#7d7a45'), sstep(-0.1, 0.4, T.fbm(12, 8, 3)))
    grass = mix3(grass, hex_lin('#46602c'), sstep(0.1, 0.5, tuft) * 0.6)
    gmask = grassz * sstep(-0.25, 0.15, T.fbm(24, 16, 4) + 0.1 + 0.25 * tuft)
    stone_col = mix3(hex_lin('#8a8070'), hex_lin('#a29a8a'), srand)
    stone_col = mix3(stone_col, hex_lin('#675f55'), sstep(0.55, 1.0, srand2))
    stone_col = stone_col * (1 + 0.14 * T.fbm(160, 100, 3))[..., None]
    shade = np.sqrt(np.clip(edge * 3.0, 0, 1))
    stone_col = stone_col * (0.7 + 0.3 * shade)[..., None]
    ash = mix3(hex_lin('#3a2e24'), hex_lin('#7d7872'), sstep(0.1, 0.6, T.fbm(48, 30, 4)))
    bc = mix3(earth, grass, gmask)
    st = np.clip(stonez * stone + grassz * stone * 0.25 * present * sstep(0.6, 0.9, srand2), 0, 1)
    bc = mix3(bc, stone_col, st)
    bc = mix3(bc, ash, np.clip(crest * 1.1, 0, 1) * 0.85)
    h = 0.025 * T.fbm(32, 20, 5) + 0.008 * tuft * gmask
    h += st * 0.12 * shade
    rough = 0.92 - 0.18 * st + 0.03 * gmask
    metal = np.zeros_like(rough)
    return _write(folder, 'HearthBank', bc, h, rough, metal, 16.0, n)


def hearth_cone(n, folder):
    """Roof of the Hearth: u 0..0.5 = bundled timber poles (grain along V, tiles in V every 4 m),
    u 0.5..1 = hide panels (tiles in V)."""
    T = dt.Tex(n, 1002)
    u, v = T.u, T.v
    left = u < 0.5
    # ---- timber: 4 poles across the zone
    up = (u * 2.0) % 1.0 * 4.0
    pole = up % 1.0
    pid = np.floor(up)
    shade = np.sin(pole * math.pi) ** 0.6
    grain = T.value(256, 6, u=u, v=v * 1.0) * 0.5 + T.value(512, 12) * 0.3
    bark = sstep(0.55, 0.8, T.fbm(64, 24, 3) * 0.5 + 0.5)
    wood = mix3(hex_lin('#4a3624'), hex_lin('#6b5236'), np.clip(0.5 + grain, 0, 1))
    wood = mix3(wood, hex_lin('#2e2219'), bark * 0.6)
    wood = wood * (0.45 + 0.55 * shade)[..., None] * (0.9 + 0.1 * np.sin(pid * 3.1))[..., None]
    hw = 0.03 * shade + 0.004 * grain - 0.006 * bark
    # lashing bands every 1 m in V
    lash = sstep(0.93, 0.97, np.abs(np.sin(v * math.pi * 4.0 + 0.3 * T.value(8, 2))))
    wood = mix3(wood, hex_lin('#7a6248'), lash * 0.0)
    # ---- hides: panels with stitched seams, mottled
    mott = T.fbm(24, 24, 5)
    hide = _tint('#9a7550', mott, 0.3)
    seam_u = np.abs(((u - 0.5) * 2.0 * 3.0) % 1.0 - 0.5)
    seam_v = np.abs((v * 5.0 + 0.2 * np.floor((u - 0.5) * 6.0)) % 1.0 - 0.5)
    seam = np.clip(sstep(0.47, 0.5, seam_u) + sstep(0.47, 0.5, seam_v), 0, 1)
    hide = mix3(hide, hex_lin('#4e3a28'), seam * 0.7)
    hide = mix3(hide, hex_lin('#6b4d33'), sstep(0.2, 0.6, T.fbm(48, 48, 3)) * 0.4)
    hh = 0.01 * mott - 0.006 * seam
    bc = np.where(left[..., None], wood, hide)
    h = np.where(left, hw, hh)
    rough = np.where(left, 0.82 + 0.1 * bark, 0.7 + 0.1 * mott)
    return _write(folder, 'HearthCone', bc, h, rough, np.zeros_like(rough), 4.0, n)


# ============================================================================ Great Hive (wonder 1)

def hive_plaster(n, folder):
    """Lime plaster over mud brick: faint brick coursing reads through, rare flaked patches, rain streaks."""
    T = dt.Tex(n, 1101)
    u, v = T.u, T.v
    M = 8.0
    big = T.fbm(4, 4, 4)
    plaster = _tint('#e8e0cf', big, 0.07)
    trowel = T.fbm(48, 48, 4)
    plaster = plaster * (1 + 0.035 * trowel)[..., None]
    # mud-brick coursing under the plaster (0.4 x 0.125 m bricks)
    rows = 64
    rv = v * rows
    ri = np.floor(rv)
    off = (ri % 2) * 0.5
    bu = u * 20 + off
    bf = bu % 1.0
    rf = rv % 1.0
    mortar = np.clip(sstep(0.0, 0.1, rf) * sstep(1.0, 0.9, rf) * sstep(0.0, 0.05, bf) * sstep(1.0, 0.95, bf), 0, 1)
    plaster = plaster * (1 - 0.06 * (1 - mortar))[..., None]
    bid = (np.floor(bu) + ri * 37) % 97
    brick = mix3(hex_lin('#b8956a'), hex_lin('#9e7a52'), (np.sin(bid * 12.9898) * 0.5 + 0.5))
    brick = brick * (1 + 0.15 * T.fbm(80, 80, 3))[..., None]
    brick = mix3(hex_lin('#8a7258'), brick, mortar)
    # vertical weathering streaks, stronger in bands (under each ledge)
    streak = T.value(60, 2) * 0.6 + T.value(120, 4) * 0.4
    streak = sstep(0.2, 0.9, streak) * sstep(-0.1, 0.6, T.fbm(8, 3, 3))
    plaster = mix3(plaster, hex_lin('#b3a68e'), streak * 0.5)
    # verifier fix: the flaked patches covered ~8 % of every 8 m tile and repeated round the body as a
    # camouflage pattern. Now rare small flakes, plus plaster lifts (~0.9 m bands of slightly different lime
    # wash) and broad grime so the wall is not a clean white decal.
    flake = T.fbm(6, 6, 5) + 0.3 * T.fbm(40, 40, 3)
    fm = sstep(0.62, 0.65, flake)
    rim = np.clip(sstep(0.58, 0.62, flake) - fm, 0, 1)
    lift_v = v * M / 0.9 + 0.08 * T.fbm(6, 2, 3)
    lift_i = np.floor(lift_v)
    lift_t = np.sin(lift_i * 12.9898 + np.floor(u * 3) * 78.233) * 0.5 + 0.5
    plaster = plaster * (0.96 + 0.06 * lift_t)[..., None]
    lift_line = sstep(0.06, 0.0, np.abs(lift_v - np.round(lift_v))) * sstep(-0.2, 0.3, T.fbm(12, 4, 3))
    plaster = mix3(plaster, hex_lin('#bdb099'), lift_line * 0.5)
    grime = sstep(0.0, 0.7, T.fbm(3, 5, 4))
    plaster = mix3(plaster, hex_lin('#c8b89a'), grime * 0.35)
    # irregular mottling, grime and pitting so the plaster is not a clean decal
    mot = T.fbm(16, 16, 5)
    plaster = plaster * (1 + 0.06 * mot)[..., None]
    plaster = mix3(plaster, hex_lin('#cfc3aa'), sstep(0.05, 0.45, T.fbm(10, 10, 4)) * 0.35)
    pits = sstep(0.82, 0.95, T.value(400, 400, smooth=False)) * sstep(0.0, 0.4, T.fbm(20, 20, 3))
    plaster = mix3(plaster, hex_lin('#a89c84'), pits * 0.4)
    bc = mix3(plaster, brick, fm * 0.85)
    bc = mix3(bc, hex_lin('#c9bba0'), rim * 0.5)
    c1, c2, _, _, _ = T.worley(7, 7)
    crack = sstep(0.025, 0.0, c2 - c1) * sstep(0.3, 0.55, T.fbm(16, 16, 3))
    bc = mix3(bc, hex_lin('#9a8d78'), crack * 0.5)
    h = 0.004 * trowel + 0.008 * big + 0.003 * mot - pits * 0.003 - fm * 0.012 - (1 - mortar) * (0.0015 + fm * 0.006) - crack * 0.004
    rough = 0.86 + 0.06 * fm
    return _write(folder, 'HivePlaster', bc, h, rough, np.zeros_like(rough), M, n)


def hive_bronze(n, folder):
    T = dt.Tex(n, 1102)
    u, v = T.u, T.v
    M = 4.0
    rows = 8
    rv = v * rows
    ri = np.floor(rv)
    bu = u * 4 + (ri % 2) * 0.5
    seam = np.clip(sstep(0.03, 0.0, rv % 1.0) + sstep(0.97, 1.0, rv % 1.0) + sstep(0.012, 0.0, bu % 1.0)
                   + sstep(0.988, 1.0, bu % 1.0), 0, 1)
    f1, f2, _, _, _ = T.worley(40, 40)
    dimple = sstep(0.0, 0.6, f1)
    pid = (np.floor(bu) * 13 + ri * 7) % 23
    plate = 1.0 + 0.06 * np.sin(pid * 4.37)
    tarn = sstep(0.1, 0.7, T.fbm(8, 8, 5))
    bc = _tint('#b08440', T.fbm(32, 32, 3), 0.05) * plate[..., None]
    bc = mix3(bc, hex_lin('#7a5a2c'), tarn * 0.45)
    bc = mix3(bc, hex_lin('#5f5030'), seam * 0.5)
    h = 0.002 * dimple - 0.003 * seam
    # rivets along horizontal seams
    rvt = (np.abs((rv + 0.5) % 1.0 - 0.5) < 0.06) & (np.abs((u * 32) % 1.0 - 0.5) < 0.18)
    h = h + rvt * 0.004
    rough = 0.24 + 0.2 * tarn + 0.1 * seam
    metal = np.ones_like(rough) * 0.98
    return _write(folder, 'HiveBronze', bc, h, rough, metal, M, n, ao_strength=3.0)


# ============================================================================ Seven Who Keep Watch (wonder 2)

def seven_paving(n, folder):
    T = dt.Tex(n, 1201)
    M = 8.0
    f1, f2, ids, _, _ = T.worley(7, 7, jitter=0.8)
    edge = f2 - f1
    flag = sstep(0.02, 0.06, edge)
    r1 = T.cell_rand(ids, 49)
    r2 = T.cell_rand(ids, 49, 3)
    stone = mix3(hex_lin('#9a8d78'), hex_lin('#8a8478'), r1)
    stone = mix3(stone, hex_lin('#a89a80'), sstep(0.7, 1.0, r2))
    stone = stone * (1 + 0.1 * T.fbm(96, 96, 4))[..., None]
    lich = sstep(0.35, 0.55, T.fbm(40, 40, 4)) * sstep(0.2, 0.5, T.fbm(10, 10, 3))
    stone = mix3(stone, hex_lin('#8f8c5a'), lich * 0.6)
    joint = mix3(hex_lin('#5a5040'), hex_lin('#5f7a3a'), sstep(0.0, 0.5, T.fbm(64, 64, 3)))
    stone = mix3(stone, hex_lin('#7a7060'), sstep(0.0, 0.6, T.fbm(5, 5, 4)) * 0.5)
    bc = mix3(joint, stone, flag)
    h = flag * (0.03 + 0.02 * np.sqrt(np.clip(edge, 0, 1))) + 0.004 * T.fbm(128, 128, 3)
    rough = 0.88 - 0.05 * flag
    return _write(folder, 'SevenPaving', bc, h, rough, np.zeros_like(rough), M, n)


def seven_granite(n, folder, fold_fn):
    """Unique cylindrical statue map: U = angle / 2pi (front of the figure at u = 0.75), V = height / H."""
    T = dt.Tex(n, 1202)
    u, v = T.u, T.v
    th = u * 2 * math.pi
    fold = fold_fn(th, v)                # metres of radial offset at this angle/height, negative = cavity
    cav = sstep(0.0, -0.9, fold)
    speck = T.value(700, 700, smooth=False)
    big = T.fbm(6, 6, 4)
    gran = _tint('#7d7a72', big, 0.08)
    gran = mix3(gran, hex_lin('#3c3a36'), sstep(0.75, 0.95, speck) * 0.6)
    gran = mix3(gran, hex_lin('#b5b0a6'), sstep(-0.75, -0.95, speck) * 0.5)
    # lichen on the shoulders and hood crown, blotches elsewhere
    lz = np.exp(-((v - 0.765) / 0.035) ** 2) + sstep(0.93, 0.98, v) * 0.8
    lich = np.clip(lz * sstep(-0.3, 0.3, T.fbm(20, 20, 4) + 0.15) + sstep(0.55, 0.7, T.fbm(30, 30, 4)) * 0.5, 0, 1)
    gran = mix3(gran, hex_lin('#8f8c5a'), lich * 0.75)
    # rain streaks below the shoulders
    st = sstep(0.2, 0.9, T.value(90, 3) * 0.5 + 0.5) * sstep(0.78, 0.45, v)
    gran = mix3(gran, hex_lin('#5f5c55'), st * 0.3)
    # base grime
    gran = mix3(gran, hex_lin('#4f4d40'), sstep(0.07, 0.0, v) * 0.6)
    # deep hood interior (front, head height): painted shadow so no face can ever read
    du = np.abs(((u - 0.75 + 0.5) % 1.0) - 0.5)
    hood = sstep(0.11, 0.06, du) * sstep(0.825, 0.86, v) * sstep(0.965, 0.935, v)
    gran = mix3(gran, hex_lin('#141311'), hood * 0.92)
    # fold cavities
    gran = gran * (1 - 0.35 * cav)[..., None]
    chisel = T.fbm(64, 160, 3)
    h = 0.006 * chisel + 0.004 * big + 0.002 * (speck > 0.8)
    rough = 0.82 + 0.08 * lich
    ao = 1 - 0.45 * cav - 0.5 * hood
    return _write(folder, 'SevenGranite', gran, h, rough, np.zeros_like(rough), 40.0, n, ao_mult=ao)


# ============================================================================ Sea Gate (wonder 3)

def gate_limestone(n, folder):
    T = dt.Tex(n, 1301)
    u, v = T.u, T.v
    M = 8.0
    rng = np.random.default_rng(77)
    nc = 11
    hts = rng.uniform(0.6, 0.9, nc)
    hts = hts / hts.sum()
    edges = np.concatenate([[0], np.cumsum(hts)])
    ci = np.clip(np.searchsorted(edges, v, side='right') - 1, 0, nc - 1)
    c_lo = edges[ci]
    c_hi = edges[ci + 1]
    vf = (v - c_lo) / (c_hi - c_lo)
    bu = np.zeros_like(u)
    bid = np.zeros_like(u)
    bl = np.zeros_like(u)
    for c in range(nc):
        lens = rng.uniform(1.0, 2.3, 12)
        k = int(np.searchsorted(np.cumsum(lens), 8.0)) + 1
        lens = lens[:k]
        lens = lens / lens.sum()
        e = np.concatenate([[0], np.cumsum(lens)])
        shift = rng.random()
        m = ci == c
        uu = (u[m] + shift) % 1.0
        bi = np.clip(np.searchsorted(e, uu, side='right') - 1, 0, k - 1)
        bu[m] = (uu - e[bi]) / (e[bi + 1] - e[bi])
        bl[m] = (e[bi + 1] - e[bi]) * M
        bid[m] = bi + c * 31
    # distance to joint in metres
    dh = np.minimum(bu, 1 - bu) * bl
    dv = np.minimum(vf, 1 - vf) * (c_hi - c_lo)[...] * M
    dj = np.minimum(dh, dv)
    joint = sstep(0.018, 0.0, dj)
    chip = sstep(0.07, 0.0, dj) * sstep(0.1, 0.5, T.fbm(64, 64, 3))
    br = np.sin(bid * 12.9898 + 4.1) * 0.5 + 0.5
    br2 = np.sin(bid * 7.233 + 1.3) * 0.5 + 0.5
    stone = _tint('#d9cdb4', (br - 0.5) * 2, 0.06)
    stone = mix3(stone, hex_lin('#cfc2a4'), sstep(0.6, 1.0, br2) * 0.7)
    stone = stone * (1 + 0.05 * T.fbm(64, 64, 4) + 0.03 * T.fbm(256, 256, 2))[..., None]
    streak = sstep(0.2, 0.9, T.value(70, 3) * 0.5 + 0.5) * sstep(-0.2, 0.5, T.fbm(6, 3, 3))
    stone = mix3(stone, hex_lin('#a39a88'), streak * 0.3)
    bc = mix3(stone, hex_lin('#a8997e'), np.clip(joint + chip * 0.5, 0, 1))
    pillow = np.clip(dj / 0.12, 0, 1)
    h = 0.012 * np.sqrt(pillow) - 0.015 * joint + 0.003 * T.fbm(128, 128, 3) - 0.006 * chip
    rough = 0.8 + 0.08 * joint
    return _write(folder, 'GateLimestone', bc, h, rough, np.zeros_like(rough), M, n)


def gate_bronze(n, folder):
    T = dt.Tex(n, 1302)
    u, v = T.u, T.v
    M = 4.0
    st = T.value(40, 2) * 0.5 + T.value(80, 4) * 0.3 + T.fbm(16, 16, 3) * 0.4
    verd = sstep(0.0, 0.5, st)
    acc = sstep(0.2, 0.6, T.fbm(8, 8, 4))
    vd = np.clip(verd * 0.7 + acc * 0.5, 0, 1)
    bc = _tint('#8a6a3a', T.fbm(32, 32, 3), 0.08)
    vcol = mix3(hex_lin('#5f8f80'), hex_lin('#7aa898'), sstep(0, 1, T.fbm(64, 64, 3) + 0.5))
    bc = mix3(bc, vcol, vd)
    h = 0.003 * T.fbm(64, 64, 4) + 0.004 * vd
    rough = 0.35 + 0.45 * vd
    metal = 1.0 - 0.9 * vd
    return _write(folder, 'GateBronze', bc, h, rough, metal, M, n, ao_strength=3.0)


# ============================================================================ shared detail atlas

def detail_atlas(n, folder):
    """4 x 4 cells (see wa_lib.CELLS). Returns stats."""
    c = n // 4
    bc = np.zeros((n, n, 3), np.float32)
    h = np.zeros((n, n), np.float32)
    rough = np.ones((n, n), np.float32) * 0.8
    metal = np.zeros((n, n), np.float32)
    mask = np.zeros((n, n, 3), np.float32)
    T = dt.Tex(c, 1401)
    u, v = T.u, T.v

    def put(idx, col, hh, rr, mm, em=None, tint=None, fl=None):
        cx, cy = idx % 4, idx // 4
        sl = (slice(cy * c, (cy + 1) * c), slice(cx * c, (cx + 1) * c))
        bc[sl] = col
        h[sl] = hh
        rough[sl] = rr
        metal[sl] = mm
        if em is not None:
            mask[sl + (0,)] = em
        if tint is not None:
            mask[sl + (1,)] = tint
        if fl is not None:
            mask[sl + (2,)] = fl
    z = np.zeros((c, c), np.float32)
    nz = T.fbm(8, 8, 5)
    # 0 fire: v = height of the flame
    fl = T.fbm(6, 3, 4, v=v * 0.7)
    t = np.clip(v + 0.12 * fl, 0, 1)
    # verifier fix: a white-hot base made the flame meshes read as white candles; now a yellow core going
    # to the concept's orange (#ff8c38) and a deep orange tip
    fire = mix3(hex_lin('#ffe6a8'), hex_lin('#ffc35e'), sstep(0.0, 0.25, t))
    fire = mix3(fire, hex_lin('#ff9a3c'), sstep(0.2, 0.6, t))
    fire = mix3(fire, hex_lin('#e8601e'), sstep(0.6, 1.0, t))
    put(0, fire, z, np.ones_like(z), z, em=np.ones_like(z), fl=np.ones_like(z))
    # 1 lamp-lit doorway interior: lamp low in the middle
    d = np.sqrt(((u - 0.5) / 0.6) ** 2 + ((v - 0.25) / 0.7) ** 2)
    glow = np.clip(1.0 - d, 0, 1) ** 1.5
    door = mix3(hex_lin('#1c140e'), hex_lin('#ffad52'), glow)
    put(1, door, z, np.ones_like(z) * 0.9, z, em=np.clip(glow * 1.3, 0, 1))
    # 2 lantern glass: panes behind bronze mullions
    mu = np.abs(((u * 4) % 1.0) - 0.5) > 0.42
    mv = (np.abs(v - 0.5) < 0.04) | (v < 0.06) | (v > 0.94)
    mull = (mu | mv).astype(np.float32)
    flame = np.clip(1.0 - np.sqrt(((u % 0.25 - 0.125) / 0.2) ** 2 + ((v - 0.35) / 0.8) ** 2), 0, 1)
    pane = mix3(hex_lin('#ffb050'), hex_lin('#fff0c0'), flame)
    lan = mix3(pane, hex_lin('#6a4e28'), mull)
    put(2, lan, mull * 0.01, 0.3 + 0.3 * mull, mull * 0.9, em=(1 - mull) * (0.75 + 0.25 * flame))
    # 3 iron with rust
    rust = sstep(0.0, 0.5, T.fbm(10, 10, 5))
    iron = mix3(hex_lin('#3a3530'), hex_lin('#7a4a2a'), rust)
    put(3, iron * (1 + 0.15 * nz)[..., None], 0.004 * T.fbm(32, 32, 4), 0.55 + 0.35 * rust, 0.75 * (1 - rust))
    # 4 polished bronze
    put(4, _tint('#b08440', nz, 0.06), 0.001 * nz, 0.25 + 0.1 * sstep(0, 1, nz), np.ones_like(z))
    # 5 bronze with verdigris
    vd = sstep(-0.1, 0.4, T.value(20, 3) * 0.6 + T.fbm(8, 8, 3) * 0.5)
    put(5, mix3(hex_lin('#8a6a3a'), hex_lin('#5f8f80'), vd), 0.003 * vd, 0.35 + 0.45 * vd, 1 - 0.9 * vd)
    # 6 bone / antler: grooved along v, darker tips
    gr = T.value(60, 4) * 0.5 + 0.5
    bone = mix3(hex_lin('#d8cbb0'), hex_lin('#8f8068'), sstep(0.7, 1.0, v) * 0.6 + gr * 0.2)
    put(6, bone, 0.003 * gr, 0.6 + 0.1 * gr, z)
    # 7 hide (culture tint mask)
    mott = T.fbm(12, 12, 5)
    put(7, _tint('#9a7550', mott, 0.3), 0.004 * mott, 0.75 + 0.05 * mott, z, tint=np.ones_like(z))
    # 8 timber: grain along v
    g = T.value(48, 3) * 0.6 + T.value(160, 8) * 0.3
    wood = mix3(hex_lin('#6b5236'), hex_lin('#9a7a55'), np.clip(0.5 + g, 0, 1))
    put(8, wood, 0.004 * g, 0.8 + 0.05 * g, z)
    # 9 river stone / rubble
    st = T.fbm(16, 16, 6)
    put(9, _tint('#8a8070', st, 0.2), 0.01 * st, 0.78 + 0.08 * st, z)
    # 10 terracotta with bands
    bands = sstep(0.9, 1.0, np.abs(np.sin(v * math.pi * 7)))
    tc = mix3(_tint('#b5683a', nz, 0.12), hex_lin('#7a4022'), bands * 0.6)
    tc = mix3(tc, hex_lin('#8a6a50'), sstep(0.0, 0.6, T.fbm(6, 6, 4)) * 0.35)
    put(10, tc, -0.002 * bands, 0.72 + 0.08 * bands, z)
    # 11 dark recess
    put(11, _tint('#15120f', nz, 0.2), z, np.ones_like(z), z)
    # 12 charcoal and ash
    ash = sstep(0.0, 0.5, T.fbm(20, 20, 5))
    put(12, mix3(hex_lin('#221d1a'), hex_lin('#7a7570'), ash), 0.004 * ash, 0.95 + 0 * ash, z)
    # 13 granite
    sp = T.value(220, 220, smooth=False)
    gr13 = mix3(_tint('#7d7a72', nz, 0.08), hex_lin('#3c3a36'), sstep(0.75, 0.95, sp) * 0.6)
    put(13, gr13, 0.005 * st, 0.82 + 0 * st, z)
    # 14 limestone
    put(14, _tint('#d9cdb4', st, 0.08), 0.006 * st, 0.8 + 0 * st, z)
    # 15 plaster
    # (verifier fix: fewer, softer mud-brick patches; the old ones read as camouflage blotches on rubble)
    put(15, mix3(_tint('#e8e0cf', nz, 0.06), hex_lin('#b8956a'), sstep(0.5, 0.62, st) * 0.7), 0.004 * st, 0.88 + 0 * st, z)
    # cells are not periodic: write directly (normal from height per whole atlas is fine; cell borders sit in margins)
    return _write(folder, 'DetailA', bc, h, rough, metal, 4.0, n, mask=mask, ao_strength=3.0)


def build_all(folder, n, which=None, fold_fn=None):
    os.makedirs(folder, exist_ok=True)
    out = []
    jobs = [('HearthBank', lambda: hearth_bank(n, folder)), ('HearthCone', lambda: hearth_cone(n, folder)),
            ('HivePlaster', lambda: hive_plaster(n, folder)), ('HiveBronze', lambda: hive_bronze(n, folder)),
            ('SevenPaving', lambda: seven_paving(n, folder)),
            ('SevenGranite', lambda: seven_granite(n, folder, fold_fn)),
            ('GateLimestone', lambda: gate_limestone(n, folder)), ('GateBronze', lambda: gate_bronze(n, folder)),
            ('DetailA', lambda: detail_atlas(n, folder))]
    for name, fn in jobs:
        if which and name not in which:
            continue
        out.append(fn())
        print('[tex]', out[-1])
    return out
