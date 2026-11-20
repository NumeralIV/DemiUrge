# DEMIURGE art/culture - preview renders (EEVEE, 1280x720) of the culture overlays on the real host kits.
#
# Run through the render slot tool (docs/ASSETS.md section 3), after build_culture.py has written culture.blend:
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       art/culture/culture.blend --python art/culture/preview_culture.py -- --set a [--views 40m,300m,1km]
#
# Sets: a, b, c   (a town vignette of that culture: Age2 hosts + Age3 hosts wearing the overlays)
#       states    (one house per culture in Intact / Construction / Damaged / Ruin, host states paired)
#       kit       (every structure, landmark, ornament and prop of all three cultures)
# Output: shots/asset-culture-<set>-<view>.png
# Rig per ASSETS.md: warm sun at 20 degrees, cool sky fill, 40 m three-quarter / 300 m / 1 km oblique views.
# The ground, water, palms and moor are preview dressing only (not exported).
import os
import sys
import json
import math
import random

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path[:0] = [HERE, os.path.join(ROOT, 'art', 'tools')]

import bpy
import bmesh
from mathutils import Vector, Matrix, Euler

V = Vector


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'set': 'a', 'views': None, 'night': False, 'tag': ''}
    for i, x in enumerate(a):
        if x == '--set':
            o['set'] = a[i + 1]
        if x == '--views':
            o['views'] = a[i + 1].split(',')
        if x == '--tag':
            o['tag'] = a[i + 1]
    return o


# ------------------------------------------------------------------------------------------- scene

def reset_scene():
    sc = bpy.data.scenes.new('preview')
    bpy.context.window.scene = sc if bpy.context.window else None
    return sc


def setup_render(sc):
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    ee = sc.eevee
    for k, v in (('taa_render_samples', 48), ('use_raytracing', True), ('volumetric_end', 4000.0),
                 ('volumetric_start', 1.0), ('volumetric_tile_size', '8'), ('use_shadows', True),
                 ('shadow_ray_count', 2), ('shadow_step_count', 8), ('fast_gi_method', 'GLOBAL_ILLUMINATION'),
                 ('use_fast_gi', True), ('ray_tracing_method', 'SCREEN')):
        try:
            setattr(ee, k, v)
        except Exception:
            pass
    try:
        sc.view_settings.view_transform = 'AgX'
        sc.view_settings.look = 'AgX - Medium High Contrast'
    except Exception:
        pass
    sc.view_settings.exposure = 0.0


def setup_light(sc, sun_az=200.0, sun_el=20.0, haze=0.00022, sky=(0.42, 0.56, 0.82), sky_strength=0.55):
    w = bpy.data.worlds.new('pv_world')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    # vertical gradient: warm horizon, cool zenith
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Generated'], sep.inputs[0])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.5
    ramp.color_ramp.elements[0].color = (0.95, 0.78, 0.6, 1)
    ramp.color_ramp.elements[1].position = 0.75
    ramp.color_ramp.elements[1].color = (*sky, 1)
    nt.links.new(sep.outputs['Z'], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = sky_strength
    nt.links.new(bg.outputs[0], out.inputs['Surface'])
    if haze > 0:
        # a finite haze box (a world volume is infinite in EEVEE and would swallow the sky)
        hm = bpy.data.materials.new('pv_haze')
        hm.use_nodes = True
        hn = hm.node_tree
        hn.nodes.clear()
        ho = hn.nodes.new('ShaderNodeOutputMaterial')
        vol = hn.nodes.new('ShaderNodeVolumePrincipled')
        vol.inputs['Color'].default_value = (0.8, 0.74, 0.66, 1)
        vol.inputs['Density'].default_value = haze
        vol.inputs['Anisotropy'].default_value = 0.4
        hn.links.new(vol.outputs[0], ho.inputs['Volume'])
        me = bpy.data.meshes.new('haze')
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        bm.to_mesh(me)
        bm.free()
        me.materials.append(hm)
        hb = bpy.data.objects.new('haze', me)
        sc.collection.objects.link(hb)
        hb.scale = (6000, 6000, 1400)
        hb.location = (0, 0, 650)
    ld = bpy.data.lights.new('sun', 'SUN')
    ld.energy = 6.0
    ld.color = (1.0, 0.8, 0.6)
    ld.angle = math.radians(1.2)
    try:
        ld.shadow_maximum_resolution = 0.002
    except Exception:
        pass
    ob = bpy.data.objects.new('sun', ld)
    sc.collection.objects.link(ob)
    # sun direction: elevation sun_el, azimuth sun_az (degrees, 0 = +Y, 90 = +X)
    a, e = math.radians(sun_az), math.radians(sun_el)
    d = V((math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), math.sin(e)))   # toward the sun
    ob.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    return ob


