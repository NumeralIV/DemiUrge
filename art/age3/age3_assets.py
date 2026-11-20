# DEMIURGE art - Age3 (Sail Age) kit: every asset as a function (lod, state) -> (MB, info).
#
# Conventions (docs/ASSETS.md): metres, Z up, front (street / water side) toward -Y, pivot at the centre of
# the footprint at ground level, foundations reach 0.3 m below Z = 0. Quays and piers: top surface at Z = 0,
# still water expected at WATER_Z.
#
# lod 0..2 are built by the same code with less detail; lod 3 is a hull. `state` is one of
# intact / construction / damaged / ruin. Buildings mark faces with tags; the generic state passes in
# build_age3.py cut, punch and thicken by tag. Docks build their states here directly.
import math
import random

from age3_geo import MB, rect_path, rotz, norm, add, sub, mul, cross, dot, length, SLOT, cell_uv, BANDS, INSET, newell

WATER_Z = -1.8
FOOT = -0.3


class Ctx:
    def __init__(self, lod, state, seed):
        self.lod = lod
        self.state = state
        self.holes = state in ('construction', 'ruin')     # openings left as holes (walls are thickened later)
        self.damaged = state == 'damaged'
        self.rng = random.Random(seed)


# ----------------------------------------------------------------------------------------------- facades
def facade(mb, a, b, z0, storeys, ctx, bays, kinds, margin=0.7, shutters=0.5, recess=0.22, sill=True, keystone=False,
           srange=None, frames=True):
    """Wall from a to b (2D, CCW footprint order: outside on the right). storeys: [dict(h, band, win=(w, h, sill))].
    kinds(k, i) -> 'window' | 'window_shut' | 'window_arch' | 'door' | 'door_wide' | 'arcade' | 'none'."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    d = (dx / L, dy / L)
    n = (d[1], -d[0])
    rot = math.atan2(d[1], d[0])
    s_lo, s_hi = (0.0, L) if srange is None else srange

    def P(s, z, off=0.0):
        return (a[0] + d[0] * s + n[0] * off, a[1] + d[1] * s + n[1] * off, z)

    def rect(s0, s1, za, zb, band, off=0.0, tag='wall'):
        if s1 - s0 < 1e-4 or zb - za < 1e-4:
            return
        mb.poly([P(s0, za, off), P(s1, za, off), P(s1, zb, off), P(s0, zb, off)], band, 'metric', anchor=z0, tag=tag)

    lod = ctx.lod
    bw = (L - 2 * margin) / max(bays, 1)
    centres = [margin + (i + 0.5) * bw for i in range(bays)]
    zc = z0
    for k, st in enumerate(storeys):
        h = st['h']
        band = st['band']
        top = zc + h
        ww, wh, ws = st.get('win', (1.1, 1.9, 0.9))
        if (lod >= 2 and not ctx.holes) or (lod >= 1 and ctx.holes):
            # flat: one wall face per storey, openings as quads floating just in front. In the construction and
            # ruin states (verifier fix) LOD1+ also take this path: the openings become dark 'charred' cards,
            # which the wall-thickening pass turns into thin dark boxes that read as openings from both sides,
            # instead of real holes (LOD1 was ~95 % of LOD0 in those states).
            rect(max(0.0, s_lo), min(L, s_hi), zc, top, band)
            for i, c in enumerate(centres):
                kd = kinds(k, i)
                if kd == 'none' or c < s_lo or c > s_hi:
                    continue
                kd, w_, h_, zs = _open_dims(kd, ww, wh, ws, zc, st, ctx)
                if ctx.holes:
                    kd = 'charred'
                mb.aquad(P(c - w_ / 2, zs, 0.03), P(c + w_ / 2, zs, 0.03), P(c + w_ / 2, zs + h_, 0.03), P(c - w_ / 2, zs + h_, 0.03), kd, tag='wall')
            zc = top
            continue
        s = 0.0
        for i, c in enumerate(centres):
            kd = kinds(k, i)
            if kd == 'none' or c < s_lo or c > s_hi:
                continue
            kd, w_, h_, zs = _open_dims(kd, ww, wh, ws, zc, st, ctx)
            s0, s1 = c - w_ / 2, c + w_ / 2
            ze = zs + h_
            rect(max(s, s_lo), s0, zc, top, band)             # pier
            rect(s0, s1, zc, zs, band)                         # below sill
            rect(s0, s1, ze, top, band)                        # above lintel
            s = s1
            if ctx.holes:
                continue
            if lod == 0:
                dep = recess if kd not in ('door_wide', 'arcade') else 0.35
                mb.aquad(P(s0, zs, -dep), P(s1, zs, -dep), P(s1, ze, -dep), P(s0, ze, -dep), kd, tag='wall')
                rb = 'dressed'
                mb.poly([P(s0, zs, 0), P(s0, zs, -dep), P(s0, ze, -dep), P(s0, ze, 0)], rb, 'fit', tag='wall')
                mb.poly([P(s1, zs, -dep), P(s1, zs, 0), P(s1, ze, 0), P(s1, ze, -dep)], rb, 'fit', tag='wall')
                mb.poly([P(s0, ze, 0), P(s0, ze, -dep), P(s1, ze, -dep), P(s1, ze, 0)], rb, 'fit', tag='wall')
                if zs > zc + 0.01:
                    mb.poly([P(s0, zs, -dep), P(s0, zs, 0), P(s1, zs, 0), P(s1, zs, -dep)], rb, 'fit', tag='wall')
                if sill and kd.startswith('window') and zs > zc + 0.3:
                    sc = P((s0 + s1) / 2, zs - 0.06, 0.07)
                    mb.box(sc, w_ / 2 + 0.12, 0.09, 0.06, 'dressed', rot=rot, mode='fit', tag='wall')
                if keystone and kd.startswith('window') and ze + 0.4 < top:
                    sc = P((s0 + s1) / 2, ze + 0.16, 0.04)
                    mb.box(sc, w_ / 2 + 0.18, 0.05, 0.16, 'dressed', rot=rot, mode='fit', tag='wall', bottom=True)
                if frames and kd in ('window', 'window_shut', 'window_broken', 'boarded') and zs > zc + 0.3:
                    # verifier fix: dressed-stone architrave (jambs + head) so the openings cast shadow lines at
                    # 40 m; LOD0 only, so it also brings LOD1 closer to the ~40 % target
                    fw, fp = 0.13, 0.04
                    for sj in (s0 - fw / 2, s1 + fw / 2):
                        mb.box(P(sj, (zs + ze) / 2, fp / 2), fw / 2, fp / 2, (ze - zs) / 2, 'dressed', rot=rot, mode='fit',
                               tag='wall', sides=(True, True, False, True))
                    if not (keystone and ze + 0.4 < top) and ze + 0.25 < top:
                        mb.box(P((s0 + s1) / 2, ze + 0.09, 0.035), w_ / 2 + fw + 0.05, 0.035, 0.09, 'dressed', rot=rot,
                               mode='fit', tag='wall', bottom=True, sides=(True, True, False, True))
                if kd == 'window' and ctx.rng.random() < shutters:
                    sw = w_ * 0.5
                    mb.aquad(P(s0 - sw, zs, 0.05), P(s0, zs, 0.05), P(s0, ze, 0.05), P(s0 - sw, ze, 0.05), 'shutter', tag='wall')
                    mb.aquad(P(s1, zs, 0.05), P(s1 + sw, zs, 0.05), P(s1 + sw, ze, 0.05), P(s1, ze, 0.05), 'shutter', tag='wall', flipx=True)
            else:
                mb.aquad(P(s0, zs, 0.0), P(s1, zs, 0.0), P(s1, ze, 0.0), P(s0, ze, 0.0), kd, tag='wall')
        rect(max(s, s_lo), min(L, s_hi), zc, top, band)
        zc = top
    return zc


def _open_dims(kd, ww, wh, ws, zc, st, ctx):
    if kd == 'door':
        w_, h_, zs = st.get('door', (1.3, 2.5))[0], st.get('door', (1.3, 2.5))[1], zc
    elif kd == 'door_wide':
        w_, h_, zs = st.get('wide', (2.8, 3.2))[0], st.get('wide', (2.8, 3.2))[1], zc
    elif kd == 'arcade':
        w_, h_, zs = st.get('arc', (2.4, 3.0))[0], st.get('arc', (2.4, 3.0))[1], zc
    else:
        w_, h_, zs = ww, wh, zc + ws
    if ctx.damaged and kd.startswith('window') and ctx.rng.random() < 0.3:
        kd = 'window_broken' if ctx.rng.random() < 0.6 else 'boarded'
    return kd, w_, h_, zs


def win_kinds(seed, pattern=None, shut=0.3):
    rng = random.Random(seed)
    memo = {}

    def f(k, i):
        key = (k, i)
        if key not in memo:
            if pattern:
                v = pattern(k, i)
                if v != 'window':
                    memo[key] = v
                    return v
            memo[key] = 'window_shut' if rng.random() < shut else 'window'
        return memo[key]
    return f


def block_walls(mb, x0, y0, x1, y1, z0, storeys, ctx, specs, margin=0.7, keystone=False):
    """specs: dict side -> (bays, kinds, srange) for 'S' (-Y, front), 'E', 'N', 'W'. None skips a side."""
    edges = {'S': ((x0, y0), (x1, y0)), 'E': ((x1, y0), (x1, y1)), 'N': ((x1, y1), (x0, y1)), 'W': ((x0, y1), (x0, y0))}
    top = z0
    for side, (a, b) in edges.items():
        sp = specs.get(side)
        if sp is None:
            continue
        bays, kinds = sp[0], sp[1]
        sr = sp[2] if len(sp) > 2 else None
        top = facade(mb, a, b, z0, storeys, ctx, bays, kinds, margin=margin, keystone=keystone, srange=sr)
    return top


def string_courses(mb, path, zs, ctx, big=False):
    if ctx.lod >= 2:
        return
    for z in zs:
        if ctx.lod == 0:
            prof = [(0, -0.12), (0.08, -0.08), (0.12, 0.0), (0.12, 0.1), (0.0, 0.14)] if not big else \
                   [(0, -0.3), (0.1, -0.24), (0.18, -0.08), (0.34, 0.06), (0.4, 0.2), (0.0, 0.24)]
        else:
            prof = [(0, -0.12), (0.12, -0.12), (0.12, 0.12), (0.0, 0.12)] if not big else [(0, -0.3), (0.38, 0.0), (0.38, 0.2), (0.0, 0.2)]
        mb.sweep(path, prof, 'dressed', z=z, closed=True, tag='wall')


def corner_pilasters(mb, x0, y0, x1, y1, z0, z1, ctx, w=0.55, p=0.07, band='ashlar', corners=(0, 1, 2, 3)):
    if ctx.lod >= 2:
        return
    cs = [(x0, y0, 1, 1), (x1, y0, -1, 1), (x1, y1, -1, -1), (x0, y1, 1, -1)]
    for ci in corners:
        cx, cy, sx, sy = cs[ci]
        # box spans w inward along both faces, protruding p outward
        xa, xb = sorted((cx - sx * p, cx + sx * w))
        ya, yb = sorted((cy - sy * p, cy + sy * w))
        mb.box(((xa + xb) / 2, (ya + yb) / 2, (z0 + z1) / 2), (xb - xa) / 2, (yb - ya) / 2, (z1 - z0) / 2, band, top=False,
               anchor=z0, tag='wall')


def plinth(mb, x0, y0, x1, y1, h, band='quay', out=0.12, ctx=None):
    mb.box(((x0 + x1) / 2, (y0 + y1) / 2, (FOOT + h) / 2), (x1 - x0) / 2 + out, (y1 - y0) / 2 + out, (h - FOOT) / 2, band,
           top='dressed' if (ctx is None or ctx.lod < 2) else band, bottom=True, anchor=FOOT, tag='base')


def chimney(mb, x, y, z0, z1, ctx, w=0.7, d=0.9, band='plaster'):
    mb.box((x, y, (z0 + z1) / 2), w / 2, d / 2, (z1 - z0) / 2, band, top=False, anchor=z0, tag='upper')
    if ctx.lod <= 1:
        mb.box((x, y, z1 + 0.08), w / 2 + 0.08, d / 2 + 0.08, 0.08, 'dressed', mode='fit', tag='upper', bottom=True)
        if ctx.lod == 0:
            mb.box((x, y, z1 + 0.3), 0.14, 0.14, 0.14, 'dressed', mode='fit', top='rubble', tag='upper')
    else:
        mb.poly([(x - w / 2, y - d / 2, z1), (x + w / 2, y - d / 2, z1), (x + w / 2, y + d / 2, z1), (x - w / 2, y + d / 2, z1)], 'rubble', 'fit', tag='upper')


def balcony(mb, a, b, z, ctx, depth=0.9):
    """Stone slab with an iron railing on the facade line a-b (2D, CCW order)."""
    if ctx.lod >= 2 or ctx.holes:
        return
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    d = (dx / L, dy / L)
    n = (d[1], -d[0])
    rot = math.atan2(d[1], d[0])
    c = ((a[0] + b[0]) / 2 + n[0] * depth / 2, (a[1] + b[1]) / 2 + n[1] * depth / 2, z - 0.1)
    mb.box(c, L / 2, depth / 2, 0.1, 'dressed', rot=rot, mode='fit', bottom=True, tag='upper')
    if ctx.lod == 0:
        # corbels
        for t in (0.15, 0.85):
            p = (a[0] + dx * t + n[0] * depth * 0.35, a[1] + dy * t + n[1] * depth * 0.35, z - 0.35)
            mb.box(p, 0.1, depth * 0.35, 0.15, 'dressed', rot=rot, mode='fit', tag='upper', top=False)
    # iron railing: rails and bars (front and two returns)
    h = 0.95
    o = depth - 0.06
    A_ = (a[0] + n[0] * o, a[1] + n[1] * o)
    B_ = (b[0] + n[0] * o, b[1] + n[1] * o)
    sides = 3
    for p0, p1 in ((A_, B_), (a, A_), (B_, b)):
        mb.cyl_along((p0[0], p0[1], z + h), (p1[0], p1[1], z + h), 0.035, sides, None, cell='iron')
        if ctx.lod == 0:
            mb.cyl_along((p0[0], p0[1], z + 0.12), (p1[0], p1[1], z + 0.12), 0.025, sides, None, cell='iron')
        seg = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
        nb = max(1, int(seg / (0.16 if ctx.lod == 0 else 0.6)))
        for i in range(1, nb):
            t = i / nb
            x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
            mb.cyl_along((x, y, z), (x, y, z + h), 0.012 if ctx.lod == 0 else 0.02, 3, None, cell='iron')


def dormer(mb, x, y_front, z_base, w, ctx, pitch_main, depth=2.2, h=1.6, kind='window'):
    """Gabled dormer on a front (-Y) roof slope. y_front: face position, z_base: sill height on the roof."""
    if ctx.lod >= 2:
        return
    x0, x1 = x - w / 2, x + w / 2
    y0, y1 = y_front, y_front + depth
    zt = z_base + h
    mb.tag = 'roof'
    # cheeks and face
    mb.poly([(x0, y0, z_base), (x1, y0, z_base), (x1, y0, zt), (x0, y0, zt)], 'plaster', 'fit', tag='roof')
    mb.poly([(x1, y0, z_base), (x1, y1, zt), (x1, y0, zt)], 'plaster', 'fit', tag='roof')
    mb.poly([(x0, y1, zt), (x0, y0, z_base), (x0, y0, zt)], 'plaster', 'fit', tag='roof')
    if not ctx.holes:
        mb.aquad((x - w * 0.3, y0 - 0.02, z_base + 0.2), (x + w * 0.3, y0 - 0.02, z_base + 0.2), (x + w * 0.3, y0 - 0.02, zt - 0.1), (x - w * 0.3, y0 - 0.02, zt - 0.1), kind, tag='roof')
    zr = mb.gable_roof(x0, x1, y0 - 0.0, y1, zt, 40, ov=0.15, ovg=0.25, axis='Y', band='tile', thick=0.12, lod=max(1, ctx.lod),
                       gable_band='plaster', tag='roof', gable_walls=False, ridge_cap=False)
    # front gable triangle
    mb.poly([(x0, y0, zt), (x1, y0, zt), (x, y0, zr - 0.1)], 'plaster', 'fit', tag='roof')
    mb.tag = 'wall'


# ----------------------------------------------------------------------------------------------- hull helper (LOD3)
def hull_block(mb, x0, y0, x1, y1, ze, roof, pitch, band='plaster', ov=0.3, axis='X'):
    mb.box(((x0 + x1) / 2, (y0 + y1) / 2, (FOOT + ze) / 2), (x1 - x0) / 2, (y1 - y0) / 2, (ze - FOOT) / 2, band, top=False, mode='fit', tag='wall')
    if roof == 'gable':
        mb.gable_roof(x0, x1, y0, y1, ze, pitch, ov=ov, ovg=ov, axis=axis, lod=3, gable_band=band, ridge_cap=False)
    elif roof == 'hip':
        mb.hip_roof(x0, x1, y0, y1, ze, pitch, ov=ov, lod=3, caps=False)
    else:
        mb.poly([(x0, y0, ze), (x1, y0, ze), (x1, y1, ze), (x0, y1, ze)], band, 'fit', tag='roof')


def hull_windows(mb, x0, y0, x1, y1, z0, storeys, bays_xy, ctx_unused=None):
    """LOD3: one window quad per bay per storey, flat on the walls (they carry the night lights)."""
    edges = [((x0, y0), (x1, y0), bays_xy[0]), ((x1, y0), (x1, y1), bays_xy[1]), ((x1, y1), (x0, y1), bays_xy[0]), ((x0, y1), (x0, y0), bays_xy[1])]
    for a, b, bays in edges:
        if not bays:
            continue
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy)
        d = (dx / L, dy / L)
        n = (d[1], -d[0])
        z = z0
        for st in storeys:
            ww, wh, ws = st.get('win', (1.1, 1.9, 0.9))
            if st.get('hull_skip'):
                z += st['h']
                continue
            # one long strip per storey: the window cell stretched across reads as a row at 1 km
            s0, s1 = 0.8, L - 0.8
            off = 0.04
            P = lambda s, zz: (a[0] + d[0] * s + n[0] * off, a[1] + d[1] * s + n[1] * off, zz)
            for i in range(bays):
                c = 0.7 + (L - 1.4) * (i + 0.5) / bays
                mb.aquad(P(c - ww / 2, z + ws), P(c + ww / 2, z + ws), P(c + ww / 2, z + ws + wh), P(c - ww / 2, z + ws + wh), 'window', tag='wall')
            z += st['h']


# ----------------------------------------------------------------------------------------------- townhouses
def townhouse_a(lod, state, seed=11):
    """Narrow gable-fronted house: 3 storeys, plaster over a stone ground floor, balcony, 6.6 x 11 m."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    info = {}
    W, D = 6.6, 11.0
    x0, x1, y0, y1 = -W / 2, W / 2, -D / 2, D / 2
    pl = 0.45
    storeys = [dict(h=3.6, band='ashlar', win=(1.1, 1.8, 1.0), door=(1.3, 2.6)),
               dict(h=3.2, band='plaster', win=(1.1, 2.0, 0.6)),
               dict(h=3.0, band='plaster', win=(1.0, 1.7, 0.7))]
    ze = pl + sum(s['h'] for s in storeys)
    info.update(footprint=rect_path(x0, y0, x1, y1), eave=ze, base=pl, W=W, D=D)
    if lod == 3:
        hull_block(mb, x0, y0, x1, y1, ze, 'gable', 42, axis='Y')
        hull_windows(mb, x0, y0, x1, y1, pl, storeys, (2, 3))
        info['ridge'] = ze + W / 2 * math.tan(math.radians(42))
        return mb, info
    plinth(mb, x0, y0, x1, y1, pl, ctx=ctx)
    front = win_kinds(seed, lambda k, i: 'door' if (k == 0 and i == 1) else 'window', 0.25)
    side = win_kinds(seed + 1, lambda k, i: 'none' if (i == 1 and k == 0) else 'window', 0.45)
    back = win_kinds(seed + 2, lambda k, i: 'door' if (k == 0 and i == 0) else 'window', 0.5)
    top = block_walls(mb, x0, y0, x1, y1, pl, storeys, ctx,
                      {'S': (2, front), 'E': (3, side), 'N': (2, back), 'W': (3, win_kinds(seed + 3, None, 0.6))}, margin=0.6)
    path = rect_path(x0, y0, x1, y1)
    string_courses(mb, path, [pl + 3.6], ctx)
    string_courses(mb, path, [ze - 0.25], ctx, big=True)
    corner_pilasters(mb, x0, y0, x1, y1, pl, ze, ctx)
    zr = mb.gable_roof(x0, x1, y0, y1, ze, 42, ov=0.35, ovg=0.45, axis='Y', lod=lod, gable_anchor=pl, info=info)
    # attic window in the front gable
    if lod <= 1 and not ctx.holes:
        zw = ze + 0.6
        mb.aquad((-0.45, y0 - 0.03, zw), (0.45, y0 - 0.03, zw), (0.45, y0 - 0.03, zw + 1.3), (-0.45, y0 - 0.03, zw + 1.3), 'window_arch', tag='wall')
    balcony(mb, (x0 + 1.0, y0), (x1 - 1.0, y0), pl + 3.6 + 0.05, ctx)
    chimney(mb, 1.4, 3.2, ze - 1.0, zr + 0.9, ctx)
    info['ridge'] = zr
    return mb, info


