# DEMIURGE art pipeline - generates every tileable material texture set used by the asset kits.
#   blender.exe -b --factory-startup --python art/tools/make_textures.py -- [names...] [--size=N]
# Output: art/textures/<name>/T_<Name>_BC.png, _N.png, _ORM.png and art/textures/manifest.json (tile size in
# metres per set, albedo statistics). Deterministic: every set has a fixed seed.
#
# Albedo targets are the de-lit hex values of docs/concept/assets/shrine/index.md ("Materials" table).
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dmtex as T  # noqa: E402
from dmtex import hex_lin as H, sstep, mix3, blur  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT = os.path.join(ROOT, 'textures')
LUMA = np.array([0.2126, 0.7152, 0.0722], np.float32)


def ramp(t, cols):
    """t (...) in 0..1 across a list of hex colours (linear interpolation in linear space)."""
    cs = np.stack([H(c) for c in cols])
    x = np.clip(t, 0, 1) * (len(cols) - 1)
    i = np.clip(np.floor(x).astype(np.int32), 0, len(cols) - 2)
    f = (x - i)[..., None]
    return cs[i] * (1 - f) + cs[i + 1] * f


def rows_partition(rng, total, lo, hi):
    """Random lengths in [lo, hi] that sum exactly to total (periodic courses)."""
    out = []
    s = 0.0
    while total - s > hi:
        L = rng.uniform(lo, hi)
        if total - s - L < lo:
            L = (total - s) / 2.0
        out.append(L)
        s += L
    out.append(total - s)
    return np.array(out, np.float32)


def coursed_blocks(t, tile, course_h, lo, hi, joint, seed_shift=0.0):
    """Ashlar courses: returns dict with block id, distance to joints (m), local coords. Periodic."""
    n = t.n
    nrows = int(round(tile / course_h))
    vm = t.v * tile
    um = t.u * tile
    row = np.clip(np.floor(vm / course_h).astype(np.int32), 0, nrows - 1)
    dy = np.minimum(vm - row * course_h, (row + 1) * course_h - vm)
    bid = np.zeros((n, n), np.int32)
    dx = np.zeros((n, n), np.float32)
    lx = np.zeros((n, n), np.float32)
    blen = np.zeros((n, n), np.float32)
    base = 0
    for r in range(nrows):
        parts = rows_partition(t.rng, tile, lo, hi)
        edges = np.concatenate([[0.0], np.cumsum(parts)])
        off = t.rng.uniform(0, tile)
        m = row == r
        uu = (um[m] + off) % tile
        k = np.clip(np.searchsorted(edges, uu, side='right') - 1, 0, len(parts) - 1)
        a = edges[k]
        b = edges[k + 1]
        dx[m] = np.minimum(uu - a, b - uu)
        lx[m] = uu - a
        blen[m] = b - a
        bid[m] = base + k
        base += len(parts)
    d = np.minimum(dx, dy)
    return dict(bid=bid, count=base, dx=dx, dy=dy, d=d, row=row, lx=lx, blen=blen, vm=vm, um=um)


# =========================================================================== material generators
# each returns dict(bc=lin rgb, h=height m, rough=, tile=m, alpha=optional, metal=optional, ao_k=strength)


def limestone(n, pale=False):
    """Fitted grey-ochre limestone: courses 0.2 m, blocks 0.45-0.85 m, thin recessed joints, worn arrises."""
    t = T.Tex(n, 101)
    tile = 2.0
    B = coursed_blocks(t, tile, 0.2, 0.45, 0.85, 0.005)
    warp = t.fbm(16, 16, 4) * 0.003
    jw = 0.0035 + 0.003 * (t.fbm(40, 40, 3) * 0.5 + 0.5)
    d = B['d'] + warp
    joint = 1.0 - sstep(jw * 0.5, jw * 1.5, d)
    rb = t.cell_rand(B['bid'], B['count'])
    rb2 = t.cell_rand(B['bid'], B['count'], 7)
    pillow = sstep(0.0, 0.03, d) ** 0.6 * 0.006
    tilt = (rb - 0.5) * 0.003 * (B['lx'] / np.maximum(B['blen'], 0.1) - 0.5)
    chisel = np.sin((t.u + t.v * 0.6) * tile * 2 * np.pi * 70 + t.fbm(30, 30, 3) * 6) * 0.00012 * (rb2 > 0.4)
    pits = np.clip(-t.fbm(120, 120, 3) - 0.35, 0, 1) * -0.003
    broad = t.fbm(6, 6, 5) * 0.002
    chips = np.clip(t.ridged(30, 30, 3) - 0.84, 0, 1) * -0.02 * sstep(0.04, 0.0, d)
    h = pillow + tilt + chisel + pits + broad + chips - joint * 0.006
    lo, mid, hi = ('#857a66', '#a39276', '#b2a68e') if not pale else ('#a29680', '#b4a68a', '#c5b99f')
    tone = np.clip(rb * 0.55 + t.fbm(10, 10, 4) * 0.3 + 0.22, 0, 1)
    bc = ramp(tone, [lo, mid, hi])
    bc *= (1.0 + t.fbm(60, 60, 5)[..., None] * 0.08)
    bc *= (1.0 + (chisel / 0.00012)[..., None] * 0.012)
    streak = np.clip(t.fbm(48, 3, 3) * 0.5 + 0.5, 0, 1) ** 3
    bc *= (1.0 - streak[..., None] * 0.12)
    bc = mix3(bc, H('#6e6250'), joint * 0.75)
    bc *= (1.0 + pits[..., None] * 25)
    rough = 0.85 + t.fbm(20, 20, 3) * 0.05 + joint * 0.06
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=40.0, target='#a39276' if not pale else '#b2a284')


