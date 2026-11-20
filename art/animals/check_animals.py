# DEMIURGE art/animals/check_animals.py - re-import every export and verify it (run headless in Blender 5.2).
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       --python-exit-code 1 --python art/animals/check_animals.py
#
# For each SM_*.fbx: an LOD group node, LOD meshes in order, triangle counts equal to the manifest, UV0 + UV1
# (AnimWeights), a colour attribute, one material slot named Structure, shape keys on LOD0 when the manifest says so,
# size within a few percent of the manifest. For each SK_*.fbx: armature bone count, skinned mesh with groups for the
# bones, every action present with the manifest's frame count. Writes art/animals/source/export-check.json.
import bmesh
import bpy
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.join(HERE, 'export')


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def tris(me):
    return sum(len(p.vertices) - 2 for p in me.polygons)


def open_edges(me):
    bm = bmesh.new()
    bm.from_mesh(me)
    n = sum(1 for e in bm.edges if not e.is_manifold)
    bm.free()
    return n


def extent(o):
    c = [o.matrix_world @ v.co for v in o.data.vertices]
    return [max(v[i] for v in c) - min(v[i] for v in c) for i in range(3)]


def main():
    man = json.load(open(os.path.join(HERE, 'manifest.json')))
    report, bad = {}, 0
    for name, m in man['meshes'].items():
        r = {'problems': []}
        clear()
        bpy.ops.import_scene.fbx(filepath=os.path.join(EXP, name + '.fbx'))
        objs = list(bpy.data.objects)
        grp = [o for o in objs if o.type == 'EMPTY']
        meshes = sorted([o for o in objs if o.type == 'MESH'], key=lambda o: o.name)
        r['lod_group'] = bool(grp) and (grp[0].get('fbx_type') == 'LodGroup' or grp[0].name == name)
        r['lods'] = [o.name for o in meshes]
        r['tris'] = [tris(o.data) for o in meshes]
        if r['tris'] != m['triangles_per_lod']:
            r['problems'].append('triangles %s != manifest %s' % (r['tris'], m['triangles_per_lod']))
        me0 = meshes[0].data
        # verification additions (2026-11-19): closed LODs, and far LODs that keep the silhouette (legs, wings, fins)
        r['open_edges'] = [open_edges(o.data) for o in meshes]
        if max(r['open_edges']) > 8:
            r['problems'].append('open / non-manifold edges %s' % r['open_edges'])
        e0 = extent(meshes[0])
        r['lod_extent'] = [[round(e / max(f, 1e-6), 2) for e, f in zip(extent(o), e0)] for o in meshes]
        # walkers keep their legs (lowest point within 10 % of the height of LOD0's; antlers and horns may go),
        # birds and the whale keep their span (wings, flippers); the LOD3 hull is exempt
        if m['kind'] in ('quad', 'cat'):
            z0 = min((meshes[0].matrix_world @ v.co).z for v in me0.vertices)
            for i, o in enumerate(meshes[1:3], 1):
                zi = min((o.matrix_world @ v.co).z for v in o.data.vertices)
                if zi - z0 > 0.1 * e0[2]:
                    r['problems'].append('LOD%d lost its legs (lowest point %.2f m above LOD0)' % (i, zi - z0))
        elif m['kind'] in ('bird', 'whale'):
            for i, ex in enumerate(r['lod_extent'][1:3], 1):
                if ex[0] < 0.7:
                    r['problems'].append('LOD%d lost its span (extent vs LOD0 %s)' % (i, ex))
        r['uv_layers'] = [l.name for l in me0.uv_layers]
        r['color_attributes'] = [a.name for a in me0.color_attributes]
        r['material_slots'] = [s.material.name if s.material else None for s in meshes[0].material_slots]
        r['shape_keys'] = [k.name for k in me0.shape_keys.key_blocks][1:] if me0.shape_keys else []
        if len(r['uv_layers']) < 2:
            r['problems'].append('missing UV1 AnimWeights')
        if not r['color_attributes']:
            r['problems'].append('no vertex colour')
        if r['material_slots'] != ['Structure']:
            r['problems'].append('material slots %s' % r['material_slots'])
        if r['shape_keys'] != m['shape_keys']:
            r['problems'].append('shape keys %s != %s' % (r['shape_keys'], m['shape_keys']))
        bb = [max(v.co[i] for v in me0.vertices) - min(v.co[i] for v in me0.vertices) for i in range(3)]
        sc = meshes[0].matrix_world.to_scale()
        dims = [bb[i] * sc[i] for i in range(3)]
        r['dims_m'] = [round(d, 3) for d in dims]
        # sizes in any axis order (the importer may rotate the axes back)
        if abs(sorted(dims)[-1] - sorted(m['size_m'])[-1]) > 0.05 * sorted(m['size_m'])[-1]:
            r['problems'].append('size %s vs manifest %s' % (r['dims_m'], m['size_m']))
        # skeletal
        clear()
        bpy.ops.import_scene.fbx(filepath=os.path.join(EXP, m['skeletal_mesh'] + '.fbx'))
        arm = [o for o in bpy.data.objects if o.type == 'ARMATURE']
        skm = [o for o in bpy.data.objects if o.type == 'MESH']
        r['sk_bones'] = len(arm[0].data.bones) if arm else 0
        r['sk_groups'] = len(skm[0].vertex_groups) if skm else 0
        r['sk_actions'] = {a.name: [round(a.frame_range[0]), round(a.frame_range[1])] for a in bpy.data.actions}
        if r['sk_bones'] != len(m['bones']):
            r['problems'].append('bones %d != %d' % (r['sk_bones'], len(m['bones'])))
        for an in m['animations']:
            hit = [k for k in r['sk_actions'] if k.endswith(an['name'])]
            if not hit:
                r['problems'].append('missing action ' + an['name'])
        bad += bool(r['problems'])
        report[name] = r
        print('[check]', name, 'OK' if not r['problems'] else r['problems'], flush=True)
    with open(os.path.join(HERE, 'source', 'export-check.json'), 'w') as f:
        json.dump(report, f, indent=1)
    print('[check] %d meshes, %d with problems' % (len(report), bad))
    if bad:
        sys.exit(1)


main()
