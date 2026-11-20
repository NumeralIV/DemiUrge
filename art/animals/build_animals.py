# DEMIURGE art/animals/build_animals.py - builds the wildlife family (family "Animal") headless in Blender 5.2.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --python art/animals/build_animals.py -- --asset grazer-wildebeest
#   (--asset all builds every species in one process; run_all.py builds them in parallel through tools/render_slot.py)
#
# Per asset it writes:
#   art/animals/source/<key>.blend            LODs, skinned mesh, armature, actions, materials (textures relative)
#   art/animals/textures/T_Animal_<A><V>_{D,N,ORM}.png   baked from the high-poly source
#   art/animals/export/SM_Animal_<A>_<V>.fbx  static mesh, LOD group (LOD0..), UV0 texture, UV1 anim weights,
#                                             vertex colour = coat colour, shape keys on LOD0 (morph targets)
#   art/animals/export/SK_Animal_<A>_<V>.fbx  skeletal LOD0 + armature + actions (walk / swim / flap cycles)
#   art/animals/source/<key>.json             numbers for manifest.json (merged by run_all.py --manifest)
#
# Pipeline: species_animals.py primitives -> signed distance field (numpy) -> OpenVDB quads (high poly, ~100-300k tris)
# -> symmetric quadric decimation + explicit low tubes (LOD0) -> smart UVs -> Cycles bakes (normal, coat colour,
# roughness, metal, AO) -> LOD1..3 by decimation -> armature, distance-based skin weights, procedural cycles.
import bpy
import bmesh
import json
import math
import os
import sys
import time
import numpy as np
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'art', 'tools'))
import species_animals as SP   # noqa: E402
import dmtex                    # noqa: E402

TEXDIR = os.path.join(HERE, 'textures')
EXPDIR = os.path.join(HERE, 'export')
SRCDIR = os.path.join(HERE, 'source')


def log(*a):
    print('[animals]', *a, flush=True)


# --------------------------------------------------------------------------- mesh helpers

def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def mesh_obj(name, verts, faces):
    me = bpy.data.meshes.new(name)
    faces = faces.tolist() if isinstance(faces, np.ndarray) else [list(map(int, f)) for f in faces]
    me.from_pydata(np.asarray(verts, np.float64).tolist(), [], faces)
    me.validate(clean_customdata=False)
    me.update()
    return link(bpy.data.objects.new(name, me))


def get_co(me):
    co = np.zeros(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get('co', co)
    return co.reshape(-1, 3)


def set_co(me, co):
    me.vertices.foreach_set('co', np.ascontiguousarray(co, np.float32).ravel())
    me.update()


def vnormals(me):
    n = np.zeros(len(me.vertices) * 3, np.float32)
    me.vertex_normals.foreach_get('vector', n)
    return n.reshape(-1, 3)


def tri_count(me):
    return int(sum(len(p.vertices) - 2 for p in me.polygons))


def signed_volume(me):
    n = len(me.polygons)
    c = np.zeros(n * 3, np.float32)
    nr = np.zeros(n * 3, np.float32)
    ar = np.zeros(n, np.float32)
    me.polygons.foreach_get('center', c)
    me.polygons.foreach_get('normal', nr)
    me.polygons.foreach_get('area', ar)
    return float((c.reshape(-1, 3) * nr.reshape(-1, 3)).sum(1) @ ar) / 3.0


def ensure_outward(me):
    if signed_volume(me) < 0:
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
        bm.to_mesh(me)
        bm.free()
        me.update()


def non_manifold_edges(me):
    bm = bmesh.new()
    bm.from_mesh(me)
    n = sum(1 for e in bm.edges if not e.is_manifold)
    bm.free()
    return n


def evaluated_copy(obj, name):
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(obj.evaluated_get(dg))
    me.name = name
    return me


def select_only(*objs, active=None):
    bpy.context.view_layer.update()
    for o in bpy.context.scene.objects:
        if o is not None:
            o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active or objs[0]


# --------------------------------------------------------------------------- high poly from the SDF

def smin(a, b, k):
    if k <= 0:
        return np.minimum(a, b)
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return (b * (1 - h) + a * h - k * h * (1 - h)).astype(np.float32)


def drop_small_parts(pts, quads, keep=0.004):
    """Remove loose shells smaller than `keep` of the largest one (meshing specks between thin primitives)."""
    par = np.arange(len(pts))

    def find(x):
        r = x
        while par[r] != r:
            r = par[r]
        while par[x] != r:
            par[x], x = r, par[x]
        return r
    for q in quads.tolist():
        r0 = find(q[0])
        for v in q[1:]:
            r = find(v)
            if r != r0:
                par[r] = r0
    roots = np.array([find(i) for i in range(len(pts))])
    ids, counts = np.unique(roots, return_counts=True)
    good = set(ids[counts >= keep * counts.max()].tolist())
    qmask = np.array([roots[q[0]] in good for q in quads])
    quads = quads[qmask]
    used = np.unique(quads)
    remap = np.full(len(pts), -1, np.int64)
    remap[used] = np.arange(len(used))
    if len(ids) > 1:
        log('dropped', int((~qmask).sum()), 'quads in', len(ids) - len(good), 'loose parts')
    return pts[used], remap[quads]


def build_high(S):
    import openvdb as vdb
    vs = S.vs
    body = [p for p in S.prims if not p.sep and not p.paint]
    lo = np.min([p.bbox()[0] for p in body], 0) - 6 * vs
    hi = np.max([p.bbox()[1] for p in body], 0) + 6 * vs
    hx = max(-lo[0], hi[0])
    nx = 2 * int(math.ceil(hx / vs)) + 1
    org = np.array([-(nx - 1) / 2 * vs, lo[1], lo[2]], np.float64)
    ny = int(math.ceil((hi[1] - lo[1]) / vs)) + 1
    nz = int(math.ceil((hi[2] - lo[2]) / vs)) + 1
    D = np.full((nx, ny, nz), 1e3, np.float32)
    for p in body:
        plo, phi = p.bbox()
        pad = 2 * p.k + 4 * vs
        i0 = np.clip(np.floor((plo - pad - org) / vs).astype(int), 0, [nx, ny, nz])
        i1 = np.clip(np.ceil((phi + pad - org) / vs).astype(int) + 1, 0, [nx, ny, nz])
        xs = (org[0] + np.arange(i0[0], i1[0]) * vs).astype(np.float32)
        ys = (org[1] + np.arange(i0[1], i1[1]) * vs).astype(np.float32)
        zs = (org[2] + np.arange(i0[2], i1[2]) * vs).astype(np.float32)
        P = np.stack(np.meshgrid(xs, ys, zs, indexing='ij'), -1)
        sl = (slice(i0[0], i1[0]), slice(i0[1], i1[1]), slice(i0[2], i1[2]))
        D[sl] = smin(D[sl], p.sdf(P), p.k)
    band = 3 * vs
    g = vdb.FloatGrid(band)
    g.copyFromArray(np.clip(D, -band, band), ijk=(0, 0, 0), tolerance=0)
    g.transform = vdb.createLinearTransform(voxelSize=vs)
    g.gridClass = vdb.GridClass.LEVEL_SET
    pts, quads = g.convertToQuads(0.0)
    pts = pts.astype(np.float64) + org
    pts, quads = drop_small_parts(pts, quads)
    log('high body', S.key, 'grid', (nx, ny, nz), 'quads', len(quads))
    obj = mesh_obj(S.name + '_HighBody', pts, quads)
    ensure_outward(obj.data)
    return obj


def tube_geom(prim, sides):
    pts = np.array(prim.pts, np.float64)
    r = np.array(prim.radii, np.float64)
    n = len(pts)
    T = np.zeros_like(pts)
    for i in range(n):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, n - 1)]
        t = b - a
        T[i] = t / np.linalg.norm(t)
    ref = np.array([0, 0, 1.0]) if abs(T[0][2]) < 0.9 else np.array([1.0, 0, 0])
    Nn = np.cross(T[0], ref)
    Nn /= np.linalg.norm(Nn)
    verts, faces = [], []
    rings = []
    for i in range(n):
        Nn = Nn - np.dot(Nn, T[i]) * T[i]
        Nn /= np.linalg.norm(Nn)
        B = np.cross(T[i], Nn)
        ring = []
        for k in range(sides):
            a = 2 * math.pi * (k + 0.5) / sides
            ring.append(len(verts))
            verts.append(pts[i] + r[i] * (math.cos(a) * Nn * prim.flat + math.sin(a) * B))
        rings.append(ring)
    for i in range(n - 1):
        for k in range(sides):
            a, b = rings[i][k], rings[i][(k + 1) % sides]
            c, d = rings[i + 1][(k + 1) % sides], rings[i + 1][k]
            faces.append((a, b, c, d))
    base = len(verts)
    verts.append(pts[0] - T[0] * r[0] * 0.5)
    tip = len(verts)
    verts.append(pts[-1] + T[-1] * r[-1])
    for k in range(sides):
        faces.append((rings[0][(k + 1) % sides], rings[0][k], base))
        faces.append((rings[-1][k], rings[-1][(k + 1) % sides], tip))
    return np.array(verts), faces


