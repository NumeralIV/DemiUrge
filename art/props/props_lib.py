# DEMIURGE props - geometry, material, LOD and export helpers (Blender 5.2, bpy + bmesh + mathutils).
#
# Geometry is assembled from Parts: plain Python lists of vertices, faces, per-corner UVs, a slot name per
# face, a vertex-colour per face and a smoothing angle per face. Builders make a part in its own local frame
# (wood is always modelled with its grain along local +Z, then rotated into place), compute the tiling UVs
# there (UV units = texture tiles, from the set's metres-per-tile), and the part is then transformed and
# merged. build_object() turns a Part into a Blender mesh: shared vertices, sharp edges by angle, all faces
# smooth, UV0 'UVMap' (tiling), UV1 'UVUnique' (0..1 non-overlapping, for baked maps and lightmaps),
# a byte 'Col' corner colour (RGB tint multiplier, A = moss / grime mask), slots in Structure, Roof, Detail order.
import math
import os
import random

import bmesh
import bpy
from mathutils import Matrix, Vector, noise

W = (1.0, 1.0, 1.0, 0.0)          # default vertex colour: no tint, no moss
TILE = {'Rock': 2.0, 'Moss': 1.0, 'Stone': 2.0, 'Metal': 1.0, 'Steel': 1.0, 'Cloth': 0.5, 'Wood': 1.0, 'Straw': 2.0,
        'Leaf': 0.6, 'Stalk': 1.0, 'Glow': 1.0}


class Part:
    __slots__ = ('v', 'f', 'uv', 'mat', 'col', 'ang')

    def __init__(self):
        self.v, self.f, self.uv, self.mat, self.col, self.ang = [], [], [], [], [], []

    def face(self, idx, uvs, mat, col=W, ang=35.0):
        self.f.append(tuple(idx))
        self.uv.append([tuple(u) for u in uvs])
        self.mat.append(mat)
        self.col.append(tuple(col))
        self.ang.append(ang)

    def add(self, *others):
        for o in others:
            if o is None:
                continue
            base = len(self.v)
            self.v.extend(Vector(p) for p in o.v)
            self.f.extend(tuple(i + base for i in fc) for fc in o.f)
            self.uv.extend(o.uv)
            self.mat.extend(o.mat)
            self.col.extend(o.col)
            self.ang.extend(o.ang)
        return self

    def xf(self, M):
        M = Matrix(M)
        self.v = [M @ Vector(p) for p in self.v]
        return self

    def at(self, loc=(0, 0, 0), rot=(0, 0, 0), scale=1.0):
        """Transform: scale, then rotate (XYZ Euler, radians), then translate."""
        from mathutils import Euler
        S = Matrix.Diagonal((scale, scale, scale, 1.0)) if not isinstance(scale, (tuple, list)) else \
            Matrix.Diagonal((scale[0], scale[1], scale[2], 1.0))
        M = Matrix.Translation(Vector(loc)) @ Euler(rot, 'XYZ').to_matrix().to_4x4() @ S
        return self.xf(M)

    def copy(self):
        p = Part()
        p.add(self)
        return p

    def tint(self, col):
        self.col = [tuple(col)] * len(self.f)
        return self

    def setmat(self, mat):
        self.mat = [mat] * len(self.f)
        return self

    def tris(self):
        return sum(len(f) - 2 for f in self.f)

    def bounds(self):
        xs = [p.x for p in self.v]
        ys = [p.y for p in self.v]
        zs = [p.z for p in self.v]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def merge(*parts):
    p = Part()
    p.add(*parts)
    return p


# --------------------------------------------------------------------------- UV helpers
def _box_uv(pts, nrm, mpt, off):
    ax = max(range(3), key=lambda i: abs(nrm[i]))
    if ax == 0:
        a, b = 1, 2
    elif ax == 1:
        a, b = 0, 2
    else:
        a, b = 0, 1
    s = 1.0 / mpt
    flip = -1.0 if (ax == 0 and nrm[0] < 0) or (ax == 1 and nrm[1] > 0) else 1.0
    return [((p[a] * flip) * s + off[0], p[b] * s + off[1]) for p in pts]


