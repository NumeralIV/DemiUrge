# DEMIURGE art - wonders-a: shared geometry, UV, LOD, export and check helpers (runs inside Blender 5.2).
#
# Everything is built into a MeshBuilder (plain python lists), then turned into a Blender mesh in one go.
# Material slots are fixed per docs/ASSETS.md: 0 Structure, 1 Roof, 2 Detail.
# Vertex colour attribute "Col" (linear): R = flicker phase 0..1 for emissive fire geometry,
# G = 1 on flame / glow geometry (lets the UE material animate only flames), B = 0, A = 1.
import math
import random

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

STRUCT, ROOF, DETAIL = 0, 1, 2
TAU = math.tau

# --------------------------------------------------------------------------- detail atlas (4 x 4 cells, v up)
# shared by the four wonders of this family: M_Wonder_DetailA / T_Wonder_DetailA_*
CELLS = {
    'fire': 0, 'doorglow': 1, 'lantern': 2, 'iron': 3,
    'bronze': 4, 'verdigris': 5, 'bone': 6, 'hide': 7,
    'timber': 8, 'stone': 9, 'terracotta': 10, 'recess': 11,
    'charcoal': 12, 'granite': 13, 'limestone': 14, 'plaster': 15,
}
CELL_MARGIN = 0.035   # fraction of a cell kept clear for mip bleeding


def cell_uv(cell, u, v):
    """Map u, v in 0..1 into atlas cell (index or name)."""
    c = CELLS[cell] if isinstance(cell, str) else cell
    cx, cy = c % 4, c // 4
    m = CELL_MARGIN
    u = min(1.0, max(0.0, u))
    v = min(1.0, max(0.0, v))
    return ((cx + m + u * (1 - 2 * m)) / 4.0, (cy + m + v * (1 - 2 * m)) / 4.0)


# --------------------------------------------------------------------------- mesh builder


class MB:
    def __init__(self):
        self.v = []      # [x, y, z]
        self.col = []    # [r, g, b, a] per vertex
        self.f = []      # vertex index tuples
        self.fm = []     # material index per face
        self.fuv = []    # list of (u, v) per loop
        self.fs = []     # smooth flag per face

    # -- basics
    def vert(self, co, col=(0.0, 0.0, 0.0, 1.0)):
        self.v.append((float(co[0]), float(co[1]), float(co[2])))
        self.col.append(col)
        return len(self.v) - 1

    def face(self, idx, mat, uvs, smooth=True):
        if len(idx) < 3:
            return
        self.f.append(tuple(idx))
        self.fm.append(mat)
        self.fuv.append([(float(a), float(b)) for a, b in uvs])
        self.fs.append(bool(smooth))

    def add(self, other):
        o = len(self.v)
        self.v.extend(other.v)
        self.col.extend(other.col)
        self.f.extend(tuple(i + o for i in f) for f in other.f)
        self.fm.extend(other.fm)
        self.fuv.extend(other.fuv)
        self.fs.extend(other.fs)
        return self

    def tris(self):
        return sum(len(f) - 2 for f in self.f)

    def transformed(self, m):
        """Return a copy transformed by a mathutils Matrix (4x4)."""
        out = MB()
        out.v = [tuple(m @ Vector(p)) for p in self.v]
        out.col = list(self.col)
        out.f = list(self.f)
        out.fm = list(self.fm)
        out.fuv = [list(x) for x in self.fuv]
        out.fs = list(self.fs)
        return out

    def map_verts(self, fn):
        self.v = [tuple(fn(Vector(p))) for p in self.v]
        return self

    def set_color(self, col):
        self.col = [col] * len(self.v)
        return self

    def bounds(self):
        a = np.array(self.v) if self.v else np.zeros((1, 3))
        return a.min(axis=0), a.max(axis=0)


