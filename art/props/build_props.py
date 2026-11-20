# DEMIURGE props - builds every mesh of the Prop family (rocks, cliffs, fences, hedgerows, crops, hay, barrels,
# carts, market goods, signposts, lamps per age, statues) with LODs, the tiling texture sets, the baked rock maps,
# the FBX exports, the .blend source and art/props/manifest.json.
#
#   blender.exe -b --factory-startup --python art/props/build_props.py -- [--textures] [--only A,B] [--nobake]
#
# --textures   regenerate the tiling sets in art/props/textures (about 40 s; otherwise reused)
# --only       rebuild only these assets (manifest names without SM_Prop_, e.g. Rock_A,Lamp_Age4); the
#              others are kept from the existing source .blend and manifest
# --nobake     skip the Cycles bakes of rocks and cliffs (keeps existing baked maps)
# Run it through the render slot tool (it bakes with Cycles):
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b
#       --factory-startup --python art/props/build_props.py -- ...
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))

import bpy  # noqa: E402

import props_lib as L  # noqa: E402

TEX = os.path.join(HERE, 'textures')
EXPORT = os.path.join(HERE, 'export')
SOURCE = os.path.join(HERE, 'source', 'props.blend')
MANIFEST = os.path.join(HERE, 'manifest.json')
L.TEX_DIR = TEX
SCREEN = [1.0, 0.35, 0.12, 0.04]

# preview-side and UE-side material instances (tints are linear multipliers of the base colour)
INSTANCES = {
    'M_Prop_Rock': {'Granite': (1.0, 1.0, 1.0), 'Sandstone': (1.32, 0.86, 0.62), 'Basalt': (0.5, 0.5, 0.53),
                    'Limestone': (1.38, 1.33, 1.22)},
    'M_Prop_RockTile': {'Granite': (1.0, 1.0, 1.0), 'Sandstone': (1.32, 0.86, 0.62), 'Basalt': (0.5, 0.5, 0.53),
                        'Limestone': (1.38, 1.33, 1.22)},
    'M_Prop_Wood': {'Valley': (1.0, 1.0, 1.0), 'CliffTerrace': (1.12, 0.94, 0.8), 'LagoonWeaver': (0.78, 0.82, 0.86),
                    'RingBuilder': (0.92, 0.94, 0.98)},
    'M_Prop_Stone': {'Valley': (1.0, 1.0, 1.0), 'CliffTerrace': (1.08, 0.8, 0.68), 'LagoonWeaver': (0.86, 0.9, 0.92),
                     'RingBuilder': (1.06, 1.07, 1.1), 'Marble': (1.18, 1.17, 1.14), 'Concrete': (0.82, 0.82, 0.8),
                     'Terracotta': (1.15, 0.62, 0.42)},
    'M_Prop_Cloth': {'Valley': (0.82, 0.34, 0.24), 'CliffTerrace': (0.22, 0.32, 0.72), 'LagoonWeaver': (1.05, 0.66, 0.2),
                     'RingBuilder': (0.52, 0.42, 0.6), 'Burlap': (0.88, 0.74, 0.55), 'Undyed': (1.0, 1.0, 1.0)},
    'M_Prop_Straw': {'Ripe': (1.0, 1.0, 1.0), 'Weathered': (0.72, 0.68, 0.62)},
    'M_Prop_Crop': {'Ripe': (1.0, 1.0, 1.0), 'Green': (0.55, 0.85, 0.32), 'Barley': (1.08, 1.0, 0.82)},
    'M_Prop_Foliage': {'Summer': (1.0, 1.0, 1.0), 'Autumn': (1.5, 0.95, 0.45), 'Dry': (1.3, 1.1, 0.6),
                       'Cabbage': (0.8, 1.05, 1.0)},
    'M_Prop_Metal': {'Iron': (1.0, 1.0, 1.0), 'PaintedGreen': (0.42, 0.7, 0.5), 'PaintedBlack': (0.45, 0.45, 0.45)},
    'M_Prop_Steel': {'Brushed': (1.0, 1.0, 1.0), 'Galvanised': (0.85, 0.87, 0.88), 'PaintedWhite': (2.6, 2.6, 2.55),
                     'PaintedGrey': (0.75, 0.76, 0.78)},
    'M_Prop_Glow': {'Fire': (1.0, 0.42, 0.1), 'Oil': (1.0, 0.62, 0.3), 'Gas': (1.0, 0.82, 0.58),
                    'Sodium': (1.0, 0.55, 0.16), 'LED': (0.85, 0.92, 1.0)},
}
# instance parameter overrides besides the tint (UE: MetallicScale, RoughnessScale)
INSTANCE_PARAMS = {
    'M_Prop_Metal': {'PaintedGreen': {'metallic': 0.0, 'roughness': 0.8},
                     'PaintedBlack': {'metallic': 0.0, 'roughness': 0.75}},
    'M_Prop_Steel': {'PaintedWhite': {'metallic': 0.0, 'roughness': 1.4},
                     'PaintedGrey': {'metallic': 0.0, 'roughness': 1.5},
                     'Galvanised': {'metallic': 1.0, 'roughness': 1.6}},
}
TEXSET = {'M_Prop_Rock': 'Rock', 'M_Prop_RockTile': 'Rock', 'M_Prop_Steel': 'Steel', 'M_Prop_Wood': 'Wood', 'M_Prop_Stone': 'Stone', 'M_Prop_Cloth': 'Cloth',
          'M_Prop_Straw': 'Straw', 'M_Prop_Crop': 'Stalk', 'M_Prop_Foliage': 'Leaf', 'M_Prop_Metal': 'Metal'}


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'textures': False, 'only': None, 'bake': True}
    i = 0
    while i < len(a):
        if a[i] == '--textures':
            o['textures'] = True
        elif a[i] == '--nobake':
            o['bake'] = False
        elif a[i] == '--only':
            o['only'] = set(a[i + 1].split(','))
            i += 1
        i += 1
    return o


