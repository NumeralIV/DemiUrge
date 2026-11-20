# DEMIURGE art pipeline - dmkit: procedural modelling kit for Blender (bpy 5.x), shared by every asset script.
#
# Conventions (binding for every asset, see docs/lab/asset-pipeline.md):
#   * metres, Z up; pivot = ground level at the centre of the footprint; in the .blend the FRONT faces -Y
#     (Blender's Front view shows the front). The exporter turns the asset 180 deg so that in Unreal the front
#     faces -Y, as the towns factory expects (DmTownsKit.h).
#   * every part is built by an MB (mesh builder) in its own local frame and becomes one Blender object; UV0 is
#     metric: 1 UV unit = one tile of the material's tileable texture (tile size from art/textures/manifest.json).
#   * material slots are named after texture sets plus an optional weathering style: "rubble", "oak@wet".
#   * vertex colour "Mask" (RGBA, linear): R ambient occlusion (baked), G stain, B growth, A fire light.
import math
import os
import random

import bmesh
import bpy
import mathutils
from mathutils import Matrix, Vector, noise

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
TEX = os.path.join(ROOT, 'textures')


def tile_of(mat):
    import json
    base = mat.split('@')[0]
    global _TILES
    try:
        _TILES
    except NameError:
        _TILES = None
    if _TILES is None:
        p = os.path.join(TEX, 'manifest.json')
        _TILES = json.load(open(p)) if os.path.exists(p) else {}
    return _TILES.get(base, {}).get('tile_m', 1.0)


def V(x, y=None, z=None):
    if y is None:
        return Vector(x)
    return Vector((x, y, z))


# =========================================================================== mesh builder


class MB:
    """Accumulates a mesh: shared vertices (optionally keyed), polygons with a material name and per-corner UVs."""

    def __init__(self, name):
        self.name = name
        self.P = []
        self.keys = {}
        self.F = []          # (indices, mat, uvs)
        self.M = Matrix.Identity(4)
        self.stain = {}      # vertex index -> extra per-vertex data (dict), e.g. growth, stain, fixed
        self.vdata = []

    # transform applied to positions added from now on (local building frames, fallen / tilted parts)
    def push(self, m):
        old = self.M
        self.M = self.M @ m
        return old

    def pop(self, old):
        self.M = old

    def v(self, p, key=None):
        wp = self.M @ Vector(p)
        if key is not None:
            k = (key, round(wp.x, 4), round(wp.y, 4), round(wp.z, 4))
            i = self.keys.get(k)
            if i is not None:
                return i
            self.keys[k] = len(self.P)
        self.P.append(wp)
        self.vdata.append(None)
        return len(self.P) - 1

    def face(self, idx, mat, uvs, want=None):
        """Polygon; if want (local-frame direction) is given the winding is fixed so the normal points along it."""
        # drop consecutive duplicates (grids collapsing at an apex)
        k = [i for i in range(len(idx)) if idx[i] != idx[(i + 1) % len(idx)]]
        idx = [idx[i] for i in k]
        uvs = [uvs[i] for i in k]
        if len(set(idx)) < 3:
            return
        if want is not None:
            w = (self.M.to_3x3() @ Vector(want))
            n = poly_normal([self.P[i] for i in idx])
            if n.dot(w) < 0:
                idx = list(reversed(idx))
                uvs = list(reversed(uvs))
        self.F.append((list(idx), mat, [tuple(u) for u in uvs]))

    def append(self, other):
        off = len(self.P)
        self.P.extend(other.P)
        self.vdata.extend(other.vdata)
        for idx, mat, uvs in other.F:
            self.F.append(([i + off for i in idx], mat, uvs))

    def tris(self):
        return sum(len(f[0]) - 2 for f in self.F)

    def to_object(self, coll, smooth_angle=35.0, merge=0.0005):
        me = bpy.data.meshes.new(self.name)
        me.from_pydata([tuple(p) for p in self.P], [], [f[0] for f in self.F])
        mats = []
        for _, m, _ in self.F:
            if m not in mats:
                mats.append(m)
        for m in mats:
            me.materials.append(get_material(m))
        me.polygons.foreach_set('material_index', [mats.index(f[1]) for f in self.F])
        uv = me.uv_layers.new(name='UV0')
        flat = []
        for _, _, uvs in self.F:
            for u in uvs:
                flat.extend(u)
        uv.data.foreach_set('uv', flat)
        me.validate(clean_customdata=False)
        me.update()
        ob = bpy.data.objects.new(self.name, me)
        coll.objects.link(ob)
        if merge > 0:
            bm = bmesh.new()
            bm.from_mesh(me)
            bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=merge)
            bm.to_mesh(me)
            bm.free()
        for p in me.polygons:
            p.use_smooth = True
        if smooth_angle is not None:
            me.set_sharp_from_angle(angle=math.radians(smooth_angle))
        return ob