def townhouse_b(lod, state, seed=23):
    """Merchant house: 4 storeys, arcaded rusticated ground floor, piano nobile with balcony, hip roof, 10 x 10 m."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    info = {}
    W, D = 10.0, 10.0
    x0, x1, y0, y1 = -W / 2, W / 2, -D / 2, D / 2
    pl = 0.35
    storeys = [dict(h=4.0, band='quay', win=(1.2, 1.6, 1.4), arc=(2.2, 3.1), door=(1.6, 3.0)),
               dict(h=3.7, band='plaster', win=(1.2, 2.3, 0.5)),
               dict(h=3.3, band='plaster', win=(1.1, 1.9, 0.7)),
               dict(h=2.5, band='plaster', win=(0.9, 1.0, 0.8))]
    ze = pl + sum(s['h'] for s in storeys)
    info.update(footprint=rect_path(x0, y0, x1, y1), eave=ze, base=pl, W=W, D=D)
    if lod == 3:
        hull_block(mb, x0, y0, x1, y1, ze, 'hip', 30)
        hull_windows(mb, x0, y0, x1, y1, pl, storeys, (3, 3))
        info['ridge'] = ze + W / 2 * math.tan(math.radians(30))
        return mb, info
    plinth(mb, x0, y0, x1, y1, pl, ctx=ctx)
    front = win_kinds(seed, lambda k, i: ('arcade' if i != 1 else 'door') if k == 0 else 'window', 0.15)
    side = win_kinds(seed + 1, lambda k, i: ('arcade' if i == 0 else 'window') if k == 0 else 'window', 0.35)
    back = win_kinds(seed + 2, lambda k, i: ('door_wide' if i == 1 else 'window') if k == 0 else 'window', 0.45)
    block_walls(mb, x0, y0, x1, y1, pl, storeys, ctx,
                {'S': (3, front), 'E': (3, side), 'N': (3, back), 'W': (3, win_kinds(seed + 3, None, 0.5))}, margin=0.8, keystone=True)
    path = rect_path(x0, y0, x1, y1)
    string_courses(mb, path, [pl + 4.0, pl + 4.0 + 3.7 + 3.3], ctx)
    string_courses(mb, path, [ze - 0.2], ctx, big=True)
    corner_pilasters(mb, x0, y0, x1, y1, pl + 4.0, ze - 0.3, ctx, w=0.6)
    balcony(mb, (-2.2, y0), (2.2, y0), pl + 4.0 + 0.05, ctx, depth=1.0)
    zr = mb.hip_roof(x0, x1, y0, y1, ze, 30, ov=0.55, lod=lod, info=info)
    chimney(mb, -2.6, 1.8, ze - 1.0, zr + 0.4, ctx)
    chimney(mb, 2.8, -1.5, ze - 1.0, zr + 0.1, ctx)
    info['ridge'] = zr
    return mb, info


def townhouse_c(lod, state, seed=37):
    """Stone house with the eaves to the street, two dormers, gable chimneys: 3 storeys, 9 x 8.5 m."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    info = {}
    W, D = 9.0, 8.5
    x0, x1, y0, y1 = -W / 2, W / 2, -D / 2, D / 2
    pl = 0.4
    storeys = [dict(h=3.4, band='ashlar', win=(1.0, 1.7, 1.0), door=(1.3, 2.5)),
               dict(h=3.1, band='ashlar', win=(1.0, 1.8, 0.7)),
               dict(h=2.9, band='ashlar', win=(0.9, 1.5, 0.8))]
    ze = pl + sum(s['h'] for s in storeys)
    pitch = 46
    info.update(footprint=rect_path(x0, y0, x1, y1), eave=ze, base=pl, W=W, D=D)
    if lod == 3:
        hull_block(mb, x0, y0, x1, y1, ze, 'gable', pitch, band='ashlar', axis='X')
        hull_windows(mb, x0, y0, x1, y1, pl, storeys, (3, 2))
        info['ridge'] = ze + D / 2 * math.tan(math.radians(pitch))
        return mb, info
    plinth(mb, x0, y0, x1, y1, pl, ctx=ctx)
    front = win_kinds(seed, lambda k, i: 'door' if (k == 0 and i == 1) else 'window', 0.35)
    back = win_kinds(seed + 2, lambda k, i: 'door' if (k == 0 and i == 2) else 'window', 0.5)
    sides = win_kinds(seed + 1, lambda k, i: 'none' if (k == 0 and i == 0) else 'window', 0.5)
    block_walls(mb, x0, y0, x1, y1, pl, storeys, ctx,
                {'S': (3, front), 'E': (2, sides), 'N': (3, back), 'W': (2, win_kinds(seed + 3, None, 0.5))}, margin=0.8, keystone=True)
    path = rect_path(x0, y0, x1, y1)
    string_courses(mb, path, [ze - 0.2], ctx, big=True)
    # verifier: dressed corner quoin strips and a first-floor band (it was the plainest house, 1.5k tris)
    string_courses(mb, path, [pl + 3.4], ctx)
    if lod == 0:
        corner_pilasters(mb, x0, y0, x1, y1, pl, ze - 0.2, ctx, w=0.6, p=0.06, band='dressed')
    zr = mb.gable_roof(x0, x1, y0, y1, ze, pitch, ov=0.4, ovg=0.3, axis='X', lod=lod, gable_band='ashlar', gable_anchor=pl, info=info)
    tp = math.tan(math.radians(pitch))
    for dxw in (-2.2, 2.2):
        yb = y0 + 1.0
        zb = ze + (yb - y0) * tp
        dormer(mb, dxw, yb, zb - 0.3, 1.5, ctx, pitch)
    chimney(mb, x0 + 0.6, 0.0, ze - 1.0, zr + 0.8, ctx, w=0.9, d=1.1, band='ashlar')
    chimney(mb, x1 - 0.6, 0.0, ze - 1.0, zr + 0.8, ctx, w=0.9, d=1.1, band='ashlar')
    if lod <= 1 and not ctx.holes:
        for xg, s in ((x0, -1), (x1, 1)):
            zw = ze + 0.5
            if s < 0:
                pts = [(xg - 0.03, -0.0 + 0.45, zw), (xg - 0.03, -0.45, zw), (xg - 0.03, -0.45, zw + 1.2), (xg - 0.03, 0.45, zw + 1.2)]
            else:
                pts = [(xg + 0.03, -0.45, zw), (xg + 0.03, 0.45, zw), (xg + 0.03, 0.45, zw + 1.2), (xg + 0.03, -0.45, zw + 1.2)]
            mb.aquad(*pts, 'window_shut', tag='wall')
    info['ridge'] = zr
    return mb, info


