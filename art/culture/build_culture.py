# DEMIURGE art/culture - build every culture overlay mesh (all states, LODs), export FBX, write the manifest.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/culture/build_culture.py -- [--tex] [--only A2RoofDomeS,CSpire] [--no-fbx] [--no-blend]
#
#   --tex       regenerate the procedural textures (art/culture/textures) even if they exist
#   --only      comma list of asset ids (see cul_assets.registry); the manifest keeps the other entries
#   --no-fbx    skip the FBX export (fast iteration)
#
# Writes art/culture/export/SM_Culture_<Asset>_<State>.fbx (objects <name>_LOD0..LOD3 inside),
# art/culture/culture.blend (every LOD of every state, culture materials) and art/culture/manifest.json.
import os
import sys
import json
import time
import math
import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path[:0] = [HERE, os.path.join(ROOT, 'art', 'tools')]

import bpy
import bmesh
from mathutils import Vector

import cul_geo as cg
import cul_tex
import cul_mats
import cul_states as cs
import cul_assets as ca

LOD_SCREEN = [1.0, 0.35, 0.12, 0.04]
LOD_RATIO = [1.0, 0.40, 0.12, 0.04]
EXPORT_DIR = os.path.join(HERE, 'export')
TEX_DIR = os.path.join(HERE, 'textures')
BUDGET = {   # LOD0 triangle ceilings (ASSETS.md budgets, scaled for parts of a house)
    'roof': 4000, 'ornament': 1000, 'prop': 1000, 'structure': 6000, 'landmark': 20000,
}


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'tex': '--tex' in a, 'fbx': '--no-fbx' not in a, 'blend': '--no-blend' not in a, 'only': None}
    if '--only' in a:
        o['only'] = set(a[a.index('--only') + 1].split(','))
    return o


def log(*x):
    print('[culture]', *x, flush=True)


# ------------------------------------------------------------------------------ host classes from manifests

def synth_hosts(measured):
    """Houses listed in the manifests whose FBX could not be measured (not exported yet): build a host record
    from the manifest's own numbers (bounds, eave, ridge) so their overlays still exist."""
    import cul_hosts
    have = {h['mesh'] for h in measured}
    out = []
    for fam, asset, name, fbx, e, roofnames, maps_to in cul_hosts.list_hosts(ROOT):
        if name in have:
            continue
        b = e.get('bounds_lod0') or e.get('bounds_m')
        ev, rg = e.get('eave'), e.get('ridge')
        if not b or ev is None or rg is None:
            continue
        if fam == 'age3':        # [x0, y0, z0, x1, y1, z1]
            x0, y0, x1, y1 = b[0], b[1], b[3], b[4]
        else:                    # [x0, x1, y0, y1, z0, z1]
            x0, x1, y0, y1 = b[0], b[1], b[2], b[3]
        axis = 'X' if (x1 - x0) >= (y1 - y0) else 'Y'
        out.append(dict(id='%s%s' % (fam.capitalize(), asset.replace('_', '')), family=fam.capitalize(), age=int(fam[-1]),
                        asset=asset, mesh=name, fbx=e.get('fbx'), maps_to=maps_to, roof_by='manifest numbers (FBX missing)',
                        roof=dict(min=[x0, y0, ev], max=[x1, y1, rg], eave_z=ev, ridge_z=rg, ridge_axis=axis)))
    return out


# ------------------------------------------------------------------------------ build