def box_uv_part(part, mpt, rng=None):
    """Per-face box projection in the part's current (local) frame."""
    off = (rng.random(), rng.random()) if rng else (0.0, 0.0)
    for i, fc in enumerate(part.f):
        pts = [part.v[j] for j in fc]
        n = Vector((0, 0, 0))
        for k in range(len(pts)):
            n += pts[k].cross(pts[(k + 1) % len(pts)])
        if n.length < 1e-12:
            n = Vector((0, 0, 1))
        part.uv[i] = _box_uv(pts, n.normalized(), mpt, off)
    return part


# --------------------------------------------------------------------------- primitives (local frame)
def box(sx, sy, sz, mat, col=W, mpt=None, rng=None, ang=30.0, z0=True):
    """Axis-aligned box sx * sy * sz, base at z = 0 (z0) or centred. Grain along Z."""
    p = Part()
    hx, hy = sx / 2, sy / 2
    za, zb = (0.0, sz) if z0 else (-sz / 2, sz / 2)
    p.v = [Vector(c) for c in ((-hx, -hy, za), (hx, -hy, za), (hx, hy, za), (-hx, hy, za),
                               (-hx, -hy, zb), (hx, -hy, zb), (hx, hy, zb), (-hx, hy, zb))]
    for fc in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        p.face(fc, [(0, 0)] * 4, mat, col, ang)
    return box_uv_part(p, mpt or TILE.get(mat_base(mat), 1.0), rng)


def bbox(sx, sy, sz, b, mat, col=W, mpt=None, rng=None, z0=True, ang=40.0):
    """Chamfered box (bevel b) as a convex hull: 26 faces, catches edge highlights."""
    hx, hy, hz = sx / 2, sy / 2, sz / 2
    pts = []
    for X in (-1, 1):
        for Y in (-1, 1):
            for Z in (-1, 1):
                pts += [(X * (hx - b), Y * hy, Z * (hz - b)), (X * hx, Y * (hy - b), Z * (hz - b)),
                        (X * (hx - b), Y * (hy - b), Z * hz)]
    p = hull(pts, mat, col, ang)
    if z0:
        p.at((0, 0, hz))
        p = Part().add(p)
    return box_uv_part(p, mpt or TILE.get(mat_base(mat), 1.0), rng)


def hull(points, mat, col=W, ang=35.0):
    bm = bmesh.new()
    for q in points:
        bm.verts.new(q)
    res = bmesh.ops.convex_hull(bm, input=bm.verts[:])
    for g in res.get('geom_interior', []) + res.get('geom_unused', []):
        if isinstance(g, bmesh.types.BMVert) and g.is_valid and not g.link_faces:
            bm.verts.remove(g)
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(1.0), verts=bm.verts[:], edges=bm.edges[:])
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
    p = from_bmesh(bm, mat, col, ang)
    bm.free()
    return p


def from_bmesh(bm, mat, col=W, ang=35.0):
    p = Part()
    bm.verts.index_update()
    p.v = [v.co.copy() for v in bm.verts]
    for f in bm.faces:
        p.face([v.index for v in f.verts], [(0, 0)] * len(f.verts), mat, col, ang)
    return p