def ensure_textures(force):
    import props_tex as PT
    os.makedirs(TEX, exist_ok=True)
    info = {}
    for nm in PT.SETS:
        if force or not os.path.exists(os.path.join(TEX, 'T_Prop_%s_D.png' % nm)):
            info[nm] = PT.build(nm, TEX)
    return info


def setup_materials():
    for mname, tex in TEXSET.items():
        m = L.get_mat(mname)
        L.setup_material(m, tex, masked=mname in L.MASKED, moss=mname in ('M_Prop_Rock', 'M_Prop_RockTile'))
    L.setup_material(L.get_mat('M_Prop_Glow'), None, emit=(1.0, 0.6, 0.3, 12.0))


def lod_group(full, lods, coll):
    e = bpy.data.objects.new(full, None)
    e.empty_display_type = 'PLAIN_AXES'
    e['fbx_type'] = 'LodGroup'
    coll.objects.link(e)
    for i, o in enumerate(lods):
        o.parent = e
        if i > 0:
            o.hide_set(True)
            o.hide_render = True
    return e


def mesh_info(o):
    xs = [v.co.x for v in o.data.vertices]
    ys = [v.co.y for v in o.data.vertices]
    zs = [v.co.z for v in o.data.vertices]
    return dict(footprint_m=[round(max(xs) - min(xs), 2), round(max(ys) - min(ys), 2)],
                height_m=round(max(zs), 2), below_ground_m=round(-min(zs), 2) if min(zs) < 0 else 0.0)