def rubble(n):
    """Rubble stone in rough courses: rounded, irregular stones 0.15-0.35 m, bedded in clay."""
    t = T.Tex(n, 202)
    tile = 2.0
    rng = t.rng
    heights = rows_partition(rng, tile, 0.11, 0.19)
    vedges = np.concatenate([[0.0], np.cumsum(heights)])
    # warp the lookup so courses wander and stone outlines are not straight
    wu = t.u * tile + t.fbm(6, 6, 4) * 0.05 + t.fbm(30, 30, 3) * 0.012
    wv = t.v * tile + t.fbm(6, 6, 4, u=t.u + 0.41) * 0.035 + t.fbm(30, 30, 3, u=t.u + 0.7) * 0.01
    wu %= tile
    wv %= tile
    row = np.clip(np.searchsorted(vedges, wv, side='right') - 1, 0, len(heights) - 1)
    ly = wv - vedges[row]
    bh = heights[row]
    lx = np.zeros_like(wu)
    bw = np.zeros_like(wu)
    bid = np.zeros(wu.shape, np.int32)
    base = 0
    for r in range(len(heights)):
        parts = rows_partition(rng, tile, 0.15, 0.36)
        e = np.concatenate([[0.0], np.cumsum(parts)])
        off = rng.uniform(0, tile)
        m = row == r
        uu = (wu[m] + off) % tile
        k = np.clip(np.searchsorted(e, uu, side='right') - 1, 0, len(parts) - 1)
        lx[m] = uu - e[k]
        bw[m] = parts[k]
        bid[m] = base + k
        base += len(parts)
    rid = t.cell_rand(bid, base)
    rid2 = t.cell_rand(bid, base, 3)
    rid3 = t.cell_rand(bid, base, 5)
    # stone = rounded box inside the block with a random inset (clay gap) and corner radius
    inset = 0.008 + rid3 * 0.014
    hx = bw * 0.5 - inset
    hy = bh * 0.5 - inset * 0.8
    px = np.abs(lx - bw * 0.5)
    py = np.abs(ly - bh * 0.5)
    rad = np.minimum(hx, hy) * (0.35 + rid * 0.5)
    qx = np.maximum(px - (hx - rad), 0)
    qy = np.maximum(py - (hy - rad), 0)
    sd = np.sqrt(qx * qx + qy * qy) - rad + np.minimum(np.maximum(px - hx, py - hy), 0)   # signed distance, <0 inside
    sd += t.fbm(40, 40, 3) * 0.006
    inside = -sd
    stone = sstep(-0.002, 0.004, inside)
    dome = np.sqrt(np.clip(inside / np.maximum(np.minimum(hx, hy), 0.02), 0, 1)) * (0.016 + rid2 * 0.026)
    facets = t.ridged(22, 22, 4) * 0.008 + t.fbm(110, 110, 3) * 0.0016
    clay = t.fbm(70, 70, 4) * 0.0025 - 0.012
    h = stone * (dome + facets) + (1 - stone) * clay
    tone = np.clip(rid * 0.8 + t.fbm(14, 14, 3) * 0.2 + 0.08, 0, 1)
    sc = ramp(tone, ['#5e4630', '#735a40', '#85694c', '#917655', '#9c8264'])
    sc *= (1.0 + t.fbm(80, 80, 4)[..., None] * 0.12 + (facets / 0.008 - 0.5)[..., None] * 0.08)
    grey = (rid2 > 0.9).astype(np.float32)
    sc = mix3(sc, H('#7a6c5c') * (0.9 + rid[..., None] * 0.2), grey * 0.5)
    cc = H('#9a7f5c') * (1.0 + t.fbm(50, 50, 4)[..., None] * 0.10)
    cc *= 0.92
    bc = mix3(cc, sc, stone)
    rough = 0.90 + (1 - stone) * 0.06 + t.fbm(30, 30, 3) * 0.03
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=45.0, target='#8a6c4c')


