# DEMIURGE art - wonders-a: wonder 0 (Stone age), the Hearth of Many Fires.
# Two concentric earth-and-river-stone banks (r 70 m and 40 m, 8 m wide, 1.8 m high) with fire pits every
# 3 m along both crests, and a 25 m cone of timber poles hung with hides and crowned with antlers.
import math
import random

from mathutils import Vector

from wa_lib import (MB, STRUCT, ROOF, DETAIL, TAU, cell_uv, lathe, cyl_between, rock, flame, fire, disc, block,
                    noise1, box_face)

ASSET = dict(
    key='hearth', name='Hearth', concept='wonder-0-stone', age=0, title='The Hearth of Many Fires',
    mats=[('HearthBank', 'Structure'), ('HearthCone', 'Roof'), ('DetailA', 'Detail')],
    emissive_tint='#ff8c38', light_kind='fire pit',
    states=['Intact', 'Construction', 'Damaged', 'Ruin'],
    day_emit=1.3,
)

R_OUT, R_IN = 74.0, 42.0
BANK_W, BANK_H = 9.0, 2.0
GAP_OUT = -math.pi / 2       # outer entrance faces the front (-Y)
GAP_IN = math.pi / 2         # inner entrance at the back: you walk half way round between the banks
GAP_HALF_M, GAP_TAPER_M = 3.5, 7.0
PIT_SPACING = 3.0
CONE_H, CONE_R = 25.0, 5.0
TILE_U = 16.0


def sstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def ang_d(a, b):
    return abs((a - b + math.pi) % TAU - math.pi)


class Bank:
    """One ring bank. gaps: list of (angle, half_width_m, taper_m, depth) where depth 1 = fully cut to ground."""

    def __init__(self, R, gaps, seed, hscale=1.0, wscale=1.0, arc=None):
        self.R, self.gaps, self.seed = R, gaps, seed
        self.hscale, self.wscale = hscale, wscale
        self.arc = arc   # (theta_start, theta_end) built portion for construction

    def gfac(self, th):
        g = 1.0
        for (a, hw, tp, depth) in self.gaps:
            d = ang_d(th, a) * self.R
            g = min(g, 1.0 - depth * (1.0 - sstep(hw, hw + tp, d)))
        if self.arc:
            a0, a1 = self.arc
            t = (th - a0) % TAU
            span = (a1 - a0) % TAU
            d0 = t * self.R
            d1 = (span - t) * self.R
            if t > span:
                g = 0.0
            else:
                g = min(g, sstep(0.0, 10.0, d0), sstep(0.0, 14.0, d1))
        return g

    def height(self, th):
        return BANK_H * self.hscale * (1.0 + 0.07 * noise1(th * self.R / 9.0, self.seed)) * self.gfac(th)

    def crest(self, th):
        r = self.R + 0.5 * noise1(th * self.R / 23.0, self.seed + 5)
        return r, self.height(th)


