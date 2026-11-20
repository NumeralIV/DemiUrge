# DEMIURGE art - Age0 (Stone Age) family build script.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age0/build_age0.py -- [--no-tex] [--only Hut_A,FirePit_A]
#
# Writes: art/age0/textures/T_Age0_*.png, art/age0/export/SM_Age0_*.fbx (one file per mesh with LOD0-3 under a
# LodGroup), art/age0/source/age0.blend (all meshes + preview materials), art/age0/manifest.json.
# Deterministic: every run rebuilds the same files.
import os
import sys
import json
import math
import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
import bmesh  # noqa: E402
import a0_assets as AS  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
TEX_DIR = os.path.join(HERE, 'textures')
EXP_DIR = os.path.join(HERE, 'export')
SRC_DIR = os.path.join(HERE, 'source')

LOD_SCREEN = [1.0, 0.35, 0.12, 0.04]
MATS = ['M_Age0_Structure', 'M_Age0_Roof', 'M_Age0_Detail']
TEXSETS = {'M_Age0_Structure': 'Structure', 'M_Age0_Roof': 'Roof', 'M_Age0_Detail': 'Detail'}
CLIMATES = {
    # name: per-slot (tint, moss amount); tint multiplies the albedo
    'Temperate': {'Structure': ((1.0, 1.0, 1.0), 0.35), 'Roof': ((1.0, 1.0, 1.0), 0.3), 'Detail': ((1.0, 1.0, 1.0), 0.4)},
    'Arid': {'Structure': ((1.25, 1.02, 0.8), 0.0), 'Roof': ((1.32, 1.16, 0.86), 0.0), 'Detail': ((1.15, 1.02, 0.88), 0.05)},
    'Tropical': {'Structure': ((0.88, 0.84, 0.76), 0.45), 'Roof': ((1.08, 1.06, 0.66), 0.4), 'Detail': ((0.95, 0.95, 0.88), 0.6)},
    'Cold': {'Structure': ((0.84, 0.84, 0.88), 0.3), 'Roof': ((0.62, 0.6, 0.58), 0.3), 'Detail': ((0.9, 0.92, 0.95), 0.45)},
    'Burnt': {'Structure': ((0.13, 0.115, 0.1), 0.0), 'Roof': ((0.12, 0.1, 0.09), 0.0), 'Detail': ((0.35, 0.33, 0.31), 0.0)},
}
MOSS = {'Temperate': (0.10, 0.12, 0.05), 'Arid': (0.5, 0.42, 0.3), 'Tropical': (0.08, 0.13, 0.04), 'Cold': (0.2, 0.21, 0.19), 'Burnt': (0.05, 0.05, 0.05)}
CULTURE_COLORS = ['#e69f00', '#56b4e9', '#009e73', '#f0e442', '#d55e00', '#cc79a7']

