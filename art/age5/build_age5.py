# DEMIURGE art/age5 - build script for the Atomic Age kit (Age5).
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/age5/build_age5.py -- [--textures] [--only <substring>] [--no-blend]
#
# Writes art/age5/textures/*.png (with --textures, or when missing), art/age5/export/<mesh>.fbx (LOD0-3 and
# sockets), art/age5/source/age5.blend and art/age5/manifest.json. Everything is generated; never hand-edit.
import json
import math
import os
import sys
import time

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import age5_geo as G       # noqa: E402
import age5_assets as A    # noqa: E402
import age5_textures as TX  # noqa: E402

TEX_DIR = os.path.join(HERE, 'textures')
EXPORT_DIR = os.path.join(HERE, 'export')
SOURCE_DIR = os.path.join(HERE, 'source')
FAMILY = 'Age5'
ALL4 = ['Intact', 'Construction', 'Damaged', 'Ruin']

CULTURES = {
    # material-instance parameters (linear multipliers on the culture-tint mask, and on the roof)
    'Neutral': {'StructureTint': (1.0, 1.0, 1.0), 'RoofTint': (1.0, 1.0, 1.0), 'DetailTint': (1.0, 1.0, 1.0), 'RoughnessAdd': 0.0},
    # Cliff-Terrace: warm ochre and rose plaster
    'CultureA': {'StructureTint': (1.12, 0.86, 0.66), 'RoofTint': (1.05, 0.88, 0.72), 'DetailTint': (1.04, 0.95, 0.86), 'RoughnessAdd': 0.04},
    # Lagoon-Weaver: silver-grey and aqua
    'CultureB': {'StructureTint': (0.82, 0.98, 0.98), 'RoofTint': (0.80, 0.90, 0.88), 'DetailTint': (0.92, 1.0, 1.0), 'RoughnessAdd': -0.04},
    # Ring-Builder: pale cool limestone and blue-grey slate
    'CultureC': {'StructureTint': (1.10, 1.10, 1.16), 'RoofTint': (0.62, 0.68, 0.80), 'DetailTint': (0.96, 0.98, 1.04), 'RoughnessAdd': 0.0},
}
CROWN_FOR = {'CultureA': 'SM_Age5_Crown_CultureA', 'CultureB': 'SM_Age5_Crown_CultureB', 'CultureC': 'SM_Age5_Crown_CultureC'}

