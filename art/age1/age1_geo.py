# DEMIURGE art - Age1 geometry kit: a small mesh builder with the primitives the Bronze Age kit needs
# (prisms, lofts / lathes, oriented tubes, occupancy walls with openings and broken tops, masked slabs for
# thatch and flat roofs), per-loop UVs in metres, a ground-grime colour attribute, welding, outward normals,
# LOD hulls, LOD groups and FBX export. Runs inside Blender 5.2 (bpy, bmesh, mathutils).
import math
import random
import bpy
import bmesh
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

from age1_tex import STRIPS, STRIP_TILE

S, R, D = 0, 1, 2                 # material slots: Structure, Roof, Detail
SLOT_NAMES = ('Structure', 'Roof', 'Detail')
FOOT = -0.3                       # every building reaches 0.3 m below ground

# real-world size of one UV tile for the tiling sets (metres)
TILE = {'Mudbrick': 2.0, 'Render': 3.0, 'Thatch': 2.0, 'Earth': 4.0, 'Soil': 3.0, 'Crop': 2.0}


# --------------------------------------------------------------------------- noise

def _h(ix, iy, iz, seed):
    n = (ix * 374761393 + iy * 668265263 + iz * 2147483647 + seed * 1274126177) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 32767.5 - 1.0


def vnoise(x, y, z, seed=0):
    """Smooth 3D value noise in [-1, 1]."""
    ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    fx, fy, fz = [t * t * (3 - 2 * t) for t in (fx, fy, fz)]
    def L(a, b, t):
        return a + (b - a) * t
    c = [[[_h(ix + i, iy + j, iz + k, seed) for k in (0, 1)] for j in (0, 1)] for i in (0, 1)]
    return L(L(L(c[0][0][0], c[0][0][1], fz), L(c[0][1][0], c[0][1][1], fz), fy),
             L(L(c[1][0][0], c[1][0][1], fz), L(c[1][1][0], c[1][1][1], fz), fy), fx)


def fbm3(p, scale, seed=0, octaves=3):
    s, a, t = 0.0, 1.0, 0.0
    for o in range(octaves):
        f = scale * (2 ** o)
        s += a * vnoise(p[0] * f, p[1] * f, p[2] * f, seed + o * 31)
        t += a
        a *= 0.5
    return s / t


# --------------------------------------------------------------------------- UV helpers

def newell(pts):
    n = Vector((0, 0, 0))
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        n.x += (a.y - b.y) * (a.z + b.z)
        n.y += (a.z - b.z) * (a.x + b.x)
        n.z += (a.x - b.x) * (a.y + b.y)
    return n.normalized() if n.length > 1e-12 else Vector((0, 0, 1))


def uv_world(pts, tile, off=(0.0, 0.0)):
    n = newell(pts)
    ax = max(range(3), key=lambda i: abs(n[i]))
    out = []
    for p in pts:
        if ax == 2:
            u, v = p.x, p.y * (1 if n.z > 0 else -1)
        elif ax == 0:
            u, v = (p.y if n.x > 0 else -p.y), p.z
        else:
            u, v = (-p.x if n.y > 0 else p.x), p.z
        out.append((u / tile + off[0], v / tile + off[1]))
    return out


def strip_v(strip, f):
    v0, v1 = STRIPS[strip]
    pad = (v1 - v0) * 0.03
    return v0 + pad + (v1 - v0 - 2 * pad) * min(1.0, max(0.0, f))


def uv_trim_face(pts, strip, uaxis, tile=None):
    """Trim mapping of one planar face: U along uaxis (metres / strip tile), V spans the strip across the face."""
    tile = tile or STRIP_TILE[strip]
    ua = Vector(uaxis).normalized()
    n = newell(pts)
    va = n.cross(ua)
    if va.length < 1e-6:
        va = Vector((0, 0, 1)) if abs(n.z) < 0.9 else Vector((0, 1, 0))
    va.normalize()
    ws = [p.dot(va) for p in pts]
    w0, w1 = min(ws), max(ws)
    span = max(1e-6, w1 - w0)
    return [(p.dot(ua) / tile, strip_v(strip, (w - w0) / span)) for p, w in zip(pts, ws)]