def lathe(profile, seg, mat, col=W, mpt=None, cap_bot=True, cap_top=True, ang=50.0, u0=0.0):
    """Surface of revolution around Z; profile = [(r, z), ...] bottom to top. Shared seam vertices,
    per-corner UVs: u around (metres / mpt), v along the profile length."""
    mpt = mpt or TILE.get(mat_base(mat), 1.0)
    p = Part()
    rmax = max(r for r, _ in profile)
    circ = 2 * math.pi * max(rmax, 0.02)
    L = [0.0]
    for i in range(1, len(profile)):
        L.append(L[-1] + math.hypot(profile[i][0] - profile[i - 1][0], profile[i][1] - profile[i - 1][1]))
    rings = []
    poles = []
    for (r, z) in profile:
        ring = []
        if r < 1e-5:   # a point on the axis: one shared vertex, triangles around it
            p.v.append(Vector((0.0, 0.0, z)))
            ring = [len(p.v) - 1] * seg
            poles.append(True)
        else:
            for k in range(seg):
                a = 2 * math.pi * k / seg
                p.v.append(Vector((r * math.cos(a), r * math.sin(a), z)))
                ring.append(len(p.v) - 1)
            poles.append(False)
        rings.append(ring)
    for i in range(len(profile) - 1):
        if poles[i] and poles[i + 1]:
            continue
        for k in range(seg):
            k2 = (k + 1) % seg
            ua, ub = k / seg * circ / mpt + u0, (k + 1) / seg * circ / mpt + u0
            va, vb = L[i] / mpt, L[i + 1] / mpt
            if poles[i]:
                p.face((rings[i][k], rings[i + 1][k2], rings[i + 1][k]),
                       (((ua + ub) / 2, va), (ub, vb), (ua, vb)), mat, col, ang)
            elif poles[i + 1]:
                p.face((rings[i][k], rings[i][k2], rings[i + 1][k]),
                       ((ua, va), (ub, va), ((ua + ub) / 2, vb)), mat, col, ang)
            else:
                p.face((rings[i][k], rings[i][k2], rings[i + 1][k2], rings[i + 1][k]),
                       ((ua, va), (ub, va), (ub, vb), (ua, vb)), mat, col, ang)

    def cap(ring, z, top):
        if profile[0 if not top else -1][0] < 1e-4:
            return
        p.v.append(Vector((0, 0, z)))
        c = len(p.v) - 1
        for k in range(seg):
            k2 = (k + 1) % seg
            a, b = p.v[ring[k]], p.v[ring[k2]]
            uvs = [(a.x / mpt, a.y / mpt), (b.x / mpt, b.y / mpt), (0.0, 0.0)]
            if top:
                p.face((ring[k], ring[k2], c), uvs, mat, col, 30.0)
            else:
                p.face((ring[k2], ring[k], c), [uvs[1], uvs[0], uvs[2]], mat, col, 30.0)
    if cap_bot:
        cap(rings[0], profile[0][1], False)
    if cap_top:
        cap(rings[-1], profile[-1][1], True)
    return p


def cyl(r, h, seg, mat, col=W, r_top=None, mpt=None, caps=True, ang=50.0, z0=0.0):
    rt = r if r_top is None else r_top
    return lathe([(r, z0), (rt, z0 + h)], seg, mat, col, mpt, caps, caps, ang)


def tube(pts, radii, seg, mat, col=W, mpt=None, caps=True, ang=55.0):
    """Sweep a circle along a polyline (parallel-transport frames). radii: float or list."""
    mpt = mpt or TILE.get(mat_base(mat), 1.0)
    pts = [Vector(q) for q in pts]
    if not isinstance(radii, (list, tuple)):
        radii = [radii] * len(pts)
    p = Part()
    tangents = []
    for i in range(len(pts)):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        tangents.append((b - a).normalized())
    t0 = tangents[0]
    up = Vector((0, 0, 1)) if abs(t0.z) < 0.9 else Vector((1, 0, 0))
    nrm = t0.cross(up).normalized()
    L = 0.0
    rings = []
    for i, q in enumerate(pts):
        t = tangents[i]
        if i > 0:
            L += (pts[i] - pts[i - 1]).length
            nrm = (nrm - t * nrm.dot(t)).normalized()
        bn = t.cross(nrm)
        ring = []
        for k in range(seg):
            a = 2 * math.pi * k / seg
            p.v.append(q + (nrm * math.cos(a) + bn * math.sin(a)) * radii[i])
            ring.append(len(p.v) - 1)
        rings.append((ring, L))
    rmax = max(radii)
    circ = 2 * math.pi * rmax
    for i in range(len(rings) - 1):
        (ra, la), (rb, lb) = rings[i], rings[i + 1]
        for k in range(seg):
            k2 = (k + 1) % seg
            ua, ub = k / seg * circ / mpt, (k + 1) / seg * circ / mpt
            p.face((ra[k], ra[k2], rb[k2], rb[k]), ((ua, la / mpt), (ub, la / mpt), (ub, lb / mpt), (ua, lb / mpt)),
                   mat, col, ang)
    if caps:
        for (ring, _), top in ((rings[0], False), (rings[-1], True)):
            c = sum((p.v[j] for j in ring), Vector()) / len(ring)
            p.v.append(c)
            ci = len(p.v) - 1
            for k in range(seg):
                k2 = (k + 1) % seg
                fc = (ring[k], ring[k2], ci) if top else (ring[k2], ring[k], ci)
                p.face(fc, [(0, 0), (0.05, 0), (0.025, 0.05)], mat, col, 30.0)
    return p