def oak(n, fresh=False, charred=False):
    t = T.Tex(n, 303 if not charred else 313)
    tile = 1.0
    if charred:
        # alligator char: blocky cells elongated across the grain, deep fissures, glowing embers in the cracks
        f1, f2, cid, _, _ = t.worley(14, 22, jitter=0.7, aspect=22 / 14 * 0.8)
        edge = (f2 - f1) * 0.5 * (tile / 22)
        crack = 1.0 - sstep(0.002, 0.006, edge + t.fbm(60, 60, 3) * 0.0015)
        rid = t.cell_rand(cid, 14 * 22)
        top = np.sqrt(sstep(0, 0.012, edge)) * (0.004 + rid * 0.004) + t.fbm(100, 100, 3) * 0.0008
        grain = t.fbm(120, 6, 3) * 0.0008
        h = top + grain - crack * 0.012
        bc = H('#2a231d') * (0.85 + rid[..., None] * 0.5) * (1 + t.fbm(50, 50, 3)[..., None] * 0.25)
        bc = mix3(bc, H('#100d0b'), crack * 0.85)
        silver = np.clip(t.fbm(8, 8, 4) * 1.5 - 0.3, 0, 1) * 0.35   # grey ash bloom on the tops
        bc = mix3(bc, H('#5a5550'), silver * sstep(0.0, 0.01, edge))
        rough = 0.82 - (1 - crack) * rid * 0.25
        alpha = crack * np.clip(t.fbm(10, 16, 3) * 1.4 + 0.3, 0, 1)   # ember mask (state "embers")
        return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=80.0, alpha=alpha, target='#1f1a16')
    # grain along v: stretched noise, growth rings as wavy streaks
    warp = t.fbm(3, 2, 4) * 0.06
    rings = np.sin((t.u + warp) * 2 * np.pi * 38 + t.fbm(6, 3, 3) * 9)
    fine = t.fbm(160, 6, 4)
    grain = rings * 0.5 + fine * 0.8
    # end checks / cracks: thin dark lines along the grain, limited length
    cr = np.clip(t.ridged(48, 2, 2) - 0.86, 0, 1) / 0.14
    cr *= np.clip(t.fbm(10, 4, 2) * 2.0, 0, 1)
    h = grain * 0.0012 - cr * 0.006 + t.fbm(8, 4, 4) * 0.002
    tone = np.clip(0.5 + grain * 0.22 + t.fbm(6, 3, 4) * 0.3, 0, 1)
    if fresh:
        bc = ramp(tone, ['#5e4430', '#7a5c40', '#93724f'])
        weather = np.zeros_like(tone)
    else:
        bc = ramp(tone, ['#33241a', '#544030', '#6e533d'])
        weather = np.clip(t.fbm(24, 2, 4) * 1.3 + t.fbm(3, 2, 3) * 0.8 + 0.05, 0, 1) * 0.42   # silver-brown streaks where the rain hits
        bc = mix3(bc, H('#7b7266') * (0.8 + tone[..., None] * 0.4), weather)
    bc = mix3(bc, H('#1c140d'), cr * 0.9)
    rough = 0.75 + weather * 0.12 + cr * 0.1
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=90.0, target='#5b4532' if not fresh else '#7a5c40')


def thatch(n, sodden=False):
    """Combed reed thatch: long continuous strands down the slope (v), soft course lines every ~0.3 m where the
    butt ends of the next bundle show, tufts and stray reeds; no rectangular clumps."""
    t = T.Tex(n, 404)
    tile = 2.0
    nc = 7
    course = tile / nc
    vm = t.v * tile
    rag = t.fbm(260, 1, 3) * 0.05 + t.value(40, 1) * 0.025
    k = np.floor((vm - rag) / course)
    f = (vm - rag) / course - k                      # 0 at the butt line .. 1 under the next course
    wu = t.u + t.fbm(4, 8, 3) * 0.01                 # strands wander a little
    s1 = t.fbm(520, 2, 2, u=wu, v=t.v)              # long strands
    s2 = t.fbm(1100, 4, 2, u=wu + 0.3, v=t.v)
    s3 = t.fbm(240, 3, 3, u=wu + 0.7, v=t.v)
    strands = s1 * 0.45 + s2 * 0.3 + s3 * 0.25
    tufts = t.fbm(30, 9, 4)                           # bundles: soft, elongated down the slope
    rise = (1.0 - f) ** 2.2
    h = rise * 0.012 + strands * 0.007 + tufts * 0.008 - sstep(0.97, 1.0, f) * 0.004
    stray = np.clip(t.ridged(150, 10, 2, u=t.u + t.v * 0.12) - 0.935, 0, 1) / 0.065
    h += stray * 0.004
    tone = np.clip(0.52 + strands * 0.6 + tufts * 0.35 + (rise - 0.4) * 0.12, 0, 1)
    bc = ramp(tone, ['#6c4f37', '#82623d', '#9a7a4a', '#a98957', '#b89868'])
    # weathered grey cast and darker shadowed butt line
    grey = np.clip(t.fbm(5, 3, 4) * 1.2 + 0.35, 0, 1) * 0.32
    bc = mix3(bc, H('#7c705e'), grey)
    bc *= (1.0 - sstep(0.0, 0.12, 0.12 - f)[..., None] * 0.18)
    bc *= (1.0 + stray[..., None] * 0.18)
    rough = 0.95 + t.fbm(20, 20, 2) * 0.03
    if sodden:
        bc = mix3(bc * 0.55, H('#3e3a2a'), np.clip(t.fbm(6, 6, 4) * 1.4 + 0.3, 0, 1) * 0.5)
        rough = rough * 0.75
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=45.0, target='#9a7a4a')