# --------------------------------------------------------------------------- builder

class MB:
    def __init__(self, seed=0):
        self.V = []
        self.F = []                      # (vertex indices, material slot, [uv per loop])
        self.rng = random.Random(seed)
        self.seed = seed

    # raw polygon with explicit UVs
    def poly(self, pts, mat, uvs):
        pts = [Vector(p) for p in pts]
        b = len(self.V)
        self.V.extend(pts)
        self.F.append((list(range(b, b + len(pts))), mat, [tuple(x) for x in uvs]))

    def polyw(self, pts, mat, tile, off=(0.0, 0.0)):
        pts = [Vector(p) for p in pts]
        self.poly(pts, mat, uv_world(pts, tile, off))

    def polyt(self, pts, strip, uaxis, tile=None):
        pts = [Vector(p) for p in pts]
        self.poly(pts, D, uv_trim_face(pts, strip, uaxis, tile))

    def tri_count(self):
        return sum(len(f[0]) - 2 for f in self.F)

    def transform(self, mat4, start=0):
        """Apply a matrix to vertices added since index `start`."""
        for i in range(start, len(self.V)):
            self.V[i] = mat4 @ self.V[i]

    def jitter(self, amp, scale=0.7, start=0, zmin=-1e9, seed=0):
        for i in range(start, len(self.V)):
            p = self.V[i]
            if p.z < zmin:
                continue
            self.V[i] = p + Vector((fbm3(p, scale, seed + 1), fbm3(p, scale, seed + 2), fbm3(p, scale, seed + 3) * 0.5)) * amp


def mat_uv(mat, pts, uv, tile_key=None):
    """uv spec -> per-loop UVs. uv = ('w', tile) | ('t', strip, uaxis) | callable(pts)->uvs"""
    if callable(uv):
        return uv(pts)
    if uv[0] == 'w':
        return uv_world(pts, uv[1])
    if uv[0] == 't':
        return uv_trim_face(pts, uv[1], uv[2])
    raise ValueError(uv)


def face(mb, pts, mat, uv):
    pts = [Vector(p) for p in pts]
    mb.poly(pts, mat, mat_uv(mat, pts, uv))


# --------------------------------------------------------------------------- primitives

def prism(mb, poly2d, z0, z1, mat, side_uv, top_uv=None, top_mat=None, top=True, bottom=True, top_poly=None):
    """Extrude a CCW 2D polygon from z0 to z1 (top_poly: optional different top outline, same vertex count).
    side_uv: ('w', tile) | ('t', strip) -> trim: U along the perimeter, V up the strip | ('tz', strip, z_span)."""
    n = len(poly2d)
    tp = top_poly or poly2d
    per = 0.0
    for i in range(n):
        a, b = Vector((*poly2d[i], 0)), Vector((*poly2d[(i + 1) % n], 0))
        A0, B0 = Vector((a.x, a.y, z0)), Vector((b.x, b.y, z0))
        A1, B1 = Vector((*tp[i], z1)), Vector((*tp[(i + 1) % n], z1))
        L = (b - a).length
        if side_uv[0] == 'w':
            mb.polyw([A0, B0, B1, A1], mat, side_uv[1], (round(per / side_uv[1], 3) * 0, 0))
        else:
            strip = side_uv[1]
            t = STRIP_TILE[strip]
            zs = side_uv[2] if len(side_uv) > 2 else (z1 - z0)
            f0 = 0.0
            f1 = min(1.0, (z1 - z0) / zs)
            mb.poly([A0, B0, B1, A1], mat, [(per / t, strip_v(strip, f0)), ((per + L) / t, strip_v(strip, f0)),
                                            ((per + L) / t, strip_v(strip, f1)), (per / t, strip_v(strip, f1))])
        per += L
    tmat = mat if top_mat is None else top_mat
    tuv = top_uv or side_uv
    if top:
        pts = [Vector((*p, z1)) for p in tp]
        face(mb, pts, tmat, tuv if tuv[0] == 'w' else ('t', tuv[1], (1, 0, 0)))
    if bottom:
        pts = [Vector((*p, z0)) for p in reversed(poly2d)]
        face(mb, pts, mat, side_uv if side_uv[0] == 'w' else ('t', side_uv[1], (1, 0, 0)))


