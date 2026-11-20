# DEMIURGE art - Age4 geometry library: parts, primitives, facades with real window reveals, roofs,
# damage states (exact booleans per part), LOD assembly, FBX export with LOD groups and sockets.
#
# Conventions (docs/ASSETS.md): metres, Z up, front faces -Y, pivot at the footprint centre on the ground,
# foundations 0.3 m below Z = 0. Material slots: 0 Structure, 1 Roof, 2 Detail (trim sheet).
# Vertex colour "Col" (linear, per face corner): R = per-window random id (night lit fraction threshold),
# G = soot / char amount (damage states), B = culture accent mask (doors, frames), A = 1.
import math
import random

import bmesh
import bpy
from mathutils import Vector

import a4tex as TX

S, R, D = 0, 1, 2
CTX = {'tile': {S: 1.8, R: 2.0, D: None}}   # metres per texture tile of the slot's tiling material
COL0 = (0.0, 0.0, 0.0, 1.0)
SENT = -1000.0


# --------------------------------------------------------------------------- small vector helpers

def v3(x, y, z):
    return Vector((float(x), float(y), float(z)))


def newell(pts):
    n = Vector((0.0, 0.0, 0.0))
    k = len(pts)
    for i in range(k):
        a = pts[i]
        b = pts[(i + 1) % k]
        n.x += (a[1] - b[1]) * (a[2] + b[2])
        n.y += (a[2] - b[2]) * (a[0] + b[0])
        n.z += (a[0] - b[0]) * (a[1] + b[1])
    if n.length < 1e-12:
        return Vector((0, 0, 1))
    return n.normalized()


def frame_axes(n):
    if abs(n.z) > 0.999:
        return Vector((1, 0, 0)), Vector((0, 1, 0)) if n.z > 0 else Vector((0, -1, 0))
    t = Vector((0, 0, 1)).cross(n).normalized()
    b = n.cross(t).normalized()
    return t, b


def frame_uv(pts, mat):
    tile = CTX['tile'].get(mat) or 2.0
    n = newell(pts)
    t, b = frame_axes(n)
    return [(Vector(p).dot(t) / tile, Vector(p).dot(b) / tile) for p in pts]


def band_uv(pts, band, axis=None):
    v0, v1, metres = TX.band_rect(band)
    n = newell(pts)
    P = [Vector(p) for p in pts]
    if axis is None:
        # longest edge direction
        best, axis = -1, Vector((1, 0, 0))
        for i in range(len(P)):
            e = P[(i + 1) % len(P)] - P[i]
            if e.length > best:
                best, axis = e.length, e
        axis = axis.normalized()
    else:
        axis = Vector(axis).normalized()
    w = n.cross(axis)
    if w.length < 0.2:     # end face: a small patch from the middle of the band
        t, b = frame_axes(n)
        vm = (v0 + v1) * 0.5
        return [(p.dot(t) / metres, vm + ((p.dot(b) / metres) % 1.0 - 0.5) * (v1 - v0) * 0.3) for p in P]
    w.normalize()
    ws = [p.dot(w) for p in P]
    lo, hi = min(ws), max(ws)
    span = max(hi - lo, 1e-6)
    return [(p.dot(axis) / metres, v0 + (wv - lo) / span * (v1 - v0)) for p, wv in zip(P, ws)]


def tile_uv(pts, tile, origin, ax_u, ax_v, su, sv):
    u0, v0, u1, v1 = TX.tile_rect(tile)
    o = Vector(origin)
    au = Vector(ax_u).normalized()
    av = Vector(ax_v).normalized()
    out = []
    for p in pts:
        d = Vector(p) - o
        fu = min(max(d.dot(au) / su, 0.0), 1.0)
        fv = min(max(d.dot(av) / sv, 0.0), 1.0)
        out.append((u0 + fu * (u1 - u0), v0 + fv * (v1 - v0)))
    return out


def planar_tile_uv(pts, tile, x0, y0, x1, y1):
    return tile_uv(pts, tile, (x0, y0, 0), (1, 0, 0), (0, 1, 0), max(x1 - x0, 1e-3), max(y1 - y0, 1e-3))


# --------------------------------------------------------------------------- deterministic noise

def _hash2(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def vnoise(x, y, seed=0):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    a = _hash2(ix, iy, seed)
    b = _hash2(ix + 1, iy, seed)
    c = _hash2(ix, iy + 1, seed)
    d = _hash2(ix + 1, iy + 1, seed)
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy


def fbm2(x, y, seed=0, octaves=3):
    s, amp, tot, f = 0.0, 1.0, 0.0, 1.0
    for o in range(octaves):
        s += amp * vnoise(x * f, y * f, seed + o * 17)
        tot += amp
        amp *= 0.5
        f *= 2.0
    return s / tot


# --------------------------------------------------------------------------- Part

class Part:
    def __init__(self, kind='solid', tags=(), capband='stone'):
        self.v = []
        self.f = []
        self.fm = []
        self.fuv = []
        self.fcol = []
        self.kind = kind          # 'solid' (closed, boolean-able) or 'detail' (removed / kept whole)
        self.tags = set(tags)
        self.capband = capband    # band used for Detail cap faces created by damage booleans
        self.vd = {}
        self._mark = 0

    def vi(self, p):
        k = (round(p[0], 4), round(p[1], 4), round(p[2], 4))
        i = self.vd.get(k)
        if i is None:
            i = len(self.v)
            self.vd[k] = i
            self.v.append((float(p[0]), float(p[1]), float(p[2])))
        return i

    def add(self, pts, mat, uv=None, col=None):
        idx = [self.vi(p) for p in pts]
        keep_i, keep_p, keep_uv = [], [], []
        for j, i in enumerate(idx):
            if keep_i and keep_i[-1] == i:
                continue
            keep_i.append(i)
            keep_p.append(pts[j])
            if uv is not None:
                keep_uv.append(uv[j])
        if len(keep_i) > 1 and keep_i[0] == keep_i[-1]:
            keep_i.pop()
            keep_p.pop()
            if uv is not None:
                keep_uv.pop()
        if len(keep_i) < 3 or len(set(keep_i)) < 3:
            return
        if uv is None:
            keep_uv = frame_uv(keep_p, mat)
        self.f.append(keep_i)
        self.fm.append(mat)
        self.fuv.append(keep_uv)
        self.fcol.append(col if col is not None else COL0)

    def begin(self):
        self._mark = len(self.f)

    def end_solid(self):
        """Orient the faces added since begin() outward (positive signed volume)."""
        vol = 0.0
        for fi in range(self._mark, len(self.f)):
            idx = self.f[fi]
            p0 = Vector(self.v[idx[0]])
            for k in range(1, len(idx) - 1):
                vol += p0.dot(Vector(self.v[idx[k]]).cross(Vector(self.v[idx[k + 1]])))
        if vol < 0:
            for fi in range(self._mark, len(self.f)):
                self.f[fi] = self.f[fi][::-1]
                self.fuv[fi] = self.fuv[fi][::-1]

    def bbox(self):
        xs = [p[0] for p in self.v]
        ys = [p[1] for p in self.v]
        zs = [p[2] for p in self.v]
        return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))

    def centroid(self):
        n = len(self.v)
        return Vector((sum(p[0] for p in self.v) / n, sum(p[1] for p in self.v) / n, sum(p[2] for p in self.v) / n))

    def tris(self):
        return sum(len(f) - 2 for f in self.f)

    def set_col(self, col):
        self.fcol = [col] * len(self.f)

    def transformed(self, fn):
        """Copy with every vertex mapped through fn(Vector)->Vector (rigid or mirror-free transforms)."""
        p = Part(self.kind, self.tags, self.capband)
        p.v = [tuple(fn(Vector(q))) for q in self.v]
        p.vd = {(round(q[0], 4), round(q[1], 4), round(q[2], 4)): i for i, q in enumerate(p.v)}
        p.f = [list(f) for f in self.f]
        p.fm = list(self.fm)
        p.fuv = [list(u) for u in self.fuv]
        p.fcol = list(self.fcol)
        return p


