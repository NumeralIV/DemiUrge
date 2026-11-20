# DEMIURGE art - Age6 (Space Age) family build.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/age6/build_age6.py -- [--only Name1,Name2] [--skip-tex] [--no-export]
#
# Writes art/age6/textures/*.png, art/age6/export/SM_Age6_*.fbx (LOD0..3 in each file),
# art/age6/source/age6.blend and art/age6/manifest.json. Never hand-edit the outputs; rerun this.
import os
import sys
import json
import math
import time
import hashlib
import datetime

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'tools'))

import age6_tex  # noqa: E402
import age6_kit as K  # noqa: E402
import age6_assets as A  # noqa: E402

TEX = os.path.join(HERE, 'textures')
EXP = os.path.join(HERE, 'export')
SRC = os.path.join(HERE, 'source')
LOD_TARGET = [1.0, 0.40, 0.12, 0.04]
SCREEN = [1.0, 0.35, 0.12, 0.04]
BUDGET = {'house': (2000, 6000), 'civic': (8000, 20000), 'prop': (50, 1000), 'vehicle': (1000, 4000), 'wonder': (30000, 80000)}

# Culture variants: material-instance parameters (linear multipliers). Tint applies where M.G = 1
# (cream panels, piers, white metal), Accent where M.B = 1 (the pale-gold bands and gold trim).
CULTURES = {
    'Default':      {'tint': (1.00, 1.00, 1.00), 'accent': (1.00, 1.00, 1.00), 'roof': (1.00, 1.00, 1.00),
                     'note': 'white and warm-cream facades, pale gold, blue solar glass (concept age-6-space)'},
    'CliffTerrace': {'tint': (1.00, 0.76, 0.60), 'accent': (1.15, 0.70, 0.45), 'roof': (1.12, 0.98, 0.78),
                     'note': 'rose-ochre plaster tone, copper accents, drier planting (culture A)'},
    'LagoonWeaver': {'tint': (0.82, 0.95, 1.00), 'accent': (1.25, 0.80, 0.25), 'roof': (0.80, 1.08, 1.02),
                     'note': 'silver-white with saffron accents, lush aqua-green planting (culture B)'},
    'RingBuilder':  {'tint': (0.74, 0.78, 0.86), 'accent': (0.70, 0.78, 1.00), 'roof': (0.92, 0.85, 1.00),
                     'note': 'cool pale stone, slate-blue silver accents, heather-toned planting (culture C)'},
}

MATERIALS = {
    # name: (texture set, has M map, tile metres, culture-tinted)
    'M_Age6_Facade':    ('Facade', True, age6_tex.FACADE_TILE, True),
    'M_Age6_GreenRoof': ('GreenRoof', False, age6_tex.ROOF_TILE, True),
    'M_Age6_Tech':      ('Tech', True, None, True),
    'M_Age6_Concrete':  ('Concrete', False, age6_tex.CONCRETE_TILE, False),
    'M_Age6_Hull':      ('Hull', False, age6_tex.HULL_TILE, False),
    'M_Age6_Cladding':  ('Cladding', False, age6_tex.CLAD_TILE, False),
    'M_Age6_RoofMetal': ('Cladding', False, age6_tex.CLAD_TILE, False),
}


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'only': None, 'skip_tex': False, 'export': True}
    i = 0
    while i < len(a):
        if a[i] == '--only':
            o['only'] = set(a[i + 1].split(','))
            i += 1
        elif a[i] == '--skip-tex':
            o['skip_tex'] = True
        elif a[i] == '--no-export':
            o['export'] = False
        i += 1
    return o


def seed_of(s):
    return int(hashlib.md5(s.encode()).hexdigest()[:8], 16)


# --------------------------------------------------------------------------- materials


def _sock(node, ident, out=False):
    for s in (node.outputs if out else node.inputs):
        if s.identifier == ident:
            return s
    raise KeyError(ident)


def _img(name, noncolor):
    p = os.path.join(TEX, name + '.png')
    img = bpy.data.images.get(name) or bpy.data.images.load(p, check_existing=True)
    img.name = name
    if noncolor:
        img.colorspace_settings.name = 'Non-Color'
    return img