# asset table ----------------------------------------------------------------------------------------------
ASSETS = [
    dict(asset='ApartmentSlab', fn=A.apt_slab, cls='building', budget=(800, 20000),
         variants=[('Low', {'storeys': 9}), ('Mid', {'storeys': 12}), ('High', {'storeys': 16})], states=ALL4,
         kit='EDmTownMesh::Apartment', desc='Precast concrete slab block, 36 x 12 m, balconies, blank gable ends.'),
    dict(asset='ApartmentPoint', fn=A.apt_point, cls='building', budget=(800, 20000),
         variants=[('Low', {'storeys': 14}), ('Mid', {'storeys': 20}), ('High', {'storeys': 26})], states=ALL4,
         kit='EDmTownMesh::Apartment', desc='Cruciform point tower, 21 x 21 m, corner loggias, roof plant and mast.'),
    dict(asset='ApartmentBrick', fn=A.apt_brick, cls='building', budget=(800, 20000),
         variants=[('Low', {'storeys': 5}), ('Mid', {'storeys': 7}), ('High', {'storeys': 9})], states=ALL4,
         kit='EDmTownMesh::Apartment', desc='Brick gallery-access block, 30 x 15 m, oriel bays, shops at street level.'),
    dict(asset='OfficeGlass', fn=A.office_glass, cls='building', budget=(800, 20000),
         variants=[('Low', {'storeys': 16}), ('Mid', {'storeys': 24}), ('High', {'storeys': 32})], states=ALL4,
         kit='EDmTownMesh::Tower', desc='Curtain-wall slab tower on a two-storey podium, aluminium fins, penthouse.'),
    dict(asset='OfficeRibbon', fn=A.office_ribbon, cls='building', budget=(800, 20000),
         variants=[('Low', {'storeys': 12}), ('Mid', {'storeys': 18}), ('High', {'storeys': 25})], states=ALL4,
         kit='EDmTownMesh::Tower', desc='Setback concrete tower with ribbon windows and sun-shade blades, roof terraces.'),
    dict(asset='OfficeRound', fn=A.office_round, cls='building', budget=(800, 20000),
         variants=[('Low', {'storeys': 12}), ('Mid', {'storeys': 18}), ('High', {'storeys': 24})], states=ALL4,
         kit='EDmTownMesh::Tower', desc='Round curtain-wall tower with balcony rings on a square podium, mast.'),
    dict(asset='PowerPlant', fn=A.reactor, cls='civic', budget=(8000, 30000),
         variants=[('Std', {})], states=ALL4, kit='EDmTownMesh::Reactor',
         desc='Nuclear station: domed containment, turbine hall, auxiliary, fuel, admin and pump buildings, stack.',
         blasts=[((-2.0, 4.0, 46.0), 13.0), ((36.0, -14.0, 22.0), 13.0)]),
    dict(asset='CoolingTower', fn=A.cooling_tower, cls='civic', budget=(6000, 20000),
         variants=[('Std', {})], states=ALL4, kit='EDmTownMesh::CoolingTower',
         desc='Hyperbolic natural-draught cooling tower, 112 m, on V-legs over a basin. SOCKET_Steam at the top.',
         blasts=[((24.0, -15.0, 104.0), 26.0)], ruin_h=(0.10, 0.34)),
    dict(asset='Switchyard', fn=A.switchyard, cls='civic', budget=(2000, 15000),
         variants=[('Std', {})], states=ALL4, kit='(new) plant switchyard, place beside PowerPlant',
         desc='High-voltage switchyard: three gantry rows, breakers, transformers, control hut, gravel pad.'),
    dict(asset='Pylon', fn=A.pylon, cls='prop', budget=(50, 1200),
         variants=[('Std', {})], states=['Intact'], kit='(new) power line pylon, 40 m',
         desc='Lattice transmission pylon, three cross-arms. SOCKET_Wire at each insulator.'),
    dict(asset='University', fn=A.university, cls='civic', budget=(8000, 20000),
         variants=[('Std', {})], states=ALL4, kit='EDmTownMesh::Academy (Age >= 5, variant 1)',
         desc='Campus: teaching slab on pilotis, library tower, lecture hall, copper planetarium dome, lawn quad.',
         blasts=[((44.0, -30.0, 34.0), 11.0), ((-24.0, -30.0, 9.0), 9.0), ((-8.0, -24.0, 14.0), 6.0)]),
    dict(asset='HighwayInterchange', fn=A.highway_interchange, cls='civic', budget=(8000, 30000),
         variants=[('Std', {})], states=['Intact', 'Damaged', 'Ruin'], kit='(new) EStreet::Highway interchange',
         desc='Cloverleaf, 240 x 240 m: elevated 6-lane highway along X over a 4-lane arterial, 4 loops, 4 ramps.',
         blasts=[((45.0, 0.0, 9.0), 14.0), ((-46.5, -85.0, 5.0), 10.0)], ruin_h=(0.0, 0.25)),
    dict(asset='HighwayViaduct', fn=A.highway_viaduct, cls='civic', budget=(800, 8000),
         variants=[('Std', {})], states=ALL4, kit='(new) EStreet::Highway, 60 m tiling segment along X',
         desc='60 m elevated segment (deck top 9 m), one hammerhead pier at its centre; tiles with the interchange.',
         blasts=[((16.0, 0.0, 8.0), 13.0)], ruin_h=(0.0, 0.3), cut=0.2),
    dict(asset='HighwayGrade', fn=A.highway_grade, cls='civic', budget=(300, 4000),
         variants=[('Std', {})], states=['Intact'], kit='(new) EStreet::Highway, 60 m at-grade segment along X',
         desc='60 m at-grade dual carriageway on a 1 m embankment with lamps.'),
    dict(asset='Crown', fn=None, cls='prop', budget=(50, 1500),
         variants=[('CultureA', {'fn': A.crown_a}), ('CultureB', {'fn': A.crown_b}), ('CultureC', {'fn': A.crown_c})],
         states=['Intact'], kit='culture roof ornament, attach at SOCKET_Crown of any Age5 tower',
         desc='Culture roof ornaments: A domed terrace kiosks, B upswept saddle canopy, C drum with slate cone.'),
]

