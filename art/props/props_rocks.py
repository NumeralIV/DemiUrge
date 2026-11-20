# DEMIURGE props - rocks, boulders and cliff chunks: sculpted high-poly -> decimated game mesh -> Cycles bake.
#
# High-poly (100k-330k triangles, never exported): a super-ellipsoid icosphere, cut by random fracture planes
# (soft flattening gives the planar facets of split rock), then displaced along its normals by fbm, ridged
# fissures and, for the cliffs, strata ledges and vertical joints. LOD0 is a collapse-decimation of it with a
# unique UV ('UVUnique'); its tangent normal map and ambient occlusion (with a ground plane, so the base
# darkens) are baked from the high-poly into T_Prop_<Asset>_N / _ORM. LOD1-3 decimate LOD0 and share the bake.
import math
import os
import random

import bmesh
import bpy
from mathutils import Vector, noise

import props_lib as L


def _sp(s, k):
    """Smooth max(0, s) with knee k."""
    return 0.5 * (s + math.sqrt(s * s + k * k)) - 0.5 * k


ROCKS = {
    # name: size (x, y, z) m, squareness e (1 = ellipsoid, 0.3 = box), cuts, cut depth, knee, fbm amp, fissure amp,
    # strata (layer m, amp), joints amp, sink fraction, high subdiv, LOD0 target tris, moss amount
    'Rock_A': dict(size=(3.0, 2.5, 1.9), e=0.95, cuts=4, depth=(0.62, 0.85), knee=0.18, amp=0.045, fis=0.02,
                   strata=None, joints=0.0, sink=0.18, sub=7, tris=900, moss=0.8, kind='Boulder, rounded granite'),
    'Rock_B': dict(size=(2.2, 1.7, 1.5), e=0.75, cuts=8, depth=(0.5, 0.78), knee=0.05, amp=0.03, fis=0.025,
                   strata=None, joints=0.0, sink=0.15, sub=7, tris=850, moss=0.5, kind='Boulder, angular fractured'),
    'Rock_C': dict(size=(2.8, 1.9, 0.75), e=0.55, cuts=6, depth=(0.6, 0.85), knee=0.06, amp=0.02, fis=0.02,
                   strata=(0.16, 0.03), joints=0.0, sink=0.25, sub=7, tris=700, moss=0.9, kind='Slab, layered'),
    'Rock_D': dict(size=(1.2, 1.0, 2.9), e=0.65, cuts=6, depth=(0.55, 0.85), knee=0.06, amp=0.03, fis=0.03,
                   strata=None, joints=0.0, sink=0.1, sub=7, tris=800, moss=0.3, kind='Standing rock, tall'),
    'Rock_E': dict(size=(0.9, 0.7, 0.55), e=0.8, cuts=5, depth=(0.55, 0.8), knee=0.04, amp=0.035, fis=0.015,
                   strata=None, joints=0.0, sink=0.2, sub=6, tris=320, moss=0.5, kind='Small rock'),
    'Rock_F': dict(size=(2.5, 2.1, 1.7), e=0.9, cuts=3, depth=(0.65, 0.85), knee=0.15, amp=0.04, fis=0.02,
                   strata=None, joints=0.0, sink=0.15, sub=7, tris=950, moss=0.7, split=0.16,
                   kind='Split boulder (two halves)'),
    'Rock_G': dict(size=(3.4, 2.6, 1.6), e=0.8, cuts=5, depth=(0.55, 0.8), knee=0.08, amp=0.035, fis=0.02,
                   strata=None, joints=0.0, sink=0.15, sub=6, tris=980, moss=0.6, cluster=3,
                   kind='Rock cluster (three stones)'),
    'Rock_H': dict(size=(2.6, 2.2, 0.8), e=0.7, cuts=5, depth=(0.5, 0.8), knee=0.04, amp=0.03, fis=0.01,
                   strata=None, joints=0.0, sink=0.1, sub=5, tris=1000, moss=0.2, scree=14,
                   kind='Scree pile (fourteen stones)'),
    'Cliff_A': dict(size=(16.0, 7.0, 11.0), e=0.32, cuts=12, depth=(0.6, 0.9), knee=0.08, amp=0.018, fis=0.03,
                    strata=(0.9, 0.03), joints=0.02, sink=0.16, sub=6, tris=2000, moss=0.6, layout='ledges',
                    kind='Cliff chunk, stratified wall'),
    'Cliff_B': dict(size=(10.0, 8.0, 15.0), e=0.38, cuts=12, depth=(0.6, 0.9), knee=0.08, amp=0.02, fis=0.03,
                    strata=None, joints=0.02, sink=0.12, sub=6, tris=2000, moss=0.4, layout='columns',
                    kind='Cliff chunk, tall buttress'),
    'Cliff_C': dict(size=(22.0, 9.0, 7.0), e=0.35, cuts=14, depth=(0.6, 0.9), knee=0.08, amp=0.016, fis=0.025,
                    strata=(0.7, 0.03), joints=0.02, sink=0.2, sub=6, tris=2000, moss=0.8, layout='ledges',
                    kind='Cliff chunk, long ledge'),
    'Cliff_D': dict(size=(14.0, 11.0, 10.0), e=0.3, cuts=10, depth=(0.6, 0.88), knee=0.06, amp=0.015, fis=0.035,
                    strata=None, joints=0.02, sink=0.15, sub=6, tris=2000, moss=0.5, layout='blocks',
                    kind='Cliff chunk, jointed block'),
}


