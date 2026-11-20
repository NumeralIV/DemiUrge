# DEMIURGE art / wonders-b - mesh builder and primitives (surfaces of revolution, boxes, tubes, rocks).
#
# Faces carry a material slot (0 Structure, 1 Roof, 2 Detail) and per-corner UVs into a 4-band atlas:
# u tiles freely (metres / band U), v stays inside the face's band (one band period per strip of faces).
import math
import random

TAU = math.tau
INSET = 12.0 / 2048.0


class Ctx:
    """Level-of-detail context: q scales segment counts, vmul merges texture rows (stretch) at low LODs."""
    q = 1.0
    vmul = 1.0
    lod = 0


CTX = Ctx()


def S(n, lo=3, mult=1):
    v = max(lo, int(round(n * CTX.q)))
    if mult > 1:
        v = max(lo, int(round(v / mult)) * mult)
    return v


def atlas_v(band, local):
    if isinstance(band, tuple):
        # (band, lo, hi): map into a sub-range of the band (lo == hi pins every vertex to one texel row: light spots)
        b, lo, hi = band
        local = lo + (hi - lo) * min(1.0, max(0.0, local))
        return b * 0.25 + local * 0.25
    local = min(1.0, max(0.0, local))
    return band * 0.25 + INSET + local * (0.25 - 2 * INSET)


class SpotSlot(int):
    """Material slot index flagged as a light spot: faces get a constant u on the band's emissive spot."""
    spot = True


class SB:
    """Slot + band + band size in metres."""
    __slots__ = ('slot', 'band', 'U', 'V')

    def __init__(self, slot, band, U, V):
        self.slot, self.band, self.U, self.V = slot, band, U, V

    def __repr__(self):
        return 'SB(%d,%d)' % (self.slot, self.band)


class MB:
    def __init__(self):
        self.V = []
        self.F = []
        self.UV = []
        self.M = []
        self.soft = set()

    def vert(self, p):
        self.V.append((float(p[0]), float(p[1]), float(p[2])))
        return len(self.V) - 1

    def face(self, ids, uvs, slot):
        if getattr(slot, 'spot', False):
            uvs = tuple((0.015, uv[1]) for uv in uvs)
        self.F.append(tuple(ids))
        self.UV.append(tuple(uvs))
        self.M.append(int(slot))

    def tris(self):
        return sum(len(f) - 2 for f in self.F)

    def extend(self, o, xf=None):
        off = len(self.V)
        for p in o.V:
            self.V.append(xf(p) if xf else p)
        foff = len(self.F)
        for f, uv, m in zip(o.F, o.UV, o.M):
            self.F.append(tuple(i + off for i in f))
            self.UV.append(uv)
            self.M.append(m)
        self.soft.update(i + foff for i in o.soft)

    def bounds(self):
        xs = [p[0] for p in self.V]
        ys = [p[1] for p in self.V]
        zs = [p[2] for p in self.V]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def xform(rotz=0.0, t=(0, 0, 0), s=1.0):
    c, sn = math.cos(rotz), math.sin(rotz)

    def f(p):
        x, y, z = p[0] * s, p[1] * s, p[2] * s
        return (x * c - y * sn + t[0], x * sn + y * c + t[1], z + t[2])
    return f


# ------------------------------------------------------------------------------------------ profiles

def subdivide_profile(prof, alt=None):
    """prof: [(r, z, sb)] (sb of the segment starting at that point; last may be None).
    Splits segments so that each piece maps about one band period in v. Returns list of points
    (r, z, sb, vloc_start, vloc_end, alt_r, alt_z) where vloc are the band-local v of the piece
    starting at this point."""
    out = []
    for i in range(len(prof) - 1):
        r0, z0, sb = prof[i][0], prof[i][1], prof[i][2]
        r1, z1 = prof[i + 1][0], prof[i + 1][1]
        a0 = alt[i] if alt else (r0, z0)
        a1 = alt[i + 1] if alt else (r1, z1)
        L = math.hypot(r1 - r0, z1 - z0)
        Veff = sb.V * CTX.vmul if sb else 1.0
        n = max(1, int(round(L / Veff))) if sb else 1
        piece = L / n
        span = 1.0 if piece >= 0.5 * Veff else piece / Veff
        for k in range(n):
            t = k / n
            out.append((r0 + (r1 - r0) * t, z0 + (z1 - z0) * t, sb, 0.0, span,
                        a0[0] + (a1[0] - a0[0]) * t, a0[1] + (a1[1] - a0[1]) * t))
    r, z = prof[-1][0], prof[-1][1]
    a = alt[-1] if alt else (r, z)
    out.append((r, z, None, 0.0, 0.0, a[0], a[1]))
    return out