def build_bank(mb, bank, q):
    seg_len = [1.6, 3.6, 8.0, 18.0][q]
    segs = max(24, int(TAU * bank.R / seg_len))
    W = BANK_W * bank.wscale
    if q == 0:
        ds = [-1.0, -0.88, -0.72, -0.52, -0.32, -0.14, 0.0, 0.14, 0.32, 0.52, 0.72, 0.88, 1.0]
    elif q == 1:
        ds = [-1.0, -0.8, -0.45, 0.0, 0.45, 0.8, 1.0]
    elif q == 2:
        ds = [-1.0, -0.6, 0.0, 0.6, 1.0]
    else:
        ds = [-1.0, 0.0, 1.0]
    half = W / 2 + 0.6

    def prof(dn, h):
        d = dn * half
        if abs(d) >= W / 2:
            z = -0.3 * sstep(W / 2, half, abs(d))
        else:
            z = h * max(0.0, math.cos(math.pi * d / W)) ** 1.3
        return d, z
    rows, uvs = [], []
    for j, dn in enumerate(ds):
        row, urow = [], []
        for i in range(segs):
            th = TAU * i / segs
            rc, h = bank.crest(th)
            d, z = prof(dn, h)
            wob = 0.25 * noise1(th * bank.R / 3.0 + dn * 3, bank.seed + 11) if 0 < abs(dn) < 1 else 0.0
            r = rc + d * (1.0 + 0.06 * noise1(th * 7 + dn, bank.seed)) + wob
            if h <= 0.02:
                z = -0.3
            row.append(mb.vert((r * math.cos(th), r * math.sin(th), z)))
            urow.append((th * bank.R / TILE_U, (dn + 1) / 2))
        rows.append(row)
        uvs.append(urow)
    # faces: rows go from inner foot (dn=-1) to outer foot (dn=+1); normal must point up/out
    nu = segs
    for j in range(len(rows) - 1):
        for i in range(nu):
            i2 = (i + 1) % nu
            th2 = TAU * (i + 1) / segs
            idx = [rows[j][i], rows[j + 1][i], rows[j + 1][i2], rows[j][i2]]
            # skip the buried part of gaps: all four verts at -0.3
            if all(mb.v[k][2] <= -0.299 for k in idx):
                continue
            u1 = uvs[j][i][0]
            u2 = th2 * bank.R / TILE_U
            uv = [(u1, uvs[j][i][1]), (u1, uvs[j + 1][i][1]), (u2, uvs[j + 1][i][1]), (u2, uvs[j][i][1])]
            mb.face(idx, STRUCT, uv, True)
    return segs


def pit_positions(bank, spacing=PIT_SPACING):
    n = int(TAU * bank.R / spacing)
    out = []
    for k in range(n):
        th = TAU * (k + 0.5) / n
        if bank.gfac(th) < 0.92:
            continue
        rc, h = bank.crest(th)
        out.append((th, rc, h))
    return out


def fire_pit(mb, pos, rng, q, lit=True, phase=None):
    x, y, z = pos
    if q == 0:
        ns = 6
        for k in range(ns):
            a = TAU * k / ns + rng.random() * 0.4
            rock(mb, (x + 0.66 * math.cos(a), y + 0.66 * math.sin(a), z + 0.05),
                 (0.27 + rng.random() * 0.08, 0.22, 0.17), rng, level=-1, rough=0.3, cell='stone')
        disc(mb, (x, y, z + 0.06), 0.55, 6, DETAIL, 'charcoal')
        for k in range(2):
            a = rng.random() * math.pi
            c, s = math.cos(a) * 0.45, math.sin(a) * 0.45
            cyl_between(mb, (x - c, y - s, z + 0.12), (x + c, y + s, z + 0.2), 0.08, 0.07, 4, DETAIL, cell='charcoal' if lit else 'timber', caps=False)
        if lit:
            fire(mb, (x, y, z + 0.1), 1.0 + rng.random() * 0.4, 0.3, rng, q=0, phase=phase, tongues=3)
    elif q == 1:
        for k in range(3):
            a = TAU * k / 3 + rng.random()
            rock(mb, (x + 0.6 * math.cos(a), y + 0.6 * math.sin(a), z + 0.05), (0.3, 0.24, 0.18), rng, level=-1, cell='stone')
        if lit:
            fire(mb, (x, y, z + 0.05), 1.1, 0.34, rng, q=1, phase=phase)
        else:
            disc(mb, (x, y, z + 0.06), 0.5, 5, DETAIL, 'charcoal')
    elif q == 2:
        if lit:
            fire(mb, (x, y, z), 1.2, 0.42, rng, q=2, phase=phase)
        else:
            disc(mb, (x, y, z + 0.06), 0.55, 4, DETAIL, 'charcoal')


def build_pits(mb, bank, rng, q, lit_fn, lights, sink=0.0):
    for k, (th, rc, h) in enumerate(pit_positions(bank)):
        lit = lit_fn(th, k)
        p = (rc * math.cos(th), rc * math.sin(th), h - 0.08 - sink)
        ph = rng.random()
        if q < 3:
            fire_pit(mb, p, rng, q, lit, phase=ph)
        if lit and lights is not None:
            lights.append(dict(kind='fire', pos=[round(p[0], 2), round(p[1], 2), round(p[2] + 0.6, 2)], radius_m=6.0,
                               color='#ff8c38', phase=round(ph, 3)))