def card(w, h, uvrect, mat, col=W, z0=0.0, bend=0.0, cols=1, rows=1):
    """Vertical quad grid in the XZ plane facing -Y, x in [-w/2, w/2], z in [z0, z0 + h];
    uvrect = (u0, v0, u1, v1) of the atlas. bend curls the top toward -Y (metres at the top)."""
    p = Part()
    u0, v0, u1, v1 = uvrect
    for j in range(rows + 1):
        for i in range(cols + 1):
            s, t = i / cols, j / rows
            p.v.append(Vector(((s - 0.5) * w, -bend * t * t, z0 + t * h)))
    for j in range(rows):
        for i in range(cols):
            a = j * (cols + 1) + i
            b, c, d = a + 1, a + cols + 2, a + cols + 1
            uv = lambda k: (u0 + (u1 - u0) * ((k % (cols + 1)) / cols), v0 + (v1 - v0) * ((k // (cols + 1)) / rows))
            p.face((a, b, c, d), (uv(a), uv(b), uv(c), uv(d)), mat, col, 80.0)
    return p


def hcard(w, d, uvrect, mat, col=W, z=0.0):
    """Horizontal quad facing +Z at height z."""
    p = Part()
    u0, v0, u1, v1 = uvrect
    p.v = [Vector((-w / 2, -d / 2, z)), Vector((w / 2, -d / 2, z)), Vector((w / 2, d / 2, z)), Vector((-w / 2, d / 2, z))]
    p.face((0, 1, 2, 3), ((u0, v0), (u1, v0), (u1, v1), (u0, v1)), mat, col, 80.0)
    return p


def ico(radius, subdiv, mat, col=W, ang=60.0):
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=radius)
    p = from_bmesh(bm, mat, col, ang)
    bm.free()
    return p


def displace(part, fn):
    """fn(Vector) -> Vector, applied to every vertex."""
    part.v = [fn(Vector(q)) for q in part.v]
    return part


def lump(radius, subdiv, mat, rng, amp=0.15, freq=1.5, squash=(1, 1, 1), col=W, mpt=None, ang=60.0):
    """A noisy blob (sacks, produce, hedge cores, haystacks)."""
    p = ico(radius, subdiv, mat, col, ang)
    seed = Vector((rng.uniform(-50, 50), rng.uniform(-50, 50), rng.uniform(-50, 50)))

    def f(q):
        n = noise.fractal(q * freq / max(radius, 1e-3) + seed, 0.5, 2.0, 3)
        return Vector((q.x * squash[0], q.y * squash[1], q.z * squash[2])) * (1.0 + amp * n)
    displace(p, f)
    return box_uv_part(p, mpt or TILE.get(mat_base(mat), 1.0), rng)


def mat_base(m):
    """'Role:Tex' -> 'Tex' (the texture set that sets the UV scale)."""
    return m.split(':')[-1]


def role_of(m):
    return m.split(':')[0]


# --------------------------------------------------------------------------- object building
SLOT_ORDER = ('Structure', 'Roof', 'Detail')


def build_object(name, part, slots, collection, unique_uv=True, weld=0.0, roles=None):
    """part.mat entries are role names ('Structure', 'Roof', 'Detail'); slots maps role -> material name.
    roles: force this slot list (the LOD0 slots) so every LOD of a mesh has the same material slots."""
    rl = [role_of(m) for m in part.mat]
    roles = list(roles) if roles else [r for r in SLOT_ORDER if r in set(rl)]
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in part.v], [], [tuple(f) for f in part.f])
    me.update()
    uvl = me.uv_layers.new(name='UVMap')
    col = me.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    li = 0
    uvd = [0.0] * (len(me.loops) * 2)
    cold = [0.0] * (len(me.loops) * 4)
    for pi, poly in enumerate(me.polygons):
        uvs = part.uv[pi]
        c = part.col[pi]
        for k in range(poly.loop_total):
            L = poly.loop_start + k
            uvd[2 * L], uvd[2 * L + 1] = uvs[k]
            cold[4 * L:4 * L + 4] = c
    uvl.data.foreach_set('uv', uvd)
    col.data.foreach_set('color', cold)
    me.polygons.foreach_set('material_index', [roles.index(m) for m in rl])
    for r in roles:
        me.materials.append(get_mat(slots[r]))
    # smoothing: all faces smooth, edges sharp above the smaller angle of their two faces
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    bm.faces.index_update()
    angs = part.ang
    for f in bm.faces:
        f.smooth = True
    for e in bm.edges:
        lf = e.link_faces
        if len(lf) != 2:
            continue
        a = min(angs[lf[0].index], angs[lf[1].index])
        try:
            if e.calc_face_angle() > math.radians(a):
                e.smooth = False
        except ValueError:
            e.smooth = False
    ngons = [f for f in bm.faces if len(f.verts) > 4]
    if ngons:
        bmesh.ops.triangulate(bm, faces=ngons)
    # verifier fix: vertices left without faces (skipped faces of torn awnings) are not exported
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context='VERTS')
    bm.to_mesh(me)
    bm.free()
    me.update()
    ob = bpy.data.objects.new(name, me)
    ob['roles'] = ','.join(roles)
    collection.objects.link(ob)
    if unique_uv:
        add_unique_uv(ob)
    return ob


