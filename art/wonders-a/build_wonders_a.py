# DEMIURGE art - wonders-a build script (wonders 0-3: Hearth, Great Hive, Seven Who Keep Watch, Sea Gate).
#
#   blender -b --factory-startup --python art/wonders-a/build_wonders_a.py -- [options]
#     --tex all|<Set,...>   generate texture sets into art/wonders-a/textures (2048^2, --texres to change)
#     --assets hearth,hive,seven,gate   which wonders (default all)
#     --build               build every state x LOD, export FBX, write .blend and manifest part
#     --render all|close,mid,far,night,states   preview renders into shots/ (run through tools/render_slot.py)
#     --manifest            merge manifest parts into art/wonders-a/manifest.json
#
# See README.md in this folder. Everything here is original and procedural (licence: original).
import argparse
import datetime
import importlib
import json
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path[:0] = [HERE, os.path.join(ROOT, 'art', 'tools')]

import bpy  # noqa: E402

import wa_lib  # noqa: E402
import wa_mat  # noqa: E402

FAMILY = 'wonders-a'
TEX = os.path.join(HERE, 'textures')
EXP = os.path.join(HERE, 'export')
SRC = os.path.join(HERE, 'source')
PARTS = os.path.join(HERE, 'source', 'manifest_parts')
SHOTS = os.path.join(ROOT, 'shots')
MODULES = {'hearth': 'wa_hearth', 'hive': 'wa_hive', 'seven': 'wa_seven', 'gate': 'wa_seagate'}
LOD_RATIO = {1: 0.40, 2: 0.12}
SCREEN = [1.0, 0.35, 0.12, 0.04]
SCRIPT = 'art/wonders-a/build_wonders_a.py'

# culture (climate style) material-instance parameters: multiply base colour, add roughness.
# The towns code picks a palette by style (Temperate / Arid / Tropical / Cold) and a per-culture colour;
# ours: MI_Wonder_<Set>_<Style>, and the per-culture colour goes in the Detail material's CultureColor
# parameter, applied where T_Wonder_DetailA_M.G = 1 (hides, banners).
CULTURES = {
    'Temperate': dict(tint=[1.0, 1.0, 1.0], rough_add=0.0),
    'Arid': dict(tint=[1.08, 1.02, 0.9], rough_add=0.04),
    'Tropical': dict(tint=[0.96, 1.03, 0.94], rough_add=-0.03),
    'Cold': dict(tint=[0.93, 0.96, 1.02], rough_add=0.02),
}


def args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument('--assets', default='hearth,hive,seven,gate')
    p.add_argument('--tex', default='')
    p.add_argument('--texres', type=int, default=2048)
    p.add_argument('--build', action='store_true')
    p.add_argument('--render', default='')
    p.add_argument('--states', default='')
    p.add_argument('--manifest', action='store_true')
    p.add_argument('--tag', default='')
    return p.parse_args(argv)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def materials(mod, emit=2.5):
    out = []
    for set_name, slot in mod.ASSET['mats']:
        tint = mod.ASSET['emissive_tint'] if slot == 'Detail' else None
        out.append(wa_mat.make_material(set_name, TEX, emissive_tint=tint, emit_strength=emit if slot == 'Detail' else 0.0))
    return out