def tubes_obj(S, name, hi):
    V, F = [], []
    for p in S.prims:
        if p.kind != 'tube':
            continue
        v, f = tube_geom(p, p.sides[0] if hi else p.sides[1])
        o = len(V)
        V.extend(v.tolist())
        F.extend([tuple(i + o for i in face) for face in f])
    if not V:
        return None
    obj = mesh_obj(name, V, F)
    ensure_outward(obj.data)
    return obj


def join(objs, name):
    objs = [o for o in objs if o is not None]
    select_only(*objs, active=objs[0])
    bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    o.name = name
    o.data.name = name
    return o


# --------------------------------------------------------------------------- tags and coat on any vertex set

def prim_dist(S, P):
    return np.stack([p.sdf(P) for p in S.prims], 1)       # (N, nprims)


def tag_weights(S, P, Dm=None):
    Dm = prim_dist(S, P) if Dm is None else Dm
    size = S.size
    s = 0.006 * size
    geo = [i for i, p in enumerate(S.prims) if not p.paint]
    dmin = Dm[:, geo].min(1)
    T = {}
    for i in geo:
        w = np.exp(-np.maximum(Dm[:, i] - dmin, 0) / s)
        t = S.prims[i].tag
        T[t] = np.maximum(T.get(t, 0), w)
    tot = sum(T.values())
    T = {k: (v / tot).astype(np.float32) for k, v in T.items()}
    for i, p in enumerate(S.prims):
        if p.paint:
            w = SP.sst(s, -s, Dm[:, i])
            T[p.tag] = np.maximum(T.get(p.tag, 0), w).astype(np.float32)
    return T


def coat_at(S, P, N):
    T = tag_weights(S, P)
    c, rough, metal = S.coat(P, N, T)
    if 'eye' in T:
        rough = np.where(T['eye'] > 0.5, np.maximum(rough, 0.35), rough)
    return np.clip(c, 0, 1).astype(np.float32), np.clip(rough, 0.02, 1).astype(np.float32), np.clip(metal, 0, 1).astype(np.float32)


def set_point_color(me, name, rgb):
    a = me.color_attributes.get(name) or me.color_attributes.new(name, 'FLOAT_COLOR', 'POINT')
    rgba = np.concatenate([rgb, np.ones((len(rgb), 1), np.float32)], 1)
    a.data.foreach_set('color', rgba.ravel())


# --------------------------------------------------------------------------- decimation and UVs