H = AS.DAMAGEABLE
I = AS.INTACT_ONLY
# asset id, variant, builder, states, category (budget), kit mapping (EDmTownMesh, variant) for the integration agent, note
REGISTRY = [
    ('Hut', 'A', lambda s, q: AS.hut('A', s, q), H, 'house', [('Hut', 0)], 'thatch beehive dome, 4.6 m: the concept camp hut'),
    ('Hut', 'B', lambda s, q: AS.hut('B', s, q), H, 'house', [('Hut', 1)], 'conical thatch on a wattle-and-daub drum, 6 m'),
    ('Hut', 'C', lambda s, q: AS.hut('C', s, q), H, 'house', [('Hut', 2), ('Tent', 1)], 'hide-covered bender dome, thatch cap, smoke hole, 5.4 m'),
    ('LeanTo', 'A', lambda s, q: AS.leanto(s, q), H, 'house', [('Tent', 2), ('Tent', 0)], 'pole lean-to, thatch and hides, hearth in front'),
    ('Longhouse', 'A', lambda s, q: AS.longhouse('A', s, q), H, 'house', [('Longhouse', 0)], '16 m bow-walled longhouse, hipped thatch'),
    ('Longhouse', 'B', lambda s, q: AS.longhouse('B', s, q), H, 'civic', [('Longhouse', 1)], '22 m bow-walled longhouse (clan hall)'),
    ('FirePit', 'A', lambda s, q: AS.firepit(s, q), I, 'prop', [('Campfire', 0)], 'stone-ringed hearth, log tipi, embers (M.A), seat logs'),
    ('DryingRack', 'A', lambda s, q: AS.dryingrack('A', s, q), I, 'prop', [], 'A-frame rack with two stretched hides (one painted)'),
    ('DryingRack', 'B', lambda s, q: AS.dryingrack('B', s, q), I, 'prop', [], 'long tripod rack with draped hides and drying strips'),
    ('Palisade', 'Seg', lambda s, q: AS.palisade_seg(s, q), H, 'wall', [('WallSeg', 0)], '8 m module of sharpened logs on an earth bank; tiles along X'),
    ('Palisade', 'Gate', lambda s, q: AS.palisade_gate(s, q), H, 'civic', [('Gatehouse', 0)], '8 m module: post-and-lintel gate, two log leaves, culture banner'),
    ('StoragePit', 'A', lambda s, q: AS.storagepit('A', s, q), I, 'prop', [('Granary', 0)], 'clay-rimmed pit under a thatch cap'),
    ('StoragePit', 'B', lambda s, q: AS.storagepit('B', s, q), I, 'prop', [], 'open pit with split-log lid and a grinding stone'),
    ('StandingStones', 'A', lambda s, q: AS.standingstones('A', s, q), ('Intact', 'Ruin'), 'civic', [('Stones', 0)], 'ring of twelve menhirs, 16 m, with an altar stone'),
    ('StandingStone', 'A', lambda s, q: AS.standingstones('B', s, q), ('Intact', 'Ruin'), 'prop', [], 'single menhir with kerb stones'),
    ('Ornament', 'Pennant', lambda s, q: AS.ornament('Pennant', s, q), I, 'prop', [], 'culture pennant pole (painted hide, M.G)'),
    ('Ornament', 'Totem', lambda s, q: AS.ornament('Totem', s, q), I, 'prop', [], 'carved totem post with painted bands (M.G)'),
]
BUDGET = {'house': (2000, 6000), 'civic': (8000, 20000), 'prop': (50, 1000), 'wall': (300, 2500)}


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'tex': True, 'only': None}
    i = 0
    while i < len(a):
        if a[i] == '--no-tex':
            o['tex'] = False
        elif a[i] == '--only':
            o['only'] = set(a[i + 1].split(',')); i += 1
        i += 1
    return o


def clean_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    s = bpy.context.scene
    s.unit_settings.system = 'METRIC'
    s.unit_settings.scale_length = 1.0


def hexlin(h):
    h = h.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def load_img(name, colour):
    path = os.path.join(TEX_DIR, name + '.png')
    img = bpy.data.images.get(name + '.png') or bpy.data.images.load(path, check_existing=True)
    img.colorspace_settings.name = 'sRGB' if colour else 'Non-Color'
    return img


