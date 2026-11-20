# DEMIURGE ships - geometry kit: a face list with trim-sheet UVs, primitives and a lofted hull.
# Units are metres; Z up; bow toward -Y; waterline at Z = 0.
import math

from mathutils import Matrix, Vector

from ship_tex import BANDS, CONTAINER_SEGMENTS, N

INSET = 1.5   # px kept free at each band edge


def V(x, y=None, z=None):
    if y is None:
        return Vector(x)
    return Vector((x, y, z))


class Part:
    """Vertices plus faces; each face = [vertex ids, band, [(u, vl)...], smooth]. u tiles freely, vl is 0..1 in the band."""

    def __init__(self):
        self.V = []
        self.F = []

    def v(self, p):
        self.V.append(Vector(p))
        return len(self.V) - 1

    def face(self, ids, band, uvl, smooth=False):
        assert band in BANDS, band
        self.F.append([list(ids), band, [tuple(x) for x in uvl], smooth])

    def extend(self, other, M=None):
        o = len(self.V)
        for p in other.V:
            self.V.append((M @ p) if M is not None else p.copy())
        for ids, band, uvl, sm in other.F:
            self.F.append([[i + o for i in ids], band, list(uvl), sm])
        return self

    def transform(self, M):
        self.V = [M @ p for p in self.V]
        return self

    def tris(self):
        return sum(len(f[0]) - 2 for f in self.F)

    def centroid(self, f):
        c = Vector((0, 0, 0))
        for i in f[0]:
            c += self.V[i]
        return c / len(f[0])

    def normal(self, f):
        ids = f[0]
        n = Vector((0, 0, 0))
        for k in range(len(ids)):
            a, b = self.V[ids[k]], self.V[ids[(k + 1) % len(ids)]]
            n.x += (a.y - b.y) * (a.z + b.z)
            n.y += (a.z - b.z) * (a.x + b.x)
            n.z += (a.x - b.x) * (a.y + b.y)
        return n

    def orient(self, start, ref):
        """Flip faces from index `start` on so their normals point away from ref(centroid) (an interior point)."""
        for f in self.F[start:]:
            c = self.centroid(f)
            if self.normal(f).dot(c - ref(c)) < 0:
                f[0].reverse()
                f[2].reverse()


def band_uv(band, u, vl):
    slot, y0, h = BANDS[band]
    vl = min(max(vl, 0.0), 1.0)
    return (u, (y0 + INSET + vl * (h - 2 * INSET)) / N)


def aspect(band):
    return N / BANDS[band][2]


def seg_u(band, k):
    """containers band: U range of colour segment k."""
    a = k / CONTAINER_SEGMENTS
    return a + 0.01, a + 1.0 / CONTAINER_SEGMENTS - 0.01


# --------------------------------------------------------------------------- primitives


def quad(P, a, b, c, d, band, lu=None, lv=None, u0=0.0, smooth=False, u_range=None):
    """a,b along the bottom edge, d,c along the top; UV scaled uniformly to the band height."""
    a, b, c, d = Vector(a), Vector(b), Vector(c), Vector(d)
    lu = (b - a).length if lu is None else lu
    lv = (d - a).length if lv is None else lv
    if u_range:
        u1, u2 = u_range
    else:
        u1 = u0
        u2 = u0 + lu / max(lv * aspect(band), 1e-6)
    ids = [P.v(a), P.v(b), P.v(c), P.v(d)]
    P.face(ids, band, [(u1, 0), (u2, 0), (u2, 1), (u1, 1)], smooth)