def poly_normal(pts):
    n = Vector((0, 0, 0))
    for i in range(len(pts)):
        a = pts[i]
        b = pts[(i + 1) % len(pts)]
        n.x += (a.y - b.y) * (a.z + b.z)
        n.y += (a.z - b.z) * (a.x + b.x)
        n.z += (a.x - b.x) * (a.y + b.y)
    return n.normalized() if n.length > 1e-12 else Vector((0, 0, 1))


def box_uv(p, n, tile, grain='z', off=(0.0, 0.0)):
    """Metric box projection. grain = axis along which v runs (wood grain, courses run along u)."""
    ax = max(range(3), key=lambda i: abs(n[i]))
    g = 'xyz'.index(grain)
    others = [i for i in range(3) if i != ax]
    if g in others:
        vi = g
        ui = others[0] if others[1] == g else others[1]
    else:
        ui, vi = others
    s = 1.0
    # keep the texture readable (not mirrored) on opposite faces
    if ax == 0:
        s = 1.0 if n[0] < 0 else -1.0
    elif ax == 1:
        s = -1.0 if n[1] < 0 else 1.0
    elif ax == 2:
        s = 1.0 if n[2] > 0 else -1.0
    u = p[ui] * (s if ui != 2 else 1.0)
    return ((u + off[0]) / tile, (p[vi] + off[1]) / tile)


# =========================================================================== primitives


def quad_uv_box(mb, pts_local, mat, want, grain='z', off=(0.0, 0.0), key=None):
    tile = tile_of(mat)
    idx = [mb.v(p, key) for p in pts_local]
    uvs = [box_uv(Vector(p), Vector(want), tile, grain, off) for p in pts_local]
    mb.face(idx, mat, uvs, want)


def cbox(mb, ctr, half, mat, c=0.02, grain='z', skip=(), off=(0.0, 0.0)):
    """Chamfered box: centre, half extents, chamfer c (0 = sharp). skip: faces to omit, e.g. ('-z',)."""
    cx, cy, cz = ctr
    hx, hy, hz = half
    c = min(c, hx * 0.45, hy * 0.45, hz * 0.45)
    tile = tile_of(mat)

    def P(sx, sy, sz, a):
        # vertex of corner (sx,sy,sz) belonging to the face perpendicular to axis a
        d = [hx - c, hy - c, hz - c]
        d[a] = [hx, hy, hz][a]
        return Vector((cx + sx * d[0], cy + sy * d[1], cz + sz * d[2]))

    def F(pts, want):
        idx = [mb.v(p, ('cb', id(mb), cx, cy, cz)) for p in pts]
        sc = TOP_UV if (want[2] > 0.9 and grain == 'z' and mat.split('@')[0] in ('limestone', 'limestone_fresh', 'plaster', 'dressed_stone')) else 1.0
        uvs = [tuple(x * sc for x in box_uv(p, Vector(want), tile, grain, off)) for p in pts]
        mb.face(idx, mat, uvs, want)

    S = (-1, 1)
    names = {(0, -1): '-x', (0, 1): '+x', (1, -1): '-y', (1, 1): '+y', (2, -1): '-z', (2, 1): '+z'}
    for a in range(3):
        for s in S:
            if names[(a, s)] in skip:
                continue
            b, d = [i for i in range(3) if i != a]
            pts = []
            for (u, w) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                sg = [0, 0, 0]
                sg[a] = s
                sg[b] = u
                sg[d] = w
                pts.append(P(sg[0], sg[1], sg[2], a))
            want = [0, 0, 0]
            want[a] = s
            F(pts, want)
    if c <= 0:
        return
    # edge strips
    for a in range(3):          # edge parallel to axis a
        b, d = [i for i in range(3) if i != a]
        for sb in S:
            for sd in S:
                if (names[(b, sb)] in skip) and (names[(d, sd)] in skip):
                    continue
                if names[(b, sb)] in skip or names[(d, sd)] in skip:
                    continue
                pts = []
                for sa in (-1, 1):
                    sg = [0, 0, 0]
                    sg[a] = sa
                    sg[b] = sb
                    sg[d] = sd
                    pts.append(P(sg[0], sg[1], sg[2], b))
                for sa in (1, -1):
                    sg = [0, 0, 0]
                    sg[a] = sa
                    sg[b] = sb
                    sg[d] = sd
                    pts.append(P(sg[0], sg[1], sg[2], d))
                want = [0, 0, 0]
                want[b] = sb
                want[d] = sd
                F(pts, want)
    # corner triangles
    for sx in S:
        for sy in S:
            for sz in S:
                if names[(0, sx)] in skip or names[(1, sy)] in skip or names[(2, sz)] in skip:
                    continue
                F([P(sx, sy, sz, 0), P(sx, sy, sz, 1), P(sx, sy, sz, 2)], (sx, sy, sz))


