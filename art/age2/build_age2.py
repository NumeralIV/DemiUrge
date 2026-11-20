# DEMIURGE art - Age2 (Iron Age) kit build script.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/age2/build_age2.py -- [--textures] [--assets A,B] [--no-export] [--previews [names]]
#
# Writes textures (art/age2/textures), meshes with LOD0..3 per damage state as FBX
# (art/age2/export/SM_Age2_<Asset>_<State>.fbx, one LOD group per file), the source scene
# (art/age2/source/age2.blend) and art/age2/manifest.json. --previews renders the preview images to
# shots/asset-age2-*.png; run it through tools/render_slot.py.
import os
import sys
import json
import math
import time
import datetime
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'art', 'tools'))
sys.path.insert(0, HERE)

import bpy
import bmesh
from mathutils import Vector

import age2_tex as tx
import age2_kit as kit
import age2_assets as A

TEX = os.path.join(HERE, 'textures')
EXP = os.path.join(HERE, 'export')
SRC = os.path.join(HERE, 'source')
SHOTS = os.path.join(ROOT, 'shots')
REL = lambda p: os.path.relpath(p, ROOT).replace('\\', '/')

LOD_SCREEN = [1.0, 0.35, 0.12, 0.04]
LOD_TARGET = [1.0, 0.40, 0.12]
SLOT_NAMES = ['Structure', 'Roof', 'Detail']

# culture material-instance parameters (tint is a linear multiplier on base colour; saturation
# scales chroma; roughness_scale multiplies roughness; texture_override swaps the texture set)
CULTURES = {
    'Valley': {},     # default (the age-2 concept valley town)
    'A': {   # Cliff-Terrace people: warm rose-ochre stone, sun-bleached roofs
        'Rubble': dict(tint=(1.16, 0.98, 0.82), saturation=1.15), 'Ashlar': dict(tint=(1.14, 0.98, 0.84), saturation=1.2),
        'Limestone': dict(tint=(1.06, 0.95, 0.84), saturation=1.25),
        'Thatch': dict(tint=(1.15, 1.02, 0.84)), 'Shingle': dict(tint=(1.25, 1.05, 0.85)), 'TileClay': dict(tint=(1.1, 0.95, 0.85)),
        'Trim': dict(tint=(1.05, 1.0, 0.95)),
    },
    'B': {   # Lagoon-Weaver people: silver-grey weathered wood, mossy, cool
        'Rubble': dict(tint=(0.92, 0.97, 1.0), saturation=0.75), 'Ashlar': dict(tint=(0.95, 0.98, 1.0), saturation=0.8),
        'Limestone': dict(tint=(0.96, 0.98, 1.0), saturation=0.8),
        'Thatch': dict(tint=(0.9, 0.95, 0.92), saturation=0.6), 'Shingle': dict(tint=(1.35, 1.38, 1.42), saturation=0.35),
        'TileClay': dict(tint=(0.95, 0.95, 0.95), saturation=0.7), 'Trim': dict(tint=(0.95, 0.98, 1.0), saturation=0.85),
    },
    'C': {   # Ring-Builder people: pale limestone, blue-grey slate roofs
        'Rubble': dict(tint=(1.28, 1.24, 1.16), saturation=0.6), 'Ashlar': dict(tint=(1.2, 1.18, 1.14), saturation=0.6),
        'Limestone': dict(tint=(1.02, 1.02, 1.02), saturation=0.7),
        'Thatch': dict(texture_override='Slate', tint=(0.95, 1.0, 1.08)), 'Shingle': dict(texture_override='Slate', tint=(0.95, 1.0, 1.08)),
        'TileClay': dict(texture_override='Slate', tint=(1.0, 1.0, 1.05)), 'Trim': dict(saturation=0.9),
    },
}


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'textures': False, 'assets': None, 'export': True, 'previews': None, 'blend': True, 'states': None}
    i = 0
    while i < len(a):
        k = a[i]
        if k == '--textures':
            o['textures'] = True
        elif k == '--assets':
            o['assets'] = a[i + 1].split(',')
            i += 1
        elif k == '--states':
            o['states'] = a[i + 1].split(',')
            i += 1
        elif k == '--no-export':
            o['export'] = False
        elif k == '--no-blend':
            o['blend'] = False
        elif k == '--previews':
            o['previews'] = []
            if i + 1 < len(a) and not a[i + 1].startswith('--'):
                o['previews'] = a[i + 1].split(',')
                i += 1
        i += 1
    return o