def finish(ob):
    """UVs, triangulation, smoothing; returns triangle count."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    cg.finish_uvs(bm)
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges[:], dist=1e-5)
    bm.to_mesh(ob.data)
    bm.free()
    cg.shade(ob, 38.0)
    return cg.tri_count(ob)


def clean_lod(ob):
    """Verifier fix (2026-11-19): collapse decimation and the LOD3 hull leave junk that Blender keeps but the
    engine should not get: folded flaps (two faces on the same three vertices; keep the one facing up, toward
    the game camera), degenerate faces, loose vertices and edges, and closed islands wound inside out (the
    convex-hull LOD3 of the pier temple came out inverted). Returns the number of faces removed / flipped."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    seen, kill = {}, set()
    for f in bm.faces:
        k = tuple(sorted(v.index for v in f.verts))
        g = seen.get(k)
        if g is None:
            seen[k] = f
        elif f.normal.z > g.normal.z:
            kill.add(g)
            seen[k] = f
        else:
            kill.add(f)
    if kill:
        bmesh.ops.delete(bm, geom=list(kill), context='FACES_ONLY')
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges[:], dist=1e-5)
    ng = [f for f in bm.faces if len(f.verts) > 3]
    if ng:
        bmesh.ops.triangulate(bm, faces=ng, quad_method='BEAUTY', ngon_method='BEAUTY')
    bmesh.ops.delete(bm, geom=[e for e in bm.edges if not e.link_faces], context='EDGES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.faces.ensure_lookup_table()
    flipped = 0
    seen_f = set()
    for f0 in bm.faces:
        if f0 in seen_f:
            continue
        isl, st = [], [f0]
        seen_f.add(f0)
        while st:
            x = st.pop()
            isl.append(x)
            for e in x.edges:
                for y in e.link_faces:
                    if y not in seen_f:
                        seen_f.add(y)
                        st.append(y)
        if not all(e.is_manifold for x in isl for e in x.edges):
            continue
        vol = 0.0
        for x in isl:
            vs = [v.co for v in x.verts]
            for k in range(1, len(vs) - 1):
                vol += vs[0].dot(vs[k].cross(vs[k + 1]))
        if vol < -1e-6:
            bmesh.ops.reverse_faces(bm, faces=isl, flip_multires=False)
            flipped += len(isl)
    bm.normal_update()
    bm.to_mesh(ob.data)
    bm.free()
    cg.shade(ob, 38.0)
    return len(kill), flipped


def nonmanifold(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    n = sum(1 for e in bm.edges if not e.is_manifold)
    bm.free()
    return n


def build_asset(e, coll, mats):
    """-> {state: [lod objects]} and stats."""
    res = {}
    for st in e['states']:
        lods = []
        t0 = None
        for lod in range(3):
            name = 'SM_Culture_%s_%s_LOD%d' % (e['asset'], st, lod)
            ob = cs.build(e['fn'], lod, st, seed=_seed(e['asset']), name=name, mats=mats, coll=coll, **state_args(e))
            t = finish(ob)
            if lod == 0:
                t0 = t
            else:
                target = int(t0 * LOD_RATIO[lod])
                if t > target * 1.3:
                    t = cg.decimate_to(ob, target)
                    cg.shade(ob, 38.0)
            lods.append(ob)
        res[st] = lods
    # LOD3 for anything over 10 m. Verifier fix: decided per asset, not per state, so every state of an asset
    # has the same LOD count (a Damaged roof used to get a LOD3 its Intact twin lacked).
    def size(ob):
        mn, mx = cg.bbox(ob)
        return max(mx.x - mn.x, mx.y - mn.y, mx.z - mn.z)
    big = any(size(l[0]) > 10.0 for l in res.values())
    for st, lods in res.items():
        if big:
            t0 = cg.tri_count(lods[0])
            name = 'SM_Culture_%s_%s_LOD3' % (e['asset'], st)
            l3 = lods[2].copy()
            l3.data = lods[2].data.copy()
            l3.name = name
            l3.data.name = name
            coll.objects.link(l3)
            tgt3 = max(24, int(t0 * LOD_RATIO[3]))
            if cg.decimate_to(l3, tgt3) > tgt3 * 2.5:
                hull_lod(l3, tgt3)
            cg.shade(l3, 38.0)
            lods.append(l3)
        for l in lods:
            clean_lod(l)
    return res


def hull_lod(ob, target):
    """LOD3 fallback when collapse decimation stalls (many small closed parts): the convex hull, in the
    material that covers most of the surface, box-mapped."""
    from collections import Counter
    area = Counter()
    for p in ob.data.polygons:
        area[p.material_index] += p.area
    mi = area.most_common(1)[0][0] if area else 0
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    for f in list(bm.faces):
        pass
    ret = bmesh.ops.convex_hull(bm, input=bm.verts[:], use_existing_faces=False)
    hull = set(g for g in ret['geom'] if isinstance(g, bmesh.types.BMFace))
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f not in hull], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    for f in bm.faces:
        f.material_index = mi
    lay = bm.faces.layers.int.get('uvm')
    if lay:
        for f in bm.faces:
            f[lay] = cg.UV_BOX
    bm.normal_update()
    cg.finish_uvs(bm)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(ob.data)
    bm.free()
    cg.decimate_to(ob, target)


def state_args(e):
    """kind / ground level / footprint / build direction for cul_states.build."""
    k = e['kind']
    if k == 'roof':        # host-space overlay: the host ground is z = 0
        rs = e['host'].get('roofs') or [e['host']['roof']]
        return dict(kind='roof', eave=0.0, foot=(min(r['min'][0] for r in rs), min(r['min'][1] for r in rs),
                                                 max(r['max'][0] for r in rs), max(r['max'][1] for r in rs)))
    if k == 'roofeave':    # pivot on the eave plane of one of our own drums
        return dict(kind='roof', eave=e['host']['eave_c'], foot=(2 * e['host']['foot'], 2 * e['host']['foot']))
    ax = 'x' if any(t in e['asset'] for t in ('Stilts', 'Walkway', 'Bridge')) else 'z'
    return dict(kind='struct', build_axis=ax)


def _seed(s):
    h = 2166136261
    for ch in s:
        h = ((h ^ ord(ch)) * 16777619) & 0xffffffff
    return h % 100003


EXPORT_MATS = {}


def export_fbx(lods, path):
    for n in cg.MATS:
        if n not in EXPORT_MATS:
            m = bpy.data.materials.get(n) or bpy.data.materials.new(n)
            EXPORT_MATS[n] = m
    tmp = []
    names = [ob.name for ob in lods]
    for ob in lods:
        ob.name = ob.name + '__src'
    for ob, nm in zip(lods, names):
        me = ob.data.copy()
        idx = [0] * len(me.polygons)
        me.polygons.foreach_get('material_index', idx)
        used = sorted(set(idx))
        remap = {u: k for k, u in enumerate(used)}
        me.materials.clear()
        for u in used:
            me.materials.append(EXPORT_MATS[cg.MATS[u]])
        me.polygons.foreach_set('material_index', [remap[i] for i in idx])
        o = bpy.data.objects.new(nm, me)
        bpy.context.scene.collection.objects.link(o)
        tmp.append(o)
    # Verifier fix: an FBX LodGroup node (empty with fbx_type = LodGroup, identity transform) parents the
    # LOD meshes, as in every other family, so Unreal's "Import Mesh LODs" reads LOD0..LOD3 as one mesh's LODs
    # instead of combining them or importing four meshes.
    grp = bpy.data.objects.new(os.path.splitext(os.path.basename(path))[0], None)
    grp['fbx_type'] = 'LodGroup'
    bpy.context.scene.collection.objects.link(grp)
    for o in tmp:
        o.parent = grp
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    grp.select_set(True)
    for o in tmp:
        o.select_set(True)
    bpy.context.view_layer.objects.active = grp
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'EMPTY', 'MESH'}, use_mesh_modifiers=True,
                             mesh_smooth_type='FACE', use_tspace=True, use_triangles=True, add_leaf_bones=False,
                             bake_anim=False, path_mode='STRIP', apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y',
                             use_custom_props=False, embed_textures=False)
    for o in tmp:
        me = o.data
        bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.meshes.remove(me)
    bpy.data.objects.remove(grp, do_unlink=True)
    for ob, nm in zip(lods, names):
        ob.name = nm