def mat_trs(loc=(0, 0, 0), rot_z=0.0, scale=1.0, rot=None):
    m = Matrix.Translation(Vector(loc))
    if rot is not None:
        m = m @ rot.to_4x4()
    else:
        m = m @ Matrix.Rotation(rot_z, 4, 'Z')
    if isinstance(scale, (int, float)):
        m = m @ Matrix.Scale(scale, 4)
    else:
        m = m @ Matrix.Diagonal(Vector((scale[0], scale[1], scale[2], 1.0)))
    return m


# --------------------------------------------------------------------------- UV helpers


def uv_box(p, n, tile, off=(0.0, 0.0), aniso=(1.0, 1.0)):
    """World box projection: p point, n face normal; tile metres per UV unit."""
    ax = max(range(3), key=lambda i: abs(n[i]))
    if ax == 2:
        u, v = p[0], p[1]
    elif ax == 0:
        u, v = p[1] * (1 if n[0] > 0 else -1), p[2]
    else:
        u, v = p[0] * (-1 if n[1] > 0 else 1), p[2]
    return (u / (tile * aniso[0]) + off[0], v / (tile * aniso[1]) + off[1])


def face_normal(pts):
    n = Vector((0, 0, 0))
    k = len(pts)
    for i in range(k):
        a = Vector(pts[i])
        b = Vector(pts[(i + 1) % k])
        n.x += (a.y - b.y) * (a.z + b.z)
        n.y += (a.z - b.z) * (a.x + b.x)
        n.z += (a.x - b.x) * (a.y + b.y)
    if n.length < 1e-12:
        return Vector((0, 0, 1))
    return n.normalized()


def box_face(mb, idx, mat, tile, off=(0.0, 0.0), aniso=(1.0, 1.0), smooth=False):
    pts = [mb.v[i] for i in idx]
    n = face_normal(pts)
    mb.face(idx, mat, [uv_box(p, n, tile, off, aniso) for p in pts], smooth)


def cell_face(mb, idx, mat, cell, lo, size, smooth=True):
    """UV a face into an atlas cell by box projection normalised to an object box (lo, size)."""
    pts = [mb.v[i] for i in idx]
    n = face_normal(pts)
    ax = max(range(3), key=lambda i: abs(n[i]))
    uvs = []
    for p in pts:
        q = [(p[i] - lo[i]) / max(1e-6, size[i]) for i in range(3)]
        if ax == 2:
            u, v = q[0], q[1]
        elif ax == 0:
            u, v = q[1], q[2]
        else:
            u, v = q[0], q[2]
        uvs.append(cell_uv(cell, u, v))
    mb.face(idx, mat, uvs, smooth)


# --------------------------------------------------------------------------- primitives


def grid_surface(mb, rows, mat, uvs, closed_u=True, smooth=True, flip=False):
    """rows: list (along v) of lists (along u) of vertex indices; uvs: same shape list of (u, v).
    Faces wind so that the normal is (du x dv) (flip reverses)."""
    nr = len(rows)
    nu = len(rows[0])
    cols = nu if closed_u else nu - 1
    for j in range(nr - 1):
        for i in range(cols):
            i2 = (i + 1) % nu
            a, b, c, d = rows[j][i], rows[j][i2], rows[j + 1][i2], rows[j + 1][i]
            ua, ub, uc, ud = uvs[j][i], uvs[j][i2], uvs[j + 1][i2], uvs[j + 1][i]
            if closed_u and i2 == 0:
                # wrap: continue u past the seam
                du = uvs[j][1][0] - uvs[j][0][0]
                ub = (uvs[j][i][0] + du, ub[1])
                uc = (uvs[j + 1][i][0] + du, uc[1])
            idx = [a, b, c, d]
            uv = [ua, ub, uc, ud]
            if flip:
                idx.reverse()
                uv.reverse()
            if len(set(idx)) < 3:
                continue
            if len(set(idx)) == 3:
                # collapse duplicates (poles)
                k = [n for n, x in enumerate(idx) if idx.index(x) == n]
                idx = [idx[n] for n in k]
                uv = [uv[n] for n in k]
            mb.face(idx, mat, uv, smooth)


