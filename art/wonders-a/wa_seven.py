# DEMIURGE art - wonders-a: wonder 2 (Iron age), the Seven Who Keep Watch.
# A round dry-stone platform 90 m across and 4 m high; on its rim seven faceless hooded granite figures,
# 35 m tall, facing outward, each holding an iron fire bowl (4 m) at chest height (~22 m).
import math
import random

from mathutils import Vector, Matrix

from wa_lib import (MB, STRUCT, ROOF, DETAIL, TAU, cell_uv, lathe, cyl_between, rock, block, flame, disc,
                    mb_cut_planes, box_face, uv_box, mat_trs, noise1)

ASSET = dict(
    key='seven', name='SevenWatchers', concept='wonder-2-iron', age=2, title='The Seven Who Keep Watch',
    mats=[('SevenPaving', 'Structure'), ('SevenGranite', 'Roof'), ('DetailA', 'Detail')],
    emissive_tint='#ff9a40', light_kind='fire bowl',
    states=['Intact', 'Construction', 'Damaged', 'Ruin'],
    day_emit=1.6,
)

PLAT_R, PLAT_H = 45.0, 4.0
FIG_H = 35.0
FIG_RING = 37.5
TILE = 8.0
FRONT = -math.pi / 2          # local front of a figure = -Y
BOWL_T = 0.63


def sstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def bump(t, a, m, b):
    if t <= a or t >= b:
        return 0.0
    if t < m:
        return sstep(a, m, t)
    return 1.0 - sstep(m, b, t)


# radius profile (normalised height t -> metres): robe hem, body, broad shoulders, a big rounded hood
PROFILE = [(0.0, 4.85), (0.03, 4.65), (0.15, 4.3), (0.35, 4.0), (0.5, 3.85), (0.6, 3.95), (0.7, 4.3),
           (0.762, 4.65), (0.785, 4.25), (0.805, 3.65), (0.83, 3.5), (0.88, 3.5), (0.93, 3.15), (0.965, 2.45),
           (0.985, 1.55), (0.997, 0.6), (1.0, 0.25)]
BOWL_T = 0.645


def base_r(t):
    for (t0, r0), (t1, r1) in zip(PROFILE, PROFILE[1:]):
        if t <= t1:
            s = (t - t0) / (t1 - t0)
            s = s * s * (3 - 2 * s)
            return r0 + (r1 - r0) * s
    return PROFILE[-1][1]


def dfront(th):
    return abs((th - FRONT + math.pi) % TAU - math.pi)


def fold_field(th, t):
    """Radial offset (m) of the robe folds; numpy-friendly (used by the texture too)."""
    try:
        import numpy as np
        cos, clip = np.cos, np.clip
    except ImportError:
        cos = math.cos
        clip = lambda x, a, b: min(b, max(a, x))
    amp = 0.62 * clip(1.0 - t / 0.72, 0.0, 1.0) ** 0.8 + 0.07
    return amp * (0.45 * cos(5 * th + 0.3) + 0.35 * cos(9 * th + 1.1) + 0.25 * cos(13 * th + 2.2)
                  + 0.12 * cos(21 * th + 0.7))


def radius(th, t):
    r = base_r(t)
    # oval: wider across the shoulders than front to back
    r *= 1.0 - 0.17 * (math.sin(th) ** 2) * sstep(0.15, 0.6, t) * (1 - 0.6 * sstep(0.78, 0.83, t))
    r += float(fold_field(th, t))
    df = dfront(th)
    # forearms meeting in front at chest height: a shelf the bowl rests on
    arm = bump(t, 0.56, 0.605, 0.648)
    r += 2.1 * arm * max(0.0, math.cos(min(math.pi / 2, df * 1.55)))
    # elbows and hanging sleeve cuffs either side of the front
    for s_ in (-0.95, 0.95):
        d = abs((th - (FRONT + s_) + math.pi) % TAU - math.pi)
        r += 0.75 * bump(t, 0.45, 0.6, 0.66) * max(0.0, 1 - d / 0.42)
    # hood: a deep opening in front (no face, only shadow), with a projecting rim
    hood = bump(t, 0.82, 0.885, 0.955)
    if df < 0.95:
        k = 1 - (df / 0.95) ** 2
        r -= 2.3 * hood * k
        r += 0.38 * sstep(0.55, 0.85, df / 0.95) * (1 - sstep(0.9, 1.0, df / 0.95)) * sstep(0.81, 0.84, t) * (1 - sstep(0.95, 0.975, t))
    return max(0.08, r)