def box(P, c, size, band, R=None, u0=0.0, skip=(), u_range=None, top_band=None):
    """Axis box centred at c (size x,y,z), optional 3x3 rotation R about c. skip: subset of '-x','+x','-y','+y','-z','+z'."""
    c = Vector(c)
    sx, sy, sz = (s * 0.5 for s in size)
    R = R if R is not None else Matrix.Identity(3)

    def p(x, y, z):
        return c + R @ Vector((x, y, z))
    faces = {
        '-x': ([(-sx, sy, -sz), (-sx, -sy, -sz), (-sx, -sy, sz), (-sx, sy, sz)], size[1], size[2]),
        '+x': ([(sx, -sy, -sz), (sx, sy, -sz), (sx, sy, sz), (sx, -sy, sz)], size[1], size[2]),
        '-y': ([(-sx, -sy, -sz), (sx, -sy, -sz), (sx, -sy, sz), (-sx, -sy, sz)], size[0], size[2]),
        '+y': ([(sx, sy, -sz), (-sx, sy, -sz), (-sx, sy, sz), (sx, sy, sz)], size[0], size[2]),
        '+z': ([(-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz)], size[0], size[1]),
        '-z': ([(-sx, sy, -sz), (sx, sy, -sz), (sx, -sy, -sz), (-sx, -sy, -sz)], size[0], size[1]),
    }
    for k, (pts, lu, lv) in faces.items():
        if k in skip:
            continue
        bnd = top_band if (top_band and k == '+z') else band
        q = [p(*x) for x in pts]
        if k in ('+z', '-z') and lv > lu:   # run U along the longer side
            q = [q[1], q[2], q[3], q[0]]
            lu, lv = lv, lu
        quad(P, q[0], q[1], q[2], q[3], bnd, lu, lv, u0, u_range=u_range)


def frame(axis):
    axis = Vector(axis).normalized()
    up = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
    x = axis.cross(up).normalized()
    y = axis.cross(x).normalized()
    return x, y


def cyl(P, p0, p1, r0, r1, n, band, cap0=True, cap1=True, smooth=True, along='v', u0=0.0, phase=0.0):
    p0, p1 = Vector(p0), Vector(p1)
    ax = p1 - p0
    L = ax.length
    ex, ey = frame(ax)
    circ = math.pi * (r0 + r1)
    r0s, r1s = [], []
    for j in range(n + 1):
        a = 2 * math.pi * j / n + phase
        d = ex * math.cos(a) + ey * math.sin(a)
        r0s.append(p0 + d * r0)
        r1s.append(p1 + d * r1)
    start = len(P.F)
    for j in range(n):
        ids = [P.v(r0s[j]), P.v(r0s[j + 1]), P.v(r1s[j + 1]), P.v(r1s[j])]
        if along == 'v':
            k = circ / max(L * aspect(band), 1e-6)
            ua, ub = u0 + k * j / n, u0 + k * (j + 1) / n
            uvl = [(ua, 0), (ub, 0), (ub, 1), (ua, 1)]
        else:
            k = L / max(circ * aspect(band), 1e-6)
            uvl = [(u0, j / n), (u0, (j + 1) / n), (u0 + k, (j + 1) / n), (u0 + k, j / n)]
        P.face(ids, band, uvl, smooth)
    mid = (p0 + p1) * 0.5
    P.orient(start, lambda c: mid + ax.normalized() * (c - mid).dot(ax.normalized()))
    start = len(P.F)
    for cap, ring, r in ((cap0, r0s, r0), (cap1, r1s, r1)):
        if cap and r > 1e-4:
            ids = [P.v(q) for q in ring[:n]]
            uvl = [(0.5 + math.cos(2 * math.pi * j / n) * 0.5 / aspect(band), 0.5 + 0.5 * math.sin(2 * math.pi * j / n)) for j in range(n)]
            P.face(ids, band, uvl, False)
    P.orient(start, lambda c: mid)
    return P


def line(P, a, b, r, band='rope', n=3):
    cyl(P, a, b, r, r, n, band, cap0=False, cap1=False, smooth=True, along='u')


def _h2(i, j, seed):
    x = math.sin(i * 12.9898 + j * 78.233 + seed * 37.719) * 43758.5453
    return x - math.floor(x)