def rope(n):
    t = T.Tex(n, 505)
    tile = 0.25   # u = around (0.25 m), v = along
    tw = np.sin((t.u * 3 + t.v * 6) * 2 * np.pi) * 0.5 + 0.5
    fib = t.fbm(120, 30, 3, u=t.u + t.v * 2) * 0.5
    h = tw * 0.004 + fib * 0.0008
    bc = H('#6b5236') * (0.75 + tw[..., None] * 0.4) * (1 + fib[..., None] * 0.3)
    rough = 0.9 + 0 * tw
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=120.0, target='#6b5236')


def wool(n):
    t = T.Tex(n, 606)
    tile = 0.5
    th = 120   # threads per tile -> ~4 mm
    a = np.sin(t.u * th * np.pi * 2)
    b = np.sin(t.v * th * np.pi * 2)
    over = np.sign(np.sin(t.u * th * np.pi) * np.sin(t.v * th * np.pi))
    warp = np.abs(np.sin(t.u * th * np.pi)) ** 0.6
    weft = np.abs(np.sin(t.v * th * np.pi)) ** 0.6
    weave = np.where(over > 0, warp, weft)
    fuzz = t.fbm(200, 200, 3)
    h = weave * 0.0012 + fuzz * 0.0003 + t.fbm(8, 8, 4) * 0.002
    dye = np.clip(0.5 + t.fbm(6, 6, 4) * 0.6 + fuzz * 0.3, 0, 1)
    bc = ramp(dye, ['#a06a1c', '#c88f2e', '#e0a848'])
    bc *= (0.80 + weave[..., None] * 0.28)
    _ = a, b
    rough = 0.9 + fuzz * 0.04
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=200.0, target='#c88f2e')


def bone(n):
    t = T.Tex(n, 707)
    tile = 0.5
    pores = np.clip(-t.fbm(140, 140, 3) - 0.3, 0, 1)
    h = t.fbm(10, 10, 5) * 0.002 - pores * 0.0006 + t.ridged(30, 30, 3) * 0.0006
    stain = np.clip(t.fbm(5, 5, 4) * 1.4, 0, 1)
    bc = H('#d9cbb0') * (1 + t.fbm(40, 40, 3)[..., None] * 0.06)
    bc = mix3(bc, H('#a8957a'), np.clip(stain * 0.6 + 0.25, 0, 1))
    bc = mix3(bc, H('#8a7a66'), pores * 0.5)
    rough = 0.7 + pores * 0.15
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=150.0, target='#d9cbb0')


def ember(n):
    t = T.Tex(n, 808)
    tile = 0.5
    f1, f2, cid, _, _ = t.worley(9, 9, jitter=0.9)
    edge = (f2 - f1) * 0.5 * (tile / 9)
    rid = t.cell_rand(cid, 81)
    lump = np.sqrt(sstep(0, 0.02, edge)) * (0.01 + rid * 0.015)
    h = lump + t.fbm(60, 60, 3) * 0.002
    ash = np.clip(t.fbm(8, 8, 4) * 1.6 + 0.1, 0, 1)
    bc = mix3(H('#1a1512') * (0.7 + rid[..., None] * 0.6), H('#7d7a74'), ash * 0.6)
    glow = (1 - sstep(0.0, 0.006, edge)) * (1 - ash) + np.clip(t.fbm(30, 30, 3) - 0.2, 0, 1) * (1 - ash) * 0.6
    rough = 0.9 + 0 * h
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=80.0, alpha=np.clip(glow, 0, 1), target=None)


def monolith(n):
    """Single weathered standing stone (grey-brown sandstone): grain, pits, flaking, no joints."""
    t = T.Tex(n, 1919)
    tile = 1.5
    grain = t.fbm(10, 22, 6)
    flake = np.clip(t.ridged(5, 5, 4) - 0.62, 0, 1)
    pits = np.clip(-t.fbm(70, 70, 3) - 0.3, 0, 1)
    h = grain * 0.006 + t.fbm(3, 3, 5) * 0.02 + flake * 0.008 - pits * 0.002 + t.fbm(150, 150, 3) * 0.0005
    tone = np.clip(0.5 + grain * 0.6 + t.fbm(4, 6, 4) * 0.35, 0, 1)
    bc = ramp(tone, ['#5a5145', '#6f6454', '#807260', '#8e8270'])
    bc *= (1 + t.fbm(60, 60, 4)[..., None] * 0.08)
    bc = mix3(bc, H('#5a5044'), pits * 0.6)
    bc = mix3(bc, H('#b4a890'), np.clip(flake * 2, 0, 1) * 0.3)
    rough = 0.88 + pits * 0.06
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=60.0, target='#8a7a62')