def rect(cx, cy, w, d, rot=0.0):
    c, s = math.cos(rot), math.sin(rot)
    out = []
    for x, y in ((-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2), (-w / 2, d / 2)):
        out.append((cx + x * c - y * s, cy + x * s + y * c))
    return out


def box(mb, cx, cy, z0, w, d, h, mat, uv, rot=0.0, top_mat=None, top_uv=None, bottom=True):
    prism(mb, rect(cx, cy, w, d, rot), z0, z0 + h, mat, uv, top_uv=top_uv, top_mat=top_mat, bottom=bottom)


def ellipse(cx, cy, rx, ry, n, phase=0.0, wob=0.0, rng=None):
    out = []
    for i in range(n):
        a = phase + 2 * math.pi * i / n
        k = 1.0 + (rng.uniform(-wob, wob) if rng else 0.0)
        out.append((cx + math.cos(a) * rx * k, cy + math.sin(a) * ry * k))
    return out


def loft(mb, rings, mat, uv_mode, closed=True, cap_top=False, cap_bottom=False, tile=None, strip=None, u_rep=None):
    """Connect rings (lists of Vector with equal counts). uv_mode:
       'lathe'  tiling material: U around (u_rep repeats), V = accumulated ring distance / tile
       'tube'   trim strip: U = along the rings / strip tile, V across the strip around the ring."""
    m = len(rings[0])
    nseg = m if closed else m - 1
    # accumulated distance along the loft (per ring, measured at ring centroids)
    cents = [sum(r, Vector()) / len(r) for r in rings]
    acc = [0.0]
    for k in range(1, len(rings)):
        d = sum(((rings[k][j] - rings[k - 1][j]).length for j in range(m))) / m
        acc.append(acc[-1] + d)
    if uv_mode == 'lathe':
        circ = sum(((rings[0][(j + 1) % m] - rings[0][j]).length for j in range(nseg)))
        rep = u_rep or max(1, round(circ / tile))
    for k in range(len(rings) - 1):
        for j in range(nseg):
            j1 = (j + 1) % m
            pts = [rings[k][j], rings[k][j1], rings[k + 1][j1], rings[k + 1][j]]
            if uv_mode == 'lathe':
                u0, u1 = rep * j / nseg, rep * (j + 1) / nseg
                v0, v1 = acc[k] / tile, acc[k + 1] / tile
                uvs = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
            elif uv_mode == 'ring':          # trim strip: U around the ring (metres), V up the profile across the strip
                t = STRIP_TILE[strip]
                circ0 = sum(((rings[k][(q + 1) % m] - rings[k][q]).length for q in range(nseg))) or 1.0
                rep = max(1, round(circ0 / t))
                tot = acc[-1] or 1.0
                u0, u1 = rep * j / nseg, rep * (j + 1) / nseg
                uvs = [(u0, strip_v(strip, acc[k] / tot)), (u1, strip_v(strip, acc[k] / tot)),
                       (u1, strip_v(strip, acc[k + 1] / tot)), (u0, strip_v(strip, acc[k + 1] / tot))]
            else:
                t = STRIP_TILE[strip]
                f0, f1 = j / nseg, (j + 1) / nseg
                uvs = [(acc[k] / t, strip_v(strip, f0)), (acc[k] / t, strip_v(strip, f1)),
                       (acc[k + 1] / t, strip_v(strip, f1)), (acc[k + 1] / t, strip_v(strip, f0))]
            # drop fully degenerate quads (apex rings)
            if (pts[2] - pts[3]).length < 1e-7:
                mb.poly(pts[:3], mat, uvs[:3])
            elif (pts[0] - pts[1]).length < 1e-7:
                mb.poly([pts[0], pts[2], pts[3]], mat, [uvs[0], uvs[2], uvs[3]])
            else:
                mb.poly(pts, mat, uvs)
    capuv = ('w', tile) if uv_mode == 'lathe' else ('t', strip, (1, 0, 0))
    if cap_top and closed:
        r = rings[-1]
        if (r[0] - r[1]).length > 1e-7:
            face(mb, list(r), mat, capuv)
    if cap_bottom and closed:
        r = rings[0]
        if (r[0] - r[1]).length > 1e-7:
            face(mb, list(reversed(r)), mat, capuv)