def make_material(name, tset, has_m, tint=(1, 1, 1), accent=(1, 1, 1), darken=1.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    N, L = nt.nodes, nt.links
    out = N.new('ShaderNodeOutputMaterial')
    bsdf = N.new('ShaderNodeBsdfPrincipled')
    L.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    uv = N.new('ShaderNodeUVMap')
    uv.uv_map = 'UVMap'

    def tex(suffix, nonc):
        t = N.new('ShaderNodeTexImage')
        t.image = _img('T_Age6_%s_%s' % (tset, suffix), nonc)
        t.interpolation = 'Cubic' if suffix == 'D' else 'Linear'
        L.new(uv.outputs['UV'], t.inputs['Vector'])
        return t
    d = tex('D', False)
    n = tex('N', True)
    orm = tex('ORM', True)
    sep = N.new('ShaderNodeSeparateColor')
    L.new(orm.outputs['Color'], sep.inputs['Color'])
    vc = N.new('ShaderNodeVertexColor')
    vc.layer_name = 'Col'

    def mix(a, bsock, blend='MULTIPLY', fac=1.0):
        mx = N.new('ShaderNodeMix')
        mx.data_type = 'RGBA'
        mx.blend_type = blend
        _sock(mx, 'Factor_Float').default_value = fac
        if hasattr(a, 'is_output'):
            L.new(a, _sock(mx, 'A_Color'))
        else:
            _sock(mx, 'A_Color').default_value = a
        if hasattr(bsock, 'is_output'):
            L.new(bsock, _sock(mx, 'B_Color'))
        else:
            _sock(mx, 'B_Color').default_value = bsock
        return mx
    col = d.outputs['Color']
    if has_m:
        mk = tex('M', True)
        sm = N.new('ShaderNodeSeparateColor')
        L.new(mk.outputs['Color'], sm.inputs['Color'])
        t1 = mix((1, 1, 1, 1), (*tint, 1), 'MIX')
        t1.name = 'Tint'
        L.new(sm.outputs['Green'], _sock(t1, 'Factor_Float'))
        c1 = mix(col, _sock(t1, 'Result_Color', True))
        t2 = mix((1, 1, 1, 1), (*accent, 1), 'MIX')
        t2.name = 'Accent'
        L.new(sm.outputs['Blue'], _sock(t2, 'Factor_Float'))
        c2 = mix(_sock(c1, 'Result_Color', True), _sock(t2, 'Result_Color', True))
        col = _sock(c2, 'Result_Color', True)
        # night: emissive windows / lamps (value node 'NightGlow' scales it; 0 by day)
        glow = N.new('ShaderNodeValue')
        glow.name = 'NightGlow'
        glow.outputs[0].default_value = 0.0
        em = N.new('ShaderNodeMath')
        em.operation = 'MULTIPLY'
        L.new(sm.outputs['Red'], em.inputs[0])
        L.new(glow.outputs[0], em.inputs[1])
        L.new(em.outputs[0], bsdf.inputs['Emission Strength'])
        bsdf.inputs['Emission Color'].default_value = (1.0, 0.68, 0.38, 1.0)
    else:
        t1 = mix(col, (*tint, 1))
        t1.name = 'Tint'
        col = _sock(t1, 'Result_Color', True)
    # AO (soft) and the vertex colour (grime / scorch from states)
    ao = mix((1, 1, 1, 1), sep.outputs['Red'], 'MIX', 0.6)
    c3 = mix(col, _sock(ao, 'Result_Color', True))
    c4 = mix(_sock(c3, 'Result_Color', True), vc.outputs['Color'])
    c5 = mix(_sock(c4, 'Result_Color', True), (darken, darken, darken, 1))
    L.new(_sock(c5, 'Result_Color', True), bsdf.inputs['Base Color'])
    L.new(sep.outputs['Green'], bsdf.inputs['Roughness'])
    L.new(sep.outputs['Blue'], bsdf.inputs['Metallic'])
    nm = N.new('ShaderNodeNormalMap')
    nm.uv_map = 'UVMap'
    L.new(n.outputs['Color'], nm.inputs['Color'])
    L.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    return m


def build_materials():
    mats = {}
    for name, (tset, has_m, tile, cult) in MATERIALS.items():
        mats[name] = make_material(name, tset, has_m, darken=0.62 if name == 'M_Age6_RoofMetal' else 1.0)
    mis = {}
    for cname, c in CULTURES.items():
        if cname == 'Default':
            continue
        for base in ('M_Age6_Facade', 'M_Age6_Tech', 'M_Age6_GreenRoof'):
            tset, has_m, tile, cult = MATERIALS[base]
            mi = 'MI_Age6_%s_%s' % (base[len('M_Age6_'):], cname)
            tint = c['roof'] if base == 'M_Age6_GreenRoof' else c['tint']
            mis[mi] = make_material(mi, tset, has_m, tint, c['accent'])
            mis[mi].use_fake_user = True   # unused in the file; keep it in the .blend
    return mats, mis


# --------------------------------------------------------------------------- build


def tri_count(me):
    return sum(len(p.vertices) - 2 for p in me.polygons)


def signed_volume(me):
    v = me.vertices
    s = 0.0
    for p in me.polygons:
        a = v[p.vertices[0]].co
        for i in range(1, len(p.vertices) - 1):
            b = v[p.vertices[i]].co
            c = v[p.vertices[i + 1]].co
            s += a.dot(b.cross(c))
    return s / 6.0


def open_edges(me):
    cnt = {}
    for p in me.polygons:
        for k in p.edge_keys:
            cnt[k] = cnt.get(k, 0) + 1
    return sum(1 for c in cnt.values() if c == 1)


def decimate(obj, ratio):
    mod = obj.modifiers.new('dec', 'DECIMATE')
    mod.ratio = max(0.005, min(1.0, ratio))
    mod.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    eo = obj.evaluated_get(dg)
    new = bpy.data.meshes.new_from_object(eo)
    obj.modifiers.clear()
    old = obj.data
    obj.data = new
    new.name = old.name
    bpy.data.meshes.remove(old)
    clean_degenerate(new)


def clean_degenerate(me):
    """Collapse decimation leaves zero-area triangles; the FBX importers drop them, so the manifest counts
    disagreed with the files. Remove them here (verification 2026-11-19)."""
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=bm.edges[:])
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    bad = [f for f in bm.faces if f.calc_area() < 1e-6]
    # thin boxes collapse into back-to-back triangle pairs on the same three vertices; importers
    # (Blender's mesh validation, and so the FBX re-import) keep only one of them
    seen = set()
    for f in bm.faces:
        k = frozenset(v.index for v in f.verts)
        if k in seen:
            bad.append(f)
        seen.add(k)
    bad = list(set(bad))
    if bad:
        bmesh.ops.delete(bm, geom=bad, context='FACES')
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context='VERTS')
    bm.to_mesh(me)
    bm.free()