def _shell(spec, rng, size, sub, extra_cut=None):
    """One closed high-poly rock shell as a bmesh (vertices in metres, centred, not yet sunk)."""
    sx, sy, sz = size
    e = spec['e']
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=1.0)
    seed = Vector((rng.uniform(-90, 90), rng.uniform(-90, 90), rng.uniform(-90, 90)))
    cuts = []
    for _ in range(spec['cuts']):
        # fracture planes: mostly sideways, a few from above; the bottom is left to the sink
        z = rng.uniform(-0.25, 0.9)
        a = rng.uniform(0, 2 * math.pi)
        n = Vector((math.cos(a), math.sin(a), z)).normalized()
        cuts.append((n, rng.uniform(*spec['depth'])))
    for v in bm.verts:
        d = v.co.normalized()
        q = Vector((math.copysign(abs(d.x) ** e, d.x) * sx / 2, math.copysign(abs(d.y) ** e, d.y) * sy / 2,
                    math.copysign(abs(d.z) ** e, d.z) * sz / 2))
        # wobble the base shape a little so no two shells are the same ellipsoid
        q *= 1.0 + 0.12 * noise.noise(d * 1.3 + seed)
        for n, dep in cuts:
            ext = abs(n.x) * sx / 2 + abs(n.y) * sy / 2 + abs(n.z) * sz / 2
            s = q.dot(n) - dep * ext
            q -= n * _sp(s, spec['knee'] * ext)
        if extra_cut is not None:
            n, off = extra_cut
            s = q.dot(n) - off
            q -= n * _sp(s, 0.01)
        v.co = q
    bm.normal_update()
    ext = max(size)
    sc = 1.0 / ext
    lay = spec.get('strata')
    # verifier fix: fissures follow one joint direction per stone (noise squashed along a random axis), so they
    # run as long cracks instead of closed cells that read as inked outlines ("giraffe" pattern)
    ja = Vector((math.sin(seed.x), math.cos(seed.y), 0.6 * math.sin(seed.z))).normalized()   # no extra rng draws
    for v in bm.verts:
        q = v.co
        p = q * sc * 3.0 + seed
        disp = spec['amp'] * ext * noise.fractal(p, 0.55, 2.0, 6)
        # fissures: sharp grooves where a 3D noise crosses zero
        pj = p - ja * (p.dot(ja) * 0.7)
        f = noise.noise(pj * 1.7 + Vector((3.1, 1.7, 9.2)))
        disp -= spec['fis'] * ext * max(0.0, 1.0 - abs(f) * 9.0) ** 2
        # fine grain
        disp += 0.004 * ext * noise.noise(p * 14.0)
        if lay:
            lh, la = lay
            w = (q.z + 0.35 * lh * noise.noise(Vector((q.x, q.y, 0)) * 0.15 + seed)) / lh
            fr = w - math.floor(w)
            # ledge: lip at the top of each layer, undercut at the bottom
            disp += la * (0.6 * (1 - fr) ** 3 - 0.9 * max(0.0, fr - 0.82) / 0.18)
        if spec['joints'] > 0:
            j = noise.noise(Vector((q.x * 0.22, q.y * 0.22, q.z * 0.03)) + seed)
            disp -= spec['joints'] * ext * max(0.0, 1.0 - abs(j) * 6.0) ** 3
        v.co = q + v.normal * disp
    return bm