def camera(sc, target, dist, elev, azim, lens=50.0, name='cam'):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    cd.clip_start = 0.5
    cd.clip_end = 20000
    ob = bpy.data.objects.new(name, cd)
    sc.collection.objects.link(ob)
    a, e = math.radians(azim), math.radians(elev)
    pos = V(target) + V((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e))) * dist
    ob.location = pos
    ob.rotation_euler = (V(target) - pos).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = ob
    return ob


# ------------------------------------------------------------------------------------------- materials

def proc_mat(name, col, rough=0.9, noise_scale=0.05, var=0.25, col2=None, spec=0.5, metallic=0.0):
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    bs.inputs['Roughness'].default_value = rough
    bs.inputs['Metallic'].default_value = metallic
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nz = nt.nodes.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = noise_scale
    nz.inputs['Detail'].default_value = 8.0
    nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    c2 = col2 or tuple(c * (1 - var) for c in col)
    ramp.color_ramp.elements[0].color = (*c2, 1)
    ramp.color_ramp.elements[1].color = (*col, 1)
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[1].position = 0.65
    nt.links.new(nz.outputs['Fac'], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], bs.inputs['Base Color'])
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.25
    nz2 = nt.nodes.new('ShaderNodeTexNoise')
    nz2.inputs['Scale'].default_value = noise_scale * 30
    nt.links.new(tc.outputs['Object'], nz2.inputs['Vector'])
    nt.links.new(nz2.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs[0], bs.inputs['Normal'])
    return m


def water_mat(name='pv_water', col=(0.06, 0.42, 0.40), deep=(0.02, 0.16, 0.2)):
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    bs.inputs['Roughness'].default_value = 0.06
    bs.inputs['IOR'].default_value = 1.33
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nz = nt.nodes.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = 0.01
    nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = (*deep, 1)
    ramp.color_ramp.elements[1].color = (*col, 1)
    nt.links.new(nz.outputs['Fac'], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], bs.inputs['Base Color'])
    wv = nt.nodes.new('ShaderNodeTexNoise')
    wv.inputs['Scale'].default_value = 1.2
    wv.inputs['Detail'].default_value = 6
    nt.links.new(tc.outputs['Object'], wv.inputs['Vector'])
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.12
    nt.links.new(wv.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs[0], bs.inputs['Normal'])
    return m


def tex_mat(name, tex, tint=(1, 1, 1)):
    """Host material from a host family's own textures (paths relative to ROOT), tinted for the culture."""
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    uv = nt.nodes.new('ShaderNodeUVMap')

    def img(p, color):
        n = nt.nodes.new('ShaderNodeTexImage')
        im = bpy.data.images.load(os.path.join(ROOT, p), check_existing=True)
        im.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
        n.image = im
        nt.links.new(uv.outputs[0], n.inputs[0])
        return n
    if tex.get('D') and os.path.exists(os.path.join(ROOT, tex['D'])):
        d = img(tex['D'], True)
        mix = nt.nodes.new('ShaderNodeMix')
        mix.data_type = 'RGBA'
        mix.blend_type = 'MULTIPLY'
        mix.inputs['Factor'].default_value = 1.0
        nt.links.new(d.outputs[0], mix.inputs['A'])
        mix.inputs['B'].default_value = (*tint, 1)
        nt.links.new(mix.outputs['Result'], bs.inputs['Base Color'])
    if tex.get('ORM') and os.path.exists(os.path.join(ROOT, tex['ORM'])):
        o = img(tex['ORM'], False)
        sp = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(o.outputs[0], sp.inputs[0])
        nt.links.new(sp.outputs[1], bs.inputs['Roughness'])
    if tex.get('N') and os.path.exists(os.path.join(ROOT, tex['N'])):
        n = img(tex['N'], False)
        nm = nt.nodes.new('ShaderNodeNormalMap')
        nt.links.new(n.outputs[0], nm.inputs['Color'])
        nt.links.new(nm.outputs[0], bs.inputs['Normal'])
    return m


# ------------------------------------------------------------------------------------------- hosts

_HOST_CACHE = {}
_MAN = {}


def host_manifest(fam):
    if fam not in _MAN:
        p = os.path.join(ROOT, 'art', fam.lower(), 'manifest.json')
        _MAN[fam] = json.load(open(p, encoding='utf-8')) if os.path.exists(p) else {}
    return _MAN[fam]


def host_tex(fam, mat_name):
    m = host_manifest(fam)
    mats = m.get('materials', {})
    e = mats.get(mat_name)
    if isinstance(e, dict) and 'textures' in e:
        return e['textures']
    # Age3 style: names like M_Age3_Structure with textures in art/age3/textures/T_Age3_Structure_*
    slot = mat_name.split('_')[-1]
    d = 'art/%s/textures' % fam.lower()
    return {k: '%s/T_%s_%s_%s.png' % (d, fam, slot, k) for k in ('D', 'N', 'ORM')}