def foot_boulders(mb, bank, rng, q, every=3.0, outer_only=False):
    if q >= 2:
        return
    step = every if q == 0 else every * 2.2
    W = BANK_W * bank.wscale
    n = int(TAU * bank.R / step)
    for k in range(n):
        th = TAU * (k + rng.random() * 0.6) / n
        g = bank.gfac(th)
        if g < 0.3:
            continue
        rc, h = bank.crest(th)
        for side in ((1,) if outer_only else (1, -1)):
            if rng.random() < 0.25:
                continue
            r = rc + side * (W / 2 - 0.4 + rng.random() * 0.8)
            s = 0.35 + rng.random() * 0.4
            rock(mb, (r * math.cos(th), r * math.sin(th), 0.05), (s * 1.2, s, s * 0.7), rng,
                 level=0 if q == 0 else -1, cell='stone')


def cone_cover(mb, q, h_cut=None, sector_cut=None, rng=None):
    """Solid cone covering (bundled poles texture, one bundle per facet)."""
    segs = [24, 14, 8, 6][q]
    rows_z = [-0.3, 2.0, 5.0, 8.5, 12.0, 15.5, 19.0, 21.5, 23.5] if q == 0 else (
        [-0.3, 4.0, 10.0, 16.0, 21.0, 23.5] if q == 1 else [-0.3, 8.0, 17.0, 23.5])
    top = 23.5
    if h_cut:
        rows_z = [z for z in rows_z if z < h_cut] + [h_cut]

    def rad(z, th):
        t = max(0.0, (z + 0.3) / (top + 0.3))
        r = (CONE_R - 0.3) * (1 - t) ** 1.08 + 0.0
        return r * (1.0 + 0.03 * math.sin(th * 5 + z * 0.4))
    grid = []
    for z in rows_z:
        row = []
        for i in range(segs):
            th = TAU * i / segs
            r = rad(z, th)
            row.append(mb.vert((r * math.cos(th), r * math.sin(th), z)))
        grid.append(row)
    for j in range(len(rows_z) - 1):
        for i in range(segs):
            i2 = (i + 1) % segs
            thc = TAU * (i + 0.5) / segs
            if sector_cut and ang_d(thc, sector_cut[0]) < sector_cut[1] and rows_z[j] > sector_cut[2]:
                continue
            z0, z1 = rows_z[j], rows_z[j + 1]
            uv = [(0.02, z0 / 4.0), (0.48, z0 / 4.0), (0.48, z1 / 4.0), (0.02, z1 / 4.0)]
            mb.face([grid[j][i], grid[j][i2], grid[j + 1][i2], grid[j + 1][i]], ROOF, uv, True)
    # close the top
    last = grid[-1]
    if h_cut or True:
        c = mb.vert((0, 0, rows_z[-1] + (0.6 if not h_cut else 0.0)))
        for i in range(segs):
            i2 = (i + 1) % segs
            mb.face([last[i], last[i2], c], ROOF, [(0.02, 0.0), (0.48, 0.0), (0.25, 0.4)], True)
    return rad


def cone_poles(mb, rng, q, n=None, top_z=27.6, broken=0.0, charred=False, keep=1.0):
    n = n or [40, 22, 10, 0][q]
    sides = [6, 4, 3, 3][q]
    for k in range(n):
        if rng.random() > keep:
            continue
        th = TAU * (k + rng.random() * 0.5) / n
        rb = CONE_R + 0.2 + rng.random() * 0.4
        p0 = Vector((rb * math.cos(th), rb * math.sin(th), -0.3))
        # cross near the apex, then splay
        apex = Vector((rng.uniform(-0.35, 0.35), rng.uniform(-0.35, 0.35), 23.6))
        d = (apex - p0).normalized()
        L = (apex - p0).length + (top_z - 23.6) / max(0.2, d.z) * (0.55 + 0.7 * rng.random())
        if broken and rng.random() < broken:
            L *= rng.uniform(0.35, 0.8)
        p1 = p0 + d * L
        if charred:
            cyl_between(mb, p0, p1, 0.2, 0.1, sides, DETAIL, cell='charcoal', caps=True)
        else:
            cyl_between(mb, p0, p1, 0.21, 0.1, sides, ROOF, uzone=(0.0, 0.125), vlen=4.0, caps=True)