def merge_parts(parts, kind='detail', tags=()):
    out = Part(kind, tags)
    for p in parts:
        base = len(out.v)
        out.v.extend(p.v)
        out.f.extend([[i + base for i in f] for f in p.f])
        out.fm.extend(p.fm)
        out.fuv.extend(p.fuv)
        out.fcol.extend(p.fcol)
    return out


# --------------------------------------------------------------------------- primitives

def prism(part, poly, z0, z1, mat_side=S, mat_top=None, mat_bot=None, side_uv=None, top_uv=None, col=None,
          band=None, bot=True):
    """Vertical prism over a 2D polygon (any orientation). side_uv: None (frame) | 'band'."""
    mat_top = mat_side if mat_top is None else mat_top
    mat_bot = mat_side if mat_bot is None else mat_bot
    part.begin()
    k = len(poly)
    a = [v3(p[0], p[1], z0) for p in poly]
    b = [v3(p[0], p[1], z1) for p in poly]
    for i in range(k):
        j = (i + 1) % k
        q = [a[i], a[j], b[j], b[i]]
        uv = band_uv(q, band, (b[i] - a[i]) if False else (a[j] - a[i])) if band else None
        part.add(q, mat_side, uv, col)
    tq = b[:]
    part.add(tq, mat_top, band_uv(tq, band) if (band and mat_top == D) else top_uv, col)
    if bot:
        bq = a[::-1]
        part.add(bq, mat_bot, band_uv(bq, band) if (band and mat_bot == D) else None, col)
    part.end_solid()


def block(part, x0, x1, y0, y1, z0, z1, mat=S, band=None, col=None, mat_top=None):
    prism(part, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1, mat, mat_top, None, band=band, col=col)


def box_rot(part, cx, cy, z0, w, d, h, ang, mat=S, band=None, col=None):
    ca, sa = math.cos(ang), math.sin(ang)
    pts = []
    for (x, y) in ((-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2), (-w / 2, d / 2)):
        pts.append((cx + x * ca - y * sa, cy + x * sa + y * ca))
    prism(part, pts, z0, z0 + h, mat, band=band, col=col)


def beam(part, p0, p1, w, h, mat=D, band='iron', up=None, col=None):
    p0 = Vector(p0)
    p1 = Vector(p1)
    d = p1 - p0
    L = d.length
    if L < 1e-5:
        return
    d = d / L
    upv = Vector(up) if up is not None else Vector((0, 0, 1))
    if abs(d.dot(upv)) > 0.98:
        upv = Vector((1, 0, 0)) if abs(d.x) < 0.9 else Vector((0, 1, 0))
    s = d.cross(upv).normalized()
    u = s.cross(d).normalized()
    prof = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
    part.begin()
    A = [p0 + s * x + u * y for x, y in prof]
    B = [p1 + s * x + u * y for x, y in prof]
    for i in range(4):
        j = (i + 1) % 4
        q = [A[i], A[j], B[j], B[i]]
        uv = band_uv(q, band, d) if mat == D else None
        part.add(q, mat, uv, col)
    capA = A[::-1]
    part.add(capA, mat, band_uv(capA, band, d) if mat == D else None, col)
    part.add(B, mat, band_uv(B, band, d) if mat == D else None, col)
    part.end_solid()


def cylinder(part, c, r0, r1, h, sides, mat=S, band=None, caps=True, col=None, top_mat=None, ang0=0.0, uv_tile=None):
    """Vertical (tapered) cylinder; base centre c. Brick/stone UVs wrap continuously (cylindrical)."""
    cx, cy, cz = c
    part.begin()
    ring0, ring1 = [], []
    for i in range(sides):
        a = ang0 + 2 * math.pi * i / sides
        ring0.append(v3(cx + r0 * math.cos(a), cy + r0 * math.sin(a), cz))
        ring1.append(v3(cx + r1 * math.cos(a), cy + r1 * math.sin(a), cz + h))
    tile = (uv_tile or CTX['tile'].get(mat) or 2.0)
    rm = (r0 + r1) * 0.5
    for i in range(sides):
        j = (i + 1) % sides
        q = [ring0[i], ring0[j], ring1[j], ring1[i]]
        if mat == D:
            v0, v1, metres = TX.band_rect(band or 'iron')
            ua = 2 * math.pi * rm * i / sides / metres
            ub = 2 * math.pi * rm * (i + 1) / sides / metres
            uv = [(ua, v0), (ub, v0), (ub, v1), (ua, v1)]
        else:
            ua = 2 * math.pi * rm * i / sides / tile
            ub = 2 * math.pi * rm * (i + 1) / sides / tile
            uv = [(ua, cz / tile), (ub, cz / tile), (ub, (cz + h) / tile), (ua, (cz + h) / tile)]
        part.add(q, mat, uv, col)
    if caps:
        tm = mat if top_mat is None else top_mat
        top = ring1[:]
        part.add(top, tm, band_uv(top, band or 'iron') if tm == D else None, col)
        bot = ring0[::-1]
        part.add(bot, mat, band_uv(bot, band or 'iron') if mat == D else None, col)
    part.end_solid()