def centre(t):
    """Posture: head bowed forward (toward -Y)."""
    fwd = 1.7 * sstep(0.75, 1.0, t) ** 1.3
    return Vector((0.0, -fwd, 0.0))


def figure(q, seed=0):
    mb = MB()
    nt = [64, 30, 14, 6][q]
    ns = [48, 24, 12, 6][q]
    rng = random.Random(seed)
    # rows denser near arms and hood
    ts = []
    for i in range(nt + 1):
        x = i / nt
        ts.append(x)
    if q == 0:
        extra = [0.5, 0.56, 0.585, 0.6, 0.615, 0.63, 0.645, 0.66, 0.74, 0.77, 0.79, 0.805, 0.825, 0.845, 0.865, 0.885, 0.905, 0.925, 0.945, 0.965, 0.985]
        ts = sorted(set([round(t, 4) for t in ts + extra]))
    z0 = -0.25
    rows, uvs = [], []
    for t in ts:
        c = centre(t)
        row, urow = [], []
        for i in range(ns):
            th = TAU * i / ns
            r = radius(th, t)
            z = z0 + t * (FIG_H - z0)
            row.append(mb.vert((c.x + r * math.cos(th), c.y + r * math.sin(th), z)))
            urow.append((i / ns, max(0.0, min(1.0, t))))
        rows.append(row)
        uvs.append(urow)
    for j in range(len(rows) - 1):
        for i in range(ns):
            i2 = (i + 1) % ns
            u1 = uvs[j][i][0]
            u2 = (i + 1) / ns
            mb.face([rows[j][i], rows[j][i2], rows[j + 1][i2], rows[j + 1][i]], ROOF,
                    [(u1, uvs[j][i][1]), (u2, uvs[j][i][1]), (u2, uvs[j + 1][i][1]), (u1, uvs[j + 1][i][1])], True)
    top = rows[-1]
    tip = mb.vert((centre(1.0).x, centre(1.0).y, FIG_H + 0.4))
    for i in range(ns):
        i2 = (i + 1) % ns
        mb.face([top[i], top[i2], tip], ROOF, [(i / ns, 0.995), ((i + 1) / ns, 0.995), ((i + 0.5) / ns, 1.0)], True)
    mb.face(list(reversed(rows[0])), ROOF, [(0.5, 0.0)] * ns, False)
    return mb


def bowl_pos():
    t = BOWL_T
    rf = radius(FRONT, t + 0.02)
    return Vector((0.0, -(rf + 0.9), t * FIG_H + 0.6))


def bowl(q, lit, rng, lights=None, world=None, phase=0.0):
    mb = MB()
    segs = [20, 12, 8, 6][q]
    prof = [(0.05, -1.25), (0.9, -1.1), (1.6, -0.65), (2.0, -0.05), (2.15, 0.1), (1.95, 0.12), (1.55, -0.38),
            (0.85, -0.75), (0.05, -0.85)]
    if q >= 2:
        prof = [prof[0], prof[2], prof[4], prof[5], prof[7], prof[8]]
    lathe(mb, prof, segs, DETAIL, uvfn=lambda i, th, j, r, z: cell_uv('iron', i / segs, j / (len(prof) - 1)))
    # three short legs/brackets under the bowl resting on the forearms
    if q < 2:
        for k in range(3):
            a = TAU * k / 3 + 0.5
            cyl_between(mb, (1.2 * math.cos(a), 1.2 * math.sin(a), -0.9), (0.9 * math.cos(a), 0.9 * math.sin(a), -2.0), 0.18, 0.14, 4, DETAIL, cell='iron')
    if lit:
        disc(mb, (0, 0, -0.42), 1.62, segs, DETAIL, 'fire', col=(phase, 1.0, 0, 1))
        if q < 3:
            n = [5, 3, 1][q]
            for k in range(n):
                a = TAU * k / max(1, n - 1) + rng.random() * 0.5
                d = 0.0 if k == 0 else 0.75 + rng.random() * 0.4
                flame(mb, (d * math.cos(a), d * math.sin(a), -0.5), (2.9 if k == 0 else 1.5 + rng.random() * 0.8), (1.05 if k == 0 else 0.62),
                      rng, sides=[6, 4, 3][q], lean=(rng.uniform(-0.1, 0.1), rng.uniform(-0.1, 0.1)), phase=phase)
    else:
        disc(mb, (0, 0, -0.42), 1.62, segs, DETAIL, 'charcoal')
    return mb


