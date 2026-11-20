# DEMIURGE art/age5 - geometry core for the Atomic Age kit (pure Python + bpy at the end).
#
# A mesh is collected in a MeshBuilder as faces with per-corner UVs, then turned into a Blender mesh
# (welded, triangulated). Buildings are made of voxel Grids: cells of one bay by one storey, so the same
# description yields the intact building, its construction stage, its damaged and its ruined state (cells
# removed by a State), with watertight walls in every case. Facade walls map to one storey-tall strip of
# the facade trim sheet, so windows always sit on the floors.
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import age5_textures as TX  # noqa: E402

STRUCT, ROOF, DETAIL = 0, 1, 2
TEX = 1024.0
UREP = TX.REPEAT_M

STRIP_BAY = {'apt_panel': 3.0, 'apt_brick': 3.0, 'office_curtain': 1.5, 'office_ribbon': 1.5,
             'ground_shop': 6.0, 'concrete_blank': 1.0, 'industrial': 1.0, 'civic_fin': 2.4}


def strip_v(name):
    k, _ = TX.STRIPS[name]
    return (k * 128 + 0.5) / TEX, ((k + 1) * 128 - 0.5) / TEX


def rows_v(name):
    r0, r1, span = TX.DETAIL_ROWS[name]
    return (r0 + 0.5) / TEX, (r1 - 0.5) / TEX, span


# --------------------------------------------------------------------------- hashing / noise


def hash01(*ints):
    h = 2166136261
    for i in ints:
        h = ((h ^ (int(i) & 0xffffffff)) * 16777619) & 0xffffffff
    h ^= h >> 13
    h = (h * 1274126177) & 0xffffffff
    h ^= h >> 16
    return (h & 0xffffff) / float(0x1000000)


def vnoise3(x, y, z, seed=0):
    xi, yi, zi = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - xi, y - yi, z - zi
    fx, fy, fz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)

    def h(a, b, c):
        return hash01(seed, xi + a, yi + b, zi + c)
    x00 = h(0, 0, 0) + (h(1, 0, 0) - h(0, 0, 0)) * fx
    x10 = h(0, 1, 0) + (h(1, 1, 0) - h(0, 1, 0)) * fx
    x01 = h(0, 0, 1) + (h(1, 0, 1) - h(0, 0, 1)) * fx
    x11 = h(0, 1, 1) + (h(1, 1, 1) - h(0, 1, 1)) * fx
    y0 = x00 + (x10 - x00) * fy
    y1 = x01 + (x11 - x01) * fy
    return y0 + (y1 - y0) * fz