def lathe(part, c, prof, sides, mat=S, band=None, top_mat=None, top_uv=None, col=None, top_col=None, bottom=True):
    """Closed solid of revolution about a vertical axis through c. prof: [(radius, z)] from bottom to top."""
    cx, cy, cz = c
    part.begin()
    rings = []
    for (r, z) in prof:
        rings.append([v3(cx + r * math.cos(2 * math.pi * i / sides), cy + r * math.sin(2 * math.pi * i / sides), cz + z) for i in range(sides)])
    tile = CTX['tile'].get(mat) or 2.0
    z0, z1 = prof[0][1], prof[-1][1]
    for k in range(len(prof) - 1):
        rm = (prof[k][0] + prof[k + 1][0]) * 0.5
        for i in range(sides):
            j = (i + 1) % sides
            q = [rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]]
            if mat == D:
                v0, v1, metres = TX.band_rect(band or 'iron')
                fa = (prof[k][1] - z0) / max(z1 - z0, 1e-6)
                fb = (prof[k + 1][1] - z0) / max(z1 - z0, 1e-6)
                ua = 2 * math.pi * rm * i / sides / metres
                ub = 2 * math.pi * rm * (i + 1) / sides / metres
                uv = [(ua, v0 + fa * (v1 - v0)), (ub, v0 + fa * (v1 - v0)), (ub, v0 + fb * (v1 - v0)), (ua, v0 + fb * (v1 - v0))]
            else:
                ua = 2 * math.pi * rm * i / sides / tile
                ub = 2 * math.pi * rm * (i + 1) / sides / tile
                za, zb = (cz + prof[k][1]) / tile, (cz + prof[k + 1][1]) / tile
                uv = [(ua, za), (ub, za), (ub, zb), (ua, zb)]
            part.add(q, mat, uv, col)
    tm = mat if top_mat is None else top_mat
    top = rings[-1][:]
    if prof[-1][0] > 1e-4:
        part.add(top, tm, top_uv(top) if top_uv else (band_uv(top, band or 'iron') if tm == D else None), top_col if top_col else col)
    if bottom and prof[0][0] > 1e-4:
        bot = rings[0][::-1]
        part.add(bot, mat, band_uv(bot, band or 'iron') if mat == D else None, col)
    part.end_solid()


def extrude_profile(part, prof, to3d, s0, s1, edge_mats, cap_mat, edge_uv=None, cap_uv=None, col=None):
    """Closed prism of a 2D profile (a, z) swept along s from s0 to s1. to3d(a, z, s) -> Vector.
    edge_mats[i] / edge_uv[i] for profile edge i -> i+1 (edge_uv entries: None (frame) or ('band', name))."""
    part.begin()
    k = len(prof)
    A = [to3d(a, z, s0) for a, z in prof]
    B = [to3d(a, z, s1) for a, z in prof]
    axis = (B[0] - A[0]).normalized()
    for i in range(k):
        j = (i + 1) % k
        q = [A[i], A[j], B[j], B[i]]
        m = edge_mats[i] if isinstance(edge_mats, (list, tuple)) else edge_mats
        e = edge_uv[i] if edge_uv else None
        uv = None
        if e is not None and e[0] == 'band':
            uv = band_uv(q, e[1], axis)
        elif m == D:
            uv = band_uv(q, 'stone', axis)
        part.add(q, m, uv, col)
    for cap in (A[::-1], B):
        uv = None
        if cap_uv is not None:
            uv = cap_uv(cap)
        elif cap_mat == D:
            uv = band_uv(cap, 'stone')
        part.add(cap, cap_mat, uv, col)
    part.end_solid()


def mound(part, cx, cy, rx, ry, h, z0, seed, rings=4, sides=10, mat=D, tile='rubble', rough=0.25, col=None, planar=None):
    """Closed noisy mound (rubble heap, spoil heap). planar: metres per tile for tiling-material UVs."""
    rng = random.Random(seed)
    part.begin()
    rows = []
    for ri in range(rings + 1):
        rho = 1.0 - ri / rings
        row = []
        for j in range(sides):
            a = 2 * math.pi * (j + (0.5 if ri % 2 else 0.0)) / sides
            jit = 1.0 + (rng.random() - 0.5) * rough * (0.6 if ri == 0 else 1.0)
            x = cx + rx * rho * math.cos(a) * jit
            y = cy + ry * rho * math.sin(a) * jit
            z = z0 + h * (1 - rho ** 2) ** 0.75 * (1.0 + (rng.random() - 0.5) * rough) if ri > 0 else z0
            row.append(v3(x, y, z))
        rows.append(row)
    top = v3(cx + (rng.random() - 0.5) * rx * 0.2, cy + (rng.random() - 0.5) * ry * 0.2, z0 + h)

    def uvf(q):
        if planar:
            return [(p[0] / planar, p[1] / planar) for p in q]
        return planar_tile_uv(q, tile, cx - rx * 1.2, cy - ry * 1.2, cx + rx * 1.2, cy + ry * 1.2)
    for ri in range(rings):
        for j in range(sides):
            k = (j + 1) % sides
            q = [rows[ri][j], rows[ri][k], rows[ri + 1][k], rows[ri + 1][j]]
            part.add(q, mat, uvf(q), col)
    for j in range(sides):
        k = (j + 1) % sides
        q = [rows[rings][j], rows[rings][k], top]
        part.add(q, mat, uvf(q), col)
    bot = [v3(p.x, p.y, z0 - 0.3) for p in rows[0]][::-1]
    # skirt down to below ground
    for j in range(sides):
        k = (j + 1) % sides
        q = [v3(rows[0][j].x, rows[0][j].y, z0 - 0.3), v3(rows[0][k].x, rows[0][k].y, z0 - 0.3), rows[0][k], rows[0][j]]
        part.add(q, mat, uvf(q), col)
    part.add(bot, mat, uvf(bot), col)
    part.end_solid()


# --------------------------------------------------------------------------- facade body with real openings

SIDES = ('S', 'E', 'N', 'W')


def side_frame(side, x0, x1, y0, y1):
    if side == 'S':
        return v3(x0, y0, 0), Vector((1, 0, 0)), Vector((0, -1, 0)), x1 - x0
    if side == 'E':
        return v3(x1, y0, 0), Vector((0, 1, 0)), Vector((1, 0, 0)), y1 - y0
    if side == 'N':
        return v3(x1, y1, 0), Vector((-1, 0, 0)), Vector((0, 1, 0)), x1 - x0
    return v3(x0, y1, 0), Vector((0, -1, 0)), Vector((-1, 0, 0)), y1 - y0


class Opening:
    __slots__ = ('u0', 'u1', 'z0', 'z1', 'tile', 'depth', 'rnd', 'accent', 'sill', 'lintel', 'arch')

    def __init__(self, u0, u1, z0, z1, tile='sash6', depth=0.16, rnd=0.0, accent=0.0, sill=True, lintel='stone'):
        self.u0, self.u1, self.z0, self.z1 = u0, u1, z0, z1
        self.tile, self.depth, self.rnd, self.accent = tile, depth, rnd, accent
        self.sill, self.lintel = sill, lintel