# ---------------------------------------------------------------------- materials


def _img(path, noncolor):
    name = os.path.basename(path)
    im = bpy.data.images.get(name)
    if im is None:
        im = bpy.data.images.load(path, check_existing=True)
    im.colorspace_settings.name = 'Non-Color' if noncolor else 'sRGB'
    return im


def material(set_name, culture='Valley', night=0.0, backface=True):
    p = CULTURES.get(culture, {}).get(set_name, {})
    tset = p.get('texture_override', set_name)
    name = 'M_Age2_%s' % set_name if culture == 'Valley' else 'MI_Age2_%s_%s' % (set_name, culture)
    if night > 0:
        name += '_Night'
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.use_backface_culling = backface
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bs = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bs.outputs['BSDF'], out.inputs['Surface'])
    base = os.path.join(TEX, 'T_Age2_%s' % tset)
    td = nt.nodes.new('ShaderNodeTexImage')
    td.image = _img(base + '_D.png', False)
    tn = nt.nodes.new('ShaderNodeTexImage')
    tn.image = _img(base + '_N.png', True)
    to = nt.nodes.new('ShaderNodeTexImage')
    to.image = _img(base + '_ORM.png', True)
    vc = nt.nodes.new('ShaderNodeVertexColor')
    vc.layer_name = 'Col'
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(to.outputs['Color'], sep.inputs['Color'])
    # base = D * tint * vertexcol * lerp(1, AO, 0.7), with optional saturation
    col = td.outputs['Color']
    sat = p.get('saturation', 1.0)
    if abs(sat - 1.0) > 1e-3:
        hs = nt.nodes.new('ShaderNodeHueSaturation')
        hs.inputs['Saturation'].default_value = sat
        nt.links.new(col, hs.inputs['Color'])
        col = hs.outputs['Color']
    m1 = nt.nodes.new('ShaderNodeVectorMath')
    m1.operation = 'MULTIPLY'
    nt.links.new(col, m1.inputs[0])
    nt.links.new(vc.outputs['Color'], m1.inputs[1])
    m2 = nt.nodes.new('ShaderNodeVectorMath')
    m2.operation = 'MULTIPLY'
    nt.links.new(m1.outputs[0], m2.inputs[0])
    m2.inputs[1].default_value = p.get('tint', (1.0, 1.0, 1.0))
    aom = nt.nodes.new('ShaderNodeMapRange')
    aom.inputs['To Min'].default_value = 0.3
    nt.links.new(sep.outputs['Red'], aom.inputs['Value'])
    m3 = nt.nodes.new('ShaderNodeVectorMath')
    m3.operation = 'SCALE'
    nt.links.new(m2.outputs[0], m3.inputs[0])
    nt.links.new(aom.outputs['Result'], m3.inputs['Scale'])
    nt.links.new(m3.outputs[0], bs.inputs['Base Color'])
    rs = nt.nodes.new('ShaderNodeMath')
    rs.operation = 'MULTIPLY'
    rs.inputs[1].default_value = p.get('roughness_scale', 1.0)
    nt.links.new(sep.outputs['Green'], rs.inputs[0])
    nt.links.new(rs.outputs[0], bs.inputs['Roughness'])
    nt.links.new(sep.outputs['Blue'], bs.inputs['Metallic'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nt.links.new(tn.outputs['Color'], nm.inputs['Color'])
    nt.links.new(nm.outputs['Normal'], bs.inputs['Normal'])
    if tset == 'Trim' and night > 0:
        tm = nt.nodes.new('ShaderNodeTexImage')
        tm.image = _img(base + '_M.png', True)
        sm = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(tm.outputs['Color'], sm.inputs['Color'])
        bs.inputs['Emission Color'].default_value = (1.0, 0.55, 0.22, 1.0)
        nt.links.new(sm.outputs['Red'], bs.inputs['Emission Strength'])
    return m


# ---------------------------------------------------------------------- meshes


RIDGES = {}


def build_mesh(fn, tiles, lod, state, name):
    b = kit.Builder(lod=lod, state=state, seed=zlib.crc32(name.encode()) % 1000, tiles=tiles)
    fn(b)
    me = kit.to_mesh(b, name, tiles)
    if lod == 0:
        RIDGES[name] = [[[round(c, 3) for c in p] for p in r] for r in b.ridges]
    return me


def decimate_to(me, ratio, name):
    ob = bpy.data.objects.new('_dec', me)
    bpy.context.scene.collection.objects.link(ob)
    mod = ob.modifiers.new('d', 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = ratio
    mod.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    nm = bpy.data.meshes.new_from_object(ev)
    bpy.data.objects.remove(ob)
    bpy.data.meshes.remove(me)
    nm.name = name
    return nm


def _area_bbox(me):
    a = sum(p.area for p in me.polygons)
    if not me.vertices:
        return a, (0.0, 0.0, 0.0)
    bb = bounds(me)
    return a, (bb[1] - bb[0], bb[3] - bb[2], bb[5] - bb[4])


def safe_decimate(me, tgt, name):
    """Collapse-decimate toward `tgt` triangles, but only keep a result that preserves the shape:
    surface area >= 92 % and every bounding-box extent within 3 % (or 0.15 m) of the hand-made LOD.
    Collapse decimation wrecked thin, blocky parts (the wall segment lost its whole body at LOD2,
    the plain tower's LOD1 was torn); a milder ratio is tried twice before falling back to the
    hand-made LOD as built. (Verification fix, 2026-11-19.)"""
    raw = len(me.polygons)
    a0, b0 = _area_bbox(me)
    ratio = tgt / raw
    for _ in range(3):
        cand = decimate_to(me.copy(), ratio, name + '_try')
        a1, b1 = _area_bbox(cand)
        ok = a1 >= 0.92 * a0 and all(abs(x - y) <= max(0.15, 0.03 * x) for x, y in zip(b0, b1))
        if ok:
            bpy.data.meshes.remove(me)
            cand.name = name
            return cand
        bpy.data.meshes.remove(cand)
        ratio = (ratio + 1.0) / 2.0
        if ratio > 0.9:
            break
    return me


def clean(me):
    """Drop zero-area / degenerate triangles (decimation leaves some; FBX export discards them,
    so they must go before the triangle counts are recorded)."""
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=bm.edges[:])
    bm.verts.index_update()
    seen = set()
    bad = []
    for f in bm.faces:
        k = tuple(sorted(v.index for v in f.verts))
        if f.calc_area() < 1e-7 or k in seen:
            bad.append(f)
        seen.add(k)
    if bad:
        bmesh.ops.delete(bm, geom=bad, context='FACES')
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    me.update()


def assign_slots(me, mats_by_slot):
    """Always the three slots Structure, Roof, Detail in that order, on every state and LOD, so a
    slot index means the same thing on Intact, Ruin and every LOD (culture material instances are
    applied by index). Returns the slots that carry faces. (Verification fix: unused slots used to
    be dropped, which shifted Detail to index 1 on ruins and to 0 on props.)"""
    idx = [p.material_index for p in me.polygons]
    used = sorted(set(idx))
    me.materials.clear()          # (clearing resets the face indices: restore them below)
    for m in mats_by_slot:
        me.materials.append(m)
    me.polygons.foreach_set('material_index', idx)
    me.update()
    return [SLOT_NAMES[s] for s in used]


def bounds(me):
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))