def _layout(spec, rng):
    """Cliff mass as a list of fractured blocks: (centre, size, z-rotation)."""
    X, Y, Z = spec['size']
    out = []
    kind = spec['layout']
    if kind == 'ledges':
        z = -1.6
        setback = 0.0
        while z < Z:
            t = rng.uniform(0.7, 1.7) * (Z / 10.0) ** 0.5
            frac = max(0.0, z / Z)
            depth = Y * (1.0 - 0.35 * frac) * rng.uniform(0.85, 1.0)
            setback += rng.uniform(-0.5, 0.9)
            width = X * (1.0 - 0.25 * frac * rng.uniform(0.5, 1.2))
            x = -width / 2
            while x < width / 2:
                w = rng.uniform(2.2, 4.8)
                if z + t > Z * 0.75 and rng.random() < 0.3:   # ragged skyline
                    x += w
                    continue
                cx = x + w / 2
                cy = setback * 0.3 + rng.uniform(-0.4, 0.4)
                out.append(((cx, cy, z + t / 2), (w * 1.12, depth * rng.uniform(0.85, 1.05), t * 1.15),
                            rng.uniform(-0.07, 0.07)))
                x += w * rng.uniform(0.85, 1.0)
            z += t
    elif kind == 'columns':
        nx, ny = 4, 3
        for i in range(nx):
            for j in range(ny):
                w = X / nx
                d = Y / ny
                cx = -X / 2 + (i + 0.5) * w + rng.uniform(-0.3, 0.3)
                cy = -Y / 2 + (j + 0.5) * d + rng.uniform(-0.3, 0.3)
                edge = min(i, nx - 1 - i) + min(j, ny - 1 - j)
                h = Z * rng.uniform(0.55, 0.75) if edge == 0 else Z * rng.uniform(0.8, 1.0)
                out.append(((cx, cy, (h - 1.5) / 2), (w * 1.15, d * 1.15, h + 1.5), rng.uniform(-0.12, 0.12)))
                if rng.random() < 0.5:   # a capstone or a broken top block
                    hh = rng.uniform(1.0, 2.2)
                    out.append(((cx + rng.uniform(-0.3, 0.3), cy, h - 0.2 + hh / 2), (w * 0.9, d * 0.9, hh),
                                rng.uniform(-0.3, 0.3)))
    else:   # blocks
        z = -1.5
        tier = 0
        while z < Z * 0.9:
            t = rng.uniform(2.5, 4.0)
            shrink = 1.0 - 0.18 * tier
            n = rng.randint(2, 3)
            for k in range(n):
                w = X * shrink / n * rng.uniform(1.0, 1.25)
                cx = (-X * shrink / 2) + (k + 0.5) * X * shrink / n + rng.uniform(-0.6, 0.6)
                d = Y * shrink * rng.uniform(0.75, 1.0)
                out.append(((cx, rng.uniform(-0.8, 0.8) + tier * 0.6, z + t / 2), (w, d, t * 1.1), rng.uniform(-0.15, 0.15)))
            z += t * 0.95
            tier += 1
    return out