def grid(P, f, ns, nt, band, smooth=True, two_sided=True, thick=0.02, u_range=(0.0, 1.0), keep=None, nfn=None,
         jitter=0.0, seed=0):
    """Surface sampled from f(s, t) (s across -> u, t up -> vl). keep(i, j) -> False drops a cell (torn sails).
    jitter (cells) moves the inner grid points so torn holes get ragged, not square, edges; the head row (t = 1)
    and the side edges stay put (verifier fix 2026-11-19)."""
    def st(i, j):
        s_, t_ = i / ns, j / nt
        if jitter > 0.0:
            if 0 < i < ns:
                s_ += (_h2(i, j, seed) - 0.5) * 2.0 * jitter / ns
            if j < nt:
                t_ += (_h2(j, i, seed + 7) - 0.5) * 2.0 * jitter / nt * (0.5 if j == 0 else 1.0)
        return s_, t_
    pts = [[Vector(f(*st(i, j))) for j in range(nt + 1)] for i in range(ns + 1)]
    u1, u2 = u_range
    for side in ((0, 1) if two_sided else (0,)):
        ids = {}
        for i in range(ns + 1):
            for j in range(nt + 1):
                p = pts[i][j]
                if two_sided:
                    # offset each side along the local normal so the two layers never z-fight
                    di = pts[min(i + 1, ns)][j] - pts[max(i - 1, 0)][j]
                    dj = pts[i][min(j + 1, nt)] - pts[i][max(j - 1, 0)]
                    nn = di.cross(dj)
                    if nn.length > 1e-9:
                        nn.normalize()
                    p = p + nn * (thick * 0.5 if side == 0 else -thick * 0.5)
                ids[i, j] = P.v(p)
        for i in range(ns):
            for j in range(nt):
                if keep is not None and not keep(i, j):
                    continue
                q = [ids[i, j], ids[i + 1, j], ids[i + 1, j + 1], ids[i, j + 1]]
                uv = [(u1 + (u2 - u1) * i / ns, j / nt), (u1 + (u2 - u1) * (i + 1) / ns, j / nt),
                      (u1 + (u2 - u1) * (i + 1) / ns, (j + 1) / nt), (u1 + (u2 - u1) * i / ns, (j + 1) / nt)]
                if side == 1:
                    q.reverse()
                    uv.reverse()
                P.face(q, band, uv, smooth)
    return P


def loft(P, rings, band_of_seg, smooth=True, u_scale_ref=None, flip=False):
    """rings: list (stations) of point lists (same length m). Segment j joins point j and j+1 of each ring.
    UV: vl runs along each contiguous run of equal bands inside a ring, u along the stations."""
    m = len(rings[0])
    segs = [band_of_seg(j) for j in range(m - 1)]
    runs = []
    j = 0
    while j < m - 1:
        k = j
        while k + 1 < m - 1 and segs[k + 1] == segs[j]:
            k += 1
        runs.append((j, k + 1, segs[j]))
        j = k + 1
    # girth fraction per ring point per run
    vl = [[0.0] * m for _ in rings]
    run_g = []
    for (a, b, band) in runs:
        gs = []
        for ri, ring in enumerate(rings):
            g = [0.0]
            for q in range(a, b):
                g.append(g[-1] + (ring[q + 1] - ring[q]).length)
            tot = max(g[-1], 1e-6)
            gs.append(tot)
            for q in range(a, b + 1):
                vl[ri][q] = g[q - a] / tot
        run_g.append(sum(gs) / len(gs))
    # u: distance along stations per point
    ud = [[0.0] * m]
    for ri in range(1, len(rings)):
        ud.append([ud[-1][q] + (rings[ri][q] - rings[ri - 1][q]).length for q in range(m)])
    ids = [[P.v(p) for p in ring] for ring in rings]
    for (a, b, band), g in zip(runs, run_g):
        k = 1.0 / max(g * aspect(band), 1e-6)
        for ri in range(len(rings) - 1):
            for q in range(a, b):
                f = [ids[ri][q], ids[ri][q + 1], ids[ri + 1][q + 1], ids[ri + 1][q]]
                uv = [(ud[ri][q] * k, vl[ri][q]), (ud[ri][q + 1] * k, vl[ri][q + 1]),
                      (ud[ri + 1][q + 1] * k, vl[ri + 1][q + 1]), (ud[ri + 1][q] * k, vl[ri + 1][q])]
                if flip:
                    f.reverse()
                    uv.reverse()
                P.face(f, band, uv, smooth)
    return ids