STATE_SEED = {'Intact': 1, 'Construction': 2, 'Damaged': 3, 'Ruin': 4}


def mesh_name(a, variant, state):
    if a['asset'] == 'Crown':
        return 'SM_%s_Crown_%s' % (FAMILY, variant)
    if len(a['states']) == 1:
        return 'SM_%s_%s_%s' % (FAMILY, a['asset'], variant)
    return 'SM_%s_%s_%s_%s' % (FAMILY, a['asset'], variant, state)


def builder(a, P):
    return P['fn'] if a['fn'] is None else a['fn']


def make_state(a, P, kind, variant=''):
    if kind == 'Intact':
        return None
    probe = G.MeshBuilder()
    builder(a, P)(probe, P, None, 2)
    xs = [p[0] for p in probe.P]
    ys = [p[1] for p in probe.P]
    zs = [p[2] for p in probe.P]
    box = (min(xs), min(ys), max(xs), max(ys), max(zs))
    kw = {}
    if 'blasts' in a:
        kw['blast'] = a['blasts']
    if 'ruin_h' in a:
        kw['ruin_h'] = a['ruin_h']
    if 'cut' in a:
        kw['cut'] = a['cut']
    S = G.State(kind, box, seed=STATE_SEED[kind] * 101 + len(variant) * 7, **kw)
    return S


def build_mb(a, P, S, lod):
    mb = G.MeshBuilder(S)
    grids = builder(a, P)(mb, P, S, lod)
    if lod == 3:
        return mb
    if a['asset'] == 'CoolingTower':
        A.cooling_dress(mb, S, lod)
    elif a['asset'] not in ('HighwayViaduct', 'HighwayInterchange', 'HighwayGrade', 'Crown', 'Pylon'):
        A.dress_state(mb, S, lod, grids or [])
    return mb


# materials -------------------------------------------------------------------------------------------------


def _img(name, colorspace):
    path = os.path.join(TEX_DIR, name + '.png')
    img = bpy.data.images.get(name)
    if img is None:
        img = bpy.data.images.load(path, check_existing=True)
        img.name = name
    img.colorspace_settings.name = colorspace
    return img


def _mix(nt, blend, fac, a, b):
    n = nt.nodes.new('ShaderNodeMix')
    n.data_type = 'RGBA'
    n.blend_type = blend
    ins = {s.identifier: s for s in n.inputs}
    if isinstance(fac, float):
        ins['Factor_Float'].default_value = fac
    else:
        nt.links.new(fac, ins['Factor_Float'])
    for key, v in (('A_Color', a), ('B_Color', b)):
        if isinstance(v, tuple):
            ins[key].default_value = v
        else:
            nt.links.new(v, ins[key])
    return [s for s in n.outputs if s.identifier == 'Result_Color'][0]