def host_tint(fam, mat_name, culture):
    m = host_manifest(fam)
    if fam == 'Age2':
        base = mat_name.replace('M_Age2_', '')
        mi = m.get('material_instances', {}).get('MI_Age2_%s_%s' % (base, culture))
        if mi and mi.get('tint_linear'):
            return tuple(mi['tint_linear'])
    else:
        c = m.get('cultures', {}).get('Culture%s' % culture, {})
        if 'Structure' in mat_name and c.get('PlasterTint'):
            p, s = c['PlasterTint'], c.get('StoneTint', c['PlasterTint'])
            return tuple((a + b) / 2 for a, b in zip(p, s))
        if 'Detail' in mat_name and c.get('PaintTint'):
            return (1, 1, 1)
    return (1, 1, 1)


def load_host(mesh_name, fbx_rel, culture, coll):
    """Import a host FBX once (LOD0 only); return a mesh datablock with culture-tinted host materials."""
    key = (mesh_name, culture)
    if key in _HOST_CACHE:
        return _HOST_CACHE[key]
    p = os.path.join(ROOT, fbx_rel)
    if not os.path.exists(p):
        return None
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=p)
    new = [o for o in bpy.data.objects if o not in before]
    lod0 = [o for o in new if o.type == 'MESH' and o.name.split('.')[0].endswith('LOD0')] or [o for o in new if o.type == 'MESH']
    me = None
    if lod0:
        ob = lod0[0]
        me = ob.data.copy()
        me.transform(ob.matrix_world)
        fam = 'Age2' if 'Age2' in mesh_name else 'Age3'
        import cul_mats
        for i, mt in enumerate(me.materials):
            base = mt.name.split('.')[0] if mt else 'M_%s_Structure' % fam
            if base in cul_mats.HOST_WALL_MATS.get(fam, ()):     # our culture wall override (manifest wall_overrides)
                tset = cul_mats.HOST_OVERRIDE[culture][0]
                tex = {k: os.path.relpath(p_, ROOT) for k, p_ in cul_mats.tex_files(tset, ROOT).items()}
                me.materials[i] = tex_mat('pvh_ovr_%s_%s' % (base, culture), tex, (1, 1, 1))
            else:
                me.materials[i] = tex_mat('pvh_%s_%s' % (base, culture), host_tex(fam, base), host_tint(fam, base, culture))
        me.name = 'pvhost_%s_%s' % (mesh_name, culture)
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    _HOST_CACHE[key] = me
    return me


# ------------------------------------------------------------------------------------------- placing

def place(coll, mesh, loc=(0, 0, 0), rz=0.0, name=None, scale=1.0):
    if mesh is None:
        return None
    ob = bpy.data.objects.new(name or mesh.name, mesh)
    coll.objects.link(ob)
    ob.location = V(loc)
    ob.rotation_euler = (0, 0, rz)
    ob.scale = (scale, scale, scale)
    return ob


def cmesh(name):
    """Mesh data of a culture LOD0 object in the opened culture.blend."""
    o = bpy.data.objects.get(name + '_LOD0') or bpy.data.objects.get(name)
    return o.data if o else None


def hosts_json():
    p = os.path.join(HERE, 'hosts.json')
    return json.load(open(p, encoding='utf-8'))['hosts'] if os.path.exists(p) else []


def house(coll, h, culture, loc, rz=0.0, state='Intact', stilts=False, host_state=None):
    """Host house + its culture roof (+ pile platform for B) at loc."""
    age = h['age']
    tag = h['asset'].replace('_', '')
    hs = host_state or state
    mesh_name = h['mesh'].replace('_Intact', '_' + hs) if h.get('mesh') else None
    fbx = h['fbx'].replace('_Intact', '_' + hs) if h.get('fbx') else None
    z = 1.8 if stilts else 0.0
    M = Matrix.Translation(V(loc) + V((0, 0, z))) @ Matrix.Rotation(rz, 4, 'Z')
    if mesh_name and fbx:
        hm = load_host(mesh_name, fbx, culture, coll)
        o = place(coll, hm)
        if o:
            o.matrix_world = M
    r = cmesh('SM_Culture_%s%dRoof%s_%s' % (culture, age, tag, state))
    o = place(coll, r)
    if o:
        o.matrix_world = M
    if stilts:
        s = cmesh('SM_Culture_B%dStilts%s_%s' % (age, tag, state))
        o = place(coll, s)
        if o:
            o.matrix_world = M


def plane(coll, name, size, z, mat, loc=(0, 0)):
    me = bpy.data.meshes.new(name)
    s = size / 2
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    ob.location = (loc[0], loc[1], z)
    return ob