def grid_face(mb, o, du, dv, nu, nv, mat, want, uv_of, disp=None, key='g'):
    """Grid of quads o + du*i/nu + dv*j/nv (vectors in the local frame). uv_of(p) -> (u, v) in metres.
    disp(p, n) -> displacement vector (local). Vertices on the grid border are keyed so neighbours weld."""
    o, du, dv = Vector(o), Vector(du), Vector(dv)
    tile = tile_of(mat)
    n = Vector(want).normalized()
    ids = {}
    pos = {}
    for j in range(nv + 1):
        for i in range(nu + 1):
            p = o + du * (i / nu) + dv * (j / nv)
            q = p + (disp(p, n) if disp else Vector())
            ids[(i, j)] = mb.v(q, (key, round(p.x, 4), round(p.y, 4), round(p.z, 4)))
            pos[(i, j)] = p
    for j in range(nv):
        for i in range(nu):
            c = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
            uvs = [tuple(x / tile for x in uv_of(pos[k])) for k in c]
            mb.face([ids[k] for k in c], mat, uvs, want)


TOP_UV = 0.55    # horizontal faces of masonry boxes use larger slabs (texture courses become 0.36 m paving rows)


def gbox(mb, mn, mx, mat, spacing=0.5, skip=(), disp=None, key='gb', uv_z0=0.0):
    """Box whose faces are grids (for vertex AO / masks and displacement). Courses are aligned to z (v = z - uv_z0)."""
    mn, mx = Vector(mn), Vector(mx)
    L = mx - mn

    def cnt(x):
        return max(1, int(round(x / spacing)))
    nx, ny, nz = cnt(L.x), cnt(L.y), cnt(L.z)
    X, Y, Z = Vector((L.x, 0, 0)), Vector((0, L.y, 0)), Vector((0, 0, L.z))
    if '-y' not in skip:
        grid_face(mb, mn, X, Z, nx, nz, mat, (0, -1, 0), lambda p: (p.x, p.z - uv_z0), disp, key)
    if '+y' not in skip:
        grid_face(mb, mn + Y, X, Z, nx, nz, mat, (0, 1, 0), lambda p: (-p.x, p.z - uv_z0), disp, key)
    if '-x' not in skip:
        grid_face(mb, mn, Y, Z, ny, nz, mat, (-1, 0, 0), lambda p: (-p.y, p.z - uv_z0), disp, key)
    if '+x' not in skip:
        grid_face(mb, mn + X, Y, Z, ny, nz, mat, (1, 0, 0), lambda p: (p.y, p.z - uv_z0), disp, key)
    if '+z' not in skip:
        grid_face(mb, mn + Z, X, Y, nx, ny, mat, (0, 0, 1), lambda p: (p.x * TOP_UV, p.y * TOP_UV), disp, key)
    if '-z' not in skip:
        grid_face(mb, mn, X, Y, nx, ny, mat, (0, 0, -1), lambda p: (p.x, -p.y), disp, key)


def frames_along(pts):
    """Parallel-transported frames along a polyline: list of (tangent, normal, binormal)."""
    pts = [Vector(p) for p in pts]
    T = []
    for i in range(len(pts)):
        a = pts[max(0, i - 1)]
        b = pts[min(len(pts) - 1, i + 1)]
        T.append((b - a).normalized())
    up = Vector((0, 0, 1)) if abs(T[0].z) < 0.9 else Vector((1, 0, 0))
    N = [T[0].cross(up).cross(T[0]).normalized()]
    for i in range(1, len(pts)):
        n = N[-1] - T[i] * N[-1].dot(T[i])
        if n.length < 1e-6:
            n = N[-1]
        N.append(n.normalized())
    return [(T[i], N[i], T[i].cross(N[i]).normalized()) for i in range(len(pts))]