def make_material(slot, culture='Neutral', night=0.0):
    """slot: Structure | Roof | Detail. Returns a Principled material wired like the UE master material should be:
    base = D * lerp(1, tint, mask) * lerp(1, soot, vc.R) * vc.G * lerp(1, ORM.R, 0.7); emissive windows from M.R."""
    base_name = 'M_%s_%s' % (FAMILY, slot)
    name = base_name if (culture == 'Neutral' and night == 0.0) else 'MI_%s_%s_%s%s' % (FAMILY, slot, culture, '_Night' if night else '')
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    tex = {'Structure': 'Facade', 'Roof': 'Roof', 'Detail': 'Detail'}[slot]
    cul = CULTURES[culture]
    tint = cul[slot + 'Tint'] if slot + 'Tint' in cul else cul['StructureTint']
    td = nt.nodes.new('ShaderNodeTexImage')
    td.image = _img('T_%s_%s_D' % (FAMILY, tex), 'sRGB')
    to = nt.nodes.new('ShaderNodeTexImage')
    to.image = _img('T_%s_%s_ORM' % (FAMILY, tex), 'Non-Color')
    tn = nt.nodes.new('ShaderNodeTexImage')
    tn.image = _img('T_%s_%s_N' % (FAMILY, tex), 'Non-Color')
    for t in (td, to, tn):
        t.interpolation = 'Linear'
    orm = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(to.outputs['Color'], orm.inputs['Color'])
    vc = nt.nodes.new('ShaderNodeVertexColor')
    vc.layer_name = 'Col'
    vcs = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(vc.outputs['Color'], vcs.inputs['Color'])
    col = td.outputs['Color']
    tint4 = (tint[0], tint[1], tint[2], 1.0)
    if slot == 'Structure':
        tm = nt.nodes.new('ShaderNodeTexImage')
        tm.image = _img('T_%s_Facade_M' % FAMILY, 'Non-Color')
        msk = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(tm.outputs['Color'], msk.inputs['Color'])
        col = _mix(nt, 'MULTIPLY', msk.outputs['Green'], col, tint4)
    else:
        col = _mix(nt, 'MULTIPLY', 1.0, col, tint4)
    col = _mix(nt, 'MIX', vcs.outputs['Red'], col, (0.022, 0.02, 0.018, 1.0))
    col = _mix(nt, 'MULTIPLY', vcs.outputs['Green'], col, (0.0, 0.0, 0.0, 1.0)) if False else col
    ao = nt.nodes.new('ShaderNodeMath')
    ao.operation = 'MULTIPLY'
    nt.links.new(vcs.outputs['Green'], ao.inputs[0])
    aot = nt.nodes.new('ShaderNodeMath')
    aot.operation = 'MULTIPLY_ADD'
    aot.inputs[1].default_value = 0.7
    aot.inputs[2].default_value = 0.3
    nt.links.new(orm.outputs['Red'], aot.inputs[0])
    nt.links.new(aot.outputs['Value'], ao.inputs[1])
    aoc = nt.nodes.new('ShaderNodeCombineColor')
    for k in ('Red', 'Green', 'Blue'):
        nt.links.new(ao.outputs['Value'], aoc.inputs[k])
    col = _mix(nt, 'MULTIPLY', 1.0, col, aoc.outputs['Color'])
    nt.links.new(col, bsdf.inputs['Base Color'])
    rough = nt.nodes.new('ShaderNodeMath')
    rough.operation = 'ADD'
    rough.inputs[1].default_value = cul['RoughnessAdd']
    rough.use_clamp = True
    nt.links.new(orm.outputs['Green'], rough.inputs[0])
    nt.links.new(rough.outputs['Value'], bsdf.inputs['Roughness'])
    nt.links.new(orm.outputs['Blue'], bsdf.inputs['Metallic'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nm.inputs['Strength'].default_value = 1.0
    nt.links.new(tn.outputs['Color'], nm.inputs['Color'])
    nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    if slot == 'Structure' and night > 0:
        gt = nt.nodes.new('ShaderNodeMath')
        gt.operation = 'GREATER_THAN'
        gt.inputs[1].default_value = 1.0 - night
        nt.links.new(msk.outputs['Red'], gt.inputs[0])
        ns = nt.nodes.new('ShaderNodeMath')
        ns.operation = 'SUBTRACT'
        ns.inputs[0].default_value = 1.0
        nt.links.new(vcs.outputs['Red'], ns.inputs[1])
        g2 = nt.nodes.new('ShaderNodeMath')
        g2.operation = 'MULTIPLY'
        nt.links.new(gt.outputs['Value'], g2.inputs[0])
        nt.links.new(ns.outputs['Value'], g2.inputs[1])
        bsdf.inputs['Emission Color'].default_value = (1.0, 0.55, 0.22, 1.0)
        g3 = nt.nodes.new('ShaderNodeMath')
        g3.operation = 'MULTIPLY'
        g3.inputs[1].default_value = 2.5
        nt.links.new(g2.outputs['Value'], g3.inputs[0])
        nt.links.new(g3.outputs['Value'], bsdf.inputs['Emission Strength'])
    return m


def materials(culture='Neutral', night=0.0):
    return [make_material(s, culture, night) for s in ('Structure', 'Roof', 'Detail')]


# meshes ----------------------------------------------------------------------------------------------------


def to_mesh(mb, name, S, mats):
    P = mb.P
    faces = [list(range(s, s + c)) for (s, c, mat, sm, so) in mb.F]
    me = bpy.data.meshes.new(name)
    me.from_pydata(P, [], faces)
    for m in mats:
        me.materials.append(m)
    uvl = me.uv_layers.new(name='UVMap')
    flat = []
    for u, v in mb.UV:
        flat.extend((u, v))
    uvl.data.foreach_set('uv', flat)
    me.polygons.foreach_set('material_index', [f[2] for f in mb.F])
    me.polygons.foreach_set('use_smooth', [bool(f[3]) for f in mb.F])
    # corner colours: R soot (state field + face bias), G vertical-face ground occlusion, B reserved, A 1
    cols = []
    for (s, c, mat, sm, so) in mb.F:
        pts = P[s:s + c]
        n = G.cross(G.sub(pts[1], pts[0]), G.sub(pts[2], pts[0]))
        ln = math.sqrt(G.dot(n, n)) or 1.0
        vertical = abs(n[2] / ln) < 0.5
        for p in pts:
            soot = so + (S.soot(p) if S else 0.0)
            ao = 1.0
            if vertical:
                ao = 0.6 + 0.4 * G.clamp((p[2] + 0.3) / 3.5, 0.0, 1.0)
            cols.extend((G.clamp(soot, 0.0, 1.0), ao, 1.0, 1.0))
    ca = me.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
    ca.data.foreach_set('color', cols)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0005)
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    bm.to_mesh(me)
    bm.free()
    me.validate(clean_customdata=False)
    me.update()
    return me


def build_object(a, variant, P, kind, mats, coll, S=None, lods=(0, 1, 2, 3)):
    name = mesh_name(a, variant, kind)
    if S is None and kind != 'Intact':
        S = make_state(a, P, kind, variant)
    root = bpy.data.objects.new(name, None)
    root.empty_display_size = 5
    # verify fix: Blender's FBX exporter writes an Empty's 'fbx_type' string as its NodeAttribute type, so this is
    # a real FBX LodGroup (as in age0-age4); Unreal imports _LOD0.._LOD3 as the mesh LODs in one go
    root['fbx_type'] = 'LodGroup'
    coll.objects.link(root)
    objs = []
    tris = []
    sockets = []
    for lod in lods:
        mb = build_mb(a, P, S, lod)
        me = to_mesh(mb, '%s_LOD%d' % (name, lod), S, mats)
        ob = bpy.data.objects.new('%s_LOD%d' % (name, lod), me)
        coll.objects.link(ob)
        ob.parent = root
        objs.append(ob)
        tris.append(len(me.polygons))
        if lod == 0:
            count = {}
            for sname, p in mb.sockets:
                count[sname] = count.get(sname, 0) + 1
                # Blender object names are unique per file, so the .blend copy carries the mesh name; export_fbx
                # writes the clean SOCKET_<Kind>_<n> name the importer turns into the Unreal socket.
                clean = '%s_%d' % (sname, count[sname])
                e = bpy.data.objects.new('%s|%s' % (name, clean), None)
                e['socket'] = clean
                e.empty_display_type = 'ARROWS'
                e.empty_display_size = 1.0
                e.location = p
                coll.objects.link(e)
                e.parent = ob
                sockets.append({'name': clean, 'pos_m': [round(c, 2) for c in p]})
        if lod > 0:
            ob.hide_render = True
            ob.hide_set(True)
    return root, objs, tris, sockets, S


def bounds(ob):
    me = ob.data
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    return min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)