def blob_mesh(name, r, mat, squash=1.0, sub=2, seed=0, rough=0.3):
    from mathutils import noise
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=1.0)
    off = V((seed * 3.1, seed * 1.7, 0))
    for v in bm.verts:
        d = v.co.normalized()
        k = 1 + rough * noise.noise(d * 1.8 + off)
        v.co = V((d.x * r * k, d.y * r * k, d.z * r * squash * k))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    for p in me.polygons:
        p.use_smooth = True
    return me


def palm_mesh(name, h=7.0, seed=0):
    """Preview dressing: a fan palm (trunk + drooping fronds)."""
    rng = random.Random(seed)
    trunk = proc_mat('pv_trunk', (0.22, 0.16, 0.11), 0.9, 0.5)
    leaf = proc_mat('pv_palm', (0.10, 0.22, 0.05), 0.7, 0.3, 0.35)
    bm = bmesh.new()
    lean = V((rng.uniform(-0.6, 0.6), rng.uniform(-0.6, 0.6), 0))
    pts = [V((0, 0, 0)) + lean * (k / 6) ** 2 + V((0, 0, h * k / 6)) for k in range(7)]
    for k in range(6):
        a, b = pts[k], pts[k + 1]
        r0, r1 = 0.22 - 0.02 * k, 0.2 - 0.02 * k
        d = (b - a)
        M = Matrix.Translation((a + b) / 2) @ d.to_track_quat('Z', 'Y').to_matrix().to_4x4()
        bmesh.ops.create_cone(bm, cap_ends=True, segments=6, radius1=r0, radius2=r1, depth=d.length, matrix=M)
    nT = len(bm.faces)
    top = pts[-1]
    for f in range(11):
        az = 2 * math.pi * f / 11 + rng.uniform(-0.2, 0.2)
        L = rng.uniform(2.6, 3.4)
        dirv = V((math.cos(az), math.sin(az), 0))
        prev = top
        for s in range(4):
            t = (s + 1) / 4
            p = top + dirv * L * t + V((0, 0, 0.9 * t - 1.6 * t * t))
            w = 0.55 * math.sin(math.pi * min(1, t + 0.15))
            side = dirv.cross(V((0, 0, 1))) * w
            v = [bm.verts.new(prev - side * 0.6), bm.verts.new(prev + side * 0.6), bm.verts.new(p + side), bm.verts.new(p - side)]
            bm.faces.new(v)
            prev = p
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(trunk)
    me.materials.append(leaf)
    for i, p in enumerate(me.polygons):
        p.material_index = 0 if i < nT else 1
        p.use_smooth = True
    return me


def figure_mesh():
    """A 1.8 m scale figure (ASSETS.md check), saffron-robed so it is easy to find."""
    me = bpy.data.meshes.get('pv_figure')
    if me:
        return me
    m = proc_mat('pv_figure_mat', (0.55, 0.25, 0.06), 0.8, 1.0, 0.1)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=0.26, radius2=0.17,
                          depth=1.5, matrix=Matrix.Translation((0, 0, 0.75)))
    bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=0.13, matrix=Matrix.Translation((0, 0, 1.67)))
    me = bpy.data.meshes.new('pv_figure')
    bm.to_mesh(me)
    bm.free()
    me.materials.append(m)
    return me


def figures(coll, pts):
    fm = figure_mesh()
    for p in pts:
        place(coll, fm, p, random.uniform(0, 6))


# ------------------------------------------------------------------------------------------- vignettes

def by_id(hosts, ident):
    for h in hosts:
        if h['id'] == ident:
            return h
    return None