def revolve(mb, prof, segs, a0=0.0, a1=TAU, jag=None, keep=None, alt=None, morph=None, alt_sb=None,
            ucount=None, caps=False, sbfn=None, xf=None, rwarp=None):
    """Surface of revolution about Z. Profile order: outer walls upward, tops inward, inner walls downward
    (counter-clockwise around the solid in the r-z plane) gives outward normals.
    jag(theta) -> max z clamp; keep(theta, r, z, i) -> bool face filter; alt/morph: alternative profile
    blended by morph(theta) in 0..1; alt_sb(i) -> SB used where morph > 0.5; ucount: fixed u periods
    around (domes); sbfn(i, theta, sb) -> SB override."""
    full = abs((a1 - a0) - TAU) < 1e-6
    P = subdivide_profile(prof, alt)
    ncol = segs if full else segs + 1
    rows = []
    for i, p in enumerate(P):
        row = []
        for j in range(ncol):
            th = a0 + (a1 - a0) * j / segs
            r, z = p[0], p[1]
            if morph:
                w = morph(th)
                r = r + (p[5] - r) * w
                z = z + (p[6] - z) * w
            if jag:
                z = min(z, jag(th))
            if rwarp:
                r = r * rwarp(th, z)
            q = (r * math.cos(th), r * math.sin(th), z)
            if xf:
                q = xf(q)
            row.append((mb.vert(q), r, z))
        rows.append(row)
    for i in range(len(P) - 1):
        sb0 = P[i][2]
        if sb0 is None:
            continue
        for j in range(segs):
            j1 = (j + 1) % ncol if full else j + 1
            thm = a0 + (a1 - a0) * (j + 0.5) / segs
            A, B, C, D = rows[i][j], rows[i][j1], rows[i + 1][j1], rows[i + 1][j]
            rm = (A[1] + C[1]) * 0.5
            zm = (A[2] + C[2]) * 0.5
            if keep and not keep(thm, rm, zm, i):
                continue
            sb = sb0
            if morph and alt_sb is not None and morph(thm) > 0.5:
                sb = alt_sb(i, sb0) or sb0
            if sbfn:
                sb = sbfn(i, thm, sb) or sb
            v0 = atlas_v(sb.band, P[i][3])
            v1 = atlas_v(sb.band, P[i][4])

            nface = max(1, int(round(TAU * max(rm, 0.01) / sb.U)))

            def U(rad, jj):
                th = a0 + (a1 - a0) * jj / segs
                if ucount:
                    return (th - a0) / TAU * ucount
                if full:
                    return (th - a0) / TAU * nface
                return (th - a0) * max(rad, 0.01) / sb.U
            ra, rb = A[1], D[1]
            uvs = ((U(ra, j), v0), (U(ra, j + 1), v0), (U(rb, j + 1), v1), (U(rb, j), v1))
            mb.face((A[0], B[0], C[0], D[0]), uvs, sb.slot)
    if caps and not full:
        for end, th in ((0, a0), (1, a1)):
            col = 0 if end == 0 else ncol - 1
            ids = [rows[i][col][0] for i in range(len(P))]
            pts = [mb.V[k] for k in ids]
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            cz = sum(p[2] for p in pts) / len(pts)
            c = mb.vert((cx, cy, cz))
            sb = P[0][2]
            for k in range(len(ids) - 1):
                a, b = ids[k], ids[k + 1]
                pa, pb = mb.V[a], mb.V[b]
                uv = ((math.hypot(pa[0], pa[1]) / sb.U, atlas_v(sb.band, 0.2)),
                      (math.hypot(pb[0], pb[1]) / sb.U, atlas_v(sb.band, 0.8)),
                      (math.hypot(cx, cy) / sb.U, atlas_v(sb.band, 0.5)))
                if end == 0:
                    mb.face((a, b, c), uv, sb.slot)
                else:
                    mb.face((b, a, c), (uv[1], uv[0], uv[2]), sb.slot)
    return rows