def townhouse_d(lod, state, seed=53):
    """Corner block: an L of two wings round a back yard, with a belvedere tower at the corner. 13 x 13 m."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    info = {}
    pl = 0.4
    st3 = [dict(h=3.6, band='ashlar', win=(1.1, 1.8, 1.0), door=(1.4, 2.7), arc=(2.2, 3.0)),
           dict(h=3.3, band='plaster', win=(1.1, 2.0, 0.6)),
           dict(h=3.0, band='plaster', win=(1.0, 1.7, 0.7))]
    ze = pl + sum(s['h'] for s in st3)
    # wing A along the street, wing B down the right side
    ax0, ax1, ay0, ay1 = -6.5, 6.5, -6.5, 0.5
    bx0, bx1, by0, by1 = 0.5, 6.5, 0.5, 6.5
    foot = [(ax0, ay0), (ax1, ay0), (bx1, by1), (bx0, by1), (bx0, ay1), (ax0, ay1)]
    info.update(footprint=foot, eave=ze, base=pl, W=13.0, D=13.0)
    tx0, tx1, ty0, ty1 = 2.5, 6.5, -6.5, -2.5     # tower (front right corner)
    zt = ze + 3.4
    if lod == 3:
        hull_block(mb, ax0, ay0, ax1, ay1, ze, 'hip', 32)
        hull_block(mb, bx0, by0 - 0.01, bx1, by1, ze, 'hip', 32)
        hull_block(mb, tx0, ty0, tx1, ty1, zt, 'hip', 35)
        hull_windows(mb, ax0, ay0, ax1, ay1, pl, st3, (4, 2))
        info['ridge'] = zt + 2.0
        return mb, info
    plinth(mb, ax0, ay0, ax1, ay1, pl, ctx=ctx)
    plinth(mb, bx0, by0 - 0.12, bx1, by1, pl, ctx=ctx)
    fa = win_kinds(seed, lambda k, i: ('arcade' if i in (0, 1) else ('door' if i == 2 else 'window')) if k == 0 else 'window', 0.2)
    block_walls(mb, ax0, ay0, ax1, ay1, pl, st3, ctx,
                {'S': (5, fa), 'E': (2, win_kinds(seed + 1, None, 0.4)), 'N': (5, win_kinds(seed + 2, lambda k, i: 'door' if (k == 0 and i == 1) else 'window', 0.5), (0.0, 6.6)),
                 'W': (2, win_kinds(seed + 3, None, 0.5))}, margin=0.7)
    block_walls(mb, bx0, by0, bx1, by1, pl, st3, ctx,
                {'S': None, 'E': (2, win_kinds(seed + 4, None, 0.4)), 'N': (2, win_kinds(seed + 5, None, 0.4)),
                 'W': (2, win_kinds(seed + 6, lambda k, i: 'door' if (k == 0 and i == 0) else 'window', 0.5))}, margin=0.7)
    string_courses(mb, rect_path(ax0, ay0, ax1, ay1), [pl + 3.6], ctx)
    string_courses(mb, rect_path(ax0, ay0, ax1, ay1), [ze - 0.22], ctx, big=True)
    string_courses(mb, rect_path(bx0, by0, bx1, by1), [ze - 0.22], ctx, big=True)
    corner_pilasters(mb, ax0, ay0, ax1, ay1, pl, ze, ctx, corners=(0, 3))
    corner_pilasters(mb, bx0, by0, bx1, by1, pl, ze, ctx, corners=(2, 3))
    mb.hip_roof(ax0, ax1, ay0, ay1, ze, 32, ov=0.45, lod=lod, info=info)
    mb.hip_roof(bx0, bx1, by0 - 0.6, by1, ze, 32, ov=0.45, lod=lod, info=info)
    # belvedere: the corner rises one storey with an open loggia under a pyramid roof
    tst = [dict(h=zt - ze, band='plaster', win=(1.2, 1.9, 0.5))]
    tk = win_kinds(seed + 7, lambda k, i: 'arcade', 0.0)
    if not ctx.holes:
        facade(mb, (tx0, ty0), (tx1, ty0), ze, tst, ctx, 2, tk, margin=0.4)
        facade(mb, (tx1, ty0), (tx1, ty1), ze, tst, ctx, 2, tk, margin=0.4)
        facade(mb, (tx1, ty1), (tx0, ty1), ze, tst, ctx, 2, tk, margin=0.4)
        facade(mb, (tx0, ty1), (tx0, ty0), ze, tst, ctx, 2, tk, margin=0.4)
        mb.hip_roof(tx0, tx1, ty0, ty1, zt, 35, ov=0.5, lod=lod, tag='roof', info=info)
        string_courses(mb, rect_path(tx0, ty0, tx1, ty1), [zt - 0.15], ctx, big=True)
    chimney(mb, -4.0, -1.5, ze - 1.0, ze + 3.6, ctx)
    chimney(mb, 4.6, 4.0, ze - 1.0, ze + 3.2, ctx)
    info['ridge'] = zt + 2.0 * math.tan(math.radians(35)) + 0.5
    return mb, info


# ----------------------------------------------------------------------------------------------- warehouses
def hoist(mb, base, out_dir, z_beam, z_load, ctx, length_=1.8, crate=True):
    """Hoist beam projecting from a wall with a pulley, rope and a hanging bale (lod 0-1)."""
    if ctx.lod >= 2 or ctx.holes:
        return
    tip = (base[0] + out_dir[0] * length_, base[1] + out_dir[1] * length_, z_beam)
    mb.beam((base[0], base[1], z_beam), tip, 0.25, 0.3, 'beam', tag='upper')
    if ctx.lod == 0:
        mb.cyl((tip[0], tip[1], 0), 0.18, 0.18, z_beam - 0.5, z_beam - 0.15, 6, None, cell='iron', tag='upper')
    zr = z_load + 0.9
    mb.cyl((tip[0], tip[1], 0), 0.03, 0.03, zr, z_beam - 0.5, 4, None, cell='rope', cap_top=False, tag='upper')
    if crate:
        mb.box((tip[0], tip[1], zr - 0.45), 0.45, 0.45, 0.45, 'planks', mode='fit', tag='upper', bottom=True)


def warehouse_a(lod, state, seed=71):
    """Long quayside warehouse: 2 storeys of rusticated stone, three loading doors, hoist dormer. 24 x 12 m."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    info = {}
    W, D = 24.0, 12.0
    x0, x1, y0, y1 = -W / 2, W / 2, -D / 2, D / 2
    pl = 0.35
    storeys = [dict(h=4.6, band='quay', win=(0.9, 1.0, 2.6), wide=(2.8, 3.5)),
               dict(h=3.6, band='ashlar', win=(0.9, 1.2, 1.1))]
    ze = pl + sum(s['h'] for s in storeys)
    pitch = 28
    info.update(footprint=rect_path(x0, y0, x1, y1), eave=ze, base=pl, W=W, D=D)
    if lod == 3:
        hull_block(mb, x0, y0, x1, y1, ze, 'gable', pitch, band='quay', axis='X')
        hull_windows(mb, x0, y0, x1, y1, pl, [dict(h=4.6, hull_skip=True), storeys[1]], (5, 2))
        info['ridge'] = ze + D / 2 * math.tan(math.radians(pitch))
        return mb, info
    plinth(mb, x0, y0, x1, y1, pl, ctx=ctx)
    front = win_kinds(seed, lambda k, i: ('door_wide' if i % 2 == 0 else 'window_shut') if k == 0 else ('door_wide' if i == 2 else 'window_shut'), 0.6)
    back = win_kinds(seed + 2, lambda k, i: ('door_wide' if i == 2 else 'window_shut') if k == 0 else 'window_shut', 0.7)
    gab = win_kinds(seed + 1, lambda k, i: 'window_shut', 0.7)
    st_front = [dict(storeys[0]), dict(storeys[1], wide=(1.8, 2.4))]
    block_walls(mb, x0, y0, x1, y1, pl, st_front, ctx, {'S': (5, front)}, margin=1.0)
    block_walls(mb, x0, y0, x1, y1, pl, storeys, ctx, {'E': (2, gab), 'N': (5, back), 'W': (2, gab)}, margin=1.0)
    path = rect_path(x0, y0, x1, y1)
    string_courses(mb, path, [pl + 4.6], ctx)
    string_courses(mb, path, [ze - 0.2], ctx, big=True)
    # buttress pilasters between the bays on the long walls
    if lod <= 1:
        bw = (W - 2.0) / 5
        for i in range(6):
            x = x0 + 1.0 + i * bw
            for yy, sy in ((y0, -1), (y1, 1)):
                mb.box((x, yy + sy * 0.12, (pl + ze) / 2), 0.3, 0.14, (ze - pl) / 2, 'ashlar', top=False, anchor=pl, tag='wall',
                       sides=(sy < 0, True, sy > 0, True))
    zr = mb.gable_roof(x0, x1, y0, y1, ze, pitch, ov=0.5, ovg=0.35, axis='X', lod=lod, gable_band='ashlar', gable_anchor=pl, info=info)
    if lod <= 1 and not ctx.holes:
        for xg, s in ((x0, -1), (x1, 1)):
            zw = ze + 0.6
            yy = 0.7
            if s < 0:
                pts = [(xg - 0.03, yy, zw), (xg - 0.03, -yy, zw), (xg - 0.03, -yy, zw + 1.4), (xg - 0.03, yy, zw + 1.4)]
            else:
                pts = [(xg + 0.03, -yy, zw), (xg + 0.03, yy, zw), (xg + 0.03, yy, zw + 1.4), (xg + 0.03, -yy, zw + 1.4)]
            mb.aquad(*pts, 'window_arch', tag='wall')
    # hoist dormer in the middle of the front slope
    if lod <= 1:
        tp = math.tan(math.radians(pitch))
        dw = 3.0
        x0d, x1d = -dw / 2, dw / 2
        yd0, yd1 = y0, y0 + 3.4
        zd = ze + 2.4
        mb.poly([(x0d, yd0, ze - 0.2), (x1d, yd0, ze - 0.2), (x1d, yd0, zd), (x0d, yd0, zd)], 'ashlar', 'metric', anchor=pl, tag='roof')
        if not ctx.holes:
            mb.aquad((-0.9, yd0 - 0.03, ze), (0.9, yd0 - 0.03, ze), (0.9, yd0 - 0.03, zd - 0.2), (-0.9, yd0 - 0.03, zd - 0.2), 'door_wide', tag='roof')
        mb.poly([(x1d, yd0, ze - 0.2), (x1d, yd1, ze - 0.2 + 0.0), (x1d, yd1, zd), (x1d, yd0, zd)], 'ashlar', 'metric', anchor=pl, tag='roof')
        mb.poly([(x0d, yd1, ze - 0.2), (x0d, yd0, ze - 0.2), (x0d, yd0, zd), (x0d, yd1, zd)], 'ashlar', 'metric', anchor=pl, tag='roof')
        zdr = mb.gable_roof(x0d, x1d, yd0, yd1 + 1.5, zd, 35, ov=0.2, ovg=0.3, axis='Y', lod=lod, gable_walls=False, ridge_cap=False, tag='roof')
        mb.poly([(x0d, yd0, zd), (x1d, yd0, zd), (0.0, yd0, zdr - 0.1)], 'ashlar', 'fit', tag='roof')
        hoist(mb, (0.0, yd0), (0.0, -1.0), zd + 0.4, pl + 1.0, ctx, length_=2.0)
    chimney(mb, -7.0, 2.0, ze - 1, zr + 0.6, ctx, band='ashlar')
    info['ridge'] = zr
    return mb, info


