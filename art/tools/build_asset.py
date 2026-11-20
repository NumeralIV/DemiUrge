# DEMIURGE art pipeline - builds one asset kit end to end in Blender (headless):
#   blender.exe -b --factory-startup --python art/tools/build_asset.py -- shrine [configs...] [--preview] [--nobake]
# For every config (state / culture variant) and LOD:
#   1. the asset module (art/tools/<asset>.py) builds its parts as MB meshes -> one Blender object per part
#   2. per-vertex mask "Mask" (RGBA linear): G stain and B growth from the asset's mask(), A fire light (ray-tested
#      falloff from the altar), R ambient occlusion ray-traced (BVH, 48 rays) on the joined mesh (ground plane as occluder)
#   3. the parts are joined into SM_<Asset>_<Config>[_LODn], turned 180 deg (Unreal front = -Y) and exported to
#      art/export/<asset>/<name>.fbx with UCX_ convex collision hulls on LOD0
# Writes art/<asset>/<asset>.blend (all parts, all configs, all LODs) and art/export/<asset>/<asset>.manifest.json.
import importlib
import json
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dmkit as K  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..'))


def cam(name):
    return ''.join(p.capitalize() for p in name.split('_'))


def setup_cycles(samples=64):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type = 'CUDA'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type in ('CUDA',)
        sc.cycles.device = 'GPU'
    except Exception as e:
        print('ASSET cycles gpu unavailable:', e)
    sc.cycles.samples = samples
    if sc.world is None:
        sc.world = bpy.data.worlds.new('World')
    sc.world.light_settings.distance = 1.6


