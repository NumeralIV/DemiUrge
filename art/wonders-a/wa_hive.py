# DEMIURGE art - wonders-a: wonder 1 (Bronze age), the Great Hive.
# A white-plastered mud-brick beehive granary, 45 m across at the base and 55 m tall including a 12 m polished
# bronze cap, with a 3 m spiral ramp making three turns round the outside, lamp-lit doorways along it, and a
# terrace of ochre storage jars at its foot.
import math
import random

from mathutils import Vector, Matrix

from wa_lib import (MB, STRUCT, ROOF, DETAIL, TAU, cell_uv, lathe, cyl_between, rock, block, face_normal,
                    mb_weld, mb_cut_planes, mb_split_sector, blob, box_face, uv_box, mat_trs, noise1)

ASSET = dict(
    key='hive', name='GreatHive', concept='wonder-1-bronze', age=1, title='The Great Hive',
    mats=[('HivePlaster', 'Structure'), ('HiveBronze', 'Roof'), ('DetailA', 'Detail')],
    emissive_tint='#ffad52', light_kind='oil lamp in doorway',
    states=['Intact', 'Construction', 'Damaged', 'Ruin'],
    day_emit=1.6,
)

RB = 22.5          # base radius (45 m across)
RT = 6.2           # radius of the body top where the bronze cap sits
ZT = 43.0          # top of the plastered body
CAP_H = 12.0
STEP = 2.8         # ramp width (with its parapet) = setback of the wall above it
TURNS = 3
THETA0 = -math.pi / 2 - 0.55    # the ramp starts at the ground, front-left
PARA_H, PARA_T = 0.9, 0.45
TILE = 8.0


def sstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def O(z):
    """Inner line of the beehive (the wall foot above each ledge); convex like a skep."""
    t = min(1.0, max(0.0, z / ZT))
    return RT + (RB - STEP - RT) * (1.0 - t ** 1.2) ** 0.9


def ramp_z(phi):
    """Height of the ramp (ledge) after turning phi radians from its start."""
    return ZT * phi / (TAU * TURNS)


def crossings(tp):
    return [ramp_z(tp + TAU * j) for j in range(TURNS)]


def wall_r(tp, z, zs=None):
    """Outer wall radius at local angle tp (0..2pi from THETA0) and height z. Each tier leans out by STEP
    from its foot (the inner edge of the ledge below) to its top (the outer edge of the ledge above)."""
    zs = zs or crossings(tp)
    if z <= zs[0]:
        return O(z) + STEP
    for j in range(TURNS - 1):
        if z <= zs[j + 1]:
            return O(z) + STEP * (z - zs[j]) / max(1e-6, zs[j + 1] - zs[j])
    return O(z)


def f(k, z):
    """Approximate wall radius of tier k (for props placed round the body)."""
    return O(z) + STEP * max(0.0, 1.0 - k / TURNS)


def column(tp, sub):
    """Profile (list of (r, z)) at local angle tp in [0, 2pi]. Same vertex count for every tp."""
    zs = crossings(tp)
    prof = [(O(0) + STEP + 0.45, -0.3), (O(0) + STEP, 0.0)]
    for j in range(TURNS):
        zj = zs[j]
        arc = (tp + TAU * j) * 15.0
        ph = PARA_H * sstep(0.0, 10.0, arc)
        z0 = prof[-1][1]
        rtop = wall_r(tp, zj, zs) if j == 0 else O(zj) + STEP
        for s_ in range(1, sub + 1):
            z = z0 + (zj - z0) * s_ / sub
            r = wall_r(tp, z - 1e-6, zs) if j == 0 else (O(z) + STEP * (z - zs[j - 1]) / max(1e-6, zj - zs[j - 1]))
            prof.append((r, z))
        prof.append((rtop, zj + ph))
        prof.append((rtop - PARA_T, zj + ph))
        prof.append((rtop - PARA_T, zj))
        prof.append((O(zj), zj))
    z0 = prof[-1][1]
    for s_ in range(1, sub + 1):
        z = z0 + (ZT - z0) * s_ / sub
        prof.append((O(z), z))
    return prof