def lathe(mb, cx, cy, profile, segs, mat, tile=None, strip=None, cap_top=False, cap_bottom=False, phase=0.0,
          noise=0.0, nscale=0.8, seed=0, top_ragged=0.0, rng=None, sx=1.0, sy=1.0, u_rep=None, ring_uv=False):
    """profile: [(r, z), ...] bottom to top (CCW faces outward). A tiling `tile` or a trim `strip`."""
    rings = []
    for k, (r, z) in enumerate(profile):
        ring = []
        for j in range(segs):
            a = phase + 2 * math.pi * j / segs
            p = Vector((cx + math.cos(a) * r * sx, cy + math.sin(a) * r * sy, z))
            if noise and r > 1e-6:
                d = fbm3(p, nscale, seed) * noise
                p = Vector((cx + math.cos(a) * (r + d) * sx, cy + math.sin(a) * (r + d) * sy, z + fbm3(p, nscale, seed + 7) * noise * 0.4))
            if top_ragged and k == len(profile) - 1:
                p.z -= abs(fbm3(p, 1.3, seed + 3)) * top_ragged * 2 + (rng.random() * top_ragged if rng else 0)
            ring.append(p)
        rings.append(ring)
    if strip:
        loft(mb, rings, mat, 'ring' if ring_uv else 'tube', strip=strip, cap_top=cap_top, cap_bottom=cap_bottom)
    else:
        loft(mb, rings, mat, 'lathe', tile=tile, cap_top=cap_top, cap_bottom=cap_bottom, u_rep=u_rep)


def tube(mb, p0, p1, r0, r1, sides, strip='logs', mat=D, cap0=True, cap1=True, twist=0.0, tile=None):
    """Oriented cylinder / cone from p0 to p1 (trim strip, or a tiling material when tile is given)."""
    p0, p1 = Vector(p0), Vector(p1)
    ax = (p1 - p0)
    L = ax.length
    if L < 1e-6:
        return
    ax.normalize()
    ref = Vector((0, 0, 1)) if abs(ax.z) < 0.95 else Vector((1, 0, 0))
    bx = ax.cross(ref).normalized()
    by = ax.cross(bx).normalized()
    def ring(c, r):
        return [c + (bx * math.cos(twist + 2 * math.pi * j / sides) + by * math.sin(twist + 2 * math.pi * j / sides)) * r
                for j in range(sides)]
    rings = [ring(p0, r0), ring(p1, r1)]
    if tile:
        loft(mb, rings, mat, 'lathe', tile=tile, cap_top=cap1 and r1 > 0, cap_bottom=cap0 and r0 > 0)
    else:
        loft(mb, rings, mat, 'tube', strip=strip, cap_top=cap1 and r1 > 0, cap_bottom=cap0 and r0 > 0)


def beam(mb, p0, p1, w, h, strip='planks', mat=D):
    """Square-section timber from p0 to p1 (w across, h vertical-ish)."""
    p0, p1 = Vector(p0), Vector(p1)
    ax = (p1 - p0).normalized()
    ref = Vector((0, 0, 1)) if abs(ax.z) < 0.95 else Vector((1, 0, 0))
    bx = ax.cross(ref).normalized()
    by = bx.cross(ax).normalized()
    c = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
    r0 = [p0 + bx * x + by * y for x, y in c]
    r1 = [p1 + bx * x + by * y for x, y in c]
    loft(mb, [r0, r1], mat, 'tube', strip=strip, cap_top=True, cap_bottom=True)