def lashing(mb, rad, q, zs=(7.5, 13.5, 19.0)):
    if q >= 2:
        return
    for z in zs:
        r = rad(z, 0.0) + 0.22
        prof = [(r, z - 0.35), (r + 0.18, z - 0.15), (r + 0.18, z + 0.15), (r, z + 0.35)]
        lathe(mb, prof, [24, 14][q], ROOF, uvfn=lambda i, th, j, rr, zz: (0.52 + 0.4 * j / 3, th * 2.0))


def hides(mb, rad, rng, q, n=11):
    if q >= 2:
        return
    for k in range(n):
        th = TAU * (k + rng.random() * 0.4) / n
        z0 = rng.uniform(1.5, 4.0)
        hgt = rng.uniform(4.0, 7.0)
        w = rng.uniform(2.8, 4.2)
        cols = 4 if q == 0 else 2
        rowsn = 4 if q == 0 else 2
        grid = []
        for j in range(rowsn + 1):
            row = []
            zz = z0 + hgt * j / rowsn
            for i in range(cols + 1):
                a = th + (i / cols - 0.5) * w / max(1.0, rad(zz, th))
                r = rad(zz, a) + 0.42 + 0.15 * math.sin(i * 1.7 + k) * (1 - j / rowsn)
                zb = zz - (0.5 * rng.random() if j == 0 else 0.0)
                row.append(mb.vert((r * math.cos(a), r * math.sin(a), zb)))
            grid.append(row)
        for j in range(rowsn):
            for i in range(cols):
                uv = [(0.52 + 0.46 * i / cols, j / rowsn), (0.52 + 0.46 * (i + 1) / cols, j / rowsn),
                      (0.52 + 0.46 * (i + 1) / cols, (j + 1) / rowsn), (0.52 + 0.46 * i / cols, (j + 1) / rowsn)]
                mb.face([grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]], ROOF, uv, True)


def antler(mb, base, direction, rng, q, scale=1.0, lying=False):
    """Main beam of 4 segments with 3 tines; bone cell."""
    sides = 4 if q == 0 else 3
    segs = 4 if q == 0 else 2
    d = Vector(direction).normalized()
    side = d.cross(Vector((0, 0, 1)))
    if side.length < 1e-3:
        side = Vector((1, 0, 0))
    side.normalize()
    p = Vector(base)
    r = 0.16 * scale
    pts = [p.copy()]
    for s in range(segs):
        bend = (Vector((0, 0, 1)) * 0.35 if not lying else side * 0.3) + side * 0.15 * math.sin(s)
        d = (d + bend * (1.0 / segs)).normalized()
        p = p + d * (3.2 * scale / segs)
        pts.append(p.copy())
    for s in range(segs):
        cyl_between(mb, pts[s], pts[s + 1], r * (1 - 0.18 * s), r * (1 - 0.18 * (s + 1)), sides, DETAIL, cell='bone', caps=(s == segs - 1))
    if q == 0:
        for t in range(3):
            a = pts[1 + t] if 1 + t < len(pts) else pts[-1]
            td = (d * 0.4 + (Vector((0, 0, 1)) if not lying else side) * 0.8 + side * (0.3 if t % 2 else -0.3)).normalized()
            cyl_between(mb, a, a + td * (1.0 * scale), r * 0.55, 0.03, 3, DETAIL, cell='bone', caps=False)