def ring_profile_box(r_out, r_in, z0, z1, sb_out, sb_top, sb_in, sb_bot=None):
    """Closed rectangular ring cross-section, CCW (outer up, top inward, inner down, bottom outward)."""
    p = [(r_out, z0, sb_out), (r_out, z1, sb_top), (r_in, z1, sb_in), (r_in, z0, sb_bot)]
    if sb_bot:
        p.append((r_out, z0, None))
    return p


# ------------------------------------------------------------------------------------------ walls / boxes

def wall_quad(mb, sb, p0, p1, z0, z1, u0=0.0, q0=None, q1=None, zt0=None, zt1=None):
    """Vertical quad from p0 to p1 (xy), split in rows of one band period. q0/q1: top xy (taper)."""
    q0 = q0 or p0
    q1 = q1 or p1
    zt0 = z1 if zt0 is None else zt0
    zt1 = z1 if zt1 is None else zt1
    L = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    H = max(zt0, zt1) - z0
    Veff = sb.V * CTX.vmul
    n = max(1, int(round(H / Veff)))
    span = 1.0 if H / n >= 0.5 * Veff else (H / n) / Veff
    prev = None
    for k in range(n + 1):
        t = k / n
        a = (p0[0] + (q0[0] - p0[0]) * t, p0[1] + (q0[1] - p0[1]) * t, z0 + (zt0 - z0) * t)
        b = (p1[0] + (q1[0] - p1[0]) * t, p1[1] + (q1[1] - p1[1]) * t, z0 + (zt1 - z0) * t)
        ia, ib = mb.vert(a), mb.vert(b)
        if prev:
            pa, pb = prev
            v0, v1 = atlas_v(sb.band, 0.0), atlas_v(sb.band, span)
            u1 = u0 + L / sb.U
            mb.face((pa, pb, ib, ia), ((u0, v0), (u1, v0), (u1, v1), (u0, v1)), sb.slot)
        prev = (ia, ib)
    return u0 + L / sb.U


def flat_quad(mb, sb, c0, c1, c2, c3, along=0):
    """Planar quad (CCW from the visible side), split into strips one band period wide.
    along: 0 -> u along c0->c1, strips advance c0->c3."""
    L = math.dist(c0, c1)
    W = math.dist(c0, c3)
    Veff = sb.V * CTX.vmul
    n = max(1, int(round(W / Veff)))
    span = 1.0 if W / n >= 0.5 * Veff else (W / n) / Veff
    prev = None
    for k in range(n + 1):
        t = k / n
        a = tuple(c0[i] + (c3[i] - c0[i]) * t for i in range(3))
        b = tuple(c1[i] + (c2[i] - c1[i]) * t for i in range(3))
        ia, ib = mb.vert(a), mb.vert(b)
        if prev:
            pa, pb = prev
            v0, v1 = atlas_v(sb.band, 0.0), atlas_v(sb.band, span)
            mb.face((pa, pb, ib, ia), ((0, v0), (L / sb.U, v0), (L / sb.U, v1), (0, v1)), sb.slot)
        prev = (ia, ib)


