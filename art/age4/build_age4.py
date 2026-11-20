# DEMIURGE art - Age4 (Industry age) kit builder. Script-generated, reproducible (docs/ASSETS.md).
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age4/build_age4.py -- [options]
#
# Options:
#   --textures         (re)generate the texture sets in art/age4/textures (also done when any is missing)
#   --only A,B         build only these assets (registry keys, e.g. Terrace_A,Factory_A)
#   --states S1,S2     build only these states (default: every state the asset declares)
#   --no-export        skip FBX export (still writes the .blend and the manifest entry)
#
# Writes art/age4/blend/<Asset>.blend (all states x LODs, materials, sockets), art/age4/export/<Mesh>_<State>.fbx
# (one file per mesh: LOD group with LOD0..LOD3 + SOCKET_ empties), and merges art/age4/manifest.json.
import json
import os
import sys
import time

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, 'lib')
if LIB not in sys.path:
    sys.path.insert(0, LIB)

import a4tex   # noqa: E402
import a4geo as G   # noqa: E402
import a4mat   # noqa: E402
import a4assets   # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
EXPORT = os.path.join(HERE, 'export')
BLEND = os.path.join(HERE, 'blend')
TEXDIR = os.path.join(HERE, 'textures')
MANIFEST = os.path.join(HERE, 'manifest.json')
LOD_SCREEN = [1.0, 0.35, 0.12, 0.04]


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'textures': False, 'only': None, 'states': None, 'export': True}
    i = 0
    while i < len(a):
        if a[i] == '--textures':
            o['textures'] = True
        elif a[i] == '--only':
            o['only'] = a[i + 1].split(',')
            i += 1
        elif a[i] == '--states':
            o['states'] = a[i + 1].split(',')
            i += 1
        elif a[i] == '--no-export':
            o['export'] = False
        i += 1
    return o


def clear_scene():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    for m in list(bpy.data.materials):
        if not m.name.startswith('_a4_slot'):
            bpy.data.materials.remove(m)


def set_tiles(slots, spec=None):
    G.CTX['lod1_grid'] = (spec or {}).get('lod1_grid', 'SNEW')
    tiles = {}
    for i, m in enumerate(slots):
        tiles[i] = a4mat.MASTERS[m][1]
    G.CTX['tile'] = tiles


def export_fbx(coll_objs, path):
    bpy.ops.object.select_all(action='DESELECT')
    for ob in coll_objs:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = coll_objs[0]
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'EMPTY', 'MESH'},
                             mesh_smooth_type='FACE', use_tspace=True, use_mesh_modifiers=True,
                             add_leaf_bones=False, bake_anim=False, use_custom_props=True,
                             colors_type='LINEAR', axis_forward='-Z', axis_up='Y', apply_scale_options='FBX_SCALE_NONE',
                             global_scale=1.0, apply_unit_scale=True, path_mode='STRIP')


def snap_sockets(lod0, sockets):
    """Damaged state: a socket over a broken stack top is dropped onto the break (0.5 m above the highest
    vertex within 2.5 m, as on the intact tops), so the smoke does not float in the air (verifier fix, 2026-11-19)."""
    vs = [v.co for v in lod0.data.vertices]
    out = []
    for (sname, pos, kind) in sockets:
        x, y, z = pos
        near = [v.z for v in vs if (v.x - x) ** 2 + (v.y - y) ** 2 < 2.5 ** 2 and v.z < z + 0.5]
        if near and z - max(near) > 1.0:
            z = max(near) + 0.5
        out.append((sname, (x, y, z), kind))
    return out