def antler_crown(mb, rng, q, n=6):
    for k in range(n):
        th = TAU * k / n + 0.3
        base = (0.5 * math.cos(th), 0.5 * math.sin(th), 23.8)
        antler(mb, base, (math.cos(th) * 0.6, math.sin(th) * 0.6, 1.0), rng, q, scale=1.0 + 0.15 * (k % 2))


def mound(mb, q, r_top=1.2, r=7.6, h=0.5, sink=0.0):
    segs = [28, 16, 10, 8][q]
    prof = [(r, -0.3), (r - 0.6, 0.0 - sink), (r - 2.0, h * 0.9 - sink), (r_top, h - sink)]
    lathe(mb, prof, segs, STRUCT, uvfn=lambda i, th, j, rr, zz: (th * rr / TILE_U, 0.40 + 0.2 * j / 3), cap_top=True)
    # fix cap uv (inside the ash band)
    mb.fuv[-1] = [(0.1 + 0.05 * math.cos(TAU * i / segs), 0.5 + 0.05 * math.sin(TAU * i / segs)) for i in range(segs)]


def timber_stack(mb, center, rotz, rng, q, n=9, length=12.0):
    c, s = math.cos(rotz), math.sin(rotz)
    for k in range(n):
        lay = k // 4
        off = (k % 4 - 1.5) * 0.45 + (0.22 if lay % 2 else 0)
        z = 0.2 + lay * 0.38
        px, py = center[0] - s * off, center[1] + c * off
        L = length * rng.uniform(0.85, 1.0)
        p0 = (px - c * L / 2, py - s * L / 2, z)
        p1 = (px + c * L / 2, py + s * L / 2, z + rng.uniform(-0.05, 0.05))
        cyl_between(mb, p0, p1, 0.2, 0.17, [6, 4, 3, 3][q], ROOF, uzone=(0.0, 0.125), vlen=4.0)


def rubble_heap(mb, center, radius, rng, q, n=None, cell='stone'):
    n = n or [26, 9, 3, 0][q]
    for k in range(n):
        a = rng.random() * TAU
        d = radius * math.sqrt(rng.random())
        s = 0.3 + rng.random() * 0.45
        z = max(0.0, (1 - d / radius)) * radius * 0.35
        rock(mb, (center[0] + d * math.cos(a), center[1] + d * math.sin(a), z), (s * 1.2, s, s * 0.8), rng,
             level=0 if q == 0 else -1, cell=cell)