def cap(P, pts, band, flip=False, ref=None):
    """Planar-ish polygon cap (triangulated later)."""
    pts = [Vector(p) for p in pts]
    xs = [p.x for p in pts]
    ys = [p.y for p in pts]
    zs = [p.z for p in pts]
    # project on the two largest extents
    ext = sorted([(max(xs) - min(xs), 0), (max(ys) - min(ys), 1), (max(zs) - min(zs), 2)], reverse=True)
    a0, a1 = ext[0][1], ext[1][1]
    lo0 = min(p[a0] for p in pts)
    lo1 = min(p[a1] for p in pts)
    l1 = max(ext[1][0], 1e-6)
    uvl = [((p[a0] - lo0) / (l1 * aspect(band)), (p[a1] - lo1) / l1) for p in pts]
    ids = [P.v(p) for p in pts]
    start = len(P.F)
    P.face(ids, band, uvl, False)
    if flip:
        P.F[-1][0].reverse()
        P.F[-1][2].reverse()
    if ref is not None:
        P.orient(start, ref)


# --------------------------------------------------------------------------- people


def person(P, pos, yaw=0.0, h=1.75, pose='stand', lean=0.0):
    """A crew figure readable at 20-60 px: tapered body and a head, ~30 tris."""
    pos = Vector(pos)
    Rz = Matrix.Rotation(yaw, 3, 'Z') @ Matrix.Rotation(lean, 3, 'X')
    part = Part()
    if pose == 'stand':
        # body + head = h (verifier fix 2026-11-19: was h * 0.80, so a 'h = 1.75' figure stood only 1.67 m)
        hb, r_hip, r_sh = h * (1.0 - 0.065 * 2.35), 0.17, 0.22
        cyl(part, (0, 0, 0), (0, 0, hb), r_hip * 0.8, r_sh, 5, 'crew', cap0=False, cap1=True, smooth=False)
    else:   # sitting / rowing: shorter torso on bent legs
        hb = h * 0.52
        cyl(part, (0, 0, 0), (0, 0, hb), 0.24, 0.21, 5, 'crew', cap0=False, cap1=True, smooth=False)
    hr = h * 0.065
    hc = Vector((0, 0, hb + hr * 1.1))
    pts = [hc + Vector(d) * hr for d in ((0, 0, 1.25), (1, 0, 0), (0, 1, 0), (-1, 0, 0), (0, -1, 0), (0, 0, -1))]
    ids = [part.v(p) for p in pts]
    for a, b in ((1, 2), (2, 3), (3, 4), (4, 1)):
        part.face([ids[0], ids[a], ids[b]], 'crew', [(0.0, 0.9), (0.05, 0.95), (0.1, 1.0)])
        part.face([ids[5], ids[b], ids[a]], 'crew', [(0.0, 0.9), (0.05, 0.95), (0.1, 1.0)])
    part.orient(len(part.F) - 8, lambda c: hc)
    M = Matrix.Translation(pos) @ Rz.to_4x4()
    P.extend(part, M)


# --------------------------------------------------------------------------- hull