def used_slots(ob):
    idx = [0] * len(ob.data.polygons)
    ob.data.polygons.foreach_get('material_index', idx)
    return [cg.MATS[i] for i in sorted(set(idx))]


def textures_of(slots_mi):
    out = []
    for slot, mi in slots_mi.items():
        s = cul_mats.MI[mi][0]
        if s:
            for k, p in cul_mats.tex_files(s, ROOT).items():
                out.append(os.path.relpath(p, ROOT).replace('\\', '/'))
    return sorted(set(out))


def main():
    o = args()
    t_start = time.time()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    need_tex = o['tex'] or not os.path.exists(os.path.join(TEX_DIR, 'T_Culture_Trim_M.png')) or \
        not os.path.exists(os.path.join(TEX_DIR, 'T_Culture_Rock_D.png'))
    if need_tex:
        log('textures ...')
        cul_tex.build_all(TEX_DIR, log=log)
    os.makedirs(EXPORT_DIR, exist_ok=True)

    man_path = os.path.join(HERE, 'manifest.json')
    old = {}
    if o['only'] and os.path.exists(man_path):
        with open(man_path, encoding='utf-8') as f:
            old = {m['name']: m for m in json.load(f).get('meshes', [])}

    import cul_hosts
    hosts = cul_hosts.analyse(ROOT, os.path.join(HERE, 'hosts.json'), log=log)
    hosts = hosts + synth_hosts(hosts)
    log('host houses from the Age2/Age3 manifests:', ', '.join(h['id'] for h in hosts) or 'none (nominal stand-ins)')
    reg = ca.registry(hosts)
    if o['only']:
        reg = [e for e in reg if e['asset'] in o['only']]

    meshes = []
    problems = []
    for e in reg:
        t0 = time.time()
        coll = bpy.data.collections.new(e['asset'])
        sc.collection.children.link(coll)
        mats = cul_mats.mats_for(e['mats'], ROOT)
        res = build_asset(e, coll, mats)
        for st, lods in res.items():
            name = 'SM_Culture_%s_%s' % (e['asset'], st)
            tris = [cg.tri_count(l) for l in lods]
            mn, mx = cg.bbox(lods[0])
            nm = nonmanifold(lods[0])
            slots = used_slots(lods[0])
            bud = BUDGET[e['role']]
            if tris[0] > bud:
                problems.append('%s LOD0 %d tris over the %d budget' % (name, tris[0], bud))
            fbx = os.path.join(EXPORT_DIR, name + '.fbx')
            if o['fbx']:
                export_fbx(lods, fbx)
            # restore names if the export copies stole them
            for k, l in enumerate(lods):
                want = '%s_LOD%d' % (name, k)
                if l.name != want:
                    l.name = want
            slot_mi = {s: e['mats'][s] for s in slots if s in e['mats']}
            entry = dict(
                name=name, asset=e['asset'], state=st, culture=e['culture'], culture_name=ca.CULTURES[e['culture']]['name'],
                ages=e['ages'], overlays=['Age%d' % a for a in e['ages']], role=e['role'],
                footprint_m=[round(mx.x - mn.x, 2), round(mx.y - mn.y, 2)], height_m=round(mx.z - mn.z, 2),
                bounds_min=[round(mn.x, 2), round(mn.y, 2), round(mn.z, 2)],
                bounds_max=[round(mx.x, 2), round(mx.y, 2), round(mx.z, 2)],
                pivot=('host pivot (overlay modelled in the host mesh space: centre of the host footprint, host ground z = 0)'
                       if e['kind'] == 'roof' else 'drum house eave plane (z = 0), drum axis' if e['kind'] == 'roofeave' else
                       ('bottom centre of the host doorway on the wall face' if e['role'] == 'ornament' and 'Attach' in e['sockets'] and 'door' in e['sockets']['Attach']
                        else ('host front wall face, bottom centre of the panel' if e['role'] == 'ornament'
                              else 'centre of the footprint at ground / water level (z = 0)'))),
                triangles=tris, lod_objects=['%s_LOD%d' % (name, k) for k in range(len(lods))],
                lod_screen_sizes=LOD_SCREEN[:len(lods)],
                materials=slot_mi, textures=textures_of(slot_mi),
                states=e['states'], culture_variants=[e['culture']],
                description=e['desc'], sockets=e['sockets'],
                nonmanifold_edges_lod0=nm,
                fbx=os.path.relpath(fbx, ROOT).replace('\\', '/'),
                licence='original', script='art/culture/build_culture.py',
            )
            if e['kind'] == 'roof' or (e.get('host') and 'roof' in e['host']):
                h = e['host']
                entry['host'] = dict(mesh=h.get('mesh'), family=h['family'], asset=h['asset'], fbx=h.get('fbx'),
                                     maps_to=h.get('maps_to'), measured_roofs=h.get('roofs') or [h['roof']],
                                     measured_by=h.get('roof_by', 'manifest'),
                                     placement='same transform as the host mesh (overlay is modelled in host space)')
                if st == 'Ruin':
                    entry['note'] = 'collapsed roof debris on the host ground; pair with the host Ruin state'
                if st == 'Construction':
                    entry['note'] = 'roofing material and cut timbers waiting on the ground; pair with the host Construction state'
            elif e['kind'] == 'roofeave':
                entry['note'] = 'sits on the drum house: place at the drum transform + RoofBase socket'
            if e['role'] == 'ornament' or e['kind'] == 'prop':
                entry['state_rule'] = 'Intact only: keep for host Intact and Damaged, hide for Construction and Ruin'
            meshes.append(entry)
            old.pop(name, None)
        log('%-22s %5.1fs  %s' % (e['asset'], time.time() - t0,
                                  '  '.join('%s:%s' % (st[:3], '/'.join(str(cg.tri_count(l)) for l in lods)) for st, lods in res.items())))

    meshes = sorted(list(old.values()) + meshes, key=lambda m: m['name'])
    # cleanup scratch
    sc_coll = bpy.data.collections.get('_scratch')
    if sc_coll:
        for ob in list(sc_coll.objects):
            bpy.data.objects.remove(ob, do_unlink=True)

    man = dict(
        family='Culture',
        description='Culture overlays for the Age2 (Iron) and Age3 (Sail) kits: roof, ornament and structural variant '
                    'meshes plus material instances that turn the Age2/Age3 houses into the three invented cultures.',
        built=datetime.date.today().isoformat(), script='art/culture/build_culture.py', licence='original',
        units='metres in Blender (Unreal x100), Z up, front -Y',
        cultures={k: dict(v, palette=_palette(k)) for k, v in ca.CULTURES.items()},
        lod_screen_sizes=LOD_SCREEN,
        hosts_measured=[{k: h[k] for k in ('id', 'mesh', 'fbx', 'family', 'roof_by', 'roofs') if k in h} for h in hosts],
        master_material=dict(name='M_Culture_Standard', inputs=['BaseColor (D, sRGB)', 'Normal (N, OpenGL +Y: flip green on import)',
                                                                  'ORM (linear: AO, Roughness, Metallic)', 'Mask (M, linear: R night windows, G accent tint, B cloth)',
                                                                  'Tint (linear RGB)', 'RoughnessScale', 'AccentTint (linear RGB)', 'Night (scalar)']),
        material_instances={k: dict(parent='M_Culture_Standard' if v[0] else 'shared water material',
                                    textures=({kk: os.path.relpath(p, ROOT).replace('\\', '/') for kk, p in cul_mats.tex_files(v[0], ROOT).items()} if v[0] else {}),
                                    tint=list(v[1]), roughness_scale=v[2], accent=list(cul_mats.ACCENT[k[-1]]), note=v[3])
                            for k, v in cul_mats.MI.items()},
        host_culture_materials=_host_mis(),
        placement=_placement_notes(),
        meshes=meshes,
    )
    with open(man_path, 'w', encoding='utf-8') as f:
        json.dump(man, f, indent=1)
    if problems:
        log('PROBLEMS:')
        for p in problems:
            log('  ', p)
    if not o['only'] and o['fbx']:
        # remove exports of meshes that no longer exist (e.g. a host the Age2/Age3 kits dropped)
        live = {m['name'] + '.fbx' for m in meshes}
        for fn_ in os.listdir(EXPORT_DIR):
            if fn_.startswith('SM_Culture_') and fn_.endswith('.fbx') and fn_ not in live:
                os.remove(os.path.join(EXPORT_DIR, fn_))
                log('removed stale export', fn_)
    if o['blend']:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, 'culture.blend'), compress=True)
    log('done: %d meshes, %.0fs' % (len(meshes), time.time() - t_start))