def slab(mb, P, nu, nv, th, mat, uvf, mask=None, down=None):
    """Thick surface from a grid of top points P[i][j] (i in 0..nu, j in 0..nv). Bottom = top - down (default
    vertical th). Cells (i, j) are present when mask(i, j) (default all); band faces close every boundary."""
    down = down or Vector((0, 0, th))
    on = [[(mask(i, j) if mask else True) for j in range(nv)] for i in range(nu)]
    Q = [[P[i][j] - down for j in range(nv + 1)] for i in range(nu + 1)]
    for i in range(nu):
        for j in range(nv):
            if not on[i][j]:
                continue
            t = [P[i][j], P[i + 1][j], P[i + 1][j + 1], P[i][j + 1]]
            uv = [uvf(i, j), uvf(i + 1, j), uvf(i + 1, j + 1), uvf(i, j + 1)]
            mb.poly(t, mat, uv)
            b = [Q[i][j], Q[i][j + 1], Q[i + 1][j + 1], Q[i + 1][j]]
            mb.poly(b, mat, [uvf(i, j), uvf(i, j + 1), uvf(i + 1, j + 1), uvf(i + 1, j)])
            # boundary bands
            edges = [((i, j), (i + 1, j), (i, j - 1)), ((i + 1, j), (i + 1, j + 1), (i + 1, j)),
                     ((i + 1, j + 1), (i, j + 1), (i, j + 1)), ((i, j + 1), (i, j), (i - 1, j))]
            for (a, b2, nb) in edges:
                ni, nj = nb
                if 0 <= ni < nu and 0 <= nj < nv and on[ni][nj]:
                    continue
                A, B = P[a[0]][a[1]], P[b2[0]][b2[1]]
                A2, B2 = Q[a[0]][a[1]], Q[b2[0]][b2[1]]
                ua, ub = uvf(*a), uvf(*b2)
                dv = th / 2.0
                mb.poly([A2, B2, B, A], mat, [(ua[0], ua[1] - dv), (ub[0], ub[1] - dv), ub, ua])


def wall(mb, p0, p1, thick, cols, mat=S, tile=2.0, voff=0.0):
    """Occupancy wall along p0->p1 (2D), outer face on the right of the direction.
    cols: list of (s0, s1, [(zlo0, zlo1, zhi0, zhi1), ...]) covering 0..L in order."""
    p0, p1 = Vector((p0[0], p0[1], 0)), Vector((p1[0], p1[1], 0))
    dvec = (p1 - p0)
    L = dvec.length
    dvec.normalize()
    nrm = Vector((dvec.y, -dvec.x, 0))          # right of the direction = outside
    sbase = p0.dot(dvec)
    def P(s, z, side):
        return p0 + dvec * s + nrm * (side * thick / 2) + Vector((0, 0, z))
    def U(s, z):
        return ((sbase + s) / tile, (z + voff) / tile)
    for (s0, s1, ivs) in cols:
        for (a0, a1, b0, b1) in ivs:
            # outer face (side +1): order s0 bottom, s1 bottom, s1 top, s0 top seen from outside
            mb.poly([P(s0, a0, 1), P(s1, a1, 1), P(s1, b1, 1), P(s0, b0, 1)], mat,
                    [U(s0, a0), U(s1, a1), U(s1, b1), U(s0, b0)])
            mb.poly([P(s1, a1, -1), P(s0, a0, -1), P(s0, b0, -1), P(s1, b1, -1)], mat,
                    [(-U(s1, a1)[0], U(s1, a1)[1]), (-U(s0, a0)[0], U(s0, a0)[1]), (-U(s0, b0)[0], U(s0, b0)[1]), (-U(s1, b1)[0], U(s1, b1)[1])])
            top = [P(s0, b0, 1), P(s1, b1, 1), P(s1, b1, -1), P(s0, b0, -1)]
            mb.polyw(top, mat, tile)
            bot = [P(s0, a0, -1), P(s1, a1, -1), P(s1, a1, 1), P(s0, a0, 1)]
            mb.polyw(bot, mat, tile)
    # side faces between columns and at the ends
    def ivs_at(c, right):
        return [((iv[1], iv[3]) if right else (iv[0], iv[2])) for iv in c[2]]
    def minus(A, B):
        out = []
        for (a0, a1) in A:
            segs = [(a0, a1)]
            for (b0, b1) in B:
                nxt = []
                for (c0, c1) in segs:
                    if b1 <= c0 or b0 >= c1:
                        nxt.append((c0, c1))
                    else:
                        if b0 > c0:
                            nxt.append((c0, b0))
                        if b1 < c1:
                            nxt.append((b1, c1))
                segs = nxt
            out.extend(s for s in segs if s[1] - s[0] > 1e-4)
        return out
    def side(s, z0, z1, facing):   # facing +1: normal along +dir
        a, b = P(s, z0, 1), P(s, z0, -1)
        c, d = P(s, z1, -1), P(s, z1, 1)
        pts = [a, b, c, d] if facing > 0 else [b, a, d, c]
        mb.polyw(pts, mat, tile)
    for k in range(len(cols)):
        right = ivs_at(cols[k], True)
        nxt = ivs_at(cols[k + 1], False) if k + 1 < len(cols) else []
        s = cols[k][1]
        for (z0, z1) in minus(right, nxt):
            side(s, z0, z1, +1)
        for (z0, z1) in minus(nxt, right):
            side(s, z0, z1, -1)
    first = ivs_at(cols[0], False)
    for (z0, z1) in first:
        side(cols[0][0], z0, z1, -1)


