# DEMIURGE art - Age0 (Stone Age) family: asset generators.
#
# Every generator is   build(state, q) -> MB   with q = LOD (0 full, 1 ~40 %, 2 ~12 %, 3 hull or card)
# and state in STATES (only the states listed in the asset's spec are built).
# Front (door, open side, outer face of the palisade) is -Y. Pivot: footprint centre at ground (Z = 0).
import math
from a0_geo import (MB, TAU, STRUCT, ROOF, DETAIL, S_BARK, S_WOOD, S_WATTLE, S_EARTH, R_THATCH, R_HIDE, D_STONE,
                    D_LEATHER, D_PAINT, D_EMBER, D_CHAR, D_INTERIOR, D_ROPE, Rng, add, sub, mul, dot, cross, norm,
                    length, lerp3, rotz, rotx, roty, vnoise, fbm, grid, fan, tube, pole, rock, box, disc, lathe, mat_of)

STATES = ('Intact', 'Construction', 'Damaged', 'Ruin', 'Burnt')
DAMAGEABLE = STATES
INTACT_ONLY = ('Intact',)


# --------------------------------------------------------------------------- thatch and shells

def surf_n(surf, t, a, eps=1e-3):
    pa = sub(surf(t, a + eps), surf(t, a - eps))
    pt = sub(surf(min(1.0, t + eps), a), surf(max(0.0, t - eps), a))
    return norm(cross(pa, pt))


def thatch_courses(mb, surf, A, Um, tb, q, th, seed, wrap=True, keep=None, ribs=0, band=R_THATCH,
                   tmax=1.0, rings=None, lump=0.05, apex_closed=True, wave=0.14):
    """Stepped thatch courses laid over a base surface surf(t, a) (t 0 eave .. 1 top).
    A: parameter per column (wrap: len segs), Um: u metres per column (+1 for the seam when wrapping).
    tb: course breakpoints. th: lip thickness at the eave. keep(k, i, j) drops quads (damage)."""
    m = mat_of(band)
    K = len(tb) - 1
    cols = len(A)
    out = []
    for k in range(K):
        ta, tz = tb[k], tb[k + 1]
        d = tz - ta
        last = (k == K - 1)
        thk = th * (1.0 - 0.45 * (ta / max(tb[-1], 1e-6)))
        top_t = tz if last else min(tz + 0.22 * d, tmax)
        ftop = 0.0 if (last and apex_closed) else 0.3
        r = rings if rings is not None else {0: 5, 1: 4, 2: 3, 3: 2}[q]
        if r >= 5:
            spec = [(ta + 0.3 * d, -0.35), (ta, 0.35), (ta + 0.1 * d, 0.85), (ta + 0.5 * d, 0.8), (top_t, ftop)]
        elif r == 4:
            spec = [(ta, 0.4), (ta + 0.1 * d, 0.85), (ta + 0.55 * d, 0.8), (top_t, ftop)]
        elif r == 3:
            spec = [(ta, 0.5), (ta + 0.1 * d, 1.0), (top_t, ftop)]
        else:
            spec = [(ta, 0.7), (top_t, ftop if last else 0.5)]
        P, U = [], []
        for i, (t0_, f) in enumerate(spec):
            row, urow = [], []
            for j in range(cols + (1 if wrap else 0)):
                a = A[j % cols]
                t = t0_
                if wave and t0_ < 0.999:
                    ca, sa = (math.cos(a), math.sin(a)) if wrap else (a * 0.35, 0.0)
                    def wv(b):
                        if b >= K:
                            return 0.0
                        db = tb[min(b + 1, K)] - tb[b]
                        return db * wave * vnoise(ca * 2.2 + b * 1.7, sa * 2.2, b * 0.53, seed + 11)
                    fr = min(1.0, max(0.0, (t0_ - ta) / d))
                    t = min(tmax, max(0.0, t0_ + wv(k) * (1.0 - fr) + wv(k + 1) * fr))
                base = surf(t, a)
                n = surf_n(surf, t, a)
                apex = (t >= 0.999 and apex_closed)
                bump = 0.0
                if not apex and f > 0.5:
                    if ribs:
                        bump += thk * 0.35 * (abs(math.cos(ribs * a * 0.5)) ** 0.6 - 0.55)
                    bump += lump * fbm(base[0] * 1.4, base[1] * 1.4, base[2] * 1.4 + k * 3.1, seed, 2)
                p = base if apex else add(base, mul(n, thk * f + bump))
                if i == (1 if r >= 5 else 0):
                    p = (p[0], p[1], p[2] - 0.035)
                if j < cols:
                    row.append(p)
                if t0_ <= ta + 1e-6 or f < 0:
                    vm = 0.0
                else:
                    vm = length(sub(surf(t, a), surf(ta, a)))
                urow.append(band.uv(Um[j] + k * 1.37, vm))
            P.append(row); U.append(urow)
        kk = (lambda i, j, k=k: keep(k, i, j)) if keep else None
        out.append(grid(mb, P, U, m, keep=kk, wrap=wrap))
    return out


def shell(mb, surf, A, Um, t0, t1, rows, off, band=D_INTERIOR, wrap=True, inward=True, keep=None):
    """A plain surface following surf, offset along the normal by `off`. inward=True flips it to face the inside."""
    m = mat_of(band)
    cols = len(A)
    P, U = [], []
    ts = [t0 + (t1 - t0) * i / rows for i in range(rows + 1)]
    if inward:
        ts = ts[::-1]
    for t in ts:
        row, urow = [], []
        for j in range(cols + (1 if wrap else 0)):
            a = A[j % cols]
            p = add(surf(t, a), mul(surf_n(surf, t, a), off))
            if j < cols:
                row.append(p)
            if band.u0 is not None:
                urow.append(band.uv(j / max(1, cols), (t - t0) / max(1e-6, t1 - t0) * band.hm))
            else:
                urow.append(band.uv(Um[j], length(sub(surf(t, a), surf(t0, a)))))
        P.append(row); U.append(urow)
    return grid(mb, P, U, m, wrap=wrap, keep=keep)


def frame_ribs(mb, surf, n, q, seed, t0=0.0, t1=0.97, off=-0.07, r=0.045, cut=None, lean=None, a0=0.0):
    """Bent-pole frame under the cover. cut(k) -> top t for broken ribs (None keeps it whole)."""
    steps = {0: 8, 1: 5, 2: 3, 3: 2}[q]
    sides = {0: 6, 1: 5, 2: 4, 3: 3}[q]
    rng = Rng(seed)
    for k in range(n):
        a = a0 + TAU * k / n + rng.r(-0.08, 0.08)
        tt = cut(k) if cut else t1
        if tt is None or tt <= t0 + 0.05:
            continue
        pts = []
        for i in range(steps + 1):
            t = t0 + (tt - t0) * i / steps
            p = add(surf(t, a), mul(surf_n(surf, t, a), off))
            if lean:
                p = lean(k, p, (t - t0) / max(1e-6, tt - t0))
            pts.append(p)
        pts[0] = (pts[0][0], pts[0][1], pts[0][2] - 0.35)
        tube(mb, pts, [r * (1.0 - 0.3 * i / steps) for i in range(steps + 1)], sides, S_BARK, cap1=True, seed=seed + k)


def hoop(mb, c, rx, ry, z, r, segs, band=D_ROPE, sides=4, seed=0, wob=0.0):
    pts = []
    for j in range(segs + 1):
        a = TAU * j / segs
        w = 1.0 + wob * vnoise(math.cos(a) * 2, math.sin(a) * 2, z, seed)
        pts.append((c[0] + math.cos(a) * rx * w, c[1] + math.sin(a) * ry * w, z))
    tube(mb, pts, [r] * len(pts), sides, band, seed=seed, rough=0.0)


def bundle(mb, c, length_, r, yaw, q, seed, band=R_THATCH, tilt=0.0):
    """A bound sheaf of thatch lying on the ground."""
    d = rotz((length_ * 0.5, 0.0, 0.0), yaw)
    d = (d[0], d[1], d[2] + tilt)
    a = sub(c, d); b = add(c, d)
    sides = {0: 7, 1: 5, 2: 4, 3: 3}[q]
    tube(mb, [a, lerp3(a, b, 0.5), b], [r * 0.85, r, r * 0.8], sides, band, cap0=False, cap1=False, seed=seed, rough=0.15)


def rand_angles(segs, a0=0.0):
    return [a0 + TAU * j / segs for j in range(segs)]


def round_um(R, segs, mu=4.0):
    ut = max(1, round(TAU * R / mu)) * mu
    return [ut * j / segs for j in range(segs + 1)]


# --------------------------------------------------------------------------- shared hut pieces

def footing(mb, R, segs, band=S_EARTH, rx=1.0, ry=1.0, h=0.12):
    prof = [(R + 0.18, -0.3), (R + 0.2, -0.03), (R + 0.08, h), (R - 0.2, h + 0.03)]
    lathe(mb, prof, segs, band, rx=rx, ry=ry)


def round_surf(Rb, H, z0, pr=2.2, pe=0.55, sag=None):
    def f(t, a):
        t = min(max(t, 0.0), 1.0)
        r = Rb * max(0.0, 1.0 - t ** pr) ** pe
        z = z0 + H * t
        if sag:
            r, z = sag(t, a, r, z)
        return (math.cos(a) * r, math.sin(a) * r, z)
    return f


def cone_surf(Re, z0, z1, k=1.15, sag=None):
    def f(t, a):
        t = min(max(t, 0.0), 1.0)
        r = Re * (1.0 - t) ** k
        z = z0 + (z1 - z0) * t
        if sag:
            r, z = sag(t, a, r, z)
        return (math.cos(a) * r, math.sin(a) * r, z)
    return f


def arch_ring(hw, hz, zs, y, n, scale=1.0):
    """Door arch outline (left ground -> over the top -> right ground) in the plane y."""
    pts = [(-hw * scale, y, -0.05)]
    for i in range(n + 1):
        th = math.pi - math.pi * i / n
        pts.append((math.cos(th) * hw * scale, y, zs + math.sin(th) * hz * scale))
    pts.append((hw * scale, y, -0.05))
    return pts