def decimate_obj(src, target, name, symmetric=True, keep=None):
    """Copy src (object) into a new object decimated to about `target` triangles.
    keep: optional per-vertex weights 0..1 of src; weighted vertices are protected from collapse."""
    cur = tri_count(src.data)
    ratio = min(1.0, target / max(cur, 1))
    best = None
    # decimate at a ~2 m working scale: Blender's collapse tolerances stall on very small meshes (fish, birds)
    work = src.data.copy()
    co = get_co(work)
    f = 2.0 / max(float(np.max(co.max(0) - co.min(0))), 1e-6)
    set_co(work, co * f)
    for it in range(6):
        tmp = link(bpy.data.objects.new('_dec', work))
        m = tmp.modifiers.new('dec', 'DECIMATE')
        m.decimate_type = 'COLLAPSE'
        m.ratio = ratio
        m.use_collapse_triangulate = True
        m.use_symmetry = symmetric
        m.symmetry_axis = 'X'
        if keep is not None:
            vg = tmp.vertex_groups.get('_keep') or tmp.vertex_groups.new(name='_keep')
            for i in np.nonzero(keep > 1e-3)[0]:
                vg.add([int(i)], float(keep[i]), 'REPLACE')
            m.vertex_group = '_keep'
            m.invert_vertex_group = True      # weight 1 = keep
            m.vertex_group_factor = 1.0
        me = evaluated_copy(tmp, name)
        bpy.data.objects.remove(tmp)
        got = tri_count(me)
        if best is not None:
            bpy.data.meshes.remove(best)
        best = me
        if abs(got - target) <= max(4, 0.02 * target):
            break
        if it >= 2 and symmetric and got > 1.1 * target:
            symmetric = False          # symmetric collapse stalls on some thin shapes; finish without it
            log('decimate', name, 'stalled at', got, '- retry without symmetry')
        ratio *= target / max(got, 1)
        ratio = min(ratio, 1.0)
    set_co(best, get_co(best) / f)
    bpy.data.meshes.remove(work)
    clean_degenerate(best, 2.0 / f)
    o = link(bpy.data.objects.new(name, best))
    o.vertex_groups.clear()          # drop the protection weights carried over from the working copy
    return o


def lod_keep_weights(S, obj):
    """Protection variants for the far LODs: the thin extremities that make the silhouette at 300 m - 2 km (legs,
    wings, flippers, fluke) and, for walkers, the head. Without protection the quadric collapse flattens them into
    zero-volume strips that clean_degenerate removes (verification, 2026-11-19: the old LOD2 grazers and deer had no
    legs, the birds no wings, the whale no flippers). Returns a list of per-vertex weight arrays (1 = keep)."""
    P = get_co(obj.data)
    hi = P.max(0)
    if S.kind == 'fish':
        return []
    if S.kind == 'bird':
        tip = (np.abs(P[:, 0]) > 0.7 * hi[0]).astype(np.float32)
        return [tip, tip * 0.5]
    W, names = skin_weights(S, P)

    def w(*pre):
        j = [i for i, n in enumerate(names) if n.startswith(pre)]
        return np.clip(W[:, j].sum(1), 0, 1).astype(np.float32)
    if S.kind == 'whale':
        e = w('fin_', 'fluke', 'tail2')
        return [e, e * 0.5]
    legs = w('F', 'H')
    low = w('FL_lower', 'FR_lower', 'FL_foot', 'FR_foot', 'HL_shin', 'HR_shin', 'HL_foot', 'HR_foot')
    head = w('head')
    return [legs, low, np.clip(low + head, 0, 1), np.clip(legs + head, 0, 1) * 0.6]


def surface_error(a, b):
    """Mean + p95 distance from the vertices of mesh a to the surface of mesh b (metres)."""
    from mathutils.bvhtree import BVHTree
    t = BVHTree.FromPolygons([v.co.copy() for v in b.vertices], [tuple(p.vertices) for p in b.polygons])
    d = np.array([(t.find_nearest(v.co)[3] or 0.0) for v in a.vertices], np.float32)
    return float(d.mean()), float(np.percentile(d, 95))


def make_lod(S, src, target, name, ref):
    """Next LOD from the previous one: try plain and extremity-protected collapse, with and without symmetry, and keep
    the candidate closest to the LOD0 surface (parts lost + parts added, mean and 95th percentile, relative to the
    animal's size) within ~30 % of the triangle target."""
    cands = []
    for kp in [None] + lod_keep_weights(S, src):
        for sym in (True, False):
            o = decimate_obj(src, target, '_cand', sym, kp)
            lost = surface_error(ref.data, o.data)
            extra = surface_error(o.data, ref.data)
            err = (lost[0] + extra[0] + 0.5 * (lost[1] + extra[1])) / S.size
            t = tri_count(o.data)
            fit = abs(t - target) / target
            score = err + (0.0 if fit <= 0.3 else 0.2 * (fit - 0.3))
            cands.append((score, kp is not None, sym, t, o, err))
    cands.sort(key=lambda x: x[0])
    best = cands[0]
    for c in cands[1:]:
        bpy.data.objects.remove(c[4])
    log('lod', name, 'tris', best[3], 'target', target, 'protected', best[1], 'sym', best[2], 'error %.4f' % best[5],
        'of', len(cands))
    o = best[4]
    o.name = name
    o.data.name = name
    return o


def clean_degenerate(me, size):
    """Merge coincident vertices and dissolve zero-area / zero-length triangles left by collapse decimation
    (FBX importers and Unreal drop them, which would make the manifest counts wrong)."""
    bm = bmesh.new()
    bm.from_mesh(me)
    eps = 1e-4 * size
    for _ in range(3):
        bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=eps)
        bmesh.ops.dissolve_degenerate(bm, edges=bm.edges[:], dist=eps)
        bad = [f for f in bm.faces if f.calc_area() < (eps * eps)]
        if bad:
            bmesh.ops.delete(bm, geom=bad, context='FACES')
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
        bad = [f for f in bm.faces if f.calc_area() < (eps * eps)]
        if not bad:
            break
    # back-to-back duplicate triangles ("pillows", zero-volume 2-face shells): importers drop them; remove both
    seen = {}
    for f in bm.faces:
        seen.setdefault(frozenset(v.index for v in f.verts), []).append(f)
    dup = [f for fs in seen.values() if len(fs) > 1 for f in fs]
    if dup:
        bmesh.ops.delete(bm, geom=dup, context='FACES')
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context='VERTS')
    bm.to_mesh(me)
    bm.free()
    # verification fix (2026-11-19): the symmetric collapse leaves duplicate edges along the x = 0 seam (two edges
    # joining the same two vertices, each used by one face), which read as 6-36 open "boundary" edges per LOD;
    # Mesh.validate merges them, so every LOD is closed again (no geometry or triangle change)
    me.validate(verbose=False, clean_customdata=False)
    me.update()