def plaster(n):
    t = T.Tex(n, 909)
    tile = 2.0
    trowel = t.fbm(10, 14, 5) * 0.003 + np.clip(t.ridged(18, 22, 3) - 0.7, 0, 1) * 0.004
    crack = np.clip(t.ridged(14, 14, 4) - 0.93, 0, 1) / 0.07 * np.clip(t.fbm(4, 4, 2) * 2, 0, 1)
    h = trowel - crack * 0.002 + t.fbm(120, 120, 3) * 0.0003
    tone = np.clip(0.5 + t.fbm(5, 5, 5) * 0.5, 0, 1)
    bc = ramp(tone, ['#d8c2aa', '#e8d6c0', '#f0e4d2'])
    dirt = np.clip(t.fbm(3, 8, 4) * 1.4 - 0.2, 0, 1) * 0.25
    bc = mix3(bc, H('#a88f74'), dirt)
    bc = mix3(bc, H('#8a7560'), crack * 0.6)
    rough = 0.85 + t.fbm(30, 30, 3) * 0.05
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=60.0, target='#e8d6c0')


def paint_cobalt(n):
    w = oak(n)
    t = T.Tex(n, 1010)
    chip = np.clip(t.fbm(20, 20, 5) * 2.4 - 0.75, 0, 1)
    paint = ramp(np.clip(0.5 + t.fbm(8, 8, 4) * 0.5, 0, 1), ['#26408c', '#2f4fa8', '#3c5cb4'])
    bc = mix3(paint, w['bc'], chip)
    h = w['h'] * 0.6 + (1 - chip) * 0.0004
    rough = 0.6 + chip * 0.2
    return dict(bc=bc, h=h, rough=rough, tile=1.0, ao_k=90.0, target='#2f4fa8')


def reed_panel(n):
    t = T.Tex(n, 1111)
    tile = 1.0
    nb = 14   # horizontal reed bundles per tile
    vv = t.v * nb
    k = np.floor(vv)
    f = vv - k
    stakes = 6   # vertical stakes, over/under alternating per bundle
    su = t.u * stakes
    sk = np.floor(su)
    sf = su - sk
    over = ((k + sk) % 2) * 2 - 1
    bulge = np.sin(f * np.pi) ** 0.7
    undul = np.cos((sf - 0.5) * np.pi) * over * 0.5 + 0.5
    reed = t.fbm(6, 300, 2, u=t.u, v=t.v)   # reed fibres along u
    h = bulge * (0.006 + undul * 0.006) + reed * 0.001
    tone = np.clip(0.5 + reed * 0.5 + (k % 3 - 1) * 0.08 + t.fbm(5, 5, 3) * 0.3, 0, 1)
    bc = ramp(tone, ['#5a4128', '#7a5c3c', '#97774f'])
    bc *= (0.65 + 0.35 * bulge[..., None]) * (0.85 + 0.15 * undul[..., None])
    rough = 0.9 + 0 * h
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=70.0, target='#7a5c3c')


def dressed_stone(n):
    t = T.Tex(n, 1212)
    tile = 2.0
    B = coursed_blocks(t, tile, 0.25, 0.5, 0.8, 0.004)
    d = B['d']
    joint = 1.0 - sstep(0.002, 0.005, d)
    rb = t.cell_rand(B['bid'], B['count'])
    h = sstep(0.0, 0.01, d) * 0.004 + t.fbm(80, 80, 3) * 0.0005 + (rb - 0.5) * 0.001 - joint * 0.004
    tone = np.clip(rb * 0.6 + 0.2 + t.fbm(12, 12, 4) * 0.2, 0, 1)
    bc = ramp(tone, ['#bba57f', '#cdb894', '#dccbab'])
    bc *= (1 + t.fbm(70, 70, 4)[..., None] * 0.06)
    bc = mix3(bc, H('#8a7a62'), joint * 0.8)
    rough = 0.8 + t.fbm(20, 20, 3) * 0.05
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=80.0, target='#cdb894')


def overlapped(t, tile, row_h, lo, hi, depth):
    """Shingle / slate rows: v up toward the ridge. Returns height, block ids, joint distance."""
    B = coursed_blocks(t, tile, row_h, lo, hi, 0.004)
    f = (B['vm'] % row_h) / row_h   # 0 at the bottom (exposed butt) .. 1 under the next row
    h = (1.0 - f) * depth
    return B, f, h


def shingle(n):
    t = T.Tex(n, 1313)
    tile = 1.0
    B, f, h = overlapped(t, tile, 0.125, 0.09, 0.2, 0.012)
    gap = 1.0 - sstep(0.001, 0.004, B['dx'])
    rb = t.cell_rand(B['bid'], B['count'])
    grain = t.fbm(200, 6, 3, u=t.u + rb[..., None][..., 0] * 0.3)
    h = h + grain * 0.0006 - gap * 0.006 + (rb - 0.5) * 0.002
    tone = np.clip(rb * 0.7 + 0.15 + grain * 0.3, 0, 1)
    bc = ramp(tone, ['#6e7276', '#8f949a', '#a8acb0'])
    bc = mix3(bc, H('#6a5a48'), np.clip(t.fbm(6, 6, 3) * 1.2, 0, 1) * 0.25)   # some brown showing
    bc *= (1 - sstep(0.85, 1.0, f)[..., None] * 0.4)
    bc = mix3(bc, H('#2a2826'), gap)
    rough = 0.8 + grain * 0.05
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=90.0, target='#8f949a')