def porch(mb, q, R, w=1.2, h=1.5, depth=0.55, band=R_THATCH, flap=True, torn=False, frame_only=False, seed=0):
    """Arched entrance tunnel pushing out of a dome at the front (-Y), with a dark night-lit opening."""
    hw = w * 0.5
    zs = h - hw
    n = {0: 7, 1: 5, 2: 3, 3: 2}[q]
    y_in = -R * 0.72
    y_out = -(R + depth)
    if not frame_only:
        rows = [arch_ring(hw - 0.12, hz=hw - 0.12, zs=zs, y=y_out + 0.04, n=n),
                arch_ring(hw + 0.1, hz=hw + 0.1, zs=zs, y=y_out, n=n)]
        rows.append(arch_ring(hw + 0.14, hz=hw + 0.16, zs=zs, y=y_out + depth * 0.5, n=n))
        rows.append(arch_ring(hw + 0.16, hz=hw + 0.2, zs=zs, y=y_in, n=n))
        U = []
        for i, row in enumerate(rows):
            vm = [0.0, 0.02, depth * 0.5, depth + 0.5][i]
            s, urow = 0.0, []
            for j, p in enumerate(row):
                if j:
                    s += length(sub(p, row[j - 1]))
                urow.append(band.uv(s + 0.7, vm))
            U.append(urow)
        if q >= 2:
            rows = [rows[1], rows[3]]; U = [U[1], U[3]]
        grid(mb, rows, U, mat_of(band))
        # opening (dark, lit at night)
        ring = arch_ring(hw - 0.1, hz=hw - 0.1, zs=zs, y=y_out + 0.22, n=n)
        c = (0.0, y_out + 0.22, zs * 0.55)
        ruv = [D_INTERIOR.uv(0.5 + p[0] / (2 * hw), (p[2] / h) * D_INTERIOR.hm) for p in ring]
        fan(mb, c, ring, D_INTERIOR.uv(0.5, 0.3 * D_INTERIOR.hm), ruv, DETAIL, flip=True)
    if flap and q <= 1:
        z = h - 0.18
        sides = 6 if q == 0 else 4
        tube(mb, [(-hw + 0.05, y_out - 0.05, z), (0.0, y_out - 0.09, z + 0.02), (hw - 0.05, y_out - 0.05, z)],
             [0.07, 0.085, 0.07], sides, D_LEATHER, cap0=True, cap1=True, seed=seed)
        if not torn:
            # half-drawn hide hanging down one side
            x0, x1 = -hw + 0.02, -0.05
            P = [[(x0, y_out - 0.06, 0.05), (x1, y_out - 0.08, 0.15)], [(x0, y_out - 0.07, z), (x1 + 0.1, y_out - 0.1, z)]]
            Uu = [[D_LEATHER.uv(0, 0), D_LEATHER.uv(0.4, 0)], [D_LEATHER.uv(0, 0.45), D_LEATHER.uv(0.4, 0.45)]]
            # two-sided: front and back
            grid(mb, [[P[0][1], P[0][0]], [P[1][1], P[1][0]]], [[Uu[0][1], Uu[0][0]], [Uu[1][1], Uu[1][0]]], DETAIL)
            Pb = [[add(p, (0, 0.015, 0)) for p in row] for row in P]
            grid(mb, Pb, Uu, DETAIL)
    if q <= 1:
        for sx in (-1, 1):
            pole(mb, (sx * (hw + 0.02), y_out - 0.02, -0.25), (sx * (hw - 0.02), y_out - 0.02, zs + 0.05), 0.055, 0.05,
                 sides=5 if q == 0 else 4, seed=seed + sx)


def topknot(mb, z, q, seed, r=0.3, hgt=0.55):
    sides = {0: 9, 1: 6, 2: 5, 3: 4}[q]
    tube(mb, [(0, 0, z - 0.3), (0, 0, z + hgt * 0.4), (0.02, 0.01, z + hgt)], [r, r * 0.75, r * 0.45], sides, R_THATCH,
         cap1=True, seed=seed, rough=0.12)
    if q == 0:
        hoop(mb, (0, 0, 0), r * 0.82, r * 0.82, z + hgt * 0.25, 0.03, 9, seed=seed)


def scatter_bundles(mb, n, R0, R1, q, seed, band=R_THATCH, side=None, z=0.05):
    rng = Rng(seed)
    for k in range(n):
        a = rng.r(0, TAU) if side is None else side + rng.r(-0.9, 0.9)
        rr = rng.r(R0, R1)
        bundle(mb, (math.cos(a) * rr, math.sin(a) * rr, z + 0.1), rng.r(0.9, 1.5), rng.r(0.1, 0.16), rng.r(0, TAU), q,
               seed + k, band=band, tilt=rng.r(-0.05, 0.05))


def ash_bed(mb, R, q, seed, h=0.25):
    segs = {0: 20, 1: 12, 2: 8, 3: 6}[q]
    rings = {0: 4, 1: 2, 2: 1, 3: 1}[q]
    P, U = [], []
    for i in range(rings + 1):
        rr = R * (1.0 - i / rings)
        zz = h * (1.0 - (rr / R) ** 2) - 0.05
        row, urow = [], []
        for j in range(segs + 1):
            a = TAU * j / segs
            x, y = math.cos(a) * rr, math.sin(a) * rr
            dz = 0.08 * fbm(x * 0.9 + seed, y * 0.9, 0.0, seed, 2) * (rr / R) if i else 0.0
            if j < segs:
                row.append((x, y, zz + dz))
            urow.append(D_CHAR.uv(0.5 + x / (2 * R), (0.5 + y / (2 * R)) * D_CHAR.hm))
        P.append(row); U.append(urow)
    grid(mb, P, U, DETAIL, wrap=True)


def debris_poles(mb, n, R, q, seed, band=S_BARK, charred=False, ry=None):
    """ry: half-depth (m) of the scatter in Y for long plans (longhouses), so debris stays on the building's own plot."""
    rng = Rng(seed)
    sy = 1.0 if ry is None else min(1.0, ry / (R * 1.1))
    for k in range(n):
        a = rng.r(0, TAU); rr = rng.r(R * 0.3, R * 1.1)
        c = (math.cos(a) * rr, math.sin(a) * rr * sy, 0.08)
        L = rng.r(1.5, 3.2)
        d = rotz((L * 0.5, 0, 0), rng.r(0, TAU))
        pole(mb, sub(c, (d[0], d[1], -0.02)), add(c, (d[0], d[1], rng.r(0.0, 0.5))), rng.r(0.04, 0.07), sides={0: 5, 1: 4, 2: 3, 3: 3}[q],
             band=band, seed=seed + k, caps=(True, True))


# --------------------------------------------------------------------------- huts (round)

HUT_SPECS = {
    # dome of thatch courses to the ground (the concept's main hut)
    'A': dict(kind='dome', R=2.3, H=3.3, z0=0.06, pr=1.7, pe=0.65, ribs=18, th=0.14),
    # conical thatch on a wattle-and-daub drum, pole ends at the top
    'B': dict(kind='cone', R=2.45, Re=3.0, eave=1.15, apex=4.55, drum=1.35, ribs=0, th=0.16),
    # hide-covered bender dome with a thatch cap and a smoke hole
    'C': dict(kind='hide', R=2.7, H=2.75, z0=0.06, pr=2.0, pe=0.5, ribs=12, th=0.15),
}

Q_SEGS_HUT = {0: 36, 1: 28, 2: 16, 3: 8}