def build_asset(key, only_states=None):
    mod = importlib.import_module(MODULES[key])
    A = mod.ASSET
    reset()
    mats = materials(mod)
    A['_mats'] = mats
    sc = bpy.context.scene
    os.makedirs(EXP, exist_ok=True)
    os.makedirs(PARTS, exist_ok=True)
    parts = []
    t0 = time.time()
    for state in A['states']:
        if only_states and state not in only_states:
            continue
        coll = bpy.data.collections.new('%s_%s' % (A['name'], state))
        sc.collection.children.link(coll)
        name = 'SM_Wonder_%s_%s' % (A['name'], state)
        lods, tris, checks = [], [], None
        lights = []
        for q in range(4):
            if q < 3:
                mb, lt = mod.build(state, q)
            else:
                mb, lt = mod.lod3(state)
            if q == 0:
                lights = lt
            ob = wa_lib.to_object(mb, '%s_LOD%d' % (name, q), mats, coll)
            n = wa_lib.triangulate(ob)
            if q in LOD_RATIO:
                target = int(tris[0] * LOD_RATIO[q])
                if n > target * 1.08:
                    n = wa_lib.decimate_to(ob, target)
            if q == 0:
                checks = wa_lib.mesh_checks(ob)
            tris.append(n)
            lods.append(ob)
            if q > 0:
                ob.hide_render = True
        path = os.path.join(EXP, name + '.fbx')
        wa_lib.export_fbx(path, lods, name, coll)
        lp = os.path.join(EXP, name + '_lights.json')
        with open(lp, 'w') as f:
            json.dump(dict(mesh=name, units='metres, Blender axes (Z up, front -Y); UE: x100 and FBX axis conversion',
                           lights=lights), f, indent=1)
        bmin, bmax = checks['bbox_min'], checks['bbox_max']
        part = dict(
            name=name, family='Wonder', asset=A['name'], title=A['title'], wonder_index=A['age'],
            age=['Stone', 'Bronze', 'Iron', 'Sail'][A['age']], state=state,
            concept='docs/concept/wonders/%s.png' % A['concept'],
            fbx='art/wonders-a/export/%s.fbx' % name,
            lods=['%s_LOD%d' % (name, q) for q in range(4)], lod_screen_sizes=SCREEN,
            footprint_m=[round(bmax[0] - bmin[0], 1), round(bmax[1] - bmin[1], 1)],
            height_m=round(bmax[2], 1), below_ground_m=round(-bmin[2], 2),
            triangles=dict(('LOD%d' % q, t) for q, t in enumerate(tris)),
            materials=[dict(slot=slot, index=i, material='M_Wonder_%s' % s,
                            textures=['T_Wonder_%s_%s' % (s, c) for c in (('D', 'N', 'ORM', 'M') if slot == 'Detail' else ('D', 'N', 'ORM'))])
                       for i, (s, slot) in enumerate(A['mats'])],
            states=['SM_Wonder_%s_%s' % (A['name'], s) for s in A['states']],
            culture_variants=['MI_Wonder_%s_%s' % (s, c) for s, _ in A['mats'] for c in CULTURES],
            night=dict(emissive_mask='T_Wonder_DetailA_M (R)', emissive_tint=A['emissive_tint'],
                       lights_file='art/wonders-a/export/%s_lights.json' % name, light_count=len(lights),
                       kind=A['light_kind']),
            checks=checks, licence='original', script=SCRIPT,
        )
        parts.append(part)
        print('[build] %s tris %s (%.0fs)' % (name, tris, time.time() - t0))
        print('[check]', name, checks)
    os.makedirs(SRC, exist_ok=True)
    blend = os.path.join(SRC, 'wonders_a_%s.blend' % key)
    bpy.ops.wm.save_as_mainfile(filepath=blend, compress=True, relative_remap=True)
    if os.path.exists(blend + '1'):
        os.remove(blend + '1')
    if not only_states:
        with open(os.path.join(PARTS, key + '.json'), 'w') as f:
            json.dump(parts, f, indent=1)
    return parts