def build(state, q):
    """Returns (MB, lights). q = LOD level 0..3."""
    rng = random.Random(100 + q * 0)   # same layout at every LOD
    mb = MB()
    lights = []
    gaps_out = [(GAP_OUT, GAP_HALF_M, GAP_TAPER_M, 1.0)]
    gaps_in = [(GAP_IN, GAP_HALF_M, GAP_TAPER_M, 1.0)]
    if state == 'Intact':
        outer = Bank(R_OUT, gaps_out, 1)
        inner = Bank(R_IN, gaps_in, 2)
        build_bank(mb, outer, q)
        build_bank(mb, inner, q)
        prng = random.Random(7)
        build_pits(mb, outer, prng, q, lambda th, k: True, lights)
        build_pits(mb, inner, prng, q, lambda th, k: True, lights)
        foot_boulders(mb, outer, random.Random(8), q)
        foot_boulders(mb, inner, random.Random(9), q)
        mound(mb, q)
        rad = cone_cover(mb, q)
        cone_poles(mb, random.Random(10), q)
        lashing(mb, rad, q)
        hides(mb, rad, random.Random(11), q)
        if q < 3:
            antler_crown(mb, random.Random(12), q)
        lights.append(dict(kind='beacon', pos=[0, 0, 26.0], radius_m=0, color='#ff8c38', note='cone top: smoke / ember FX socket'))
    elif state == 'Construction':
        # outer bank half raised (being carried round from the entrance), inner bank done but unlit
        outer = Bank(R_OUT, gaps_out, 1, arc=(GAP_OUT + 0.12, GAP_OUT + 0.12 + TAU * 0.55))
        inner = Bank(R_IN, gaps_in, 2)
        build_bank(mb, outer, q)
        build_bank(mb, inner, q)
        prng = random.Random(7)
        build_pits(mb, inner, prng, q, lambda th, k: k % 5 == 0, lights)
        foot_boulders(mb, inner, random.Random(9), q)
        mound(mb, q)
        # bare pole frame: no cover, no hides, no antlers
        cone_poles(mb, random.Random(10), q, keep=0.6)
        rr = random.Random(13)
        # heaps of earth and river stones where the outer bank stops
        end = GAP_OUT + 0.12 + TAU * 0.55
        for k in range(4):
            a = end + 0.06 + 0.05 * k
            rubble_heap(mb, (R_OUT * math.cos(a), R_OUT * math.sin(a), 0), 3.0 + rr.random(), rr, q)
        for k in range(3):
            a = 0.8 + k * 0.5
            timber_stack(mb, (18 * math.cos(a), 18 * math.sin(a)), a + 1.2, rr, q)
    elif state == 'Damaged':
        breaches_o = gaps_out + [(0.35, 2.0, 6.0, 0.75), (2.4, 3.0, 5.0, 0.6), (4.1, 1.5, 6.0, 0.5)]
        breaches_i = gaps_in + [(-0.9, 2.0, 4.0, 0.7), (3.4, 1.5, 4.0, 0.55)]
        outer = Bank(R_OUT, breaches_o, 1)
        inner = Bank(R_IN, breaches_i, 2)
        build_bank(mb, outer, q)
        build_bank(mb, inner, q)
        prng = random.Random(7)
        lit = lambda th, k: (math.sin(th * 3.0 + 1.0) + 0.3 * math.sin(k * 1.7)) > 0.1
        build_pits(mb, outer, prng, q, lit, lights)
        build_pits(mb, inner, prng, q, lit, lights)
        foot_boulders(mb, outer, random.Random(8), q)
        foot_boulders(mb, inner, random.Random(9), q)
        mound(mb, q)
        rad = cone_cover(mb, q, sector_cut=(0.6, 0.9, 6.0))
        cone_poles(mb, random.Random(10), q, broken=0.35)
        lashing(mb, rad, q, zs=(7.5, 13.5))
        hides(mb, rad, random.Random(11), q, n=5)
        rr = random.Random(14)
        if q < 3:
            for k in range(3):
                a = rr.random() * TAU
                d = 9 + rr.random() * 6
                antler(mb, (d * math.cos(a), d * math.sin(a), 0.25), (math.cos(a), math.sin(a), 0.05), rr, q, lying=True)
            for k in range(8 if q == 0 else 3):
                a = 0.6 + rr.uniform(-0.6, 0.6)
                d = 7 + rr.random() * 10
                p0 = (d * math.cos(a), d * math.sin(a), 0.2)
                b = a + rr.uniform(-1.2, 1.2)
                L = rr.uniform(6, 14)
                cyl_between(mb, p0, (p0[0] + L * math.cos(b), p0[1] + L * math.sin(b), 0.25), 0.2, 0.12,
                            [6, 4, 3][q], DETAIL, cell='charcoal')
            for k in range(3):
                a = [0.35, 2.4, -0.9][k]
                R = [R_OUT, R_OUT, R_IN][k]
                rubble_heap(mb, (R * math.cos(a), R * math.sin(a), 0), 3.0, rr, q, n=[14, 5, 2, 0][q])
    elif state == 'Ruin':
        many = [(GAP_OUT, 5.0, 8.0, 1.0), (0.4, 4.0, 8.0, 0.9), (1.6, 3.0, 10.0, 0.8), (2.6, 6.0, 9.0, 1.0),
                (3.7, 3.0, 8.0, 0.7), (5.0, 4.0, 10.0, 0.85)]
        outer = Bank(R_OUT, many, 1, hscale=0.42, wscale=1.25)
        inner = Bank(R_IN, [(GAP_IN, 5.0, 8.0, 1.0), (-0.5, 3.0, 9.0, 0.9), (2.0, 3.0, 7.0, 0.75), (4.3, 4.0, 9.0, 1.0)],
                     2, hscale=0.42, wscale=1.25)
        build_bank(mb, outer, q)
        build_bank(mb, inner, q)
        # scattered pit stones, no fires
        rr = random.Random(15)
        if q < 2:
            for bank in (outer, inner):
                for (th, rc, h) in pit_positions(bank, 9.0):
                    p = (rc * math.cos(th), rc * math.sin(th), h - 0.1)
                    for k in range(2 if q == 0 else 1):
                        a = rr.random() * TAU
                        rock(mb, (p[0] + 0.7 * math.cos(a), p[1] + 0.7 * math.sin(a), p[2]), (0.28, 0.22, 0.16), rr,
                             level=0 if q == 0 else -1, cell='stone')
        foot_boulders(mb, outer, random.Random(8), q, every=5.0)
        mound(mb, q, h=0.3)
        # charred stumps of the cone, a fallen antler
        for k in range([16, 9, 5, 0][q]):
            th = TAU * (k + rr.random() * 0.5) / 16
            rb = CONE_R + rr.random() * 0.4
            p0 = (rb * math.cos(th), rb * math.sin(th), -0.3)
            hgt = rr.uniform(0.8, 3.2)
            cyl_between(mb, p0, (p0[0] * 0.92, p0[1] * 0.92, hgt), 0.22, 0.16, [6, 4, 3][q], DETAIL, cell='charcoal')
        if q < 3:
            for k in range([7, 3, 1][q]):
                a = rr.random() * TAU
                d = 6 + rr.random() * 12
                p0 = (d * math.cos(a), d * math.sin(a), 0.15)
                b = rr.random() * TAU
                L = rr.uniform(5, 12)
                cyl_between(mb, p0, (p0[0] + L * math.cos(b), p0[1] + L * math.sin(b), 0.15), 0.18, 0.12,
                            [6, 4, 3][q], DETAIL, cell='charcoal')
            antler(mb, (11, -6, 0.2), (1, 0.3, 0.02), rr, q, lying=True)
    return mb, lights