def vignette_a(sc, coll, hosts):
    """Cliff-Terrace: houses stacked up a red canyon slope on terraces, the temple niche at the top."""
    rng = random.Random(1)
    rock = proc_mat('pv_rock_a', (0.26, 0.11, 0.06), 0.92, 0.15, 0.45, (0.14, 0.06, 0.035))
    soil = proc_mat('pv_soil_a', (0.20, 0.10, 0.06), 0.95, 0.2, 0.35)
    field = proc_mat('pv_field_a', (0.12, 0.26, 0.05), 0.85, 0.08, 0.4)
    water = water_mat('pv_river', (0.12, 0.32, 0.30), (0.04, 0.12, 0.12))
    plane(coll, 'ground', 3000, -0.05, soil)
    # river and terraced fields in front
    rv = plane(coll, 'river', 1, 0.02, water, (0, -70))
    rv.scale = (900, 26, 1)
    for k in range(10):
        f = plane(coll, 'field%d' % k, 1, 0.03, field, (-260 + 52 * k + rng.uniform(-5, 5), -40))
        f.scale = (46, 18, 1)
    terr = cmesh('SM_Culture_ATerraceStair_Intact')
    cist = cmesh('SM_Culture_ACistern_Intact')
    niche = cmesh('SM_Culture_ATempleNiche_Intact')
    a2 = [h for h in hosts if h['age'] == 2 and len(h.get('roofs') or [1]) == 1]
    a3 = [h for h in hosts if h['age'] == 3 and len(h.get('roofs') or [1]) == 1]
    palms = [palm_mesh('palm%d' % i, rng.uniform(6, 9), i) for i in range(4)]
    levels, cols = 6, 7
    for lv in range(levels):
        zt = 6.0 * lv + 3.0
        y = 8.0 * lv
        for c in range(cols):
            x = (c - cols // 2) * 12.0 + (lv % 2) * 6.0
            # stack of terrace blocks reaching this level (two per row: the rows climb 6 m every 8 m)
            for k in range(2 * lv + 1):
                place(coll, terr, (x, y, 3.0 * k))
            if (c + lv) % 4 == 3:
                place(coll, cist, (x + 2.5, y + 1.5, zt))
                place(coll, palms[(c + lv) % 4], (x - 3.0, y + 1.0, zt))
                continue
            a2f = [hh for hh in a2 if hh['roof']['max'][1] - hh['roof']['min'][1] <= 9.0 and
                   hh['roof']['max'][0] - hh['roof']['min'][0] <= 11.6] or a2
            pool = a3 if (lv == levels - 1 and c >= cols - 3 and a3) else a2f
            h = pool[(c * 3 + lv) % len(pool)]
            house(coll, h, 'A', (x, y + 0.3, zt))
            if rng.random() < 0.6:
                place(coll, palms[(c * 7 + lv) % 4], (x + rng.choice([-5.0, 5.0]), y - 2.5, zt), rng.uniform(0, 6))
    # cliff behind with the temple niche
    cliff = blob_mesh('cliff', 1.0, rock, squash=1.0, sub=4, seed=3, rough=0.25)
    o = place(coll, cliff, (0, 8.0 * levels + 26, 0))
    o.scale = (170, 30, 52)
    for sx in (-1, 1):
        o = place(coll, cliff, (sx * 140, 30, 0))
        o.scale = (60, 110, 40)
    place(coll, niche, (0, 8.0 * levels + 3.5, 6.0 * levels - 3.0))
    for k in range(2 * levels):
        place(coll, terr, (-6, 8.0 * levels - 1, 3.0 * k - 3.0))
        place(coll, terr, (6, 8.0 * levels - 1, 3.0 * k - 3.0))
    for i in range(40):
        place(coll, palms[i % 4], (rng.uniform(-200, 200), rng.uniform(-60, -25), 0), rng.uniform(0, 6))
    figures(coll, [(-9.5, 6.0, 9.0), (-8.6, 6.4, 9.0), (-1.0, 13.2, 15.0), (2.9, -5.2, 0.0)])
    return dict(t40=V((-6, 10, 16)), t300=V((0, 22, 14)), az40=-28.0)


def vignette_b(sc, coll, hosts):
    """Lagoon-Weaver: houses on piles round circular water courts, curved boardwalks, rafts, temple."""
    rng = random.Random(2)
    water = water_mat('pv_lagoon', (0.10, 0.55, 0.50), (0.03, 0.24, 0.30))
    sand = proc_mat('pv_sand', (0.62, 0.52, 0.36), 0.95, 0.02, 0.15)
    mangrove = proc_mat('pv_mangrove', (0.07, 0.16, 0.05), 0.8, 0.2, 0.4)
    plane(coll, 'lagoon', 4000, 0.0, water)
    sb = plane(coll, 'sandbar', 1, 0.15, sand, (0, 230))
    sb.scale = (900, 40, 1)
    tree = blob_mesh('mangrove', 1.0, mangrove, squash=0.55, sub=2, seed=5, rough=0.35)
    for i in range(70):
        place(coll, tree, (rng.uniform(150, 420), rng.uniform(40, 260), 1.0), rng.uniform(0, 6), scale=rng.uniform(5, 9))
    temple = cmesh('SM_Culture_BTempleSpiral_Intact')
    curve = cmesh('SM_Culture_BWalkwayCurve_Intact')
    straight = cmesh('SM_Culture_BWalkwayStraight_Intact')
    bridge = cmesh('SM_Culture_BBridge_Intact')
    raft = cmesh('SM_Culture_BGardenRaft_Intact')
    brk = cmesh('SM_Culture_BBreakwater_Intact')
    place(coll, temple, (0, 0, 0))
    b2 = [h for h in hosts if h['age'] == 2 and len(h.get('roofs') or [1]) == 1]
    b3 = [h for h in hosts if h['age'] == 3 and len(h.get('roofs') or [1]) == 1 and h['roof']['eave_z'] < 11]
    courts = [(-48, -18), (46, -22), (-40, 42), (44, 40), (0, -62)]
    # curve piece: centre-line radius 10 m spanning 45 degrees; its local arc centre is at (0, 10 - cy) after
    # centring. Compute the offset so 8 pieces form a ring.
    R = 10.0
    ang = math.radians(45)
    pts = [V((R * math.sin(ang * k / 10), R - R * math.cos(ang * k / 10), 0)) for k in range(11)]
    cx = (min(p.x for p in pts) + max(p.x for p in pts)) / 2
    cy = (min(p.y for p in pts) + max(p.y for p in pts)) / 2
    arc_c = V((-cx, R - cy, 0))           # arc centre in the piece's local space
    for ci, (x, y) in enumerate(courts):
        c = V((x, y, 0))
        for k in range(8):
            rz = ang * k
            # piece local arc centre must land on the court centre
            off = Matrix.Rotation(rz, 3, 'Z') @ arc_c
            place(coll, curve, c - off, rz)
        n = 6
        for k in range(n):
            a = 2 * math.pi * (k + 0.5 * (ci % 2)) / n
            pool = b3 if (ci == 3 and b3 and k % 3 == 0) else b2
            h = pool[(k + ci) % len(pool)]
            rr = 10.0 + 1.6 + 5.2
            p = c + V((math.cos(a) * rr, math.sin(a) * rr, 0))
            # front (-Y) of the house faces the court centre
            rz = math.atan2(math.sin(a), math.cos(a)) - math.pi / 2
            house(coll, h, 'B', p, rz, stilts=True)
        for k in range(3):
            a = rng.uniform(0, 6.28)
            place(coll, raft, c + V((math.cos(a) * rng.uniform(2, 6), math.sin(a) * rng.uniform(2, 6), 0)), rng.uniform(0, 6))
        # link to the temple
        d = (V((0, 0, 0)) - c)
        L = d.length - 10.0 - 10.5
        rz = math.atan2(d.y, d.x)
        nseg = max(1, int(L / 8.0))
        for s in range(nseg):
            p = c + d.normalized() * (10.0 + 4.0 + 8.0 * s)
            if s == nseg // 2:
                place(coll, bridge, p + d.normalized() * 1.0, rz)
            else:
                place(coll, straight, p, rz)
    for i in range(26):
        place(coll, raft, (rng.uniform(-110, 110), rng.uniform(-110, 90), 0), rng.uniform(0, 6))
    # breakwater along an arc in front (radius 30 m pieces on a 130 m circle: slight kinks are fine)
    Rb = 130.0
    for k in range(-7, 8):
        a = k * 0.092
        p = V((math.sin(a) * Rb, -math.cos(a) * Rb + 20, 0))
        place(coll, brk, p, a)
    figures(coll, [(-48 + 9.6, -18.0, 1.3), (-48 + 10.2, -18.6, 1.3), (-50.0, -8.0, 1.3)])
    return dict(t40=V((-48, -6, 5)), t300=V((0, -6, 4)), az40=-30.0)


def vignette_c(sc, coll, hosts):
    """Ring-Builder: drum houses on concentric rings, dry-stone ring walls, spire, wedge fields, stones."""
    rng = random.Random(3)
    moor = proc_mat('pv_moor', (0.13, 0.14, 0.06), 0.95, 0.12, 0.35, (0.12, 0.08, 0.09))
    garden = proc_mat('pv_garden', (0.08, 0.14, 0.04), 0.9, 0.4, 0.45)
    street = proc_mat('pv_street', (0.20, 0.17, 0.12), 0.95, 0.2, 0.25)
    fieldA = proc_mat('pv_fieldA', (0.24, 0.22, 0.10), 0.9, 0.3, 0.35)
    fieldB = proc_mat('pv_fieldB', (0.11, 0.15, 0.05), 0.9, 0.3, 0.4)
    fieldC = proc_mat('pv_fieldC', (0.15, 0.10, 0.06), 0.9, 0.3, 0.35)
    lake = water_mat('pv_lake', (0.10, 0.20, 0.30), (0.04, 0.08, 0.14))
    plane(coll, 'moor', 4000, -0.02, moor)
    lk = plane(coll, 'lake', 1, 0.0, lake, (120, 520))
    lk.scale = (700, 260, 1)

    def disc(name, r0, r1, mat, a0=0.0, a1=2 * math.pi, z=0.0, n=96):
        bm = bmesh.new()
        k = max(3, int(n * (a1 - a0) / (2 * math.pi)))
        inner = [bm.verts.new((r0 * math.cos(a0 + (a1 - a0) * i / k), r0 * math.sin(a0 + (a1 - a0) * i / k), z)) for i in range(k + 1)]
        outer = [bm.verts.new((r1 * math.cos(a0 + (a1 - a0) * i / k), r1 * math.sin(a0 + (a1 - a0) * i / k), z)) for i in range(k + 1)]
        for i in range(k):
            bm.faces.new([inner[i], inner[i + 1], outer[i + 1], outer[i]])
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        bm.free()
        me.materials.append(mat)
        o = bpy.data.objects.new(name, me)
        coll.objects.link(o)
        return o
    disc('plaza', 0, 10, street, z=0.01)
    rings = [(17.0, 8, 2), (30.0, 14, 2), (43.0, 20, 3)]
    walls = [23.5, 36.5, 50.0]
    for i, (r, n, age) in enumerate(rings):
        disc('garden%d' % i, r - 5.5, r + 5.5, garden, z=0.01)
    spokes = 4
    for k in range(spokes):
        a = 2 * math.pi * k / spokes + math.pi / 4
        st = plane(coll, 'spoke%d' % k, 1, 0.02, street, (math.cos(a) * 45, math.sin(a) * 45))
        st.scale = (90, 3.2, 1)
        st.rotation_euler = (0, 0, a)
    drum2, drum3 = cmesh('SM_Culture_C2DrumHouse_Intact'), cmesh('SM_Culture_C3DrumHouse_Intact')
    cone2, cone3 = cmesh('SM_Culture_C2RoofCone_Intact'), cmesh('SM_Culture_C3RoofCone_Intact')
    for i, (r, n, age) in enumerate(rings):
        for k in range(n):
            a = 2 * math.pi * (k + 0.5) / n + 0.08 * i
            if any(abs(((a - (2 * math.pi * s / spokes + math.pi / 4)) + math.pi) % (2 * math.pi) - math.pi) < 0.12 for s in range(spokes)):
                continue
            p = V((math.cos(a) * r, math.sin(a) * r, 0))
            rz = a + math.pi / 2          # door (-Y) faces the centre
            d3 = (age == 3 and k % 2 == 0)
            place(coll, drum3 if d3 else drum2, p, rz)
            place(coll, cone3 if d3 else cone2, p + V((0, 0, 5.6 if d3 else 2.75)), rz)
    seg_c = cmesh('SM_Culture_CRingWallCurve_Intact')
    seg_s = cmesh('SM_Culture_CRingWallStraight_Intact')
    for i, rw in enumerate(walls):
        n = int(2 * math.pi * rw / 10.0)
        for k in range(n):
            a = 2 * math.pi * (k + 0.5) / n
            if any(abs(((a - (2 * math.pi * s / spokes + math.pi / 4)) + math.pi) % (2 * math.pi) - math.pi) < 3.0 / rw for s in range(spokes)):
                continue
            p = V((math.cos(a) * rw, math.sin(a) * rw, 0))
            place(coll, seg_c if i == 0 else seg_s, p, a - math.pi / 2 + (math.pi if i == 0 else 0))
    place(coll, cmesh('SM_Culture_CSpire_Intact'), (0, 0, 0))
    gran = by_id(hosts, 'Age2HouseB')
    if gran:
        house(coll, gran, 'C', (11.5, 0, 0), math.pi / 2)
    # wedge fields with radial dry-stone walls
    nw = 28
    for k in range(nw):
        a0 = 2 * math.pi * k / nw
        a1 = 2 * math.pi * (k + 1) / nw
        disc('wedge%d' % k, 52, 52 + rng.uniform(60, 110), (fieldA, fieldB, fieldC)[k % 3], a0, a1, 0.015, n=200)
        for s in range(6):
            rr = 57 + 10 * s
            place(coll, seg_s, (math.cos(a0) * rr, math.sin(a0) * rr, 0), a0)
    stones = [cmesh('SM_Culture_CStandingStone%d_Intact' % (i + 1)) for i in range(3)]
    for k in range(14):
        for sx in (-1, 1):
            place(coll, stones[(k + (sx > 0)) % 3], (sx * 5.0 + rng.uniform(-0.4, 0.4), -60 - 9 * k, 0), rng.uniform(0, 6))
    figures(coll, [(0.6, -11.5, 0.0), (-1.0, -12.4, 0.0), (5.2, -20.5, 0.0)])
    return dict(t40=V((-2, -22, 3)), t300=V((0, 0, 2)), az40=-24.0)


def states_set(sc, coll, hosts):
    a = by_id(hosts, 'Age2HouseA')
    b = by_id(hosts, 'Age2HouseB') or a
    grd = proc_mat('pv_ground_n', (0.26, 0.24, 0.20), 0.95, 0.05, 0.2)
    water = water_mat('pv_lagoon', (0.10, 0.55, 0.50), (0.03, 0.24, 0.30))
    plane(coll, 'ground', 4000, -0.02, grd)
    wp = plane(coll, 'water', 1, 0.0, water, (0, 22))
    wp.scale = (200, 16, 1)
    sts = ('Intact', 'Construction', 'Damaged', 'Ruin')
    for i, st in enumerate(sts):
        x = (i - 1.5) * 17.0
        if a:
            house(coll, a, 'A', (x, 0, 0), state=st)
        if b:
            house(coll, b, 'B', (x, 22, -1.8), state=st, stilts=True)
        place(coll, cmesh('SM_Culture_C2DrumHouse_%s' % st), (x, -20, 0))
        place(coll, cmesh('SM_Culture_C2RoofCone_%s' % st), (x, -20, 2.75))
    figures(coll, [((i - 1.5) * 17.0 - 5.0, -6.0, 0.0) for i in range(4)])
    return dict(t40=V((0, -2, 2)), t300=V((0, 0, 3)), az40=-12.0, d40=128.0, el40=36.0)


def kit_set(sc, coll, hosts):
    grd = proc_mat('pv_ground_n', (0.26, 0.24, 0.20), 0.95, 0.05, 0.2)
    plane(coll, 'ground', 4000, -0.02, grd)
    rows = [
        ['SM_Culture_ATempleNiche_Intact', 'SM_Culture_ATerraceStair_Intact', 'SM_Culture_ACistern_Intact',
         'SM_Culture_A2Ornament_Intact', 'SM_Culture_A3Ornament_Intact'],
        ['SM_Culture_BTempleSpiral_Intact', 'SM_Culture_BBridge_Intact', 'SM_Culture_BWalkwayCurve_Intact',
         'SM_Culture_BWalkwayStraight_Intact', 'SM_Culture_BGardenRaft_Intact', 'SM_Culture_BBreakwater_Intact',
         'SM_Culture_B2Ornament_Intact', 'SM_Culture_B3Ornament_Intact'],
        ['SM_Culture_CSpire_Intact', 'SM_Culture_C3DrumHouse_Intact', 'SM_Culture_C2DrumHouse_Intact',
         'SM_Culture_CRingWallCurve_Intact', 'SM_Culture_CRingWallStraight_Intact', 'SM_Culture_CStandingStone1_Intact',
         'SM_Culture_CStandingStone2_Intact', 'SM_Culture_CStandingStone3_Intact', 'SM_Culture_C2Ornament_Intact',
         'SM_Culture_C3Ornament_Intact'],
    ]
    y = 0.0
    for row in rows:
        x = -40.0
        depth = 0.0
        for nm in row:
            me = cmesh(nm)
            if not me:
                continue
            xs = [v.co.x for v in me.vertices]
            ys = [v.co.y for v in me.vertices]
            w = max(xs) - min(xs)
            place(coll, me, (x - min(xs), y - min(ys), 0))
            if 'Cone' not in nm and 'Drum' in nm:
                place(coll, cmesh(nm.replace('DrumHouse', 'RoofCone')), (x - min(xs), y - min(ys), 2.75 if 'C2' in nm else 5.6))
            x += w + 3.0
            depth = max(depth, max(ys) - min(ys))
        y += depth + 6.0
    return dict(t40=V((-4, 20, 4)), t300=V((0, 20, 4)), az40=-15.0, d40=95.0)


# ------------------------------------------------------------------------------------------- main

def main():
    o = args()
    sc = bpy.context.scene
    # hide every source object of culture.blend; build the preview in a fresh collection
    for c in list(sc.collection.children):
        sc.collection.children.unlink(c)
    for ob in list(sc.collection.objects):
        sc.collection.objects.unlink(ob)
    coll = bpy.data.collections.new('preview')
    sc.collection.children.link(coll)
    setup_render(sc)
    hosts = hosts_json()
    fn = {'a': vignette_a, 'b': vignette_b, 'c': vignette_c, 'states': states_set, 'kit': kit_set}[o['set']]
    sun_az = {'a': 245.0, 'b': 250.0, 'c': 235.0}.get(o['set'], 240.0)
    setup_light(sc, sun_az=sun_az, sun_el=20.0, haze=0.00018 if o['set'] in ('a', 'b', 'c') else 0.0)
    info = fn(sc, coll, hosts)
    views = o['views'] or ['40m', '300m', '1km']
    os.makedirs(os.path.join(ROOT, 'shots'), exist_ok=True)
    for vw in views:
        if vw == '40m':
            camera(sc, info['t40'], info.get('d40', 40.0), info.get('el40', 24.0), info.get('az40', -30.0), 50.0, 'cam40')
        elif vw == '300m':
            camera(sc, info['t300'], 300.0, 40.0, -20.0, 50.0, 'cam300')
        elif vw == '1km':
            camera(sc, info['t300'], 1000.0, 40.0, -20.0, 50.0, 'cam1k')
        out = os.path.join(ROOT, 'shots', 'asset-culture-%s%s-%s.png' % (o['set'], o['tag'], vw))
        sc.render.filepath = out
        bpy.ops.render.render(write_still=True)
        print('[preview] wrote', out, flush=True)


if __name__ == '__main__':
    main()