def lathe(mb, prof, segs, mat, uvfn=None, theta0=0.0, theta1=TAU, cap_top=False, cap_bot=False, smooth=True,
          center=(0, 0, 0), rfn=None, col=(0, 0, 0, 1)):
    """Revolve profile [(r, z), ...] (bottom -> top) around Z. Normal points outward when the profile goes up.
    uvfn(i_seg, theta, j, r, z) -> (u, v). rfn(theta, j, r, z) -> (r, z) lets callers wobble the surface."""
    closed = abs((theta1 - theta0) - TAU) < 1e-6
    nu = segs if closed else segs + 1
    rows, uvs = [], []
    cx, cy, cz = center
    for j, (r, z) in enumerate(prof):
        row, urow = [], []
        for i in range(nu):
            th = theta0 + (theta1 - theta0) * i / segs
            rr, zz = (rfn(th, j, r, z) if rfn else (r, z))
            row.append(mb.vert((cx + rr * math.cos(th), cy + rr * math.sin(th), cz + zz), col))
            urow.append(uvfn(i, th, j, rr, zz) if uvfn else (i / segs, j / max(1, len(prof) - 1)))
        rows.append(row)
        uvs.append(urow)
    grid_surface(mb, rows, mat, uvs, closed_u=closed, smooth=smooth, flip=False)
    if cap_top:
        r0 = rows[-1]
        idx = list(r0)
        pts = [mb.v[i] for i in idx]
        mb.face(idx, mat, [((p[0] - cx) * 0.1, (p[1] - cy) * 0.1) for p in pts], False)
    if cap_bot:
        idx = list(reversed(rows[0]))
        pts = [mb.v[i] for i in idx]
        mb.face(idx, mat, [((p[0] - cx) * 0.1, (p[1] - cy) * 0.1) for p in pts], False)
    return rows


def cyl_between(mb, p0, p1, r0, r1, sides, mat, cell=None, tile=None, col=(0, 0, 0, 1), caps=True, uzone=None,
                vlen=None, smooth=True):
    """A tapered cylinder from p0 to p1. UV: atlas cell (wraps once around) or a zone (u0, u1) with v tiling."""
    p0 = Vector(p0)
    p1 = Vector(p1)
    ax = p1 - p0
    L = ax.length
    if L < 1e-6:
        return
    d = ax / L
    t = Vector((1, 0, 0)) if abs(d.x) < 0.9 else Vector((0, 1, 0))
    a = d.cross(t).normalized()
    b = d.cross(a).normalized()
    rings = []
    for k, (pc, rr) in enumerate(((p0, r0), (p1, r1))):
        ring = []
        for i in range(sides):
            th = TAU * i / sides
            ring.append(mb.vert(pc + (a * math.cos(th) + b * math.sin(th)) * rr, col))
        rings.append(ring)
    for i in range(sides):
        i2 = (i + 1) % sides
        idx = [rings[0][i], rings[0][i2], rings[1][i2], rings[1][i]]
        if cell is not None:
            uv = [cell_uv(cell, i / sides, 0.0), cell_uv(cell, (i + 1) / sides, 0.0),
                  cell_uv(cell, (i + 1) / sides, 1.0), cell_uv(cell, i / sides, 1.0)]
        else:
            u0, u1 = uzone
            vl = L / (vlen or 4.0)
            uu = lambda s: u0 + (u1 - u0) * s
            uv = [(uu(i / sides), 0.0), (uu((i + 1) / sides), 0.0), (uu((i + 1) / sides), vl), (uu(i / sides), vl)]
        mb.face(idx, mat, uv, smooth)
    if caps:
        for k, ring in enumerate(rings):
            idx = list(ring) if k == 1 else list(reversed(ring))
            if cell is not None:
                uv = [cell_uv(cell, 0.5 + 0.4 * math.cos(TAU * n / sides), 0.5 + 0.4 * math.sin(TAU * n / sides)) for n in range(sides)]
                if k == 0:
                    uv = list(reversed(uv))
            else:
                u0, u1 = uzone
                uv = [(u0 + (u1 - u0) * (0.5 + 0.45 * math.cos(TAU * n / sides)), 0.5 + 0.45 * math.sin(TAU * n / sides)) for n in range(sides)]
                if k == 0:
                    uv = list(reversed(uv))
            mb.face(idx, mat, uv, False)