def _cliff_high(name, spec, seed):
    rng = random.Random(seed)
    bspec = dict(e=0.5, cuts=7, depth=(0.6, 0.9), knee=0.03, amp=0.02, fis=0.02, strata=None, joints=0.0)
    me = bpy.data.meshes.new(name + '_blocks')
    bm = bmesh.new()
    for (c, sz, rz) in _layout(spec, rng):
        sh = _shell(bspec, rng, sz, 5)
        ca, sa = math.cos(rz), math.sin(rz)
        for v in sh.verts:
            x, y = v.co.x, v.co.y
            v.co = Vector((x * ca - y * sa + c[0], x * sa + y * ca + c[1], v.co.z + c[2]))
        tmp = bpy.data.meshes.new('tmp')
        sh.to_mesh(tmp)
        bm.from_mesh(tmp)
        bpy.data.meshes.remove(tmp)
        sh.free()
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new('tmp_blocks', me)
    bpy.context.scene.collection.objects.link(ob)
    m = ob.modifiers.new('rm', 'REMESH')
    m.mode = 'VOXEL'
    m.voxel_size = max(spec['size']) / 190.0
    m.adaptivity = 0.0
    dg = bpy.context.evaluated_depsgraph_get()
    rme = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    bpy.data.objects.remove(ob)
    bpy.data.meshes.remove(me)
    # surface detail on the united mass
    bm = bmesh.new()
    bm.from_mesh(rme)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.normal_update()
    ext = max(spec['size'])
    seed_v = Vector((rng.uniform(-90, 90), rng.uniform(-90, 90), rng.uniform(-90, 90)))
    lay = spec.get('strata')
    for v in bm.verts:
        q = v.co
        p = q / ext * 3.0 + seed_v
        disp = spec['amp'] * ext * noise.fractal(p * 2.0, 0.6, 2.0, 5)
        # verifier fix: joints mostly vertical (noise squashed along Z), half as deep and narrower; the closed
        # 0.4-0.8 m deep cells read as black ink outlines on the cliff faces
        f = noise.noise(Vector((p.x, p.y, p.z * 0.25)) * 2.3 + Vector((3.1, 1.7, 9.2)))
        disp -= 0.55 * spec['fis'] * ext * max(0.0, 1.0 - abs(f) * 14.0) ** 2
        disp += 0.003 * ext * noise.noise(p * 25.0)
        if lay:
            lh, la = lay
            w = (q.z + 0.3 * lh * noise.noise(Vector((q.x, q.y, 0)) * 0.2 + seed_v)) / lh
            fr = w - math.floor(w)
            disp += la * ext * 0.1 * (0.5 * (1 - fr) ** 3 - 0.8 * max(0.0, fr - 0.85) / 0.15)
        v.co = q + v.normal * disp
    zs = [v.co.z for v in bm.verts]
    zmin = min(zs)
    for v in bm.verts:
        if v.co.z < -1.0:
            v.co.z = -1.0 + (v.co.z + 1.0) * 0.5
    out = bpy.data.meshes.new(name + '_high')
    bm.to_mesh(out)
    bm.free()
    bpy.data.meshes.remove(rme)
    return out


def build_high(name, spec, seed):
    if spec.get('layout'):
        return _cliff_high(name, spec, seed)
    rng = random.Random(seed)
    sx, sy, sz = spec['size']
    bm = bmesh.new()
    shells = []
    if spec.get('split'):
        g = spec['split']
        n = Vector((1, rng.uniform(-0.2, 0.2), 0)).normalized()
        a = _shell(spec, random.Random(seed), spec['size'], spec['sub'], (n, -g / 2))
        b = _shell(spec, random.Random(seed), spec['size'], spec['sub'], (-n, -g / 2))
        # open the crack: push the halves apart and tilt them slightly
        for v in a.verts:
            v.co -= n * 0.08
        for v in b.verts:
            v.co += n * 0.08
        shells = [a, b]
    elif spec.get('cluster'):
        k = spec['cluster']
        for i in range(k):
            s = (0.62 - 0.14 * i)
            sz_ = (sx * s, sy * s * rng.uniform(0.8, 1.1), sz * (1.0 - 0.18 * i))
            sh = _shell(spec, rng, sz_, spec['sub'])
            ang = 2 * math.pi * i / k + rng.uniform(-0.3, 0.3)
            off = Vector((math.cos(ang) * sx * 0.22, math.sin(ang) * sy * 0.2, -sz * 0.08 * i))
            for v in sh.verts:
                v.co += off
            shells.append(sh)
    elif spec.get('scree'):
        k = spec['scree']
        for i in range(k):
            r = math.sqrt(rng.random()) * 0.5
            ang = rng.uniform(0, 2 * math.pi)
            s = rng.uniform(0.25, 0.42) * (1.0 - r * 0.6)
            sz_ = (sx * s, sy * s * rng.uniform(0.7, 1.1), sz * s * 1.6 * rng.uniform(0.7, 1.0))
            sh = _shell(spec, rng, sz_, spec['sub'])
            h = (1.0 - r * 1.6) * sz * 0.5
            off = Vector((math.cos(ang) * r * sx, math.sin(ang) * r * sy * 0.9, max(h, 0.0) * 0.6))
            rot = rng.uniform(0, 2 * math.pi)
            ca, sa = math.cos(rot), math.sin(rot)
            for v in sh.verts:
                x, y = v.co.x, v.co.y
                v.co = Vector((x * ca - y * sa, x * sa + y * ca, v.co.z)) + off
            shells.append(sh)
    else:
        shells = [_shell(spec, rng, spec['size'], spec['sub'])]
    me = bpy.data.meshes.new(name + '_high')
    for sh in shells:
        tmp = bpy.data.meshes.new('tmp')
        sh.to_mesh(tmp)
        bm.from_mesh(tmp)
        bpy.data.meshes.remove(tmp)
        sh.free()
    # sink: the lowest part goes below ground; flatten what is far under so the hidden base is cheap
    zs = [v.co.z for v in bm.verts]
    zmin, zmax = min(zs), max(zs)
    ground = zmin + (zmax - zmin) * spec['sink']
    for v in bm.verts:
        v.co.z -= ground
        if v.co.z < -0.3:
            v.co.z = -0.3 + (v.co.z + 0.3) * 0.35
    bm.to_mesh(me)
    bm.free()
    return me