def wall_cols(L, top, openings=(), base=FOOT, step=None, breaks=()):
    """Columns for wall(): top(s) -> height at s; openings: [(s0, s1, z0, z1)] removed from the solid."""
    xs = {0.0, L}
    for (a, b, _, _) in openings:
        xs.update((max(0.0, min(L, a)), max(0.0, min(L, b))))
    xs.update(b for b in breaks if 0 < b < L)
    if step:
        n = max(1, int(round(L / step)))
        xs.update(L * k / n for k in range(1, n))
    xs = sorted(xs)
    cols = []
    for s0, s1 in zip(xs[:-1], xs[1:]):
        if s1 - s0 < 1e-4:
            continue
        sm = (s0 + s1) / 2
        t0, t1 = top(s0), top(s1)
        cuts = sorted([(z0, z1) for (a, b, z0, z1) in openings if a <= sm <= b])
        ivs = []
        lo = base
        for (z0, z1) in cuts:
            if z0 > lo + 1e-3:
                ivs.append((lo, lo, min(z0, t0), min(z0, t1)))
            lo = max(lo, z1)
        if min(t0, t1) > lo + 1e-3:
            ivs.append((lo, lo, t0, t1))
        cols.append((s0, s1, [iv for iv in ivs if iv[2] > iv[0] + 1e-3 and iv[3] > iv[1] + 1e-3]))
    return cols


def rubble(mb, cx, cy, r, h, rng, mat=S, tile=2.0, segs=9):
    prof = [(r * 1.05, FOOT), (r, 0.0), (r * 0.75, h * 0.45), (r * 0.4, h * 0.85), (0.0, h)]
    lathe(mb, cx, cy, prof, segs, mat, tile=tile, noise=r * 0.18, nscale=1.0 / max(0.4, r), seed=rng.randint(0, 999),
          sx=rng.uniform(0.8, 1.2), sy=rng.uniform(0.8, 1.2), phase=rng.random())


# --------------------------------------------------------------------------- bpy output

def to_mesh(mb, name, weld=1e-4):
    me = bpy.data.meshes.new(name)
    verts = [tuple(v) for v in mb.V]
    faces = [f[0] for f in mb.F]
    me.from_pydata(verts, [], faces)
    me.polygons.foreach_set('material_index', [f[1] for f in mb.F])
    uvl = me.uv_layers.new(name='UVMap')
    flat = []
    for f in mb.F:
        for uv in f[2]:
            flat.extend(uv)
    uvl.data.foreach_set('uv', flat)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=weld)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-6)
    big = [f for f in bm.faces if len(f.verts) > 4]
    if big:
        bmesh.ops.triangulate(bm, faces=big)
    _orient_components(bm)
    bm.to_mesh(me)
    bm.free()
    _grime(me)
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(35))
    return me