def slate(n):
    t = T.Tex(n, 1414)
    tile = 1.0
    B, f, h = overlapped(t, tile, 0.11, 0.18, 0.3, 0.008)
    gap = 1.0 - sstep(0.001, 0.003, B['dx'])
    rb = t.cell_rand(B['bid'], B['count'])
    cleave = t.fbm(30, 30, 4) * 0.0008 + np.clip(t.ridged(40, 40, 3) - 0.8, 0, 1) * 0.003
    h = h + cleave - gap * 0.005 + (rb - 0.5) * 0.0015
    tone = np.clip(rb * 0.6 + 0.2 + t.fbm(16, 16, 4) * 0.25, 0, 1)
    bc = ramp(tone, ['#2c3036', '#3c4048', '#4c525c'])
    bc *= (1 - sstep(0.85, 1.0, f)[..., None] * 0.35)
    bc = mix3(bc, H('#15171a'), gap)
    rough = 0.55 + rb * 0.15 + t.fbm(30, 30, 3) * 0.05
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=90.0, target='#3c4048')


def turf(n):
    t = T.Tex(n, 1515)
    tile = 2.0
    blades = t.fbm(300, 300, 2) * 0.5 + 0.5
    clumps = t.fbm(20, 20, 5) * 0.5 + 0.5
    soil = np.clip(0.35 - clumps, 0, 1) * 2.5
    h = clumps * 0.03 + blades * 0.006 * (1 - soil)
    gc = ramp(np.clip(blades * 0.7 + clumps * 0.4 - 0.1, 0, 1), ['#334019', '#4c5a24', '#606a30', '#787a40'])
    bc = mix3(gc, H('#5a4632') * (0.8 + blades[..., None] * 0.3), soil)
    rough = 0.95 + 0 * h
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=40.0, target='#5f6b2e')


def birch_bark(n):
    t = T.Tex(n, 1616)
    tile = 1.0
    lent = np.clip(t.fbm(8, 90, 3, u=t.u, v=t.v) - 0.25, 0, 1)   # horizontal lenticels (u around, v up)
    lent = (np.clip(t.ridged(10, 60, 2) - 0.78, 0, 1) / 0.22) * np.clip(t.fbm(10, 4, 3) * 2 + 0.5, 0, 1)
    patches = np.clip(t.fbm(6, 10, 4) * 1.8 - 0.4, 0, 1)
    h = -lent * 0.002 + t.fbm(40, 80, 3) * 0.0004
    bc = H('#d8d4c8') * (1 + t.fbm(30, 60, 3)[..., None] * 0.06)
    bc = mix3(bc, H('#2a2622'), lent * 0.9)
    bc = mix3(bc, H('#4a443c'), patches * 0.6)
    rough = 0.7 + lent * 0.2
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=100.0, target=None)