def tube(mb, pts, radii, sides, mat, cap0=False, cap1=False, uv_v0=0.0, jitter=0.0, seed=0):
    """Tapered tube along a polyline. UV u = metres around, v = metres along (bark / rope / grain stays to scale)."""
    pts = [Vector(p) for p in pts]
    if isinstance(radii, (int, float)):
        radii = [radii] * len(pts)
    fr = frames_along(pts)
    tile = tile_of(mat)
    rings = []
    along = uv_v0
    rnd = random.Random(seed)
    for i, p in enumerate(pts):
        if i > 0:
            along += (pts[i] - pts[i - 1]).length
        t, n, b = fr[i]
        ring = []
        for k in range(sides + 1):
            a = 2 * math.pi * k / sides
            r = radii[i] * (1 + (rnd.uniform(-jitter, jitter) if k < sides else 0))
            q = p + (n * math.cos(a) + b * math.sin(a)) * r
            ring.append((q, (radii[i] * a / tile, along / tile), (n * math.cos(a) + b * math.sin(a))))
        if jitter:
            ring[sides] = (ring[0][0], ring[sides][1], ring[sides][2])
        rings.append(ring)
    vid = [[mb.v(q) for (q, _, _) in r] for r in rings]
    for i in range(len(pts) - 1):
        for k in range(sides):
            idx = [vid[i][k], vid[i][k + 1], vid[i + 1][k + 1], vid[i + 1][k]]
            uvs = [rings[i][k][1], rings[i][k + 1][1], rings[i + 1][k + 1][1], rings[i + 1][k][1]]
            out = (rings[i][k][2] + rings[i + 1][k + 1][2])
            mb.face(idx, mat, uvs, mb.M.inverted().to_3x3() @ (mb.M.to_3x3() @ out) if False else out)
    for cap, i, sgn in ((cap0, 0, -1), (cap1, len(pts) - 1, 1)):
        if cap and radii[i] > 1e-4:
            t = fr[i][0]
            c = mb.v(pts[i] + t * (radii[i] * 0.3 * sgn))
            for k in range(sides):
                idx = [vid[i][k], vid[i][k + 1], c]
                uvs = [(rings[i][k][2].x * radii[i] / tile, rings[i][k][2].y * radii[i] / tile),
                       (rings[i][k + 1][2].x * radii[i] / tile, rings[i][k + 1][2].y * radii[i] / tile), (0, 0)]
                mb.face(idx, mat, uvs, t * sgn)


def lathe(mb, prof, sides, mat, ctr=(0, 0, 0), cap_top=True, cap_bottom=False, key='la'):
    """Surface of revolution about the local Z axis through ctr. prof = [(r, z), ...] bottom to top."""
    ctr = Vector(ctr)
    tile = tile_of(mat)
    rings = []
    s_along = [0.0]
    for i in range(1, len(prof)):
        s_along.append(s_along[-1] + math.hypot(prof[i][0] - prof[i - 1][0], prof[i][1] - prof[i - 1][1]))
    for (r, z) in prof:
        rings.append([ctr + Vector((r * math.cos(2 * math.pi * k / sides), r * math.sin(2 * math.pi * k / sides), z)) for k in range(sides + 1)])
    vid = [[mb.v(p) for p in ring] for ring in rings]
    for i in range(len(prof) - 1):
        rm = max(prof[i][0], prof[i + 1][0], 0.01)
        for k in range(sides):
            a0 = 2 * math.pi * k / sides
            a1 = 2 * math.pi * (k + 1) / sides
            am = (a0 + a1) / 2
            idx = [vid[i][k], vid[i][k + 1], vid[i + 1][k + 1], vid[i + 1][k]]
            uvs = [(rm * a0 / tile, s_along[i] / tile), (rm * a1 / tile, s_along[i] / tile), (rm * a1 / tile, s_along[i + 1] / tile), (rm * a0 / tile, s_along[i + 1] / tile)]
            dz = prof[i + 1][0] - prof[i][0]
            want = Vector((math.cos(am), math.sin(am), 0)) * (prof[i + 1][1] - prof[i][1]) + Vector((0, 0, -dz))
            if want.length < 1e-6:
                want = Vector((0, 0, 1 if dz < 0 else -1))
            mb.face(idx, mat, uvs, want)
    for cap, i, sgn in ((cap_top, len(prof) - 1, 1), (cap_bottom, 0, -1)):
        if cap and prof[i][0] > 1e-4:
            c = mb.v(ctr + Vector((0, 0, prof[i][1])))
            r = prof[i][0]
            for k in range(sides):
                a0 = 2 * math.pi * k / sides
                a1 = 2 * math.pi * (k + 1) / sides
                mb.face([vid[i][k], vid[i][k + 1], c], mat,
                        [(r * math.cos(a0) / tile, r * math.sin(a0) / tile), (r * math.cos(a1) / tile, r * math.sin(a1) / tile), (0, 0)], (0, 0, sgn))