def build_asset(asset, states, coll, export=True):
    fn, stex, rtex, kind = A.ASSETS[asset]
    tiles = {kit.S: tx.TILE[stex], kit.R: tx.TILE[rtex]}
    mats = [material(stex), material(rtex), material('Trim')]
    rows = {}
    nl = None
    for state in states:
        if kind == 'prop' and state != 'Intact':
            continue
        base = 'SM_Age2_%s_%s' % (asset, state)
        lods = []
        me0 = build_mesh(fn, tiles, 0, state, base + '_LOD0')
        t0 = len(me0.polygons)
        lods.append(me0)
        bb = bounds(me0)
        size = max(bb[1] - bb[0], bb[3] - bb[2])
        if nl is None:   # LOD3 hull on every building (all exceed or approach 10 m with dressing)
            nl = 3 if kind == 'prop' else 4
        for lod in range(1, nl):
            me = build_mesh(fn, tiles, lod, state, base + '_LOD%d' % lod)
            if lod <= 2:
                tgt = LOD_TARGET[lod] * t0
                if len(me.polygons) > tgt * 1.35:
                    me = safe_decimate(me, tgt, base + '_LOD%d' % lod)
            lods.append(me)
        for me in lods:
            clean(me)
        slots = None
        for me in lods:
            s = assign_slots(me, mats)
            if slots is None:
                slots = s
        # objects: an empty carrying the FBX LodGroup attribute, LOD meshes as children
        grp = bpy.data.objects.new(base, None)
        grp['fbx_type'] = 'LodGroup'
        coll.objects.link(grp)
        obs = []
        for i, me in enumerate(lods):
            ob = bpy.data.objects.new(base + '_LOD%d' % i, me)
            ob.parent = grp
            coll.objects.link(ob)
            obs.append(ob)
        if export:
            os.makedirs(EXP, exist_ok=True)
            for o in bpy.context.view_layer.objects:
                o.select_set(False)
            grp.select_set(True)
            for o in obs:
                o.select_set(True)
            bpy.context.view_layer.objects.active = grp
            bpy.ops.export_scene.fbx(filepath=os.path.join(EXP, base + '.fbx'), use_selection=True,
                                     object_types={'EMPTY', 'MESH'}, mesh_smooth_type='FACE', use_tspace=True,
                                     use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
                                     apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
                                     axis_forward='-Z', axis_up='Y', use_custom_props=False, path_mode='STRIP')
        rows[state] = dict(name=base, lods=lods, tris=[len(m.polygons) for m in lods], bounds=bb, slots=slots,
                           mats=[m.name for m in mats], ridges=RIDGES.get(base + '_LOD0', []))
        print('  %-34s tris %s' % (base, [len(m.polygons) for m in lods]))
    return rows


