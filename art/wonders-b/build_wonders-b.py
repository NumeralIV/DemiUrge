# DEMIURGE art / wonders-b - build script (Blender 5.2, headless).
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/wonders-b/build_wonders-b.py -- [--assets choir,ear,ladder,sunwell,stair] [--retex] [--no-export]
#
# Writes: art/wonders-b/textures/T_Wonder_*.png, art/wonders-b/export/SM_Wonder_*.fbx (one per mesh with its
# LOD group), art/wonders-b/source/<asset>.blend, art/wonders-b/manifest.json.
# Previews are rendered separately by wb_preview.py (through tools/render_slot.py).
import os
import sys
import json
import math
import time
import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

import bpy  # noqa: E402
import wb_defs as D  # noqa: E402
import wb_geo as G  # noqa: E402
import wb_assets as A  # noqa: E402

TEX = os.path.join(HERE, 'textures')
EXP = os.path.join(HERE, 'export')
SRC = os.path.join(HERE, 'source')

# LOD settings: (q segment scale, vmul texture-row merge, lod index)
LODS = {
    'default': [(1.0, 1.0, 0), (0.55, 2.0, 1), (0.2, 6.0, 2), (0.1, 8.0, 3)],
}
LOD_SCREEN = [1.0, 0.35, 0.12, 0.04]


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'assets': list(D.ORDER), 'retex': False, 'export': True}
    i = 0
    while i < len(a):
        if a[i] == '--assets':
            o['assets'] = a[i + 1].split(',')
            i += 1
        elif a[i] == '--retex':
            o['retex'] = True
        elif a[i] == '--no-export':
            o['export'] = False
        i += 1
    return o