def platform(q, collapse=None):
    mb = MB()
    segs = [128, 64, 32, 16][q]
    R = PLAT_R
    prof = [(R + 0.7, -0.3), (R + 0.45, 0.2), (R + 0.05, PLAT_H - 0.45), (R + 0.35, PLAT_H - 0.4), (R + 0.35, PLAT_H),
            (R - 0.3, PLAT_H + 0.08)]
    if q >= 2:
        prof = [prof[0], prof[2], prof[4], prof[5]]

    def uvf(i, th, j, r, z):
        if j < 2:
            return (th * R / (TILE * 2.2), z / (TILE * 0.55))
        return (th * R / TILE, (z + r) / TILE)
    rows = lathe(mb, prof, segs, STRUCT, uvfn=uvf)
    ring = rows[-1]
    mb.face(list(ring), STRUCT, [uv_box(mb.v[k], (0, 0, 1), TILE) for k in ring], False)
    # stair up the back (+Y)
    if q < 3:
        n = [12, 6, 3][q]
        for s in range(n):
            h = PLAT_H * (s + 1) / n
            y0 = R + 6.0 * (1 - (s + 1) / n) + 0.2
            block(mb, (0.0, y0 + 0.5 * 6.0 / n, h / 2 - 0.15), (4.0, 0.5 * 6.0 / n + 0.02, h / 2 + 0.15), 0.0, STRUCT, tile=TILE)
        for sx in (-4.4, 4.4):
            block(mb, (sx, R + 3.0, PLAT_H / 2 - 0.1), (0.4, 3.2, PLAT_H / 2 + 0.2), 0.0, STRUCT, tile=TILE)
    return mb


def place(mb, part, k, scale=1.0, extra=None):
    a = TAU * k / 7 + FRONT          # figure 0 stands at the front, facing out (-Y)
    pos = Vector((FIG_RING * math.cos(a), FIG_RING * math.sin(a), PLAT_H))
    rot = a - FRONT
    m = Matrix.Translation(pos) @ Matrix.Rotation(rot, 4, 'Z') @ Matrix.Scale(scale, 4)
    if extra is not None:
        m = m @ extra
    mb.add(part.transformed(m))
    return m


def figure_cut(fig, t_cut, tilt=(0.0, 0.0), keep='below', jag=0.0, seed=0):
    z = t_cut * FIG_H
    rng = random.Random(seed)
    planes = []
    if keep == 'below':
        planes.append(((0, 0, z), (tilt[0], tilt[1], 1.0)))
        if jag:
            planes.append(((0, 0, z - jag), (rng.uniform(0.3, 0.6), rng.uniform(-0.4, 0.4), 1.0)))
            planes.append(((0, 0, z - jag * 0.6), (rng.uniform(-0.6, -0.3), rng.uniform(-0.4, 0.4), 1.0)))
    else:
        planes.append(((0, 0, z), (-tilt[0], -tilt[1], -1.0)))
    return mb_cut_planes(fig, planes, DETAIL, cap_cell='granite')


def scaffold_tower(mb, center, h, w, q):
    if q >= 3:
        return
    cx, cy = center
    sides = [4, 3, 3][q]
    for dx in (-w, w):
        for dy in (-w, w):
            cyl_between(mb, (cx + dx, cy + dy, PLAT_H), (cx + dx, cy + dy, PLAT_H + h), 0.15, 0.13, sides, DETAIL, cell='timber')
    if q < 2:
        z = PLAT_H + 3.0
        while z < PLAT_H + h:
            for (a, b) in (((-w, -w), (w, -w)), ((w, -w), (w, w)), ((w, w), (-w, w)), ((-w, w), (-w, -w))):
                cyl_between(mb, (cx + a[0], cy + a[1], z), (cx + b[0], cy + b[1], z), 0.1, 0.1, 3, DETAIL, cell='timber', caps=False)
            z += 4.0