# ---------------------------------------------------------------------- manifest

SIM_MAP = {
    'House_A': 'EDmTownMesh::Cottage (Age 2, small); dm house tiers 0-1',
    'House_B': 'EDmTownMesh::Cottage (Age 2, long) / Longhouse replacement in Age 2',
    'House_C': 'EDmTownMesh::Cottage (Age 2, two storeys); town cores, tier >= 2',
    'House_D': 'EDmTownMesh::Cottage (Age 2, farmstead); town edges and villages',
    'WallSeg': 'EDmTownMesh::WallSeg variant 1 (Iron); same 16 m unit along X, outer face -Y, planner scales X',
    'WallTower_A': 'EDmTownMesh::WallTower variant 1 (Iron), crenellated',
    'WallTower_B': 'EDmTownMesh::WallTower variant 1 alternate (hoarded, roofed) - mix 1 in 3, or for Age 2 culture variety',
    'Gatehouse': 'EDmTownMesh::Gatehouse variant 1 (Iron); road along Y through the arch',
    'Market': 'EDmTownMesh::Market (Age 2-3), dm::Building::MARKET',
    'Temple': 'EDmTownMesh::TempleClassic variant 0 (Age 2), dm::Building::TEMPLE',
    'Academy': 'EDmTownMesh::Academy (Age 2 form), dm::Building::ACADEMY',
    'MarketStall_A': 'market dressing / scatter prop (gable awning stall)',
    'MarketStall_B': 'market dressing / scatter prop (lean-to canopy stall)',
    'MarketStall_C': 'market dressing / scatter prop (cart stall)',
    'Ornament_A': 'culture A (Cliff-Terrace) ridge ornament: attach at each house ridge end (manifest ridge_ends)',
    'Ornament_B': 'culture B (Lagoon-Weaver) ridge ornament: attach at each ridge end, local -Y pointing outward along the ridge',
    'Ornament_C': 'culture C (Ring-Builder) ridge ornament: attach at each ridge end',
}