ICO_V = None
ICO_F = None


def _ico(level):
    t = (1 + 5 ** 0.5) / 2
    vs = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
          (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    vs = [Vector(v).normalized() for v in vs]
    fs = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
          (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
          (8, 6, 7), (9, 8, 1)]
    for _ in range(level):
        cache = {}
        nf = []

        def mid(a, b):
            k = (min(a, b), max(a, b))
            if k not in cache:
                vs.append(((vs[a] + vs[b]) * 0.5).normalized())
                cache[k] = len(vs) - 1
            return cache[k]
        for a, b, c in fs:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        fs = nf
    return vs, fs


_ICO_CACHE = {}


def ico(level):
    if level not in _ICO_CACHE:
        _ICO_CACHE[level] = _ico(level)
    return _ICO_CACHE[level]


def octa():
    vs = [Vector(v) for v in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))]
    fs = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
    return vs, fs


def rock(mb, center, size, rng, mat=DETAIL, cell='stone', level=1, flat=0.35, rough=0.22, tile=None, rotz=None,
         sink=0.0, col=(0, 0, 0, 1)):
    """A lumpy boulder. size = (sx, sy, sz) half-extents. Bottom flattened. UV into atlas cell or box tiling."""
    if level < 0:
        vs, fs = octa()
    else:
        vs, fs = ico(level)
    ph = [rng.random() * 10 for _ in range(6)]
    rz = rng.random() * TAU if rotz is None else rotz
    cr, sr = math.cos(rz), math.sin(rz)
    base = len(mb.v)
    pts = []
    for v in vs:
        k = 1.0 + rough * (math.sin(v.x * 2.3 + ph[0]) * math.sin(v.y * 2.9 + ph[1]) + 0.6 * math.sin(v.z * 3.7 + ph[2])
                           + 0.4 * math.sin((v.x + v.y) * 5.1 + ph[3]))
        x, y, z = v.x * size[0] * k, v.y * size[1] * k, v.z * size[2] * k
        if z < -size[2] * flat:
            z = -size[2] * flat + (z + size[2] * flat) * 0.25
        x, y = x * cr - y * sr, x * sr + y * cr
        pts.append((center[0] + x, center[1] + y, center[2] + z - sink))
    for p in pts:
        mb.vert(p, col)
    lo = [center[i] - size[i] * 1.3 for i in range(3)]
    sz = [size[i] * 2.6 for i in range(3)]
    for f in fs:
        idx = [base + i for i in f]
        if tile:
            box_face(mb, idx, mat, tile, smooth=True)
            mb.fs[-1] = True
        else:
            cell_face(mb, idx, mat, cell, lo, sz, smooth=True)


def block(mb, center, half, rotz, mat, tile=None, cell=None, rot=None, col=(0, 0, 0, 1), jitter=0.0, rng=None):
    """Oriented box; UVs world-box (tile) or into an atlas cell."""
    hx, hy, hz = half
    cs = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
    m = rot.to_3x3() if rot is not None else Matrix.Rotation(rotz, 3, 'Z')
    base = len(mb.v)
    for c in cs:
        j = Vector((0, 0, 0))
        if jitter and rng:
            j = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * jitter
        p = Vector(center) + m @ (Vector((c[0] * hx, c[1] * hy, c[2] * hz)) + j)
        mb.vert(p, col)
    quads = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    lo = [center[i] - max(half) * 1.8 for i in range(3)]
    sz = [max(half) * 3.6] * 3
    for q in quads:
        idx = [base + i for i in q]
        if tile:
            box_face(mb, idx, mat, tile)
        else:
            cell_face(mb, idx, mat, cell, lo, sz, smooth=False)