def hut(var, state, q):
    sp = HUT_SPECS[var]
    mb = MB()
    seed = {'A': 101, 'B': 202, 'C': 303}[var]
    segs = Q_SEGS_HUT[q]
    a0 = -math.pi / 2.0                       # a vertex column faces the front
    A = rand_angles(segs, a0)
    R = sp['R']
    Um = round_um(R, segs)
    rng = Rng(seed + 7)
    burnt = state == 'Burnt'
    ruin = state == 'Ruin'

    # ---- base surface for this state
    if sp['kind'] == 'cone':
        def sag(t, a, r, z):
            if state == 'Damaged':
                w = max(0.0, math.cos(a + 0.85)) ** 2
                z -= 0.35 * w * math.sin(math.pi * t) + 0.25 * w * t
            if ruin:
                z = 0.35 + (z - 0.35) * 0.35
            return r, z
        surf = cone_surf(sp['Re'], sp['eave'], sp['apex'], sag=sag)
    else:
        def sag(t, a, r, z):
            if state == 'Damaged':
                w = max(0.0, math.cos(a + 0.85)) ** 2
                z -= 0.4 * w * math.sin(math.pi * min(1.0, t * 1.2)) + 0.15 * w * t
            if ruin:
                z = sp['z0'] + (z - sp['z0']) * 0.32
                r *= 1.12
            return r, z
        surf = round_surf(R, sp['H'], sp['z0'], sp['pr'], sp['pe'], sag=sag)

    # ---- footing / drum
    if q <= 2 or sp['kind'] == 'cone':
        footing(mb, R if sp['kind'] != 'cone' else R + 0.05, max(8, segs // (1 if q < 2 else 1)))
    if sp['kind'] == 'cone':
        dh = sp['drum'] if state in ('Intact', 'Damaged', 'Construction') else (0.7 if ruin else 0.55)
        keepd = None
        if state == 'Construction':
            keepd = lambda i, j: (j / segs) < 0.6 or i == 0
        prof = [(R, -0.3), (R, dh * 0.5), (R, dh)] if q <= 1 else [(R, -0.3), (R, dh)]
        lathe(mb, prof, segs, S_WATTLE, vmetres=[0.0, (dh + 0.3) * 0.5 * 0.74, (dh + 0.3) * 0.74][:len(prof)] if q <= 1 else [0.0, (dh + 0.3) * 0.74], keep=keepd,
              disp=(lambda a, i: 1.0 + 0.012 * vnoise(math.cos(a) * 3, math.sin(a) * 3, i, seed)) if q == 0 else None)
        # wall top rim
        lathe(mb, [(R, dh), (R - 0.12, dh + 0.04), (R - 0.24, dh)], segs if q <= 1 else max(8, segs // 2), S_WATTLE)
        # inner face so a ruined / unroofed drum is not see-through
        if state != 'Intact' or q == 0:
            lathe(mb, [(R - 0.24, dh), (R - 0.24, -0.1)], max(8, segs // 2), S_WATTLE)

    # ---- covering
    ribs_n = {0: 12, 1: 8, 2: 6, 3: 4}[q]
    if sp['kind'] == 'cone':
        tb = {0: [0, .16, .32, .47, .61, .74, .86, 1.0], 1: [0, .2, .4, .6, .8, 1.0], 2: [0, .33, .66, 1.0], 3: [0, 1.0]}[q]
    elif sp['kind'] == 'dome':
        tb = {0: [0, .17, .33, .48, .62, .75, .87, 1.0], 1: [0, .2, .4, .58, .75, .9, 1.0], 2: [0, .3, .58, .82, 1.0], 3: [0, .5, 1.0]}[q]
    else:
        tb = {0: [.5, .66, .82, .955], 1: [.5, .73, .955], 2: [.5, .955], 3: [.5, .955]}[q]

    def hole(k, i, j):
        a = A[j % segs]
        t = tb[k]
        d1 = math.hypot((((a + 0.85 + math.pi) % TAU) - math.pi) * 1.1, (t - 0.55) * 2.2)
        d2 = math.hypot((((a + 2.45 + math.pi) % TAU) - math.pi) * 1.3, (t - 0.62) * 2.5)
        nz = 0.12 * vnoise(math.cos(a) * 3, math.sin(a) * 3, t * 4, seed)
        return d1 > 0.42 + nz and d2 > 0.28 + nz

    keep = None
    tb_use = tb
    if state == 'Damaged':
        keep = hole
    elif state == 'Construction':
        tb_use = tb[:max(2, len(tb) // 2)]
        lastk = len(tb_use) - 2
        keep = lambda k, i, j: k < lastk or (j / segs) < 0.55
    elif ruin:
        keep = lambda k, i, j: (vnoise(math.cos(A[j % segs]) * 2.5, math.sin(A[j % segs]) * 2.5, k * 0.9, seed + 3) > 0.05)

    th = sp['th']
    if not burnt:
        if sp['kind'] == 'hide':
            # hide panels sag between the ribs on the lower dome
            nrib = sp['ribs']
            def hsurf(t, a):
                p = surf(t, a)
                s = 1.0 - 0.035 * (1.0 - abs(math.cos(nrib * (a - a0) * 0.5))) * math.sin(math.pi * min(1.0, t / 0.62))
                return (p[0] * s, p[1] * s, p[2])
            hrows = {0: 7, 1: 5, 2: 3, 3: 2}[q]
            hk = None
            if state == 'Damaged':
                hk = lambda i, j: hole(0, i, j) if i > 1 else True
            if state == 'Construction':
                hk = lambda i, j: (j / segs) < 0.5 or i < 2
            if ruin:
                hk = lambda i, j: vnoise(math.cos(A[j]) * 2, math.sin(A[j]) * 2, i * 0.7, seed) > -0.1
            shell(mb, hsurf, A, Um, 0.0, 0.66, hrows, 0.02, band=R_HIDE, inward=False, keep=hk)
            # hem lip
            lathe(mb, [(R + 0.06, -0.08), (R + 0.03, sp['z0'] + 0.05)], segs, R_HIDE)
        thatch_courses(mb, surf, A, Um, tb_use, q, th, seed, keep=keep, ribs=sp['ribs'] if sp['kind'] != 'hide' else 0,
                       apex_closed=(sp['kind'] != 'hide'), lump=0.05 if q <= 1 else 0.0)

    # ---- apex
    if state == 'Intact' or (state == 'Damaged' and q <= 2):
        if sp['kind'] == 'dome':
            topknot(mb, surf(1.0, 0)[2], q, seed)
        elif sp['kind'] == 'cone':
            if q <= 2:
                z1 = surf(1.0, 0)[2]
                for k in range({0: 6, 1: 5, 2: 3}[q]):
                    a = a0 + 0.3 + TAU * k / {0: 6, 1: 5, 2: 3}[q]
                    pole(mb, (math.cos(a) * 0.5, math.sin(a) * 0.5, z1 - 1.0), (math.cos(a + 0.4) * -0.32, math.sin(a + 0.4) * -0.32, z1 + 0.85),
                         0.06, 0.04, sides={0: 5, 1: 4, 2: 3}[q], seed=seed + k)
                topknot(mb, z1 - 0.05, q, seed, r=0.26, hgt=0.3)
        else:
            # smoke hole collar + dark opening (lit at night)
            tt = tb[-1]
            p = surf(tt, 0.0)
            rr = math.hypot(p[0], p[1])
            sg = max(8, segs // 2)
            lathe(mb, [(rr + 0.12, p[2] - 0.05), (rr + 0.05, p[2] + 0.12), (rr - 0.08, p[2] + 0.1)], sg, R_THATCH)
            disc(mb, (0, 0, 0), rr - 0.05, sg, D_INTERIOR, z=p[2] - 0.15)
            if q <= 1:
                hoop(mb, (0, 0, 0), rr + 0.08, rr + 0.08, p[2] + 0.04, 0.035, sg, seed=seed)

    # ---- hide-dome extras: lashing ropes and hem stones
    if sp['kind'] == 'hide' and state in ('Intact', 'Damaged') and q <= 1:
        for tt in (0.22, 0.45):
            p = surf(tt, 0.0)
            rr = math.hypot(p[0], p[1]) + 0.03
            hoop(mb, (0, 0, 0), rr, rr, p[2], 0.03, segs, seed=seed + int(tt * 100))
    if sp['kind'] == 'hide' and state != 'Burnt' and q <= 1:
        ns = 10 if q == 0 else 6
        for k in range(ns):
            a = a0 + TAU * (k + 0.5) / ns
            if abs(((a - a0 + math.pi) % TAU) - math.pi) < 0.35:
                continue
            rock(mb, (math.cos(a) * (R + 0.15), math.sin(a) * (R + 0.15), 0.0), (0.38, 0.3, 0.22), seed + k * 13,
                 segs=6 if q == 0 else 5, rings=3)

    # ---- door
    if sp['kind'] in ('dome', 'hide') and state in ('Intact', 'Damaged', 'Construction') and q <= 2:
        porch(mb, q, R, band=R_THATCH if sp['kind'] == 'dome' else R_HIDE, flap=state != 'Construction',
              torn=state == 'Damaged', frame_only=state == 'Construction', seed=seed)
    if sp['kind'] == 'cone' and state in ('Intact', 'Damaged', 'Construction') and q <= 2:
        cone_door(mb, q, R, seed, flap=state == 'Intact')

    # ---- frame under / instead of the cover
    if state in ('Construction', 'Damaged', 'Burnt', 'Ruin') and q <= 2:
        n = {0: 10, 1: 7, 2: 5}[q]
        if sp['kind'] == 'cone':
            fs = cone_surf(sp['Re'] - 0.25, sp['eave'] - 0.6, sp['apex'] - 0.1)
        else:
            fs = round_surf(R - 0.05, sp['H'] - 0.08, -0.1, sp['pr'], sp['pe'])
        cut = None
        lean = None
        if state == 'Burnt':
            cut = lambda k: [0.95, 0.4, 0.7, None, 0.55, 0.85, 0.3, None, 0.62, 0.45][k % 10]
        if state == 'Ruin':
            cut = lambda k: [0.5, None, 0.35, 0.6, None, 0.3, None, 0.45, None, 0.25][k % 10]
            lean = lambda k, p, f: (p[0] * (1 + 0.5 * f), p[1] * (1 + 0.5 * f), p[2] * (1 - 0.45 * f))
        if True:
            frame_ribs(mb, fs, n, q, seed, t0=0.0, t1=0.96, off=-0.02, cut=cut, lean=lean, a0=a0 + 0.2)
        if state == 'Construction' and q <= 1:
            for tt in (0.25, 0.5, 0.72):
                p = fs(tt, 0.0)
                rr = math.hypot(p[0], p[1])
                hoop(mb, (0, 0, 0), rr, rr, p[2], 0.035, max(10, segs // 2), band=S_BARK, sides=4, seed=seed + int(tt * 50))
    if state == 'Damaged' and q <= 2:
        # inner lining (underside of the thatch) so the holes read as holes, not as see-through
        shell(mb, surf, A, Um, 0.0, 0.97, {0: 6, 1: 4, 2: 3}[q], -0.06, band=R_THATCH, inward=True)

    # ---- ground clutter per state
    if state == 'Construction' and q <= 2:
        scatter_bundles(mb, {0: 7, 1: 4, 2: 2}[q], R + 0.7, R + 1.6, q, seed + 50, side=0.4)
        if q <= 1:
            debris_poles(mb, 3, R + 1.0, q, seed + 60)
    if state == 'Damaged' and q <= 2:
        scatter_bundles(mb, {0: 4, 1: 2, 2: 1}[q], R + 0.2, R + 1.2, q, seed + 70, side=0.6)
    if ruin:
        if q <= 2:
            scatter_bundles(mb, {0: 5, 1: 3, 2: 1}[q], R * 0.6, R + 1.0, q, seed + 80)
            debris_poles(mb, {0: 4, 1: 2, 2: 1}[q], R, q, seed + 90)
    if burnt:
        ash_bed(mb, R + 0.3, q, seed, h=0.3)
        if q <= 2:
            debris_poles(mb, {0: 6, 1: 3, 2: 2}[q], R, q, seed + 100)
    return mb


def cone_door(mb, q, R, seed, flap=True):
    y = -R - 0.02
    w, h = 0.95, 1.25
    # dark opening on the drum face (night lit)
    P = [[(w * 0.5, y - 0.005, -0.05), (-w * 0.5, y - 0.005, -0.05)], [(w * 0.5, y - 0.005, h), (-w * 0.5, y - 0.005, h)]]
    U = [[D_INTERIOR.uv(1, 0), D_INTERIOR.uv(0, 0)], [D_INTERIOR.uv(1, D_INTERIOR.hm), D_INTERIOR.uv(0, D_INTERIOR.hm)]]
    # rows bottom->top, columns right->left seen from the front: normal faces -Y
    grid(mb, [[P[0][1], P[0][0]], [P[1][1], P[1][0]]], [[U[0][1], U[0][0]], [U[1][1], U[1][0]]], DETAIL)
    sides = {0: 6, 1: 5, 2: 4, 3: 3}[q]
    for sx in (-1, 1):
        pole(mb, (sx * (w * 0.5 + 0.06), y - 0.06, -0.3), (sx * (w * 0.5 + 0.06), y - 0.06, h + 0.12), 0.07, 0.06, sides=sides, seed=seed + sx)
    pole(mb, (-w * 0.5 - 0.25, y - 0.1, h + 0.12), (w * 0.5 + 0.25, y - 0.1, h + 0.12), 0.065, 0.06, sides=sides, seed=seed + 5, caps=(True, True))
    if flap and q <= 1:
        x0, x1 = 0.02, w * 0.5
        Pf = [[(x1, y - 0.03, 0.0), (x0, y - 0.05, 0.1)], [(x1, y - 0.03, h), (x0 - 0.05, y - 0.04, h)]]
        Uf = [[D_LEATHER.uv(0.4, 0), D_LEATHER.uv(0, 0)], [D_LEATHER.uv(0.4, 0.45), D_LEATHER.uv(0, 0.45)]]
        grid(mb, Pf, Uf, DETAIL)


# --------------------------------------------------------------------------- lean-to

def leanto(state, q, var='A'):
    mb = MB()
    seed = 404
    W = 4.7
    rng = Rng(seed)
    burnt, ruin = state == 'Burnt', state == 'Ruin'
    sides = {0: 6, 1: 5, 2: 4, 3: 3}[q]
    yr, zr = -0.95, 2.25                       # ridge line
    yb, zb = 1.75, -0.05                       # back foot of the rafters
    if q == 3:
        # hull: the cover as one slab and two uprights
        P = [[(W * 0.5, yb, zb), (-W * 0.5, yb, zb)], [(W * 0.5, yr - 0.15, zr + 0.15), (-W * 0.5, yr - 0.15, zr + 0.15)]]
        if burnt or ruin:
            P = [[(W * 0.5, yb, zb), (-W * 0.5, yb, zb)], [(W * 0.5, yr + 0.8, 0.6), (-W * 0.5, yr + 0.8, 0.6)]]
        U = [[R_THATCH.uv(0, 0), R_THATCH.uv(W, 0)], [R_THATCH.uv(0, 1.9), R_THATCH.uv(W, 1.9)]]
        grid(mb, P, U, DETAIL if burnt else ROOF)
        # underside: the slab is single-sided, so from the open front it vanished (verifier fix, +2 tris; 2 cm below the
        # top so the weld in build_age0 does not merge it away as a duplicate face)
        grid(mb, [[(p[0], p[1], p[2] - 0.02) for p in row[::-1]] for row in P], [u[::-1] for u in U], DETAIL if burnt else ROOF)
        if not (burnt or ruin):
            for sx in (-1, 1):
                pole(mb, (sx * 2.1, yr, -0.3), (sx * 2.1, yr, zr), 0.08, 0.07, sides=3, seed=seed + sx)
        return mb
    standing = not (ruin or burnt)
    # uprights with forks
    for sx in (-1, 1):
        top = zr if standing else (zr * 0.6 if burnt else None)
        if ruin and sx > 0:
            pole(mb, (sx * 2.1, yr - 0.4, 0.05), (sx * 1.0, yr - 1.6, 0.25), 0.08, 0.07, sides=sides, seed=seed + 11, caps=(True, True))
            continue
        if top is None:
            top = 1.3
        pole(mb, (sx * 2.1, yr, -0.3), (sx * 2.1, yr, top), 0.085, 0.07, sides=sides, segs=2, bend=0.03, seed=seed + sx)
        if standing and q <= 1:
            for fx in (-1, 1):
                pole(mb, (sx * 2.1, yr, zr - 0.25), (sx * 2.1 + fx * 0.18, yr - 0.03, zr + 0.25), 0.05, 0.035, sides=4, seed=seed + 3 * sx + fx)
    # ridge pole
    if standing:
        pole(mb, (-W * 0.52, yr, zr + 0.02), (W * 0.52, yr, zr + 0.02), 0.08, 0.07, sides=sides, segs=3, bend=0.04, seed=seed + 20, caps=(True, True))
    elif ruin:
        pole(mb, (-W * 0.5, yr + 0.2, 0.1), (W * 0.45, yr - 0.5, 0.95), 0.08, 0.07, sides=sides, segs=2, seed=seed + 20, caps=(True, True))
    # rafters
    nr = {0: 9, 1: 6, 2: 4}[q]
    for k in range(nr):
        x = -W * 0.45 + W * 0.9 * k / (nr - 1)
        if burnt:
            f = [0.55, 0.3, 0.8, 0.0, 0.45, 0.65, 0.2, 0.5, 0.35][k % 9]
            if f <= 0.05:
                continue
            a = (x * 1.03, yb, zb - 0.2); b = (x, yr + 0.1, zr + 0.1)
            pole(mb, a, lerp3(a, b, f), 0.05, 0.045, sides=max(3, sides - 1), seed=seed + 30 + k)
            continue
        if ruin:
            a = (x * 1.03, yb, zb - 0.1); b = (x + rng.r(-0.4, 0.4), yr + 0.6, 0.35 + rng.r(0, 0.4))
        else:
            a = (x * 1.03, yb + 0.05, zb - 0.3); b = (x, yr - 0.12, zr - 0.02)
        pole(mb, a, b, 0.05, 0.04, sides=max(3, sides - 1), seed=seed + 30 + k)

    # cover: thatch courses laid bottom (back, on the ground) -> top (front ridge)
    def surf(t, a):
        x = -a
        y = yb + (yr - 0.15 - yb) * t
        z = zb + 0.08 + (zr + 0.1 - zb - 0.08) * t
        if ruin:
            z = zb + 0.08 + (0.55 - zb) * t * (1.0 - 0.25 * t)
            y = yb + (yr + 0.7 - yb) * t
        z -= 0.05 * (1.0 - abs(math.cos(nr * 0.5 * math.pi * (x / (W * 0.45) + 1.0) * 0.5 * 2))) * math.sin(math.pi * t)
        return (x, y, z)
    ncol = {0: 18, 1: 12, 2: 6}[q]
    A = [-W * 0.5 + W * j / (ncol - 1) for j in range(ncol)]
    Um = [a + W * 0.5 for a in A]
    tb = {0: [0, .34, .68, 1.0], 1: [0, .5, 1.0], 2: [0, 1.0]}[q]
    if not burnt:
        keep = None
        if state == 'Construction':
            keep = lambda k, i, j: k == 0 and j < ncol * 0.7
        if state == 'Damaged':
            keep = lambda k, i, j: not (k >= 1 and 0.3 < j / ncol < 0.62)
        if ruin:
            keep = lambda k, i, j: vnoise(j * 0.6, k * 1.3, 0.5, seed) > -0.2
        thatch_courses(mb, surf, A, Um, tb, q, 0.14, seed, wrap=False, keep=keep, apex_closed=False, lump=0.04 if q <= 1 else 0.0)
        # underside (seen from the open front)
        under = lambda i, j: True
        if state == 'Construction':
            under = lambda i, j: j < ncol * 0.7 and i == 0
        if state == 'Damaged':
            under = lambda i, j: not (i >= 1 and 0.3 < j / ncol < 0.62)
        if ruin:
            under = lambda i, j: False
        if not ruin:
            shell(mb, surf, A, Um, 0.0, 1.0, {0: 3, 1: 2, 2: 1}[q], -0.03, band=R_THATCH, wrap=False, inward=True, keep=under)
        # hides weighted on top of the thatch
        if q <= 1 and state in ('Intact', 'Damaged'):
            for k, (x0, x1, t0, t1) in enumerate(((-2.0, -0.4, 0.5, 0.98), (0.6, 2.1, 0.35, 0.9))):
                if state == 'Damaged' and k == 1:
                    continue
                rows = []
                urows = []
                nn = 3 if q == 0 else 2
                for i in range(nn + 1):
                    t = t0 + (t1 - t0) * i / nn
                    row, ur = [], []
                    for jx in range(nn + 1):
                        x = x0 + (x1 - x0) * jx / nn
                        p = surf(t, -x)
                        p = add(p, (0, 0.06 * 0.7, 0.2 + 0.02 * vnoise(x * 2, t * 4, 0, seed)))
                        row.append(p)
                        ur.append(R_HIDE.uv(x - x0, (t - t0) * 3.0))
                    rows.append(row[::-1]); urows.append(ur[::-1])
                grid(mb, rows, urows, ROOF)
    else:
        ash_bed(mb, 2.0, q, seed, h=0.15)
    # bedding hide and a small hearth in front
    if state in ('Intact', 'Damaged') and q <= 1:
        P = [[(-1.3, 0.8, 0.09), (1.2, 0.8, 0.09)], [(-1.3, -0.6, 0.09), (1.2, -0.6, 0.09)]]
        U = [[D_LEATHER.uv(0, 0), D_LEATHER.uv(2.5, 0)], [D_LEATHER.uv(0, 0.45), D_LEATHER.uv(2.5, 0.45)]]
        grid(mb, [[P[0][1], P[0][0]], [P[1][1], P[1][0]]], [[U[0][1], U[0][0]], [U[1][1], U[1][0]]], DETAIL)
    if state in ('Intact', 'Damaged', 'Construction') and q <= 2:
        n = {0: 7, 1: 5, 2: 4}[q]
        for k in range(n):
            a = TAU * k / n
            rock(mb, (0.3 + math.cos(a) * 0.55, -2.0 + math.sin(a) * 0.45, 0.0), (0.3, 0.25, 0.2), seed + 40 + k, segs=5, rings=3)
        disc(mb, (0.3, -2.0, 0.0), 0.42, 8, D_EMBER if state == 'Intact' else D_CHAR, z=0.07)
    if state == 'Construction' and q <= 2:
        scatter_bundles(mb, {0: 5, 1: 3, 2: 1}[q], 2.5, 3.2, q, seed + 50, side=math.pi / 2)
    if ruin and q <= 2:
        debris_poles(mb, {0: 3, 1: 2, 2: 1}[q], 2.0, q, seed + 60)
    if burnt and q <= 2:
        debris_poles(mb, {0: 5, 1: 3, 2: 2}[q], 1.8, q, seed + 70)
    return mb


# --------------------------------------------------------------------------- longhouse

LONG_SPECS = {'A': dict(L=16.0, seed=505), 'B': dict(L=22.0, seed=606)}


def superellipse(a, b, n, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (a * math.copysign(abs(c) ** (2.0 / n), c), b * math.copysign(abs(s) ** (2.0 / n), s))


def arc_angles(a, b, n, segs, a0=-math.pi / 2):
    """Angles that space `segs` points evenly by arc length on a superellipse (starting at the front, -Y)."""
    M = 2000
    pts = [superellipse(a, b, n, a0 + TAU * i / M) for i in range(M + 1)]
    cum = [0.0]
    for i in range(1, M + 1):
        cum.append(cum[-1] + math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]))
    tot = cum[-1]
    out, k = [], 0
    for j in range(segs):
        target = tot * j / segs
        while cum[k + 1] < target:
            k += 1
        f = (target - cum[k]) / max(1e-9, cum[k + 1] - cum[k])
        out.append(a0 + TAU * (k + f) / M)
    return out, tot


def longhouse(var, state, q):
    sp = LONG_SPECS[var]
    L = sp['L']
    seed = sp['seed']
    mb = MB()
    burnt, ruin = state == 'Burnt', state == 'Ruin'
    aw, bw = L * 0.5, 3.0                     # wall plan
    ae, be = L * 0.5 + 0.75, 3.75             # eave plan
    ar, br = L * 0.5 - 2.6, 0.12              # ridge
    ze, zr = 1.05, 5.6
    segs = {0: int(48 + L * 1.5), 1: int(30 + L), 2: int(16 + L * 0.5), 3: 12}[q]
    A, perim = arc_angles(ae, be, 2.6, segs)
    ut = max(1, round(perim / 4.0)) * 4.0
    # u metres per column from arc length on the eave ring
    Um = []
    for j in range(segs + 1):
        Um.append(ut * j / segs)

    def surf(t, ang):
        t = min(max(t, 0.0), 1.0)
        tt = 1.0 - (1.0 - t) ** 1.25
        a_ = ae + (ar - ae) * t
        b_ = be + (br - be) * tt
        n_ = 2.6 + 1.4 * t
        x, y = superellipse(a_, b_, n_, ang)
        z = ze + (zr - ze) * (1.0 - (1.0 - t) ** 1.08)
        if state == 'Damaged':
            w = max(0.0, math.cos(ang + 1.0)) ** 3
            z -= 0.6 * w * math.sin(math.pi * t)
        if ruin:
            z = 0.3 + (z - 0.3) * 0.3
        return (x, y, z)

    # walls (bowed wattle and daub) and footing
    wsegs = {0: segs, 1: segs, 2: max(12, segs // 2), 3: 10}[q]
    WA, wper = arc_angles(aw, bw, 2.6, wsegs)
    wut = max(1, round(wper / 4.0)) * 4.0
    wh = {'Intact': 1.35, 'Construction': 1.35, 'Damaged': 1.35, 'Ruin': 0.7, 'Burnt': 0.5}[state]
    def wallring(z, off=0.0):
        return [(superellipse(aw + off, bw + off, 2.6, a)[0], superellipse(aw + off, bw + off, 2.6, a)[1], z) for a in WA]
    rows = [wallring(-0.3), wallring(wh * 0.5), wallring(wh)] if q <= 1 else [wallring(-0.3), wallring(wh)]
    vms = [0.0, (wh + 0.3) * 0.37, (wh + 0.3) * 0.74] if q <= 1 else [0.0, (wh + 0.3) * 0.74]
    U = [[S_WATTLE.uv(wut * j / wsegs, vm) for j in range(wsegs + 1)] for vm in vms]
    wkeep = None
    if state == 'Construction':
        wkeep = lambda i, j: j / wsegs < 0.7
    if ruin:
        wkeep = lambda i, j: i == 0 or vnoise(j * 0.4, i, 0.3, seed) > -0.3
    grid(mb, rows, U, STRUCT, wrap=True, keep=wkeep)
    # wall top + inner face (unroofed states and LOD0)
    if state != 'Intact' or q == 0:
        inner = [wallring(wh, -0.22), wallring(-0.1, -0.22)]
        top = [wallring(wh), wallring(wh + 0.03, -0.11), wallring(wh, -0.22)]
        grid(mb, top, [[S_WATTLE.uv(wut * j / wsegs, 0.74 + 0.05 * i) for j in range(wsegs + 1)] for i in range(3)], STRUCT, wrap=True, keep=wkeep)
        grid(mb, inner, [[S_WATTLE.uv(wut * j / wsegs, 0.05 + 0.6 * i) for j in range(wsegs + 1)] for i in range(2)], STRUCT, wrap=True)
    # footing plinth
    if q <= 2:
        fr = [wallring(-0.3, 0.3), wallring(0.0, 0.3), wallring(0.12, 0.12)]
        grid(mb, fr, [[S_EARTH.uv(wut * j / wsegs, 0.3 * i) for j in range(wsegs + 1)] for i in range(3)], STRUCT, wrap=True)

    # roof
    tb = {0: [0, .14, .28, .42, .56, .7, .84, 1.0], 1: [0, .2, .4, .6, .8, 1.0], 2: [0, .35, .7, 1.0], 3: [0, 1.0]}[q]
    def hole(k, i, j):
        a = A[j % segs]
        p = surf(tb[k], a)
        d1 = math.hypot((p[0] - L * 0.18) / 2.4, (p[1] + 1.6) / 1.4)
        d2 = math.hypot((p[0] + L * 0.25) / 1.6, (p[1] - 1.2) / 1.1)
        nz = 0.25 * vnoise(p[0] * 0.8, p[1] * 0.8, 0, seed)
        return d1 > 1.0 + nz and d2 > 1.0 + nz
    keep = None
    tb_use = tb
    if state == 'Damaged':
        keep = hole
    if state == 'Construction':
        tb_use = tb[:max(2, (len(tb) + 1) // 2)]
        lastk = len(tb_use) - 2
        keep = lambda k, i, j: k < lastk or (j / segs) < 0.5
    if ruin:
        keep = lambda k, i, j: vnoise(math.cos(A[j % segs]) * 4, math.sin(A[j % segs]) * 4, k * 0.8, seed) > -0.2
    if not burnt:
        thatch_courses(mb, surf, A, Um, tb_use, q, 0.2, seed, keep=keep, ribs=0, apex_closed=False,
                       lump=0.07 if q <= 1 else 0.0, rings=None)
        if state in ('Intact', 'Damaged'):
            # ridge roll with lashings, smoke vents at both ends
            sides = {0: 10, 1: 7, 2: 5, 3: 4}[q]
            zt = surf(1.0, 0.0)[2]
            n = {0: 8, 1: 5, 2: 2, 3: 1}[q]
            pts = [(-ar - 0.3 + (2 * ar + 0.6) * i / n, 0.0, zt + 0.1 - (0.35 if state == 'Damaged' and abs(i / n - 0.6) < 0.25 else 0.0)) for i in range(n + 1)]
            tube(mb, pts, [0.32] * len(pts), sides, R_THATCH, cap0=True, cap1=True, seed=seed, rough=0.1)
            if q <= 1:
                for k in range(int(L / 2.2)):
                    x = -ar + 0.6 + (2 * ar - 1.2) * k / max(1, int(L / 2.2) - 1)
                    ring = [(x, math.cos(TAU * i / 8) * 0.34, zt + 0.1 + math.sin(TAU * i / 8) * 0.34) for i in range(9)]
                    tube(mb, ring, [0.03] * 9, 4, D_ROPE, seed=seed + k)
            if q <= 2:
                for sx in (-1, 1):
                    # small dark vent triangle under the ridge end (night lit)
                    xv = sx * (ar + 0.15)
                    a_ = (xv, -0.45, zt - 0.55); b_ = (xv, 0.45, zt - 0.55); c_ = (xv + sx * 0.12, 0.0, zt - 0.05)
                    ids = [mb.v(a_), mb.v(b_), mb.v(c_)]
                    uv = [D_INTERIOR.uv(0, 0), D_INTERIOR.uv(1, 0), D_INTERIOR.uv(0.5, D_INTERIOR.hm)]
                    mb.f(ids if sx < 0 else ids[::-1], uv if sx < 0 else uv[::-1], DETAIL)
    else:
        # burnt: ash over the floor
        segs_a = {0: 28, 1: 16, 2: 10, 3: 8}[q]
        P, U = [], []
        for i, f in enumerate((1.0, 0.6, 0.0)):
            row, urow = [], []
            for j in range(segs_a + 1):
                ang = TAU * j / segs_a
                x, y = superellipse(aw * f + 0.01, bw * f + 0.01, 2.6, ang)
                z = 0.25 * (1.0 - f) + 0.02 + (0.06 * fbm(x * 0.5, y * 0.5, 0, seed, 2) if i == 1 else 0.0)
                if j < segs_a:
                    row.append((x, y, z))
                urow.append(D_CHAR.uv(0.5 + x / (2.2 * aw), (0.5 + y / (2.2 * bw)) * D_CHAR.hm))
            P.append(row); U.append(urow)
        grid(mb, P, U, DETAIL, wrap=True)

    # posts: ridge posts inside, porch at the front
    if state in ('Construction', 'Burnt', 'Ruin', 'Damaged') and q <= 2:
        nposts = max(3, int(L / 5))
        for k in range(nposts):
            x = -ar + 2 * ar * k / (nposts - 1)
            top = zr + 0.1
            if burnt:
                top = [3.8, 1.6, 4.5, 2.4, 3.1][k % 5]
            if ruin:
                top = [1.4, None, 2.2, 0.9, None][k % 5]
            if top is None:
                continue
            if state == 'Damaged':
                top = zr - 0.25
            pole(mb, (x, 0, -0.3), (x, 0.0, top), 0.13, 0.11, sides={0: 7, 1: 5, 2: 4}[q], seed=seed + k)
        if state in ('Construction', 'Burnt', 'Damaged'):
            # rafters: pairs from the wall plates up to the ridge
            nraf = {0: int(L / 1.4), 1: int(L / 2.2), 2: int(L / 4)}[q]
            for k in range(nraf):
                x = -ar + 2 * ar * k / max(1, nraf - 1)
                for sy in (-1, 1):
                    a_ = (x, sy * (bw + 0.5), ze - 0.45)
                    b_ = (x, sy * 0.15, zr - 0.25)
                    f = 1.0
                    if burnt:
                        f = [0.5, 0.0, 0.8, 0.3, 0.0, 0.65, 0.2][(k * 2 + (sy > 0)) % 7]
                        if f <= 0.0:
                            continue
                    if state == 'Construction' and x > ar * 0.3:
                        continue
                    if state == 'Damaged' and k % 2:
                        continue
                    pole(mb, a_, lerp3(a_, b_, f), 0.08, 0.07, sides={0: 5, 1: 4, 2: 3}[q], seed=seed + 40 + k * 2 + (sy > 0))
            if not burnt:
                pole(mb, (-ar, 0, zr - 0.15), (ar * (0.4 if burnt else 1.0), 0, zr - 0.15), 0.1, 0.09, sides={0: 6, 1: 5, 2: 4}[q], seed=seed + 99,
                     caps=(True, True))
    if state in ('Intact', 'Damaged', 'Construction') and q <= 2:
        long_porch(mb, q, bw, seed, state)
    if state == 'Damaged' and q <= 2:
        shell(mb, surf, A, Um, 0.0, 1.0, {0: 3, 1: 3, 2: 2}[q], -0.1, band=R_THATCH, inward=True)
    if state == 'Construction' and q <= 2:
        rng = Rng(seed)
        for k in range({0: 10, 1: 5, 2: 3}[q]):
            x = L * 0.2 + rng.r(-2.5, 3.5)
            y = -bw - 2.2 + rng.r(-0.6, 0.6)
            bundle(mb, (x, y, 0.12 + 0.18 * (k % 3)), rng.r(1.0, 1.5), 0.14, rng.r(-0.3, 0.3), q, seed + k)
    if ruin and q <= 2:
        scatter_bundles(mb, {0: 8, 1: 4, 2: 2}[q], bw, bw + 2.0, q, seed + 80)
        debris_poles(mb, {0: 6, 1: 3, 2: 2}[q], L * 0.4, q, seed + 90, ry=bw + 0.3)
    if burnt and q <= 2:
        debris_poles(mb, {0: 8, 1: 4, 2: 2}[q], L * 0.35, q, seed + 95, ry=bw + 0.3)
    return mb


def long_porch(mb, q, bw, seed, state):
    """Gabled entrance porch on the long front (-Y) with a dark doorway."""
    w, d = 2.6, 1.7
    y0 = -bw + 0.1
    y1 = -bw - d
    ze, zr = 1.4, 2.65
    sides = {0: 6, 1: 5, 2: 4}[q]
    for sx in (-1, 1):
        pole(mb, (sx * (w * 0.5 - 0.2), y1 + 0.25, -0.3), (sx * (w * 0.5 - 0.2), y1 + 0.25, ze + 0.05), 0.08, 0.07, sides=sides, seed=seed + 7 + sx)
    pole(mb, (-w * 0.5 - 0.1, y1 + 0.25, ze + 0.12), (w * 0.5 + 0.1, y1 + 0.25, ze + 0.12), 0.07, 0.07, sides=sides, seed=seed + 9, caps=(True, True))
    # doorway (dark, night lit) on the wall
    dw, dh = 1.0, 1.75
    yd = -bw - 0.04
    P = [[(-dw * 0.5, yd, -0.05), (dw * 0.5, yd, -0.05)], [(-dw * 0.5, yd, dh), (dw * 0.5, yd, dh)]]
    U = [[D_INTERIOR.uv(0, 0), D_INTERIOR.uv(1, 0)], [D_INTERIOR.uv(0, D_INTERIOR.hm), D_INTERIOR.uv(1, D_INTERIOR.hm)]]
    grid(mb, P, U, DETAIL)
    if state == 'Construction':
        return
    # gable roof: two thatch planes with a lip, plus undersides
    for sx in (-1, 1):
        rows = []
        n = 3 if q <= 1 else 2
        for i in range(n + 1):
            f = i / n
            x = sx * (w * 0.5 + 0.35) * (1.0 - f) + sx * 0.0 * f
            z = ze + (zr - ze) * f
            rows.append([(x, y0 + 0.4, z + 0.12 * (1 - f)), (x, y1 - 0.15, z)] if sx < 0 else [(x, y1 - 0.15, z), (x, y0 + 0.4, z + 0.12 * (1 - f))])
        U = [[R_THATCH.uv(0.3, f * 1.6), R_THATCH.uv(0.3 + d + 0.55, f * 1.6)] for f in [i / n for i in range(n + 1)]]
        grid(mb, rows, U, ROOF)
        under = [[add(p, (0, 0, -0.08)) for p in row[::-1]] for row in rows]
        grid(mb, under, [u[::-1] for u in U], ROOF)
    # gable triangle face (hide)
    a_ = (-w * 0.5 - 0.3, y1 - 0.1, ze + 0.05); b_ = (w * 0.5 + 0.3, y1 - 0.1, ze + 0.05); c_ = (0.0, y1 - 0.1, zr - 0.05)
    ids = [mb.v(a_), mb.v(b_), mb.v(c_)]
    mb.f([ids[2], ids[1], ids[0]], [R_HIDE.uv(w * 0.5, 1.2), R_HIDE.uv(w + 0.6, 0), R_HIDE.uv(0, 0)], ROOF)


# --------------------------------------------------------------------------- fire pit

def firepit(state, q):
    mb = MB()
    seed = 707
    rng = Rng(seed)
    ns = {0: 12, 1: 9, 2: 7, 3: 0}[q]
    for k in range(ns):
        a = TAU * k / ns + rng.r(-0.1, 0.1)
        rr = 1.05 + rng.r(-0.05, 0.08)
        rock(mb, (math.cos(a) * rr, math.sin(a) * rr, 0.0), (rng.r(0.35, 0.5), rng.r(0.28, 0.38), rng.r(0.22, 0.34)), seed + k,
             segs={0: 6, 1: 5, 2: 4}[q], rings={0: 4, 1: 3, 2: 2}[q], yaw=a + rng.r(-0.3, 0.3), vlift=0.7)
    if q == 3:
        disc(mb, (0, 0, 0), 1.3, 8, D_STONE, z=0.12)
        disc(mb, (0, 0, 0), 0.8, 6, D_EMBER, z=0.15)
        return mb
    # ash and embers
    disc(mb, (0, 0, 0), 0.95, {0: 14, 1: 10, 2: 7}[q], D_CHAR, z=0.04, noise=0.06, seed=seed, rings=2 if q == 0 else 1)
    disc(mb, (0, 0, 0), 0.55, {0: 12, 1: 8, 2: 6}[q], D_EMBER, z=0.1, noise=0.05, seed=seed + 1, rings=2 if q == 0 else 1)
    # log tipi, charred at the foot
    nl = {0: 6, 1: 4, 2: 3}[q]
    for k in range(nl):
        a = TAU * k / nl + 0.3
        b = (math.cos(a) * 0.62, math.sin(a) * 0.62, 0.08)
        t = (math.cos(a + 0.5) * 0.06, math.sin(a + 0.5) * 0.06, 0.95)
        pole(mb, b, lerp3(b, t, 0.45), 0.075, 0.07, sides={0: 6, 1: 5, 2: 4}[q], band=S_BARK, seed=seed + 20 + k, caps=(True, False))
        pole(mb, lerp3(b, t, 0.45), t, 0.07, 0.05, sides={0: 6, 1: 5, 2: 4}[q], seed=seed + 30 + k, caps=(False, True))
    # seat logs
    if q <= 1:
        for k in range(3):
            a = TAU * k / 3 + 0.9
            c = (math.cos(a) * 2.3, math.sin(a) * 2.3, 0.18)
            d = rotz((0.85, 0, 0), a + math.pi / 2)
            pole(mb, sub(c, d), add(c, d), 0.2, 0.19, sides=7 if q == 0 else 5, segs=2, bend=0.03, seed=seed + 40 + k, caps=(True, True))
    # cooking tripod
    if q == 0:
        top = (0.0, 0.0, 1.9)
        for k in range(3):
            a = TAU * k / 3 + 0.2
            pole(mb, (math.cos(a) * 1.35, math.sin(a) * 1.35, -0.1), top, 0.035, 0.03, sides=4, seed=seed + 60 + k)
        tube(mb, [(0, 0, 1.92), (0.02, 0, 1.4), (0, 0.02, 1.15)], [0.012] * 3, 3, D_ROPE, seed=seed)
    return mb


# --------------------------------------------------------------------------- drying racks

def hide_panel(mb, corners, nx, ny, seed, band=D_LEATHER, sag=0.06, two_sided=True, ragged=0.08):
    """Stretched hide between four corners (bl, br, tr, tl), front facing the corner winding normal."""
    bl, br, tr, tl = corners
    rng = Rng(seed)
    P, U = [], []
    for i in range(ny + 1):
        fy = i / ny
        row, urow = [], []
        for j in range(nx + 1):
            fx = j / nx
            p = lerp3(lerp3(bl, br, fx), lerp3(tl, tr, fx), fy)
            nrm = norm(cross(sub(br, bl), sub(tl, bl)))
            s = sag * math.sin(math.pi * fx) * math.sin(math.pi * fy)
            p = add(p, mul(nrm, -s))
            if i in (0, ny) or j in (0, nx):
                p = add(p, (rng.r(-ragged, ragged) * 0.5, rng.r(-ragged, ragged) * 0.5, rng.r(-ragged, ragged)))
            row.append(p)
            urow.append(band.uv(fx * length(sub(br, bl)), fy * min(band.hm - 0.02, length(sub(tl, bl)))))
        P.append(row); U.append(urow)
    m = mat_of(band)
    grid(mb, P, U, m)
    if two_sided:
        nrm = norm(cross(sub(br, bl), sub(tl, bl)))
        Pb = [[add(p, mul(nrm, -0.012)) for p in row[::-1]] for row in P]
        Ub = [row[::-1] for row in U]
        grid(mb, Pb, Ub, m)


def dryingrack(var, state, q):
    mb = MB()
    seed = 808 if var == 'A' else 909
    sides = {0: 5, 1: 4, 2: 3, 3: 3}[q]
    if var == 'A':
        # two A-frames and a cross bar with two stretched hides on lashed frames
        for sx in (-1, 1):
            for sy in (-1, 1):
                pole(mb, (sx * 1.55, sy * 0.55, -0.25), (sx * 1.5, sy * 0.06, 2.25), 0.045, 0.035, sides=sides, seed=seed + sx * 3 + sy)
        pole(mb, (-1.85, 0, 2.12), (1.85, 0, 2.12), 0.045, 0.04, sides=sides, segs=2 if q <= 1 else 1, bend=0.03, seed=seed + 9, caps=(True, True))
        if q <= 2:
            for k, x0 in enumerate((-1.25, 0.05)):
                w = 1.15
                fr = [(x0, -0.02, 0.4), (x0 + w, -0.02, 0.45), (x0 + w - 0.02, 0.0, 2.0), (x0 + 0.03, 0.0, 1.98)]
                if q <= 1:
                    for a_, b_ in ((0, 1), (1, 2), (2, 3), (3, 0)):
                        pole(mb, fr[a_], fr[b_], 0.025, 0.025, sides=3, seed=seed + 20 + k * 4 + a_, caps=(False, False))
                inset = [add(fr[0], (0.08, -0.02, 0.08)), add(fr[1], (-0.08, -0.02, 0.08)), add(fr[2], (-0.08, -0.02, -0.08)), add(fr[3], (0.08, -0.02, -0.08))]
                hide_panel(mb, inset, 4 if q == 0 else 2, 5 if q == 0 else 2, seed + k, band=D_PAINT if (k == 1 and q <= 1) else D_LEATHER, sag=0.04)
        else:
            hide_panel(mb, [(-1.3, -0.02, 0.5), (1.3, -0.02, 0.5), (1.3, -0.02, 2.0), (-1.3, -0.02, 2.0)], 1, 1, seed, sag=0.0)
    else:
        # long rack: two tripods carrying a pole with hanging hides and strips
        L = 5.0
        for sx in (-1, 1):
            for k in range(3):
                a = TAU * k / 3 + (0.3 if sx > 0 else 0.9)
                pole(mb, (sx * L * 0.5 + math.cos(a) * 0.55, math.sin(a) * 0.55, -0.25), (sx * L * 0.5, 0.0, 1.95), 0.04, 0.03, sides=sides, seed=seed + 3 * k + sx)
        pole(mb, (-L * 0.5 - 0.3, 0, 1.92), (L * 0.5 + 0.3, 0, 1.92), 0.05, 0.045, sides=sides, segs=3 if q <= 1 else 1, bend=0.06, seed=seed + 9, caps=(True, True))
        if q <= 2:
            for k, (x0, x1, drop) in enumerate(((-2.1, -0.9, 1.25), (-0.6, 0.7, 1.45), (1.0, 2.0, 1.1))):
                if q == 2 and k == 1:
                    continue
                # hide draped over the pole: two panels
                for sy in (-1, 1):
                    top = 1.94
                    c = [(x0, sy * 0.05, top), (x1, sy * 0.05, top), (x1 - 0.05, sy * 0.25, top - drop), (x0 + 0.05, sy * 0.22, top - drop * 0.9)]
                    c = [c[3], c[2], c[1], c[0]] if sy < 0 else [c[2], c[3], c[0], c[1]]
                    hide_panel(mb, c, 3 if q == 0 else 1, 3 if q == 0 else 1, seed + k * 2 + (sy > 0), sag=-0.03, two_sided=(q == 0), ragged=0.06)
            if q == 0:
                rng = Rng(seed)
                for k in range(9):
                    x = -2.2 + 4.4 * k / 8 + rng.r(-0.1, 0.1)
                    if any(x0 - 0.1 < x < x1 + 0.1 for x0, x1, _ in ((-2.1, -0.9, 0), (-0.6, 0.7, 0), (1.0, 2.0, 0))):
                        continue
                    tube(mb, [(x, 0, 1.9), (x + 0.02, 0.03, 1.4), (x, 0.0, 1.05)], [0.05, 0.06, 0.03], 3, D_LEATHER, seed=seed + k, cap1=True)
        else:
            hide_panel(mb, [(-2.0, -0.02, 0.6), (2.0, -0.02, 0.6), (2.0, -0.02, 1.9), (-2.0, -0.02, 1.9)], 1, 1, seed, sag=0.0)
    return mb


# --------------------------------------------------------------------------- palisade

PAL_LEN = 8.0


def palisade_logs(mb, x0, x1, q, seed, state, hmin=3.1, hmax=3.9, y=0.0):
    rng = Rng(seed)
    d = 0.29
    n = max(1, int(round((x1 - x0) / d)))
    step = (x1 - x0) / n
    sides = {0: 7, 1: 5, 2: 4, 3: 3}[q]
    for k in range(n):
        x = x0 + step * (k + 0.5)
        h = rng.r(hmin, hmax)
        r = rng.r(0.12, 0.15)
        lean = (rng.r(-0.03, 0.03), rng.r(-0.04, 0.02))
        tip = rng.r(0.35, 0.5)
        u = rng.r(0, 1)
        if state == 'Construction' and x > x0 + (x1 - x0) * 0.55:
            continue
        if state == 'Damaged':
            if u < 0.12:
                continue
            if u < 0.3:
                h *= rng.r(0.35, 0.6); tip = None
            if 0.3 <= u < 0.4:
                lean = (rng.r(-0.6, 0.6), rng.r(-0.9, -0.5))
        if state == 'Ruin':
            if u < 0.35:
                continue
            h = rng.r(0.3, 1.3); tip = None
        if state == 'Burnt':
            if u < 0.15:
                continue
            h = rng.r(0.4, 2.2); tip = rng.r(0.1, 0.3) if u > 0.6 else None
        base = (x, y, -0.4)
        top = (x + lean[0] * h / 3.0, y + lean[1] * h / 3.0, h - (tip or 0.0))
        segs = 2 if q == 0 else 1
        pole(mb, base, top, r, r * 0.92, sides=sides, segs=segs, bend=0.02, band=S_BARK, seed=seed + k, tip=tip, caps=(False, True), rough=0.06)


def palisade_bank(mb, x0, x1, q, seed, state):
    """Low earth bank along the outer (-Y) face, flat ends so segments abut."""
    n = {0: 8, 1: 4, 2: 2, 3: 1}[q]
    prof = [(-1.15, -0.3), (-1.0, 0.02), (-0.55, 0.42), (-0.15, 0.5), (0.25, 0.25), (0.45, -0.3)]
    if state in ('Ruin', 'Burnt'):
        prof = [(-1.25, -0.3), (-1.05, 0.0), (-0.5, 0.3), (0.0, 0.32), (0.35, 0.12), (0.5, -0.3)]
    rows, U = [], []
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        row, ur = [], []
        s = 0.0
        for k, (y, z) in enumerate(prof):
            dz = 0.06 * fbm(x * 0.7, y, 0, seed, 2) if 0 < k < len(prof) - 1 and 0 < i < n else 0.0
            row.append((x, y, z + dz))
            if k:
                s += math.hypot(y - prof[k - 1][0], z - prof[k - 1][1])
            ur.append(S_EARTH.uv(x, s * 0.4))
        rows.append(row); U.append(ur)
    # rows along +X, columns from front (-Y) to back: normal = cross(d/dcol, d/drow) -> up
    grid(mb, rows, U, STRUCT)
    # end caps so the bank reads solid when a segment ends
    for x, flip in ((x0, True), (x1, False)):
        pts = [(x, y, z) for y, z in prof]
        c = (x, -0.35, -0.1)
        uv = [S_EARTH.uv(y, (z + 0.3) * 0.5) for y, z in prof]
        ids = [mb.v(p) for p in pts]
        ci = mb.v(c)
        for k in range(len(pts) - 1):
            tri = [ci, ids[k], ids[k + 1]]
            tuv = [S_EARTH.uv(-0.35, 0.1), uv[k], uv[k + 1]]
            if flip:
                tri = tri[::-1]; tuv = tuv[::-1]
            mb.f(tri, tuv, STRUCT)


def palisade_rails(mb, x0, x1, q, seed, z, y=0.2, state='Intact'):
    if q >= 2:
        return
    pole(mb, (x0 + 0.02, y, z), (x1 - 0.02, y, z + 0.05), 0.07, 0.065, sides=5 if q == 0 else 4, segs=2, bend=0.03, seed=seed, caps=(True, True))
    if q == 0:
        n = int((x1 - x0) / 0.9)
        for k in range(n):
            x = x0 + (x1 - x0) * (k + 0.5) / n
            ring = [(x + 0.02 * math.sin(TAU * i / 6), y - 0.02 + math.cos(TAU * i / 6) * 0.17, z + math.sin(TAU * i / 6) * 0.11) for i in range(7)]
            tube(mb, ring, [0.018] * 7, 3, D_ROPE, seed=seed + k)


def palisade_seg(state, q):
    mb = MB()
    seed = 1001
    x0, x1 = -PAL_LEN * 0.5, PAL_LEN * 0.5
    if q == 3:
        hh = {'Intact': 3.6, 'Construction': 3.6, 'Damaged': 3.0, 'Ruin': 0.9, 'Burnt': 1.4}[state]
        xr = x1 if state != 'Construction' else 0.4
        box(mb, ((x0 + xr) * 0.5, 0.0, (hh - 0.4) * 0.5), ((xr - x0) * 0.5, 0.14, (hh + 0.4) * 0.5), S_BARK)
        palisade_bank(mb, x0, x1, 3, seed, state)
        return mb
    palisade_logs(mb, x0, x1, q, seed, state)
    palisade_bank(mb, x0, x1, q, seed, state)
    if state in ('Intact', 'Damaged', 'Construction'):
        palisade_rails(mb, x0, x1 if state != 'Construction' else 0.6, q, seed + 50, 2.3, state=state)
        if q == 0 and state == 'Intact':
            palisade_rails(mb, x0, x1, q, seed + 60, 0.9)
    if state == 'Construction' and q <= 2:
        rng = Rng(seed)
        for k in range({0: 6, 1: 3, 2: 2}[q]):
            y = 1.3 + 0.27 * (k % 3); z = 0.15 + 0.24 * (k // 3)
            pole(mb, (0.8 + rng.r(-0.2, 0.2), y, z), (3.9 + rng.r(-0.2, 0.2), y + rng.r(-0.1, 0.1), z), 0.13, 0.12, sides={0: 7, 1: 5, 2: 4}[q],
                 seed=seed + 70 + k, caps=(True, True))
    if state in ('Damaged', 'Ruin', 'Burnt') and q <= 2:
        rng = Rng(seed + 5)
        for k in range({0: 3, 1: 2, 2: 1}[q]):
            x = rng.r(-3.0, 3.0)
            pole(mb, (x, -0.8, 0.12), (x + rng.r(-1.5, 1.5), -3.2, 0.2), 0.13, 0.12, sides={0: 6, 1: 5, 2: 4}[q], seed=seed + 80 + k, caps=(True, True))
    if state == 'Burnt':
        P = [[(x1, -0.4, 0.03), (x0, -0.4, 0.03)], [(x1, 0.9, 0.03), (x0, 0.9, 0.03)]]
        U = [[D_CHAR.uv(1, 0), D_CHAR.uv(0, 0)], [D_CHAR.uv(1, D_CHAR.hm), D_CHAR.uv(0, D_CHAR.hm)]]
        grid(mb, P, U, DETAIL)
    return mb


def palisade_gate(state, q):
    """8 m module: 3.6 m opening between two big posts with a lintel, two log leaves, palisade stubs both sides."""
    mb = MB()
    seed = 1101
    x0, x1 = -PAL_LEN * 0.5, PAL_LEN * 0.5
    ow = 3.6
    pw = ow * 0.5 + 0.28
    sides = {0: 8, 1: 6, 2: 4, 3: 4}[q]
    standing = state in ('Intact', 'Damaged', 'Construction')
    ph = {'Intact': 5.4, 'Damaged': 5.4, 'Construction': 5.4, 'Ruin': 1.6, 'Burnt': 3.1}[state]
    for sx in (-1, 1):
        h = ph if not (state == 'Burnt' and sx > 0) else 1.9
        pole(mb, (sx * pw, 0.0, -0.6), (sx * pw, 0.0, h), 0.26, 0.23, sides=sides, segs=2 if q == 0 else 1, bend=0.02, seed=seed + sx,
             tip=0.35 if standing else None, caps=(False, True))
    if q == 3:
        box(mb, (-(x1 + pw) * 0.5 - 0.0, 0, 1.4), ((x1 - pw) * 0.5, 0.14, 1.8), S_BARK)
        box(mb, ((x1 + pw) * 0.5, 0, 1.4), ((x1 - pw) * 0.5, 0.14, 1.8), S_BARK)
        if standing:
            box(mb, (0, 0, ph - 0.6), (pw + 0.5, 0.18, 0.18), S_BARK)
        palisade_bank(mb, x0, -pw - 0.25, 3, seed, state)
        palisade_bank(mb, pw + 0.25, x1, 3, seed + 1, state)
        return mb
    if standing and state != 'Construction':
        pole(mb, (-pw - 0.6, -0.05, ph - 0.55), (pw + 0.6, -0.05, ph - 0.5), 0.2, 0.19, sides=sides, segs=2 if q == 0 else 1, bend=0.03, seed=seed + 5, caps=(True, True))
        if q <= 1:
            # culture banner hanging from the lintel (painted hide, culture colour in M.G)
            hide_panel(mb, [(-0.55, -0.3, ph - 2.05), (0.55, -0.3, ph - 2.05), (0.6, -0.27, ph - 0.7), (-0.6, -0.27, ph - 0.7)],
                       3 if q == 0 else 1, 4 if q == 0 else 1, seed + 6, band=D_PAINT, sag=-0.03, two_sided=True)
            for sx in (-1, 1):
                pole(mb, (sx * pw, -0.25, ph - 0.25), (sx * (pw + 0.05), -0.3, ph + 0.9), 0.07, 0.04, sides=4, seed=seed + 8 + sx, tip=0.2)
    elif state == 'Construction' and q <= 2:
        # the lintel waits on the ground
        pole(mb, (-pw - 0.4, -1.4, 0.2), (pw + 0.5, -1.6, 0.2), 0.2, 0.19, sides=sides, seed=seed + 5, caps=(True, True))
    # leaves (swung inward, +Y)
    if state in ('Intact', 'Damaged') and q <= 2:
        for sx in (-1, 1):
            if state == 'Damaged' and sx > 0:
                continue
            ang = sx * (-0.75)
            hinge = (sx * (pw - 0.25), 0.25, 0.0)
            nl = {0: 7, 1: 5, 2: 3}[q]
            lw = ow * 0.5 - 0.05
            for k in range(nl):
                f = (k + 0.5) / nl
                off = rotz((-sx * lw * f, 0.0, 0.0), ang)
                b = add(hinge, off)
                pole(mb, (b[0], b[1], 0.0), (b[0], b[1], 3.0), 0.1, 0.09, sides={0: 5, 1: 4, 2: 3}[q], seed=seed + 20 + k + (sx > 0) * 10,
                     tip=0.25, caps=(True, True))
            if q <= 1:
                for z in (0.6, 2.4):
                    a_ = add(hinge, (0, 0.12, z)); b_ = add(add(hinge, rotz((-sx * lw, 0.0, 0.0), ang)), (0, 0.12, z))
                    pole(mb, a_, b_, 0.06, 0.06, sides=4, seed=seed + 40 + int(z * 10), caps=(True, True))
    if state == 'Damaged' and q <= 2:
        # the right leaf torn off and lying in front
        for k in range({0: 6, 1: 4, 2: 3}[q]):
            x = 0.6 + 0.2 * k
            pole(mb, (x, -0.9, 0.12 + 0.02 * k), (x - 0.4, -3.8, 0.2), 0.1, 0.09, sides=4, seed=seed + 60 + k, caps=(True, True))
    # flanking palisade stubs + banks
    palisade_logs(mb, x0, -pw - 0.25, q, seed + 100, state)
    palisade_logs(mb, pw + 0.25, x1, q, seed + 200, state)
    palisade_bank(mb, x0, -pw - 0.25, q, seed, state)
    palisade_bank(mb, pw + 0.25, x1, q, seed + 1, state)
    if state in ('Intact', 'Damaged'):
        palisade_rails(mb, x0, -pw - 0.25, q, seed + 300, 2.3)
        palisade_rails(mb, pw + 0.25, x1, q, seed + 400, 2.3)
    # trodden threshold
    if q <= 2:
        P = [[(ow * 0.5, -2.5, 0.02), (-ow * 0.5, -2.5, 0.02)], [(ow * 0.5, 2.0, 0.02), (-ow * 0.5, 2.0, 0.02)]]
        U = [[S_EARTH.uv(ow, 0.05), S_EARTH.uv(0, 0.05)], [S_EARTH.uv(ow, 0.95), S_EARTH.uv(0, 0.95)]]
        grid(mb, P, U, STRUCT)
    if state == 'Burnt' and q <= 2:
        P = [[(x1, -0.5, 0.04), (x0, -0.5, 0.04)], [(x1, 1.2, 0.04), (x0, 1.2, 0.04)]]
        U = [[D_CHAR.uv(1, 0), D_CHAR.uv(0, 0)], [D_CHAR.uv(1, D_CHAR.hm), D_CHAR.uv(0, D_CHAR.hm)]]
        grid(mb, P, U, DETAIL)
    return mb


# --------------------------------------------------------------------------- storage pits

def storagepit(var, state, q):
    mb = MB()
    seed = 1201 if var == 'A' else 1301
    segs = {0: 20, 1: 12, 2: 8, 3: 6}[q]
    R = 0.95
    # raised clay rim
    prof = [(R + 0.55, -0.3), (R + 0.5, 0.02), (R + 0.25, 0.22), (R + 0.02, 0.24), (R - 0.12, 0.12)]
    if q >= 2:
        prof = [prof[0], prof[2], prof[4]]
    lathe(mb, prof, segs, S_EARTH, disp=(lambda a, i: 1.0 + 0.04 * vnoise(math.cos(a) * 2, math.sin(a) * 2, i, seed)) if q <= 1 else None)
    if var == 'A':
        # domed thatch cap, tied down, weighted with stones
        surf = round_surf(R + 0.18, 0.75, 0.12, 2.0, 0.6)
        A = rand_angles(segs)
        Um = round_um(R, segs)
        tb = {0: [0, .45, 1.0], 1: [0, .5, 1.0], 2: [0, 1.0], 3: [0, 1.0]}[q]
        thatch_courses(mb, surf, A, Um, tb, q, 0.1, seed, ribs=0, lump=0.02 if q <= 1 else 0.0)
        if q <= 1:
            topknot(mb, 0.12 + 0.75, q, seed, r=0.16, hgt=0.25)
            for k in range(2):
                ang = k * math.pi / 2
                pts = []
                for i in range(9):
                    t = -1 + 2 * i / 8
                    p = surf(1 - abs(t), 0)
                    rr = math.hypot(p[0], p[1]) + 0.08
                    pts.append(rotz((math.copysign(rr, t), 0.0, p[2] + 0.06), ang))
                tube(mb, pts, [0.022] * 9, 3, D_ROPE, seed=seed + k)
            for k in range(4):
                a = k * math.pi / 2 + 0.2
                rock(mb, (math.cos(a) * (R + 0.45), math.sin(a) * (R + 0.45), 0.15), (0.3, 0.24, 0.2), seed + 10 + k, segs=5, rings=3)
    else:
        # open pit: dark shaft, plank lid half across, grinding stone
        disc(mb, (0, 0, 0), R - 0.1, segs, D_INTERIOR, z=-0.55)
        if q <= 2:
            # inner wall: the profile runs top -> bottom, so the lathe already faces the axis (into the pit). The old
            # extra flip turned it toward the earth, where back-face culling hid it (verifier fix).
            lathe(mb, [(R - 0.12, 0.12), (R - 0.16, -0.6)], segs, S_EARTH)
        nl = {0: 4, 1: 3, 2: 2, 3: 1}[q]
        for k in range(nl):
            y = -0.45 + 0.3 * k
            pole(mb, (-R - 0.3, y, 0.28), (R + 0.25, y + 0.05, 0.3), 0.13, 0.12, sides={0: 6, 1: 5, 2: 4, 3: 3}[q], band=S_WOOD,
                 seed=seed + k, caps=(True, True))
        if q <= 1:
            rock(mb, (R + 0.9, 0.6, 0.0), (0.9, 0.6, 0.3), seed + 20, segs=7 if q == 0 else 5, rings=3, flat=0.5)
            rock(mb, (R + 0.85, 0.55, 0.28), (0.35, 0.3, 0.16), seed + 21, segs=5, rings=3)
    return mb


# --------------------------------------------------------------------------- standing stones

def menhir(mb, c, h, w, d, yaw, seed, q, lean=(0.0, 0.0), sink=0.4, broken=False):
    sides = {0: 9, 1: 7, 2: 5, 3: 4}[q]
    rings = {0: 7, 1: 5, 2: 3, 3: 2}[q]
    P, U = [], []
    for i in range(rings + 1):
        f = i / rings
        z = -sink + (h + sink) * f
        taper = 1.0 - 0.35 * f ** 1.6
        row, urow = [], []
        for j in range(sides + 1):
            a = TAU * j / sides
            x = math.cos(a) * w * 0.5 * taper
            y = math.sin(a) * d * 0.5 * taper
            k = 1.0 + 0.18 * fbm(math.cos(a) * 1.3 + seed, math.sin(a) * 1.3, f * 2.5, seed, 3)
            x *= k; y *= k
            zz = z
            if i == rings:
                zz = z - (0.0 if broken else 0.25 * h * 0.15) * (1.0 + math.cos(a + seed)) * 0.5
                if broken:
                    zz += 0.12 * fbm(x * 3, y * 3, seed, seed, 2)
            p = rotz((x + lean[0] * (zz + sink), y + lean[1] * (zz + sink), zz), yaw)
            if j < sides:
                row.append(add(c, p))
            urow.append(D_STONE.uv(TAU * max(w, d) * 0.5 * j / sides + (seed * 0.3) % D_STONE.mu, max(0.0, zz)))
        P.append(row); U.append(urow)
    grid(mb, P, U, DETAIL, wrap=True)
    top = P[-1]
    cz = sum(p[2] for p in top) / len(top) + (0.0 if broken else 0.06 * h)
    cx = sum(p[0] for p in top) / len(top); cy = sum(p[1] for p in top) / len(top)
    fan(mb, (cx, cy, cz), top, D_STONE.uv((seed * 0.3) % D_STONE.mu, h), U[-1][:-1], DETAIL)


def standingstones(var, state, q):
    mb = MB()
    seed = 1401 if var == 'A' else 1501
    rng = Rng(seed)
    if var == 'A':
        n = 12
        Rr = 7.0
        for k in range(n):
            a = TAU * k / n + rng.r(-0.04, 0.04)
            h = rng.r(2.3, 3.6)
            w = rng.r(0.85, 1.15); d = rng.r(0.45, 0.6)
            c = (math.cos(a) * Rr, math.sin(a) * Rr, 0.0)
            yaw = a + math.pi / 2 + rng.r(-0.2, 0.2)
            u = rng.r()
            if state == 'Ruin':
                if u < 0.35:
                    # toppled: lying on its side, pointing outward
                    lay = (math.cos(a) * (Rr + h * 0.45), math.sin(a) * (Rr + h * 0.45), 0.0)
                    mb2 = MB()
                    menhir(mb2, (0, 0, 0), h, w, d, 0.0, seed + k, q, sink=0.0)
                    ca, sa = math.cos(a), math.sin(a)
                    def xf(p, a=a, lay=lay, d=d):
                        p = roty(p, math.pi / 2)            # lie along +X
                        p = (p[0] - h * 0.5, p[1], p[2] + d * 0.35)
                        p = rotz(p, a)
                        return add(p, lay)
                    mb.append(mb2, xf)
                    continue
                if u < 0.55:
                    h *= rng.r(0.3, 0.55)
                    menhir(mb, c, h, w, d, yaw, seed + k, q, broken=True)
                    continue
                menhir(mb, c, h, w, d, yaw, seed + k, q, lean=(math.cos(a) * 0.08, math.sin(a) * 0.08))
                continue
            menhir(mb, c, h, w, d, yaw, seed + k, q, lean=(rng.r(-0.03, 0.03), rng.r(-0.03, 0.03)))
        # recumbent altar stone in the middle
        mb2 = MB()
        menhir(mb2, (0, 0, 0), 2.0, 1.1, 0.9, 0.0, seed + 99, q, sink=0.0)
        mb.append(mb2, lambda p: add(rotz((roty(p, math.pi / 2)[0] - 1.0, roty(p, math.pi / 2)[1], roty(p, math.pi / 2)[2] + 0.3), 0.4), (0, 0, 0)))
        if q <= 1 and state == 'Intact':
            # offering stones and a ring of small kerb stones
            for k in range(16 if q == 0 else 8):
                a = TAU * k / (16 if q == 0 else 8)
                rock(mb, (math.cos(a) * 2.2, math.sin(a) * 2.2, 0.0), (0.3, 0.25, 0.18), seed + 200 + k, segs=5, rings=3)
    else:
        h = 3.4
        if state == 'Ruin':
            mb2 = MB()
            menhir(mb2, (0, 0, 0), h, 1.0, 0.55, 0.0, seed, q, sink=0.0)
            mb.append(mb2, lambda p: add(roty(p, math.pi / 2 - 0.12), (-h * 0.45, 0, 0.35)))
            menhir(mb, (h * 0.55, 0.1, 0.0), 0.5, 1.0, 0.55, 0.3, seed + 1, q, broken=True)
        else:
            menhir(mb, (0, 0, 0), h, 1.0, 0.55, 0.3, seed, q, lean=(0.04, -0.02))
        if q <= 1:
            for k in range(5):
                a = TAU * k / 5 + 0.4
                rock(mb, (math.cos(a) * 1.0, math.sin(a) * 0.8, 0.0), (0.35, 0.3, 0.2), seed + 10 + k, segs=5, rings=3)
    return mb


# --------------------------------------------------------------------------- ornaments (culture)

def ornament(var, state, q):
    mb = MB()
    seed = 1601 if var == 'Pennant' else 1701
    sides = {0: 6, 1: 5, 2: 4, 3: 3}[q]
    if var == 'Pennant':
        H = 5.5
        pole(mb, (0, 0, -0.5), (0.03, 0.0, H), 0.07, 0.045, sides=sides, segs=2 if q == 0 else 1, bend=0.04, seed=seed, tip=0.25)
        # streamer: painted hide tapering, two-sided
        n = {0: 6, 1: 3, 2: 2, 3: 1}[q]
        P, Ub = [], []
        for i in range(n + 1):
            f = i / n
            x = 0.05 + 2.6 * f
            sag = 0.7 * f * f
            wv = 0.15 * math.sin(f * 5.0) * f
            hh = 1.05 * (1.0 - 0.7 * f)
            P.append([(x, wv, H - 0.15 - sag - hh), (x, wv, H - 0.15 - sag)])
            Ub.append([D_PAINT.uv(x, 0.0), D_PAINT.uv(x, 0.47)])
        # rows along the streamer, columns bottom->top: faces -Y
        grid(mb, P, Ub, DETAIL)
        grid(mb, [[add(p, (0, 0.015, 0)) for p in row[::-1]] for row in P], [u[::-1] for u in Ub], DETAIL)
        if q <= 1:
            for k in range(3):
                a = TAU * k / 3
                rock(mb, (math.cos(a) * 0.3, math.sin(a) * 0.3, 0.0), (0.28, 0.24, 0.2), seed + k, segs=5, rings=3)
    else:
        # carved totem post with painted bands and a horned crown
        H = 3.6
        n = {0: 9, 1: 5, 2: 3, 3: 1}[q]
        sides_t = {0: 10, 1: 7, 2: 5, 3: 4}[q]
        pts = [(0, 0, -0.5 + (H + 0.5) * i / n) for i in range(n + 1)]
        rad = [0.24 * (1.0 + (0.18 * math.sin(i * 2.1) if 0 < i < n else 0.0)) for i in range(n + 1)]
        tube(mb, pts, rad, sides_t, S_WOOD, cap1=True, seed=seed)
        if q <= 2:
            for k, z in enumerate((0.9, 1.9, 2.8)):
                lathe(mb, [(0.25, z - 0.18), (0.27, z), (0.25, z + 0.18)], sides_t, D_PAINT, vmetres=[0.05, 0.24, 0.45])
        if q <= 1:
            for sx in (-1, 1):
                tube(mb, [(sx * 0.15, 0, H - 0.1), (sx * 0.55, -0.05, H + 0.25), (sx * 0.6, -0.1, H + 0.75)], [0.08, 0.06, 0.02], 5, D_STONE,
                     seed=seed + sx, cap1=True)
    return mb