def export_fbx(root, path):
    bpy.ops.object.select_all(action='DESELECT')
    stack = [root]
    while stack:
        o = stack.pop()
        o.hide_set(False)
        o.select_set(True)
        stack.extend(o.children)
    bpy.context.view_layer.objects.active = root
    # sockets: export under their clean names (SOCKET_Crown_1, not a Blender-suffixed SOCKET_Crown_1.033)
    renamed = []
    for o in bpy.context.selected_objects:
        if 'socket' in o.keys():
            held = bpy.data.objects.get(o['socket'])
            if held is not None and held != o:
                held.name = held.name + '|held'
                renamed.append((held, o['socket']))
            renamed.append((o, o.name))
            o.name = o['socket']
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'MESH', 'EMPTY'}, mesh_smooth_type='FACE',
                             use_tspace=True, add_leaf_bones=False, bake_anim=False, apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y', path_mode='STRIP',
                             colors_type='LINEAR', use_mesh_modifiers=False, use_custom_props=False)
    for o, n in reversed(renamed):
        o.name = n
    bpy.ops.object.select_all(action='DESELECT')


def iter_meshes(only=None):
    for a in ASSETS:
        for variant, vp in a['variants']:
            P = dict(vp)
            for kind in a['states']:
                name = mesh_name(a, variant, kind)
                if only and only not in name:
                    continue
                yield a, variant, P, kind, name