def flame(mb, base, height, radius, rng, sides=6, lean=(0.0, 0.0), phase=None):
    """A soft, twisted emissive flame tongue (closed teardrop) mapped onto the fire cell (v = height)."""
    ph = rng.random() if phase is None else phase
    col = (ph, 1.0, 0.0, 1.0)
    if sides >= 6:
        levels = [0.0, 0.14, 0.34, 0.58, 0.8, 1.0]
        radii = [0.8, 1.0, 0.9, 0.6, 0.28, 0.0]
    elif sides >= 4:
        levels = [0.0, 0.25, 0.6, 1.0]
        radii = [0.9, 1.0, 0.5, 0.0]
    else:
        levels = [0.0, 0.35, 1.0]
        radii = [1.0, 0.75, 0.0]
    tw = rng.random() * TAU
    jit = [1.0 + 0.22 * (rng.random() - 0.5) for _ in range(sides)]
    rows = []
    for t, rr0 in zip(levels, radii):
        r = rr0 * radius
        if r <= 0:
            rows.append([mb.vert((base[0] + lean[0] * height, base[1] + lean[1] * height, base[2] + height), col)])
            continue
        row = []
        for i in range(sides):
            th = TAU * i / sides + tw + t * 1.6
            rr = r * jit[i]
            row.append(mb.vert((base[0] + rr * math.cos(th) + lean[0] * height * t * t,
                                base[1] + rr * math.sin(th) + lean[1] * height * t * t,
                                base[2] + height * t), col))
        rows.append(row)
    for k in range(len(rows) - 1):
        a, b = rows[k], rows[k + 1]
        for i in range(sides):
            i2 = (i + 1) % sides
            uva = cell_uv('fire', i / sides, levels[k])
            uvb = cell_uv('fire', (i + 1) / sides, levels[k])
            if len(b) == 1:
                mb.face([a[i], a[i2], b[0]], DETAIL, [uva, uvb, cell_uv('fire', (i + 0.5) / sides, 1.0)])
            else:
                mb.face([a[i], a[i2], b[i2], b[i]], DETAIL,
                        [uva, uvb, cell_uv('fire', (i + 1) / sides, levels[k + 1]), cell_uv('fire', i / sides, levels[k + 1])])
    mb.face(list(reversed(rows[0])), DETAIL, [cell_uv('fire', 0.5, 0.02)] * sides, True)


def fire(mb, base, height, radius, rng, q=0, phase=None, tongues=3):
    """A small fire: one main tongue and smaller ones round it (fewer at lower LODs)."""
    ph = rng.random() if phase is None else phase
    sides = [6, 4, 3, 3][q]
    n = [tongues, 2, 1, 1][q] if tongues > 1 else 1
    flame(mb, base, height, radius, rng, sides=sides, phase=ph,
          lean=(rng.uniform(-0.08, 0.08), rng.uniform(-0.08, 0.08)))
    for k in range(1, n):
        a = rng.random() * TAU
        d = radius * 0.7
        flame(mb, (base[0] + d * math.cos(a), base[1] + d * math.sin(a), base[2]), height * rng.uniform(0.45, 0.7),
              radius * 0.6, rng, sides=min(sides, 4), phase=ph, lean=(math.cos(a) * 0.12, math.sin(a) * 0.12))


def disc(mb, center, r, sides, mat, cell, z_up=True, col=(0, 0, 0, 1)):
    base = len(mb.v)
    for i in range(sides):
        th = TAU * i / sides
        mb.vert((center[0] + r * math.cos(th), center[1] + r * math.sin(th), center[2]), col)
    idx = [base + i for i in range(sides)]
    uv = [cell_uv(cell, 0.5 + 0.45 * math.cos(TAU * i / sides), 0.5 + 0.45 * math.sin(TAU * i / sides)) for i in range(sides)]
    if not z_up:
        idx.reverse()
        uv.reverse()
    mb.face(idx, mat, uv, True)