def build_asset(key, spec, opt):
    t0 = time.time()
    clear_scene()
    slots = spec['slots']
    mats = a4mat.slot_materials(slots)
    states = spec['states'] if not opt['states'] else [s for s in spec['states'] if s in opt['states']]
    entry = {'states': {}}
    for state in states:
        mesh_name = '%s_%s' % (spec['mesh'], state)
        coll = bpy.data.collections.new(mesh_name)
        bpy.context.scene.collection.children.link(coll)
        grp = bpy.data.objects.new(mesh_name, None)
        grp['fbx_type'] = 'LodGroup'
        coll.objects.link(grp)
        objs = [grp]
        tris, checks = [], []
        sockets = []
        lod0 = None
        for lod in range(4):
            set_tiles(slots, spec)
            asm = spec['gen'](lod)
            if lod == 0:
                bb = asm.bbox()
            asm = G.apply_state(asm, state, spec.get('seed', 1), lod, spec.get('hooks'))
            ob, n = G.build_object(asm, '%s_LOD%d' % (mesh_name, lod), mats, coll)
            ob.parent = grp
            objs.append(ob)
            tris.append(n)
            checks.append(G.mesh_checks(ob))
            if lod == 0:
                lod0 = ob
                if state in ('Intact', 'Damaged'):
                    sockets = asm.sockets
                foot = (bb[1] - bb[0], bb[3] - bb[2])
                height = checks[0]['zmax']
                centre = ((bb[0] + bb[1]) / 2, (bb[2] + bb[3]) / 2)
        if state == 'Damaged' and sockets:
            sockets = snap_sockets(lod0, sockets)
        for (sname, pos, kind) in sockets:
            e = bpy.data.objects.new('SOCKET_' + sname, None)
            if e.name != 'SOCKET_' + sname:     # never let Blender suffix a socket name (.001) in the export
                e.name = 'SOCKET_' + sname
            e.empty_display_type = 'ARROWS'
            e.empty_display_size = 0.5
            e.location = pos
            e.parent = lod0
            coll.objects.link(e)
            objs.append(e)
        fbx = os.path.join(EXPORT, mesh_name + '.fbx')
        if opt['export']:
            export_fbx(objs, fbx)
        # Free the socket names for the next state. Before this, the Damaged state's empties were created
        # while the Intact ones still held the names, so Blender exported them as SOCKET_Smoke_0.001 and so
        # on, which UE would not find (verifier fix, 2026-11-19).
        for ob in objs:
            if ob.name.startswith('SOCKET_'):
                ob.name = ob.name + '@' + state
        entry['states'][state] = {
            'mesh': mesh_name, 'fbx': os.path.relpath(fbx, ROOT).replace('\\', '/'),
            'triangles': tris, 'nonmanifold_edges_lod0': checks[0]['nonmanifold_edges'],
            'zmin': round(checks[0]['zmin'], 3), 'height': round(checks[0]['zmax'], 2),
            'sockets': [{'name': 'SOCKET_' + s[0], 'kind': s[2], 'location': [round(c, 3) for c in s[1]]} for s in sockets],
        }
        if state == 'Intact':
            entry['footprint_m'] = [round(foot[0], 2), round(foot[1], 2)]
            entry['footprint_centre_offset_m'] = [round(centre[0], 2), round(centre[1], 2)]
            entry['height_m'] = round(height, 2)
        print('A4BUILD %s %s tris %s nm %d detail_uv_out %d zmin %.2f' % (key, state, tris, checks[0]['nonmanifold_edges'], checks[0]['detail_uv_out'], checks[0]['zmin']))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(BLEND, key + '.blend'), compress=True, relative_remap=True)
    meta = dict(spec.get('meta', {}))
    entry.update({
        'name': spec['mesh'], 'family': 'Age4', 'class': spec['class'], 'budget_lod0': spec['budget'],
        'materials': {'Structure': slots[0], 'Roof': slots[1], 'Detail': slots[2]},
        'textures': sorted({t for m in slots for t in a4mat.tex_files(m)}),
        # every culture, Base included, names its material instance; Base used to name the bare master,
        # which would drop the Base instance's tint (verifier fix, 2026-11-19)
        'culture_variants': {c: {role: a4mat.mi_name(m, c) for role, m in zip(('Structure', 'Roof', 'Detail'), slots)} for c in a4mat.CULTURES},
        'lod_screen_sizes': LOD_SCREEN, 'licence': 'original', 'script': 'art/age4/build_age4.py',
        'source_blend': 'art/age4/blend/%s.blend' % key,
    })
    entry.update(meta)
    print('A4BUILD %s done in %.1f s' % (key, time.time() - t0))
    return entry


def main():
    opt = args()
    os.makedirs(EXPORT, exist_ok=True)
    os.makedirs(BLEND, exist_ok=True)
    need = [os.path.join(TEXDIR, 'T_Age4_%s.png' % n) for n in ('Brick_D', 'Slate_D', 'Stone_D', 'Gravel_D', 'Trim_D', 'Trim_M')]
    if opt['textures'] or not all(os.path.exists(p) for p in need):
        a4tex.make_all(TEXDIR)
    man = {}
    if os.path.exists(MANIFEST):
        with open(MANIFEST, 'r', encoding='utf-8') as f:
            man = json.load(f)
    man.setdefault('assets', {})
    reg = a4assets.registry()
    for key, spec in reg.items():
        if opt['only'] and key not in opt['only']:
            continue
        e = build_asset(key, spec, opt)
        if opt['states'] and key in man['assets']:
            old = man['assets'][key]
            old['states'].update(e['states'])
            e['states'] = old['states']
        man['assets'][key] = e
    man['family'] = 'Age4'
    man['description'] = 'Industry age kit: brick terraces, factories and stacks, rail station, mine headframe, spoil heaps, warehouses, bridges.'
    man['conventions'] = {
        'units': 'metres (Unreal imports x100)', 'axes': 'Z up, front -Y (Unreal +X after the standard FBX import)',
        'pivot': 'footprint centre at ground level; bridges: deck centre at rail level',
        'material_slots': ['Structure', 'Roof', 'Detail'],
        'vertex_colour_Col': {'R': 'per-window random id: lit when R < instance LitFraction', 'G': 'soot / char amount (damage states)',
                              'B': 'culture accent mask (painted doors)', 'A': 'unused (1)'},
        'night_windows': 'T_Age4_Trim_M channel R (emissive mask), G per-pane random',
        'fbx': 'binary, LOD group (LOD0..LOD3), smoothing groups from 35 deg sharp edges, tangents, linear vertex colours, SOCKET_ empties under LOD0',
        'lod_screen_sizes': LOD_SCREEN,
    }
    man['masters'] = a4mat.manifest_masters()
    man['material_instances'] = a4mat.manifest_instances()
    man['ornaments'] = a4assets.ORNAMENT_INFO
    with open(MANIFEST, 'w', encoding='utf-8') as f:
        json.dump(man, f, indent=1)
    print('A4BUILD manifest written:', len(man['assets']), 'assets')


main()