def box(mb, sb, cx, cy, z0, z1, w, d, rot=0.0, top=None, bottom=False, taper=1.0, taper_d=None):
    """Axis box rotated by rot about Z at (cx, cy). sb: sides; top: SB for the roof (None = side SB)."""
    top = top or sb
    td = taper if taper_d is None else taper_d
    c, s = math.cos(rot), math.sin(rot)

    def P(x, y, z):
        return (cx + x * c - y * s, cy + x * s + y * c, z)
    hw, hd = w * 0.5, d * 0.5
    base_ = [(-hw, -hd), (hw, -hd), (hw, hd), (-hw, hd)]
    tp = [(x * taper, y * td) for x, y in base_]
    u = 0.0
    for k in range(4):
        x0, y0 = base_[k]
        x1, y1 = base_[(k + 1) % 4]
        X0, Y0 = tp[k]
        X1, Y1 = tp[(k + 1) % 4]
        p0, p1 = P(x0, y0, 0), P(x1, y1, 0)
        q0, q1 = P(X0, Y0, 0), P(X1, Y1, 0)
        u = wall_quad(mb, sb, p0[:2], p1[:2], z0, z1, u, q0[:2], q1[:2])
    if top is not False:
        flat_quad(mb, top, P(tp[0][0], tp[0][1], z1), P(tp[1][0], tp[1][1], z1), P(tp[2][0], tp[2][1], z1), P(tp[3][0], tp[3][1], z1))
    if bottom:
        flat_quad(mb, sb, P(-hw, hd, z0), P(hw, hd, z0), P(hw, -hd, z0), P(-hw, -hd, z0))


def beam(mb, sb, p0, p1, w, h=None, up=(0, 0, 1)):
    """Rectangular beam between two 3D points (4 sides, open ends)."""
    h = w if h is None else h
    tube(mb, sb, [p0, p1], w * 0.7071, sides=4, phase=math.pi / 4, up=up)


def _frame(t, up):
    ux, uy, uz = up
    if abs(t[0] * ux + t[1] * uy + t[2] * uz) > 0.95:
        ux, uy, uz = (1, 0, 0) if abs(t[0]) < 0.9 else (0, 1, 0)
    # n = normalize(up - t (t.up))
    d = t[0] * ux + t[1] * uy + t[2] * uz
    n = (ux - t[0] * d, uy - t[1] * d, uz - t[2] * d)
    ln = math.sqrt(sum(a * a for a in n))
    n = tuple(a / ln for a in n)
    b = (t[1] * n[2] - t[2] * n[1], t[2] * n[0] - t[0] * n[2], t[0] * n[1] - t[1] * n[0])
    return n, b


def tube(mb, sb, pts, r, sides=6, caps=False, r_end=None, phase=0.0, up=(0, 0, 1), radii=None, vr=(0.0, 1.0)):
    """Tube along a polyline. u along the length (tiles), v around (one band). Outward normals."""
    if len(pts) < 2:
        return
    radii = radii or [r + ((r_end if r_end is not None else r) - r) * i / (len(pts) - 1) for i in range(len(pts))]
    rings = []
    L = 0.0
    lengths = [0.0]
    for i in range(1, len(pts)):
        L += math.dist(pts[i - 1], pts[i])
        lengths.append(L)
    for i, p in enumerate(pts):
        if i == 0:
            t = [pts[1][k] - pts[0][k] for k in range(3)]
        elif i == len(pts) - 1:
            t = [pts[i][k] - pts[i - 1][k] for k in range(3)]
        else:
            t = [pts[i + 1][k] - pts[i - 1][k] for k in range(3)]
        lt = math.sqrt(sum(a * a for a in t)) or 1.0
        t = [a / lt for a in t]
        n, b = _frame(t, up)
        ring = []
        for k in range(sides):
            a = phase + TAU * k / sides
            ca, sa = math.cos(a) * radii[i], math.sin(a) * radii[i]
            ring.append(mb.vert((p[0] + n[0] * ca + b[0] * sa, p[1] + n[1] * ca + b[1] * sa, p[2] + n[2] * ca + b[2] * sa)))
        rings.append(ring)
    for i in range(len(pts) - 1):
        u0, u1 = lengths[i] / sb.U, lengths[i + 1] / sb.U
        for k in range(sides):
            k1 = (k + 1) % sides
            v0 = atlas_v(sb.band, vr[0] + (vr[1] - vr[0]) * k / sides)
            v1 = atlas_v(sb.band, vr[0] + (vr[1] - vr[0]) * (k + 1) / sides)
            mb.face((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]),
                    ((u0, v0), (u0, v1), (u1, v1), (u1, v0)), sb.slot)
    if caps:
        for end in (0, len(pts) - 1):
            ring = rings[end]
            p = pts[end]
            c = mb.vert(p)
            for k in range(sides):
                k1 = (k + 1) % sides
                uv = ((0.0, atlas_v(sb.band, 0.2)), (0.1, atlas_v(sb.band, 0.8)), (0.05, atlas_v(sb.band, 0.5)))
                if end == 0:
                    mb.face((ring[k1], ring[k], c), uv, sb.slot)
                else:
                    mb.face((ring[k], ring[k1], c), uv, sb.slot)