def lod3(state):
    """Simple hull: coarse banks, cone, and one emissive crest strip per lit ring (the night read)."""
    mb, lights = build(state, 3)
    if state in ('Intact', 'Damaged', 'Construction'):
        rings = [R_IN] if state == 'Construction' else [R_OUT, R_IN]
        for R in rings:
            segs = 28
            for i in range(segs):
                if state == 'Damaged' and i % 3 == 1:
                    continue
                a0, a1 = TAU * i / segs, TAU * (i + 0.7) / segs
                gap = GAP_OUT if R == R_OUT else GAP_IN
                if ang_d((a0 + a1) / 2, gap) * R < 6:
                    continue
                z = BANK_H + 0.1
                v = [mb.vert((R * math.cos(a), R * math.sin(a), zz), (0.5, 1.0, 0.0, 1.0))
                     for a, zz in ((a0, z), (a1, z), (a1, z + 1.2), (a0, z + 1.2))]
                mb.face(v, DETAIL, [cell_uv('fire', 0.1, 0.1), cell_uv('fire', 0.9, 0.1), cell_uv('fire', 0.9, 0.7), cell_uv('fire', 0.1, 0.7)], True)
                mb.face(list(reversed(v)), DETAIL, [cell_uv('fire', 0.1, 0.1)] * 4, True)
    return mb, lights


CAMERAS = {
    # name: (target, distance, azimuth_deg (0 = from -Y/front, + toward +X), elevation_deg, focal_mm)
    'close': ((0.0, -36.0, 6.0), 40.0, 28.0, 16.0, 30.0),
    'mid': ((0.0, 0.0, 4.0), 300.0, 32.0, 32.0, 35.0),
    'far': ((0.0, 0.0, 4.0), 1000.0, 32.0, 40.0, 35.0),
    'figure': (-4.0, -50.0, 0.0),   # between the banks, in frame of the 40 m view (verifier fix)
}