def render_asset(key, which, tag=''):
    import wa_render as R
    mod = importlib.import_module(MODULES[key])
    A = mod.ASSET
    blend = os.path.join(SRC, 'wonders_a_%s.blend' % key)
    bpy.ops.wm.open_mainfile(filepath=blend)
    sun = R.setup(water=getattr(mod, 'WATER', False))
    cams = mod.CAMERAS
    fig = wa_lib.MB()
    fx, fy, fr = cams['figure'][:3]
    fz = cams['figure'][3] if len(cams['figure']) > 3 else 0.0
    wa_lib.scale_figure(fig, (fx, fy, fz), fr)
    rig = bpy.data.collections['WA_Rig']
    figob = wa_lib.to_object(fig, 'WA_Figure', [bpy.data.materials['M_Wonder_DetailA']], rig)
    figob.data.materials[0] = bpy.data.materials['M_Wonder_DetailA']
    R.aim_sun(sun, cams['mid'][2])
    intact = [bpy.data.objects['SM_Wonder_%s_Intact_LOD0' % A['name']]]
    base = os.path.join(SHOTS, 'asset-%s-%s' % (FAMILY, A['concept']))
    sfx = ('-' + tag) if tag else ''
    want = set(which.split(',')) if which != 'all' else {'close', 'mid', 'far', 'night', 'states'}
    R.day(sun)
    wa_mat.set_emit(A.get('day_emit', 2.5))
    R.show_only(intact)
    for v in ('close', 'mid', 'far'):
        if v in want:
            R.camera(cams[v])
            R.render_to('%s-%s%s.png' % (base, {'close': '40m', 'mid': '300m', 'far': '1km'}[v], sfx))
    if 'night' in want:
        # the lights the manifest asks UE to spawn (fires, lamps, lantern), so the preview shows their reach
        lp = os.path.join(EXP, 'SM_Wonder_%s_Intact_lights.json' % A['name'])
        with open(lp) as f:
            Ls = [L for L in json.load(f)['lights'] if L.get('radius_m', 0) > 0]
        step = max(1, len(Ls) // 90)
        for k, L in enumerate(Ls[::step]):
            ld = bpy.data.lights.new('WA_L%d' % k, 'POINT')
            ld.energy = min(2.5e5, 6.0 * L['radius_m'] ** 2 * step)
            ld.color = wa_mat.hex_rgb(L['color'])
            ld.shadow_soft_size = 0.5
            try:
                ld.use_shadow = False
            except Exception:
                pass
            lo = bpy.data.objects.new('WA_L%d' % k, ld)
            lo.location = L['pos']
            rig.objects.link(lo)
        R.night(sun)
        wa_mat.set_emit(A.get('night_emit', 14.0))
        R.camera(cams.get('night', cams['mid']))
        R.render_to('%s-night%s.png' % (base, sfx))
        for o in list(rig.objects):
            if o.name.startswith('WA_L'):
                bpy.data.objects.remove(o)
        R.day(sun)
        wa_mat.set_emit(A.get('day_emit', 2.5))
    if 'states' in want:
        paths = []
        for st in A['states']:
            ob = bpy.data.objects['SM_Wonder_%s_%s_LOD0' % (A['name'], st)]
            R.show_only([ob])
            R.camera(cams.get('states', cams['mid']))
            p = os.path.join(SRC, '_state_%s_%s.png' % (key, st))
            R.render_to(p)
            paths.append(p)
        R.sheet(paths, '%s-states%s.png' % (base, sfx))
        for p in paths:
            os.remove(p)
    if 'lods' in want:
        paths = []
        for q in range(4):
            ob = bpy.data.objects['SM_Wonder_%s_Intact_LOD%d' % (A['name'], q)]
            ob.hide_render = False
            R.show_only([ob])
            R.camera(cams['mid'])
            p = os.path.join(SRC, '_lod_%s_%d.png' % (key, q))
            R.render_to(p)
            paths.append(p)
        R.sheet(paths, '%s-lods%s.png' % (base, sfx))
        for p in paths:
            os.remove(p)


def merge_manifest():
    meshes = []
    for key in MODULES:
        p = os.path.join(PARTS, key + '.json')
        if os.path.exists(p):
            with open(p) as f:
                meshes += json.load(f)
    sets = {}
    for m in meshes:
        for mat in m['materials']:
            sets[mat['material']] = mat
    man = dict(
        family='wonders-a', unreal_family='Wonder', generated=datetime.date.today().isoformat(),
        script=SCRIPT, licence='original (procedural, no external inputs)',
        conventions=dict(
            units='1 Blender unit = 1 m; Unreal imports at x100', axes='Z up, front faces -Y',
            pivot='centre of footprint at ground level (Z = 0); the Sea Gate pivot is at mean water level',
            fbx='one file per mesh: an FBX LodGroup node with children <name>_LOD0..3; smoothing groups, tangents, linear vertex colours',
            material_slots='0 Structure, 1 Roof, 2 Detail',
            vertex_color='Col: R = flicker phase 0..1, G = 1 on flame / glow geometry (animate only these), B = 0',
            detail_mask='T_Wonder_DetailA_M: R = night emissive mask, G = culture-colour mask (hides), B = flame mask',
            emissive='Emissive = D * M.R * lerp(1, EmissiveTint, 0.4) * EmissiveStrength * NightFactor (fires also burn by day at about 1/8 of night strength: 1/7 to 1/11 by wonder)',
            uv='Structure/Roof: tiling world-scale UVs (not 0-1), Detail: inside 0-1 atlas cells; no baked lightmaps (use UE generated lightmap UVs if needed)'),
        materials=dict((k, dict(textures=v['textures'], slot=v['slot'])) for k, v in sorted(sets.items())),
        culture_variants=dict(styles=CULTURES, naming='MI_Wonder_<Set>_<Style>',
                              detail_param='CultureColor (linear RGB) multiplies D where M.G = 1'),
        lod_screen_sizes=SCREEN, meshes=meshes)
    with open(os.path.join(HERE, 'manifest.json'), 'w') as f:
        json.dump(man, f, indent=1)
    print('[manifest]', len(meshes), 'meshes')


def main():
    a = args()
    keys = [k for k in a.assets.split(',') if k]
    if a.tex:
        import wa_tex
        which = None if a.tex == 'all' else set(a.tex.split(','))
        fold = None
        if which is None or 'SevenGranite' in which:
            import wa_seven
            fold = wa_seven.fold_field
        wa_tex.build_all(TEX, a.texres, which, fold_fn=fold)
    if a.build:
        for k in keys:
            build_asset(k, set(a.states.split(',')) if a.states else None)
    if a.render:
        for k in keys:
            render_asset(k, a.render, a.tag)
    if a.manifest:
        merge_manifest()


main()