def cylinder(mb, sb, cx, cy, z0, z1, r, segs, r_top=None, top=None, top_cap=True, xf=None):
    r_top = r if r_top is None else r_top
    prof = [(r, z0, sb), (r_top, z1, top or sb if top_cap else None)]
    if top_cap:
        prof.append((0.0, z1, None))
    revolve(mb, prof, segs, xf=xform(0, (cx, cy, 0)) if xf is None else xf)


def lathe_at(mb, prof, segs, cx, cy, **kw):
    kw.setdefault('xf', xform(0, (cx, cy, 0)))
    return revolve(mb, prof, segs, **kw)


# ------------------------------------------------------------------------------------------ organic

_ICO = None


def _icosphere(sub):
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=1.0)
    verts = [tuple(v.co) for v in bm.verts]
    faces = [tuple(v.index for v in f.verts) for f in bm.faces]
    bm.free()
    return verts, faces


_ICO_CACHE = {}


def blob(mb, sb, c, size, seed, sub=1, squash=1.0, rough=0.25, flat_bottom=None, stretch=(1, 1, 1), soft=False):
    """Displaced icosphere (rocks, rubble, canopy clumps). UV: u around (metres), v by height."""
    if sub not in _ICO_CACHE:
        _ICO_CACHE[sub] = _icosphere(sub)
    verts, faces = _ICO_CACHE[sub]
    rnd = random.Random(seed)
    ph = [rnd.uniform(0, TAU) for _ in range(6)]
    ids = []
    pts = []
    for v in verts:
        x, y, z = v
        d = 1.0 + rough * (math.sin(3.1 * x + ph[0]) * math.sin(2.7 * y + ph[1]) * 0.6
                           + math.sin(5.3 * z + ph[2]) * 0.4 + math.sin(4.1 * x + 3.3 * z + ph[3]) * 0.3)
        p = [x * size * d * stretch[0], y * size * d * stretch[1], z * size * d * squash * stretch[2]]
        if flat_bottom is not None:
            p[2] = max(p[2], flat_bottom)
        q = (c[0] + p[0], c[1] + p[1], c[2] + p[2])
        pts.append(q)
        ids.append(mb.vert(q))
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    if soft:
        # verifier fix: canopies get one continuous wrap-around mapping (u around the blob, v by height). The
        # per-face box projection gave every face its own tangent frame, so the normal map faceted the crowns and
        # the texture broke into a mosaic at every edge.
        circ = TAU * size * max(stretch[0], stretch[1])
        per = max(1.0, round(circ / sb.U))
        zext = max(hi[2] - lo[2], 1e-6)
        for f in faces:
            us, vs = [], []
            for i in f:
                vx, vy, vz = verts[i]
                us.append((math.atan2(vy, vx) / TAU) * per if (abs(vx) + abs(vy)) > 1e-6 else None)
                vs.append((pts[i][2] - lo[2]) / zext)
            known = [x for x in us if x is not None]
            ref = known[0] if known else 0.0
            us = [ref if x is None else x for x in us]
            us = [x + per if ref - x > per / 2 else (x - per if x - ref > per / 2 else x) for x in us]
            uvs = [(us[k], atlas_v(sb.band, vs[k])) for k in range(len(f))]
            mb.face(tuple(ids[i] for i in f), tuple(uvs), sb.slot)
            mb.soft.add(len(mb.F) - 1)
        return
    for f in faces:
        # box projection on the face's dominant axis (no polar stretching)
        a, b, c3 = pts[f[0]], pts[f[1]], pts[f[2]]
        n = [(b[1] - a[1]) * (c3[2] - a[2]) - (b[2] - a[2]) * (c3[1] - a[1]),
             (b[2] - a[2]) * (c3[0] - a[0]) - (b[0] - a[0]) * (c3[2] - a[2]),
             (b[0] - a[0]) * (c3[1] - a[1]) - (b[1] - a[1]) * (c3[0] - a[0])]
        ax = max(range(3), key=lambda k: abs(n[k]))
        ua, va = {0: (1, 2), 1: (0, 2), 2: (0, 1)}[ax]
        ext = max(hi[va] - lo[va], sb.V)
        uvs = [(pts[i][ua] / sb.U, atlas_v(sb.band, (pts[i][va] - lo[va]) / ext)) for i in f]
        mb.face(tuple(ids[i] for i in f), tuple(uvs), sb.slot)
        if soft:
            mb.soft.add(len(mb.F) - 1)