def body(x0, x1, y0, y1, z0, z1, openings, lod, mat=S, tags=('body',), dressing=True, top=True):
    """Closed rectangular wall mass with openings. openings: {side: [Opening]}.
    Returns (wall part, list of dressing parts (sills/lintels), list of decal parts (lod2 windows))."""
    part = Part('solid', tags)
    dress = []
    zl = {round(z0, 4), round(z1, 4)}
    gsides = SIDES if lod == 0 else (CTX.get('lod1_grid', 'SNEW') if lod == 1 else '')

    def gridded(side):
        return side in gsides
    if lod <= 1:
        for side in SIDES:
            if not gridded(side):
                continue
            for o in openings.get(side, []):
                zl.add(round(o.z0, 4))
                zl.add(round(o.z1, 4))
    zl = sorted(zl)
    top_loop = []
    bot_loop = []
    for side in SIDES:
        org, dirv, nrm, L = side_frame(side, x0, x1, y0, y1)
        ops = openings.get(side, []) if gridded(side) else []
        ul = {0.0, round(L, 4)}
        for o in ops:
            ul.add(round(o.u0, 4))
            ul.add(round(o.u1, 4))
        ul = sorted(ul)

        def P(u, z, dep=0.0):
            return org + dirv * u + Vector((0, 0, z)) - nrm * dep

        def owner(uc, zc):
            for o in ops:
                if o.u0 < uc < o.u1 and o.z0 < zc < o.z1:
                    return o
            return None
        cells = {}
        for i in range(len(ul) - 1):
            for j in range(len(zl) - 1):
                cells[(i, j)] = owner((ul[i] + ul[i + 1]) * 0.5, (zl[j] + zl[j + 1]) * 0.5)
        for (i, j), o in cells.items():
            ua, ub, za, zb = ul[i], ul[i + 1], zl[j], zl[j + 1]
            if o is None:
                part.add([P(ua, za), P(ub, za), P(ub, zb), P(ua, zb)], mat)
                continue
            dep = o.depth if lod == 0 else 0.0

            def ouv(qs, o=o, side=side):
                return tile_uv(qs, o.tile, P(o.u0, o.z0, dep), dirv, (0, 0, 1), o.u1 - o.u0, o.z1 - o.z0)
            col = (o.rnd, 0.0, o.accent, 1.0)
            q = [P(ua, za, dep), P(ub, za, dep), P(ub, zb, dep), P(ua, zb, dep)]
            part.add(q, D, ouv(q), col)
            if dep > 0:
                # reveals toward every neighbouring wall cell
                nb = [((i, j - 1), (ua, za), (ub, za)), ((i + 1, j), (ub, za), (ub, zb)),
                      ((i, j + 1), (ub, zb), (ua, zb)), ((i - 1, j), (ua, zb), (ua, za))]
                for key, pa, pb in nb:
                    if cells.get(key, None) is o:
                        continue
                    # in the wall cell this edge runs pb -> pa (reverse of our CCW order)
                    q = [P(pa[0], pa[1]), P(pb[0], pb[1]), P(pb[0], pb[1], dep), P(pa[0], pa[1], dep)]
                    part.add(q[::-1][::-1], mat)
        # outer loops (for the caps)
        for i in range(len(ul) - 1):
            top_loop.append(P(ul[i], z1))
            bot_loop.append(P(ul[i], z0))
        # dressing: sills and lintels
        if lod == 0 and dressing:
            for o in openings.get(side, []):
                w = o.u1 - o.u0
                if o.sill and o.z0 > z0 + 0.2:
                    dp = Part('detail', ('sill',), 'stone')
                    a = P(o.u0 - 0.06, o.z0 - 0.09)
                    _side_box(dp, org, dirv, nrm, o.u0 - 0.07, o.u1 + 0.07, o.z0 - 0.10, o.z0, -0.02, 0.09, 'stone')
                    dress.append(dp)
                if o.lintel:
                    dp = Part('detail', ('lintel',), 'stone')
                    _side_box(dp, org, dirv, nrm, o.u0 - 0.12, o.u1 + 0.12, o.z1, o.z1 + 0.22, -0.02, 0.025, o.lintel)
                    dress.append(dp)
    # caps: fan from the centroid (no degenerate triangles from collinear points)
    if top:
        c = sum(top_loop, Vector()) / len(top_loop)
        for k in range(len(top_loop)):
            part.add([c, top_loop[k], top_loop[(k + 1) % len(top_loop)]], mat)
    cb = sum(bot_loop, Vector()) / len(bot_loop)
    for k in range(len(bot_loop)):
        part.add([cb, bot_loop[(k + 1) % len(bot_loop)], bot_loop[k]], mat)
    # orientation sanity (faces were built outward; flip all if not)
    part._mark = 0
    part.end_solid()
    decals = []
    if lod in (1, 2):
        dp = Part('detail', ('window',), 'iron')
        for side in SIDES:
            if gridded(side):
                continue
            org, dirv, nrm, L = side_frame(side, x0, x1, y0, y1)
            for o in openings.get(side, []):
                q = [org + dirv * o.u0 + Vector((0, 0, o.z0)) + nrm * 0.03, org + dirv * o.u1 + Vector((0, 0, o.z0)) + nrm * 0.03,
                     org + dirv * o.u1 + Vector((0, 0, o.z1)) + nrm * 0.03, org + dirv * o.u0 + Vector((0, 0, o.z1)) + nrm * 0.03]
                uv = tile_uv(q, o.tile, q[0], dirv, (0, 0, 1), o.u1 - o.u0, o.z1 - o.z0)
                dp.add(q, D, uv, (o.rnd, 0.0, o.accent, 1.0))
        if dp.f:
            decals.append(dp)
    return part, dress, decals


def _side_box(part, org, dirv, nrm, u0, u1, za, zb, d0, d1, band):
    """Small box on a facade: u range, z range, depth range measured outward from the wall plane."""
    def P(u, z, d):
        return org + dirv * u + Vector((0, 0, z)) + nrm * d
    part.begin()
    c = [P(u0, za, d0), P(u1, za, d0), P(u1, za, d1), P(u0, za, d1), P(u0, zb, d0), P(u1, zb, d0), P(u1, zb, d1), P(u0, zb, d1)]
    faces = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
    for f in faces:
        q = [c[i] for i in f]
        part.add(q, D, band_uv(q, band, dirv))
    part.end_solid()


def band_ring(x0, x1, y0, y1, za, zb, out, band='stone', tags=('string',)):
    """A moulding / plinth / string course around a rectangle, protruding `out` metres: four strips (not a slab,
    so hollowed ruins show no floating floors)."""
    t = out + 0.06
    parts = []
    for (a, b, c, d) in ((x0 - out, x1 + out, y0 - out, y0 - out + t), (x0 - out, x1 + out, y1 + out - t, y1 + out),
                         (x0 - out, x0 - out + t, y0 - out + t, y1 + out - t), (x1 + out - t, x1 + out, y0 - out + t, y1 + out - t)):
        p = Part('detail', tags, band)
        prism(p, [(a, c), (b, c), (b, d), (a, d)], za, zb, D, band=band)
        parts.append(p)
    return parts


# --------------------------------------------------------------------------- roofs