def main():
    t0 = time.time()
    o = args()
    os.makedirs(EXPORT, exist_ok=True)
    os.makedirs(os.path.dirname(SOURCE), exist_ok=True)
    tex_info = ensure_textures(o['textures'])
    import props_models as PM
    registry = PM.registry()
    names = list(registry)
    if o['only'] and os.path.exists(SOURCE):
        bpy.ops.wm.open_mainfile(filepath=SOURCE)
        todo = [n for n in names if n in o['only']]
    else:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        todo = [n for n in names if not o['only'] or n in o['only']]
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1.0
    root = bpy.data.collections.get('Props') or bpy.data.collections.new('Props')
    if root.name not in scene.collection.children:
        scene.collection.children.link(root)
    setup_materials()
    man = {}
    if os.path.exists(MANIFEST) and o['only']:
        with open(MANIFEST) as f:
            man = {m['name']: m for m in json.load(f)['meshes']}
    for n in todo:
        spec = registry[n]
        full = 'SM_Prop_' + n
        # remove an older build of this asset
        old = bpy.data.collections.get(full)
        if old:
            for ob in list(old.objects):
                me = ob.data
                bpy.data.objects.remove(ob)
                if me is not None and me.users == 0:
                    bpy.data.meshes.remove(me)
            bpy.data.collections.remove(old)
        coll = bpy.data.collections.new(full)
        root.children.link(coll)
        t1 = time.time()
        lods, extra = spec['build'](n, coll, o['bake'])
        grp = lod_group(full, lods, coll)
        path = os.path.join(EXPORT, full + '.fbx')
        L.export_fbx(path, [grp] + lods)
        info = mesh_info(lods[0])
        tris = [L.tri_count(x) for x in lods]
        mats = []
        roles = lods[0].get('roles', ','.join(spec.get('roles', ['Structure']))).split(',')
        for i, slot in enumerate(lods[0].material_slots):
            mats.append({'index': i, 'slot': roles[i] if i < len(roles) else 'Detail',
                         'material': slot.material.name})
        entry = {
            'name': full, 'family': 'Prop', 'category': spec['category'], 'description': spec['desc'],
            'fbx': 'art/props/export/%s.fbx' % full, 'lods': len(lods), 'tris': tris,
            'lod_screen_sizes': SCREEN[:len(lods)], **info,
            'materials': mats,
            'textures': sorted(set(sum((spec_textures(m['material']) for m in mats), [])) | set(extra.get('textures', []))),
            'material_instances': {m['material']: sorted(INSTANCES.get(m['material'], {}))
                                   for m in mats if m['material'] in INSTANCES},
            'ages': spec.get('ages', 'any'), 'cultures': spec.get('cultures', 'any'),
            'state': spec.get('state', 'Intact'), 'states_of': spec.get('states_of'),
            'variant_group': spec.get('group'),
            'placement': spec.get('placement', ''),
            'vertex_colour': 'Col: RGB = albedo tint multiplier, A = moss / grime mask',
            'uv': 'UV0 UVMap = tiling (texture tiles), UV1 UVUnique = 0..1 non-overlapping (bake / lightmap)',
            'licence': 'original', 'script': 'art/props/build_props.py',
        }
        for k in ('tile_length_m', 'emissive', 'budget_note'):
            if k in spec:
                entry[k] = spec[k]
        entry.update(extra.get('manifest', {}))
        man[full] = entry
        print('PROP %-34s tris %-24s %.1fs' % (full, tris, time.time() - t1), flush=True)
    # state groups: every mesh of a group lists the meshes of all its states
    groups = {}
    for e in man.values():
        if e.get('variant_group'):
            groups.setdefault(e['variant_group'], {})[e.get('state', 'Intact')] = e['name']
    for e in man.values():
        if e.get('variant_group') in groups:
            e['states'] = groups[e['variant_group']]
    bpy.ops.wm.save_as_mainfile(filepath=SOURCE, compress=True)
    out = {'family': 'Prop', 'generated_by': 'art/props/build_props.py', 'blender': bpy.app.version_string,
           'units': 'metres, Z up, front -Y, pivot = footprint centre at ground (Z = 0)',
           'texture_sets': texture_sets(),
           'material_instances': {k: {c: {'tint': list(v), **INSTANCE_PARAMS.get(k, {}).get(c, {})}
                                      for c, v in d.items()} for k, d in INSTANCES.items()},
           'meshes': [man[k] for k in sorted(man)]}
    with open(MANIFEST, 'w') as f:
        json.dump(out, f, indent=1)
    print('PROPS done: %d assets built, %d in manifest, %.0f s' % (len(todo), len(man), time.time() - t0))


def spec_textures(mat):
    t = TEXSET.get(mat)
    if not t:
        return []
    r = ['T_Prop_%s_D' % t, 'T_Prop_%s_N' % t, 'T_Prop_%s_ORM' % t]
    if mat in ('M_Prop_Rock', 'M_Prop_RockTile'):
        r += ['T_Prop_Moss_D', 'T_Prop_Moss_N', 'T_Prop_Moss_ORM']
    return r


def texture_sets():
    import props_tex as PT
    d = {}
    for nm in PT.SETS:
        d['T_Prop_' + nm] = {'tile_m': L.TILE[nm], 'size': 1024, 'maps': ['D', 'N', 'ORM'],
                             'alpha_in_D': nm in ('Leaf', 'Stalk')}
    return d


if __name__ == '__main__':
    main()