def rubble(mb, sb, cx, cy, radius, count, seed, size=(0.6, 2.2), z=0.0):
    rnd = random.Random(seed)
    for k in range(count):
        a = rnd.uniform(0, TAU)
        d = radius * math.sqrt(rnd.random())
        s = rnd.uniform(*size)
        blob(mb, sb, (cx + d * math.cos(a), cy + d * math.sin(a), z + s * 0.25), s, seed * 31 + k, sub=1,
             squash=0.55, rough=0.35, flat_bottom=-s * 0.35)


def scaffold(mb, sb, cx, cy, z0, z1, w, d, rot=0.0, bay=4.0, lift=4.0, r=0.12):
    """Pole scaffold around a box footprint: standards, ledgers and a few braces."""
    c, s = math.cos(rot), math.sin(rot)

    def P(x, y, z):
        return (cx + x * c - y * s, cy + x * s + y * c, z)
    hw, hd = w * 0.5, d * 0.5
    nx = max(1, int(round(w / bay)))
    ny = max(1, int(round(d / bay)))
    pts = []
    for i in range(nx + 1):
        pts.append((-hw + w * i / nx, -hd))
        pts.append((-hw + w * i / nx, hd))
    for j in range(1, ny):
        pts.append((-hw, -hd + d * j / ny))
        pts.append((hw, -hd + d * j / ny))
    for x, y in pts:
        tube(mb, sb, [P(x, y, z0), P(x, y, z1)], r, sides=4)
    nl = max(1, int((z1 - z0) / lift))
    for k in range(1, nl + 1):
        z = z0 + k * (z1 - z0) / nl
        corners = [P(-hw, -hd, z), P(hw, -hd, z), P(hw, hd, z), P(-hw, hd, z)]
        for i in range(4):
            tube(mb, sb, [corners[i], corners[(i + 1) % 4]], r * 0.8, sides=4)
    tube(mb, sb, [P(-hw, -hd, z0), P(hw, -hd, z1)], r * 0.8, sides=4)
    tube(mb, sb, [P(hw, hd, z0), P(-hw, hd, z1)], r * 0.8, sides=4)


def scaffold_ring(mb, sb, cx, cy, radius, z0, z1, n=24, lift=4.0, r=0.14, a0=0.0, a1=TAU):
    full = abs(a1 - a0 - TAU) < 1e-6
    cols = n if full else n + 1
    pts = []
    for i in range(cols):
        a = a0 + (a1 - a0) * i / n
        pts.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))
    for x, y in pts:
        tube(mb, sb, [(x, y, z0), (x, y, z1)], r, sides=4)
    nl = max(1, int((z1 - z0) / lift))
    for k in range(1, nl + 1):
        z = z0 + k * (z1 - z0) / nl
        ring = [(x, y, z) for x, y in pts] + ([(pts[0][0], pts[0][1], z)] if full else [])
        tube(mb, sb, ring, r * 0.8, sides=4)