def _palette(k):
    return {
        'A': {'plaster': '#c48f6c', 'dome limewash': '#cf9f80', 'sandstone': '#9c5b42', 'cobalt': '#2f4fa8', 'greenery': 'palm green'},
        'B': {'shingle': '#8f949a', 'moss': '#5f6b3a', 'tarred timber': '#3a2f27', 'reed': '#7a5c3c', 'saffron': '#c88f2e', 'water': 'turquoise'},
        'C': {'limestone': '#cdb894', 'slate': '#3c4048', 'dry-stone': '#9a9488', 'moor': 'olive with purple heather'},
    }[k]


def _host_mis():
    """The host kits re-skin themselves per culture (their own manifests define it); record what to pair."""
    out = {'note': 'Hosts keep their own culture materials (read from their manifests, never edited here). Pair a '
                   'host in culture X with its culture-X material set below and with the SM_Culture_X* overlays.'}
    for fam in ('age2', 'age3'):
        p = os.path.join(ROOT, 'art', fam, 'manifest.json')
        if not os.path.exists(p):
            continue
        try:
            m = json.load(open(p, encoding='utf-8'))
        except Exception:
            continue
        d = {}
        mis = m.get('material_instances') or {}
        for c in 'ABC':
            names = sorted(k for k in mis if k.endswith('_' + c))
            if names:
                d[c] = names
            elif isinstance(m.get('cultures'), dict) and ('Culture' + c) in m['cultures']:
                d[c] = {'parameters': 'Culture' + c, 'values': m['cultures']['Culture' + c]}
        out[fam.capitalize()] = d
    ov = {}
    for fam, mats in cul_mats.HOST_WALL_MATS.items():
        for c, (tex, note) in cul_mats.HOST_OVERRIDE.items():
            for mname in mats:
                ov['MI_Culture_%s%s_%s' % (fam, mname.split('_')[-1], c)] = dict(
                    parent=mname, replaces='%s in culture %s (instead of the host kit tint-only instance)' % (mname, c),
                    textures={k: os.path.relpath(p, ROOT).replace(os.sep, '/') for k, p in cul_mats.tex_files(tex, ROOT).items()},
                    uv_tile_m=4.0, note=note)
    out['wall_overrides'] = ov
    return out


def _placement_notes():
    return {
        'A': 'Stack houses up slopes on ATerraceStair blocks (TerraceTop socket); every house wears A2/A3RoofDome* so its '
             'roof is the terrace of the house above; A*Ornament on each front door; ACistern on every 2nd-3rd terrace; '
             'ATempleNiche replaces the Age2/Age3 temple facade (set against the slope). Host walls: MI_Age*_Structure_A.',
        'B': 'Every house stands on B*Stilts* (HostBase socket, deck +1.8 m over water) and wears B*RoofSaddle*; houses '
             'cluster in rings of 5-8 round open water courts linked by BWalkwayCurve / BWalkwayStraight and BBridge over '
             'channels; BGardenRaft scattered on the water; BBreakwater segments along the seaward side; BTempleSpiral at '
             'the centre (replaces the temple).',
        'C': 'Houses are C*DrumHouse + C*RoofCone (or rectangular hosts with C<age>Roof<Host>) snapped to concentric rings '
             'round a central plaza; CRingWallCurve between rings, CRingWallStraight along the radial streets; CSpire at '
             'the centre beside the granary; CStandingStone1-3 along the approach road.',
    }


if __name__ == '__main__':
    main()