def body(q, mb=None):
    mb = mb or MB()
    ncol = [200, 80, 36, 16][q]
    sub = [6, 2, 1, 1][q]
    cols = []
    for i in range(ncol + 1):
        tp = TAU * i / ncol
        th = THETA0 + tp
        prof = column(tp, sub)
        col = []
        for (r, z) in prof:
            col.append((mb.vert((r * math.cos(th), r * math.sin(th), z)), r, z, th))
        cols.append(col)
    nv = len(cols[0])
    for i in range(ncol):
        a, b = cols[i], cols[i + 1]
        for k in range(nv - 1):
            idx = [a[k][0], b[k][0], b[k + 1][0], a[k + 1][0]]
            pts = [mb.v[x] for x in idx]
            n = face_normal(pts)
            if abs(n.z) > 0.7:     # ledges, parapet tops: planar world mapping
                uv = [uv_box(p, n, TILE) for p in pts]
            else:
                uv = [(e[3] * e[1] / TILE, e[2] / TILE) for e in (a[k], b[k], b[k + 1], a[k + 1])]
            mb.face(idx, STRUCT, uv, abs(n.z) < 0.7)
    # seam: radial plane at THETA0 between the end of the last turn (cols[-1]) and the start (cols[0])
    A, B = cols[-1], cols[0]
    loop = [e[0] for e in B] + [e[0] for e in reversed(A)]
    pts = [mb.v[x] for x in loop]
    tdir = Vector((-math.sin(THETA0), math.cos(THETA0), 0))
    n = face_normal(pts)
    if n.dot(tdir) < 0:
        loop.reverse()
        pts.reverse()
    mb.face(loop, STRUCT, [uv_box(p, tdir, TILE) for p in pts], False)
    # top seat and bottom
    top = [cols[i][-1][0] for i in range(ncol)]
    mb.face(top, STRUCT, [uv_box(mb.v[x], (0, 0, 1), TILE) for x in top], False)
    bot = [cols[i][0][0] for i in range(ncol)][::-1]
    mb.face(bot, STRUCT, [uv_box(mb.v[x], (0, 0, -1), TILE) for x in bot], False)
    return mb_weld(mb, 0.002)