def hex_rgba(h, a=1.0):
    h = h.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    c = [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
    return (c[0], c[1], c[2], a)


# ------------------------------------------------------------------------------------------ textures

def tex_prefix(asset, slot):
    return 'T_Wonder_%s%s' % (D.ASSETS[asset][1], slot)


def build_textures(asset, retex):
    import wb_tex
    files = {}
    for si, slot in enumerate(D.SLOTS):
        prefix = tex_prefix(asset, slot)
        names = ['%s_%s.png' % (prefix, t) for t in ('D', 'N', 'ORM', 'M')]
        if retex or not all(os.path.exists(os.path.join(TEX, n)) for n in names):
            t0 = time.time()
            wb_tex.build_atlas(asset, slot, D.BANDS[asset][slot], TEX, prefix, seed=17 + 13 * D.ORDER.index(asset) + si)
            print('textures %s %s %.1fs' % (asset, slot, time.time() - t0))
        files[slot] = names
    return files


# ------------------------------------------------------------------------------------------ materials

def load_img(path, colorspace):
    name = os.path.basename(path)
    img = bpy.data.images.get(name)
    if img is None:
        img = bpy.data.images.load(path, check_existing=True)
    img.colorspace_settings.name = colorspace
    return img


def make_material(name, asset, slot, tex_files, params):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.use_backface_culling = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    L = nt.links
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bs = nt.nodes.new('ShaderNodeBsdfPrincipled')
    L.new(bs.outputs[0], out.inputs[0])
    uv = nt.nodes.new('ShaderNodeUVMap')
    uv.uv_map = 'UVMap'
    imgs = {}
    for tag, cs in (('D', 'sRGB'), ('N', 'Non-Color'), ('ORM', 'Non-Color'), ('M', 'Non-Color')):
        n = nt.nodes.new('ShaderNodeTexImage')
        n.name = tag
        n.image = load_img(os.path.join(TEX, '%s_%s.png' % (tex_prefix(asset, slot), tag)), cs)
        n.interpolation = 'Linear'
        L.new(uv.outputs[0], n.inputs[0])
        imgs[tag] = n
    sep_orm = nt.nodes.new('ShaderNodeSeparateColor')
    L.new(imgs['ORM'].outputs[0], sep_orm.inputs[0])
    sep_m = nt.nodes.new('ShaderNodeSeparateColor')
    L.new(imgs['M'].outputs[0], sep_m.inputs[0])
    base_t = nt.nodes.new('ShaderNodeRGB')
    base_t.name = 'BaseTint'
    base_t.outputs[0].default_value = (1, 1, 1, 1)
    acc_t = nt.nodes.new('ShaderNodeRGB')
    acc_t.name = 'AccentTint'
    acc_t.outputs[0].default_value = (1, 1, 1, 1)
    tmix = nt.nodes.new('ShaderNodeMix')
    tmix.data_type = 'RGBA'
    L.new(sep_m.outputs[2], tmix.inputs['Factor'])
    L.new(base_t.outputs[0], tmix.inputs[6])
    L.new(acc_t.outputs[0], tmix.inputs[7])
    mul = nt.nodes.new('ShaderNodeMix')
    mul.data_type = 'RGBA'
    mul.blend_type = 'MULTIPLY'
    mul.inputs['Factor'].default_value = 1.0
    L.new(imgs['D'].outputs[0], mul.inputs[6])
    L.new(tmix.outputs[2], mul.inputs[7])
    # baked cavity occlusion into the base colour (light touch)
    ao = nt.nodes.new('ShaderNodeMapRange')
    ao.inputs['To Min'].default_value = 0.45
    L.new(sep_orm.outputs[0], ao.inputs['Value'])
    mul2 = nt.nodes.new('ShaderNodeMix')
    mul2.data_type = 'RGBA'
    mul2.blend_type = 'MULTIPLY'
    mul2.inputs['Factor'].default_value = 1.0
    L.new(mul.outputs[2], mul2.inputs[6])
    L.new(ao.outputs['Result'], mul2.inputs[7])
    L.new(mul2.outputs[2], bs.inputs['Base Color'])
    rs = nt.nodes.new('ShaderNodeMath')
    rs.name = 'RoughScale'
    rs.operation = 'MULTIPLY'
    rs.use_clamp = True
    rs.inputs[1].default_value = 1.0
    L.new(sep_orm.outputs[1], rs.inputs[0])
    L.new(rs.outputs[0], bs.inputs['Roughness'])
    L.new(sep_orm.outputs[2], bs.inputs['Metallic'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nm.uv_map = 'UVMap'
    L.new(imgs['N'].outputs[0], nm.inputs['Color'])
    L.new(nm.outputs[0], bs.inputs['Normal'])
    ema = nt.nodes.new('ShaderNodeRGB')
    ema.name = 'EmA'
    ema.outputs[0].default_value = hex_rgba(params['EmissiveA'])
    emb = nt.nodes.new('ShaderNodeRGB')
    emb.name = 'EmB'
    emb.outputs[0].default_value = hex_rgba(params['EmissiveB'])
    emix = nt.nodes.new('ShaderNodeMix')
    emix.data_type = 'RGBA'
    L.new(sep_m.outputs[1], emix.inputs['Factor'])
    L.new(ema.outputs[0], emix.inputs[6])
    L.new(emb.outputs[0], emix.inputs[7])
    L.new(emix.outputs[2], bs.inputs['Emission Color'])
    es = nt.nodes.new('ShaderNodeMath')
    es.name = 'EmStrength'
    es.operation = 'MULTIPLY'
    es.inputs[1].default_value = params['EmissiveDay']
    L.new(sep_m.outputs[0], es.inputs[0])
    L.new(es.outputs[0], bs.inputs['Emission Strength'])
    for k, v in params.items():
        m['dm_' + k] = v
    return m


def make_lightshaft():
    m = bpy.data.materials.new('M_Wonder_LightShaft')
    m.use_nodes = True
    m.surface_render_method = 'BLENDED'
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    em = nt.nodes.new('ShaderNodeEmission')
    em.inputs['Color'].default_value = hex_rgba('#fff1c8')
    em.inputs['Strength'].default_value = 6.0
    em.name = 'Em'
    tr = nt.nodes.new('ShaderNodeBsdfTransparent')
    add = nt.nodes.new('ShaderNodeMixShader')
    at = nt.nodes.new('ShaderNodeAttribute')
    at.attribute_name = 'Fade'
    # fresnel-ish edge softening: facing ratio
    lw = nt.nodes.new('ShaderNodeLayerWeight')
    lw.inputs['Blend'].default_value = 0.6
    inv = nt.nodes.new('ShaderNodeMath')
    inv.operation = 'SUBTRACT'
    inv.inputs[0].default_value = 1.0
    nt.links.new(lw.outputs['Facing'], inv.inputs[1])
    mul = nt.nodes.new('ShaderNodeMath')
    mul.operation = 'MULTIPLY'
    nt.links.new(at.outputs['Fac'], mul.inputs[0])
    nt.links.new(inv.outputs[0], mul.inputs[1])
    mul2 = nt.nodes.new('ShaderNodeMath')
    mul2.operation = 'MULTIPLY'
    mul2.inputs[1].default_value = 0.85
    nt.links.new(mul.outputs[0], mul2.inputs[0])
    nt.links.new(mul2.outputs[0], add.inputs['Fac'])
    nt.links.new(tr.outputs[0], add.inputs[1])
    nt.links.new(em.outputs[0], add.inputs[2])
    nt.links.new(add.outputs[0], out.inputs[0])
    m['dm_kind'] = 'additive translucent light shaft; opacity = vertex colour Fade * view facing'
    return m


def culture_copy(m, culture):
    c = D.CULTURE_MI[culture]
    name = m.name.replace('M_Wonder_', 'MI_Wonder_') + '_' + culture
    mi = bpy.data.materials.get(name) or m.copy()
    mi.name = name
    mi.use_fake_user = True
    nt = mi.node_tree
    nt.nodes['BaseTint'].outputs[0].default_value = hex_rgba(c['BaseTint'])
    nt.nodes['AccentTint'].outputs[0].default_value = hex_rgba(c['AccentTint'])
    nt.nodes['RoughScale'].inputs[1].default_value = c['RoughnessScale']
    return mi


# ------------------------------------------------------------------------------------------ meshes

def set_fade(ob, fade):
    if not fade:
        return
    me = ob.data
    lo, hi = fade
    attr = me.color_attributes.new('Fade', 'FLOAT_COLOR', 'POINT')
    vals = []
    for v in me.vertices:
        z = v.co.z
        t = 1.0 if z <= lo else max(0.0, 1.0 - (z - lo) / (hi - lo))
        vals.extend((t, t, t, t))
    attr.data.foreach_set('color', vals)


def build_lods(name, fn, mats, coll, lods, fade=None):
    """fn() -> MB (uses G.CTX). Returns (group empty, [objects], [tris])."""
    group = bpy.data.objects.new(name, None)
    group['fbx_type'] = 'LodGroup'
    coll.objects.link(group)
    obs, tris = [], []
    for q, vm, li in lods:
        G.CTX.q, G.CTX.vmul, G.CTX.lod = q, vm, li
        mb = fn()
        if isinstance(mb, tuple):
            mb = mb[0]
        ob = G.to_object(mb, '%s_LOD%d' % (name, li), mats, collection=coll)
        set_fade(ob, fade)
        ob.parent = group
        obs.append(ob)
        tris.append(G.tri_count(ob))
    G.CTX.q, G.CTX.vmul, G.CTX.lod = 1.0, 1.0, 0
    return group, obs, tris


def export_fbx(group, obs, path):
    bpy.ops.object.select_all(action='DESELECT')
    group.select_set(True)
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = group
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'EMPTY', 'MESH'},
                             apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y',
                             use_mesh_modifiers=True, mesh_smooth_type='FACE', use_tspace=True, add_leaf_bones=False,
                             path_mode='STRIP', embed_textures=False, bake_anim=False, colors_type='SRGB')


def dims(ob):
    xs = [v.co.x for v in ob.data.vertices]
    ys = [v.co.y for v in ob.data.vertices]
    zs = [v.co.z for v in ob.data.vertices]
    return [round(max(xs) - min(xs), 2), round(max(ys) - min(ys), 2)], round(max(zs), 2), round(min(zs), 2)


def mesh_checks(ob):
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    nm = sum(1 for e in bm.edges if not e.is_manifold)
    uv = ob.data.uv_layers[0].data
    vs = [d.uv[1] for d in uv]
    bm.free()
    return {'non_manifold_edges': nm, 'uv_v_range': [round(min(vs), 4), round(max(vs), 4)]}


# ------------------------------------------------------------------------------------------ main

def main():
    o = args()
    for d in (TEX, EXP, SRC):
        os.makedirs(d, exist_ok=True)
    man_path = os.path.join(HERE, 'manifest.json')
    manifest = json.load(open(man_path)) if os.path.exists(man_path) else {}
    manifest.setdefault('meshes', {})
    rel_script = 'art/wonders-b/build_wonders-b.py'
    for asset in o['assets']:
        t_asset = time.time()
        task_id, token, title, concept = D.ASSETS[asset]
        bpy.ops.wm.read_factory_settings(use_empty=True)
        files = build_textures(asset, o['retex'])
        mats = [make_material('M_Wonder_%s%s' % (token, slot), asset, slot, files, D.MATPARAMS[asset][slot]) for slot in D.SLOTS]
        mat_names = {slot: m.name for slot, m in zip(D.SLOTS, mats)}
        mi = {c: [culture_copy(m, c).name for m in mats] for c in D.CULTURES}
        tex_list = [f for slot in D.SLOTS for f in files[slot]]
        lods = LODS.get(asset, LODS['default'])
        root = bpy.context.scene.collection
        state_names = ['SM_Wonder_%s_%s' % (token, s) for s in D.STATES]
        orn = A.ORNAMENTS[asset]
        orn_names = ['SM_Wonder_%s_%s' % (token, orn[i]) for i in range(3)]
        for (s, name) in zip(D.STATES, state_names):
            coll = bpy.data.collections.new(name)
            root.children.link(coll)
            t0 = time.time()
            holder = {}

            def fn(s=s):
                mb, so = A.BUILDERS[asset](s)
                if G.CTX.lod == 0:
                    holder['so'] = so
                return mb
            group, obs, tris = build_lods(name, fn, mats, coll, lods)
            fp, h, zmin = dims(obs[0])
            chk = mesh_checks(obs[0])
            print('%-34s tris %s  footprint %s h %.1f zmin %.2f  %.1fs  %s' % (name, tris, fp, h, zmin, time.time() - t0, chk))
            if o['export']:
                export_fbx(group, obs, os.path.join(EXP, name + '.fbx'))
            so = holder['so']
            entry = {
                'asset_id': task_id, 'title': title, 'kind': 'wonder', 'state': s, 'family': D.FAMILY,
                'footprint_m': fp, 'height_m': h, 'base_z_m': zmin, 'pivot': 'footprint centre at ground level (Z = 0)',
                'triangles': {'LOD%d' % i: t for i, t in enumerate(tris)}, 'lod_screen_sizes': LOD_SCREEN,
                'materials': mat_names, 'textures': tex_list,
                'states': dict(zip(D.STATES, state_names)),
                'culture_variants': {c: {'material_instances': mi[c], 'ornament_mesh': orn_names[i], 'label': D.CULTURE_MI[c]['label']}
                                     for i, c in enumerate(D.CULTURES)},
                'ornament_sockets': {'slot': orn[4], 'transforms': so['ornament'],
                                     'note': 'instance the culture ornament mesh at every transform (rotation about Z in degrees)'},
                'fx_sockets': so['fx'], 'lights': so['lights'], 'tree_sockets': so['trees'],
                'concept': concept, 'licence': D.LICENCE, 'script': rel_script, 'export': 'art/wonders-b/export/%s.fbx' % name,
                'checks': chk,
            }
            if asset == 'sunwell':
                entry['terrain_cut'] = {'shape': 'disc', 'radius_m': 101.0, 'floor_z_m': -27.0,
                                        'note': 'the Sunwell is a pit: lower or hide terrain inside radius 101 m down to -27 m'}
            if asset == 'stair':
                entry['terrain_note'] = 'brings its own terraced mountain (footprint 304 m, summit about 118 m); blend its skirt (r 150-152 m, z 0 to -2) into the terrain; best on a hill or slope'
            if asset == 'ear':
                entry['terrain_note'] = 'brings its own grassy berm (r 123-178 m); the dish floor sits at +1 m, so flatten the ground inside r 178 m'
            manifest['meshes'][name] = entry
        # ornaments
        coll = bpy.data.collections.new('Ornaments')
        root.children.link(coll)
        for i, c in enumerate(D.CULTURES):
            name = orn_names[i]
            group, obs, tris = build_lods(name, lambda c=c: orn[3](c), mats, coll, lods[:3])
            fp, h, zmin = dims(obs[0])
            print('%-34s tris %s  footprint %s h %.1f' % (name, tris, fp, h))
            if o['export']:
                export_fbx(group, obs, os.path.join(EXP, name + '.fbx'))
            manifest['meshes'][name] = {
                'asset_id': task_id, 'title': title + ' - culture ornament ' + c, 'kind': 'ornament', 'culture': c,
                'socket_slot': orn[4], 'footprint_m': fp, 'height_m': h, 'pivot': 'socket origin (sits on the socket point)',
                'triangles': {'LOD%d' % k: t for k, t in enumerate(tris)}, 'lod_screen_sizes': LOD_SCREEN[:3],
                'materials': mat_names, 'material_instances': mi[c], 'textures': tex_list,
                'licence': D.LICENCE, 'script': rel_script, 'export': 'art/wonders-b/export/%s.fbx' % name,
            }
        # extras
        extras = []
        if asset == 'ladder':
            for s in D.STATES:
                extras.append(('SM_Wonder_Ladder_Tether_%s' % s, lambda s=s: A.ladder_tether(s), mats, s,
                               'the tether: emissive core (Detail band 3, tether_core); vertex colour Fade (1 at 1.8 km to 0 at 3 km) for the fade-out; bounds are 3 km tall'))
            extras.append(('SM_Wonder_Ladder_Climber', A.ladder_climber, mats, None,
                           'climber capsule, 10 m; slides on the tether (bore radius 1.3 m); animate along Z'))
        if asset == 'sunwell':
            extras.append(('SM_Wonder_Sunwell_Beam', A.sunwell_beam, [make_lightshaft()], None,
                           'sky beam from the mirror (z 57 m) to 1.5 km; additive translucent; vertex colour Fade; pulse with devotion'))
        if extras:
            coll = bpy.data.collections.new('Extras')
            root.children.link(coll)
        for name, fn, mm, s, note in extras:
            fade = None

            def wrapfn(fn=fn, holder={}):
                r = fn()
                if isinstance(r, tuple):
                    holder['fade'] = r[1]
                    return r[0]
                return r
            # learn the fade range first
            r = fn()
            fade = r[1] if isinstance(r, tuple) else None
            group, obs, tris = build_lods(name, wrapfn, mm, coll, lods[:3], fade=fade)
            fp, h, zmin = dims(obs[0])
            print('%-34s tris %s  footprint %s h %.1f' % (name, tris, fp, h))
            if o['export']:
                export_fbx(group, obs, os.path.join(EXP, name + '.fbx'))
            manifest['meshes'][name] = {
                'asset_id': task_id, 'title': title + ' - ' + name.split('_', 3)[-1], 'kind': 'extra', 'state': s,
                'footprint_m': fp, 'height_m': h, 'base_z_m': zmin, 'triangles': {'LOD%d' % k: t for k, t in enumerate(tris)},
                'lod_screen_sizes': LOD_SCREEN[:3], 'materials': {('Structure' if len(mm) == 1 else s_): m.name for s_, m in zip(D.SLOTS, mm)},
                'textures': tex_list if len(mm) == 3 else [], 'licence': D.LICENCE, 'script': rel_script,
                'export': 'art/wonders-b/export/%s.fbx' % name, 'note': note,
            }
        # material table
        manifest.setdefault('materials', {})
        for slot, m in zip(D.SLOTS, mats):
            manifest['materials'][m.name] = dict(asset=task_id, slot=slot, textures={t: '%s_%s.png' % (tex_prefix(asset, slot), t) for t in ('D', 'N', 'ORM', 'M')},
                                                 bands=[{'band': i, 'name': n, 'size_m': [4 * v, v]} for i, (n, v) in enumerate(D.BANDS[asset][slot])],
                                                 params=D.MATPARAMS[asset][slot],
                                                 instances={c: manifest_mi(m.name, c) for c in D.CULTURES})
        if asset == 'sunwell':
            manifest['materials']['M_Wonder_LightShaft'] = dict(asset=task_id, slot='Structure', textures={},
                                                               params={'Color': '#fff1c8', 'EmissiveDay': 6.0, 'EmissiveNight': 10.0},
                                                               note='additive translucent; opacity = vertex colour Fade x view-facing falloff')
        bpy.context.preferences.filepaths.save_version = 0     # no stale .blend1 copies next to the sources
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SRC, '%s.blend' % asset), compress=True)
        print('asset %s done in %.1fs' % (asset, time.time() - t_asset))
    manifest['family'] = 'wonders-b'
    manifest['generated'] = datetime.date.today().isoformat()
    manifest['script'] = rel_script
    manifest['regenerate'] = '"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/wonders-b/build_wonders-b.py -- [--assets choir,ear,ladder,sunwell,stair] [--retex]'
    manifest['conventions'] = {
        'units': '1 Blender unit = 1 m; Z up; front faces -Y; FBX exported with the standard axis settings',
        'material_slots': list(D.SLOTS),
        'atlas': 'each slot texture is a 2048x2048 atlas of four 2048x512 bands (band k = UV v in [k/4,(k+1)/4]); u tiles (UVs run past 0..1 in u by design), v stays inside one band; band k covers U x V metres as listed per material. Use Wrap addressing.',
        'M_channels': {'R': 'night emissive mask', 'G': 'emissive colour select (0 EmissiveA, 1 EmissiveB)', 'B': 'culture accent mask (AccentTint applies here)'},
        'ORM_channels': {'R': 'ambient occlusion (cavity)', 'G': 'roughness', 'B': 'metallic'},
        'normal': 'tangent space, OpenGL (+Y up): flip green on import into Unreal',
        'emissive': 'Emissive = M.R * lerp(EmissiveA, EmissiveB, M.G) * (EmissiveDay or EmissiveNight by sun)',
        'culture': 'BaseColor = D * lerp(BaseTint, AccentTint, M.B); Roughness = ORM.G * RoughnessScale',
        'lod_screen_sizes': LOD_SCREEN,
    }
    manifest['culture_instances'] = D.CULTURE_MI
    json.dump(manifest, open(man_path, 'w'), indent=1)
    print('manifest written', man_path)


def manifest_mi(mname, c):
    cm = D.CULTURE_MI[c]
    return {'name': mname.replace('M_Wonder_', 'MI_Wonder_') + '_' + c, 'BaseTint': cm['BaseTint'], 'AccentTint': cm['AccentTint'],
            'RoughnessScale': cm['RoughnessScale']}


main()