def scale_figure(mb, pos, rotz=0.0):
    """A 1.8 m person block-out for scale checks in previews only (never exported)."""
    rng = random.Random(1)
    m = Matrix.Translation(Vector(pos)) @ Matrix.Rotation(rotz, 4, 'Z')
    tmp = MB()
    lathe(tmp, [(0.0, 0.0), (0.2, 0.02), (0.22, 0.9), (0.25, 1.35), (0.2, 1.45), (0.08, 1.52), (0.12, 1.6),
                (0.11, 1.75), (0.0, 1.8)], 8, DETAIL, lambda i, th, j, r, z: cell_uv('hide', i / 8, z / 1.8))
    mb.add(tmp.transformed(m))


# --------------------------------------------------------------------------- Blender objects


def to_object(mb, name, mats, coll, sharp_deg=50.0):
    me = bpy.data.meshes.new(name)
    me.from_pydata(mb.v, [], [list(f) for f in mb.f])
    uvl = me.uv_layers.new(name='UVMap')
    flat = [c for face in mb.fuv for uv in face for c in uv]
    uvl.data.foreach_set('uv', flat)
    me.polygons.foreach_set('material_index', mb.fm)
    me.polygons.foreach_set('use_smooth', mb.fs)
    ca = me.color_attributes.new('Col', 'FLOAT_COLOR', 'POINT')
    ca.data.foreach_set('color', [c for col in mb.col for c in col])
    for m in mats:
        me.materials.append(m)
    me.validate(clean_customdata=False)
    # sharp edges by angle (smoothing groups for the FBX)
    bm = bmesh.new()
    bm.from_mesh(me)
    thr = math.radians(sharp_deg)
    for e in bm.edges:
        if len(e.link_faces) == 2:
            try:
                if e.calc_face_angle() > thr:
                    e.smooth = False
            except ValueError:
                e.smooth = False
        else:
            e.smooth = False
    bm.to_mesh(me)
    bm.free()
    me.update()
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def triangulate(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    # verifier fix: plane cuts left a few zero-area slivers (Hive and Seven ruins); dissolve them
    if any(f.calc_area() < 1e-8 for f in bm.faces):
        bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges[:])
        bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()
    return len(ob.data.polygons)