def warehouse_b(lod, state, seed=83):
    """Tall gable-fronted stack warehouse: 3 storeys + loft, stacked loading doors, hoist at the apex. 11 x 16 m."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    info = {}
    W, D = 11.0, 16.0
    x0, x1, y0, y1 = -W / 2, W / 2, -D / 2, D / 2
    pl = 0.35
    storeys = [dict(h=4.2, band='quay', win=(1.0, 1.4, 1.6), wide=(2.6, 3.3), door=(1.3, 2.6)),
               dict(h=3.4, band='plaster', win=(1.0, 1.5, 0.8), wide=(1.9, 2.6)),
               dict(h=3.2, band='plaster', win=(1.0, 1.4, 0.8), wide=(1.9, 2.5))]
    ze = pl + sum(s['h'] for s in storeys)
    pitch = 48
    info.update(footprint=rect_path(x0, y0, x1, y1), eave=ze, base=pl, W=W, D=D)
    if lod == 3:
        hull_block(mb, x0, y0, x1, y1, ze, 'gable', pitch, axis='Y')
        hull_windows(mb, x0, y0, x1, y1, pl, storeys, (3, 4))
        info['ridge'] = ze + W / 2 * math.tan(math.radians(pitch))
        return mb, info
    plinth(mb, x0, y0, x1, y1, pl, ctx=ctx)
    front = win_kinds(seed, lambda k, i: 'door_wide' if i == 1 else ('door' if (k == 0 and i == 0) else 'window_shut'), 0.6)
    side = win_kinds(seed + 1, lambda k, i: 'window_shut', 0.6)
    back = win_kinds(seed + 2, lambda k, i: 'door_wide' if (k == 0 and i == 1) else 'window_shut', 0.6)
    block_walls(mb, x0, y0, x1, y1, pl, storeys, ctx, {'S': (3, front), 'E': (4, side), 'N': (3, back), 'W': (4, side)}, margin=0.9)
    path = rect_path(x0, y0, x1, y1)
    string_courses(mb, path, [pl + 4.2], ctx)
    string_courses(mb, path, [ze - 0.2], ctx, big=True)
    corner_pilasters(mb, x0, y0, x1, y1, pl + 4.2, ze, ctx)
    zr = mb.gable_roof(x0, x1, y0, y1, ze, pitch, ov=0.4, ovg=0.5, axis='Y', lod=lod, gable_anchor=pl, info=info)
    if lod <= 1 and not ctx.holes:
        zw = ze + 0.4
        mb.aquad((-0.95, y0 - 0.03, zw), (0.95, y0 - 0.03, zw), (0.95, y0 - 0.03, zw + 2.3), (-0.95, y0 - 0.03, zw + 2.3), 'door_wide', tag='wall')
    hoist(mb, (0.0, y0 - 0.45), (0.0, -1.0), zr - 1.6, pl + 0.9, ctx, length_=1.6)
    if lod <= 1:
        # small apex hood over the hoist beam
        mb.gable_roof(-0.8, 0.8, y0 - 2.2, y0 + 0.2, zr - 1.0, 35, ov=0.1, ovg=0.1, axis='Y', lod=lod, gable_walls=False, ridge_cap=False, tag='roof')
    info['ridge'] = zr
    return mb, info


# ----------------------------------------------------------------------------------------------- docks
def bollard(mb, x, y, ctx, h=0.75, r=0.22, z=0.0):
    sides = 8 if ctx.lod == 0 else 6
    mb.cyl((x, y, 0), r, r * 0.8, z, z + h, sides, None, cell='iron', cap_top=False, tag='prop')
    mb.cyl((x, y, 0), r * 1.25, r * 1.25, z + h, z + h + 0.12, sides, None, cell='iron', cap_top=True, tag='prop')


def _quay_rng(seed):
    return random.Random(seed)


def quay_block(mb, x0, x1, y0, y1, ctx, depth=-5.5, water_faces=('S',), coping=True, top_band='ashlar'):
    """Solid quay block, top at 0, faces to the water in `water_faces`."""
    mb.box(((x0 + x1) / 2, (y0 + y1) / 2, depth / 2), (x1 - x0) / 2, (y1 - y0) / 2, -depth / 2, 'quay', top=top_band,
           bottom=True, anchor=0.0, tag='quay')


def quay_straight(lod, state, seed=101):
    """Stone quay wall segment, 20 m of frontage, 10 m deep; water on the -Y side."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    rng = _quay_rng(seed)
    x0, x1, y0, y1 = -10.0, 10.0, -5.0, 5.0
    info = dict(footprint=rect_path(x0, y0, x1, y1), eave=0.0, base=0.0, W=20.0, D=10.0, ridge=0.25, water_z=WATER_Z)
    ruin = state == 'ruin'
    dmg = state == 'damaged'
    if lod == 3:
        mb.box((0, 0, -2.75), 10, 5, 2.75, 'quay', top='ashlar', mode='fit', tag='quay')
        return mb, info
    quay_block(mb, x0, x1, y0, y1, ctx)
    _coping(mb, x0, x1, y0, ctx, rng, dmg or ruin, ruin)
    if not ruin:
        for x in (-6.5, 0.0, 6.5):
            if dmg and x == 0.0:
                continue
            bollard(mb, x, y0 + 0.9, ctx)
        _fenders(mb, x0, x1, y0, ctx, rng, broken=dmg)
        _ladder(mb, 3.2, y0, ctx)
    _quay_damage(mb, x0, x1, y0, ctx, rng, dmg, ruin)
    return mb, info


def _coping(mb, x0, x1, y0, ctx, rng, gaps, ruin):
    if ctx.lod >= 2:
        mb.box(((x0 + x1) / 2, y0 + 0.35, 0.1), (x1 - x0) / 2, 0.38, 0.1, 'dressed', mode='fit', tag='coping')
        return
    n = 10
    L = (x1 - x0) / n
    for i in range(n):
        if gaps and rng.random() < (0.35 if ruin else 0.15):
            continue
        xa = x0 + i * L
        mb.box((xa + L / 2, y0 + 0.35, 0.1), L / 2 - 0.02, 0.38, 0.1, 'dressed', mode='fit', tag='coping', sides=(True, i == n - 1, False, i == 0) if not gaps else (True, True, False, True))


def _fenders(mb, x0, x1, y0, ctx, rng, broken=False):
    if ctx.lod >= 2:
        return
    for x in [x0 + 2.0 + 4.0 * i for i in range(int((x1 - x0) / 4.0))]:
        if broken and rng.random() < 0.3:
            continue
        zb = WATER_Z - 1.0
        mb.box((x, y0 - 0.14, (zb + 0.05) / 2), 0.14, 0.14, (0.05 - zb) / 2, 'beam', mode='fit', tag='prop', bottom=True)


def _ladder(mb, x, y0, ctx):
    if ctx.lod >= 1:
        return
    zb = WATER_Z - 0.8
    for xx in (x - 0.25, x + 0.25):
        mb.box((xx, y0 - 0.08, (zb + 0.2) / 2), 0.025, 0.04, (0.2 - zb) / 2, 'beam', mode='fit', tag='prop')
    z = zb + 0.3
    while z < 0.0:
        mb.cyl_along((x - 0.25, y0 - 0.1, z), (x + 0.25, y0 - 0.1, z), 0.02, 3, None, cell='iron')
        z += 0.3


def rubble_mound(mb, cx, cy, z, r, h, rng, lod, band='rubble', blocks=4, beams=1, charred=False):
    """Low irregular heap of rubble with a few loose blocks and timbers."""
    if lod >= 3:
        return
    sides = 7 if lod == 0 else 5
    ring1 = []
    ring2 = []
    for j in range(sides):
        a = 2 * math.pi * j / sides + rng.uniform(-0.2, 0.2)
        rr = r * rng.uniform(0.75, 1.15)
        ring1.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr, z - 0.15))
        rr2 = rr * rng.uniform(0.35, 0.6)
        ring2.append((cx + math.cos(a) * rr2, cy + math.sin(a) * rr2, z + h * rng.uniform(0.6, 0.9)))
    apex = (cx + rng.uniform(-0.2, 0.2) * r, cy + rng.uniform(-0.2, 0.2) * r, z + h)
    for j in range(sides):
        k = (j + 1) % sides
        mb.poly([ring1[j], ring1[k], ring2[k], ring2[j]], band, 'fit', tag='debris')
        mb.poly([ring2[j], ring2[k], apex], band, 'fit', tag='debris')
    if lod >= 2:
        return
    for _ in range(blocks):
        a = rng.uniform(0, 2 * math.pi)
        rr = r * rng.uniform(0.6, 1.3)
        s = rng.uniform(0.25, 0.45)
        mb.box((cx + math.cos(a) * rr, cy + math.sin(a) * rr, z + s * 0.6), s * 1.4, s, s * 0.7, 'ashlar', rot=rng.uniform(0, 3), mode='fit', tag='debris')
    for _ in range(beams):
        a = rng.uniform(0, 2 * math.pi)
        p0 = (cx + math.cos(a) * r * 0.2, cy + math.sin(a) * r * 0.2, z + h * 0.8)
        p1 = (cx + math.cos(a + 0.4) * r * 1.4, cy + math.sin(a + 0.4) * r * 1.4, z + 0.05)
        if charred:
            mb.beam(p0, p1, 0.22, 0.22, cell='charred', tag='debris')
        else:
            mb.beam(p0, p1, 0.22, 0.22, 'beam', tag='debris')


def _quay_damage(mb, x0, x1, y0, ctx, rng, dmg, ruin):
    if not (dmg or ruin):
        return
    # fallen coping and blocks along the edge, a breach for the ruin
    for i in range(2 if dmg else 4):
        x = rng.uniform(x0 + 2, x1 - 2)
        rubble_mound(mb, x, y0 + rng.uniform(1.2, 3.0), 0.0, rng.uniform(0.9, 1.5), rng.uniform(0.35, 0.6), rng, ctx.lod, band='quay', blocks=3, beams=0)
    if ruin:
        # slumped blocks in the water at the foot of the wall
        for i in range(5):
            x = rng.uniform(x0 + 1, x1 - 1)
            rubble_mound(mb, x, y0 - rng.uniform(0.6, 1.6), WATER_Z - 0.6, rng.uniform(1.0, 1.6), 0.9, rng, ctx.lod, band='quay', blocks=2, beams=0)