def ground_plane():
    me = bpy.data.meshes.new('ground')
    s = 30.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new('ground', me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def write_masks(ob, part, maskfn, fire, bvh):
    me = ob.data
    attr = me.color_attributes.get('Mask') or me.color_attributes.new('Mask', 'FLOAT_COLOR', 'CORNER')
    mats = [m.name for m in me.materials]
    cn = me.corner_normals
    out = []
    poly_of_loop = [0] * len(me.loops)
    for p in me.polygons:
        for li in p.loop_indices:
            poly_of_loop[li] = p.index
    mw = ob.matrix_world
    for li, lp in enumerate(me.loops):
        p = mw @ me.vertices[lp.vertex_index].co
        n = (mw.to_3x3() @ cn[li].vector).normalized()
        mat = mats[me.polygons[poly_of_loop[li]].material_index] if mats else ''
        st, gr = maskfn(p, n, part, mat)
        fl = 0.0
        if fire is not None and not mat.startswith(('flame', 'ember')):
            L = fire - p
            d = L.length
            if d > 1e-3:
                lam = max(0.0, n.dot(L / d))
                if lam > 0:
                    hit = bvh.ray_cast(p + n * 0.02, L / d, d - 0.12) if bvh else (None,)
                    if hit[0] is None:
                        fl = lam / (1.0 + (d / 0.9) ** 2)
        out.extend((1.0, st, gr, min(1.0, fl * 1.6)))
    attr.data.foreach_set('color', out)
    me.color_attributes.active_color = attr
    me.color_attributes.render_color_index = me.color_attributes.find('Mask')


def hemisphere(n_dirs=48):
    """Fixed cosine-weighted directions on the +Z hemisphere (Fibonacci spiral, deterministic)."""
    out = []
    ga = math.pi * (3 - math.sqrt(5))
    for i in range(n_dirs):
        u = (i + 0.5) / n_dirs
        r = math.sqrt(u)
        a = i * ga
        out.append(Vector((r * math.cos(a), r * math.sin(a), math.sqrt(max(0.0, 1 - u)))))
    return out


def bake_ao(ob, ground, dist=1.6, n_dirs=48):
    """Ray-traced ambient occlusion per corner (cached per vertex + normal) into Mask.R; the ground plane occludes.
    (Cycles' bake to colour attributes returns zeros headless in Blender 5.2, so this is done with a BVH.)"""
    me = ob.data
    t0 = time.time()
    verts = [v.co.copy() for v in me.vertices]
    polys = [list(p.vertices) for p in me.polygons]
    g0 = len(verts)
    s = 40.0
    verts += [Vector((-s, -s, 0)), Vector((s, -s, 0)), Vector((s, s, 0)), Vector((-s, s, 0))]
    polys.append([g0, g0 + 1, g0 + 2, g0 + 3])
    bvh = BVHTree.FromPolygons(verts, polys, epsilon=0.0)
    dirs = hemisphere(n_dirs)
    cn = me.corner_normals
    m = me.color_attributes['Mask']
    b = [0.0] * (len(me.loops) * 4)
    m.data.foreach_get('color', b)
    cache = {}
    for li, lp in enumerate(me.loops):
        n = cn[li].vector.normalized()
        key = (lp.vertex_index, round(n.x, 2), round(n.y, 2), round(n.z, 2))
        a = cache.get(key)
        if a is None:
            p = me.vertices[lp.vertex_index].co
            if p.z < -0.05:
                a = 0.15                    # buried skirt
            else:
                rot = n.to_track_quat('Z', 'Y')
                o = p + n * 0.03
                occ = 0.0
                for d in dirs:
                    w = rot @ d
                    h = bvh.ray_cast(o, w, dist)
                    if h[0] is not None and h[3] > 0.05:      # ignore grazing self-hits of the displaced surface
                        occ += 1.0 - (h[3] / dist) * 0.7
                a = 1.0 - occ / len(dirs)
            cache[key] = a
        b[li * 4] = a
    m.data.foreach_set('color', b)
    return time.time() - t0, sum(b[0::4]) / max(1, len(me.loops))


def ucx(points, name, coll):
    bm = bmesh.new()
    for p in points:
        bm.verts.new(p)
    bmesh.ops.convex_hull(bm, input=bm.verts)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def export_fbx(objs, path):
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in objs:
        o.hide_set(False)
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL',
                             axis_forward='-Z', axis_up='Y', object_types={'MESH'}, use_mesh_modifiers=False,
                             mesh_smooth_type='FACE', use_tspace=False, colors_type='LINEAR', prioritize_active_color=True,
                             add_leaf_bones=False, bake_anim=False, path_mode='STRIP', embed_textures=False, use_custom_props=False)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    asset = argv[0] if argv else 'shrine'
    flags = [a for a in argv[1:] if a.startswith('--')]
    want = [a for a in argv[1:] if not a.startswith('--')]
    mod = importlib.import_module(asset)
    cfgs = want or mod.CONFIGS
    lods = [0, 1, 2]
    if '--lod0' in flags:
        lods = [0]
    K.clean_scene()
    setup_cycles(64)
    ground = ground_plane()
    exp_dir = os.path.join(ROOT, 'export', asset)
    man_path = os.path.join(exp_dir, asset + '.manifest.json')
    manifest = json.load(open(man_path)) if os.path.exists(man_path) else {'asset': asset, 'meshes': {}}
    manifest.update({'units': 'metres in Blender; FBX in centimetres after the UE import (scale 1)', 'pivot': 'ground level, centre of the footprint',
                     'forward': 'front faces -Y in Unreal (turned 180 deg at export; -Y in the .blend)',
                     'vertex_color': 'Mask RGBA linear: R ambient occlusion, G stain, B growth, A fire light',
                     'uv0': 'metric: 1 UV unit = one tile of the slot texture set (art/textures/manifest.json)'})
    stats = {}
    for cfg in cfgs:
        entry = {'lods': [], 'slots': [], 'all_slots': [], 'tris': [], 'bounds_m': None, 'fire': mod.has_fire(cfg)}
        for lod in lods:
            t0 = time.time()
            coll = K.collection('%s_LOD%d' % (cfg, lod), K.collection(cfg))
            parts = mod.build(cfg, lod)
            objs = []
            for pname, mb in parts.items():
                if not mb.F:
                    continue
                mb.name = '%s.%s.LOD%d' % (cfg, pname, lod)
                ob = mb.to_object(coll)
                ob['part'] = pname
                objs.append(ob)
            name = 'SM_%s_%s' % (cam(asset), cam(cfg)) + ('' if lod == 0 else '_LOD%d' % lod)
            ex = K.join_copies(objs, name, K.collection('export_' + cfg))
            ex.data.set_sharp_from_angle(angle=math.radians(35.0))
            # masks on the joined mesh (parts keep a copy for the .blend preview)
            ex_bvh = BVHTree.FromObject(ex, bpy.context.evaluated_depsgraph_get())
            fire = mod.FIRE if mod.has_fire(cfg) else None
            # part name per polygon: rebuild from the source objects' polygon counts
            part_of_poly = []
            for ob in objs:
                part_of_poly.extend([ob['part']] * len(ob.data.polygons))
            maskfn = mod.mask(cfg)
            write_masks_joined(ex, part_of_poly, maskfn, fire, ex_bvh)
            if '--nobake' not in flags:
                tb, ao_mean = bake_ao(ex, ground)
            else:
                tb, ao_mean = 0.0, 1.0
            # copy the joined masks back onto the part objects (preview .blend)
            copy_masks_to_parts(ex, objs)
            # engine frame: turn 180 deg about Z
            ex.data.transform(Matrix.Rotation(math.pi, 4, 'Z'))
            exp = [ex]
            if lod == 0:
                for i, pts in enumerate(mod.collision()):
                    u = ucx([Matrix.Rotation(math.pi, 4, 'Z') @ p for p in pts], 'UCX_%s_%02d' % (name, i), K.collection('export_' + cfg))
                    exp.append(u)
            path = os.path.join(exp_dir, name + '.fbx')
            export_fbx(exp, path)
            tris = K.tri_count(ex)
            bb = [ex.matrix_world @ Vector(c) for c in ex.bound_box]
            lo = [min(c[i] for c in bb) for i in range(3)]
            hi = [max(c[i] for c in bb) for i in range(3)]
            entry['lods'].append(os.path.relpath(path, ROOT).replace('\\', '/'))
            entry['tris'].append(tris)
            for m_ in ex.data.materials:
                if m_.name not in entry['all_slots']:
                    entry['all_slots'].append(m_.name)
            if lod == 0:
                entry['slots'] = [m.name for m in ex.data.materials]
                entry['bounds_m'] = [[round(x, 3) for x in lo], [round(x, 3) for x in hi]]
                entry['collision_hulls'] = len(exp) - 1
            print('ASSET %s LOD%d: %d tris, %d slots, bounds %s..%s, AO bake %.1fs (mean %.2f), %.1fs total' % (
                name, lod, tris, len(ex.data.materials), [round(x, 2) for x in lo], [round(x, 2) for x in hi], tb, ao_mean, time.time() - t0))
            # keep the export copy hidden in the .blend (re-turned to the Blender frame for viewing)
            ex.data.transform(Matrix.Rotation(math.pi, 4, 'Z'))
            ex.hide_set(True)
            ex.hide_render = True
            for o in exp[1:]:
                o.hide_set(True)
                o.hide_render = True
            if lod > 0:
                coll.hide_render = True
                for o in coll.objects:
                    o.hide_set(True)
        manifest['meshes']['SM_%s_%s' % (cam(asset), cam(cfg))] = entry
        stats[cfg] = entry['tris']
    os.makedirs(exp_dir, exist_ok=True)
    with open(man_path, 'w') as f:
        json.dump(manifest, f, indent=1)
    bdir = os.path.join(ROOT, asset)
    os.makedirs(bdir, exist_ok=True)
    ground.hide_render = False
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(bdir, asset + ('' if not want else '_partial') + '.blend'), compress=True, relative_remap=True)
    print('ASSET done', json.dumps(stats))