def decimate_to(ob, target_tris):
    n = len(ob.data.polygons)
    if n <= target_tris:
        return n
    mod = ob.modifiers.new('dec', 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = max(0.01, target_tris / n)
    mod.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    old = ob.data
    ob.modifiers.remove(mod)
    ob.data = me
    bpy.data.meshes.remove(old)
    return triangulate(ob)


def mesh_checks(ob):
    """Returns dict: tris, non-manifold edge count, boundary edges, inverted closed shells, bbox."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    nm = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    bd = sum(1 for e in bm.edges if len(e.link_faces) == 1)
    # signed volume of each connected component that is closed
    inverted = 0
    closed = 0
    seen = set()
    for f in bm.faces:
        if f.index in seen:
            continue
        stack = [f]
        comp = []
        seen.add(f.index)
        while stack:
            g = stack.pop()
            comp.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h.index not in seen:
                        seen.add(h.index)
                        stack.append(h)
        is_closed = all(len(e.link_faces) == 2 for g in comp for e in g.edges)
        if is_closed:
            closed += 1
            vol = 0.0
            for g in comp:
                vs = [l.vert.co for l in g.loops]
                for i in range(1, len(vs) - 1):
                    vol += vs[0].dot(vs[i].cross(vs[i + 1])) / 6.0
            if vol < 0:
                inverted += 1
    co = np.array([v.co[:] for v in bm.verts]) if bm.verts else np.zeros((1, 3))
    out = dict(tris=sum(len(f.verts) - 2 for f in bm.faces), nonmanifold_edges=nm, open_edges=bd,
               closed_shells=closed, inverted_shells=inverted,
               bbox_min=[round(float(x), 2) for x in co.min(axis=0)], bbox_max=[round(float(x), 2) for x in co.max(axis=0)])
    uv = ob.data.uv_layers[0].data
    arr = np.zeros(len(uv) * 2, np.float32)
    uv.foreach_get('uv', arr)
    bm.free()
    return out


def export_fbx(path, lod_objs, group_name, coll):
    """One FBX per mesh: an LodGroup empty with children <name>_LOD0..n."""
    emp = bpy.data.objects.new(group_name, None)
    emp['fbx_type'] = 'LodGroup'
    coll.objects.link(emp)
    for ob in lod_objs:
        ob.parent = emp
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    emp.select_set(True)
    for ob in lod_objs:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = emp
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'EMPTY', 'MESH'},
                             mesh_smooth_type='FACE', use_tspace=True, add_leaf_bones=False, bake_anim=False,
                             apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y',
                             path_mode='STRIP', embed_textures=False, use_mesh_modifiers=True,
                             colors_type='LINEAR', use_custom_props=False)
    return emp


def noise1(x, seed=0):
    """Cheap smooth 1D noise (sum of sines), roughly -1..1."""
    return (math.sin(x * 1.0 + seed * 1.7) * 0.5 + math.sin(x * 2.3 + seed * 3.1) * 0.3
            + math.sin(x * 5.3 + seed * 0.7) * 0.2)


def noise2(x, y, seed=0):
    return (math.sin(x * 0.9 + seed) * math.cos(y * 1.1 - seed * 0.5) * 0.5
            + math.sin(x * 2.1 + y * 1.7 + seed * 2.3) * 0.3 + math.sin(x * 4.7 - y * 3.9 + seed) * 0.2)


# --------------------------------------------------------------------------- MB <-> bmesh, cuts, booleans


def mb_to_bm(mb):
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new('UVMap')
    col = bm.verts.layers.float_color.new('Col')
    vs = []
    for p, c in zip(mb.v, mb.col):
        v = bm.verts.new(p)
        v[col] = c
        vs.append(v)
    bm.verts.ensure_lookup_table()
    for f, m, uv, s in zip(mb.f, mb.fm, mb.fuv, mb.fs):
        try:
            face = bm.faces.new([vs[i] for i in f])
        except ValueError:
            continue
        face.material_index = m
        face.smooth = s
        for l, t in zip(face.loops, uv):
            l[uvl].uv = t
    return bm


def bm_to_mb(bm):
    mb = MB()
    uvl = bm.loops.layers.uv.get('UVMap') or bm.loops.layers.uv.active
    col = bm.verts.layers.float_color.get('Col')
    bm.verts.index_update()
    for v in bm.verts:
        mb.v.append(tuple(v.co))
        mb.col.append(tuple(v[col]) if col else (0, 0, 0, 1))
    for f in bm.faces:
        mb.f.append(tuple(l.vert.index for l in f.loops))
        mb.fm.append(f.material_index)
        mb.fuv.append([tuple(l[uvl].uv) if uvl else (0, 0) for l in f.loops])
        mb.fs.append(f.smooth)
    return mb


def mb_weld(mb, dist=1e-4):
    bm = mb_to_bm(mb)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=dist)
    out = bm_to_mb(bm)
    bm.free()
    return out


def mb_cut_planes(mb, planes, cap_mat=STRUCT, cap_tile=8.0, cap_cell=None, fill=True):
    """Remove everything on the +normal side of each plane (co, no); fill the cut loops."""
    bm = mb_to_bm(mb)
    uvl = bm.loops.layers.uv['UVMap']
    for co, no in planes:
        res = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], dist=1e-5,
                                     plane_co=Vector(co), plane_no=Vector(no), clear_outer=True)
        if not fill:
            continue
        cut_edges = [e for e in res['geom_cut'] if isinstance(e, bmesh.types.BMEdge) and e.is_valid and e.is_boundary]
        if not cut_edges:
            continue
        try:
            nf = bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=False, edges=cut_edges, normal=Vector(no))
        except Exception:
            continue
        faces = [f for f in nf['geom'] if isinstance(f, bmesh.types.BMFace)]
        for f in faces:
            f.material_index = cap_mat
            f.smooth = False
            n = f.normal
            if n.dot(Vector(no)) < 0:
                f.normal_flip()
            n = f.normal
            for l in f.loops:
                p = l.vert.co
                if cap_cell is not None:
                    l[uvl].uv = cell_uv(cap_cell, (p.x % 6.0) / 6.0, (p.y % 6.0) / 6.0)
                else:
                    l[uvl].uv = uv_box(p, n, cap_tile)
    out = bm_to_mb(bm)
    bm.free()
    return out


def mb_boolean(mb, cutter, mats, op='DIFFERENCE'):
    """Exact boolean of a closed MB with a closed cutter MB (cutter faces keep their own material and UVs)."""
    coll = bpy.context.scene.collection
    a = to_object(mb, '_bool_a', mats, coll)
    b = to_object(cutter, '_bool_b', mats, coll)
    mod = a.modifiers.new('b', 'BOOLEAN')
    mod.operation = op
    mod.solver = 'EXACT'
    mod.object = b
    mod.material_mode = 'INDEX'
    dg = bpy.context.evaluated_depsgraph_get()
    ev = a.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    bm = bmesh.new()
    bm.from_mesh(me)
    if not bm.verts.layers.float_color.get('Col'):
        bm.verts.layers.float_color.new('Col')
    out = bm_to_mb(bm)
    bm.free()
    for o in (a, b):
        m = o.data
        bpy.data.objects.remove(o)
        bpy.data.meshes.remove(m)
    bpy.data.meshes.remove(me)
    return out


def blob(center, radius, seed, level=2, rough=0.3, mat=STRUCT, tile=8.0, squash=(1, 1, 1)):
    """A closed noisy ico blob, used as a boolean cutter (box UVs, given material)."""
    mb = MB()
    vs, fs = ico(level)
    rng = random.Random(seed)
    ph = [rng.random() * 10 for _ in range(4)]
    base = len(mb.v)
    for v in vs:
        k = 1 + rough * (math.sin(v.x * 3.1 + ph[0]) * math.sin(v.y * 2.7 + ph[1]) + 0.5 * math.sin(v.z * 4.3 + ph[2])
                         + 0.35 * math.sin((v.x - v.z) * 7.0 + ph[3]))
        mb.vert((center[0] + v.x * radius * k * squash[0], center[1] + v.y * radius * k * squash[1],
                 center[2] + v.z * radius * k * squash[2]))
    for f in fs:
        box_face(mb, [base + i for i in f], mat, tile)
    return mb


def mb_split_sector(mb, a1, a2, cap_mat=STRUCT, cap_tile=8.0, cap_cell=None):
    """Split a closed mesh by two vertical half-planes through the Z axis at angles a1 < a2 (span < pi).
    Returns (inside, [outside pieces]); every piece is closed again (cut faces filled)."""
    n1 = Vector((-math.sin(a1), math.cos(a1), 0.0))
    n2 = Vector((-math.sin(a2), math.cos(a2), 0.0))
    o = (0.0, 0.0, 0.0)
    kw = dict(cap_mat=cap_mat, cap_tile=cap_tile, cap_cell=cap_cell)
    inside = mb_cut_planes(mb, [(o, -n1), (o, n2)], **kw)
    out_a = mb_cut_planes(mb, [(o, n1)], **kw)
    out_b = mb_cut_planes(mb, [(o, -n1), (o, -n2)], **kw)
    return inside, [out_a, out_b]
