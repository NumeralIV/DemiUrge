# DEMIURGE art - build the whole Age1 (Bronze Age) kit.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/age1/build_age1.py -- [--textures] [--no-fbx] [--only House,Granary]
#
# Writes art/age1/textures/*.png (with --textures, or when missing), art/age1/source/age1.blend,
# art/age1/export/<mesh>.fbx (one file per mesh, LOD0..LOD3 under an FBX LodGroup) and art/age1/manifest.json.
import os
import sys
import json
import math
import time
import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path[:0] = [HERE, os.path.join(ROOT, 'art', 'tools')]

import bpy
import age1_tex
import age1_geo as G
import age1_assets as A

TEXDIR = os.path.join(HERE, 'textures')
EXPDIR = os.path.join(HERE, 'export')
SRCDIR = os.path.join(HERE, 'source')
FAMILY = 'Age1'

ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
ONLY = None
if '--only' in ARGS:
    ONLY = set(ARGS[ARGS.index('--only') + 1].split(','))

# --------------------------------------------------------------------------- materials

SETS = {   # material -> texture set, has M mask
    'Mudbrick': ('Mudbrick', False), 'Render': ('Render', False), 'Thatch': ('Thatch', False),
    'Earth': ('Earth', False), 'Soil': ('Soil', False), 'Crop': ('Crop', False), 'Trim': ('Trim', True),
}

# culture material instances: linear tint on the base colour, roughness offset
CULTURES = {
    'Valley': {'note': 'default river-valley people (concept age-1-bronze): ochre mudbrick, golden-grey thatch',
               'tints': {}, 'ornament': None},
    'CliffTerrace': {'note': 'culture A: rose-ochre plaster, warm straw; prefers the flat-roofed house C',
                     'tints': {'Mudbrick': (1.28, 0.86, 0.74, 0.0), 'Render': (1.25, 0.88, 0.78, -0.03),
                               'Thatch': (1.05, 0.95, 0.82, 0.0)}, 'ornament': None},
    'LagoonWeaver': {'note': 'culture B: grey clay, silvered timber, mossy grey thatch; crossed gable horns',
                     'tints': {'Mudbrick': (0.86, 0.9, 0.92, 0.0), 'Render': (0.9, 0.93, 0.95, 0.0),
                               'Thatch': (0.66, 0.78, 0.68, 0.02), 'Trim': (0.88, 0.94, 1.0, 0.0)}, 'ornament': 'SM_Age1_Orn_GableHorns'},
    'RingBuilder': {'note': 'culture C: pale limestone-coloured render, dark blue-grey aged thatch; clay pot finials',
                    'tints': {'Mudbrick': (1.4, 1.36, 1.32, 0.0), 'Render': (1.4, 1.38, 1.36, 0.0),
                              'Thatch': (0.42, 0.48, 0.6, 0.0)}, 'ornament': 'SM_Age1_Orn_Finial'},
}
STATE_MI = {
    'Burnt': {'note': 'apply to _Damaged / _Ruin meshes for a burnt town (cheap _Burnt state)',
              'tints': {'Mudbrick': (0.42, 0.36, 0.32, 0.05), 'Render': (0.42, 0.36, 0.32, 0.05), 'Thatch': (0.16, 0.14, 0.13, 0.05),
                        'Trim': (0.25, 0.22, 0.2, 0.05), 'Earth': (0.5, 0.45, 0.4, 0.0), 'Soil': (0.45, 0.4, 0.36, 0.0)}},
}
CROP_MI = {'Ripe': (1.0, 1.0, 1.0, 0.0), 'Green': (0.55, 1.05, 0.5, -0.05), 'Stubble': (1.12, 1.0, 0.78, 0.03)}


def load_img(name, srgb):
    path = os.path.join(TEXDIR, name + '.png')
    img = bpy.data.images.get(name) or bpy.data.images.load(path, check_existing=True)
    img.name = name
    img.colorspace_settings.name = 'sRGB' if srgb else 'Non-Color'
    return img