def ico(subdiv):
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=1.0)
    verts = [v.co.copy() for v in bm.verts]
    faces = [[v.index for v in f.verts] for f in bm.faces]
    bm.free()
    return verts, faces


def blob(mb, ctr, size, mat, seed, subdiv=2, rough=0.18, flat_bottom=None, squash_top=0.0, rot=0.0, scale_fn=None):
    """Rock / stone / skull: icosphere scaled to size (full extents), fbm-displaced, optional flat bottom (z, local).
    UV = metric box projection."""
    verts, faces = ico(subdiv)
    ctr = Vector(ctr)
    sx, sy, sz = size[0] / 2, size[1] / 2, size[2] / 2
    off = Vector((seed * 17.31, seed * 5.77, seed * 11.13))
    R = Matrix.Rotation(rot, 3, 'Z')
    tile = tile_of(mat)
    pts = []
    for v in verts:
        d = v.normalized()
        k = 1.0 + rough * noise.fractal(d * 1.3 + off, 0.6, 2.0, 4) + rough * 0.35 * noise.noise(d * 4.0 + off)
        p = Vector((d.x * sx * k, d.y * sy * k, d.z * sz * k))
        if scale_fn:
            p = scale_fn(p)
        if squash_top and p.z > 0:
            p.z *= (1.0 - squash_top)
        p = R @ p
        if flat_bottom is not None and p.z < flat_bottom - 0.0:
            p.z = flat_bottom + (p.z - flat_bottom) * 0.05
        pts.append(ctr + p)
    ids = [mb.v(p) for p in pts]
    for f in faces:
        ps = [pts[i] for i in f]
        n = poly_normal(ps)
        c = sum(ps, Vector()) / len(ps)
        want = c - ctr
        uvs = [box_uv(p, n, tile) for p in ps]
        mb.face([ids[i] for i in f], mat, uvs, want)


def card(mb, p0, right, up, w, h, mat, uv_rect, segs=1, bend=0.0, normal=None):
    """Upright quad (strip of segs) from p0 (bottom centre) along up, width along right. uv_rect = (u0, v0, u1, v1)
    in atlas space (not metric). bend: displacement along the normal at the top (metres, quadratic)."""
    p0, right, up = Vector(p0), Vector(right).normalized(), Vector(up).normalized()
    n = normal if normal is not None else right.cross(up)
    u0, v0, u1, v1 = uv_rect
    rows = []
    for j in range(segs + 1):
        f = j / segs
        c = p0 + up * (h * f) + Vector(n) * (bend * f * f)
        rows.append((mb.v(c - right * (w / 2)), mb.v(c + right * (w / 2)), v0 + (v1 - v0) * f))
    for j in range(segs):
        a, b, va = rows[j]
        c, d, vb = rows[j + 1]
        mb.face([a, b, d, c], mat, [(u0, va), (u1, va), (u1, vb), (u0, vb)], n)


# =========================================================================== materials (Blender preview)

_MATS = {}


def camel(name):
    return ''.join(p.capitalize() for p in name.split('_'))


def tex_path(base, kind):
    return os.path.join(TEX, base, 'T_%s_%s.png' % (camel(base), kind))


STYLE_PREVIEW = {   # stain colour multiplier, growth colour (preview only; Unreal uses the MI parameters)
    '': ((0.12, 0.10, 0.09), (0.30, 0.36, 0.12)),
    'wet': ((0.45, 0.45, 0.42), (0.16, 0.22, 0.08)),
    'dust': ((0.35, 0.30, 0.25), (0.48, 0.43, 0.36)),
    'ruin': ((0.12, 0.10, 0.09), (0.26, 0.32, 0.10)),
}