def main(argv):
    t0 = time.time()
    only = None
    if '--only' in argv:
        only = argv[argv.index('--only') + 1]
    if '--textures' in argv or not os.path.exists(os.path.join(TEX_DIR, 'T_Age5_Facade_D.png')):
        TX.build_all(TEX_DIR)
    if '--textures-only' in argv:
        return
    os.makedirs(EXPORT_DIR, exist_ok=True)
    os.makedirs(SOURCE_DIR, exist_ok=True)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    coll = bpy.context.scene.collection
    mats = materials()
    manifest_path = os.path.join(HERE, 'manifest.json')
    old = {}
    if only and os.path.exists(manifest_path):
        with open(manifest_path, encoding='utf-8') as f:
            old = {m['name']: m for m in json.load(f).get('meshes', [])}
    entries = dict(old)
    roots = []
    for a, variant, P, kind, name in iter_meshes(only):
        root, objs, tris, sockets, S = build_object(a, variant, P, kind, mats, coll)
        path = os.path.join(EXPORT_DIR, name + '.fbx')
        export_fbx(root, path)
        b = bounds(objs[0])
        siblings = [mesh_name(a, variant, k) for k in a['states']] if len(a['states']) > 1 else []
        entries[name] = {
            'name': name,
            'asset': a['asset'],
            'variant': variant,
            'state': kind,
            'class': a['cls'],
            'description': a['desc'],
            'params': {k: v for k, v in P.items() if k != 'fn'},
            'footprint_m': [round(b[3] - b[0], 2), round(b[4] - b[1], 2)],
            'height_m': round(b[5], 2),
            'bounds_m': [round(c, 2) for c in b],
            'triangles': {'LOD%d' % i: t for i, t in enumerate(tris)},
            'lod_ratio': [round(t / tris[0], 3) for t in tris],
            'lod_screen_size': [1.0, 0.35, 0.12, 0.04],
            'budget_lod0': list(a['budget']),
            'within_budget': tris[0] <= a['budget'][1],
            'materials': ['M_Age5_Structure', 'M_Age5_Roof', 'M_Age5_Detail'],
            'textures': {
                'M_Age5_Structure': ['T_Age5_Facade_D', 'T_Age5_Facade_N', 'T_Age5_Facade_ORM', 'T_Age5_Facade_M'],
                'M_Age5_Roof': ['T_Age5_Roof_D', 'T_Age5_Roof_N', 'T_Age5_Roof_ORM'],
                'M_Age5_Detail': ['T_Age5_Detail_D', 'T_Age5_Detail_N', 'T_Age5_Detail_ORM'],
            },
            'states': siblings,
            'culture_variants': ['MI_Age5_%s_%s' % (s, c) for c in ('CultureA', 'CultureB', 'CultureC')
                                 for s in ('Structure', 'Roof', 'Detail')],
            'culture_crowns': CROWN_FOR if any(s['name'].startswith('SOCKET_Crown') for s in sockets) else {},
            'sockets': sockets,
            'kit_mapping': a['kit'],
            'export': 'art/age5/export/%s.fbx' % name,
            'lod_nodes': ['%s_LOD%d' % (name, i) for i in range(4)],
            'licence': 'original',
            'script': 'art/age5/build_age5.py',
        }
        roots.append(root)
        print('[age5] %-48s LOD tris %s  %.0fs' % (name, tris, time.time() - t0), flush=True)
    # source .blend: lay the roots out on a grid so it can be browsed
    x = y = 0.0
    rowh = 0.0
    for r in roots:
        ob0 = [c for c in r.children if c.name.endswith('_LOD0')][0]
        b = bounds(ob0)
        w = b[3] - b[0] + 30
        if x + w > 2400:
            x = 0.0
            y += rowh
            rowh = 0.0
        r.location = (x - b[0], y - b[1], 0)
        x += w
        rowh = max(rowh, b[4] - b[1] + 30)
    meshes = sorted(entries.values(), key=lambda m: m['name'])
    man = {
        'family': FAMILY,
        'generated': time.strftime('%Y-%m-%d'),
        'script': 'art/age5/build_age5.py',
        'licence': 'original (procedural, no external inputs)',
        'units': 'metres, Z up, front -Y, pivot at footprint centre on the ground; foundations reach Z = -0.3',
        'conventions': {
            'lods': 'one FBX per mesh: Empty <name> exported as an FBX LodGroup (fbx_type) with children '
                    '<name>_LOD0..3 (LOD3 = hull); sockets are SOCKET_<Kind>_<n> empties parented to LOD0',
            'material_slots': ['Structure (facade trim sheet)', 'Roof (tiling membrane/gravel, 16 m)', 'Detail (trim sheet)'],
            'vertex_color': 'Col, linear: R soot/burn (0 clean .. 1 charred), G ground occlusion on walls, B and A 1',
            'facade_M': 'R: 0 on walls, per-window random rank (0.15..1] on glass; lit if M.R > 1 - LitFraction. '
                        'G: culture-tint mask. B: unused',
            'facade_strips': {k: {'strip_from_bottom': v[0], 'storey_m': v[1]} for k, v in TX.STRIPS.items()},
            'detail_rows_px': {k: list(v) for k, v in TX.DETAIL_ROWS.items()},
        },
        'cultures': CULTURES,
        'meshes': meshes,
        'totals': {
            'mesh_count': len(meshes),
            'triangles_LOD0': sum(m['triangles']['LOD0'] for m in meshes),
            'triangles_all_lods': sum(sum(m['triangles'].values()) for m in meshes),
        },
    }
    with open(manifest_path, 'w', encoding='utf-8') as f:
        json.dump(man, f, indent=1)
    if '--no-blend' not in argv and not only:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SOURCE_DIR, 'age5.blend'), compress=True)
    print('[age5] done: %d meshes, LOD0 tris %d, %.0fs' % (len(meshes), man['totals']['triangles_LOD0'], time.time() - t0))


if __name__ == '__main__':
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    main(argv)