class HullSpec:
    """Hull lines. t in 0..1 from bow (y = -L/2) to stern (y = +L/2)."""

    def __init__(self, L, B, sheer, keel, n=3.0, bow=0.25, stern=0.2, transom=0.0, sheer_bow=0.0, sheer_stern=0.0,
                 rocker=0.0, flare=0.08, tumble=0.0, bow_pow=1.0, stern_pow=1.4, bow_rake=0.0, stern_rake=0.0,
                 n_end=None, rocker_stern=None):
        self.__dict__.update(locals())
        del self.__dict__['self']

    def y(self, t):
        return -self.L * 0.5 + t * self.L

    def half_beam(self, t):
        if t < self.bow:
            x = t / self.bow
            f = math.sin(0.5 * math.pi * x) ** self.bow_pow
        elif t > 1.0 - self.stern:
            x = (t - (1.0 - self.stern)) / self.stern
            f = 1.0 - (1.0 - self.transom) * x ** self.stern_pow
        else:
            f = 1.0
        return max(f, 0.0) * self.B * 0.5

    def end_factor(self, t):
        """0 amidships, 1 at the very ends (for rocker and sheer)."""
        if t < self.bow:
            return (1 - t / self.bow)
        if t > 1 - self.stern:
            return (t - (1 - self.stern)) / self.stern
        return 0.0

    def sheer_z(self, t):
        e = self.end_factor(t)
        rise = self.sheer_bow if t < 0.5 else self.sheer_stern
        return self.sheer + rise * e ** 2

    def keel_z(self, t):
        e = self.end_factor(t)
        r = self.rocker if (t < 0.5 or self.rocker_stern is None) else self.rocker_stern
        return self.keel + (r * e ** 2)

    def nexp(self, t):
        if self.n_end is None:
            return self.n
        e = self.end_factor(t)
        return self.n + (self.n_end - self.n) * e


def section(hs, t, m_bottom, m_side, z_marks=()):
    """Half section from keel (x=0) to sheer, as list of (x, z). The bottom is a superellipse quarter up to the
    turn of the bilge at z = zb, the side then rises with flare to the sheer. z_marks: extra z levels to insert."""
    b = hs.half_beam(t)
    zk = hs.keel_z(t)
    zs = hs.sheer_z(t)
    if zk > zs - 0.05:
        zk = zs - 0.05
    n = hs.nexp(t)
    depth = zs - zk
    zb = zk + depth * 0.55   # turn of the bilge
    bw = b * (1.0 - hs.flare)
    pts = []
    for i in range(m_bottom + 1):
        th = 0.5 * math.pi * i / m_bottom
        x = bw * (math.sin(th) ** (2.0 / n))
        z = zb - (zb - zk) * (math.cos(th) ** (2.0 / n))
        pts.append((x, z))
    for i in range(1, m_side + 1):
        s = i / m_side
        z = zb + (zs - zb) * s
        x = bw + (b - bw) * s - hs.tumble * b * s * s
        pts.append((x, z))
    # insert z marks (e.g. waterline, stripe edges) where they cross
    for zm in z_marks:
        for k in range(len(pts) - 1):
            (x0, z0), (x1, z1) = pts[k], pts[k + 1]
            if (z0 - zm) * (z1 - zm) < 0:
                f = (zm - z0) / (z1 - z0)
                pts.insert(k + 1, (x0 + (x1 - x0) * f, zm))
                break
    return pts