def crane(mb, sb, x, y, h, jib, rot=0.0, w=2.2):
    """Lattice tower crane (mast, jib, counter-jib) in one material band."""
    c, s = math.cos(rot), math.sin(rot)
    hw = w * 0.5
    corners = [(-hw, -hw), (hw, -hw), (hw, hw), (-hw, hw)]
    for cx_, cy_ in corners:
        tube(mb, sb, [(x + cx_, y + cy_, 0), (x + cx_, y + cy_, h)], 0.18, sides=4)
    n = max(2, int(h / 4))
    for k in range(n):
        z0, z1 = h * k / n, h * (k + 1) / n
        for i in range(4):
            a, b = corners[i], corners[(i + 1) % 4]
            tube(mb, sb, [(x + a[0], y + a[1], z0), (x + b[0], y + b[1], z1)], 0.08, sides=3)
    tip = (x + jib * c, y + jib * s, h + 1.0)
    back = (x - jib * 0.3 * c, y - jib * 0.3 * s, h + 1.0)
    tube(mb, sb, [back, tip], 0.5, sides=4)
    tube(mb, sb, [(x, y, h + 6.0), tip], 0.08, sides=3)
    tube(mb, sb, [(x, y, h + 6.0), back], 0.08, sides=3)
    box(mb, sb, back[0], back[1], h - 1.0, h + 1.5, 3.0, 2.5, rot)
    tube(mb, sb, [(tip[0] * 0.6 + x * 0.4, tip[1] * 0.6 + y * 0.4, h + 0.5), (tip[0] * 0.6 + x * 0.4, tip[1] * 0.6 + y * 0.4, h * 0.45)], 0.05, sides=3)


# ------------------------------------------------------------------------------------------ Blender

def to_object(mb, name, mats, merge=0.0005, sharp_deg=32.0, collection=None):
    import bpy
    import bmesh
    me = bpy.data.meshes.new(name)
    me.from_pydata(mb.V, [], mb.F)
    me.update()
    uvl = me.uv_layers.new(name='UVMap')
    flat = []
    for poly, uvs in zip(me.polygons, mb.UV):
        for uv in uvs:
            flat.extend(uv)
    uvl.data.foreach_set('uv', flat)
    me.polygons.foreach_set('material_index', mb.M)
    for m in mats:
        me.materials.append(m)
    bm = bmesh.new()
    bm.from_mesh(me)
    soft_l = bm.faces.layers.int.new('dm_soft')
    if mb.soft:
        bm.faces.ensure_lookup_table()
        for i in mb.soft:
            if i < len(bm.faces):
                bm.faces[i][soft_l] = 1
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=merge)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=merge)
    # drop zero-area faces that survive
    bad = [f for f in bm.faces if f.calc_area() < 1e-7]
    if bad:
        bmesh.ops.delete(bm, geom=bad, context='FACES')
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context='VERTS')
    # smooth shading with sharp edges by angle; edges between two 'soft' faces (canopies) stay smooth
    lim = math.radians(sharp_deg)
    for f in bm.faces:
        f.smooth = True
    # verifier fix: the soft flags used to be collected into a set of BMFace wrappers and the layer removed
    # first; removing the layer invalidates those wrappers, so no edge was ever treated as soft and every canopy
    # was shaded by the 32 degree angle rule (faceted blossom crowns). Read the layer directly, remove it after.
    for e in bm.edges:
        lf = e.link_faces
        if len(lf) != 2:
            e.smooth = True
            continue
        if lf[0][soft_l] and lf[1][soft_l]:
            e.smooth = True
            continue
        e.smooth = lf[0].normal.angle(lf[1].normal, 0.0) <= lim
    bm.faces.layers.int.remove(soft_l)
    bm.to_mesh(me)
    bm.free()
    me.validate(clean_customdata=False)
    ob = bpy.data.objects.new(name, me)
    (collection or bpy.context.scene.collection).objects.link(ob)
    return ob


def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)