def write_masks_joined(ex, part_of_poly, maskfn, fire, bvh):
    me = ex.data
    attr = me.color_attributes.get('Mask') or me.color_attributes.new('Mask', 'FLOAT_COLOR', 'CORNER')
    mats = [m.name for m in me.materials]
    cn = me.corner_normals
    out = [0.0] * (len(me.loops) * 4)
    for p in me.polygons:
        mat = mats[p.material_index] if mats else ''
        part = part_of_poly[p.index] if p.index < len(part_of_poly) else ''
        for li in p.loop_indices:
            v = me.vertices[me.loops[li].vertex_index].co
            n = cn[li].vector
            st, gr = maskfn(v, n, part, mat)
            fl = 0.0
            if fire is not None and not mat.startswith(('flame', 'ember', 'foliage')):
                L = fire - v
                d = L.length
                if d > 1e-3:
                    lam = max(0.0, n.dot(L / d))
                    if lam > 0:
                        hit = bvh.ray_cast(v + n * 0.02, L / d, max(0.0, d - 0.15))
                        if hit[0] is None:
                            fl = lam / (1.0 + (d / 1.4) ** 2)
            o = li * 4
            out[o:o + 4] = [1.0, st, gr, min(1.0, fl * 1.6)]
    attr.data.foreach_set('color', out)
    me.color_attributes.active_color = attr
    me.color_attributes.render_color_index = me.color_attributes.find('Mask')


def copy_masks_to_parts(ex, objs):
    src = ex.data.color_attributes['Mask']
    n = len(ex.data.loops)
    a = [0.0] * (n * 4)
    src.data.foreach_get('color', a)
    off = 0
    for ob in objs:
        me = ob.data
        k = len(me.loops)
        attr = me.color_attributes.get('Mask') or me.color_attributes.new('Mask', 'FLOAT_COLOR', 'CORNER')
        attr.data.foreach_set('color', a[off * 4:(off + k) * 4])
        me.color_attributes.active_color = attr
        me.color_attributes.render_color_index = me.color_attributes.find('Mask')
        off += k


main()