def foliage(n):
    """Masked foliage atlas, 2 x 2 tiles: (0,0) grass tuft, (1,0) fern frond, (0,1) birch twig, (1,1) weed strands.
    Alpha = coverage. Each tile covers u, v in [0, 0.5]."""
    t = T.Tex(n, 1717)
    rng = np.random.default_rng(1717)
    bc = np.zeros((n, n, 3), np.float32)
    al = np.zeros((n, n), np.float32)
    hgt = np.zeros((n, n), np.float32)
    half = n // 2
    yy, xx = np.mgrid[0:half, 0:half].astype(np.float32) / half   # v up, u across (0..1 within the tile)

    def put(tx, ty, a, c, h):
        sl = (slice(ty * half, (ty + 1) * half), slice(tx * half, (tx + 1) * half))
        m = a > al[sl]
        al[sl] = np.where(m, a, al[sl])
        bc[sl] = np.where(m[..., None], c, bc[sl])
        hgt[sl] = np.where(m, h, hgt[sl])

    # grass tuft: 70 curved blades from the bottom centre
    for _ in range(70):
        x0 = 0.5 + rng.normal(0, 0.08)
        lean = rng.normal(0, 0.25)
        L = rng.uniform(0.45, 0.95)
        w0 = rng.uniform(0.010, 0.022)
        s = np.clip(yy / L, 0, 1)
        cx = x0 + lean * s ** 1.6 * L
        w = w0 * (1 - s) ** 0.8
        d = np.abs(xx - cx)
        a = ((d < w) & (yy < L)).astype(np.float32)
        tone = rng.uniform(0, 1)
        c = ramp(np.full_like(s, tone) * 0.7 + s * 0.3, ['#3d4a22', '#5f6b2e', '#8a8a48', '#a89a5e'])
        put(0, 0, a, c, 1 - d / np.maximum(w, 1e-4))
    # fern frond: a rachis with paired pinnae
    for side in range(3):
        x0 = 0.3 + side * 0.2
        lean = (side - 1) * 0.15
        cx = x0 + lean * yy ** 1.5
        d = np.abs(xx - cx)
        a = ((d < 0.006) & (yy < 0.95)).astype(np.float32)
        c = np.broadcast_to(H('#4a5a24'), (half, half, 3))
        put(1, 0, a, c, a)
        for k in range(18):
            yk = 0.1 + k * 0.047
            lk = 0.16 * (1 - (yk - 0.1) / 0.9) ** 0.6
            for sgn in (-1, 1):
                ax = xx - (x0 + lean * yk ** 1.5)
                ay = yy - yk
                along = ax * sgn
                perp = ay - along * 0.35
                a2 = ((along > 0) & (along < lk) & (np.abs(perp) < 0.012 * (1 - along / max(lk, 1e-3)) + 0.002)).astype(np.float32)
                c2 = ramp(np.clip(np.full_like(xx, rng.uniform(0.2, 0.8)), 0, 1), ['#3a4a1c', '#56702a', '#7a8a3a'])
                put(1, 0, a2, c2, a2)
    # birch twig: small serrated ovate leaves on a thin twig
    for k in range(26):
        lx, ly = rng.uniform(0.12, 0.88), rng.uniform(0.12, 0.88)
        ang = rng.uniform(0, 2 * np.pi)
        ca, sa = np.cos(ang), np.sin(ang)
        px = (xx - lx) * ca + (yy - ly) * sa
        py = -(xx - lx) * sa + (yy - ly) * ca
        L = rng.uniform(0.07, 0.11)
        W = L * 0.42
        s = px / L
        wid = W * np.sqrt(np.clip(1 - (2 * s - 1) ** 2, 0, 1)) * (1 + 0.08 * np.sin(s * 40))
        a = ((s > 0) & (s < 1) & (np.abs(py) < wid)).astype(np.float32)
        tone = rng.uniform(0, 1)
        c = ramp(np.clip(np.full_like(xx, tone) + (py / max(W, 1e-3)) * 0.15, 0, 1), ['#4a6a24', '#6a8a2e', '#93a84a'])
        put(0, 1, a, c, a * (1 - np.abs(py) / max(W, 1e-3)))
    # weed strands: hanging slimy strands (for drowned)
    for _ in range(40):
        x0 = rng.uniform(0.1, 0.9)
        L = rng.uniform(0.4, 1.0)
        s = np.clip((1 - yy) / L, 0, 1)
        cx = x0 + np.sin(s * 6 + rng.uniform(0, 6)) * 0.02
        w = rng.uniform(0.006, 0.016) * (1 - s * 0.7)
        a = ((np.abs(xx - cx) < w) & ((1 - yy) < L)).astype(np.float32)
        c = ramp(np.full_like(xx, rng.uniform(0, 1)), ['#2a3418', '#3d4a2a', '#4a5a2a'])
        put(1, 1, a, c, a)
    # dilate colour into transparent texels so mips do not fringe dark
    filled = al > 0.5
    col = bc.copy()
    for r in (2, 4, 8, 16, 32):
        wsum = blur(filled.astype(np.float32), r)
        csum = np.stack([blur(col[..., i] * filled, r) for i in range(3)], -1)
        est = csum / np.maximum(wsum, 1e-4)[..., None]
        col = np.where(filled[..., None], col, np.where((wsum > 1e-3)[..., None], est, col))
        filled = filled | (wsum > 1e-3)
    h = hgt * 0.002
    rough = 0.8 + 0 * h
    return dict(bc=col, h=h, rough=rough, tile=1.0, ao_k=0.0, alpha=al, target=None)


def fringe(n):
    """Hanging straw ends for thatch eaves (alpha cards): u across (0.5 m), v up (card height). Strands hang from the
    top; lengths vary so the bottom edge is ragged."""
    t = T.Tex(n, 2020)
    rng = np.random.default_rng(2020)
    al = np.zeros((n, n), np.float32)
    tone = np.zeros((n, n), np.float32)
    xs = np.arange(n, dtype=np.float32) / n
    ys = np.arange(n, dtype=np.float32) / n
    X, Y = np.meshgrid(xs, ys)
    for k in range(260):
        x0 = rng.uniform(0, 1)
        L = rng.uniform(0.35, 1.0)
        lean = rng.normal(0, 0.05)
        w = rng.uniform(0.0015, 0.0035)
        s = np.clip((1.0 - Y) / L, 0, 1)
        cx = (x0 + lean * s) % 1.0
        dx = np.abs(((X - cx) + 0.5) % 1.0 - 0.5)
        m = (dx < w * (1.0 - 0.5 * s)) & ((1.0 - Y) < L)
        al = np.where(m, 1.0, al)
        tone = np.where(m, rng.uniform(0, 1) * 0.7 + (1 - s) * 0.3, tone)
    bc = ramp(tone, ['#5e4a36', '#86663f', '#9a7a4a', '#b89868'])
    filled = al > 0.5
    col = bc.copy()
    for r in (2, 4, 8, 16):
        wsum = blur(filled.astype(np.float32), r)
        csum = np.stack([blur(col[..., i] * filled, r) for i in range(3)], -1)
        est = csum / np.maximum(wsum, 1e-4)[..., None]
        col = np.where(filled[..., None], col, np.where((wsum > 1e-3)[..., None], est, col))
        filled = filled | (wsum > 1e-3)
    return dict(bc=col, h=np.zeros_like(al), rough=np.full_like(al, 0.9), tile=0.5, ao_k=0.0, alpha=al, target=None)