def add_unique_uv(ob, margin=0.01):
    me = ob.data
    if 'UVUnique' not in me.uv_layers:
        me.uv_layers.new(name='UVUnique')
    me.uv_layers.active = me.uv_layers['UVUnique']
    view_layer = bpy.context.view_layer
    for o in list(bpy.context.selected_objects):
        o.select_set(False)
    view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=margin, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    ob.select_set(False)
    me.uv_layers.active = me.uv_layers['UVMap']
    me.uv_layers[0].active_render = True


def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def decimated_copy(ob, name, ratio, collection):
    """LOD by collapse decimation, UVs and colours carried; returns new object (modifier applied)."""
    tmp = ob.copy()
    tmp.data = ob.data
    collection.objects.link(tmp)
    m = tmp.modifiers.new('dec', 'DECIMATE')
    m.decimate_type = 'COLLAPSE'
    m.ratio = max(0.001, min(1.0, ratio))
    m.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
    me.name = name
    bpy.data.objects.remove(tmp)
    removed = clean_mesh(me)
    new = bpy.data.objects.new(name, me)
    new['cleaned'] = removed
    collection.objects.link(new)
    return new


def clean_mesh(me):
    """Verifier fix (2026-11-19): collapse decimation folds small closed parts into zero-volume sheets made of
    duplicate face pairs, and leaves loose vertices. FBX importers drop those faces (so the counts in the manifest
    did not match what an importer sees) and the sheets render as paper-thin flaps. Delete both faces of every
    duplicate pair, any face with a repeated vertex, and every vertex left without faces. Returns faces removed."""
    bm = bmesh.new()
    bm.from_mesh(me)
    seen = {}
    kill = set()
    for f in bm.faces:
        ids = [v.index for v in f.verts]
        if len(set(ids)) < len(ids):
            kill.add(f)
            continue
        k = frozenset(ids)
        if k in seen:
            kill.add(f)
            kill.add(seen[k])
        else:
            seen[k] = f
    if kill:
        bmesh.ops.delete(bm, geom=list(kill), context='FACES_ONLY')
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context='VERTS')
    wire = [e for e in bm.edges if not e.link_faces]
    if wire:
        bmesh.ops.delete(bm, geom=wire, context='EDGES')
    bm.to_mesh(me)
    bm.free()
    me.update()
    return len(kill)


# --------------------------------------------------------------------------- materials (Blender side)
TEX_DIR = None   # set by the build script


def _img(name, nonc):
    path = os.path.join(TEX_DIR, name + '.png')
    key = name
    if key in bpy.data.images:
        return bpy.data.images[key]
    im = bpy.data.images.load(path)
    im.name = key
    im.colorspace_settings.name = 'Non-Color' if nonc else 'sRGB'
    return im


def get_mat(name):
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
    return m


MAT_TEX = {   # master material -> texture set (tiling, UV0)
    'M_Prop_Rock': 'Rock', 'M_Prop_Stone': 'Stone', 'M_Prop_Wood': 'Wood', 'M_Prop_Straw': 'Straw',
    'M_Prop_Metal': 'Metal', 'M_Prop_Cloth': 'Cloth', 'M_Prop_Foliage': 'Leaf', 'M_Prop_Crop': 'Stalk',
}
MASKED = {'M_Prop_Foliage', 'M_Prop_Crop'}