def quay_corner(lod, state, seed=113):
    """Outer corner of the quay: water on -Y and +X, a rounded nose (r 3 m). 10 x 10 m."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    rng = _quay_rng(seed)
    x0, x1, y0, y1 = -5.0, 5.0, -5.0, 5.0
    info = dict(footprint=rect_path(x0, y0, x1, y1), eave=0.0, base=0.0, W=10.0, D=10.0, ridge=0.25, water_z=WATER_Z)
    R = 3.0
    segs = 6 if lod == 0 else (4 if lod == 1 else 2)
    foot = [(x0, y0)]
    for i in range(segs + 1):
        a = -math.pi / 2 + (math.pi / 2) * i / segs
        foot.append((x1 - R + R * math.cos(a), y0 + R + R * math.sin(a)))
    foot += [(x1, y1), (x0, y1)]
    depth = -5.5
    if lod == 3:
        mb.prism(foot, -5.5, 0.0, 'quay', top_band='ashlar', mode='fit', tag='quay')
        return mb, info
    mb.prism(foot, depth, 0.0, 'quay', top_band='ashlar', anchor=0.0, tag='quay', bottom=True)
    ruin = state == 'ruin'
    dmg = state == 'damaged'
    # coping along the water edges (open path, inset 0.35 m)
    edge = foot[:-1]
    if lod <= 1:
        prof = [(-0.75, 0.0), (-0.75, 0.2), (0.05, 0.2), (0.05, -0.05), (0.0, -0.05)]
        prof = [(0.0, 0.0), (0.06, 0.0), (0.06, 0.2), (-0.75, 0.2), (-0.75, 0.0)]
        mb.sweep(edge, prof, 'dressed', z=0.0, closed=False, tag='coping', cap_ends=True)
    else:
        mb.sweep(edge, [(0.0, 0.0), (0.0, 0.2), (-0.7, 0.2)], 'dressed', z=0.0, closed=False, tag='coping')
    if not ruin:
        bollard(mb, x1 - 1.6, y0 + 1.6, ctx, r=0.26)
        bollard(mb, x0 + 1.5, y0 + 0.9, ctx)
        bollard(mb, x1 - 0.9, y1 - 1.5, ctx)
        _fenders(mb, x0, x1 - R, y0, ctx, rng, broken=dmg)
    if dmg or ruin:
        for i in range(2 if dmg else 4):
            rubble_mound(mb, rng.uniform(-3, 3), rng.uniform(-3, 3), 0.0, rng.uniform(0.9, 1.4), 0.5, rng, lod, band='quay', blocks=3, beams=0)
        if ruin:
            for i in range(4):
                a = rng.uniform(-math.pi / 2, 0)
                rubble_mound(mb, x1 - R + (R + 1.2) * math.cos(a), y0 + R + (R + 1.2) * math.sin(a), WATER_Z - 0.6, 1.3, 0.9, rng, lod, band='quay', blocks=2, beams=0)
    return mb, info


def quay_stairs(lod, state, seed=127):
    """Straight quay segment (20 m) with a flight of water stairs along its face."""
    mb, info = quay_straight(lod, state, seed)
    ctx = Ctx(lod, state, seed)
    rng = _quay_rng(seed + 9)
    y0 = -5.0
    info['footprint'] = rect_path(-10.0, -7.2, 10.0, 5.0)
    info['D'] = 12.2
    info['name_note'] = 'stairs project 2.2 m in front of the quay face (to y = -7.2)'
    if lod == 3:
        mb.box((1.0, -6.1, -2.75), 5.5, 1.1, 2.75, 'quay', top='ashlar', mode='fit', tag='quay')
        return mb, info
    ruin = state == 'ruin'
    steps = 9
    rise = (0.0 - (WATER_Z + 0.25)) / steps
    run = 0.55
    xs = -4.0
    yy0, yy1 = -7.2, -5.0
    for i in range(steps):
        zt = -rise * (i + 1) + 0.0
        xa = xs + i * run
        if ruin and i in (3, 4):
            continue
        mb.box((xa + run / 2, (yy0 + yy1) / 2, (zt - 5.5) / 2), run / 2, (yy1 - yy0) / 2, (zt + 5.5) / 2, 'quay', top='dressed',
               sides=(True, i == steps - 1, False, i == 0), anchor=0.0, tag='quay')
    # landing
    xl = xs + steps * run
    zl = -rise * steps
    mb.box((xl + 1.0, (yy0 + yy1) / 2, (zl - 5.5) / 2), 1.0, (yy1 - yy0) / 2, (zl + 5.5) / 2, 'quay', top='dressed', sides=(True, True, False, False), anchor=0.0, tag='quay')
    # parapet wall on the water side of the flight
    if lod <= 1 and not ruin:
        z = 0.0
        for i in range(steps):
            xa = xs + i * run
            zt = -rise * i
            mb.box((xa + run / 2, yy0 + 0.15, zt + 0.35), run / 2, 0.15, 0.35, 'dressed', mode='fit', tag='coping',
                   sides=(True, False, True, i == 0))
    return mb, info


def pier(lod, state, seed=131):
    """Timber pier on piles, 5 x 24 m, deck top at Z = 0, running out over the water toward -Y."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    rng = random.Random(seed)
    x0, x1, y0, y1 = -2.5, 2.5, -12.0, 12.0
    info = dict(footprint=rect_path(x0, y0, x1, y1), eave=0.0, base=0.0, W=5.0, D=24.0, ridge=1.0, water_z=WATER_Z)
    ruin = state == 'ruin'
    dmg = state == 'damaged'
    th = 0.22
    if lod == 3:
        mb.box((0, 0, -th / 2), 2.5, 12, th / 2, 'planks', mode='fit', tag='deck', bottom=True)
        for y in (-10.5, 0.0, 10.5):
            for x in (-2.2, 2.2):
                mb.box((x, y, (WATER_Z - 2.0 - th) / 2), 0.15, 0.15, (-th - WATER_Z + 2.0) / 2, 'beam', mode='fit', tag='pile', top=False)
        return mb, info
    # deck in panels between bents (lets the damaged / ruined states drop panels)
    bents = [y0 + 0.5 + i * 2.875 for i in range(9)]
    for i in range(len(bents) - 1):
        ya, yb = bents[i], bents[i + 1]
        if ruin and (i < 5 or (i == 5 and rng.random() < 0.5)):
            continue
        if dmg and i in (2, 3) and rng.random() < 0.9:
            if i == 2:
                # broken panel: half the boards gone
                mb.box((x0 / 2 - 0.3, (ya + yb) / 2, -th / 2), 1.0, (yb - ya) / 2, th / 2, 'planks', mode='fit', tag='deck', bottom=True)
            continue
        mb.poly([(x0, ya, 0.0), (x1, ya, 0.0), (x1, yb, 0.0), (x0, yb, 0.0)], 'planks', 'metric', frame=((0.0, 1.0, 0.0), (-1.0, 0.0, 0.0)), tag='deck')
        if lod <= 2:
            mb.poly([(x0, yb, -th), (x1, yb, -th), (x1, ya, -th), (x0, ya, -th)], 'planks', 'fit', tag='deck')
            mb.poly([(x0, ya, -th), (x0, yb, -th), (x0, yb, 0), (x0, ya, 0)], 'beam', 'fit', frame=((0.0, -1.0, 0.0), (0.0, 0.0, 1.0)), tag='deck')
            mb.poly([(x1, yb, -th), (x1, ya, -th), (x1, ya, 0), (x1, yb, 0)], 'beam', 'fit', frame=((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), tag='deck')
        if i == 0:
            mb.poly([(x1, ya, -th), (x0, ya, -th), (x0, ya, 0), (x1, ya, 0)], 'beam', 'fit', tag='deck')
        if i == len(bents) - 2:
            mb.poly([(x0, yb, -th), (x1, yb, -th), (x1, yb, 0), (x0, yb, 0)], 'beam', 'fit', tag='deck')
    sides = 6 if lod == 0 else (5 if lod == 1 else 4)
    zb = WATER_Z - 2.5
    for k, y in enumerate(bents):
        zt = -th
        if ruin:
            zt = WATER_Z + rng.uniform(-0.2, 1.4) if k < 6 else -th
        for x in (x0 + 0.25, x1 - 0.25):
            ztop = zt if not (dmg and k == 3 and x < 0) else WATER_Z + 0.6
            mb.cyl((x, y, 0), 0.2, 0.18, zb, ztop, sides, 'beam', mode='axis', cap_top=ztop < -th - 0.01, tag='pile')
        if lod <= 1 and not (ruin and k < 6):
            # cap beam (cross head) and diagonal bracing
            mb.beam((x0 - 0.1, y, -th - 0.15), (x1 + 0.1, y, -th - 0.15), 0.3, 0.3, 'beam', tag='pile')
            if lod == 0:
                mb.beam((x0 + 0.3, y, WATER_Z + 0.2), (x1 - 0.3, y, -th - 0.35), 0.16, 0.12, 'beam', tag='pile', caps=False)
    if lod <= 1 and not ruin:
        # stringers under the deck
        for x in (x0 + 0.25, 0.0, x1 - 0.25):
            mb.beam((x, y0 + 0.3, -th - 0.15), (x, y1 - 0.3, -th - 0.15), 0.22, 0.3, 'beam', tag='pile', caps=False)
        # mooring posts and a few cargo pieces
        for y in (y0 + 1.0, -4.0, 4.0):
            for x in (x0 + 0.2, x1 - 0.2):
                mb.cyl((x, y, 0), 0.16, 0.15, 0.0, 0.7, sides, 'beam', mode='axis', tag='prop')
        if lod == 0:
            rr = random.Random(seed + 5)
            for i in range(5):
                x = rr.uniform(-1.4, 1.4)
                y = rr.uniform(2.0, 10.0)
                if rr.random() < 0.5:
                    mb.box((x, y, 0.35), 0.35, 0.35, 0.35, 'planks', mode='fit', rot=rr.uniform(0, 1), tag='prop', top='planks')
                else:
                    mb.cyl((x, y, 0), 0.32, 0.32, 0.0, 0.8, 8, None, cell='crate', tag='prop')
    if dmg or ruin:
        for i in range(2):
            p0 = (rng.uniform(-2, 2), rng.uniform(-9, -3), WATER_Z + 0.05)
            mb.beam(p0, (p0[0] + rng.uniform(-1, 1), p0[1] + 2.6, WATER_Z + 0.25), 0.25, 0.08, 'planks', tag='debris')
    return mb, info


# ----------------------------------------------------------------------------------------------- crane
def port_crane(lod, state, seed=151):
    """Treadwheel harbour crane: stone base, braced timber tower, slewing cab with a jib over the water (-Y)."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    rng = random.Random(seed)
    info = dict(footprint=rect_path(-3.5, -3.5, 3.5, 3.5), base=1.4, W=7.0, D=7.0)
    zb = 1.4
    zc = 10.0         # cab floor
    zcr = 13.4        # cab eaves
    info['eave'] = zcr
    info['jib_tip'] = (0.0, -11.0, zc + 4.2)
    if lod == 3:
        mb.box((0, 0, (FOOT + zb) / 2), 3.2, 3.2, (zb - FOOT) / 2, 'quay', top='dressed', mode='fit', tag='base')
        mb.box((0, 0, (zb + zc) / 2), 2.0, 2.0, (zc - zb) / 2, 'beam', top=False, mode='fit', tag='wall')
        mb.box((0, 0, (zc + zcr) / 2), 2.6, 2.2, (zcr - zc) / 2, 'planks', top=False, mode='fit', tag='wall')
        mb.gable_roof(-2.6, 2.6, -2.2, 2.2, zcr, 35, ov=0.3, ovg=0.3, lod=3, gable_band='planks')
        mb.beam((0, -2.2, zc + 0.8), (0, -11.0, zc + 4.0), 0.4, 0.5, 'beam', tag='jib')
        info['ridge'] = zcr + 2.2 * math.tan(math.radians(35))
        return mb, info
    # stone base with a stair
    mb.box((0, 0, (FOOT + zb) / 2), 3.2, 3.2, (zb - FOOT) / 2, 'quay', top='dressed', bottom=True, anchor=0.0, tag='base')
    if lod <= 1:
        mb.sweep(rect_path(-3.2, -3.2, 3.2, 3.2), [(0.0, 0.0), (0.1, 0.0), (0.1, 0.18), (0.0, 0.18)] if lod else
                 [(0.0, -0.05), (0.12, 0.0), (0.12, 0.2), (0.0, 0.22)], 'dressed', z=zb - 0.2, tag='base')
    # tower posts, girts and braces
    P = 2.0
    posts = [(-P, -P), (P, -P), (P, P), (-P, P)]
    for (x, y) in posts:
        mb.box((x, y, (zb + zc) / 2), 0.22, 0.22, (zc - zb) / 2, 'beam', top=False, mode='fit', tag='wall')
    if lod <= 1:
        for z in (zb + 2.8, zb + 5.6, zc - 0.2):
            for i in range(4):
                a, b = posts[i], posts[(i + 1) % 4]
                mb.beam((a[0], a[1], z), (b[0], b[1], z), 0.2, 0.24, 'beam', tag='wall', caps=False)
        for i in range(4):
            a, b = posts[i], posts[(i + 1) % 4]
            for z0_, z1_ in ((zb + 0.1, zb + 2.8), (zb + 2.8, zb + 5.6), (zb + 5.6, zc - 0.2)):
                mb.beam((a[0], a[1], z0_), (b[0], b[1], z1_), 0.16, 0.16, 'beam', tag='wall', caps=False)
                if lod == 0:
                    mb.beam((b[0], b[1], z0_), (a[0], a[1], z1_), 0.14, 0.14, 'beam', tag='wall', caps=False)
    # slewing cab: plank walls on a beam floor, tiled roof
    cx0, cx1, cy0, cy1 = -2.6, 2.6, -2.2, 2.2
    mb.box((0, 0, zc - 0.15), 2.7, 2.3, 0.18, 'beam', mode='fit', tag='wall', bottom=True)
    cab = [dict(h=zcr - zc, band='planks', win=(0.9, 0.9, 1.4), wide=(1.6, 2.2))]
    if not ctx.holes:
        facade(mb, (cx0, cy0), (cx1, cy0), zc, cab, ctx, 1, lambda k, i: 'door_wide', margin=0.3, shutters=0)
        facade(mb, (cx1, cy0), (cx1, cy1), zc, cab, ctx, 1, lambda k, i: 'window_shut', margin=0.3, shutters=0)
        facade(mb, (cx1, cy1), (cx0, cy1), zc, cab, ctx, 1, lambda k, i: 'none', margin=0.3, shutters=0)
        facade(mb, (cx0, cy1), (cx0, cy0), zc, cab, ctx, 1, lambda k, i: 'window_shut', margin=0.3, shutters=0)
    else:
        for a, b in (((cx0, cy0), (cx1, cy0)), ((cx1, cy0), (cx1, cy1)), ((cx1, cy1), (cx0, cy1)), ((cx0, cy1), (cx0, cy0))):
            facade(mb, a, b, zc, cab, ctx, 1, lambda k, i: 'none', margin=0.3, shutters=0)
    zr = mb.gable_roof(cx0, cx1, cy0, cy1, zcr, 35, ov=0.35, ovg=0.3, axis='X', lod=lod, gable_band='planks', gable_anchor=zc, info=info)
    # treadwheel on the +X side of the cab
    wc = (cx1 + 0.55, 0.0, zc + 1.4)
    Rw = 2.4
    nseg = 20 if lod == 0 else 12
    rim = []
    for i in range(nseg):
        a = 2 * math.pi * i / nseg
        rim.append((wc[0], wc[1] + Rw * math.cos(a), wc[2] + Rw * math.sin(a)))
    for i in range(nseg):
        a, b = rim[i], rim[(i + 1) % nseg]
        mb.beam(add(a, (0.45, 0, 0)), add(b, (0.45, 0, 0)), 0.16, 0.2, 'beam', up=(1, 0, 0), tag='wheel', caps=False)
        mb.beam(add(a, (-0.45, 0, 0)), add(b, (-0.45, 0, 0)), 0.16, 0.2, 'beam', up=(1, 0, 0), tag='wheel', caps=False)
        if lod == 0 and i % 2 == 0:
            mb.beam(add(a, (-0.5, 0, 0)), add(a, (0.5, 0, 0)), 0.1, 0.1, 'planks', tag='wheel', caps=False)
    for i in range(0, nseg, nseg // 4 if lod else nseg // 8):
        mb.beam(wc, rim[i], 0.12, 0.12, 'beam', tag='wheel', caps=False)
    mb.beam(add(wc, (-0.7, 0, 0)), add(wc, (0.7, 0, 0)), 0.22, 0.22, 'beam', tag='wheel')
    # jib with a strut, rope, pulley and a hook block with a hanging bale
    j0 = (0.0, cy0 + 0.2, zc + 0.9)
    jt = (0.0, -11.0, zc + 4.2)
    mb.beam(j0, jt, 0.38, 0.48, 'beam', tag='jib')
    mb.beam((0.0, cy0 + 0.3, zc - 1.2), lerp_(j0, jt, 0.55), 0.26, 0.26, 'beam', tag='jib', caps=False)
    if lod <= 1:
        mb.beam((0.0, cy0, zr - 0.3), lerp_(j0, jt, 0.85), 0.05, 0.05, None, cell='rope', tag='jib', caps=False) if False else \
            mb.cyl_along((0.0, cy0, zr - 0.3), lerp_(j0, jt, 0.85), 0.04, 3, None, cell='rope')
        mb.cyl_along((0.0, jt[1] + 0.25, jt[2] - 0.35), (0.0, jt[1] + 0.25, 3.2), 0.035, 3 if lod else 4, None, cell='rope')
        mb.box((0.0, jt[1] + 0.25, jt[2] - 0.2), 0.12, 0.3, 0.3, 'dressed', mode='fit', tag='jib')
        if state != 'ruin':
            mb.box((0.0, jt[1] + 0.25, 2.6), 0.6, 0.6, 0.6, 'planks', mode='fit', tag='jib', bottom=True)
    info['ridge'] = zr
    info['jib_tip'] = jt
    return mb, info


def lerp_(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


# ----------------------------------------------------------------------------------------------- lighthouse
def lighthouse(lod, state, seed=171):
    """Harbour lighthouse: square stone platform, tapering octagonal ashlar tower, gallery, glazed lantern, lead cap."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    info = dict(footprint=rect_path(-5.0, -5.0, 5.0, 5.0), base=2.0, W=10.0, D=10.0)
    zb = 2.0
    zt = 23.0
    r0, r1 = 3.4, 2.5
    sides = 8
    a0 = math.pi / 8
    info['eave'] = zt
    if lod == 3:
        mb.box((0, 0, (FOOT + zb) / 2), 5, 5, (zb - FOOT) / 2, 'quay', top='ashlar', mode='fit', tag='base')
        mb.cyl((0, 0, 0), r0, r1, zb, zt, 8, 'ashlar', mode='axis', a0=a0, cap_top=False, tag='wall')
        mb.cyl((0, 0, 0), 3.2, 3.2, zt, zt + 0.4, 8, 'dressed', mode='axis', a0=a0, tag='wall')
        mb.cyl((0, 0, 0), 1.7, 1.7, zt + 0.4, zt + 3.4, 8, None, cell='lantern', a0=a0, cap_top=False, tag='roof')
        mb.cyl((0, 0, 0), 2.0, 0.0, zt + 3.4, zt + 5.2, 8, 'lead', mode='axis', a0=a0, cap_top=False, tag='roof')
        info['ridge'] = zt + 5.6
        return mb, info
    # platform with a battered skirt and steps on the front
    mb.box((0, 0, (FOOT + zb) / 2), 5.0, 5.0, (zb - FOOT) / 2, 'quay', top='ashlar', bottom=True, anchor=0.0, tag='base')
    if lod <= 1:
        mb.sweep(rect_path(-5, -5, 5, 5), [(0.0, -0.1), (0.15, 0.0), (0.15, 0.25), (0.0, 0.3)], 'dressed', z=zb - 0.3, tag='base')  # verifier: top flush with the platform (was 5 cm proud and open)
        for i in range(4):
            zt_ = zb * (i + 1) / 5
            mb.box((0.0, -5.0 - 0.35 * (4 - i) + 0.175, zt_ / 2 + FOOT / 2), 1.2, 0.175, (zt_ - FOOT) / 2, 'quay', top='dressed', anchor=0.0, tag='base',
                   sides=(True, True, False, True))
    # tower in courses between string courses
    rings = [zb, 9.0, 16.0, zt]
    def rad(z):
        return r0 + (r1 - r0) * (z - zb) / (zt - zb)
    for i in range(len(rings) - 1):
        za, zz = rings[i], rings[i + 1]
        k = max(1, int(round((zz - za) / 2.5)))
        mb.cyl((0, 0, 0), rad(za), rad(zz), za, zz, sides, 'ashlar', a0=a0, cap_top=False,
               rings=[za + (zz - za) * j / k for j in range(k + 1)], tag='wall')
    # string courses
    if lod <= 1:
        for z in rings[1:-1]:
            r = rad(z) / math.cos(math.pi / 8)
            path = [(r * math.cos(a0 + 2 * math.pi * j / 8), r * math.sin(a0 + 2 * math.pi * j / 8)) for j in range(8)]
            mb.sweep(path, [(-0.03, -0.1), (0.12, 0.0), (0.12, 0.16), (-0.03, 0.2)], 'dressed', z=z, tag='wall')  # verifier: tucked into the battered wall
    # windows (small, alternating faces) and the door
    if not ctx.holes:
        for zi, z in enumerate((5.0, 8.0, 11.8, 15.0, 19.0)):
            j = (zi * 3) % 8
            ang = a0 + 2 * math.pi * (j + 0.5) / 8
            _octo_quad(mb, ang, rad(z) * math.cos(math.pi / 8), z, 0.55, 1.25, 'window_arch' if zi % 2 else 'window_shut', rad)
        ang = -math.pi / 2
        _octo_quad(mb, ang, rad(zb) * math.cos(math.pi / 8), zb + 0.02, 1.2, 2.4, 'door', rad)
    # gallery: corbel ring, slab and railing
    rg = 3.3
    ring = [(rg * math.cos(a0 + 2 * math.pi * j / 8), rg * math.sin(a0 + 2 * math.pi * j / 8)) for j in range(8)]
    rt = rad(zt) / math.cos(math.pi / 8)
    tpath = [(rt * math.cos(a0 + 2 * math.pi * j / 8), rt * math.sin(a0 + 2 * math.pi * j / 8)) for j in range(8)]
    if lod <= 1:
        mb.sweep(tpath, [(0.0, -0.9), (0.25, -0.6), (0.55, -0.25), (0.85, 0.0), (0.0, 0.0)] if lod == 0 else
                 [(0.0, -0.8), (0.85, 0.0), (0.0, 0.0)], 'dressed', z=zt, tag='wall')
    mb.prism(ring, zt, zt + 0.35, 'dressed', mode='fit', tag='wall', bottom=lod >= 2)
    zg = zt + 0.35
    if lod <= 1:
        rr = rg - 0.08
        rp = [(rr * math.cos(a0 + 2 * math.pi * j / 8), rr * math.sin(a0 + 2 * math.pi * j / 8)) for j in range(8)]
        for j in range(8):
            a, b = rp[j], rp[(j + 1) % 8]
            mb.cyl_along((a[0], a[1], zg + 1.0), (b[0], b[1], zg + 1.0), 0.04, 3 if lod else 4, None, cell='iron')
            if lod == 0:
                mb.cyl_along((a[0], a[1], zg + 0.5), (b[0], b[1], zg + 0.5), 0.025, 3, None, cell='iron')
            mb.cyl((a[0], a[1], 0), 0.04, 0.04, zg, zg + 1.05, 4, None, cell='iron', cap_top=False, tag='upper')
    # lantern room
    rl = 1.75
    zl0, zl1, zl2 = zg, zg + 0.8, zg + 3.2
    mb.cyl((0, 0, 0), rl, rl, zl0, zl1, 8, 'dressed', a0=a0, cap_top=False, tag='roof')
    mb.cyl((0, 0, 0), rl - 0.05, rl - 0.05, zl1, zl2, 8, None, cell='lantern', a0=a0, cap_top=False, tag='roof')
    if lod <= 1:
        for j in range(8):
            a = a0 + 2 * math.pi * j / 8
            mb.box((rl * math.cos(a), rl * math.sin(a), (zl1 + zl2) / 2), 0.07, 0.07, (zl2 - zl1) / 2, 'beam', rot=a, mode='fit', top=False, tag='roof')
    mb.cyl((0, 0, 0), rl + 0.25, rl + 0.25, zl2, zl2 + 0.25, 8, 'dressed', a0=a0, cap_top=False, tag='roof', mode='axis')
    tip = mb.dome((0, 0, 0), rl + 0.25, zl2 + 0.25, 1.7, 8, 4 if lod == 0 else 3, 'lead', a0=a0, tag='roof', open_r=0.22)
    mb.cyl((0, 0, 0), 0.22, 0.12, tip[1], tip[1] + 0.5, 6, None, cell='gilt', tag='roof')
    if lod <= 1:
        mb.dome((0, 0, 0), 0.3, tip[1] + 0.5, 0.6, 6, 3, 'lead', tag='roof',
                profile=lambda t: (math.cos(t * math.pi - math.pi / 2) if t > 0 else 0.0, (math.sin(t * math.pi - math.pi / 2) + 1) / 2))
        mb.cyl_along((0, 0, tip[1] + 1.0), (0, 0, tip[1] + 2.2), 0.03, 3, None, cell='iron')
        mb.poly([(0.0, 0.0, tip[1] + 1.8), (0.9, 0.0, tip[1] + 1.8), (0.9, 0.0, tip[1] + 2.15), (0.0, 0.0, tip[1] + 2.15)], 'gilt', tag='roof')
        mb.poly([(0.9, 0.0, tip[1] + 1.8), (0.0, 0.0, tip[1] + 1.8), (0.0, 0.0, tip[1] + 2.15), (0.9, 0.0, tip[1] + 2.15)], 'gilt', tag='roof')
    info['ridge'] = tip[1] + 2.2
    info['light'] = (0.0, 0.0, (zl1 + zl2) / 2)
    return mb, info


def dentils(mb, a, b, z, w=0.16, d=0.12, h=0.18, gap=0.34, tag='wall', end=0.3):
    """Row of dentil blocks hung under a cornice along a-b (2D, outside on the right of travel), top at z."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = uy, -ux
    rot = math.atan2(uy, ux)
    k = max(1, int((L - 2 * end) / gap))
    for i in range(k + 1):
        s = end + (L - 2 * end) * i / k
        c = (a[0] + ux * s + nx * d / 2, a[1] + uy * s + ny * d / 2, z - h / 2)
        mb.box(c, w / 2, d / 2, h / 2, 'dressed', rot=rot, mode='fit', top=False, bottom=True, tag=tag,
               sides=(True, True, False, True))


def fluted_shaft(mb, x, y, z0, z1, r0, r1, flutes, tag, cap=False, depth=0.07):
    """Column shaft with `flutes` concave flutes (alternating ridge / flute vertices), texture 'dressed' run up
    the shaft like cyl(mode='axis')."""
    _, v0, v1, hm, wm = BANDS['dressed']
    v0, v1 = v0 + INSET, v1 - INSET
    n = 2 * flutes
    def ring(z, r):
        return [(x + r * (1.0 - (depth if k % 2 else 0.0)) * math.cos(2 * math.pi * k / n),
                 y + r * (1.0 - (depth if k % 2 else 0.0)) * math.sin(2 * math.pi * k / n), z) for k in range(n)]
    A, B = ring(z0, r0), ring(z1, r1)
    for k in range(n):
        j = (k + 1) % n
        fa, fb = k / n, (k + 1) / n
        mb.face([A[k], A[j], B[j], B[k]], SLOT['Structure'],
                [(z0 / wm, v0 + (v1 - v0) * fa), (z0 / wm, v0 + (v1 - v0) * fb), (z1 / wm, v0 + (v1 - v0) * fb), (z1 / wm, v0 + (v1 - v0) * fa)], tag)
    if cap:
        mb.poly(B, 'dressed', 'fit', tag=tag)


def drum_window(mb, cx, cy, R, sides, a_c, w, z, h, cell, tag, off=0.04):
    """Atlas cell `cell` of width w centred at angle a_c on a regular `sides`-gon prism (vertices at 2*pi*k/sides,
    radius R), split at the polygon corners so every piece lies `off` in front of its own facet."""
    hw = (w / 2) / R
    a0, a1 = a_c - hw, a_c + hw
    step = 2 * math.pi / sides
    cuts = [a0]
    k = math.floor(a0 / step) + 1
    while k * step < a1 - 1e-6:
        cuts.append(k * step)
        k += 1
    cuts.append(a1)
    u0, v0, u1, v1 = cell_uv(cell)
    ap = R * math.cos(math.pi / sides)
    for i in range(len(cuts) - 1):
        aa, ab = cuts[i], cuts[i + 1]
        af = (math.floor(((aa + ab) / 2) / step) + 0.5) * step      # facet normal angle

        def P(ang, zz):
            r = ap / math.cos(ang - af)
            return (cx + r * math.cos(ang) + math.cos(af) * off, cy + r * math.sin(ang) + math.sin(af) * off, zz)
        ua = u0 + (u1 - u0) * (aa - a0) / (a1 - a0)
        ub = u0 + (u1 - u0) * (ab - a0) / (a1 - a0)
        mb.face([P(aa, z), P(ab, z), P(ab, z + h), P(aa, z + h)], SLOT['Detail'], [(ua, v0), (ub, v0), (ub, v1), (ua, v1)], tag)


def _octo_quad(mb, ang, apothem, z, w, h, cell, rad):
    c, s = math.cos(ang), math.sin(ang)
    n = (c, s)
    t = (-s, c)
    off = 0.04
    def P(u, zz):
        ap = rad(zz) * math.cos(math.pi / 8) + off
        return (n[0] * ap + t[0] * u, n[1] * ap + t[1] * u, zz)
    mb.aquad(P(-w / 2, z), P(w / 2, z), P(w / 2, z + h), P(-w / 2, z + h), cell, tag='wall')


# ----------------------------------------------------------------------------------------------- civic dome
def civic_dome(lod, state, seed=191):
    """Domed civic temple: stepped podium, six-column portico with a pediment, cella with side aisles, drum, ribbed
    lead dome and lantern. ~30 x 40 m, 36 m to the lantern cross."""
    ctx = Ctx(lod, state, seed)
    mb = MB(seed)
    rng = random.Random(seed)
    px0, px1, py0, py1 = -15.0, 15.0, -20.5, 18.0
    info = dict(footprint=rect_path(px0, py0, px1, py1), W=30.0, D=38.5)
    zp = 1.5
    info['base'] = zp
    cx0, cx1, cy0, cy1 = -10.5, 10.5, -8.0, 15.0     # cella
    zce = zp + 13.0                                  # cella cornice (wall top)
    ax0, ax1 = -14.0, 14.0                           # side aisles
    zae = zp + 8.5
    pitch = 17.0
    dcx, dcy = 0.0, 3.5
    Rd = 7.4
    if lod == 3:
        mb.box((0, (py0 + py1) / 2, (FOOT + zp) / 2), 15.0, (py1 - py0) / 2, (zp - FOOT) / 2, 'ashlar', top='dressed', mode='fit', tag='base')
        hull_block(mb, cx0, -17.0, cx1, cy1, zce, 'gable', pitch, band='ashlar', axis='Y')
        mb.box((ax0 + 1.75, 3.5, (zp + zae) / 2), 1.75, 11.0, (zae - zp) / 2, 'ashlar', mode='fit', tag='wall')
        mb.box((ax1 - 1.75, 3.5, (zp + zae) / 2), 1.75, 11.0, (zae - zp) / 2, 'ashlar', mode='fit', tag='wall')
        mb.cyl((dcx, dcy, 0), Rd, Rd, zce, zce + 7.0, 12, 'ashlar', mode='axis', cap_top=False, tag='wall')
        mb.dome((dcx, dcy, 0), Rd + 0.3, zce + 7.0, 8.5, 12, 3, 'lead', tag='roof')
        hull_windows(mb, ax0, -7.5, ax1, 14.5, zp, [dict(h=8.0, win=(2.0, 4.5, 2.0))], (0, 5))
        info['ridge'] = zce + 7.0 + 8.5 + 5.0
        info['eave'] = zce
        return mb, info
    # podium: three steps all round, a wide flight in front of the portico
    for i in range(3):
        o = (2 - i) * 0.6
        zt_ = zp * (i + 1) / 3
        mb.box((0, (py0 + py1) / 2 + o * 0.0, (FOOT + zt_) / 2), (px1 - px0) / 2 + o - 0.0, (py1 - py0) / 2 + o, (zt_ - FOOT) / 2, 'ashlar',
               top='dressed' if i == 2 else 'ashlar', anchor=0.0, tag='base', bottom=(i == 0))
    # verifier fix: a walkable front flight (0.167 m risers, 0.33 m treads) between stone cheeks; the podium's
    # 0.5 m steps alone do not read as human scale next to the 1.8 m figure
    if lod <= 2:
        nst = 9 if lod <= 1 else 3
        rise, tread, sw = zp / nst, 0.99 / (nst / 3.0), 6.4
        for k in range(1, nst):
            yf = py0 - k * tread
            zt_ = zp - k * rise
            mb.box((0.0, (yf + py0 + 0.3) / 2, (FOOT + zt_) / 2), sw, (py0 + 0.3 - yf) / 2, (zt_ - FOOT) / 2, 'dressed',
                   mode='fit', tag='base', sides=(True, False, False, False))
        ylo = py0 - (nst - 1) * tread - 0.2
        for s in (-1, 1):
            mb.box((s * (sw + 0.45), (ylo + py0) / 2, (FOOT + zp + 0.15) / 2), 0.45, (py0 - ylo) / 2, (zp + 0.15 - FOOT) / 2, 'ashlar',
                   top='dressed', anchor=0.0, tag='base', sides=(True, True, False, True))
    # cella walls (front wall sits behind the portico)
    cst = [dict(h=8.0, band='ashlar', win=(2.0, 4.6, 2.2), door=(3.2, 6.0)), dict(h=zce - zp - 8.0, band='ashlar', win=(1.4, 2.0, 1.0))]
    front = lambda k, i: ('door' if i == 1 else 'window_arch') if k == 0 else 'window_arch'
    sidek = lambda k, i: 'window_arch' if k == 1 else 'none'
    backk = lambda k, i: 'window_arch'
    block_walls(mb, cx0, cy0, cx1, cy1, zp, cst, ctx, {'S': (3, front), 'E': (5, sidek), 'N': (3, backk), 'W': (5, sidek)}, margin=1.2, keystone=True)
    corner_pilasters(mb, cx0, cy0, cx1, cy1, zp, zce, ctx, w=0.9, p=0.12)
    # pilasters along the cella sides (above the aisle roofs) and the back
    if lod <= 1:
        for i in range(6):
            y = cy0 + 1.2 + i * (cy1 - cy0 - 2.4) / 5
            for x, sx in ((cx0, -1), (cx1, 1)):
                mb.box((x + sx * 0.08, y, (zae + zce) / 2), 0.1, 0.4, (zce - zae) / 2, 'ashlar', top=False, anchor=zp, tag='wall', sides=(True, sx > 0, True, sx < 0))
    string_courses(mb, rect_path(cx0, cy0, cx1, cy1), [zce - 0.4], ctx, big=True)
    # side aisles: lower blocks with lean-to roofs
    ast = [dict(h=zae - zp, band='ashlar', win=(1.8, 4.0, 2.0), door=(1.8, 3.6))]
    for (xa, xb, side) in ((ax0, cx0, 'W'), (cx1, ax1, 'E')):
        ya, yb = -6.0, 13.0
        kk = win_kinds(seed + (1 if side == 'W' else 2), lambda k, i: 'door' if i == 2 else 'window_arch', 0.0)
        sp = {'S': (1, lambda k, i: 'window_arch'), 'N': (1, lambda k, i: 'window_arch')}
        sp[side] = (5, kk)
        block_walls(mb, xa, ya, xb, yb, zp, ast, ctx, sp, margin=1.0, keystone=True)
        string_courses(mb, [(xa, ya), (xb, ya), (xb, yb), (xa, yb)] if side == 'E' else [(xa, ya), (xb, ya), (xb, yb), (xa, yb)], [zae - 0.3], ctx)
        # lean-to roof falling away from the cella
        ov = 0.5
        hgt = (xb - xa) * math.tan(math.radians(pitch))
        if side == 'W':
            hi_x, lo_x = xb, xa - ov
        else:
            hi_x, lo_x = xa, xb + ov
        zl = zae - ov * math.tan(math.radians(pitch))
        zh = zae + hgt
        # verifier fix: every face here is oriented explicitly; the slopes of both lean-to roofs faced down,
        # so with back-face culling the aisles showed no roof at all
        def orient(q, want):
            return q if dot(newell(q), want) > 0 else q[::-1]
        out_x = -1.0 if side == 'W' else 1.0
        q = [(lo_x, ya - ov, zl), (lo_x, yb + ov, zl), (hi_x, yb + ov, zh), (hi_x, ya - ov, zh)]
        mb.poly(orient(q, (0.0, 0.0, 1.0)), 'tile', 'metric', tag='roof')
        if lod <= 2:
            mb.poly(orient([(lo_x, ya - ov, zl - 0.2), (lo_x, yb + ov, zl - 0.2), (lo_x, yb + ov, zl), (lo_x, ya - ov, zl)], (out_x, 0.0, 0.0)),
                    'planks', 'fit', tag='roof')
            mb.poly(orient([(lo_x, ya - ov, zl - 0.2), (hi_x, ya - ov, zh - 0.2), (hi_x, ya - ov, zh), (lo_x, ya - ov, zl)], (0.0, -1.0, 0.0)),
                    'planks', 'fit', tag='roof')
            mb.poly(orient([(lo_x, yb + ov, zl - 0.2), (hi_x, yb + ov, zh - 0.2), (hi_x, yb + ov, zh), (lo_x, yb + ov, zl)], (0.0, 1.0, 0.0)),
                    'planks', 'fit', tag='roof')
            # soffit under the overhang
            mb.poly(orient([(lo_x, ya - ov, zl - 0.2), (lo_x, yb + ov, zl - 0.2), (lo_x - out_x * ov, yb + ov, zae - 0.2), (lo_x - out_x * ov, ya - ov, zae - 0.2)],
                           (0.0, 0.0, -1.0)), 'planks', 'fit', tag='roof')
    # portico: six columns in front, two returns, entablature, pediment
    porch_y0 = -17.0
    col_r = 0.62
    zcol0, zcol1 = zp, zce - 1.9
    xs = [-9.0, -5.4, -1.8, 1.8, 5.4, 9.0]
    cols = [(x, porch_y0 + 1.0) for x in xs] + [(-9.0, porch_y0 + 4.6), (9.0, porch_y0 + 4.6)]
    csides = 16 if lod == 0 else (10 if lod == 1 else 6)
    broken = set()
    for ci, (x, y) in enumerate(cols):
        ztop = zcol1
        if ctx.holes and state == 'ruin':
            if rng.random() < 0.55:
                ztop = zcol0 + rng.uniform(1.0, 7.0)
                broken.add(ci)
        if lod <= 1:
            mb.box((x, y, zcol0 + 0.25), 0.85, 0.85, 0.25, 'dressed', mode='fit', tag='column')
            mb.cyl((x, y, 0), 0.78, 0.7, zcol0 + 0.5, zcol0 + 0.75, csides, 'dressed', mode='axis', tag='column', cap_top=True)  # verifier: closed (open top showed its inside)
        if lod == 0:
            # verifier fix: fluted shafts at LOD0 (20 flutes) so the portico catches the low sun at 40 m
            fluted_shaft(mb, x, y, zcol0 + 0.5, ztop - (0.6 if ci not in broken else 0.0), col_r, col_r * 0.86, 20, 'column',
                         cap=ci in broken)
        else:
            mb.cyl((x, y, 0), col_r, col_r * 0.86, zcol0 + 0.5, ztop - (0.6 if ci not in broken else 0.0), csides, 'dressed', mode='axis',
                   tag='column', cap_top=ci in broken)
        if ci in broken:
            continue
        if lod <= 1:
            mb.cyl((x, y, 0), col_r * 0.86, 0.95, ztop - 0.6, ztop - 0.25, csides, 'dressed', mode='axis', tag='column', cap_top=False)
        mb.box((x, y, ztop - 0.12), 0.95, 0.95, 0.13, 'dressed', mode='fit', tag='column')
    # entablature over the porch, carried back to the cella
    zent0, zent1 = zcol1, zce
    if not (ctx.holes and state == 'ruin'):
        mb.box((0.0, (porch_y0 + cy0) / 2 - 0.2, (zent0 + zent1) / 2), 10.3, (cy0 - porch_y0) / 2 + 0.2, (zent1 - zent0) / 2, 'ashlar',
               top=False, anchor=zp, tag='upper', sides=(True, True, False, True), bottom=True)
        string_courses(mb, [(-10.3, porch_y0 - 0.4), (10.3, porch_y0 - 0.4), (10.3, cy0), (-10.3, cy0)], [zce - 0.4], ctx, big=True)
    # main gable roof over portico + cella; the front gable is the pediment
    zr = mb.gable_roof(cx0, cx1, porch_y0 - 0.4, cy1, zce, pitch, ov=0.6, ovg=0.9, axis='Y', lod=lod, gable_band='ashlar',
                       gable_anchor=zp, info=info, tag='roof')
    if lod <= 1 and not ctx.holes:
        # raking cornice under the pediment slopes and acroteria
        hw = (cx1 - cx0) / 2 + 0.6
        tp = math.tan(math.radians(pitch))
        y = porch_y0 - 0.4 - 0.9
        for s in (-1, 1):
            a = (s * hw, y, zce - 0.6 * tp)
            b = (0.0, y, zce + (cx1 - cx0) / 2 * tp)
            mb.beam(a, b, 0.5, 0.35, 'dressed', up=(0, -1, 0), tag='roof')
            mb.box((s * (hw - 0.4), y + 0.4, zce + 0.5), 0.35, 0.35, 0.5, 'dressed', mode='fit', tag='roof')
        mb.box((0.0, y + 0.4, zr + 0.6), 0.4, 0.4, 0.6, 'dressed', mode='fit', tag='roof')
    if lod == 0 and not ctx.holes:
        # verifier: dentil courses under the portico and cella cornices and a sculpture group in the pediment
        # (the temple was 5.1k tris against the 8-20k civic budget and read plain at 40 m)
        ye = porch_y0 - 0.4
        zd = zce - 0.7
        dentils(mb, (-10.3, ye), (10.3, ye), zd, tag='upper')
        dentils(mb, (10.3, ye), (10.3, cy0), zd, tag='upper')
        dentils(mb, (-10.3, cy0), (-10.3, ye), zd, tag='upper')
        dentils(mb, (cx1, cy0), (cx1, cy1), zd, tag='upper')
        dentils(mb, (cx1, cy1), (cx0, cy1), zd, tag='upper')
        dentils(mb, (cx0, cy1), (cx0, cy0), zd, tag='upper')
        tp = math.tan(math.radians(pitch))
        srng = random.Random(seed + 5)
        for xi in (-7.6, -5.7, -3.8, -1.9, 0.0, 1.9, 3.8, 5.7, 7.6):
            hmax = (10.5 - abs(xi) - 0.5) * tp - 0.25
            if hmax < 0.6:
                continue
            hb = min(2.3, hmax) * (0.82 if xi else 1.0) * srng.uniform(0.92, 1.0)
            lean = 0.0 if abs(xi) < 3 else (0.35 if abs(xi) > 6 else 0.0)
            hb = hb - lean
            mb.box((xi, ye - 0.14, zce + 0.02 + hb * 0.4), 0.42, 0.14, hb * 0.4, 'dressed', mode='fit', tag='upper', sides=(True, True, False, True))
            mb.box((xi, ye - 0.12, zce + 0.02 + hb * 0.8 + 0.02), 0.28, 0.12, hb * 0.2, 'dressed', mode='fit', tag='upper', sides=(True, True, False, True))
            if hb > 1.1:
                mb.box((xi, ye - 0.1, zce + 0.04 + hb + 0.12), 0.13, 0.1, 0.13, 'dressed', mode='fit', tag='upper', sides=(True, True, False, True))
    # drum, dome, lantern
    zd0 = zr - 1.2
    zd1 = zr + 5.2
    dsides = 36 if lod == 0 else (24 if lod == 1 else 12)     # verifier: 24/16 -> 36/24 (smoother dome silhouette at 40 m)
    mb.cyl((dcx, dcy, 0), Rd, Rd, zd0, zd1, dsides, 'ashlar', cap_top=False, tag='drum')
    if lod <= 1:
        mb.cyl((dcx, dcy, 0), Rd + 0.35, Rd + 0.35, zd0, zd0 + 0.9, dsides, 'dressed', cap_top=True, tag='drum', mode='axis')
        circ = [(dcx + (Rd) * math.cos(2 * math.pi * j / dsides), dcy + (Rd) * math.sin(2 * math.pi * j / dsides)) for j in range(dsides)]
        mb.sweep(circ, [(0.0, -0.2), (0.25, -0.1), (0.5, 0.15), (0.6, 0.35), (0.0, 0.4)] if lod == 0 else [(0.0, -0.2), (0.55, 0.2), (0.0, 0.4)],
                 'dressed', z=zd1 - 0.4, tag='drum')
    if not ctx.holes:
        nwin = 12 if lod <= 1 else 8
        for j in range(nwin):
            a = 2 * math.pi * (j + 0.5) / nwin
            # verifier fix: the flat window card sat on a facet corner of the drum polygon and was half buried in
            # it; drum_window bends the card across the facets it spans
            drum_window(mb, dcx, dcy, Rd, dsides, a, 1.3, zd0 + 1.5, 2.9, 'window_arch', 'drum')
            if lod == 0:
                a2 = 2 * math.pi * j / nwin
                mb.box((dcx + (Rd + 0.1) * math.cos(a2), dcy + (Rd + 0.1) * math.sin(a2), (zd0 + 0.9 + zd1 - 0.4) / 2), 0.14, 0.35,
                       (zd1 - 0.4 - zd0 - 0.9) / 2, 'ashlar', rot=a2, top=False, anchor=zp, tag='drum', sides=(True, True, True, False))
    if not (ctx.holes and state == 'ruin'):
        H = 8.6
        rings = 14 if lod == 0 else (8 if lod == 1 else 5)
        prof = lambda t: (math.cos(t * math.pi / 2) ** 0.9, math.sin(t * math.pi / 2))
        top = mb.dome((dcx, dcy, 0), Rd + 0.25, zd1, H, dsides, rings, 'lead', open_r=1.5, tag='roof', profile=prof)
        if lod <= 1:
            nr = 12 if lod == 0 else 8
            for j in range(nr):
                a = 2 * math.pi * j / nr
                pts = []
                for i in range(rings + 1):
                    t = i / rings
                    rf, zf = prof(t)
                    r = (Rd + 0.25) * rf
                    if r < 1.5:
                        break
                    pts.append((dcx + (r + 0.04) * math.cos(a), dcy + (r + 0.04) * math.sin(a), zd1 + H * zf))
                for i in range(len(pts) - 1):
                    mb.cyl_along(pts[i], pts[i + 1], 0.16, 3, 'lead', half=True)
        # lantern
        zl0 = top[1]
        mb.cyl((dcx, dcy, 0), 1.5, 1.5, zl0 - 0.2, zl0 + 2.6, 8, 'dressed', cap_top=False, tag='roof', mode='axis')
        if lod <= 1:
            for j in range(8):
                a = 2 * math.pi * (j + 0.5) / 8
                ap = 1.5 * math.cos(math.pi / 8) + 0.03
                n_ = (math.cos(a), math.sin(a))
                t_ = (-math.sin(a), math.cos(a))
                P = lambda u, zz: (dcx + n_[0] * ap + t_[0] * u, dcy + n_[1] * ap + t_[1] * u, zz)
                mb.aquad(P(-0.3, zl0 + 0.4), P(0.3, zl0 + 0.4), P(0.3, zl0 + 2.0), P(-0.3, zl0 + 2.0), 'window_arch', tag='roof')
        mb.cyl((dcx, dcy, 0), 1.75, 1.75, zl0 + 2.6, zl0 + 2.85, 8, 'dressed', tag='roof', mode='axis', cap_top=False)
        t2 = mb.dome((dcx, dcy, 0), 1.75, zl0 + 2.85, 1.4, 8, 3, 'lead', open_r=0.2, tag='roof')
        mb.cyl((dcx, dcy, 0), 0.2, 0.12, t2[1], t2[1] + 0.6, 6, None, cell='gilt', tag='roof', cap_top=False)
        if lod <= 1:
            mb.dome((dcx, dcy, 0), 0.38, t2[1] + 0.6, 0.76, 6, 4, 'lead', tag='roof',
                    profile=lambda t: (math.sin(t * math.pi) if t < 1 else 0.0, (1 - math.cos(t * math.pi)) / 2))
            mb.cyl((dcx, dcy, 0), 0.06, 0.04, t2[1] + 1.3, t2[1] + 3.0, 4, None, cell='gilt', tag='roof')
        info['ridge'] = t2[1] + 3.0
    else:
        info['ridge'] = zd1
    info['eave'] = zce
    return mb, info


# ----------------------------------------------------------------------------------------------- registry
# name: (function, kind, state set, description)
STATES_BUILDING = ['Intact', 'Construction', 'Damaged', 'Ruin']
STATES_DOCK = ['Intact', 'Damaged', 'Ruin']

ASSETS = {
    'Townhouse_A': (townhouse_a, 'house', STATES_BUILDING, 'Narrow gable-fronted townhouse, 3 storeys, stone ground floor, balcony'),
    'Townhouse_B': (townhouse_b, 'house', STATES_BUILDING, 'Merchant house, 4 storeys, arcaded rusticated ground floor, hip roof'),
    'Townhouse_C': (townhouse_c, 'house', STATES_BUILDING, 'Ashlar house with eaves to the street, two dormers, gable chimneys'),
    'Townhouse_D': (townhouse_d, 'house', STATES_BUILDING, 'L-shaped corner block round a back yard with a belvedere tower'),
    'Warehouse_A': (warehouse_a, 'warehouse', STATES_BUILDING, 'Long quayside warehouse, 2 storeys, 3 loading doors, hoist dormer'),
    'Warehouse_B': (warehouse_b, 'warehouse', STATES_BUILDING, 'Tall gable-fronted stack warehouse, stacked loading doors, apex hoist'),
    'Quay_Straight': (quay_straight, 'quay', STATES_DOCK, 'Stone quay wall, 20 m frontage, bollards, fenders, ladder'),
    'Quay_Corner': (quay_corner, 'quay', STATES_DOCK, 'Outer quay corner with a rounded nose, 10 x 10 m'),
    'Quay_Stairs': (quay_stairs, 'quay', STATES_DOCK, 'Quay segment with a flight of water stairs along its face'),
    'Pier': (pier, 'pier', STATES_DOCK, 'Timber pier on piles, 5 x 24 m, mooring posts, cargo'),
    'PortCrane': (port_crane, 'crane', STATES_BUILDING, 'Treadwheel harbour crane: stone base, braced tower, slewing cab, jib'),
    'Lighthouse': (lighthouse, 'lighthouse', STATES_BUILDING, 'Octagonal ashlar lighthouse with gallery, glazed lantern, lead cap'),
    'CivicDome': (civic_dome, 'civic', STATES_BUILDING, 'Domed civic temple: portico and pediment, aisles, drum, ribbed lead dome, lantern'),
}