def build_asset(entry, mats, coll_root):
    name, fn, slots, states, cut_skin, klass, sim = entry
    out = {}
    offset = None
    for state in states:
        mesh_name = 'SM_Age6_%s_%s' % (name, state)
        coll = bpy.data.collections.new(mesh_name)
        coll_root.children.link(coll)
        objs = []
        tris = []
        info0 = None
        for lod in range(4):
            b = A.B(lod, seed_of(name))
            info = fn(b)
            parts = b.parts
            if offset is None:
                if info.get('autocenter'):
                    lo = [min(v.co[i] for p in parts for v in p.bm.verts) for i in range(2)]
                    hi = [max(v.co[i] for p in parts for v in p.bm.verts) for i in range(2)]
                    offset = (-(lo[0] + hi[0]) * 0.5, -(lo[1] + hi[1]) * 0.5)
                else:
                    offset = (0.0, 0.0)
            if offset != (0.0, 0.0):
                for p in parts:
                    p.move(offset[0], offset[1], 0)
            if lod == 0:
                info0 = info
            sinfo = dict(info)
            if 'rubble_c' in info:
                sinfo['cx'], sinfo['cy'] = info['rubble_c'][0] + offset[0], info['rubble_c'][1] + offset[1]
                sinfo['w'] = sinfo['d'] = info['rubble_r'] * 2
            ctx = K.StateCtx(sinfo, A.state_skins(cut_skin), lod, seed_of(name + state))
            parts = K.apply_state(parts, state, ctx)
            me = bpy.data.meshes.new('%s_LOD%d' % (mesh_name, lod))
            used = K.merge(parts, me)
            for p in parts:
                p.bm.free()
            for si in used:
                me.materials.append(mats[slots[si]])
            ob = bpy.data.objects.new('%s_LOD%d' % (mesh_name, lod), me)
            coll.objects.link(ob)
            clean_degenerate(me)
            me.set_sharp_from_angle(angle=math.radians(55))
            t = tri_count(me)
            if lod > 0 and tris:
                tgt = LOD_TARGET[lod] * tris[0]
                # floor: collapse-decimating a handful of separate boxes or slabs below ~100 triangles
                # shatters them (crawlerway lanes broke into dashes, the hall and bunker lost faces); a
                # 100-triangle hull costs nothing at those screen sizes. Never more than the LOD before.
                # (verification 2026-11-19)
                tgt = min(max(tgt, 96), tris[-1])
                if t > tgt * 1.15 or t > tris[-1]:
                    decimate(ob, tgt / t)
                    me = ob.data
                    me.set_sharp_from_angle(angle=math.radians(55))
                    t = tri_count(me)
            tris.append(t)
            objs.append(ob)
            if lod > 0:
                ob.hide_render = True
                ob.hide_set(True) if False else None
                ob['lod'] = lod
            ob['lod'] = lod
            ob['screen_size'] = SCREEN[lod]
        grp = bpy.data.objects.new(mesh_name, None)
        grp['fbx_type'] = 'LodGroup'
        coll.objects.link(grp)
        for ob in objs:
            ob.parent = grp
        me0 = objs[0].data
        bb = [o for o in objs[0].bound_box]
        xs = [c[0] for c in bb]
        ys = [c[1] for c in bb]
        zs = [c[2] for c in bb]
        mats_used = [m.name for m in me0.materials]
        slot_map = {}
        for si, sname in enumerate(('Structure', 'Roof', 'Detail')):
            if slots[si] and slots[si] in mats_used:
                slot_map[sname] = slots[si]
        out[mesh_name] = {
            'objects': objs, 'group': grp, 'offset': offset,
            'asset': name, 'state': state, 'class': klass, 'sim': sim, 'info': info0,
            'footprint_m': [round(info0['w'], 2), round(info0['d'], 2)],
            'bounds_m': {'x': [round(min(xs), 2), round(max(xs), 2)], 'y': [round(min(ys), 2), round(max(ys), 2)],
                         'z': [round(min(zs), 2), round(max(zs), 2)]},
            'height_m': round(max(zs), 2),
            'triangles': tris,
            'slots': slot_map,
            'checks': {'signed_volume_m3': round(signed_volume(me0), 1), 'open_edges_lod0': open_edges(me0),
                       'pivot_ground': abs(min(zs) + 0.3) < 0.05 or name in ('Rocket',)},
        }
        print('  %-36s tris %s' % (mesh_name, tris))
    return out