def hull(P, hs, n_st, m_bottom, m_side, bands, deck_z=None, bulwark=0.0, deck_band='deck', thick=0.12,
         stem_band=None, transom_band=None, marks=(), t_range=(0.0, 1.0), y_rake=None, rail_band='oar',
         inner_band=None):
    """Lofted hull. bands(zmid) -> band for a segment by its height. marks: z levels that split bands.
    Returns info dict with deck outline. Because marks may insert points at different indices per station, every
    section is resampled to a fixed parameterisation: bottom points, then side points, with marks snapped."""
    ts = []
    for i in range(n_st + 1):
        x = i / n_st
        # cluster stations toward the ends where curvature is highest
        x = 0.5 - 0.5 * math.cos(math.pi * x)
        ts.append(t_range[0] + (t_range[1] - t_range[0]) * x)
    rings = []
    secs = []
    for t in ts:
        sec = section(hs, t, m_bottom, m_side)
        # snap: move the point closest in z to each mark onto the mark (keeps a constant point count)
        zs_ = [z for _, z in sec]
        for zm in marks:
            if zs_[0] < zm < zs_[-1]:
                k = min(range(1, len(sec) - 1), key=lambda q: abs(sec[q][1] - zm))
                # interpolate x on the polyline at zm
                for q in range(len(sec) - 1):
                    if (sec[q][1] - zm) * (sec[q + 1][1] - zm) <= 0 and sec[q + 1][1] != sec[q][1]:
                        f = (zm - sec[q][1]) / (sec[q + 1][1] - sec[q][1])
                        x = sec[q][0] + (sec[q + 1][0] - sec[q][0]) * f
                        break
                else:
                    x = sec[k][0]
                sec[k] = (x, zm)
        secs.append(sec)
        y = hs.y(t)
        ring = [Vector((-x, y + (y_rake(t, z) if y_rake else 0.0), z)) for x, z in reversed(sec)] + \
               [Vector((x, y + (y_rake(t, z) if y_rake else 0.0), z)) for x, z in sec[1:]]
        rings.append(ring)
    m = len(rings[0])

    def seg_band(j):
        # evaluate on the midship ring
        r = rings[len(rings) // 2]
        zmid = 0.5 * (r[j].z + r[j + 1].z)
        return bands(zmid)
    start = len(P.F)
    ids = loft(P, rings, seg_band, smooth=True)
    yc = 0.5 * (hs.y(ts[0]) + hs.y(ts[-1]))
    zmid_all = 0.5 * (hs.keel + hs.sheer)
    def hull_ref(c):
        # interior reference on the centreline of the station the face belongs to (keel and sheer vary along
        # the length: rocker and upswept ends)
        t = min(max((c.y + hs.L * 0.5) / hs.L, 0.0), 1.0)
        zk, zs = hs.keel_z(t), hs.sheer_z(t)
        lo = zk + 0.3 * (zs - zk)
        hi = 0.5 * (zk + zs)
        if abs(c.x) <= 1e-4:
            return Vector((0.0, c.y, c.z + 1.0))
        return Vector((0.0, c.y, min(max(c.z, lo), hi)))
    P.orient(start, hull_ref)
    # end caps (stem at bow, transom at stern): close each end ring
    for ri, band in ((0, stem_band), (len(rings) - 1, transom_band)):
        ring = rings[ri]
        if hs.half_beam(ts[ri]) < 0.02:
            continue
        cap(P, ring, band or bands(0.5 * (ring[0].z + ring[m // 2].z)),
            ref=lambda c, yy=ring[0].y: Vector((c.x, yy + (1.0 if ri == 0 else -1.0), c.z)))
    # bulwark inner face + deck
    deck_rings = []
    if deck_z is not None:
        top = []
        for k, t in enumerate(ts):
            r = rings[k]
            pt, ps = r[0], r[-1]          # port and starboard sheer points
            dz = deck_z(t) if callable(deck_z) else deck_z
            dz = min(dz, pt.z - 0.02)
            hb = abs(pt.x)
            sec = secs[k]
            # the inner bulwark follows the hull skin (flare, round sections) at a constant thickness
            xin = max(hb - thick, 0.0)
            xd = max(x_at(sec, dz) - thick, 0.0)
            yd = hs.y(t) + (y_rake(t, dz) if y_rake else 0.0)
            top.append((Vector((-xin, pt.y, pt.z)), Vector((-xd, yd, dz)), Vector((xd, yd, dz)), Vector((xin, ps.y, ps.z)), Vector((-hb, pt.y, pt.z)), Vector((hb, ps.y, ps.z))))
        # rail cap (sheer to inner), inner bulwark, deck
        rail_p = [[a[4], a[0]] for a in top]
        rail_s = [[a[3], a[5]] for a in top]
        s0 = len(P.F)
        loft(P, rail_p, lambda j: rail_band, smooth=False)
        loft(P, rail_s, lambda j: rail_band, smooth=False)
        P.orient(s0, lambda c: c - Vector((0, 0, 1)))
        # deck: straight strips parallel to the centreline, planar-mapped (verifier fix 2026-11-19: the old
        # beam-normalised loft UVs zig-zagged the planks on every tapering deck quad and made them ~1.4 m wide)
        xs_ = deck_strip_lines(top, deck_band)
        cuts = deck_strips(P, top, xs_, deck_band)
        s0 = len(P.F)
        bulwark_inner(P, top, cuts, inner_band or deck_band)
        P.orient(s0, lambda c: Vector((c.x * 3.0, c.y, c.z)))
        deck_rings = top
        # close the bulwark at the ends (inner face of the transom / stem above the deck)
        for k, sgn in ((0, 1.0), (len(top) - 1, -1.0)):
            a = top[k]
            if (a[3] - a[0]).length > 0.02:
                s0 = len(P.F)
                end_cap(P, a, xs_, inner_band or deck_band)
                yy = a[0].y
                P.orient(s0, lambda c, yy=yy, sgn=sgn: Vector((c.x, yy - sgn, c.z)))
    return dict(rings=rings, ts=ts, deck=deck_rings)


# deck plank strips: strip width per deck band (m); LOD 1 doubles it, LOD 2+ uses one strip for the whole deck
DECK_STRIP_W = {'deck': 2.0, 'plank_oiled': 1.6, 'reed': 1.0, 'steel_deck': 6.0, 'composite': 4.0}
LOD = 0          # set by ship_defs.Ctx for the mesh being built


def deck_strip_lines(top, band):
    """x positions of the strip boundaries, symmetric about the centreline, covering the widest deck station."""
    xmax = max(a[2].x for a in top)
    if xmax <= 1e-4:
        return [-1e-3, 1e-3]
    w = DECK_STRIP_W.get(band, 3.0) * (2.0 if LOD == 1 else 1.0)
    n = 1 if LOD >= 2 else max(1, int(math.ceil(2.0 * xmax / w - 1e-6)))
    w = 2.0 * xmax / n
    return [-xmax + w * k for k in range(n + 1)]


def _clip(poly, x0, x1):
    """Sutherland-Hodgman clip of a polygon [(x, s)] to the slab x0 <= x <= x1."""
    for lim, keep_ge in ((x0, True), (x1, False)):
        out = []
        m = len(poly)
        for i in range(m):
            p, q = poly[i], poly[(i + 1) % m]
            pin = p[0] >= lim if keep_ge else p[0] <= lim
            qin = q[0] >= lim if keep_ge else q[0] <= lim
            if pin:
                out.append(p)
            if pin != qin and abs(q[0] - p[0]) > 1e-12:
                f = (lim - p[0]) / (q[0] - p[0])
                out.append((lim, p[1] + (q[1] - p[1]) * f))
        poly = out
        if len(poly) < 3:
            return []
    # drop repeated points
    clean = []
    for p in poly:
        if not clean or abs(p[0] - clean[-1][0]) > 1e-6 or abs(p[1] - clean[-1][1]) > 1e-6:
            clean.append(p)
    if len(clean) > 1 and abs(clean[0][0] - clean[-1][0]) < 1e-6 and abs(clean[0][1] - clean[-1][1]) < 1e-6:
        clean.pop()
    return clean if len(clean) >= 3 else []


def deck_strips(P, top, xs, band):
    """Deck between consecutive stations, cut into strips at xs. V = position across the strip, U = along the ship,
    both planar, so planks run straight fore and aft. Returns, per station gap, the s-positions where the strip lines
    cross the port and starboard deck edges (the inner bulwark gets the same vertices, so there are no T-junctions)."""
    w = xs[1] - xs[0]
    ku = 1.0 / max(w * aspect(band), 1e-6)
    cuts = []
    for i in range(len(top) - 1):
        a, b = top[i], top[i + 1]
        xa, xb = a[2].x, b[2].x
        pa, pb = a[1], b[1]                       # port deck points (y, z per station)
        poly = [(-xa, 0.0), (xa, 0.0), (xb, 1.0), (-xb, 1.0)]

        def to3(x, s, pa=pa, pb=pb):
            return Vector((x, pa.y + (pb.y - pa.y) * s, pa.z + (pb.z - pa.z) * s))
        for k in range(len(xs) - 1):
            cp = _clip(poly, xs[k], xs[k + 1])
            if not cp:
                continue
            pts = [to3(x, s) for x, s in cp]
            area = 0.0
            for j in range(1, len(pts) - 1):
                area += (pts[j] - pts[0]).cross(pts[j + 1] - pts[0]).length * 0.5
            if area < 1e-6:
                continue
            ids = [P.v(p) for p in pts]
            P.face(ids, band, [(p.y * ku, min(max((p.x - xs[k]) / w, 0.0), 1.0)) for p in pts], False)
            if (P.normal(P.F[-1]).z) < 0:
                P.F[-1][0].reverse()
                P.F[-1][2].reverse()
        # strip lines crossing the port edge (-xa -> -xb) and the starboard edge (xa -> xb)
        cs = []
        if abs(xb - xa) > 1e-9:
            for x in xs[1:-1]:
                s = (x - xa) / (xb - xa)
                if 1e-6 < s < 1 - 1e-6:
                    cs.append(s)
        cuts.append(sorted(cs))
    return cuts


def bulwark_inner(P, top, cuts, band):
    """Inner bulwark faces from the rail (a[0], a[3]) down to the deck edge (a[1], a[2]), with the deck-strip
    vertices on the bottom edge. UV like loft(): u along the stations, vl 0 at the deck edge, 1 at the rail."""
    g = sum((a[0] - a[1]).length for a in top) / len(top)
    ku = 1.0 / max(g * aspect(band), 1e-6)
    ud = [0.0]
    for i in range(1, len(top)):
        ud.append(ud[-1] + (top[i][1] - top[i - 1][1]).length)
    for i in range(len(top) - 1):
        a, b = top[i], top[i + 1]
        for lo_, hi_ in ((1, 0), (2, 3)):
            bottom = [(a[lo_], ud[i])]
            for s in cuts[i]:
                bottom.append((a[lo_].lerp(b[lo_], s), ud[i] + (ud[i + 1] - ud[i]) * s))
            bottom.append((b[lo_], ud[i + 1]))
            pts = [p for p, _ in bottom] + [b[hi_], a[hi_]]
            uvs = [(u * ku, 0.0) for _, u in bottom] + [(ud[i + 1] * ku, 1.0), (ud[i] * ku, 1.0)]
            if (pts[0] - pts[-1]).length < 1e-5 and (pts[len(bottom) - 1] - pts[len(bottom)]).length < 1e-5:
                continue
            P.face([P.v(p) for p in pts], band, uvs, False)


def end_cap(P, a, xs, band):
    """Bulwark end face above the deck at the first / last station, with the strip vertices on its deck edge."""
    xd = a[2].x
    bottom = [a[1]] + [Vector((x, a[1].y, a[1].z)) for x in xs[1:-1] if -xd + 1e-6 < x < xd - 1e-6] + [a[2]]
    lu = (a[2] - a[1]).length
    lv = max((a[0] - a[1]).length, 1e-6)
    u2 = lu / max(lv * aspect(band), 1e-6)
    uvs = [(u2 * ((p - a[1]).length / max(lu, 1e-9)), 0.0) for p in bottom] + [(u2, 1.0), (0.0, 1.0)]
    P.face([P.v(p) for p in bottom + [a[3], a[0]]], band, uvs, False)


def x_at(sec, z):
    """Half breadth of a section polyline [(x, z)...] at height z."""
    for k in range(len(sec) - 1):
        (x0, z0), (x1, z1) = sec[k], sec[k + 1]
        if (z0 - z) * (z1 - z) <= 0 and z1 != z0:
            return x0 + (x1 - x0) * (z - z0) / (z1 - z0)
    return sec[-1][0] if z >= sec[-1][1] else sec[0][0]


def deck_halfwidth(hs, t, deck_z, thick=0.12):
    """Inner half width of the deck at t (approx: the hull half beam at sheer minus the bulwark thickness)."""
    return max(hs.half_beam(t) - thick, 0.0)


def t_of_y(hs, y):
    return (y + hs.L * 0.5) / hs.L