def triangulate(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(obj.data)
    bm.free()


def smart_uv(obj):
    select_only(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(70), island_margin=0.012, area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
    try:
        bpy.ops.uv.pack_islands(rotate=True, margin=0.008)
    except Exception as e:
        log('pack_islands failed', e)
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.data.uv_layers[0].name = 'UVMap'


def uv_report(me, res=256):
    uv = np.zeros(len(me.loops) * 2, np.float32)
    me.uv_layers[0].data.foreach_get('uv', uv)
    uv = uv.reshape(-1, 2)
    inside = bool((uv >= -1e-4).all() and (uv <= 1 + 1e-4).all())
    cov = np.zeros((res, res), np.int32)
    for p in me.polygons:
        ls = list(p.loop_indices)
        for i in range(1, len(ls) - 1):
            t = uv[[ls[0], ls[i], ls[i + 1]]] * res
            x0, y0 = np.floor(t.min(0)).astype(int)
            x1, y1 = np.ceil(t.max(0)).astype(int)
            x0, y0 = max(x0, 0), max(y0, 0)
            x1, y1 = min(x1, res), min(y1, res)
            if x1 <= x0 or y1 <= y0:
                continue
            X, Y = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
            a, b, c = t
            den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(den) < 1e-12:
                continue
            l1 = ((b[1] - c[1]) * (X - c[0]) + (c[0] - b[0]) * (Y - c[1])) / den
            l2 = ((c[1] - a[1]) * (X - c[0]) + (a[0] - c[0]) * (Y - c[1])) / den
            m = (l1 > 0.02) & (l2 > 0.02) & (1 - l1 - l2 > 0.02)
            cov[y0:y1, x0:x1] += m
    used = float((cov > 0).mean())
    overlap = float((cov > 1).sum()) / max(1, (cov > 0).sum())
    return inside, used, overlap


# --------------------------------------------------------------------------- baking

def new_img(name, n):
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    img = bpy.data.images.new(name, n, n, alpha=False, float_buffer=True)
    img.colorspace_settings.name = 'Non-Color'
    return img


def img_array(img):
    n = img.size[0]
    a = np.zeros(n * n * 4, np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(n, n, 4)[..., :3].copy()


def bake_textures(S, high, low):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.use_denoising = False
    sc.render.bake.margin = max(4, S.tex // 64)
    sc.render.bake.margin_type = 'EXTEND'
    if sc.world is None:
        sc.world = bpy.data.worlds.new('BakeWorld')
    sc.world.light_settings.distance = 0.18 * S.size
    thin = S.kind in ('bird', 'fish')
    ext = (0.02 if thin else 0.012) * S.size
    ray = (0.10 if thin else 0.08) * S.size
    # source material: emission of a point colour attribute
    src = bpy.data.materials.new('BakeSrc')
    src.use_nodes = True
    nt = src.node_tree
    nt.nodes.clear()
    at = nt.nodes.new('ShaderNodeAttribute')
    at.attribute_type = 'GEOMETRY'
    em = nt.nodes.new('ShaderNodeEmission')
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    nt.links.new(at.outputs['Color'], em.inputs['Color'])
    nt.links.new(em.outputs['Emission'], out.inputs['Surface'])
    high.data.materials.clear()
    high.data.materials.append(src)
    dst = bpy.data.materials.new('BakeDst')
    dst.use_nodes = True
    tn = dst.node_tree.nodes.new('ShaderNodeTexImage')
    dst.node_tree.nodes.active = tn
    low.data.materials.clear()
    low.data.materials.append(dst)
    for o in (high, low):
        for poly in o.data.polygons:
            poly.use_smooth = True
    select_only(high, low, active=low)
    res = {}
    n = S.tex

    def run(kind, samples, cage=None, dist=None, **kw):
        img = new_img('bake_' + kind, n)
        tn.image = img
        sc.cycles.samples = samples
        t0 = time.time()
        bpy.ops.object.bake(use_selected_to_active=True, cage_extrusion=ext if cage is None else cage,
                            max_ray_distance=ray if dist is None else dist, use_clear=True,
                            margin=sc.render.bake.margin, **kw)
        log('bake', S.key, kind, '%.1fs' % (time.time() - t0))
        return img_array(img)
    for name in ('coat', 'rough', 'metal'):
        at.attribute_name = name
        res[name] = run(name, 4, type='EMIT')
    res['normal'] = run('normal', 4, type='NORMAL', normal_space='TANGENT')
    # AO: the low shell must not shadow the high surface it is matched against
    low.visible_diffuse = low.visible_shadow = low.visible_glossy = low.visible_transmission = low.visible_volume_scatter = False
    res['ao'] = run('ao', 48, type='AO')
    low.visible_diffuse = low.visible_shadow = low.visible_glossy = low.visible_transmission = low.visible_volume_scatter = True
    return res


def write_textures(S, maps):
    os.makedirs(TEXDIR, exist_ok=True)
    base = 'T_Animal_%s%s' % (S.asset, S.variant)
    ao = np.clip(maps['ao'][..., 0], 0, 1)
    ao = 0.35 + 0.65 * ao                       # keep crevices from going black at game distance
    d = np.clip(maps['coat'], 0, 1)
    orm = np.stack([ao, np.clip(maps['rough'][..., 0], 0.02, 1), np.clip(maps['metal'][..., 0], 0, 1)], -1)
    paths = {
        'D': os.path.join(TEXDIR, base + '_D.png'),
        'N': os.path.join(TEXDIR, base + '_N.png'),
        'ORM': os.path.join(TEXDIR, base + '_ORM.png'),
    }
    dmtex.save_png(paths['D'], d, srgb=True)
    # verification fix (2026-11-19): the decimated bird wings are thin flat plates over a rounded high-poly wing, so
    # 8 % of the starling's and 3 % of the egret's normal texels tilted past 70 deg and rendered as dark facets on the
    # wings (a shorter bake ray did not help). Birds keep a third of the baked tilt (feather breakup, no facets), and
    # every map is limited to ~70 deg of tilt so a stray texel cannot go black.
    nv = np.clip(maps['normal'], 0, 1) * 2 - 1
    # a normal facing into the surface is a bake error (the ray hit the far side): use the flat normal there
    nv = np.where((nv[..., 2] < 0.0)[..., None], np.array([0.0, 0.0, 1.0], np.float32), nv)
    if S.kind == 'bird':
        nv[..., :2] *= 0.33
        nv /= np.maximum(np.linalg.norm(nv, axis=-1, keepdims=True), 1e-6)
    zmin = 0.35
    low_z = nv[..., 2] < zmin
    if low_z.any():
        xy = nv[..., :2] * (np.sqrt(1 - zmin * zmin) / np.maximum(np.linalg.norm(nv[..., :2], axis=-1, keepdims=True), 1e-6))
        nv = np.where(low_z[..., None], np.concatenate([xy, np.full(xy.shape[:-1] + (1,), zmin, np.float32)], -1), nv)
    dmtex.save_png(paths['N'], np.clip(nv * 0.5 + 0.5, 0, 1), srgb=False)
    dmtex.save_png(paths['ORM'], orm, srgb=False)
    return paths, d


def sample_tex(img, uv):
    n = img.shape[0]
    x = np.clip(uv[:, 0] * n - 0.5, 0, n - 1.001)
    y = np.clip(uv[:, 1] * n - 0.5, 0, n - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    a = img[y0, x0] * (1 - fx) + img[y0, x0 + 1] * fx
    b = img[y0 + 1, x0] * (1 - fx) + img[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy


def final_material(S, paths):
    mat = bpy.data.materials.new('M_Animal_%s%s' % (S.asset, S.variant))
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bs = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bs.outputs['BSDF'], out.inputs['Surface'])

    def tex(path, srgb):
        img = bpy.data.images.load(path, check_existing=True)
        img.colorspace_settings.name = 'sRGB' if srgb else 'Non-Color'
        n = nt.nodes.new('ShaderNodeTexImage')
        n.image = img
        return n
    d = tex(paths['D'], True)
    nt.links.new(d.outputs['Color'], bs.inputs['Base Color'])
    orm = tex(paths['ORM'], False)
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(orm.outputs['Color'], sep.inputs['Color'])
    nt.links.new(sep.outputs['Green'], bs.inputs['Roughness'])
    nt.links.new(sep.outputs['Blue'], bs.inputs['Metallic'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nn = tex(paths['N'], False)
    nt.links.new(nn.outputs['Color'], nm.inputs['Color'])
    nt.links.new(nm.outputs['Normal'], bs.inputs['Normal'])
    if S.sheen > 0:
        # fur: a little sheen tinted by the coat (white sheen reads chalky from the god camera), low specular
        bs.inputs['Sheen Weight'].default_value = S.sheen
        bs.inputs['Sheen Roughness'].default_value = 0.6
        nt.links.new(d.outputs['Color'], bs.inputs['Sheen Tint'])
        bs.inputs['Specular IOR Level'].default_value = 0.3
    # bake the AO into the base colour multiplier slightly (Blender preview only; Unreal reads ORM.R)
    mul = nt.nodes.new('ShaderNodeMix')
    mul.data_type = 'RGBA'
    mul.blend_type = 'MULTIPLY'
    mul.inputs['Factor'].default_value = 0.6
    nt.links.new(d.outputs['Color'], mul.inputs[6])
    nt.links.new(sep.outputs['Red'], mul.inputs[7])
    nt.links.new(mul.outputs[2], bs.inputs['Base Color'])
    return mat


# --------------------------------------------------------------------------- per-vertex data for the game material

def anim_weight(S, P, W, bone_names):
    """UV1.x as the in-game critter material expects (DmNatureCritters.h)."""
    def wsum(*names):
        s = np.zeros(len(P), np.float32)
        for n in names:
            if n in bone_names:
                s += W[:, bone_names.index(n)]
        return s
    if S.kind in ('quad', 'cat'):
        return np.clip(wsum('neck') * 0.45 + wsum('head'), 0, 1)
    if S.kind == 'bird':
        half = max(abs(b[2][0]) for b in S.bones)
        return np.clip(wsum('wing_L', 'wing_R', 'hand_L', 'hand_R') * np.abs(P[:, 0]) / half, 0, 1)
    ymin, ymax = P[:, 1].min(), P[:, 1].max()
    L = ymax - ymin
    return (np.clip((P[:, 1] - ymin - 0.35 * L) / (0.65 * L), 0, 1) ** 1.5).astype(np.float32)


def skin_weights(S, P):
    """Per vertex, per bone weights from the distance to each bone's primitives (top 3, normalised)."""
    names = [b[0] for b in S.bones]
    Dm = prim_dist(S, P)
    big = np.full((len(P), len(names)), 1e3, np.float32)
    for i, p in enumerate(S.prims):
        if p.paint:
            continue
        j = names.index(p.bone)
        big[:, j] = np.minimum(big[:, j], Dm[:, i])
    dmin = big.min(1, keepdims=True)
    sig = 0.012 * S.size
    W = np.exp(-np.maximum(big - dmin, 0) / sig)
    W[big > 9e2] = 0
    order = np.argsort(-W, 1)
    keep = np.zeros_like(W, bool)
    rows = np.arange(len(P))[:, None]
    keep[rows, order[:, :3]] = True
    W = np.where(keep & (W > 0.02), W, 0)
    W /= W.sum(1, keepdims=True)
    return W.astype(np.float32), names


def set_corner_color(me, rgb_per_loop, name='Col'):
    a = me.color_attributes.new(name, 'BYTE_COLOR', 'CORNER')
    rgba = np.concatenate([np.clip(rgb_per_loop, 0, 1), np.ones((len(rgb_per_loop), 1), np.float32)], 1)
    a.data.foreach_set('color', rgba.astype(np.float32).ravel())
    me.color_attributes.active_color = a
    me.color_attributes.render_color_index = me.color_attributes.find(name)


def loop_vidx(me):
    vi = np.zeros(len(me.loops), np.int32)
    me.loops.foreach_get('vertex_index', vi)
    return vi


def loop_uv(me, layer=0):
    uv = np.zeros(len(me.loops) * 2, np.float32)
    me.uv_layers[layer].data.foreach_get('uv', uv)
    return uv.reshape(-1, 2)


def add_game_data(S, obj, d_lin):
    """Vertex colour (coat colour sampled from D) and UV1 'AnimWeights'."""
    me = obj.data
    P = get_co(me).astype(np.float32)
    W, names = skin_weights(S, P)
    aw = anim_weight(S, P, W, names)
    vi = loop_vidx(me)
    uv = loop_uv(me)
    col = sample_tex(d_lin, uv)
    set_corner_color(me, col)
    uvl = me.uv_layers.new(name='AnimWeights')
    a = np.zeros((len(vi), 2), np.float32)
    a[:, 0] = aw[vi]
    uvl.data.foreach_set('uv', a.ravel())
    me.uv_layers.active_index = 0
    for l in me.uv_layers:
        l.active_render = (l.name == 'UVMap')
    return W, names


# --------------------------------------------------------------------------- armature and animation

def build_armature(S):
    arm = bpy.data.armatures.new('Armature')
    ao = link(bpy.data.objects.new('Armature', arm))
    select_only(ao)
    bpy.ops.object.mode_set(mode='EDIT')
    eb = {}
    for name, h, t, parent in S.bones:
        b = arm.edit_bones.new(name)
        b.head = Vector(h.tolist())
        b.tail = Vector(t.tolist())
        d = (b.tail - b.head).normalized()
        ref = Vector((0, -1, 0)) if abs(d.z) > 0.7 else Vector((0, 0, 1))
        b.align_roll(ref)
        b.use_deform = True
        eb[name] = b
    for name, h, t, parent in S.bones:
        if parent:
            eb[name].parent = eb[parent]
            eb[name].use_connect = False
    bpy.ops.object.mode_set(mode='OBJECT')
    return ao


def skin(S, ao, obj, W, names):
    for j, n in enumerate(names):
        vg = obj.vertex_groups.new(name=n)
        idx = np.nonzero(W[:, j] > 0)[0]
        for i in idx:
            vg.add([int(i)], float(W[i, j]), 'REPLACE')
    m = obj.modifiers.new('Armature', 'ARMATURE')
    m.object = ao
    obj.parent = ao


class Poser:
    def __init__(self, ao):
        self.ao = ao
        self.rest = {b.name: b.matrix_local.to_3x3() for b in ao.data.bones}
        for pb in ao.pose.bones:
            pb.rotation_mode = 'QUATERNION'

    def clear(self):
        for pb in self.ao.pose.bones:
            pb.rotation_quaternion = Quaternion()
            pb.location = Vector()

    def rot(self, bone, axis, deg):
        if bone not in self.rest:
            return
        R = self.rest[bone]
        qw = Quaternion(Vector(axis), math.radians(deg)).to_matrix()
        ql = (R.inverted() @ qw @ R).to_quaternion()
        pb = self.ao.pose.bones[bone]
        pb.rotation_quaternion = pb.rotation_quaternion @ ql

    def move(self, bone, world_off):
        R = self.rest[bone]
        self.ao.pose.bones[bone].location = R.inverted() @ Vector(world_off)


X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)


def pose_at(S, po, mode, t, prm):
    """Set the pose for cycle position t in [0, 1)."""
    po.clear()
    w = 2 * math.pi * t
    g = S.size
    if mode == 'walk':
        sw, kn = prm['swing'], prm['knee']
        for leg, ph in (('HL', 0.0), ('FL', 0.25), ('HR', 0.5), ('FR', 0.75)):
            a = 2 * math.pi * (t + ph)
            up = sw * math.cos(a)
            flex = kn * max(0.0, math.sin(a)) ** 1.3
            sd = leg[1]
            if leg[0] == 'F':
                po.rot('F%s_upper' % sd, X, up * 0.65)
                po.rot('F%s_lower' % sd, X, up * 0.35 - flex * 0.35)
                po.rot('F%s_foot' % sd, X, flex * (0.9 if S.kind == 'quad' else 0.7))
            else:
                po.rot('H%s_thigh' % sd, X, up * 0.75)
                po.rot('H%s_shin' % sd, X, flex * 0.45)
                po.rot('H%s_foot' % sd, X, -flex * 0.8)
        po.move('root', (0, 0, -0.012 * g * (0.5 + 0.5 * math.cos(2 * w))))
        po.rot('chest', Z, 2.0 * math.sin(w))
        po.rot('pelvis', Z, -2.0 * math.sin(w))
        po.rot('neck', X, 3.0 * math.sin(2 * w + 0.6))
        po.rot('tail', Z, 7.0 * math.sin(w))
        po.rot('tail2', Z, 10.0 * math.sin(w - 0.7))
    elif mode == 'graze' or mode == 'graze_pose':
        gc, gn, gh = S.graze
        still = mode == 'graze_pose'
        po.rot('chest', X, gc)
        po.rot('neck', X, gn + (0 if still else 3 * math.sin(w * 2)))
        po.rot('head', X, gh + (0 if still else 5 * math.sin(w * 4)))
        for sd in 'LR':
            po.rot('F%s_upper' % sd, X, -5)
        po.rot('tail', Z, 0 if mode == 'graze_pose' else 9 * math.sin(w * 3))
    elif mode == 'idle':
        po.rot('chest', X, 1.2 * math.sin(w * 2))
        po.rot('neck', Z, 10 * math.sin(w))
        po.rot('head', X, 3 * math.sin(w + 1))
        po.rot('tail', Z, 12 * math.sin(w * 2))
        po.rot('tail2', Z, 20 * math.sin(w * 2 - 0.8))
    elif mode in ('flap', 'wing_up', 'wing_down', 'glide'):
        if mode == 'flap':
            a = prm['amp'] * math.cos(w) + 6
            b = prm['amp'] * 0.65 * math.cos(w - 0.7) + 4
            po.move('root', (0, 0, 0.03 * g * math.sin(w)))
        elif mode == 'glide':
            a = 6 + 1.5 * math.sin(w)
            b = 3 + 1.0 * math.sin(w - 0.5)
        elif mode == 'wing_up':
            a, b = 42, 30
        else:
            a, b = -34, -18
        po.rot('wing_L', Y, -a)
        po.rot('hand_L', Y, -b)
        po.rot('wing_R', Y, a)
        po.rot('hand_R', Y, b)
        po.rot('tail', X, 4 * math.sin(w))
    elif mode in ('swim', 'tail_left', 'tail_right'):
        if mode == 'swim':
            A = prm['amp']
            po.rot('head', Z, -A * 0.25 * math.sin(w + 0.5))
            po.rot('tail0', Z, A * 0.6 * math.sin(w))
            po.rot('tail1', Z, A * 1.1 * math.sin(w - 0.9))
        else:
            s = 1 if mode == 'tail_left' else -1
            po.rot('tail0', Z, 14 * s)
            po.rot('tail1', Z, 24 * s)
    elif mode in ('whale_swim', 'fluke_up', 'fluke_down'):
        if mode == 'whale_swim':
            po.rot('head', X, 1.5 * math.sin(w + 1.0))
            po.rot('tail0', X, 3.0 * math.sin(w))
            po.rot('tail1', X, 6.0 * math.sin(w - 0.7))
            po.rot('tail2', X, 9.0 * math.sin(w - 1.4))
            po.rot('fluke', X, 14.0 * math.sin(w - 2.1))
            po.rot('fin_L', Y, -4 * math.sin(w))
            po.rot('fin_R', Y, 4 * math.sin(w))
            po.move('root', (0, 0, 0.08 * math.sin(w - 1.0)))
        else:
            s = 1 if mode == 'fluke_up' else -1
            po.rot('tail1', X, 6 * s)
            po.rot('tail2', X, 10 * s)
            po.rot('fluke', X, 16 * s)


def make_actions(S, ao):
    po = Poser(ao)
    ao.animation_data_create()
    acts = []
    for name, frames, mode, prm in S.anims:
        act = bpy.data.actions.new('AS_Animal_%s_%s_%s' % (S.asset, S.variant, name))
        act.use_fake_user = True
        ao.animation_data.action = act
        for f in range(frames + 1):
            pose_at(S, po, mode, (f % frames) / frames, prm)
            for pb in ao.pose.bones:
                pb.keyframe_insert('rotation_quaternion', frame=f)
                pb.keyframe_insert('location', frame=f)
        act.frame_range = (0, frames)
        acts.append((act.name, frames))
    po.clear()
    ao.animation_data.action = bpy.data.actions[acts[0][0]] if acts else None
    return acts


def pose_shapes(S, ao, targets):
    """Shape keys from named poses. targets: {mesh object: skinned object with the same topology}."""
    if not S.shape_keys:
        return []
    po = Poser(ao)
    ao.animation_data.action = None
    for tgt in targets:
        tgt.shape_key_add(name='Basis', from_mix=False)
    out = []
    for kname, mode in S.shape_keys:
        po.clear()
        pose_at(S, po, mode, 0.0, {})
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        cache = {}
        for skinned in set(targets.values()):
            ev = skinned.evaluated_get(dg)
            m2 = ev.to_mesh()
            cache[skinned.name] = get_co(m2)
            ev.to_mesh_clear()
        for tgt, skinned in targets.items():
            kb = tgt.shape_key_add(name=kname, from_mix=False)
            kb.data.foreach_set('co', cache[skinned.name].ravel())
            kb.value = 0.0
        out.append(kname)
    po.clear()
    bpy.context.view_layer.update()
    return out


# --------------------------------------------------------------------------- export

def export_fbx(path, objs, anim, active=None):
    select_only(*objs, active=active or objs[0])
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, check_existing=False,
        object_types={'EMPTY', 'MESH', 'ARMATURE'},
        apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL', global_scale=1.0,
        axis_forward='-Z', axis_up='Y', bake_space_transform=False,
        use_mesh_modifiers=True, mesh_smooth_type='FACE', use_tspace=True, use_triangles=True,
        colors_type='SRGB', prioritize_active_color=True,
        add_leaf_bones=False, primary_bone_axis='Y', secondary_bone_axis='X', armature_nodetype='NULL',
        use_armature_deform_only=False,
        bake_anim=anim, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=anim,
        bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
        path_mode='STRIP', embed_textures=False)


def with_slot_name(mat, fn):
    old = mat.name
    mat.name = 'Structure'
    try:
        fn()
    finally:
        mat.name = old


# --------------------------------------------------------------------------- one asset

def build(S):
    t0 = time.time()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    for d in (TEXDIR, EXPDIR, SRCDIR):
        os.makedirs(d, exist_ok=True)
    allb = [p.bbox() for p in S.prims if not p.paint]
    lo = np.min([b[0] for b in allb], 0)
    hi = np.max([b[1] for b in allb], 0)
    S.size = float(np.max(hi - lo))
    # high poly: SDF body + fine tubes, surface noise, coat attributes
    hb = build_high(S)
    me = hb.data
    lowsrc = link(bpy.data.objects.new('_lowsrc', me.copy()))   # undisplaced for decimation
    P = get_co(me)
    Nn = vnormals(me)
    am, af, fa, ff = S.noise
    disp = am * SP.fbm(P, af, 3, 91) + fa * SP.vnoise(P, ff, 93) + fa * 0.5 * SP.vnoise(P, ff * 2.3, 95)
    if S.shag:
        Tb = tag_weights(S, P)
        for tag, (amp, fq) in S.shag.items():
            if tag in Tb:
                # shaggy hair: clumped, mostly outward displacement
                clump = np.abs(SP.fbm(P * SP.V(1.0, 1.0, 0.55), fq, 3, 97)) * 2.0
                disp = disp + Tb[tag] * amp * clump
    set_co(me, P + Nn * disp[:, None])
    th = tubes_obj(S, '_tubes_hi', True)
    high = join([hb, th], S.name + '_High')
    hme = high.data
    P = get_co(hme)
    Nn = vnormals(hme)
    c, r, m = coat_at(S, P, Nn)
    set_point_color(hme, 'coat', c)
    set_point_color(hme, 'rough', np.repeat(r[:, None], 3, 1))
    set_point_color(hme, 'metal', np.repeat(m[:, None], 3, 1))
    log('high', S.key, 'tris', tri_count(hme), '%.1fs' % (time.time() - t0))
    # LOD0: decimated body + low tubes
    tl = tubes_obj(S, '_tubes_lo', False)
    tube_tris = tri_count(tl.data) if tl else 0
    if tl:
        triangulate(tl)
    body_lo = decimate_obj(lowsrc, S.lod0 - tube_tris, '_body_lo')
    bpy.data.objects.remove(lowsrc)
    lod0 = join([body_lo, tl], S.name + '_LOD0')
    clean_degenerate(lod0.data, S.size)
    ensure_outward(lod0.data)
    smart_uv(lod0)
    for poly in lod0.data.polygons:
        poly.use_smooth = True
    log('lod0', S.key, tri_count(lod0.data), 'tubes', tube_tris)
    # bakes
    maps = bake_textures(S, high, lod0)
    paths, d_lin = write_textures(S, maps)
    bpy.data.objects.remove(high)
    mat = final_material(S, paths)
    lod0.data.materials.clear()
    lod0.data.materials.append(mat)
    # LOD1.. each by decimation of the previous LOD (UVs carried), extremities protected (make_lod)
    lods = [lod0]
    n0 = tri_count(lod0.data)
    for i, ratio in enumerate(S.lods[1:], 1):
        o = make_lod(S, lods[-1], max(12, int(round(n0 * ratio))), S.name + '_LOD%d' % i, lod0)
        o.data.materials.clear()
        o.data.materials.append(mat)
        for poly in o.data.polygons:
            poly.use_smooth = True
        lods.append(o)
    # game data on every LOD
    weights = {}
    for o in lods:
        weights[o.name] = add_game_data(S, o, d_lin)
    # skeleton, skinned LOD0 copy, actions
    ao = build_armature(S)
    skn = S.name.replace('SM_', 'SK_')
    sk = link(bpy.data.objects.new(skn, lod0.data.copy()))
    sk.data.name = skn
    W, names = weights[lod0.name]
    skin(S, ao, sk, W, names)
    acts = make_actions(S, ao)
    # shape keys: posed copies -> LOD0 (and the skinned mesh); birds get them on every LOD
    targets = {sk: sk, lod0: sk}
    helpers = []
    if S.kind == 'bird':
        for o in lods[1:]:
            h = link(bpy.data.objects.new('_skin_' + o.name, o.data.copy()))
            Wl, nl = weights[o.name]
            skin(S, ao, h, Wl, nl)
            helpers.append(h)
            targets[o] = h
    keys = pose_shapes(S, ao, targets)
    for h in helpers:
        bpy.data.objects.remove(h)
    ao.animation_data.action = bpy.data.actions[acts[0][0]] if acts else None
    # LOD group empty
    grp = link(bpy.data.objects.new(S.name, None))
    grp['fbx_type'] = 'LodGroup'
    for o in lods:
        o.parent = grp
    # checks
    me0 = lod0.data
    co0 = get_co(me0)
    inside, used, overlap = uv_report(me0)
    checks = {
        'non_manifold_edges_lod0': non_manifold_edges(me0),
        'signed_volume_lod0_m3': round(signed_volume(me0), 5),
        'min_z': round(float(co0[:, 2].min()), 4),
        'uv_inside_0_1': inside, 'uv_coverage': round(used, 3), 'uv_overlap_fraction': round(overlap, 4),
    }
    dims = co0.max(0) - co0.min(0)
    # exports
    smp = os.path.join(EXPDIR, S.name + '.fbx')
    skp = os.path.join(EXPDIR, skn + '.fbx')
    with_slot_name(mat, lambda: export_fbx(smp, [grp] + lods, False, active=grp))
    with_slot_name(mat, lambda: export_fbx(skp, [ao, sk], True, active=ao))
    # blend source (textures relative)
    blend = os.path.join(SRCDIR, S.key + '.blend')
    for o in list(bpy.data.objects):
        if o.name.startswith('_'):
            bpy.data.objects.remove(o)
    bpy.ops.wm.save_as_mainfile(filepath=blend, relative_remap=True, compress=True)
    bpy.ops.file.make_paths_relative()
    bpy.ops.wm.save_mainfile(compress=True)
    info = {
        'mesh': S.name, 'skeletal_mesh': skn, 'key': S.key, 'asset': S.asset, 'variant': S.variant, 'kind': S.kind,
        'footprint_m': [round(float(dims[0]), 3), round(float(dims[1]), 3)], 'height_m': round(float(dims[2]), 3),
        'size_m': [round(float(v), 3) for v in dims],
        'triangles_per_lod': [tri_count(o.data) for o in lods],
        'lod_screen_sizes': [1.0, 0.35, 0.12, 0.04][:len(lods)],
        'materials': {'Structure': 'MI_Animal_%s%s' % (S.asset, S.variant)},
        'textures': {k: os.path.relpath(v, ROOT).replace('\\', '/') for k, v in paths.items()},
        'texture_size': S.tex,
        'bones': [b[0] for b in S.bones],
        'animations': [{'name': a, 'frames': f, 'fps': 24, 'loop': True} for a, f in acts],
        'shape_keys': keys,
        'biome_variants': {k: {'tint': v[0], 'roughness_offset': v[1]} for k, v in S.biomes.items()},
        'flock_scale': S.flock_scale,
        'notes': S.notes,
        'checks': checks,
        'exports': {'static': os.path.relpath(smp, ROOT).replace('\\', '/'), 'skeletal': os.path.relpath(skp, ROOT).replace('\\', '/')},
        'source': os.path.relpath(blend, ROOT).replace('\\', '/'),
        'build_seconds': round(time.time() - t0, 1),
    }
    with open(os.path.join(SRCDIR, S.key + '.json'), 'w') as f:
        json.dump(info, f, indent=1)
    log('done', S.key, info['triangles_per_lod'], checks, '%.1fs' % (time.time() - t0))
    return info


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    keys = []
    i = 0
    while i < len(argv):
        if argv[i] == '--asset':
            keys += argv[i + 1].split(',')
            i += 2
        else:
            i += 1
    specs = SP.all_specs()
    if not keys or keys == ['all']:
        keys = [s.key for s in specs]
    for k in keys:
        build(SP.get(k))


if __name__ == '__main__':
    main()