DESC = {
    'House_A': 'Small rubble-stone cottage with a steep reed-thatch gable roof and a stone chimney.',
    'House_B': 'Stone longhouse with a dark split-shingle roof, a shingled door porch and a central chimney.',
    'House_C': 'Two-storey house: rubble ground floor, jettied timber-framed lime-plaster upper floor, shingle roof gable-end to the street.',
    'House_D': 'Farmstead: thatched hip-roofed hall, thatched back wing, low walled yard and a woodshed.',
    'WallSeg': '16 m town-wall unit: 3.2 m thick, 7.6 m walkway, crenellated outer parapet, battered base; tiles along X.',
    'WallTower_A': '9 x 9 m hollow square wall tower, 15 m, crenellated top, arrow slits, doors to the wall walk.',
    'WallTower_B': '9 x 9 m square wall tower with timber hoarding on corbels and a pyramidal clay-tile roof.',
    'Gatehouse': 'Twin-tower gatehouse: round-arched passage with portcullis and open gate leaves, timber gallery roof, hoarded towers.',
    'Market': 'Colonnaded market square: clay-tiled galleries on timber posts round a paved court, striped-awning stalls, round fountain.',
    'Temple': 'Peripteral stone temple, 6 x 11 columns on a three-step stylobate, pale stone-slab roof with pediments, altar in front.',
    'Academy': 'Cloistered limestone quadrangle with slate roofs, garden court with an armillary sphere, columned porch and a verdigris-domed observatory tower.',
    'MarketStall_A': 'Market stall: counter on four posts under a madder-striped gable awning.',
    'MarketStall_B': 'Market stall: lean-to ochre-striped canopy over a counter with a back board and baskets.',
    'MarketStall_C': 'Market stall: two-wheeled hand cart with an indigo-striped flat canopy.',
    'Ornament_A': 'Ridge finial, Cliff-Terrace culture: plaster dome with a cobalt knob.',
    'Ornament_B': 'Ridge horn, Lagoon-Weaver culture: upswept carved timber horn with a saffron ribbon.',
    'Ornament_C': 'Ridge finial, Ring-Builder culture: pale stone ring on a short post.',
}