def bake_maps(low, high, tex_dir, asset, size=1024):
    """Cycles bake of tangent normals and AO (with a ground plane) from high to low into UVUnique."""
    scene = bpy.context.scene
    prev_engine = scene.render.engine
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.render.bake.margin = 8
    ext = max(low.dimensions)
    # only the sculpt, the game mesh and a ground plane may take part (other assets sit at the origin too)
    hidden = [o for o in scene.objects if not o.hide_render and o is not low]
    for o in hidden:
        o.hide_render = True
    # ground plane for the AO bake
    gme = bpy.data.meshes.new('bake_ground')
    s = ext * 3
    gme.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ground = bpy.data.objects.new('bake_ground', gme)
    scene.collection.objects.link(ground)
    scene.collection.objects.link(high)
    if low.name not in scene.collection.objects:
        scene.collection.objects.link(low)
    low.visible_diffuse = False
    low.visible_glossy = False
    low.visible_shadow = False
    low.visible_transmission = False
    low.visible_volume_scatter = False
    imgs = {}
    bake_mat = bpy.data.materials.new('bake_tmp')
    bake_mat.use_nodes = True
    nt = bake_mat.node_tree
    tnode = nt.nodes.new('ShaderNodeTexImage')
    nt.nodes.active = tnode
    saved = [s_.material for s_ in low.material_slots]
    low.data.materials.clear()
    low.data.materials.append(bake_mat)
    low.data.uv_layers.active = low.data.uv_layers['UVUnique']
    low.data.uv_layers['UVUnique'].active_render = True
    for o in list(bpy.context.selected_objects):
        o.select_set(False)
    high.select_set(True)
    low.select_set(True)
    bpy.context.view_layer.objects.active = low
    for kind, samples in (('NORMAL', 4), ('AO', 48)):
        img = bpy.data.images.new('%s_%s' % (asset, kind), size, size, alpha=False, float_buffer=(kind == 'NORMAL'))
        img.colorspace_settings.name = 'Non-Color'
        tnode.image = img
        scene.cycles.samples = samples
        ground.hide_render = (kind == 'NORMAL')
        bpy.ops.object.bake(type=kind, use_selected_to_active=True, cage_extrusion=ext * 0.035,
                            max_ray_distance=ext * 0.08, normal_space='TANGENT', margin=8,
                            uv_layer='UVUnique', target='IMAGE_TEXTURES')
        imgs[kind] = img
    # write N as is; ORM: R = baked AO, G = roughness (crevices rougher), B = 0
    import numpy as np
    n = np.array(imgs['NORMAL'].pixels[:], np.float32).reshape(size, size, 4)
    ao = np.array(imgs['AO'].pixels[:], np.float32).reshape(size, size, 4)[..., 0]
    ao = np.clip(ao, 0.0, 1.0)
    rough = 0.82 + (1.0 - ao) * 0.12
    orm = np.stack([ao, rough, np.zeros_like(ao)], -1)
    import dmtex as T
    T.save_png(os.path.join(tex_dir, 'T_Prop_%s_N.png' % asset.replace('_', '')), n[..., :3], None, srgb=False)
    T.save_png(os.path.join(tex_dir, 'T_Prop_%s_ORM.png' % asset.replace('_', '')), orm, None, srgb=False)
    # restore
    low.data.materials.clear()
    for m in saved:
        low.data.materials.append(m)
    low.data.uv_layers.active = low.data.uv_layers['UVMap']
    for k in ('visible_diffuse', 'visible_glossy', 'visible_shadow', 'visible_transmission', 'visible_volume_scatter'):
        setattr(low, k, True)
    for o in hidden:
        o.hide_render = False
    scene.collection.objects.unlink(high)
    if low.name in scene.collection.objects:
        scene.collection.objects.unlink(low)
    bpy.data.objects.remove(ground)
    bpy.data.meshes.remove(gme)
    bpy.data.materials.remove(bake_mat)
    for img in imgs.values():
        bpy.data.images.remove(img)
    scene.render.engine = prev_engine
    return float(ao.mean())