def export_fbx(mesh_name, objs, grp):
    bpy.ops.object.select_all(action='DESELECT')
    grp.select_set(True)
    for o in objs:
        o.hide_set(False)
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    path = os.path.join(EXP, mesh_name + '.fbx')
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'MESH', 'EMPTY'}, use_mesh_modifiers=True,
                             mesh_smooth_type='FACE', use_tspace=True, use_triangles=True, add_leaf_bones=False,
                             bake_anim=False, path_mode='STRIP', colors_type='LINEAR', apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y')
    for o in objs[1:]:
        o.hide_set(True)
    return os.path.relpath(path, os.path.dirname(os.path.dirname(HERE))).replace('\\', '/')


def main():
    o = args()
    t0 = time.time()
    os.makedirs(EXP, exist_ok=True)
    os.makedirs(SRC, exist_ok=True)
    if not (o['skip_tex'] and os.path.exists(os.path.join(TEX, 'T_Age6_Facade_D.png'))):
        for s in age6_tex.build_all(TEX):
            print('  tex', s)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system = 'METRIC'
    bpy.context.scene.unit_settings.scale_length = 1.0
    mats, mis = build_materials()
    root = bpy.data.collections.new('Age6')
    bpy.context.scene.collection.children.link(root)
    results = {}
    for entry in A.ASSETS:
        if o['only'] and entry[0] not in o['only']:
            continue
        print('building', entry[0])
        results.update(build_asset(entry, mats, root))
    man_path = os.path.join(HERE, 'manifest.json')
    manifest = {}
    if o['only'] and os.path.exists(man_path):
        with open(man_path, encoding='utf-8') as f:
            manifest = json.load(f)
    meshes = manifest.get('meshes', {})
    for mesh_name, r in results.items():
        fbx = export_fbx(mesh_name, r['objects'], r['group']) if o['export'] else None
        info = r['info']
        lo, hi = BUDGET.get(r['class'], (0, 1e9))
        meshes[mesh_name] = {
            'asset': 'SM_Age6_' + r['asset'], 'state': r['state'],
            'sim_mapping': r['sim'],
            'footprint_m': r['footprint_m'], 'height_m': r['height_m'], 'bounds_m': r['bounds_m'],
            'triangles_per_lod': r['triangles'], 'lod_screen_sizes': SCREEN,
            'budget_class': r['class'], 'budget_lod0': [lo, hi],
            'within_budget': r['triangles'][0] <= hi,
            'material_slots': r['slots'],
            'textures': sorted({'T_Age6_%s_%s' % (MATERIALS[m][0], s) for m in r['slots'].values()
                                for s in (('D', 'N', 'ORM', 'M') if MATERIALS[m][1] else ('D', 'N', 'ORM'))}),
            'states': [s for s in A.ASSETS_STATES.get(r['asset'], [])] if hasattr(A, 'ASSETS_STATES') else None,
            'culture_variants': (['Default'] + ['MI_Age6_*_%s' % c for c in CULTURES if c != 'Default'])
            if any(MATERIALS[m][3] for m in r['slots'].values()) else ['Default'],
            'sockets': {k: [round(v[0] + r['offset'][0], 2), round(v[1] + r['offset'][1], 2)] + list(v[2:]) for k, v in info.get('sockets', {}).items()},
            'vertex_colour': 'R=G=B multiplier on albedo (1 clean, down to 0.12 scorched)',
            'checks': r['checks'],
            'fbx': fbx, 'licence': 'original', 'script': 'art/age6/build_age6.py',
        }
    for k, v in meshes.items():
        a = v['asset'][len('SM_Age6_'):]
        for e in A.ASSETS:
            if e[0] == a:
                v['states'] = ['SM_Age6_%s_%s' % (a, s) for s in e[3]]
    # hall placement: yaw 180 so its doors face the pad, door centre at the end of three crawlerway
    # segments chained from the pad's crawlerway socket (0, -62)
    hall_pos = [0, -260, 0, 180]
    hd = meshes.get('SM_Age6_AssemblyHall_Intact', {}).get('sockets', {}).get('doors')
    if hd:
        hall_pos = [round(hd[0], 2), round(-62.0 - 3 * 60.0 + hd[1], 2), 0, 180]
    manifest = {
        'family': 'Age6',
        'generated': datetime.datetime.now().isoformat(timespec='seconds'),
        'units': '1 Blender unit = 1 m; Z up; front faces -Y; pivot at footprint centre, ground Z = 0; plinth to Z = -0.3',
        'fbx': 'one file per mesh: an LodGroup empty <mesh> (fbx_type = LodGroup) with children <mesh>_LOD0..LOD3 (triangulated, smoothing groups, tangents, vertex colour Col); import with Import Mesh LODs',
        'materials': {k: {'textures': ['T_Age6_%s_%s' % (v[0], s) for s in (('D', 'N', 'ORM', 'M') if v[1] else ('D', 'N', 'ORM'))],
                          'tile_m': v[2], 'culture_tinted': v[3]} for k, v in MATERIALS.items()},
        'material_notes': {
            'M channel': 'R emissive (lit windows / lamps at night), G culture tint mask, B accent (pale gold) mask',
            'ORM': 'R ambient occlusion, G roughness, B metallic (linear)',
            'N': 'tangent space, OpenGL (+Y); flip green on UE import',
            'UVs': 'tiling world-scale UVs (not 0..1; nothing is baked) except the Tech atlas, which is region-mapped',
            'M_Age6_RoofMetal': 'same textures as M_Age6_Cladding, albedo x0.62',
        },
        'culture_instances': {c: v for c, v in CULTURES.items()},
        'launch_site_layout': {
            'note': 'suggested placement relative to the LaunchPad pivot (metres, yaw degrees); the rocket stands on the pad socket rocket_mount',
            'SM_Age6_Rocket_Intact': [0, 0, A.MOUNT_Z, 0],
            'SM_Age6_AssemblyHall': hall_pos,
            'SM_Age6_ControlBunker': [150, -60, 0, 200],
            'SM_Age6_Crawlerway_Intact': 'chain 3 segments along -Y from the pad socket crawlerway, rotated 90',
        },
        'meshes': dict(sorted(meshes.items())),
    }
    with open(man_path, 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=1)
    for ob in bpy.data.objects:
        if ob.get('lod', 0) > 0:
            ob.hide_set(True)
    if not o['only']:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SRC, 'age6.blend'), compress=True)
    else:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SRC, 'age6_partial.blend'), compress=True)
    print('DONE %d meshes in %.0f s' % (len(results), time.time() - t0))


main()