def gable_roof(x0, x1, y0, y1, ze, pitch_deg, ox=0.05, oy=0.3, fascia=0.22, ridge_x=True, gable_mat=S,
               tags=('roof',), hip=False):
    """Closed gable (or hipped) roof mass. Ridge along X (ridge_x) or Y. Returns (part, ridge z)."""
    part = Part('solid', tags, 'lead')
    if not ridge_x:
        # build along X in a rotated frame: (x, y) -> (-y, x) maps an X-ridge onto a Y-ridge
        w, d = y1 - y0, x1 - x0
        cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
        sub, zr = gable_roof(-w / 2, w / 2, -d / 2, d / 2, ze, pitch_deg, ox, oy, fascia, True, gable_mat, tags, hip)
        return sub.transformed(lambda p: Vector((cx - p.y, cy + p.x, p.z))), zr
    D_ = (y1 - y0) + 2 * oy
    yc = (y0 + y1) * 0.5
    zr = ze + (D_ / 2) * math.tan(math.radians(pitch_deg))
    ya, yb = y0 - oy, y1 + oy
    xa, xb = x0 - ox, x1 + ox
    if not hip:
        prof = [(ya, ze - fascia), (yb, ze - fascia), (yb, ze), (yc, zr), (ya, ze)]
        mats = [S, D, R, R, D]
        uvs = [None, ('band', 'timber'), None, None, ('band', 'timber')]
        extrude_profile(part, prof, lambda a, z, s: v3(s, a, z), xa, xb, mats, gable_mat, uvs)
        return part, zr
    h = zr - ze
    run = D_ / 2
    xr0, xr1 = xa + run, xb - run
    if xr1 < xr0:
        xr0 = xr1 = (xa + xb) * 0.5
    B = [v3(xa, ya, ze - fascia), v3(xb, ya, ze - fascia), v3(xb, yb, ze - fascia), v3(xa, yb, ze - fascia)]
    E = [v3(xa, ya, ze), v3(xb, ya, ze), v3(xb, yb, ze), v3(xa, yb, ze)]
    R0, R1 = v3(xr0, yc, zr), v3(xr1, yc, zr)
    part.begin()
    part.add(B[::-1], S)
    for i in range(4):
        j = (i + 1) % 4
        q = [B[i], B[j], E[j], E[i]]
        part.add(q, D, band_uv(q, 'timber', E[j] - E[i]))
    part.add([E[0], E[1], R1, R0], R)
    part.add([E[1], E[2], R1], R)
    part.add([E[2], E[3], R0, R1], R)
    part.add([E[3], E[0], R0], R)
    part.end_solid()
    return part, zr


def lean_roof(x0, x1, y0, y1, z_low, z_high, low_side='S', ox=0.15, oy=0.25, fascia=0.18, tags=('roof',)):
    """Mono-pitch roof; the low eave on low_side ('S' or 'N' with the slope across Y, 'E'/'W' across X)."""
    part = Part('solid', tags, 'lead')
    if low_side in ('S', 'N'):
        ya, yb = y0 - oy, y1 + oy
        lo, hi = (ya, yb) if low_side == 'S' else (yb, ya)
        prof = [(lo, z_low - fascia), (hi, z_high - fascia), (hi, z_high), (lo, z_low)]
        extrude_profile(part, prof, lambda a, z, s: v3(s, a, z), x0 - ox, x1 + ox, [S, S, R, D], S,
                        [None, None, None, ('band', 'timber')])
    else:
        xa, xb = x0 - oy, x1 + oy
        lo, hi = (xa, xb) if low_side == 'W' else (xb, xa)
        prof = [(lo, z_low - fascia), (hi, z_high - fascia), (hi, z_high), (lo, z_low)]
        extrude_profile(part, prof, lambda a, z, s: v3(a, s, z), y0 - ox, y1 + ox, [S, S, R, D], S,
                        [None, None, None, ('band', 'timber')])
    return part