def build_rock(name, spec, coll, tex_dir, seed, bake=True):
    """Returns (lod objects list, info dict). LOD0 decimated from the sculpt, LOD1/2(/3) from LOD0."""
    hme = build_high(name, spec, seed)
    high = bpy.data.objects.new(name + '_high', hme)
    high_tris = sum(len(p.vertices) - 2 for p in hme.polygons)
    full = 'SM_Prop_' + name
    # decimate the sculpt to the LOD0 budget
    lod0 = L.decimated_copy(high, full + '_LOD0', spec['tris'] / high_tris, coll)
    me = lod0.data
    for p in me.polygons:
        p.use_smooth = True
    # unique UV first (for the bake), then tiling UV0 by box projection in metres / 2 m tiles
    while len(me.uv_layers):
        me.uv_layers.remove(me.uv_layers[0])
    # rocks: UV0 = UVUnique (the baked maps; UE builds the tangent basis from UV0), UV1 = UVMap (tiling detail)
    me.uv_layers.new(name='UVUnique')
    me.uv_layers.new(name='UVMap')
    L.add_unique_uv(lod0, margin=0.006)
    _box_uv_mesh(me, L.TILE['Rock'])
    me.uv_layers['UVUnique'].active_render = True
    # vertex colour: RGB white (biome tints come from the material instance), A = moss on the upper surfaces
    _rock_colours(me, spec['moss'], seed)
    me.materials.append(L.get_mat('M_Prop_Rock'))
    ao_mean = None
    if bake:
        ao_mean = bake_maps(lod0, high, tex_dir, name)
    lods = [lod0]
    ratios = [0.4, 0.12] + ([0.035] if max(spec['size']) > 10 else [])
    for i, r in enumerate(ratios, start=1):
        # verifier fix: chain from the previous LOD (Cliff_B LOD3 stalled at the LOD2 count when decimated from LOD0)
        prev = lods[-1]
        o = L.decimated_copy(prev, '%s_LOD%d' % (full, i), spec['tris'] * r / L.tri_count(prev), coll)
        lods.append(o)
    bpy.data.objects.remove(high)
    bpy.data.meshes.remove(hme)
    print("BAKE", name, "ao_mean", ao_mean, flush=True)
    return lods, dict(high_tris=high_tris, ao_mean=ao_mean)


def _box_uv_mesh(me, mpt):
    uv = me.uv_layers['UVMap']
    data = [0.0] * (len(me.loops) * 2)
    for p in me.polygons:
        n = p.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        a, b = ((1, 2), (0, 2), (0, 1))[ax]
        for li in range(p.loop_start, p.loop_start + p.loop_total):
            co = me.vertices[me.loops[li].vertex_index].co
            data[2 * li] = co[a] / mpt
            data[2 * li + 1] = co[b] / mpt
    uv.data.foreach_set('uv', data)
    me.uv_layers.active = uv


def _rock_colours(me, moss, seed):
    col = me.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    me.update()
    vn = [v.normal.copy() for v in me.vertices]
    off = Vector((seed % 97, seed % 89, seed % 83))
    data = [0.0] * (len(me.loops) * 4)
    for li, lp in enumerate(me.loops):
        v = me.vertices[lp.vertex_index]
        up = max(0.0, min(1.0, (vn[lp.vertex_index].z - 0.25) / 0.6))
        n = noise.noise(v.co * 0.6 + off) * 0.5 + 0.5
        a = max(0.0, min(1.0, up * moss * (0.4 + n)))
        data[4 * li:4 * li + 4] = (1.0, 1.0, 1.0, a)
    col.data.foreach_set('color', data)