def make_material(mname, tset, has_m, tint=(1, 1, 1, 0.0)):
    mat = bpy.data.materials.new(mname)
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bs = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bs.outputs[0], out.inputs[0])
    uv = nt.nodes.new('ShaderNodeUVMap')
    uv.uv_map = 'UVMap'
    def tex(suffix, srgb):
        n = nt.nodes.new('ShaderNodeTexImage')
        n.image = load_img('T_%s_%s_%s' % (FAMILY, tset, suffix), srgb)
        n.interpolation = 'Linear'
        nt.links.new(uv.outputs[0], n.inputs[0])
        return n
    td, tn, to = tex('D', True), tex('N', False), tex('ORM', False)
    vc = nt.nodes.new('ShaderNodeVertexColor')
    vc.layer_name = 'Col'
    tintn = nt.nodes.new('ShaderNodeRGB')
    tintn.name = 'Tint'
    tintn.outputs[0].default_value = (tint[0], tint[1], tint[2], 1.0)
    m1 = nt.nodes.new('ShaderNodeMix')
    m1.data_type = 'RGBA'
    m1.blend_type = 'MULTIPLY'
    m1.inputs['Factor'].default_value = 1.0
    nt.links.new(td.outputs['Color'], m1.inputs['A'])
    nt.links.new(tintn.outputs[0], m1.inputs['B'])
    m2 = nt.nodes.new('ShaderNodeMix')
    m2.data_type = 'RGBA'
    m2.blend_type = 'MULTIPLY'
    m2.inputs['Factor'].default_value = 1.0
    nt.links.new(m1.outputs['Result'], m2.inputs['A'])
    nt.links.new(vc.outputs['Color'], m2.inputs['B'])
    nt.links.new(m2.outputs['Result'], bs.inputs['Base Color'])
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(to.outputs['Color'], sep.inputs[0])
    radd = nt.nodes.new('ShaderNodeMath')
    radd.name = 'RoughAdd'
    radd.operation = 'ADD'
    radd.use_clamp = True
    radd.inputs[1].default_value = tint[3]
    nt.links.new(sep.outputs[1], radd.inputs[0])
    nt.links.new(radd.outputs[0], bs.inputs['Roughness'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nm.uv_map = 'UVMap'
    nt.links.new(tn.outputs['Color'], nm.inputs['Color'])
    nt.links.new(nm.outputs[0], bs.inputs['Normal'])
    if has_m:
        tm = tex('M', False)
        sm = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(tm.outputs['Color'], sm.inputs[0])
        night = nt.nodes.new('ShaderNodeValue')
        night.name = 'Night'
        night.outputs[0].default_value = 0.0
        mul = nt.nodes.new('ShaderNodeMath')
        mul.operation = 'MULTIPLY'
        nt.links.new(sm.outputs[0], mul.inputs[0])
        nt.links.new(night.outputs[0], mul.inputs[1])
        bs.inputs['Emission Color'].default_value = (1.0, 0.55, 0.22, 1.0)
        nt.links.new(mul.outputs[0], bs.inputs['Emission Strength'])
    mat['dm_texture_set'] = tset
    mat.use_fake_user = True
    return mat


def build_materials():
    mats = {}
    for mname, (tset, has_m) in SETS.items():
        mats[mname] = make_material('M_%s_%s' % (FAMILY, mname), tset, has_m)
    # culture / state / crop instances (Blender copies with the tint baked into the node values, for previews)
    for cul, c in CULTURES.items():
        for mname, tint in c['tints'].items():
            tset, has_m = SETS[mname]
            make_material('MI_%s_%s_%s' % (FAMILY, mname, cul), tset, has_m, tint)
    for st, c in STATE_MI.items():
        for mname, tint in c['tints'].items():
            tset, has_m = SETS[mname]
            make_material('MI_%s_%s_%s' % (FAMILY, mname, st), tset, has_m, tint)
    for k, tint in CROP_MI.items():
        make_material('MI_%s_Crop_%s' % (FAMILY, k), 'Crop', False, tint)
    return mats


# --------------------------------------------------------------------------- registry

HOUSE_MATS = ('Mudbrick', 'Thatch', 'Trim')
REG = []   # (mesh name, asset key, generator, variant, state, slot materials, budget class, lod3 mode)
for v in 'ABCD':
    for st in A.STATES:
        REG.append(('SM_Age1_House_%s_%s' % (v, st), 'House', A.house, v, st,
                    ('Mudbrick', 'Render', 'Trim') if v == 'C' else HOUSE_MATS, 'house', 'none'))
for st in A.STATES:
    REG.append(('SM_Age1_Granary_A_%s' % st, 'Granary', A.granary, 'A', st, ('Render', 'Thatch', 'Trim'), 'civic', 'hull'))
    REG.append(('SM_Age1_Temple_A_%s' % st, 'Temple', A.temple, 'A', st, ('Mudbrick', 'Render', 'Trim'), 'civic', 'hull'))
    REG.append(('SM_Age1_TempleCourt_A_%s' % st, 'TempleCourt', A.temple_court, 'A', st, ('Mudbrick', 'Render', 'Trim'), 'civic', 'lod2'))
    REG.append(('SM_Age1_Palisade_Straight_%s' % st, 'Palisade', A.palisade, 'Straight', st, ('Earth', None, 'Trim'), 'wall', 'none'))
    REG.append(('SM_Age1_Rampart_Straight_%s' % st, 'Rampart', A.rampart, 'Straight', st, ('Earth', None, 'Trim'), 'wall', 'hull'))
    REG.append(('SM_Age1_Rampart_End_%s' % st, 'Rampart', A.rampart, 'End', st, ('Earth', None, 'Trim'), 'wall', 'hull'))
    REG.append(('SM_Age1_Gate_A_%s' % st, 'Gate', A.gate, 'A', st, ('Earth', 'Thatch', 'Trim'), 'wall', 'hull'))
    REG.append(('SM_Age1_Landing_A_%s' % st, 'Landing', A.landing, 'A', st, ('Earth', None, 'Trim'), 'civic-small', 'hull'))
for v in ('Ripe', 'Growing', 'Ploughed', 'Stubble'):
    REG.append(('SM_Age1_Field_%s' % v, 'Field', A.field, v, 'Intact', ('Soil', 'Crop', 'Trim'), 'field', 'hull'))
for v in ('A', 'B'):
    REG.append(('SM_Age1_ReedBoat_%s' % v, 'ReedBoat', A.reed_boat, v, 'Intact', (None, None, 'Trim'), 'vehicle', 'none'))
REG.append(('SM_Age1_Orn_GableHorns', 'Ornament', A.ornament, 'GableHorns', 'Intact', (None, None, 'Trim'), 'prop', 'none'))
REG.append(('SM_Age1_Orn_Finial', 'Ornament', A.ornament, 'Finial', 'Intact', (None, 'Render', None), 'prop', 'none'))

BUDGET = {'house': (2000, 6000), 'civic': (8000, 20000), 'civic-small': (2000, 8000), 'wall': (500, 6000),
          'field': (300, 3000), 'vehicle': (1000, 4000), 'prop': (50, 1000)}


# --------------------------------------------------------------------------- build

def ensure_coll(name, parent):
    c = bpy.data.collections.get(name)
    if not c:
        c = bpy.data.collections.new(name)
        parent.children.link(c)
    return c


def remap_slots(me, slot_mats):
    used = sorted({p.material_index for p in me.polygons})
    order = [i for i in range(3) if i in used]
    idx = {old: new for new, old in enumerate(order)}
    me.polygons.foreach_set('material_index', [idx[p.material_index] for p in me.polygons])
    return order


def build():
    t0 = time.time()
    if '--textures' in ARGS or not os.path.exists(os.path.join(TEXDIR, 'T_Age1_Trim_M.png')):
        os.makedirs(TEXDIR, exist_ok=True)
        for line in age1_tex.build_all(TEXDIR):
            print('[tex]', line)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    mats = build_materials()
    root = ensure_coll('Age1', scene.collection)
    manifest = {'family': FAMILY, 'generated': datetime.date.today().isoformat(),
                'script': 'art/age1/build_age1.py', 'licence': 'original',
                'units': 'metres (Unreal imports at 100x), Z up, front faces -Y, pivot at the footprint centre at ground level',
                'lod_screen_sizes': [1.0, 0.35, 0.12, 0.04],
                'material_slots': 'in order Structure, Roof, Detail; a slot a mesh does not use is omitted (see each mesh slots)',
                'vertex_color': "'Col' (corner, sRGB bytes): ground-contact grime / variation, multiply into base colour",
                'textures': {}, 'materials': {}, 'material_instances': {}, 'cultures': {}, 'meshes': {}}
    for mname, (tset, has_m) in SETS.items():
        manifest['materials']['M_Age1_' + mname] = {
            'textures': ['T_Age1_%s_%s' % (tset, s) for s in (('D', 'N', 'ORM', 'M') if has_m else ('D', 'N', 'ORM'))],
            'uv_tile_m': G.TILE.get(tset), 'trim_strips': age1_tex.STRIPS if tset == 'Trim' else None,
            'params': {'Tint': 'linear RGB multiplier on D', 'RoughAdd': 'added to ORM.G', 'Night': 'emissive = M.R x Night (Trim only)'}}
    for f in sorted(os.listdir(TEXDIR)):
        if f.endswith('.png'):
            manifest['textures'][f[:-4]] = {'file': 'art/age1/textures/' + f, 'size': 1024,
                                            'colour_space': 'sRGB' if f.endswith('_D.png') else 'linear'}
    for cul, c in CULTURES.items():
        manifest['cultures'][cul] = {'note': c['note'], 'ornament': c['ornament'],
                                     'instances': {'M_Age1_' + k: 'MI_Age1_%s_%s' % (k, cul) for k in c['tints']}}
        for k, tint in c['tints'].items():
            manifest['material_instances']['MI_Age1_%s_%s' % (k, cul)] = {'parent': 'M_Age1_' + k, 'Tint': list(tint[:3]), 'RoughAdd': tint[3]}
    for st, c in STATE_MI.items():
        for k, tint in c['tints'].items():
            manifest['material_instances']['MI_Age1_%s_%s' % (k, st)] = {'parent': 'M_Age1_' + k, 'Tint': list(tint[:3]), 'RoughAdd': tint[3], 'note': c['note']}
    for k, tint in CROP_MI.items():
        manifest['material_instances']['MI_Age1_Crop_' + k] = {'parent': 'M_Age1_Crop', 'Tint': list(tint[:3]), 'RoughAdd': tint[3]}

    os.makedirs(EXPDIR, exist_ok=True)
    report = []
    for (name, key, gen, var, st, slotmats, budget, lod3) in REG:
        if ONLY and key not in ONLY:
            continue
        coll = ensure_coll(key, root)
        meshes, info = [], None
        for lod in range(3):
            mb, inf = gen(var, st, lod)
            if lod == 0:
                info = inf
            meshes.append(G.to_mesh(mb, '%s_LOD%d' % (name, lod)))
        big = max(info['W'], info['D'], info['H']) > 10.0
        if big and lod3 == 'hull':
            meshes.append(G.hull_mesh(meshes[2], '%s_LOD3' % name, None))
        elif big and lod3 == 'lod2':
            meshes.append(meshes[2].copy())
            meshes[3].name = '%s_LOD3' % name
        empty = bpy.data.objects.new(name, None)
        empty['fbx_type'] = 'LodGroup'
        empty.empty_display_size = 1.0
        coll.objects.link(empty)
        objs = []
        for lod, me in enumerate(meshes):
            ob = bpy.data.objects.new(me.name, me)
            coll.objects.link(ob)
            ob.parent = empty
            objs.append(ob)
        # LOD ratio correction (target 40 % / 12 % / ~4 %)
        t_0 = G.tris_of(objs[0].data)
        for lod, target in ((1, 0.40), (2, 0.12), (3, 0.05)):
            if lod < len(objs):
                tl = G.tris_of(objs[lod].data)
                if lod == 3 and lod3 == 'lod2':
                    G.decimate_to(objs[3], min(1.0, 0.04 * t_0 / max(1, tl)))
                elif lod < 3 and tl > target * 1.2 * t_0:
                    G.decimate_to(objs[lod], target * t_0 / tl)
        order = None
        for ob in objs:
            # verifier fix: drop duplicate / degenerate faces (coincident slab bands, collapsed caps). The FBX
            # importers (Blender, Unreal) drop them anyway, so without this the manifest counts were too high.
            ob.data.validate(clean_customdata=False)
            o = remap_slots(ob.data, slotmats)
            order = o if order is None else order
            for i in o:
                ob.data.materials.append(mats[slotmats[i]])
        tris = [G.tris_of(ob.data) for ob in objs]
        lo, hi = BUDGET[budget]
        flag = '' if lo <= tris[0] <= hi else '  <-- outside %s budget %d-%d' % (budget, lo, hi)
        report.append('%-36s %s%s' % (name, ' / '.join(str(t) for t in tris), flag))
        entry = {
            'asset': key, 'variant': var, 'state': st if key not in ('Field', 'ReedBoat', 'Ornament') else None,
            'title': info['title'],
            'footprint_m': [round(info['W'], 2), round(info['D'], 2)], 'height_m': round(info['H'], 2),
            'triangles': {'LOD%d' % i: t for i, t in enumerate(tris)},
            'slots': [{'slot': G.SLOT_NAMES[i], 'material': 'M_Age1_' + slotmats[i]} for i in order],
            'textures': sorted({t for i in order for t in manifest['materials']['M_Age1_' + slotmats[i]]['textures']}),
            'fbx': 'art/age1/export/%s.fbx' % name, 'licence': 'original', 'script': 'art/age1/build_age1.py',
            'budget_class': budget,
        }
        if info.get('sockets'):
            entry['sockets'] = info['sockets']
        manifest['meshes'][name] = entry
        if '--no-fbx' not in ARGS:
            bpy.ops.object.select_all(action='DESELECT')
            empty.select_set(True)
            for ob in objs:
                ob.select_set(True)
            bpy.context.view_layer.objects.active = empty
            bpy.ops.export_scene.fbx(filepath=os.path.join(EXPDIR, name + '.fbx'), use_selection=True,
                                     object_types={'EMPTY', 'MESH'}, mesh_smooth_type='FACE', use_tspace=True,
                                     apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', add_leaf_bones=False,
                                     bake_space_transform=False, use_custom_props=False, colors_type='SRGB',
                                     path_mode='STRIP', embed_textures=False, axis_forward='-Z', axis_up='Y')
        print('[mesh]', report[-1], flush=True)
    # states and cultures summary per asset
    by_asset = {}
    for n, e in manifest['meshes'].items():
        by_asset.setdefault(e['asset'], []).append(n)
    manifest['assets'] = {}
    for k, names in by_asset.items():
        manifest['assets'][k] = {'meshes': names,
                                 'states': sorted({manifest['meshes'][n]['state'] for n in names if manifest['meshes'][n]['state']},
                                                  key=lambda s: A.STATES.index(s)),
                                 'culture_variants': list(CULTURES)}
    manifest['sim_mapping'] = {
        'b.house (Bronze, tiers 1-3)': 'SM_Age1_House_{A,B,C,D}_<State>; CliffTerrace prefers C; RingBuilder/LagoonWeaver A, B, D',
        'b.granary': 'SM_Age1_Granary_A_<State>',
        'b.temple': 'SM_Age1_Temple_A_<State> + SM_Age1_TempleCourt_A_<State> (same pivot)',
        'b.walls (Bronze)': 'SM_Age1_Rampart_Straight / Rampart_End / Gate_A along the ring; SM_Age1_Palisade_Straight for small villages',
        'fields': 'SM_Age1_Field_{Ripe,Growing,Ploughed,Stubble} with MI_Age1_Crop_{Ripe,Green,Stubble}',
        'port / landing': 'SM_Age1_Landing_A_<State> + SM_Age1_ReedBoat_{A,B} at its mooring sockets',
        'Burnt': 'Damaged / Ruin mesh with the MI_Age1_*_Burnt instances',
        'TownKit stand-ins replaced': {'MudHouse': 'House_A..D', 'Granary(V0)': 'Granary_A', 'TempleEarly': 'Temple_A',
                                       'WallSeg(V0)': 'Rampart_Straight', 'Gatehouse(V0)': 'Gate_A', 'Pier(V0)': 'Landing_A', 'Boat': 'ReedBoat_A/B'},
    }
    manifest['total_triangles_lod0'] = sum(e['triangles']['LOD0'] for e in manifest['meshes'].values())
    if not ONLY:
        with open(os.path.join(HERE, 'manifest.json'), 'w') as f:
            json.dump(manifest, f, indent=1)
    os.makedirs(SRCDIR, exist_ok=True)
    blend = os.path.join(SRCDIR, 'age1.blend' if not ONLY else 'age1_partial.blend')
    bpy.ops.file.make_paths_relative() if bpy.data.filepath else None
    bpy.ops.wm.save_as_mainfile(filepath=blend, relative_remap=True, compress=True)
    bpy.ops.file.make_paths_relative()
    bpy.ops.wm.save_mainfile(compress=True)
    print('\n'.join(report))
    print('[done] %d meshes, LOD0 total %d tris, %.1f s' % (len(manifest['meshes']), manifest['total_triangles_lod0'], time.time() - t0))


build()