def doorways(mb, q, lights, zmax=99.0, lit_fn=None, skip_fn=None):
    if q >= 3:
        return
    spacing = 13.0
    arc_total = 0.0
    n = 0
    # walk the ramp; doors sit in the inner wall (tier j + 1) just above the ledge
    steps = 600
    last = -spacing * 0.5
    for s in range(steps):
        phi = TAU * TURNS * (s + 0.5) / steps
        j = int(phi // TAU)
        z = ramp_z(phi)
        r = O(z) + 1.5
        arc_total = phi * r
        if arc_total - last < spacing:
            continue
        last = arc_total
        if z + 2.2 > min(ZT - 1.0, zmax) or z < 1.0:
            continue
        th = THETA0 + (phi % TAU)
        if skip_fn and skip_fn(th, z):
            continue
        n += 1
        lit = lit_fn(n) if lit_fn else True
        cell = 'doorglow' if lit else 'recess'
        w, h = 1.25, 2.1
        tp = phi % TAU
        rr = wall_r(tp, z + 1.0) + 0.04
        shape = [(-w / 2, 0.0), (w / 2, 0.0), (w / 2, h - w / 2)]
        segs = 4 if q == 0 else 2
        for k in range(1, segs):
            a = math.pi * k / segs
            shape.append((w / 2 * math.cos(a), h - w / 2 + w / 2 * math.sin(a)))
        shape.append((-w / 2, h - w / 2))
        idx, uvs = [], []
        for (x, y) in shape:
            a = th + x / rr
            zz = z + 0.05 + y
            rz = wall_r(tp, zz) + 0.05
            idx.append(mb.vert((rz * math.cos(a), rz * math.sin(a), zz), (0.0, 1.0 if lit else 0.0, 0.0, 1.0)))
            uvs.append(cell_uv(cell, (x + w / 2) / w, y / h))
        # front faces outward (+r): order shape as seen from outside (x grows with theta -> CCW from above)
        mb.face(idx, DETAIL, uvs, False)
        if lit and lights is not None:
            lights.append(dict(kind='lamp', pos=[round(rr * math.cos(th) * 1.02, 2), round(rr * math.sin(th) * 1.02, 2),
                                                 round(z + 1.0, 2)], radius_m=5.0, color='#ffad52'))


def terrace(mb, q, broken=None):
    segs = [96, 48, 24, 16][q]
    prof = [(RB + 2.2, -0.3), (RB + 2.0, 0.35), (RB + 1.75, 0.42), (RB - 1.0, 0.42)]
    th0, th1 = 0.0, TAU
    lathe(mb, prof, segs, STRUCT, uvfn=lambda i, th, j, r, z: (th * (RB + 2.0) / TILE, (z if j < 2 else (RB + 2.0 - r)) / TILE))


def jar(mb, pos, h, rng, q, lying=False):
    segs = [8, 6, 4, 4][q]
    prof = [(0.0001, 0.0), (0.28, 0.02), (0.45, 0.25), (0.52, 0.55), (0.48, 0.8), (0.3, 0.95), (0.2, 1.0), (0.23, 1.05)]
    if q > 0:
        prof = [prof[0], prof[1], prof[3], prof[5], prof[7]]
    prof = [(r * h, z * h) for r, z in prof]
    tmp = MB()
    lathe(tmp, prof, segs, DETAIL, uvfn=lambda i, th, j, r, z: cell_uv('terracotta', i / segs, z / h), cap_top=True)
    tmp.fuv[-1] = [cell_uv('recess', 0.5, 0.5)] * len(tmp.fuv[-1])
    if lying:
        m = Matrix.Translation(Vector(pos) + Vector((0, 0, h * 0.45))) @ Matrix.Rotation(rng.random() * TAU, 4, 'Z') @ Matrix.Rotation(math.pi / 2 - 0.1, 4, 'X')
    else:
        m = Matrix.Translation(Vector(pos)) @ Matrix.Rotation(rng.random() * TAU, 4, 'Z')
    mb.add(tmp.transformed(m))


def jars(mb, q, rng, keep=1.0, broken=0.0):
    n = [44, 44, 18, 0][q]
    k = 0
    # three clusters of jars on the terrace (front, left, right of the ramp start)
    for (ac, cnt) in ((-1.9, 16), (-1.2, 14), (-2.6, 14)):
        for i in range(cnt):
            if k >= n:
                return
            k += 1
            if rng.random() > keep:
                continue
            row = i % 2
            a = ac + (i // 2 - cnt / 4) * 0.055
            r = RB + 0.8 + row * 2.2
            zj = 0.42 if row == 0 else 0.0
            h = rng.uniform(1.3, 1.7)
            if broken and rng.random() < broken:
                jar(mb, (r * math.cos(a), r * math.sin(a), zj), h, rng, q, lying=True)
            else:
                jar(mb, (r * math.cos(a), r * math.sin(a), zj), h, rng, q)


def cap(q, mb=None, base_z=ZT):
    mb = mb or MB()
    segs = [64, 32, 16, 10][q]
    r0 = O(ZT) + 0.7
    rows = [0.0, 0.08, 0.2, 0.35, 0.5, 0.65, 0.8, 0.92, 1.0] if q == 0 else ([0, 0.2, 0.5, 0.8, 1.0] if q < 3 else [0, 0.5, 1.0])
    prof = [(r0 - 0.2, base_z - 0.6), (r0, base_z - 0.3), (r0 + 0.15, base_z + 0.2)]
    for t in rows[1:-1]:
        prof.append(((r0 + 0.1) * (1 - t) ** 1.12, base_z + 0.2 + (CAP_H - 0.2) * t))
    prof.append((0.08, base_z + CAP_H - 0.05))
    lathe(mb, prof, segs, ROOF, uvfn=lambda i, th, j, r, z: (th * r0 / 4.0 * 0 + i / segs * (TAU * r0 / 4.0), (z - base_z) / 4.0),
          cap_top=True, cap_bot=True)
    # plaster collar where the cap sits on the body
    if q < 3:
        lathe(mb, [(r0 - 0.3, ZT - 1.4), (r0 + 0.25, ZT - 1.1), (r0 + 0.3, ZT - 0.6), (r0 - 0.1, ZT - 0.4)], [64, 32, 16][q], STRUCT,
              uvfn=lambda i, th, j, r, z: (i / 8.0, z / TILE))
    return mb


def scaffold(mb, q, z0, z1, rng):
    """Timber scaffold ring round the working top of an unfinished tier."""
    if q >= 3:
        return
    n = [40, 20, 10][q]
    sides = [4, 3, 3][q]
    for i in range(n):
        th = TAU * i / n
        rr = f(2, z0) + 1.2
        p0 = (rr * math.cos(th), rr * math.sin(th), z0 - 6)
        p1 = (rr * 0.97 * math.cos(th), rr * 0.97 * math.sin(th), z1)
        cyl_between(mb, p0, p1, 0.13, 0.11, sides, DETAIL, cell='timber')
    if q < 2:
        for z in (z0 - 3, z0, z1 - 1.5):
            m = n
            for i in range(m):
                a, b = TAU * i / m, TAU * (i + 1) / m
                rr = f(2, z0) + 1.15
                cyl_between(mb, (rr * math.cos(a), rr * math.sin(a), z), (rr * math.cos(b), rr * math.sin(b), z), 0.09, 0.09, 3, DETAIL, cell='timber', caps=False)


def rubble(mb, center, radius, rng, q, n, cell='plaster', height=0.4):
    for k in range([n, n // 3, n // 8, 0][q]):
        a = rng.random() * TAU
        d = radius * math.sqrt(rng.random())
        s = 0.5 + rng.random() * 1.0
        z = max(0.0, 1 - d / radius) * radius * height
        rock(mb, (center[0] + d * math.cos(a), center[1] + d * math.sin(a), z), (s * 1.3, s, s * 0.8), rng,
             level=0 if q == 0 else -1, cell=cell)


def build(state, q):
    rng = random.Random(200)
    mb = MB()
    lights = []
    if state == 'Intact':
        mb.add(body(q))
        doorways(mb, q, lights)
        terrace(mb, q)
        jars(mb, q, rng)
        cap(q, mb)
        lights.append(dict(kind='glint', pos=[0, 0, ZT + CAP_H], radius_m=0, color='#ffffff', note='bronze tip: moonlight glint, no light'))
    elif state == 'Construction':
        zc = 22.0
        b = body(q)
        planes = [((0, 0, zc), (0.0, 0.0, 1.0)), ((0, 0, zc - 1.2), (0.12, 0.05, 1.0)), ((0, 0, zc - 0.8), (-0.08, -0.1, 1.0))]
        b = mb_cut_planes(b, planes, STRUCT, TILE)
        mb.add(b)
        doorways(mb, q, lights, zmax=zc - 3, lit_fn=lambda n: n % 3 == 0)
        terrace(mb, q)
        jars(mb, q, rng, keep=0.3)
        scaffold(mb, q, zc - 1.5, zc + 5.0, rng)
        # brick stacks and timber on the terrace / ground
        if q < 3:
            for k in range([10, 5, 2][q]):
                a = -0.4 + k * 0.33
                r = RB + 9 + rng.random() * 3
                block(mb, (r * math.cos(a), r * math.sin(a), 0.6), (1.4, 0.9, 0.6), a, DETAIL, cell='plaster')
    elif state == 'Damaged':
        b = body(q)
        ins, outs = mb_split_sector(b, -1.25, -0.15)
        ins = mb_cut_planes(ins, [((0, 0, 25.0), (0.3, -0.35, 1.0)), ((0, 0, 28.0), (-0.45, 0.1, 1.0)),
                                  ((14.0, -12.0, 19.0), (0.8, -0.7, 0.9))], STRUCT, TILE)
        mb.add(ins)
        for o in outs:
            mb.add(o)
        doorways(mb, q, lights, lit_fn=lambda n: n % 2 == 0,
                 skip_fn=lambda th, z: -1.3 < ((th + math.pi) % TAU - math.pi) < -0.1 and z > 17)
        terrace(mb, q)
        jars(mb, q, rng, keep=0.8, broken=0.3)
        # the fallen cap lies at the foot, tilted
        c = cap(q)
        m = Matrix.Translation(Vector((34.0, -20.0, 3.2))) @ Matrix.Rotation(-0.6, 4, 'Z') @ Matrix.Rotation(1.75, 4, 'Y') @ Matrix.Translation(Vector((0, 0, -ZT - 4.0)))
        mb.add(c.transformed(m))
        rubble(mb, (25.0, -21.0, 0), 11.0, rng, q, 70)
    elif state == 'Ruin':
        b = body(q)
        planes = [((0, 0, 17.0), (0.0, 0.0, 1.0)), ((0, 0, 13.0), (0.35, -0.2, 1.0)), ((0, 0, 15.0), (-0.3, 0.25, 1.0)),
                  ((0, 0, 11.0), (0.1, 0.5, 1.0))]
        b = mb_cut_planes(b, planes, STRUCT, TILE)
        ins, outs = mb_split_sector(b, -1.3, -0.1)
        ins = mb_cut_planes(ins, [((0, 0, 6.0), (0.3, -0.3, 1.0)), ((0, 0, 8.0), (-0.4, 0.2, 1.0)),
                                  ((16.0, -14.0, 3.0), (0.7, -0.7, 0.8))], STRUCT, TILE)
        mb.add(ins)
        for o in outs:
            mb.add(o)
        terrace(mb, q)
        jars(mb, q, rng, keep=0.25, broken=0.8)
        rubble(mb, (22.0, -19.0, 0), 14.0, rng, q, 90)
        rubble(mb, (-20.0, 15.0, 0), 10.0, rng, q, 50)
        rubble(mb, (5.0, 24.0, 0), 8.0, rng, q, 30)
    return mb, lights


def lod3(state):
    return build(state, 3)


CAMERAS = {
    'close': ((-6.0, -18.0, 12.0), 46.0, -20.0, 14.0, 30.0),
    'mid': ((0.0, 0.0, 22.0), 300.0, 32.0, 30.0, 35.0),
    'far': ((0.0, 0.0, 20.0), 1000.0, 32.0, 40.0, 35.0),
    'figure': (-2.0, -23.5, 0.6, 0.42),   # on the jar terrace, in frame (verifier fix)
}