def get_material(name):
    """Blender preview material for a slot name "base@style": textures + vertex mask (AO multiply, stain, growth)."""
    if name in _MATS:
        return _MATS[name]
    if name in bpy.data.materials:
        _MATS[name] = bpy.data.materials[name]
        return _MATS[name]
    base, _, style = name.partition('@')
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bsdf.outputs[0], out.inputs[0])
    bc_p = tex_path(base, 'BC')
    if base == 'flame':
        em = nt.nodes.new('ShaderNodeEmission')
        tr = nt.nodes.new('ShaderNodeBsdfTransparent')
        add = nt.nodes.new('ShaderNodeAddShader')
        img = nt.nodes.new('ShaderNodeTexImage')
        if os.path.exists(bc_p):
            img.image = bpy.data.images.load(bc_p, check_existing=True)
        nt.links.new(img.outputs[0], em.inputs[0])
        em.inputs[1].default_value = 6.0
        nt.links.new(em.outputs[0], add.inputs[0])
        nt.links.new(tr.outputs[0], add.inputs[1])
        nt.links.new(add.outputs[0], out.inputs[0])
        _MATS[name] = m
        return m
    if os.path.exists(bc_p):
        img = nt.nodes.new('ShaderNodeTexImage')
        img.image = bpy.data.images.load(bc_p, check_existing=True)
        img.image.colorspace_settings.name = 'sRGB'
        vc = nt.nodes.new('ShaderNodeVertexColor')
        vc.layer_name = 'Mask'
        sep = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(vc.outputs[0], sep.inputs[0])
        sc, gc = STYLE_PREVIEW.get(style, STYLE_PREVIEW[''])
        mixs = nt.nodes.new('ShaderNodeMix')
        mixs.data_type = 'RGBA'
        mixs.blend_type = 'MULTIPLY'
        mixs.inputs[7].default_value = (*sc, 1)
        nt.links.new(sep.outputs[1], mixs.inputs[0])
        nt.links.new(img.outputs[0], mixs.inputs[6])
        mixg = nt.nodes.new('ShaderNodeMix')
        mixg.data_type = 'RGBA'
        mixg.inputs[7].default_value = (*gc, 1)
        nt.links.new(sep.outputs[2], mixg.inputs[0])
        nt.links.new(mixs.outputs[2], mixg.inputs[6])
        mixa = nt.nodes.new('ShaderNodeMix')
        mixa.data_type = 'RGBA'
        mixa.blend_type = 'MULTIPLY'
        mixa.inputs[0].default_value = 1.0
        nt.links.new(sep.outputs[0], mixa.inputs[7])
        nt.links.new(mixg.outputs[2], mixa.inputs[6])
        nt.links.new(mixa.outputs[2], bsdf.inputs['Base Color'])
        if base in ('foliage', 'fringe'):
            nt.links.new(img.outputs[1], bsdf.inputs['Alpha'])
        if base in ('ember', 'oak_charred'):
            em = nt.nodes.new('ShaderNodeMath')
            em.operation = 'MULTIPLY'
            em.inputs[1].default_value = 4.0 if base == 'ember' else 0.0
            nt.links.new(img.outputs[1], em.inputs[0])
            nt.links.new(em.outputs[0], bsdf.inputs['Emission Strength'])
            bsdf.inputs['Emission Color'].default_value = (1.0, 0.35, 0.08, 1)
    orm_p = tex_path(base, 'ORM')
    if os.path.exists(orm_p):
        img = nt.nodes.new('ShaderNodeTexImage')
        img.image = bpy.data.images.load(orm_p, check_existing=True)
        img.image.colorspace_settings.name = 'Non-Color'
        sep = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(img.outputs[0], sep.inputs[0])
        nt.links.new(sep.outputs[1], bsdf.inputs['Roughness'])
    n_p = tex_path(base, 'N')
    if os.path.exists(n_p):
        img = nt.nodes.new('ShaderNodeTexImage')
        img.image = bpy.data.images.load(n_p, check_existing=True)
        img.image.colorspace_settings.name = 'Non-Color'
        nm = nt.nodes.new('ShaderNodeNormalMap')
        nm.uv_map = 'UV0'
        nt.links.new(img.outputs[0], nm.inputs[1])
        nt.links.new(nm.outputs[0], bsdf.inputs['Normal'])
    bsdf.inputs['Specular IOR Level'].default_value = 0.35
    _MATS[name] = m
    return m