def build(state, q):
    rng = random.Random(300)
    mb = MB()
    lights = []
    figs = [figure(q, seed=k) for k in range(1)]
    fig = figs[0]
    bp = bowl_pos()
    if state == 'Intact':
        mb.add(platform(q))
        for k in range(7):
            m = place(mb, fig, k, scale=1.0 + 0.02 * math.sin(k * 2.3))
            ph = rng.random()
            b = bowl(q, True, rng, phase=ph)
            mb.add(b.transformed(m @ Matrix.Translation(bp)))
            w = m @ (bp + Vector((0, 0, 1.5)))
            lights.append(dict(kind='fire', pos=[round(w.x, 2), round(w.y, 2), round(w.z, 2)], radius_m=30.0,
                               color='#ff9a40', phase=round(ph, 3)))
    elif state == 'Construction':
        mb.add(platform(q))
        for k in range(7):
            if k in (0, 1):
                m = place(mb, fig, k)
                b = bowl(q, k == 0, rng, phase=0.3)
                mb.add(b.transformed(m @ Matrix.Translation(bp)))
                if k == 0:
                    w = m @ (bp + Vector((0, 0, 1.5)))
                    lights.append(dict(kind='fire', pos=[round(w.x, 2), round(w.y, 2), round(w.z, 2)], radius_m=30.0, color='#ff9a40'))
            elif k in (2, 3, 4):
                t = [0.62, 0.42, 0.25][k - 2]
                part = figure_cut(fig, t)
                m = place(mb, part, k)
                a = TAU * k / 7 + FRONT
                c = (FIG_RING * math.cos(a), FIG_RING * math.sin(a))
                scaffold_tower(mb, c, t * FIG_H + 4.0, 5.5, q)
            else:
                a = TAU * k / 7 + FRONT
                if q < 3:
                    for s in range([6, 3, 1][q]):
                        block(mb, (FIG_RING * math.cos(a) + rng.uniform(-3, 3), FIG_RING * math.sin(a) + rng.uniform(-3, 3),
                                   PLAT_H + 0.9 + (s // 3) * 1.8), (1.6, 1.0, 0.9), rng.random(), DETAIL, cell='granite')
    elif state == 'Damaged':
        mb.add(platform(q))
        for k in range(7):
            a = TAU * k / 7 + FRONT
            if k in (2, 5):
                lower = figure_cut(fig, 0.4 if k == 2 else 0.52, tilt=(0.25, -0.1), keep='below', jag=2.0, seed=k)
                place(mb, lower, k)
                upper = figure_cut(fig, 0.4 if k == 2 else 0.52, tilt=(0.25, -0.1), keep='above')
                # the upper part fell inward onto the court, lying on its back
                t0 = (0.4 if k == 2 else 0.52) * FIG_H
                lie = (Matrix.Translation(Vector((0.0, 5.0 + 3.0 * (k == 5), 3.0))) @ Matrix.Rotation(0.25 if k == 2 else -0.4, 4, 'Z')
                       @ Matrix.Rotation(-math.pi / 2 + 0.06, 4, 'X') @ Matrix.Translation(Vector((0, 0, -t0))))
                place(mb, upper, k, extra=lie)
                b = bowl(q, False, rng)
                mb.add(b.transformed(Matrix.Translation(Vector(((FIG_RING - 12) * math.cos(a + 0.18), (FIG_RING - 12) * math.sin(a + 0.18), PLAT_H + 1.1)))
                                     @ Matrix.Rotation(1.9, 4, 'X')))
                if q < 3:
                    for s in range([16, 6, 2][q]):
                        d = rng.uniform(5, 16)
                        rock(mb, ((FIG_RING - d) * math.cos(a + rng.uniform(-0.12, 0.12)), (FIG_RING - d) * math.sin(a + rng.uniform(-0.12, 0.12)), PLAT_H + 0.3),
                             (rng.uniform(0.6, 1.4),) * 2 + (0.6,), rng, level=0 if q == 0 else -1, cell='granite')
            else:
                m = place(mb, fig, k)
                lit = k not in (3,)
                ph = rng.random()
                b = bowl(q, lit, rng, phase=ph)
                mb.add(b.transformed(m @ Matrix.Translation(bp)))
                if lit:
                    w = m @ (bp + Vector((0, 0, 1.5)))
                    lights.append(dict(kind='fire', pos=[round(w.x, 2), round(w.y, 2), round(w.z, 2)], radius_m=30.0, color='#ff9a40', phase=round(ph, 3)))
    elif state == 'Ruin':
        p = platform(q)
        # a sector of the revetment has slumped
        p = mb_cut_planes(p, [((PLAT_R - 6.0, 8.0, PLAT_H), (0.7, 0.2, 0.7))], STRUCT, TILE)
        mb.add(p)
        cuts = [0.22, 0.38, 0.15, 0.3, 0.46, 0.12, 0.27]
        for k in range(7):
            part = figure_cut(fig, cuts[k], tilt=(0.2 * math.sin(k * 2.1), 0.15 * math.cos(k * 1.3)), jag=2.5, seed=k + 10)
            place(mb, part, k)
        # three hooded heads lie in the court and below the platform
        head = figure_cut(fig, 0.7, keep='above')
        for (x, y, z, rz) in ((6.0, -10.0, PLAT_H + 2.0, 0.4), (-14.0, 8.0, PLAT_H + 2.0, 2.2), (49.0, 18.0, 2.1, 1.2)):
            m = Matrix.Translation(Vector((x, y, z))) @ Matrix.Rotation(rz, 4, 'Z') @ Matrix.Rotation(-math.pi / 2 + 0.1, 4, 'X') @ Matrix.Translation(Vector((0, 0, -0.85 * FIG_H)))
            mb.add(head.transformed(m))
        if q < 3:
            for s in range([60, 20, 6][q]):
                a = rng.random() * TAU
                d = rng.uniform(20, 52)
                z = PLAT_H + 0.4 if d < PLAT_R - 1 else 0.3
                rock(mb, (d * math.cos(a), d * math.sin(a), z), (rng.uniform(0.6, 1.6),) * 2 + (0.7,), rng, level=0 if q == 0 else -1, cell='granite')
            for s in range([40, 12, 4][q]):
                a = 0.25 + rng.uniform(-0.25, 0.25)
                d = rng.uniform(44, 52)
                rock(mb, (d * math.cos(a), d * math.sin(a), 0.4), (rng.uniform(0.5, 1.1),) * 2 + (0.5,), rng, level=0 if q == 0 else -1, cell='stone')
    return mb, lights


def lod3(state):
    mb, lights = build(state, 3)
    if state in ('Intact', 'Damaged', 'Construction'):
        # a bright billboard-ish tetra per lit bowl so the ring of seven reads at night from orbit
        for L in lights:
            x, y, z = L['pos']
            v = [mb.vert((x + dx, y + dy, z + dz), (0.5, 1.0, 0, 1)) for dx, dy, dz in ((1.6, 0, -1), (-0.8, 1.4, -1), (-0.8, -1.4, -1), (0, 0, 2.5))]
            for f in ((0, 1, 3), (1, 2, 3), (2, 0, 3), (0, 2, 1)):
                mb.face([v[i] for i in f], DETAIL, [cell_uv('fire', 0.5, 0.2)] * 3, True)
    return mb, lights


CAMERAS = {
    'close': ((0.0, -38.0, 21.0), 60.0, 22.0, 8.0, 30.0),   # whole hooded figure in frame (verifier fix)
    'mid': ((0.0, 0.0, 14.0), 300.0, 32.0, 32.0, 35.0),
    'far': ((0.0, 0.0, 14.0), 1000.0, 32.0, 40.0, 35.0),
    'figure': (-8.0, -32.0, 0.4, 4.08),   # on the platform beside the front figure (verifier fix)
}