def chimney_stack(x, y, z0, z1, w=0.55, d=1.3, pots=4, lod=0, along_x=False, tags=('chimney',)):
    """Party-wall chimney stack: brick shaft, stone cap, clay pots. Returns parts and pot-top positions."""
    parts = []
    p = Part('solid', tags + ('stackbody',))
    if along_x:
        w, d = d, w
    block(p, x - w / 2, x + w / 2, y - d / 2, y + d / 2, z0, z1)
    parts.append(p)
    tops = []
    if lod <= 1:
        cap = Part('detail', ('chimneycap',), 'stone')
        block(cap, x - w / 2 - 0.06, x + w / 2 + 0.06, y - d / 2 - 0.06, y + d / 2 + 0.06, z1, z1 + 0.14, D, band='stone')
        parts.append(cap)
        np_ = pots if lod == 0 else max(1, pots // 2)
        pp = Part('detail', ('pots',), 'terracotta')
        for k in range(np_):
            t = (k + 0.5) / np_
            px = x + ((t - 0.5) * (w - 0.2) if along_x else 0.0)
            py = y + (0.0 if along_x else (t - 0.5) * (d - 0.2))
            hgt = 0.55 + 0.15 * ((k * 7) % 3) / 2
            cylinder(pp, (px, py, z1 + 0.14), 0.13, 0.11, hgt, 6 if lod == 0 else 4, D, band='terracotta')
            tops.append(v3(px, py, z1 + 0.14 + hgt))
        parts.append(pp)
    else:
        tops.append(v3(x, y, z1))
    return parts, tops


# --------------------------------------------------------------------------- damage states

class Asm:
    """A building at one LOD: parts + metadata for states."""

    def __init__(self):
        self.parts = []
        self.sockets = []       # (name, Vector, kind)
        self.info = {}

    def add(self, p):
        if isinstance(p, (list, tuple)):
            for q in p:
                self.add(q)
        elif p is not None and p.f:
            self.parts.append(p)

    def bbox(self, tag=None):
        bs = [p.bbox() for p in self.parts if (tag is None or tag in p.tags)]
        if not bs:
            bs = [p.bbox() for p in self.parts]
        return (min(b[0] for b in bs), max(b[1] for b in bs), min(b[2] for b in bs), max(b[3] for b in bs),
                min(b[4] for b in bs), max(b[5] for b in bs))

    def tris(self):
        return sum(p.tris() for p in self.parts)


_PLACEHOLDER = []


def _placeholder_mats():
    global _PLACEHOLDER
    alive = [m for m in _PLACEHOLDER if m.name in bpy.data.materials]
    if len(alive) == 3:
        return _PLACEHOLDER
    _PLACEHOLDER = [bpy.data.materials.new('_a4_slot%d' % i) for i in range(3)]
    return _PLACEHOLDER


def part_to_mesh(part, name, sentinel=False, mats=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata(part.v, [], part.f)
    for m in (mats or _placeholder_mats()):
        me.materials.append(m)
    uvl = me.uv_layers.new(name='UVMap')
    col = me.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
    mi = [0] * len(part.f)
    uvs = []
    cols = []
    for fi, f in enumerate(part.f):
        mi[fi] = part.fm[fi]
        for k in range(len(f)):
            uvs.extend((SENT, SENT) if sentinel else part.fuv[fi][k])
            cols.extend(part.fcol[fi])
    me.polygons.foreach_set('material_index', mi)
    uvl.data.foreach_set('uv', uvs)
    col.data.foreach_set('color', cols)
    me.update()
    return me


def mesh_to_part(me, like, cap_soot=0.55):
    out = Part(like.kind, like.tags, like.capband)
    out.v = [tuple(v.co) for v in me.vertices]
    out.vd = {}
    uvl = me.uv_layers.active
    col = me.color_attributes.get('Col')
    dom = max(set(like.fm), key=like.fm.count) if like.fm else S
    for poly in me.polygons:
        idx = list(poly.vertices)
        loops = list(poly.loop_indices)
        uv = [tuple(uvl.data[li].uv) for li in loops]
        c = tuple(col.data[loops[0]].color) if col else COL0
        m = poly.material_index
        if any(u[0] < SENT * 0.5 for u in uv):
            pts = [out.v[i] for i in idx]
            m = dom if m not in (S, R) or dom == D else m
            if dom == D:
                m = D
                v0, v1, metres = TX.band_rect(like.capband)
                vm = (v0 + v1) * 0.5
                fu = frame_uv(pts, S)
                uv = [(a[0] * 0.3, vm) for a in fu]
            else:
                m = dom
                uv = frame_uv(pts, m)
            c = (c[0], max(c[1], cap_soot), c[2], 1.0)
        out.f.append(idx)
        out.fm.append(m)
        out.fuv.append(uv)
        out.fcol.append(c)
    return out


def boolean_part(part, cutter_part, op, cap_soot=0.55):
    """Exact boolean, with a sanity check: a DIFFERENCE or INTERSECT result can never reach outside the
    input part. When the exact solver mis-classifies a part made of touching boxes (the Terrace_D parapet), it
    kept pieces of the cutter, and a brick lump stood above the roofline. Such a result is retried with the
    float solver, and the part is left uncut if that fails as well (verifier fix, 2026-11-19)."""
    res = _boolean_part(part, cutter_part, op, cap_soot, 'EXACT')
    if res.f and not _inside_bbox(res, part):
        res = _boolean_part(part, cutter_part, op, cap_soot, 'FLOAT')
        if res.f and not _inside_bbox(res, part):
            res = part
    return res


def _inside_bbox(res, part, eps=0.03):
    a, b = res.bbox(), part.bbox()
    return (a[0] >= b[0] - eps and a[1] <= b[1] + eps and a[2] >= b[2] - eps and a[3] <= b[3] + eps
            and a[4] >= b[4] - eps and a[5] <= b[5] + eps)


def _boolean_part(part, cutter_part, op, cap_soot, solver):
    me = part_to_mesh(part, '_a4_op')
    ob = bpy.data.objects.new('_a4_op', me)
    cme = part_to_mesh(cutter_part, '_a4_cut', sentinel=True)
    cob = bpy.data.objects.new('_a4_cut', cme)
    sc = bpy.context.scene.collection
    sc.objects.link(ob)
    sc.objects.link(cob)
    mod = ob.modifiers.new('b', 'BOOLEAN')
    mod.operation = op
    mod.solver = solver
    mod.object = cob
    try:
        mod.material_mode = 'INDEX'
    except Exception:
        pass
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    em = ev.to_mesh()
    res = mesh_to_part(em, part, cap_soot)
    ev.to_mesh_clear()
    bpy.data.objects.remove(ob)
    bpy.data.objects.remove(cob)
    bpy.data.meshes.remove(me)
    bpy.data.meshes.remove(cme)
    return res


def heightfield_cutter(bb, hf, cell=1.2, zbot=-5.0):
    """Closed solid: everything below hf(x, y) inside bbox (expanded)."""
    x0, x1, y0, y1 = bb[0] - 0.7, bb[1] + 0.7, bb[2] - 0.7, bb[3] + 0.7
    nx = max(2, int(math.ceil((x1 - x0) / cell)))
    ny = max(2, int(math.ceil((y1 - y0) / cell)))
    p = Part('solid')
    xs = [x0 + (x1 - x0) * i / nx for i in range(nx + 1)]
    ys = [y0 + (y1 - y0) * j / ny for j in range(ny + 1)]
    Z = [[hf(x, y) for x in xs] for y in ys]
    p.begin()
    for j in range(ny):
        for i in range(nx):
            q = [v3(xs[i], ys[j], Z[j][i]), v3(xs[i + 1], ys[j], Z[j][i + 1]), v3(xs[i + 1], ys[j + 1], Z[j + 1][i + 1]), v3(xs[i], ys[j + 1], Z[j + 1][i])]
            p.add([q[0], q[1], q[2]], S)
            p.add([q[0], q[2], q[3]], S)
    # walls
    ring = [(i, 0) for i in range(nx)] + [(nx, j) for j in range(ny)] + [(i, ny) for i in range(nx, 0, -1)] + [(0, j) for j in range(ny, 0, -1)]
    for k in range(len(ring)):
        i0, j0 = ring[k]
        i1, j1 = ring[(k + 1) % len(ring)]
        a, b = v3(xs[i0], ys[j0], Z[j0][i0]), v3(xs[i1], ys[j1], Z[j1][i1])
        p.add([v3(a.x, a.y, zbot), v3(b.x, b.y, zbot), b, a], S)
    bot = [v3(xs[i], ys[j], zbot) for (i, j) in ring][::-1]
    c = sum(bot, Vector()) / len(bot)
    for k in range(len(bot)):
        p.add([c, bot[k], bot[(k + 1) % len(bot)]], S)
    p._mark = 0
    p.end_solid()
    return p


def blob_cutter(c, r, seed, squash=1.0):
    p = Part('solid')
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=2, radius=1.0)
    rng = random.Random(seed)
    vs = []
    for v in bm.verts:
        n = v.co.normalized()
        k = 1.0 + 0.35 * (vnoise(n.x * 2.3 + 5, n.y * 2.3 + n.z * 1.7, seed) - 0.5) * 2 + (rng.random() - 0.5) * 0.15
        vs.append(Vector((c[0] + n.x * r * k, c[1] + n.y * r * k, c[2] + n.z * r * k * squash)))
    p.begin()
    for f in bm.faces:
        p.add([vs[v.index] for v in f.verts], S)
    p.end_solid()
    bm.free()
    return p


def _apply_cut(asm, hf, keep_tags=(), drop_tags=('roof',), lod=0):
    cellk = (1.0, 1.8, 3.0, 5.0)[lod]
    out = []
    for p in asm.parts:
        if p.tags & set(drop_tags) and not (p.tags & set(keep_tags)):
            continue
        bb = p.bbox()
        samples = [hf(x, y) for x in (bb[0], (bb[0] + bb[1]) / 2, bb[1]) for y in (bb[2], (bb[2] + bb[3]) / 2, bb[3])]
        if bb[5] <= min(samples) - 0.05:
            out.append(p)
            continue
        if bb[4] >= max(samples) + 0.05:
            continue
        if p.kind != 'solid':
            c = p.centroid()
            if p.tags & {'beam'} or c.z < hf(c.x, c.y):
                if bb[5] < hf(c.x, c.y) + 1.0:
                    out.append(p)
            continue
        cut = heightfield_cutter(bb, hf, cell=cellk * min(1.5, max(0.4, (bb[1] - bb[0] + bb[3] - bb[2]) / 16)))
        r = boolean_part(p, cut, 'INTERSECT', cap_soot=0.7)
        if r.f:
            out.append(r)
    asm.parts = out


def _apply_holes(asm, blobs):
    out = []
    for p in asm.parts:
        bb = p.bbox()
        hit = []
        for (c, r, seed, squash) in blobs:
            if bb[0] - r < c[0] < bb[1] + r and bb[2] - r < c[1] < bb[3] + r and bb[4] - r * squash < c[2] < bb[5] + r * squash:
                hit.append((c, r, seed, squash))
        if not hit:
            out.append(p)
            continue
        if p.kind != 'solid':
            cc = p.centroid()
            if any((cc - Vector(c)).length < r * 0.9 for (c, r, s, q) in hit):
                continue
            out.append(p)
            continue
        q = p
        for (c, r, seed, squash) in hit:
            q2 = boolean_part(q, blob_cutter(c, r, seed, squash), 'DIFFERENCE')
            if not q2.f:
                # either swallowed whole or a failed boolean: drop only if the blob really contains the part
                inside = all((Vector(v) - Vector(c)).length < r * 0.95 for v in q.v[::max(1, len(q.v) // 12)])
                if inside:
                    q = q2
                    break
                continue
            q = q2
        if q.f:
            out.append(q)
    asm.parts = out


def soot(asm, centres, radius, base=0.0):
    for p in asm.parts:
        newc = []
        for fi, f in enumerate(p.f):
            c = p.fcol[fi]
            pc = Vector(p.v[f[0]])
            s = base
            for cc in centres:
                d = (pc - Vector(cc)).length
                s = max(s, 1.0 - d / radius)
            newc.append((c[0], max(c[1], min(1.0, s)), c[2], 1.0))
        p.fcol = newc


def scaffold(asm, bb, ztop, lod, seed=1, spacing=2.6, gap=1.1):
    if lod >= 3:
        return
    x0, x1, y0, y1 = bb[0] - gap, bb[1] + gap, bb[2] - gap, bb[3] + gap
    p = Part('detail', ('scaffold',), 'timber')
    nx = max(1, int((x1 - x0) / spacing))
    ny = max(1, int((y1 - y0) / spacing))
    pts = [(x0 + (x1 - x0) * i / nx, y0) for i in range(nx)] + [(x1, y0 + (y1 - y0) * j / ny) for j in range(ny)] + \
          [(x1 - (x1 - x0) * i / nx, y1) for i in range(nx)] + [(x0, y1 - (y1 - y0) * j / ny) for j in range(ny)]
    pw = 0.12 if lod == 0 else 0.2
    step = 1 if lod == 0 else 2
    for (x, y) in pts[::step]:
        beam(p, (x, y, -0.1), (x, y, ztop + 1.0), pw, pw, D, 'timber')
    lv = 2.0
    z = lv
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    while z < ztop + 0.5:
        for k in range(4):
            a, b = corners[k], corners[(k + 1) % 4]
            beam(p, (a[0], a[1], z), (b[0], b[1], z), 0.1, 0.1, D, 'timber')
            if lod == 0:
                # plank deck just inside
                ia = (a[0] + (0.35 if a[0] < (x0 + x1) / 2 else -0.35), a[1] + (0.35 if a[1] < (y0 + y1) / 2 else -0.35))
                ib = (b[0] + (0.35 if b[0] < (x0 + x1) / 2 else -0.35), b[1] + (0.35 if b[1] < (y0 + y1) / 2 else -0.35))
                beam(p, (ia[0], ia[1], z + 0.06), (ib[0], ib[1], z + 0.06), 0.6, 0.05, D, 'timber')
        z += lv * (1 if lod == 0 else 2)
    if lod == 0:
        # a few diagonal braces on the long sides
        for (a, b) in ((corners[0], corners[1]), (corners[2], corners[3])):
            beam(p, (a[0], a[1], 0.0), ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, min(ztop, 6.0)), 0.08, 0.08, D, 'timber')
    asm.add(p)


def rubble_ring(asm, bb, seed, count, lod, size=(2.5, 5.0), zt=0.0, inside=True):
    rng = random.Random(seed)
    if lod >= 2:
        count = max(1, count // (2 if lod == 2 else 4))
    p = Part('detail', ('rubble',), 'stone')
    for k in range(count):
        if inside:
            x = rng.uniform(bb[0] + 1, bb[1] - 1)
            y = rng.uniform(bb[2] + 1, bb[3] - 1)
        else:
            side = rng.randrange(4)
            t = rng.random()
            x = bb[0] + (bb[1] - bb[0]) * t if side < 2 else (bb[0] - 1.2 if side == 2 else bb[1] + 1.2)
            y = (bb[2] - 1.2 if side == 0 else bb[3] + 1.2) if side < 2 else bb[2] + (bb[3] - bb[2]) * t
        r = rng.uniform(*size) * 0.5
        h = r * rng.uniform(0.35, 0.6)
        mound(p, x, y, r * rng.uniform(0.8, 1.2), r * rng.uniform(0.8, 1.2), h, zt, seed * 31 + k,
              rings=(3, 2, 1, 1)[lod], sides=(9, 7, 5, 4)[lod], mat=D, tile='rubble', rough=0.3)
    asm.add(p)


def fallen_beams(asm, bb, seed, count, lod, zt=0.4):
    if lod >= 2:
        return
    rng = random.Random(seed)
    p = Part('detail', ('beam',), 'timber')
    for k in range(count):
        x = rng.uniform(bb[0] + 1, bb[1] - 1)
        y = rng.uniform(bb[2] + 1, bb[3] - 1)
        a = rng.uniform(0, math.pi)
        L = rng.uniform(3, 6)
        dz = rng.uniform(0.3, 2.0)
        p0 = (x - math.cos(a) * L / 2, y - math.sin(a) * L / 2, zt)
        p1 = (x + math.cos(a) * L / 2, y + math.sin(a) * L / 2, zt + dz)
        beam(p, p0, p1, 0.22, 0.22, D, 'timber' if k % 3 else 'iron')
    for fi in range(len(p.f)):
        p.fcol[fi] = (0.0, 0.85, 0.0, 1.0)
    asm.add(p)


def ruin_hf(bb, zmax, seed, lo=0.25, hi=0.6, amp=None):
    span = max(bb[1] - bb[0], bb[3] - bb[2])
    a = (hi - lo) * zmax if amp is None else amp

    def hf(x, y):
        n = fbm2(x * 0.12 + 3.1, y * 0.12 - 7.7, seed, 3)
        n2 = vnoise(x * 0.6, y * 0.6, seed + 99)
        return zmax * lo + a * n + 0.6 * n2
    return hf


def construction_hf(zmax, seed, frac=0.55):
    def hf(x, y):
        n = fbm2(x * 0.08 + 1.3, y * 0.08 + 2.1, seed, 2)
        steps = math.floor(n * 4) * 0.75
        return zmax * frac - 1.2 + steps
    return hf


HOLLOW_SKIP = {'pilaster', 'stack', 'privy', 'bay', 'pier', 'deck', 'parapet', 'spandrel', 'bin', 'tank', 'cathead', 'platform', 'heap'}


def hollow_bodies(asm, t=0.45, lod=0):
    """Turn solid building masses into wall shells with a floor, so cuts and holes read as ruined rooms."""
    if lod >= 3:
        return
    out = []
    for p in asm.parts:
        if p.kind != 'solid' or 'body' not in p.tags or (p.tags & HOLLOW_SKIP):
            out.append(p)
            continue
        bb = p.bbox()
        if min(bb[1] - bb[0], bb[3] - bb[2]) < 2.2 or bb[5] - bb[4] < 2.5:
            out.append(p)
            continue
        inner = Part('solid')
        block(inner, bb[0] + t, bb[1] - t, bb[2] + t, bb[3] - t, max(bb[4] + 0.45, 0.15), bb[5] + 6.0)
        q = boolean_part(p, inner, 'DIFFERENCE', cap_soot=0.85)
        out.append(q if q.f else p)
    asm.parts = out


def apply_state(asm, state, seed, lod, hooks=None):
    """Generic damage states. hooks may override per asset: hooks[state](asm, lod) after the generic pass,
    or hooks['custom_' + state] to replace it."""
    hooks = hooks or {}
    if state == 'Intact':
        return asm
    if ('custom_' + state) in hooks:
        hooks['custom_' + state](asm, lod)
        return asm
    bb = asm.bbox('body')
    zmax = asm.info.get('eave', bb[5])
    rng = random.Random(seed * 7 + hash_state(state))
    if state in ('Construction', 'Ruin') or (state == 'Damaged' and lod <= 2):
        hollow_bodies(asm, lod=lod)
    if state == 'Construction':
        hf = construction_hf(zmax, seed, asm.info.get('con_frac', 0.55))
        _apply_cut(asm, hf, drop_tags=('roof', 'window', 'pots', 'chimneycap', 'ornament', 'stackonly'), lod=lod)
        scaffold(asm, bb, hf((bb[0] + bb[1]) / 2, (bb[2] + bb[3]) / 2) + 1.0, lod, seed)
        # material stacks
        p = Part('detail', ('stock',), 'timber')
        for k in range(3 if lod <= 1 else 1):
            x = bb[0] + (bb[1] - bb[0]) * (0.2 + 0.3 * k)
            block(p, x - 0.6, x + 0.6, bb[2] - 3.2, bb[2] - 2.2, 0.0, 1.0, D, band='timber')
        asm.add(p)
        remap_tiles(asm, WINDOW_TILES, 'char')
    elif state == 'Damaged' and lod >= 3:
        soot(asm, [], 1.0, 0.25)
    elif state == 'Damaged':
        blobs = asm.info.get('blobs')
        if blobs is None:
            blobs = []
            nb = asm.info.get('n_holes', 3)
            roofz = asm.info.get('ridge', zmax + 2)
            for k in range(nb):
                x = rng.uniform(bb[0] + 0.15 * (bb[1] - bb[0]), bb[1] - 0.15 * (bb[1] - bb[0]))
                y = rng.uniform(bb[2] + 0.2 * (bb[3] - bb[2]), bb[3] - 0.2 * (bb[3] - bb[2]))
                z = rng.uniform(zmax * 0.75, roofz)
                r = rng.uniform(1.6, 2.8) * asm.info.get('hole_scale', 1.0)
                blobs.append(((x, y, z), r, seed + k, 1.8))
        _apply_holes(asm, blobs)
        centres = [Vector(b[0]) for b in blobs]
        remap_tiles(asm, WINDOW_TILES, 'char',
                    lambda p, fi: min((Vector(p.v[p.f[fi][0]]) - c).length for c in centres) < 7.0 * asm.info.get('hole_scale', 1.0))
        soot(asm, [b[0] for b in blobs], 6.0 * asm.info.get('hole_scale', 1.0), 0.12)
        rubble_ring(asm, bb, seed + 5, max(2, len(blobs)), lod, (2.0, 4.0), 0.0, inside=False)
    elif state == 'Ruin':
        hf = asm.info.get('ruin_hf') or ruin_hf(bb, zmax, seed)
        _apply_cut(asm, hf, drop_tags=('roof', 'window', 'pots', 'chimneycap', 'ornament', 'stackonly'), lod=lod)
        soot(asm, [], 1.0, 0.45)
        remap_tiles(asm, WINDOW_TILES, 'char')
        rubble_ring(asm, bb, seed + 11, asm.info.get('n_rubble', 5), lod, asm.info.get('rubble_size', (3.0, 6.0)), 0.0, inside=True)
        rubble_ring(asm, bb, seed + 13, 3, lod, (2.0, 4.0), 0.0, inside=False)
        fallen_beams(asm, bb, seed + 17, asm.info.get('n_beams', 4), lod)
    if state in hooks:
        hooks[state](asm, lod)
    return asm


def hash_state(s):
    return sum(ord(c) * (i + 1) for i, c in enumerate(s))


# --------------------------------------------------------------------------- assembling Blender objects

def build_object(asm, name, mats, collection):
    big = merge_parts(asm.parts)
    me = part_to_mesh(big, name, mats=mats)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=0.001)
    bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')
    # drop zero-area / sliver triangles and duplicate triangles (same three vertices). The FBX
    # round trip merged duplicates, so the manifest counts disagreed with the export by one
    # (verifier fix, 2026-11-19).
    bad = [f for f in bm.faces if f.calc_area() < 1e-6]
    seen = set()
    for f in bm.faces:
        if f.calc_area() < 1e-6:
            continue
        k = tuple(sorted(v.index for v in f.verts))
        if k in seen:
            bad.append(f)
        else:
            seen.add(k)
    if bad:
        bmesh.ops.delete(bm, geom=bad, context='FACES')
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(35))
    me.update()
    ob = bpy.data.objects.new(name, me)
    collection.objects.link(ob)
    return ob, len(me.polygons)


def mesh_checks(ob):
    """Returns dict: tris, non-manifold edge count, uv out of range for the detail slot, min z."""
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    nm = sum(1 for e in bm.edges if not e.is_manifold)
    bm.free()
    uvl = me.uv_layers.active
    bad_uv = 0
    for poly in me.polygons:
        if poly.material_index == D:
            for li in poly.loop_indices:
                u, v = uvl.data[li].uv
                if v < -0.001 or v > 1.001:
                    bad_uv += 1
    zs = [v.co.z for v in me.vertices]
    return {'tris': len(me.polygons), 'nonmanifold_edges': nm, 'detail_uv_out': bad_uv, 'zmin': min(zs), 'zmax': max(zs)}


WINDOW_TILES = ('sash6', 'sash2', 'factorywin', 'shop', 'bigwin', 'lunette', 'door_green', 'door_red')


def remap_tiles(asm, src_tiles, dst, pred=None):
    """Move faces mapped to any of src_tiles onto tile dst (e.g. dark empty openings on ruins)."""
    rects = {t: TX.tile_rect(t) for t in src_tiles}
    d = TX.tile_rect(dst)
    for p in asm.parts:
        for fi, uv in enumerate(p.fuv):
            if p.fm[fi] != D:
                continue
            u = sum(a for a, b in uv) / len(uv)
            v = sum(b for a, b in uv) / len(uv)
            for t, r in rects.items():
                if r[0] - 1e-4 <= u <= r[2] + 1e-4 and r[1] - 1e-4 <= v <= r[3] + 1e-4:
                    if pred is not None and not pred(p, fi):
                        break
                    p.fuv[fi] = [(d[0] + (a - r[0]) / (r[2] - r[0]) * (d[2] - d[0]),
                                  d[1] + (b - r[1]) / (r[3] - r[1]) * (d[3] - d[1])) for a, b in uv]
                    c = p.fcol[fi]
                    p.fcol[fi] = (0.0, c[1], c[2], 1.0)
                    break