def flame(n):
    """Additive flame card (BC only, black = nothing). Two tongues; v up."""
    t = T.Tex(n, 1818)
    x = (t.u - 0.5) * 2
    y = t.v
    wob = t.fbm(4, 6, 3) * 0.18 * y
    w = 0.55 * (1 - y) ** 0.8 * np.sqrt(np.clip(y * 4, 0, 1))
    core = np.clip(1 - np.abs(x + wob) / np.maximum(w, 1e-3), 0, 1)
    turb = np.clip(t.fbm(8, 12, 4, u=t.u, v=t.v) * 0.5 + 0.6, 0, 1)
    i = core ** 1.5 * turb * (1 - y) ** 0.5
    col = ramp(np.clip(i * 1.3, 0, 1), ['#000000', '#a02a08', '#ff7a1a', '#ffd27a', '#fff2d0'])
    return dict(bc=col * i[..., None], h=np.zeros_like(i), rough=np.ones_like(i), tile=1.0, ao_k=0.0, target=None)


SETS = {
    'limestone': (limestone, 2048, {}),
    'limestone_fresh': (limestone, 2048, {'pale': True}),
    'rubble': (rubble, 2048, {}),
    'oak': (oak, 2048, {}),
    'oak_fresh': (oak, 1024, {'fresh': True}),
    'oak_charred': (oak, 1024, {'charred': True}),
    'thatch': (thatch, 2048, {}),
    'thatch_sodden': (thatch, 1024, {'sodden': True}),
    'rope': (rope, 512, {}),
    'wool': (wool, 1024, {}),
    'bone': (bone, 512, {}),
    'ember': (ember, 512, {}),
    'monolith': (monolith, 1024, {}),
    'plaster': (plaster, 2048, {}),
    'paint_cobalt': (paint_cobalt, 1024, {}),
    'reed_panel': (reed_panel, 1024, {}),
    'dressed_stone': (dressed_stone, 2048, {}),
    'shingle': (shingle, 1024, {}),
    'slate': (slate, 1024, {}),
    'turf': (turf, 1024, {}),
    'birch_bark': (birch_bark, 512, {}),
    'foliage': (foliage, 1024, {}),
    'fringe': (fringe, 512, {}),
    'flame': (flame, 256, {}),
}


def camel(name):
    return ''.join(p.capitalize() for p in name.split('_'))


def build(name, size_override=None):
    fn, size, kw = SETS[name]
    n = size_override or size
    t0 = time.time()
    r = fn(n, **kw)
    d = os.path.join(OUT, name)
    os.makedirs(d, exist_ok=True)
    base = os.path.join(d, 'T_' + camel(name))
    bc = np.clip(r['bc'], 0, 1)
    T.save_png(base + '_BC.png', bc, r.get('alpha'), srgb=True)
    info = {'tile_m': r['tile'], 'size': n, 'alpha': 'alpha' in r}
    if name != 'flame':
        nrm = T.normal_from_height(r['h'], r['tile'], n)
        T.save_png(base + '_N.png', nrm, None, srgb=False)
        ao = T.cavity_ao(r['h'], n, 0.01, r['tile'], r['ao_k']) if r['ao_k'] > 0 else np.ones_like(r['h'])
        orm = np.stack([ao, np.clip(r['rough'], 0.04, 1), np.zeros_like(ao)], -1)
        T.save_png(base + '_ORM.png', orm, None, srgb=False)
        info['ao_mean'] = float(ao.mean())
        info['rough_mean'] = float(np.clip(r['rough'], 0, 1).mean())
    info['albedo'] = T.stats(name, bc if 'alpha' not in r else bc[r['alpha'] > 0.5], r.get('target'))
    info['seconds'] = round(time.time() - t0, 1)
    print('DMTEX', name, info)
    return info


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    size = None
    names = []
    for a in argv:
        if a.startswith('--size='):
            size = int(a.split('=')[1])
        else:
            names.append(a)
    names = names or list(SETS)
    mpath = os.path.join(OUT, 'manifest.json')
    man = {}
    if os.path.exists(mpath):
        with open(mpath) as f:
            man = json.load(f)
    for nm in names:
        man[nm] = build(nm, size)
    os.makedirs(OUT, exist_ok=True)
    with open(mpath, 'w') as f:
        json.dump(man, f, indent=1, sort_keys=True)


if __name__ == '__main__':
    main()