def write_manifest(all_rows):
    mf_path = os.path.join(HERE, 'manifest.json')
    old = {}
    if os.path.exists(mf_path):
        try:
            old = json.load(open(mf_path, encoding='utf-8'))
        except Exception:
            old = {}
    meshes = old.get('meshes', {})
    assets = old.get('assets', {})
    for asset, rows in all_rows.items():
        fn, stex, rtex, kind = A.ASSETS[asset]
        state_names = {st: r['name'] for st, r in rows.items()}
        cv = {}
        for cul in CULTURES:
            if cul == 'Valley':
                continue
            cv[cul] = {}
            for slot, ts in zip(SLOT_NAMES, (stex, rtex, 'Trim')):
                if CULTURES[cul].get(ts):
                    cv[cul][slot] = 'MI_Age2_%s_%s' % (ts, cul)
        for st, r in rows.items():
            bb = r['bounds']
            slot_mats = {s: r['mats'][i] for i, s in enumerate(SLOT_NAMES)}
            texs = []
            for mname in slot_mats.values():
                tset = mname.replace('M_Age2_', '')
                for suf in ('D', 'N', 'ORM') + (('M',) if tset == 'Trim' else ()):
                    texs.append('art/age2/textures/T_Age2_%s_%s.png' % (tset, suf))
            meshes[r['name']] = {
                'asset': asset, 'state': st, 'kind': kind,
                'fbx': 'art/age2/export/%s.fbx' % r['name'],
                'footprint_m': [round(bb[1] - bb[0], 2), round(bb[3] - bb[2], 2)],
                'bounds_m': [round(v, 2) for v in bb],
                'height_m': round(bb[5], 2),
                'triangles': r['tris'],
                'lod_screen_sizes': LOD_SCREEN[:len(r['tris'])],
                'materials': slot_mats,
                'slots_with_faces': r['slots'],
                'textures': texs,
                'states': state_names,
                'culture_variants': cv,
                'ridge_ends': r['ridges'] if kind == 'house' and st == 'Intact' else [],
                'licence': 'original',
                'script': 'art/age2/build_age2.py',
            }
        assets[asset] = {
            'description': DESC[asset], 'kind': kind, 'states': state_names,
            'structure_texture': stex, 'roof_texture': rtex, 'maps_to': SIM_MAP[asset],
        }
    mats = {}
    for ts in sorted(tx.TILE.keys() | {'Trim'}):   # sorted: set order varied run to run (verification fix)
        mats['M_Age2_%s' % ts] = {
            'textures': {k: 'art/age2/textures/T_Age2_%s_%s.png' % (ts, k) for k in (('D', 'N', 'ORM', 'M') if ts == 'Trim' else ('D', 'N', 'ORM'))},
            'uv_tile_m': tx.TILE.get(ts, tx.TRIM_U),
            'notes': ('trim sheet: horizontal bands tile in U only (%.1f m per U); M.R = night-window emissive mask' % tx.TRIM_U) if ts == 'Trim' else 'tileable in U and V',
        }
    mis = {}
    for cul, d in CULTURES.items():
        for ts, p in d.items():
            mis['MI_Age2_%s_%s' % (ts, cul)] = dict(parent='M_Age2_%s' % ts, tint_linear=list(p.get('tint', (1, 1, 1))),
                                                     saturation=p.get('saturation', 1.0), roughness_scale=p.get('roughness_scale', 1.0),
                                                     texture_override=('T_Age2_%s_*' % p['texture_override']) if 'texture_override' in p else None)
    totals = {}
    for nm, m in meshes.items():
        for i, t in enumerate(m['triangles']):
            totals['LOD%d' % i] = totals.get('LOD%d' % i, 0) + t
    mf = {
        'family': 'Age2',
        'title': 'Iron Age kit (stone houses, town walls, gatehouse, market, temple, academy)',
        'generated': datetime.date.today().isoformat(),
        'script': 'art/age2/build_age2.py',
        'blender': bpy.app.version_string,
        'licence': 'original (procedural geometry and textures, no external inputs)',
        'units': 'metres (1 Blender unit = 1 m), Z up, front -Y, pivot at footprint centre on the ground; foundations reach 0.3-1.2 m below Z=0',
        'material_slots': 'exactly 3 on every mesh, state and LOD, in the order Structure, Roof, Detail (index 0, 1, 2); a slot may carry no faces (ruins have no roof, props use only Detail): see slots_with_faces',
        'vertex_colour': 'Col (RGB): baked grime/occlusion and scorch multiplier for base colour (1 = clean)',
        'lods': {'screen_sizes': LOD_SCREEN, 'note': 'LOD3 (massing hull) on every building and wall piece, props stop at LOD2; FBX holds an LodGroup with children _LOD0.._LODn'},
        'states': list(kit.STATES),
        'cultures': {'Valley': 'default material set (age-2 concept)', 'A': 'Cliff-Terrace (warm rose-ochre)', 'B': 'Lagoon-Weaver (silver-grey, mossy)', 'C': 'Ring-Builder (pale stone, blue-grey slate roofs via texture override)'},
        'materials': mats,
        'material_instances': mis,
        'assets': assets,
        'meshes': dict(sorted(meshes.items())),
        'triangle_totals': totals,
    }
    json.dump(mf, open(mf_path, 'w', encoding='utf-8'), indent=1)
    return mf


# ---------------------------------------------------------------------- main


def main():
    o = args()
    t0 = time.time()
    if o['textures'] or not os.path.exists(os.path.join(TEX, 'T_Age2_Trim_D.png')):
        for m in tx.build_all(TEX):
            print(m)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    coll = bpy.data.collections.new('Age2')
    bpy.context.scene.collection.children.link(coll)
    names = o['assets'] or list(A.ASSETS.keys())
    states = o['states'] or list(kit.STATES)
    all_rows = {}
    for nm in names:
        print('asset', nm)
        all_rows[nm] = build_asset(nm, states, coll, o['export'])
    if o['export'] and o['assets'] is None and o['states'] is None:
        write_manifest(all_rows)
    elif o['export']:
        write_manifest(all_rows)
    if o['blend'] and o['export']:
        os.makedirs(SRC, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SRC, 'age2.blend'), compress=True)
    print('build done in %.1f s' % (time.time() - t0))
    if o['previews'] is not None:
        import age2_preview as P
        P.render_all(all_rows, o['previews'], material)


if __name__ == '__main__':
    main()