def setup_material(m, tex, tint=(1, 1, 1), rough_mul=1.0, metal_mul=1.0, emit=None, moss=False,
                   baked=None, masked=False, sss=0.0):
    """Preview node tree that mirrors the intended UE material: D x vertex RGB x tint, ORM, tangent N on UV0;
    optional baked N / AO on UVUnique (rocks), moss blended by vertex alpha x up-facing."""
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bs = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bs.outputs[0], out.inputs['Surface'])
    if emit is not None:
        bs.inputs['Base Color'].default_value = (*emit[:3], 1)
        bs.inputs['Emission Color'].default_value = (*emit[:3], 1)
        bs.inputs['Emission Strength'].default_value = emit[3]
        bs.inputs['Roughness'].default_value = 0.3
        return m
    uv0 = nt.nodes.new('ShaderNodeUVMap')
    uv0.uv_map = 'UVMap'
    D = nt.nodes.new('ShaderNodeTexImage')
    D.image = _img('T_Prop_%s_D' % tex, False)
    ORM = nt.nodes.new('ShaderNodeTexImage')
    ORM.image = _img('T_Prop_%s_ORM' % tex, True)
    NT = nt.nodes.new('ShaderNodeTexImage')
    NT.image = _img('T_Prop_%s_N' % tex, True)
    for n_ in (D, ORM, NT):
        nt.links.new(uv0.outputs[0], n_.inputs[0])
    vc = nt.nodes.new('ShaderNodeVertexColor')
    vc.layer_name = 'Col'
    mul = nt.nodes.new('ShaderNodeMix')
    mul.data_type = 'RGBA'
    mul.blend_type = 'MULTIPLY'
    mul.inputs[0].default_value = 1.0
    nt.links.new(D.outputs['Color'], mul.inputs[6])
    nt.links.new(vc.outputs['Color'], mul.inputs[7])
    tn = nt.nodes.new('ShaderNodeMix')
    tn.data_type = 'RGBA'
    tn.blend_type = 'MULTIPLY'
    tn.inputs[0].default_value = 1.0
    nt.links.new(mul.outputs[2], tn.inputs[6])
    tn.inputs[7].default_value = (*tint, 1)
    base = tn.outputs[2]
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(ORM.outputs['Color'], sep.inputs[0])
    rmul = nt.nodes.new('ShaderNodeMath')
    rmul.operation = 'MULTIPLY'
    rmul.inputs[1].default_value = rough_mul
    nt.links.new(sep.outputs[1], rmul.inputs[0])
    mm = nt.nodes.new('ShaderNodeMath')
    mm.operation = 'MULTIPLY'
    mm.inputs[1].default_value = metal_mul
    nt.links.new(sep.outputs[2], mm.inputs[0])
    nmap = nt.nodes.new('ShaderNodeNormalMap')
    nmap.uv_map = 'UVMap'
    nt.links.new(NT.outputs['Color'], nmap.inputs['Color'])
    normal_out = nmap.outputs[0]
    rough_out = rmul.outputs[0]
    if baked:
        uv1 = nt.nodes.new('ShaderNodeUVMap')
        uv1.uv_map = 'UVUnique'
        BN = nt.nodes.new('ShaderNodeTexImage')
        BN.image = _img(baked + '_N', True)
        BO = nt.nodes.new('ShaderNodeTexImage')
        BO.image = _img(baked + '_ORM', True)
        nt.links.new(uv1.outputs[0], BN.inputs[0])
        nt.links.new(uv1.outputs[0], BO.inputs[0])
        bmap = nt.nodes.new('ShaderNodeNormalMap')
        bmap.uv_map = 'UVUnique'
        nt.links.new(BN.outputs['Color'], bmap.inputs['Color'])
        # detail normal on top of the baked one: bump from the tiling detail colour
        bw = nt.nodes.new('ShaderNodeRGBToBW')
        nt.links.new(D.outputs['Color'], bw.inputs[0])
        bump = nt.nodes.new('ShaderNodeBump')
        bump.inputs['Strength'].default_value = 0.35
        bump.inputs['Distance'].default_value = 0.02
        nt.links.new(bw.outputs[0], bump.inputs['Height'])
        nt.links.new(bmap.outputs[0], bump.inputs['Normal'])
        normal_out = bump.outputs[0]
        bsep = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(BO.outputs['Color'], bsep.inputs[0])
        ao = nt.nodes.new('ShaderNodeMix')
        ao.data_type = 'RGBA'
        ao.blend_type = 'MULTIPLY'
        ao.inputs[0].default_value = 1.0
        nt.links.new(base, ao.inputs[6])
        nt.links.new(bsep.outputs[0], ao.inputs[7])
        base = ao.outputs[2]
    if moss:
        MD = nt.nodes.new('ShaderNodeTexImage')
        MD.image = _img('T_Prop_Moss_D', False)
        sc = nt.nodes.new('ShaderNodeVectorMath')
        sc.operation = 'SCALE'
        sc.inputs['Scale'].default_value = TILE['Rock'] / TILE['Moss']
        nt.links.new(uv0.outputs[0], sc.inputs[0])
        nt.links.new(sc.outputs[0], MD.inputs[0])
        geo = nt.nodes.new('ShaderNodeNewGeometry')
        sz = nt.nodes.new('ShaderNodeSeparateXYZ')
        nt.links.new(geo.outputs['Normal'], sz.inputs[0])
        up = nt.nodes.new('ShaderNodeMapRange')
        up.inputs[1].default_value = 0.35
        up.inputs[2].default_value = 0.8
        nt.links.new(sz.outputs[2], up.inputs[0])
        fac = nt.nodes.new('ShaderNodeMath')
        fac.operation = 'MULTIPLY'
        nt.links.new(up.outputs[0], fac.inputs[0])
        nt.links.new(vc.outputs['Alpha'], fac.inputs[1])
        noi = nt.nodes.new('ShaderNodeTexNoise')
        noi.inputs['Scale'].default_value = 1.2
        nt.links.new(uv0.outputs[0], noi.inputs['Vector'])
        f2 = nt.nodes.new('ShaderNodeMapRange')
        f2.inputs[1].default_value = 0.42
        f2.inputs[2].default_value = 0.6
        nt.links.new(noi.outputs[0], f2.inputs[0])
        fac2 = nt.nodes.new('ShaderNodeMath')
        fac2.operation = 'MULTIPLY'
        nt.links.new(fac.outputs[0], fac2.inputs[0])
        nt.links.new(f2.outputs[0], fac2.inputs[1])
        mx = nt.nodes.new('ShaderNodeMix')
        mx.data_type = 'RGBA'
        nt.links.new(fac2.outputs[0], mx.inputs[0])
        nt.links.new(base, mx.inputs[6])
        nt.links.new(MD.outputs['Color'], mx.inputs[7])
        base = mx.outputs[2]
        rmx = nt.nodes.new('ShaderNodeMix')
        nt.links.new(fac2.outputs[0], rmx.inputs[0])
        nt.links.new(rough_out, rmx.inputs[2])
        rmx.inputs[3].default_value = 0.95
        rough_out = rmx.outputs[0]
    nt.links.new(base, bs.inputs['Base Color'])
    nt.links.new(rough_out, bs.inputs['Roughness'])
    nt.links.new(mm.outputs[0], bs.inputs['Metallic'])
    nt.links.new(normal_out, bs.inputs['Normal'])
    if masked:
        nt.links.new(D.outputs['Alpha'], bs.inputs['Alpha'])
        m.surface_render_method = 'DITHERED'
        m.use_backface_culling = False
        bs.inputs['Subsurface Weight'].default_value = 0.0
        # thin foliage: a little transmission so back-lit cards glow
        tr = bs.inputs.get('Transmission Weight')
        if tr is not None:
            tr.default_value = 0.0
    if sss > 0:
        bs.inputs['Subsurface Weight'].default_value = sss
    return m


# --------------------------------------------------------------------------- export
def export_fbx(path, objs):
    view_layer = bpy.context.view_layer
    for o in list(bpy.context.selected_objects):
        o.select_set(False)
    hidden = [o for o in objs if o.hide_get()]
    for o in hidden:
        o.hide_set(False)
    for o in objs:
        o.select_set(True)
    view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'EMPTY', 'MESH'},
        apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_NONE',
        axis_forward='-Z', axis_up='Y', use_mesh_modifiers=False, mesh_smooth_type='FACE',
        use_tspace=True, colors_type='SRGB', add_leaf_bones=False, bake_anim=False,
        use_custom_props=True, path_mode='STRIP', embed_textures=False)
    for o in objs:
        o.select_set(False)
    for o in hidden:
        o.hide_set(True)


def rng_for(name):
    return random.Random(sum(ord(c) * (i + 7) for i, c in enumerate(name)))