# =========================================================================== scene helpers


def collection(name, parent=None):
    c = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    p = parent or bpy.context.scene.collection
    if c.name not in [x.name for x in p.children]:
        p.children.link(c)
    return c


def clean_scene():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)
    _MATS.clear()


def join_copies(objs, name, coll):
    """Joined copy of objects (applies transforms), keeping material slots by name."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    mats = []
    for ob in objs:
        tmp = ob.data.copy()
        tmp.transform(ob.matrix_world)
        remap = []
        for m in tmp.materials:
            if m.name not in mats:
                mats.append(m.name)
            remap.append(mats.index(m.name))
        for p in tmp.polygons:
            p.material_index = remap[p.material_index] if remap else 0
        bm.from_mesh(tmp)
        bpy.data.meshes.remove(tmp)
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(bpy.data.materials[m])
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def fbm(p, octaves=3, scale=1.0, seed=0.0):
    return noise.fractal(Vector(p) * scale + Vector((seed, seed * 0.7, seed * 1.3)), 0.6, 2.0, octaves)


# =========================================================================== walls


def wall(mb, L, t, z0, top, holes, mat, ds=0.25, dz=0.3, cap0=True, cap1=True, jitter=0.012, corner=0.03,
         rough_top=0.0, seed=0, uoff=0.0, kinks=(), key='w'):
    """Solid wall slab in a local frame: s along [0, L], w across [0, t] (w = t is the OUTER face), z up from z0.
    top(s) -> top height; holes = [(s0, s1, z0, z1)] through openings. Front/back faces are grids (vertex masks,
    displacement), the top follows top(s) (use kinks for steps / breaks). Displacement is a pure function of the
    undisplaced position, so shared vertices weld."""
    tile = tile_of(mat)
    br = set([0.0, L])
    n = max(1, int(round(L / ds)))
    for i in range(n + 1):
        br.add(round(L * i / n, 5))
    for (a, b, _, _) in holes:
        br.add(round(a, 5))
        br.add(round(b, 5))
    for k in kinks:
        if 0 < k < L:
            br.add(round(k, 5))
    br = sorted(br)
    zmax = max(top(s) for s in br)
    lev = set()
    m = max(1, int(round((zmax - z0) / dz)))
    for i in range(m + 1):
        lev.add(round(z0 + (zmax - z0) * i / m, 5))
    for (_, _, a, b) in holes:
        lev.add(round(a, 5))
        lev.add(round(b, 5))
    lev = sorted(lev)
    off = Vector((seed * 3.1, seed * 1.7, 0.0))

    def disp(s, w, z):
        q = Vector((s * 2.3, z * 2.3, w * 0.0)) + off
        d = noise.fractal(q, 0.7, 2.0, 3) * jitter
        e = math.exp(-min(s, L - s) / 0.18)
        ds_ = 0.0
        dw = d * (1.0 if w > 0 else 0.6)
        if corner:
            ds_ += e * corner * noise.noise(Vector((z * 6.0, w * 3.0, s)) + off) * (1 if s < L / 2 else -1)
            dw += e * corner * 0.7 * noise.noise(Vector((z * 5.0 + 3.1, s, w * 2.0)) + off)
        for (a, b, ha, hb) in holes:   # rough reveals
            if ha - 0.01 <= z <= hb + 0.01:
                if abs(s - a) < 1e-4 or abs(s - b) < 1e-4:
                    ds_ += jitter * 0.8 * noise.noise(Vector((z * 7.0, w * 4.0, a)) + off)
        dz_ = 0.0
        if rough_top and z >= top(s) - 1e-5:
            dz_ = rough_top * noise.fractal(Vector((s * 3.0, w * 2.0, 9.0)) + off, 0.6, 2.0, 2)
        return Vector((s + ds_, (w + dw) if w > 0 else (w - dw), z + dz_))

    def P(s, w, z):
        return mb.v(disp(s, w, z), (key, round(s, 4), round(w, 4), round(z, 4)))

    def solid(sm):
        iv = [(z0, None)]   # None = top
        for (a, b, ha, hb) in holes:
            if a - 1e-6 <= sm <= b + 1e-6:
                new = []
                for (za, zb) in iv:
                    zb_ = top(sm) if zb is None else zb
                    if hb <= za or ha >= zb_:
                        new.append((za, zb))
                        continue
                    if ha > za:
                        new.append((za, ha))
                    if hb < zb_:
                        new.append((hb, zb))
                iv = new
        return iv

    def edge_levels(za, zb):
        return [za] + [z for z in lev if za + 1e-5 < z < zb - 1e-5] + [zb]

    def uv_face(w, s, z):
        return (((s if w > 0 else -s) + uoff + (0 if w > 0 else 0.37)) / tile, (z - z0) / tile)

    for c in range(len(br) - 1):
        sa, sb = br[c], br[c + 1]
        sm = (sa + sb) / 2
        for (za, zb) in solid(sm):
            is_top = zb is None
            zl = top(sa) if is_top else zb
            zr = top(sb) if is_top else zb
            if zl <= za + 1e-4 and zr <= za + 1e-4:
                continue
            Ll = edge_levels(za, max(zl, za + 1e-3))
            Lr = edge_levels(za, max(zr, za + 1e-3))
            common = [z for z in Ll if z in Lr and z < min(zl, zr) - 1e-5]
            for w, want in ((t, (0, 1, 0)), (0.0, (0, -1, 0))):
                for i in range(len(common) - 1):
                    q = [(sa, common[i]), (sb, common[i]), (sb, common[i + 1]), (sa, common[i + 1])]
                    mb.face([P(s, w, z) for s, z in q], mat, [uv_face(w, s, z) for s, z in q], want)
                zc = common[-1]
                left = [z for z in Ll if z >= zc - 1e-6]
                right = [z for z in Lr if z >= zc - 1e-6]
                poly = [(sa, z) for z in left] + [(sb, z) for z in reversed(right)]
                if len(poly) >= 3:
                    mb.face([P(s, w, z) for s, z in poly], mat, [uv_face(w, s, z) for s, z in poly], want)
            # top / sill (interval top) and head (interval bottom above an opening)
            q = [(sa, 0.0, zl), (sa, t, zl), (sb, t, zr), (sb, 0.0, zr)]
            mb.face([P(*x) for x in q], mat, [((x[0] + uoff) / tile, x[1] / tile) for x in q], (0, 0, 1))
            if za > z0 + 1e-5:
                q = [(sa, 0.0, za), (sa, t, za), (sb, t, za), (sb, 0.0, za)]
                mb.face([P(*x) for x in q], mat, [((x[0] + uoff) / tile, x[1] / tile) for x in q], (0, 0, -1))

    def cover(s_mid):
        out = []
        for (za, zb) in solid(s_mid):
            out.append((za, top(s_mid) if zb is None else zb))
        return out

    def side(s, za, zb, want_s):
        if zb - za < 1e-4:
            return
        zs = edge_levels(za, zb)
        for i in range(len(zs) - 1):
            q = [(s, 0.0, zs[i]), (s, t, zs[i]), (s, t, zs[i + 1]), (s, 0.0, zs[i + 1])]
            mb.face([P(*x) for x in q], mat, [(x[1] / tile, (x[2] - z0) / tile) for x in q], (want_s, 0, 0))

    def diff(A, B):
        """intervals of A not covered by B"""
        out = []
        for (a0, a1) in A:
            segs = [(a0, a1)]
            for (b0, b1) in B:
                nxt = []
                for (c0, c1) in segs:
                    if b1 <= c0 or b0 >= c1:
                        nxt.append((c0, c1))
                        continue
                    if b0 > c0:
                        nxt.append((c0, b0))
                    if b1 < c1:
                        nxt.append((b1, c1))
                segs = nxt
            out.extend(segs)
        return out

    for c in range(1, len(br) - 1):
        s = br[c]
        A = cover((br[c - 1] + s) / 2)
        B = cover((s + br[c + 1]) / 2)
        for (a, b) in diff(A, B):
            side(s, a, min(b, top(s)), 1)
        for (a, b) in diff(B, A):
            side(s, a, min(b, top(s)), -1)
    if cap0:
        for (a, b) in cover(br[0] + 1e-4):
            side(0.0, a, min(b, top(0.0)), -1)
    if cap1:
        for (a, b) in cover(br[-1] - 1e-4):
            side(L, a, min(b, top(L)), 1)


def frame(origin, ex, ey, ez=(0, 0, 1)):
    """4x4 matrix mapping local (x, y, z) to origin + x*ex + y*ey + z*ez."""
    m = Matrix.Identity(4)
    for i in range(3):
        m[i][0] = ex[i]
        m[i][1] = ey[i]
        m[i][2] = ez[i]
        m[i][3] = origin[i]
    return m