class Rng:
    def __init__(self, seed):
        self.s = seed & 0xffffffff or 1

    def r(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xffffffff
        return ((self.s >> 8) & 0xffffff) / float(0x1000000)

    def u(self, a, b):
        return a + (b - a) * self.r()

    def i(self, a, b):
        return a + int(self.r() * (b - a + 1)) if b >= a else a


def clamp(x, a, b):
    return a if x < a else (b if x > b else x)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    l = math.sqrt(dot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-12 else (0.0, 0.0, 1.0)


# --------------------------------------------------------------------------- states


class State:
    """Which cells and parts exist, and how sooted things are, for one building state.

    kind: Intact, Construction, Damaged, Ruin. Box is the building's bounds (x0, y0, x1, y1, h)."""

    def __init__(self, kind, box, seed=1, blast=None, cut=0.55, ruin_h=(0.06, 0.28)):
        self.kind = kind
        self.box = box
        self.seed = seed
        x0, y0, x1, y1, h = box
        self.h = h
        w, d = x1 - x0, y1 - y0
        self.w, self.d = w, d
        # blast: centre near the front-right top corner (toward the usual camera), radius from size
        if blast is None:
            r = max(7.0, 0.42 * min(w, d) + 0.05 * h)
            blast = [((x1 - 0.15 * w, y0 + 0.12 * d, 0.70 * h), r)]
            if h > 30:
                blast.append(((x1 - 0.35 * w, y0 + 0.3 * d, h + 0.2 * r), r * 0.9))
        self.blasts = blast
        self.cut_z = cut * h
        self.ruin_h = ruin_h

    def ruin_height(self, x, y):
        lo, hi = self.ruin_h
        n = vnoise3(x / 9.0, y / 9.0, 0.5, self.seed + 7)
        hgt = (lo + (hi - lo) * n) * self.h
        # one tall jagged remnant at the back-left corner
        x0, y0, x1, y1, _ = self.box
        cx, cy = x0 + 0.18 * self.w, y1 - 0.2 * self.d
        dd = math.hypot(x - cx, y - cy) / max(6.0, 0.3 * min(self.w, self.d))
        if dd < 1.0:
            hgt = max(hgt, (0.45 - 0.25 * dd) * self.h)
        return max(hgt, 2.5 if self.h > 8 else 0.6 * self.h)

    def removed(self, p, jitter=0.0):
        """Is the point p (cell or part centre) gone in this state?"""
        k = self.kind
        if k == 'Intact':
            return False
        if k == 'Construction':
            return p[2] > self.cut_z
        if k == 'Damaged':
            for c, r in self.blasts:
                n = vnoise3(p[0] / 4.0, p[1] / 4.0, p[2] / 4.0, self.seed + 3) - 0.5
                dd = math.sqrt((p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + ((p[2] - c[2]) * 0.8) ** 2)
                if dd < r * (1.0 + 0.55 * n + jitter):
                    return True
            return False
        if k == 'Ruin':
            n = vnoise3(p[0] / 3.0, p[1] / 3.0, p[2] / 3.0, self.seed + 5) - 0.5
            return p[2] > self.ruin_height(p[0], p[1]) * (1.0 + 0.5 * n)
        return False

    def soot(self, p):
        k = self.kind
        if k == 'Damaged':
            s = 0.0
            for c, r in self.blasts:
                dz = p[2] - c[2]
                dz = dz * 0.45 if dz > 0 else dz * 1.4      # fire stains climb
                dd = math.sqrt((p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + dz * dz)
                s = max(s, clamp(1.25 - dd / (r * 1.5), 0.0, 1.0))
            # streaky: fire stains run up the facade in vertical tongues
            streak = vnoise3(p[0] / 2.5, p[1] / 2.5, p[2] / 14.0, self.seed + 11)
            return 0.72 * s * (0.35 + 0.65 * streak)
        if k == 'Ruin':
            return clamp(0.2 + 0.45 * vnoise3(p[0] / 6, p[1] / 6, p[2] / 6, self.seed + 9), 0, 1)
        return 0.0


INTACT = None  # filled per asset


# --------------------------------------------------------------------------- mesh builder


class MeshBuilder:
    def __init__(self, state=None):
        self.P = []       # corner positions (flat per face)
        self.F = []       # (start, count, mat, smooth, soot_bias)
        self.UV = []
        self.state = state
        self.sockets = []

    def face(self, pts, uvs, mat, smooth=False, soot=0.0):
        s = len(self.P)
        self.P.extend(pts)
        self.UV.extend(uvs)
        self.F.append((s, len(pts), mat, smooth, soot))

    def tri_count(self):
        return sum(c - 2 for _, c, _, _, _ in self.F)

    def socket(self, name, p):
        self.sockets.append((name, p))


# ---- face helpers with mapping


def wall_quad(mb, a, b, z0, z1, mapping, soot=0.0, sv=None):
    """Vertical quad from 2D a to b (outward on the right), mapped by mapping:
       ('strip', name, u0, storey_base, storey_h)  facade strip, u0 = metres offset at a
       ('rows', name, u0, t0, ext, ur)            detail rows; t = z; ext: extent fitted into rows
       ('roof',)                                    world projection"""
    pts = [(a[0], a[1], z0), (b[0], b[1], z0), (b[0], b[1], z1), (a[0], a[1], z1)]
    L = math.hypot(b[0] - a[0], b[1] - a[1])
    kind = mapping[0]
    if kind == 'strip':
        _, name, u0, zb, sh = mapping
        v0, v1 = strip_v(name)
        f0 = clamp((z0 - zb) / sh, 0, 1)
        f1 = clamp((z1 - zb) / sh, 0, 1)
        va, vb = v0 + (v1 - v0) * f0, v0 + (v1 - v0) * f1
        ua, ub = u0 / UREP, (u0 + L) / UREP
        uvs = [(ua, va), (ub, va), (ub, vb), (ua, vb)]
    elif kind == 'rows':
        _, name, u0, t0, ext, ur = mapping
        uvs = rows_uv(name, [(u0, z0 - t0), (u0 + L, z0 - t0), (u0 + L, z1 - t0), (u0, z1 - t0)], ext, ur)
    else:
        uvs = [roof_uv(p, (b[0] - a[0], b[1] - a[1])) for p in pts]
    mb.face(pts, uvs, mapping_mat(mapping), soot=soot)


def mapping_mat(mapping):
    k = mapping[0]
    if k == 'strip':
        return STRUCT
    if k == 'roof':
        return ROOF
    return DETAIL


def rows_uv(name, st, ext=None, ur=None):
    """st: list of (s, t) metres; t measured from the face's lower edge. Fits ext metres into the rows."""
    v0, v1, span = rows_v(name)
    e = max(span, ext or span) if ext is not None else span
    out = []
    for s, t in st:
        if ur is None:
            u = s / UREP
        else:
            u0, u1, ulen = ur
            u = u0 + (u1 - u0) * clamp(s / ulen, 0, 1)
        f = clamp(t / e, 0, 1)
        out.append((u, v0 + (v1 - v0) * f))
    return out


def roof_uv(p, along=None):
    return (p[0] / 16.0, p[1] / 16.0 + p[2] / 16.0)


def hquad(mb, x0, y0, x1, y1, z, mapping, up=True, soot=0.0):
    """Horizontal rectangle. mapping: ('roof',) or ('rows', name, ...) projected on x/y."""
    if up:
        pts = [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]
    else:
        pts = [(x0, y1, z), (x1, y1, z), (x1, y0, z), (x0, y0, z)]
    if mapping[0] == 'roof':
        uvs = [roof_uv(p) for p in pts]
        mat = ROOF
    elif mapping[0] == 'strip':
        _, name = mapping[:2]
        v0, v1 = strip_v(name)
        uvs = [(p[0] / UREP, v0 + (v1 - v0) * 0.5) for p in pts]
        mat = STRUCT
    else:
        name = mapping[1]
        v0, v1, span = rows_v(name)
        uvs = []
        for p in pts:
            f = (p[1] % span) / span if mapping[2:3] == ('wrapless',) else None
            uvs.append((p[0] / UREP, v0 + (v1 - v0) * clamp((p[1] - min(y0, y1)) / max(span, abs(y1 - y0)), 0, 1)))
        mat = DETAIL
    mb.face(pts, uvs, mat, soot=soot)


def quad_auto(mb, pts, center, mat, uvs, smooth=False, soot=0.0):
    """Adds a planar quad/tri, flipping it so its normal points away from center."""
    n = cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))
    fc = mul(add(add(pts[0], pts[1]), add(pts[2], pts[-1])), 0.25)
    if dot(n, sub(fc, center)) < 0:
        pts = list(reversed(pts))
        uvs = list(reversed(uvs))
    mb.face(pts, uvs, mat, smooth=smooth, soot=soot)


def box(mb, c, half, mapping=('rows', 'concrete'), yaw=0.0, skip_bottom=True, soot=0.0, ur=None):
    """Oriented box. c centre, half (hx, hy, hz). mapping ('rows', name) or ('roof',) or ('strip', name)."""
    hx, hy, hz = half
    cy_, sy_ = math.cos(yaw), math.sin(yaw)

    def P(x, y, z):
        return (c[0] + x * cy_ - y * sy_, c[1] + x * sy_ + y * cy_, c[2] + z)
    faces = [
        ((-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz), 2 * hx, 2 * hz),   # -Y
        ((hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz), 2 * hy, 2 * hz),     # +X
        ((hx, hy, -hz), (-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz), 2 * hx, 2 * hz),     # +Y
        ((-hx, hy, -hz), (-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), 2 * hy, 2 * hz),  # -X
        ((-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz), 2 * hx, 2 * hy),     # top
        ((-hx, hy, -hz), (hx, hy, -hz), (hx, -hy, -hz), (-hx, -hy, -hz), 2 * hx, 2 * hy),  # bottom
    ]
    for i, (a, b, cc, d, L, H) in enumerate(faces):
        if i == 5 and skip_bottom:
            continue
        pts = [P(*a), P(*b), P(*cc), P(*d)]
        st = [(0, 0), (L, 0), (L, H), (0, H)]
        if mapping[0] == 'rows':
            uvs = rows_uv(mapping[1], st, ext=H, ur=ur)
            mat = DETAIL
        elif mapping[0] == 'roof':
            uvs = [roof_uv(p) for p in pts]
            mat = ROOF
        else:  # facade strip, whole face = one storey
            v0, v1 = strip_v(mapping[1])
            uo = mapping[2] if len(mapping) > 2 else 0.0
            uvs = [((uo + s) / UREP, v0 + (v1 - v0) * (t / H if H > 0 else 0)) for s, t in st]
            if i >= 4:
                uvs = [(s / UREP, v0 + (v1 - v0) * 0.5) for s, t in st]
            mat = STRUCT
        mb.face(pts, uvs, mat, soot=soot)


def beam(mb, p0, p1, w, mapping=('rows', 'steel'), ur=(0.0, 0.48, 12.0), soot=0.0, h=None, caps=True):
    """Square-section beam between two points (w wide, h tall)."""
    h = w if h is None else h
    d = sub(p1, p0)
    L = math.sqrt(dot(d, d))
    if L < 1e-6:
        return
    ax = norm(d)
    up = (0, 0, 1) if abs(ax[2]) < 0.95 else (1, 0, 0)
    u = norm(cross(ax, up))
    v = norm(cross(u, ax))
    hu, hv = mul(u, w * 0.5), mul(v, h * 0.5)
    corners0 = [add(add(p0, mul(hu, sx)), mul(hv, sy)) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    corners1 = [add(c, d) for c in corners0]
    center = mul(add(p0, p1), 0.5)
    name = mapping[1]
    for i in range(4):
        j = (i + 1) % 4
        pts = [corners0[i], corners0[j], corners1[j], corners1[i]]
        uvs = rows_uv(name, [(0, 0), (0, w), (L, w), (L, 0)], ext=w, ur=(ur[0], ur[1], max(L, ur[2])) if ur else None)
        quad_auto(mb, pts, center, DETAIL, uvs, soot=soot)
    for cs in ((corners0, corners1) if caps else ()):
        uvs = rows_uv(name, [(0, 0), (w, 0), (w, h), (0, h)], ext=h, ur=(ur[0], ur[1], max(w, ur[2])) if ur else None)
        quad_auto(mb, list(cs), center, DETAIL, uvs, soot=soot)


def frustum(mb, cx, cy, z0, z1, r0, r1, K, mapping, cap_top=None, cap_bottom=False, smooth=True, a0=0.0, soot=0.0,
            seg_keep=None, circ_u=None):
    """Truncated cone side (+ optional caps). mapping like wall_quad but per segment.
       cap_top: None or mapping for the top disc. seg_keep(k) -> bool to drop segments."""
    C = 2 * math.pi * (r0 + r1) * 0.5
    U = circ_u or C
    for k in range(K):
        if seg_keep and not seg_keep(k):
            continue
        t0 = a0 + 2 * math.pi * k / K
        t1 = a0 + 2 * math.pi * (k + 1) / K
        p00 = (cx + r0 * math.cos(t0), cy + r0 * math.sin(t0), z0)
        p10 = (cx + r0 * math.cos(t1), cy + r0 * math.sin(t1), z0)
        p11 = (cx + r1 * math.cos(t1), cy + r1 * math.sin(t1), z1)
        p01 = (cx + r1 * math.cos(t0), cy + r1 * math.sin(t0), z1)
        s0, s1 = U * k / K, U * (k + 1) / K
        if mapping[0] == 'strip':
            _, name, uo, zb, sh = mapping
            v0, v1 = strip_v(name)
            f0 = clamp((z0 - zb) / sh, 0, 1)
            f1 = clamp((z1 - zb) / sh, 0, 1)
            va, vb = v0 + (v1 - v0) * f0, v0 + (v1 - v0) * f1
            uvs = [((uo + s0) / UREP, va), ((uo + s1) / UREP, va), ((uo + s1) / UREP, vb), ((uo + s0) / UREP, vb)]
            mat = STRUCT
        elif mapping[0] == 'rows':
            ext = mapping[2] if len(mapping) > 2 else None
            ur = mapping[3] if len(mapping) > 3 else None
            uvs = rows_uv(mapping[1], [(s0, 0), (s1, 0), (s1, z1 - z0), (s0, z1 - z0)], ext=ext if ext else (z1 - z0), ur=ur)
            mat = DETAIL
        else:
            uvs = [roof_uv(p) for p in (p00, p10, p11, p01)]
            mat = ROOF
        mb.face([p00, p10, p11, p01], uvs, mat, smooth=smooth, soot=soot)
    if cap_top is not None:
        disc(mb, cx, cy, z1, r1, K, cap_top, up=True, a0=a0, soot=soot)
    if cap_bottom:
        disc(mb, cx, cy, z0, r0, K, ('rows', 'concrete'), up=False, a0=a0, soot=soot)


def disc(mb, cx, cy, z, r, K, mapping, up=True, a0=0.0, soot=0.0):
    c = (cx, cy, z)
    for k in range(K):
        t0 = a0 + 2 * math.pi * k / K
        t1 = a0 + 2 * math.pi * (k + 1) / K
        p0 = (cx + r * math.cos(t0), cy + r * math.sin(t0), z)
        p1 = (cx + r * math.cos(t1), cy + r * math.sin(t1), z)
        pts = [c, p0, p1] if up else [c, p1, p0]
        if mapping[0] == 'roof':
            uvs = [roof_uv(p) for p in pts]
            mat = ROOF
        elif mapping[0] == 'strip':
            v0, v1 = strip_v(mapping[1])
            uvs = [(p[0] / UREP, (v0 + v1) * 0.5) for p in pts]
            mat = STRUCT
        else:
            v0, v1, span = rows_v(mapping[1])
            uw = 0.45 if mapping[1] == 'steel' else 1.0
            uvs = [(uw * clamp((p[0] - cx + r) / max(UREP * uw, 2 * r), 0, 1), v0 + (v1 - v0) * clamp((p[1] - cy + r) / max(span, 2 * r), 0, 1))
                   for p in pts]
            mat = DETAIL
        mb.face(pts, uvs, mat, soot=soot)


def dome(mb, cx, cy, z0, r, hratio, K, rings, mapping=('rows', 'concrete'), panel_keep=None, thick=0.0, soot=0.0,
         inner_mapping=('rows', 'concrete')):
    """Spherical cap (ellipsoid) dome. panel_keep(k, ring, p) -> bool. If thick > 0 builds an inner shell and
    rims around removed panels (a broken dome you can see into)."""
    def pt(k, j, rr):
        t = 2 * math.pi * k / K
        phi = (math.pi / 2) * j / rings
        return (cx + rr * math.cos(phi) * math.cos(t), cy + rr * math.cos(phi) * math.sin(t), z0 + rr * hratio * math.sin(phi))
    keep = {}
    for j in range(rings):
        for k in range(K):
            pc = pt(k + 0.5, j + 0.5, r)
            keep[(k, j)] = panel_keep(k, j, pc) if panel_keep else True
    name = mapping[1]
    C = 2 * math.pi * r
    for j in range(rings):
        for k in range(K):
            if not keep[(k, j)]:
                continue
            a, b, c2, d = pt(k, j, r), pt(k + 1, j, r), pt(k + 1, j + 1, r), pt(k, j + 1, r)
            s0, s1 = C * k / K, C * (k + 1) / K
            ar = (math.pi / 2) * r
            t0, t1 = ar * j / rings, ar * (j + 1) / rings
            uvs = rows_uv(name, [(s0, t0), (s1, t0), (s1, t1), (s0, t1)], ext=ar)
            fl = (lambda q: list(reversed(q))) if hratio < 0 else (lambda q: q)
            if j == rings - 1:
                mb.face(fl([a, b, c2]), fl(uvs[:3]), DETAIL, smooth=True, soot=soot)
            else:
                mb.face(fl([a, b, c2, d]), fl(uvs), DETAIL, smooth=True, soot=soot)
            if thick > 0:
                ri = r - thick
                ai, bi, ci, di = pt(k, j, ri), pt(k + 1, j, ri), pt(k + 1, j + 1, ri), pt(k, j + 1, ri)
                uvs2 = rows_uv(inner_mapping[1], [(s0, t0), (s0, t1), (s1, t1), (s1, t0)], ext=ar)
                if j == rings - 1:
                    mb.face([ai, ci, bi], [uvs2[0], uvs2[2], uvs2[3]], DETAIL, smooth=True, soot=max(soot, 0.6))
                else:
                    mb.face([ai, di, ci, bi], uvs2, DETAIL, smooth=True, soot=max(soot, 0.6))
                # rims toward removed neighbours
                for (nk, nj, e0, e1) in (((k + 1) % K, j, (k + 1, j), (k + 1, j + 1)), ((k - 1) % K, j, (k, j + 1), (k, j)),
                                         (k, j + 1, (k + 1, j + 1), (k, j + 1)), (k, j - 1, (k, j), (k + 1, j))):
                    if nj < 0:
                        continue
                    if nj >= rings or keep.get((nk, nj), True):
                        continue
                    o0, o1 = pt(e0[0], e0[1], r), pt(e1[0], e1[1], r)
                    i0, i1 = pt(e0[0], e0[1], ri), pt(e1[0], e1[1], ri)
                    pc = pt(k + 0.5, j + 0.5, r - thick * 0.5)
                    em = mul(add(o0, o1), 0.5)
                    ref = add(pc, mul(sub(pc, em), 0.5))
                    quad_auto(mb, [o0, o1, i1, i0], ref, DETAIL,
                              rows_uv('rubble', [(0, 0), (3, 0), (3, thick), (0, thick)]), soot=0.8)


def rubble(mb, c, rad, height, seed, K=9, chunks=6, lod=0, soot=0.4):
    """A lumpy rubble mound plus a few broken slabs."""
    rng = Rng(seed)
    rings = 3
    pts = {}
    for j in range(rings + 1):
        for k in range(K):
            t = 2 * math.pi * (k + rng.u(-0.2, 0.2)) / K
            f = j / rings
            rr = rad * (1 - f * 0.92) * rng.u(0.8, 1.15)
            zz = height * math.sin(f * math.pi / 2) * rng.u(0.85, 1.1) if j > 0 else -0.3
            pts[(k, j)] = (c[0] + rr * math.cos(t), c[1] + rr * math.sin(t), c[2] + zz)
    top = (c[0], c[1], c[2] + height * 1.05)
    for j in range(rings):
        for k in range(K):
            k1 = (k + 1) % K
            a, b, cc, d = pts[(k, j)], pts[(k1, j)], pts[(k1, j + 1)], pts[(k, j + 1)]
            s = 2 * math.pi * rad / K
            uvs = rows_uv('rubble', [(k * s, j), (k * s + s, j), (k * s + s, j + 1), (k * s, j + 1)], ext=3.0)
            mb.face([a, b, cc, d], uvs, DETAIL, smooth=False, soot=soot)
    for k in range(K):
        a, b = pts[(k, rings)], pts[((k + 1) % K, rings)]
        mb.face([a, b, top], rows_uv('rubble', [(0, 0), (2, 0), (1, 1.5)]), DETAIL, soot=soot)
    if lod == 0:
        for i in range(chunks):
            t = rng.u(0, 2 * math.pi)
            rr = rng.u(0.2, 0.9) * rad
            cz = c[2] + height * (1 - rr / rad) * 0.8
            s = rng.u(0.6, 1.6) * max(1.0, rad / 6)
            box(mb, (c[0] + rr * math.cos(t), c[1] + rr * math.sin(t), cz + s * 0.15), (s, s * 0.6, s * 0.18),
                ('rows', 'rubble'), yaw=rng.u(0, 3.14), soot=soot)


# --------------------------------------------------------------------------- voxel grids


SIDES = {
    # side: (dx, dy) neighbour offset
    'S': (0, -1), 'E': (1, 0), 'N': (0, 1), 'W': (-1, 0),
}


class Grid:
    """A block of cells: nx * ny bays (bx, by metres) and storeys of given heights from z0.

    occ(ix, iy, iz) -> bool: intact occupancy (setbacks, cross plans, pilotis).
    strip(side, iz, ix, iy) -> facade strip name (None = 'concrete_blank').
    roof_kind: 'roof' (Roof material) or a detail row name for the topmost faces."""

    def __init__(self, x0, y0, nx, ny, bx, by, z0, heights, occ=None, strip=None, seed=1, bottom_visible=False,
                 roof_kind='roof', name='grid'):
        self.x0, self.y0, self.nx, self.ny, self.bx, self.by, self.z0 = x0, y0, nx, ny, bx, by, z0
        self.heights = heights
        self.zs = [z0]
        for h in heights:
            self.zs.append(self.zs[-1] + h)
        self.nz = len(heights)
        self.occ = occ or (lambda ix, iy, iz: True)
        self.strip = strip or (lambda side, iz, ix, iy: 'apt_panel')
        self.seed = seed
        self.bottom_visible = bottom_visible
        self.roof_kind = roof_kind
        self.name = name
        rng = Rng(seed * 7919 + 13)
        self.uoff = {}
        for iz in range(self.nz):
            for side in 'SENW':
                st = self.strip(side, iz, 0, 0) or 'concrete_blank'
                bay = STRIP_BAY.get(st, 1.0)
                self.uoff[(side, iz)] = bay * rng.i(0, int(UREP / bay) - 1)

    @property
    def x1(self):
        return self.x0 + self.nx * self.bx

    @property
    def y1(self):
        return self.y0 + self.ny * self.by

    @property
    def top(self):
        return self.zs[-1]

    def inside(self, ix, iy, iz):
        return 0 <= ix < self.nx and 0 <= iy < self.ny and 0 <= iz < self.nz

    def centre(self, ix, iy, iz):
        return (self.x0 + (ix + 0.5) * self.bx, self.y0 + (iy + 0.5) * self.by, (self.zs[iz] + self.zs[iz + 1]) * 0.5)

    def solid_map(self, state):
        sol = {}
        for iz in range(self.nz):
            for iy in range(self.ny):
                for ix in range(self.nx):
                    if self.occ(ix, iy, iz):
                        sol[(ix, iy, iz)] = not (state and state.removed(self.centre(ix, iy, iz)))
        return sol

    def build(self, mb, state, lod, merge=True):
        sol = self.solid_map(state)
        intact = state is None or state.kind == 'Intact'
        merge = merge and (intact or state.kind == 'Construction')
        runs = {}   # facade runs keyed by (side, iz, line) -> list of (s0, s1, strip)

        def occ(ix, iy, iz):
            return self.inside(ix, iy, iz) and self.occ(ix, iy, iz)

        def soot_at(p):
            return state.soot(p) if state else 0.0
        for (ix, iy, iz), s in sol.items():
            if not s:
                continue
            x0 = self.x0 + ix * self.bx
            y0 = self.y0 + iy * self.by
            x1, y1 = x0 + self.bx, y0 + self.by
            z0, z1 = self.zs[iz], self.zs[iz + 1]
            for side, (dx, dy) in SIDES.items():
                n = (ix + dx, iy + dy, iz)
                if side == 'S':
                    a, b, s0, ln = (x0, y0), (x1, y0), x0 - self.x0, y0
                elif side == 'E':
                    a, b, s0, ln = (x1, y0), (x1, y1), y0 - self.y0, x1
                elif side == 'N':
                    a, b, s0, ln = (x1, y1), (x0, y1), self.x1 - x1, y1
                else:
                    a, b, s0, ln = (x0, y1), (x0, y0), self.y1 - y1, x0
                L = self.bx if side in 'SN' else self.by
                if not occ(*n):
                    st = self.strip(side, iz, ix, iy) or 'concrete_blank'
                    if merge:
                        runs.setdefault((side, iz, round(ln, 4), st), []).append((s0, s0 + L, a, b))
                    else:
                        wall_quad(mb, a, b, z0, z1, ('strip', st, self.uoff[(side, iz)] + s0, z0, z1 - z0))
                elif not sol.get(n, False):
                    # broken interior wall toward a removed cell
                    wall_quad(mb, a, b, z0, z1, ('strip', 'concrete_blank', s0, z0, z1 - z0), soot=0.75)
                    if lod <= 1 and iz > 0:
                        # verify pass: the floor slab carries on past the broken wall as a pale torn lip, so a
                        # blast hole reads as exposed storeys (horizontal slab lines) instead of stacked dark voxels
                        hh = hash01(self.seed, ix, iy, iz, ord(side) + 31)
                        out_ = 0.45 + 0.9 * hh
                        nx_, ny_ = SIDES[side]
                        cxm, cym = (a[0] + b[0]) * 0.5 + nx_ * out_ * 0.5, (a[1] + b[1]) * 0.5 + ny_ * out_ * 0.5
                        half = (L * 0.5 if side in 'SN' else out_ * 0.5, out_ * 0.5 if side in 'SN' else L * 0.5, 0.17)
                        box(mb, (cxm, cym, z0 + 0.02 - 0.25 * (hh - 0.5) * out_), half, ('rows', 'concrete'),
                            soot=0.05, skip_bottom=False)
            # top
            above = (ix, iy, iz + 1)
            if not occ(*above):
                if self.roof_kind == 'roof':
                    hquad(mb, x0, y0, x1, y1, z1, ('roof',))
                else:
                    hquad(mb, x0, y0, x1, y1, z1, ('rows', self.roof_kind))
            elif not sol.get(above, False):
                hquad(mb, x0, y0, x1, y1, z1, ('rows', 'concrete'), soot=0.0 if state.kind == 'Construction' else 0.55)
            # bottom
            below = (ix, iy, iz - 1)
            if iz == 0:
                if self.bottom_visible:
                    hquad(mb, x0, y0, x1, y1, z0, ('rows', 'concrete'), up=False, soot=0.2)
            elif not occ(*below):
                hquad(mb, x0, y0, x1, y1, z0, ('rows', 'concrete'), up=False, soot=0.2)
            elif not sol.get(below, False):
                hquad(mb, x0, y0, x1, y1, z0, ('rows', 'concrete'), up=False, soot=0.7)
        # merged facade runs
        for (side, iz, ln, st), segs in runs.items():
            segs.sort(key=lambda t: t[0])
            z0, z1 = self.zs[iz], self.zs[iz + 1]
            cur = None
            for s0, s1, a, b in segs:
                if cur and abs(cur[1] - s0) < 1e-4:
                    cur = (cur[0], s1, cur[2], b)
                else:
                    if cur:
                        wall_quad(mb, cur[2], cur[3], z0, z1, ('strip', st, self.uoff[(side, iz)] + cur[0], z0, z1 - z0))
                    cur = (s0, s1, a, b)
            if cur:
                wall_quad(mb, cur[2], cur[3], z0, z1, ('strip', st, self.uoff[(side, iz)] + cur[0], z0, z1 - z0))
        # slab remnants poking into removed cells (damage reads as torn floors, not cut voxels)
        if state and state.kind in ('Damaged', 'Ruin') and lod <= 1:
            for (ix, iy, iz), s in sol.items():
                if s or iz == 0:
                    continue
                for side, (dx, dy) in SIDES.items():
                    n = (ix - dx, iy - dy, iz)
                    if sol.get(n, False) and sol.get((ix - dx, iy - dy, iz - 1), False) is not None:
                        h = hash01(self.seed, ix, iy, iz, ord(side))
                        if h < 0.45:
                            continue
                        cx, cy, _ = self.centre(ix, iy, iz)
                        z = self.zs[iz]
                        ext = (0.25 + 0.5 * h) * (self.bx if side in 'EW' else self.by)
                        # slab hangs out of neighbour n into this removed cell
                        if side == 'E':
                            x0 = self.x0 + ix * self.bx
                            c = (x0 + ext * 0.5, cy, z)
                            hx, hy = ext * 0.5, self.by * 0.5 * (0.6 + 0.4 * h)
                        elif side == 'W':
                            x1 = self.x0 + (ix + 1) * self.bx
                            c = (x1 - ext * 0.5, cy, z)
                            hx, hy = ext * 0.5, self.by * 0.5 * (0.6 + 0.4 * h)
                        elif side == 'N':
                            y0 = self.y0 + iy * self.by
                            c = (cx, y0 + ext * 0.5, z)
                            hx, hy = self.bx * 0.5 * (0.6 + 0.4 * h), ext * 0.5
                        else:
                            y1 = self.y0 + (iy + 1) * self.by
                            c = (cx, y1 - ext * 0.5, z)
                            hx, hy = self.bx * 0.5 * (0.6 + 0.4 * h), ext * 0.5
                        tilt = (h - 0.7) * 0.6
                        box(mb, (c[0], c[1], c[2] - 0.15 - tilt * ext * 0.3), (hx, hy, 0.15), ('rows', 'rubble'),
                            soot=0.7, skip_bottom=False)

    def hull(self, mb, state):
        """LOD3: one box per storey tier (same solid footprint), facades fitted to one strip."""
        sol = self.solid_map(state)
        tiers = []
        for iz in range(self.nz):
            cells = [(ix, iy) for (ix, iy, z), s in sol.items() if z == iz and s]
            if not cells:
                continue
            bb = (min(c[0] for c in cells), min(c[1] for c in cells), max(c[0] for c in cells), max(c[1] for c in cells))
            if tiers and tiers[-1][0] == bb and tiers[-1][2] == iz:
                tiers[-1][2] = iz + 1
            else:
                tiers.append([bb, iz, iz + 1])
        for bb, za, zb in tiers:
            x0 = self.x0 + bb[0] * self.bx
            y0 = self.y0 + bb[1] * self.by
            x1 = self.x0 + (bb[2] + 1) * self.bx
            y1 = self.y0 + (bb[3] + 1) * self.by
            z0, z1 = self.zs[za], self.zs[zb]
            mid = (za + zb) // 2
            for side, a, b in (('S', (x0, y0), (x1, y0)), ('E', (x1, y0), (x1, y1)), ('N', (x1, y1), (x0, y1)), ('W', (x0, y1), (x0, y0))):
                st = self.strip(side, min(mid, self.nz - 1), 0, 0) or 'concrete_blank'
                wall_quad(mb, a, b, z0, z1, ('strip', st, 0.0, z0, z1 - z0))
            hquad(mb, x0, y0, x1, y1, z1, ('roof',))


class CylGrid:
    """Round block: K wedges around (cx, cy), radius r, storeys from z0. Wedge cells reach the centre."""

    def __init__(self, cx, cy, r, K, z0, heights, strip=None, seed=1, a0=0.0, circ_u=None, roof_kind='roof', taper=0.0):
        self.cx, self.cy, self.r, self.K, self.z0 = cx, cy, r, K, z0
        self.heights = heights
        self.zs = [z0]
        for h in heights:
            self.zs.append(self.zs[-1] + h)
        self.nz = len(heights)
        self.strip = strip or (lambda iz: 'office_ribbon')
        self.seed = seed
        self.a0 = a0
        C = 2 * math.pi * r
        self.circ_u = circ_u or max(UREP, round(C / UREP) * UREP)
        self.roof_kind = roof_kind
        self.taper = taper
        rng = Rng(seed * 31 + 7)
        self.uoff = [STRIP_BAY.get(self.strip(iz) or 'concrete_blank', 1.0) * rng.i(0, 7) for iz in range(self.nz)]

    @property
    def top(self):
        return self.zs[-1]

    def rad(self, z):
        return self.r * (1 - self.taper * (z - self.z0) / max(1e-6, self.zs[-1] - self.z0))

    def pt(self, k, z, rr=None):
        t = self.a0 + 2 * math.pi * k / self.K
        rr = self.rad(z) if rr is None else rr
        return (self.cx + rr * math.cos(t), self.cy + rr * math.sin(t), z)

    def centre(self, k, iz):
        t = self.a0 + 2 * math.pi * (k + 0.5) / self.K
        z = (self.zs[iz] + self.zs[iz + 1]) * 0.5
        rr = self.rad(z) * 0.7
        return (self.cx + rr * math.cos(t), self.cy + rr * math.sin(t), z)

    def build(self, mb, state, lod):
        sol = {}
        for iz in range(self.nz):
            for k in range(self.K):
                sol[(k, iz)] = not (state and state.removed(self.centre(k, iz)))
        for (k, iz), s in sol.items():
            if not s:
                continue
            z0, z1 = self.zs[iz], self.zs[iz + 1]
            a0, a1 = self.pt(k, z0), self.pt(k + 1, z0)
            b1, b0 = self.pt(k + 1, z1), self.pt(k, z1)
            st = self.strip(iz) or 'concrete_blank'
            v0, v1 = strip_v(st)
            s0 = self.uoff[iz] + self.circ_u * k / self.K
            s1 = self.uoff[iz] + self.circ_u * (k + 1) / self.K
            mb.face([a0, a1, b1, b0], [(s0 / UREP, v0), (s1 / UREP, v0), (s1 / UREP, v1), (s0 / UREP, v1)], STRUCT,
                    smooth=True, soot=0.0)
            c0, c1 = (self.cx, self.cy, z0), (self.cx, self.cy, z1)
            # radial walls toward removed neighbours
            for nk, e in (((k + 1) % self.K, k + 1), ((k - 1) % self.K, k)):
                if not sol[(nk, iz)]:
                    p0, p1 = self.pt(e, z0), self.pt(e, z1)
                    rr = self.rad(z0)
                    quad_auto(mb, [c0, p0, p1, c1], self.centre(k, iz), STRUCT,
                              [(0, strip_v('concrete_blank')[0]), (rr / UREP, strip_v('concrete_blank')[0]),
                               (rr / UREP, strip_v('concrete_blank')[1]), (0, strip_v('concrete_blank')[1])], soot=0.75)
            # top
            if iz == self.nz - 1 or not sol[(k, iz + 1)]:
                mapping = ('roof',) if (iz == self.nz - 1 and self.roof_kind == 'roof') else ('rows', 'concrete')
                soot = 0.0 if (iz == self.nz - 1 or state.kind == 'Construction') else 0.55
                pts = [c1, b0, b1]
                if mapping[0] == 'roof':
                    mb.face(pts, [roof_uv(p) for p in pts], ROOF, soot=soot)
                else:
                    v0_, v1_, span = rows_v('concrete')
                    mb.face(pts, [(p[0] / UREP, v0_ + (v1_ - v0_) * clamp((p[1] - self.cy + self.r) / (2 * self.r), 0, 1)) for p in pts], DETAIL, soot=soot)
            if iz > 0 and not sol[(k, iz - 1)]:
                pts = [c0, a1, a0]
                v0_, v1_, span = rows_v('concrete')
                mb.face(pts, [(p[0] / UREP, v0_ + (v1_ - v0_) * 0.5) for p in pts], DETAIL, soot=0.7)

    def hull(self, mb, state, K=8):
        top = self.zs[-1]
        if state and state.kind != 'Intact':
            top = max(self.zs[1], min(top, max((self.zs[iz + 1] for iz in range(self.nz) for k in range(self.K)
                                                 if not state.removed(self.centre(k, iz))), default=self.zs[1])))
        st = self.strip(self.nz // 2) or 'concrete_blank'
        frustum(mb, self.cx, self.cy, self.z0, top, self.rad(self.z0), self.rad(top), K, ('strip', st, 0.0, self.z0, top - self.z0),
                cap_top=('roof',), circ_u=UREP * 2)


# --------------------------------------------------------------------------- shells of revolution (cooling tower)


def shell(mb, cx, cy, profile, K, thick, state, outer_map='concrete_blank', inner_soot=0.35, circ_u=None, smooth=True,
          rim_soot=0.0):
    """Thin shell of revolution. profile: list of (z, r) bottom to top. Panels (k, j) are removed by state.
    Outer face: facade strip 'concrete_blank' (one strip per ring band = pour lifts). Inner face darker."""
    J = len(profile) - 1
    keep = {}
    for j in range(J):
        for k in range(K):
            z = (profile[j][0] + profile[j + 1][0]) * 0.5
            r = (profile[j][1] + profile[j + 1][1]) * 0.5
            t = 2 * math.pi * (k + 0.5) / K
            keep[(k, j)] = not (state and state.removed((cx + r * math.cos(t), cy + r * math.sin(t), z)))
    Cm = 2 * math.pi * sum(r for _, r in profile) / len(profile)
    U = circ_u or max(UREP, round(Cm / UREP) * UREP)
    v0, v1 = strip_v(outer_map)

    def P(k, j, inner=False):
        z, r = profile[j]
        r = r - thick if inner else r
        t = 2 * math.pi * k / K
        return (cx + r * math.cos(t), cy + r * math.sin(t), z)
    for j in range(J):
        for k in range(K):
            if not keep[(k, j)]:
                continue
            s0, s1 = U * k / K, U * (k + 1) / K
            uv = [(s0 / UREP, v0), (s1 / UREP, v0), (s1 / UREP, v1), (s0 / UREP, v1)]
            mb.face([P(k, j), P(k + 1, j), P(k + 1, j + 1), P(k, j + 1)], uv, STRUCT, smooth=smooth)
            # inner face (reversed)
            uvi = [(s0 / UREP, v0), (s0 / UREP, v1), (s1 / UREP, v1), (s1 / UREP, v0)]
            mb.face([P(k, j, True), P(k, j + 1, True), P(k + 1, j + 1, True), P(k + 1, j, True)], uvi, STRUCT, smooth=smooth,
                    soot=inner_soot)
            # rims: top of shell / removed neighbours
            for (nk, nj, e0, e1) in (((k + 1) % K, j, (k + 1, j), (k + 1, j + 1)), ((k - 1) % K, j, (k, j + 1), (k, j)),
                                     (k, j + 1, (k + 1, j + 1), (k, j + 1)), (k, j - 1, (k, j), (k + 1, j))):
                if nj < 0:
                    continue
                if nj < J and keep.get((nk, nj), True):
                    continue
                o0, o1 = P(*e0), P(*e1)
                i0, i1 = P(*e0, inner=True), P(*e1, inner=True)
                broken = nj < J
                mid = mul(add(P(k, j), P(k + 1, j + 1)), 0.5)
                cen = (cx, cy, mid[2])
                # orient rim outward from the panel: use panel centre pushed back across the edge
                pc = mul(add(add(P(k, j), P(k + 1, j)), add(P(k + 1, j + 1), P(k, j + 1))), 0.25)
                em = mul(add(o0, o1), 0.5)
                ref = add(pc, mul(sub(pc, em), 0.5))
                quad_auto(mb, [o0, o1, i1, i0], ref, DETAIL,
                          rows_uv('rubble' if broken else 'concrete', [(0, 0), (3, 0), (3, thick), (0, thick)]),
                          soot=0.8 if broken else rim_soot)
    return keep