def _orient_components(bm):
    """Make every closed component's normals point outward (signed volume > 0)."""
    bm.faces.ensure_lookup_table()
    seen = set()
    for f in bm.faces:
        if f.index in seen:
            continue
        comp, stack = [], [f]
        seen.add(f.index)
        while stack:
            g = stack.pop()
            comp.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h.index not in seen:
                        seen.add(h.index)
                        stack.append(h)
        authored = [(g.normal.copy(), g.calc_area()) for g in comp]
        bmesh.ops.recalc_face_normals(bm, faces=comp)
        vol = 0.0
        for g in comp:
            vs = [v.co for v in g.verts]
            for k in range(1, len(vs) - 1):
                vol += vs[0].dot(vs[k].cross(vs[k + 1]))
        closed = all(e.is_manifold for g in comp for e in g.edges)
        if closed:
            if vol < 0:
                bmesh.ops.reverse_faces(bm, faces=comp)
        else:
            # verifier fix: on open parts (half-built drums, boat end cones, single cards) recalc_face_normals can
            # turn the whole part inside out; keep the authored (outward-built) orientation of the majority.
            agree = sum(a * (1.0 if g.normal.dot(n0) >= 0 else -1.0) for g, (n0, a) in zip(comp, authored))
            if agree < 0:
                bmesh.ops.reverse_faces(bm, faces=comp)


def _grime(me):
    """Colour attribute 'Col': ground-contact darkening and soft variation (multiply into base colour)."""
    attr = me.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    cols = []
    for poly in me.polygons:
        for li in poly.loop_indices:
            v = me.vertices[me.loops[li].vertex_index].co
            if poly.material_index == S:
                g = 0.58 + 0.42 * min(1.0, max(0.0, (v.z + 0.1) / 1.4))
            elif poly.material_index == R:
                g = 0.78 + 0.12 * vnoise(v.x * 0.07, v.y * 0.07, v.z * 0.07, 5) + 0.1 * vnoise(v.x * 0.35, v.y * 0.35, v.z * 0.35, 6)
            else:
                g = 0.75 + 0.25 * min(1.0, max(0.0, (v.z + 0.1) / 1.0))
            g *= 0.93 + 0.07 * vnoise(v.x * 0.9, v.y * 0.9, v.z * 0.9, 11)
            cols.extend((g, g, g, 1.0))
    attr.data.foreach_set('color', cols)


def tris_of(me):
    return sum(len(p.vertices) - 2 for p in me.polygons)


def hull_mesh(src_me, name, mats):
    """Convex hull of a mesh; each hull face takes the material of the nearest source face."""
    bm = bmesh.new()
    for v in src_me.vertices:
        bm.verts.new(v.co)
    res = bmesh.ops.convex_hull(bm, input=bm.verts)
    for g in res.get('geom_interior', []) + res.get('geom_unused', []):
        if isinstance(g, bmesh.types.BMVert) and g.is_valid and not g.link_faces:
            bm.verts.remove(g)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    sbm = bmesh.new()
    sbm.from_mesh(src_me)
    tree = BVHTree.FromBMesh(sbm)
    sbm.faces.ensure_lookup_table()
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    uvl = me.uv_layers.new(name='UVMap')
    for p in me.polygons:
        loc, nrm, idx, dist = tree.find_nearest(p.center)
        p.material_index = sbm.faces[idx].material_index if idx is not None else 0
        pts = [me.vertices[me.loops[li].vertex_index].co for li in p.loop_indices]
        tile = 3.0
        uvs = uv_world(pts, tile)
        for li, uv in zip(p.loop_indices, uvs):
            uvl.data[li].uv = uv
    sbm.free()
    _grime(me)
    return me


def decimate_to(obj, ratio):
    m = obj.modifiers.new('dec', 'DECIMATE')
    m.decimate_type = 'COLLAPSE'
    m.ratio = max(0.01, min(1.0, ratio))
    m.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    obj.modifiers.remove(m)
    # verifier fix: drop the zero-area triangles the collapse leaves behind (the FBX exporter dropped them, so the
    # manifest's LOD triangle counts did not match the files)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-5)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.calc_area() < 1e-9], context='FACES')
    bm.to_mesh(me)
    bm.free()
    old = obj.data
    obj.data = me
    bpy.data.meshes.remove(old)
    me.name = obj.name


def link_obj(name, me, coll, mats):
    ob = bpy.data.objects.new(name, me)
    for m in mats:
        ob.data.materials.append(m)
    coll.objects.link(ob)
    return ob