def make_material(name, texset, tint=(1, 1, 1), moss_amt=0.3, moss=(0.3, 0.33, 0.17), culture='#e69f00', night=0.0, fire=6.0):
    """Preview material mirroring what the UE master material does with the same maps and parameters."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    m.use_backface_culling = True
    nt = m.node_tree
    nt.nodes.clear()
    N = nt.nodes.new; L = nt.links.new
    out = N('ShaderNodeOutputMaterial'); out.location = (900, 0)
    bs = N('ShaderNodeBsdfPrincipled'); bs.location = (600, 0)
    uv = N('ShaderNodeUVMap'); uv.uv_map = 'UVMap'; uv.location = (-900, 0)
    def tex(suffix, colour, y):
        t = N('ShaderNodeTexImage'); t.location = (-650, y)
        t.image = load_img('T_Age0_%s_%s' % (texset, suffix), colour)
        t.interpolation = 'Linear'
        L(uv.outputs['UV'], t.inputs['Vector'])
        return t
    tD = tex('D', True, 300); tN = tex('N', False, -300); tO = tex('ORM', False, 0); tM = tex('M', False, -600)
    sO = N('ShaderNodeSeparateColor'); sO.location = (-350, 0); L(tO.outputs['Color'], sO.inputs['Color'])
    sM = N('ShaderNodeSeparateColor'); sM.location = (-350, -600); L(tM.outputs['Color'], sM.inputs['Color'])
    # albedo * tint, moss blend (M.B), culture paint (M.G), AO
    mt = N('ShaderNodeMix'); mt.data_type = 'RGBA'; mt.blend_type = 'MULTIPLY'; mt.location = (-150, 300)
    mt.inputs['Factor'].default_value = 1.0
    L(tD.outputs['Color'], mt.inputs['A']); mt.inputs['B'].default_value = (*tint, 1.0)
    mm = N('ShaderNodeMath'); mm.operation = 'MULTIPLY'; mm.location = (-150, -500)
    L(sM.outputs['Blue'], mm.inputs[0]); mm.inputs[1].default_value = moss_amt
    mo = N('ShaderNodeMix'); mo.data_type = 'RGBA'; mo.blend_type = 'MIX'; mo.location = (50, 300)
    L(mm.outputs['Value'], mo.inputs['Factor']); L(mt.outputs['Result'], mo.inputs['A']); mo.inputs['B'].default_value = (*moss, 1.0)
    cc = N('ShaderNodeMix'); cc.data_type = 'RGBA'; cc.blend_type = 'MULTIPLY'; cc.location = (220, 300)
    L(sM.outputs['Green'], cc.inputs['Factor']); L(mo.outputs['Result'], cc.inputs['A'])
    cc.inputs['B'].default_value = (*hexlin(culture), 1.0)
    ao = N('ShaderNodeMix'); ao.data_type = 'RGBA'; ao.blend_type = 'MULTIPLY'; ao.location = (400, 300)
    ao.inputs['Factor'].default_value = 0.6
    L(cc.outputs['Result'], ao.inputs['A']); L(sO.outputs['Red'], ao.inputs['B'])
    L(ao.outputs['Result'], bs.inputs['Base Color'])
    L(sO.outputs['Green'], bs.inputs['Roughness'])
    bs.inputs['Metallic'].default_value = 0.0
    nm = N('ShaderNodeNormalMap'); nm.location = (300, -300)
    L(tN.outputs['Color'], nm.inputs['Color']); L(nm.outputs['Normal'], bs.inputs['Normal'])
    # emission: M.A fire (always on) + M.R night-lit openings
    ef = N('ShaderNodeMath'); ef.operation = 'MULTIPLY'; ef.location = (-150, -800)
    L(tM.outputs['Alpha'], ef.inputs[0]); ef.inputs[1].default_value = fire
    en = N('ShaderNodeMath'); en.operation = 'MULTIPLY_ADD'; en.location = (50, -800)
    L(sM.outputs['Red'], en.inputs[0]); en.inputs[1].default_value = night; L(ef.outputs['Value'], en.inputs[2])
    bs.inputs['Emission Color'].default_value = (1.0, 0.42, 0.12, 1.0)
    L(en.outputs['Value'], bs.inputs['Emission Strength'])
    L(bs.outputs['BSDF'], out.inputs['Surface'])
    m['dm_tint'] = list(tint); m['dm_moss'] = moss_amt; m['dm_culture'] = culture
    m.use_fake_user = True
    return m


def build_materials():
    for base, ts in TEXSETS.items():
        make_material(base, ts)
        for cl, slots in CLIMATES.items():
            tint, moss = slots[ts]
            make_material('MI_Age0_%s_%s' % (ts, cl), ts, tint=tint, moss_amt=moss, moss=MOSS[cl], fire=6.0 if cl != 'Burnt' else 1.5)


def weld(me, dist=1e-4):
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=dist)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-6)
    # vertices left without faces by the generators' keep() filters (damage holes, ruins) would ship as loose points
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.to_mesh(me)
    bm.free()
    me.validate()
    me.update()


def make_object(mb, name, coll):
    me = mb.to_mesh(name)
    weld(me)
    try:
        me.set_sharp_from_angle(angle=math.radians(55))
    except Exception:
        pass
    for mn in MATS:
        me.materials.append(bpy.data.materials[mn])
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    me.calc_loop_triangles()
    return ob, len(me.loop_triangles)


def export_fbx(group, children, path):
    bpy.ops.object.select_all(action='DESELECT')
    for o in [group] + children:
        o.hide_set(False)
        o.select_set(True)
    bpy.context.view_layer.objects.active = group
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'EMPTY', 'MESH'},
                             apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS', bake_space_transform=False,
                             mesh_smooth_type='FACE', use_tspace=True, use_mesh_modifiers=True, use_triangles=False,
                             add_leaf_bones=False, bake_anim=False, path_mode='STRIP', embed_textures=False,
                             axis_forward='-Z', axis_up='Y')
    bpy.ops.object.select_all(action='DESELECT')


def main():
    o = args()
    os.makedirs(EXP_DIR, exist_ok=True)
    os.makedirs(SRC_DIR, exist_ok=True)
    if o['tex']:
        import a0_tex
        a0_tex.build_all(TEX_DIR)
    clean_scene()
    build_materials()
    root = bpy.context.scene.collection
    manifest = {
        'family': 'Age0',
        'generated': datetime.date.today().isoformat(),
        'script': 'art/age0/build_age0.py (with a0_geo.py, a0_assets.py, a0_tex.py)',
        'licence': 'original',
        'units': 'metres in Blender, FBX unit scale to centimetres; Z up, front -Y, pivot = footprint centre at ground',
        'lod_screen_sizes': LOD_SCREEN,
        'material_slots': ['Structure', 'Roof', 'Detail'],
        'materials': {
            'M_Age0_Structure': {'textures': ['T_Age0_Structure_%s' % s for s in ('D', 'N', 'ORM', 'M')],
                                 'trim_bands_v': {'bark': [0.0, 0.25], 'weathered_wood': [0.25, 0.5], 'wattle_daub': [0.5, 0.75], 'earth': [0.75, 1.0]}},
            'M_Age0_Roof': {'textures': ['T_Age0_Roof_%s' % s for s in ('D', 'N', 'ORM', 'M')],
                            'trim_bands_v': {'thatch_course': [0.0, 0.5], 'hide_panels': [0.5, 1.0]}},
            'M_Age0_Detail': {'textures': ['T_Age0_Detail_%s' % s for s in ('D', 'N', 'ORM', 'M')],
                              'trim_bands_v': {'stone': [0.0, 0.5], 'leather': [0.5, 0.625], 'painted_hide': [0.625, 0.75],
                                               'patches(u: embers 0-.25, char .25-.5, interior .5-1)': [0.75, 0.875], 'rope': [0.875, 1.0]}},
        },
        'texture_channels': {'D': 'sRGB albedo', 'N': 'tangent normal, OpenGL (+Y): flip green on UE import',
                             'ORM': 'R ambient occlusion, G roughness, B metallic (0)',
                             'M': 'R night emissive (doorways, smoke holes, vents), G culture-colour paint mask, B moss/grime mask, A always-on fire (embers)'},
        'material_parameters': {'Tint': 'RGB multiplier on albedo', 'MossAmount': 'scales M.B', 'MossColor': 'linear RGB',
                                'CultureColor': 'per instance (custom data), multiplies albedo where M.G', 'NightGlow': 'scales M.R emission at night',
                                'FireGlow': 'scales M.A emission, always on'},
        'material_instances': {},
        'culture_colors': CULTURE_COLORS,
        'meshes': {},
    }
    for cl, slots in CLIMATES.items():
        for ts in ('Structure', 'Roof', 'Detail'):
            tint, moss = slots[ts]
            manifest['material_instances']['MI_Age0_%s_%s' % (ts, cl)] = {'parent': 'M_Age0_%s' % ts, 'Tint': list(tint), 'MossAmount': moss,
                                                                         'MossColor': list(MOSS[cl]),
                                                                         'use': 'Burnt state override' if cl == 'Burnt' else 'climate style %s (DmTownsPlan EStyle)' % cl}
    report = []
    for asset, var, fn, states, cat, kit, note in REGISTRY:
        key = '%s_%s' % (asset, var)
        if o['only'] and key not in o['only']:
            continue
        coll = bpy.data.collections.new('Age0_' + key)
        root.children.link(coll)
        for st in states:
            name = 'SM_Age0_%s_%s' % (key, st) if len(states) > 1 else 'SM_Age0_%s' % key
            grp = bpy.data.objects.new(name, None)
            grp['fbx_type'] = 'LodGroup'
            grp['dm_asset'] = key; grp['dm_state'] = st
            coll.objects.link(grp)
            kids, tris = [], []
            bb = None
            for q in range(4):
                mb = fn(st, q)
                ob, nt = make_object(mb, '%s_LOD%d' % (name, q), coll)
                ob.parent = grp
                ob['dm_lod'] = q
                kids.append(ob); tris.append(nt)
                if q == 0:
                    bb = mb.bounds()
                    used = sorted(set(mb.M))
            export_fbx(grp, kids, os.path.join(EXP_DIR, name + '.fbx'))
            (x0, y0, z0), (x1, y1, z1) = bb
            lo, hi = BUDGET[cat]
            entry = {
                'asset': asset, 'variant': var, 'state': st, 'states': list(states), 'category': cat,
                'footprint_m': [round(x1 - x0, 2), round(y1 - y0, 2)], 'height_m': round(z1, 2), 'below_ground_m': round(-z0, 2),
                'triangles': tris, 'lod_ratio': [round(t / max(1, tris[0]), 3) for t in tris],
                'budget_lod0': [lo, hi], 'within_budget': tris[0] <= hi,
                'materials': MATS, 'slots_used': [['Structure', 'Roof', 'Detail'][i] for i in used],
                'textures': ['T_Age0_%s_%s' % (['Structure', 'Roof', 'Detail'][i], s) for i in used for s in ('D', 'N', 'ORM', 'M')],
                'pivot': 'footprint centre at ground (Z = 0), front (door) toward -Y',
                'culture_variants': ['MI_Age0_*_%s' % c for c in ('Temperate', 'Arid', 'Tropical', 'Cold')] + ['CultureColor per instance (M.G)'],
                'state_material_override': 'MI_Age0_*_Burnt' if st == 'Burnt' else None,
                'kit_mapping': [{'EDmTownMesh': k, 'variant': v} for k, v in kit],
                'export': 'art/age0/export/%s.fbx' % name, 'licence': 'original', 'script': 'art/age0/build_age0.py',
                'note': note,
            }
            if asset == 'Palisade':
                entry['tiling'] = ('module length 8 m along X, outer face -Y; place end to end, scale X for other lengths '
                                   '(the kit WallSeg is a 16 m stretch: two modules, or X scale = length / 8 m)')
            manifest['meshes'][name] = entry
            report.append('%-34s %6d %6d %6d %6d  %s' % (name, tris[0], tris[1], tris[2], tris[3], '' if tris[0] <= hi else 'OVER %d' % hi))
            print(report[-1], flush=True)
    mp = os.path.join(HERE, 'manifest.json')
    if o['only'] and os.path.exists(mp):
        old = json.load(open(mp))
        old['meshes'].update(manifest['meshes'])
        manifest['meshes'] = old['meshes']
    with open(mp, 'w') as f:
        json.dump(manifest, f, indent=1)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SRC_DIR, 'age0.blend' if not o['only'] else 'age0_partial.blend'), compress=True)
    print('BUILD DONE', len(report), 'meshes')


if __name__ == '__main__':
    main()
