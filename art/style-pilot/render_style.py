# DEMIURGE style pilot - renders of the test scene in every look (Cycles, golden hour).
#   blender -b art/style-pilot/scene/style_village.blend --python art/style-pilot/render_style.py --
#           [--looks=CUR,PHOTO,P,S1,S2] [--views=s300,s80,c15] [--samples=96] [--res=960,600] [--out=<dir>]
# Looks: CUR = the shipped procedural sets (art/textures, art/age1/textures); PHOTO = the photoreal Flow library
# as generated; P = the library processed down; S1 / S2 = the two stylised Flow prompt families. All Flow sets are
# make_maps.py output from maps/<LOOK>/ (style_maps.py). Geometry, vertex masks, light and camera never change.
import json
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
MAPS = os.path.join(HERE, 'maps')
NAMES = {'rubble': 'Rubble', 'thatch': 'Thatch', 'mudbrick': 'Mudbrick', 'plaster': 'Plaster',
         'terracotta': 'Terracotta', 'planks': 'Planks', 'path': 'Path', 'grass': 'Grass'}
# CURRENT procedural sets for the materials the kit has: (D, N, ORM, tile m)
def _cur(d, stem, tile):
    return tuple(os.path.join(ROOT, d, '%s_%s.png' % (stem, k)) for k in ('BC' if 'art/textures/' in d + '/' else 'D', 'N', 'ORM')) + (tile,)
CUR = {
    'plaster': _cur('art/age1/textures', 'T_Age1_Render', 3.0),        # the kit's clay render
    'path': _cur('art/age1/textures', 'T_Age1_Soil', 3.0),
    'grass': _cur('art/textures/turf', 'T_Turf', None),
}
# view: distance m, azimuth deg (0 = from the south / the shrine's front, + = toward east), elevation deg, lens, target
VIEWS = {
    's300': (300.0, 35.0, 45.0, 50.0, (0.0, -3.0, 0.0)),     # strategy camera, town scale
    's80': (80.0, 35.0, 38.0, 35.0, (0.0, -2.0, 0.0)),       # strategy camera, zoomed in on the block
    'c15': (15.0, 28.0, 11.0, 35.0, (0.6, -1.2, 1.9)),       # close: shrine front, a house behind
}
SUN_EL, SUN_AZ = 16.0, -50.0


def tex_manifest_tile(name):
    with open(os.path.join(ROOT, 'art', 'textures', 'manifest.json')) as f:
        return json.load(f)[name]['tile_m']


def look_set(look, key):
    """-> (D, N, ORM, tile_m) for a look and material key, or None to keep the asset's own material."""
    if look == 'CUR':
        if key not in CUR:
            return None
        d, n, o, t = CUR[key]
        return d, n, o, t if t else tex_manifest_tile('turf')
    nm = NAMES[key]
    base = os.path.join(MAPS, look, 'T_Style%s_%s_' % (look, nm))
    with open(os.path.join(MAPS, look, '%s.json' % nm)) as f:
        tile = json.load(f)['tile_m']
    return base + 'D.png', base + 'N.png', base + 'ORM.png', tile


def img(path, color):
    im = bpy.data.images.load(path, check_existing=True)
    im.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
    return im


_mean_cache = {}


def mean_rgb(path):
    if path not in _mean_cache:
        im = img(path, True)
        im.scale(64, 64)
        px = np.empty(64 * 64 * 4, np.float32)
        im.pixels.foreach_get(px)
        _mean_cache[path] = px.reshape(-1, 4)[:, :3].mean(0)
        im.reload()
    return _mean_cache[path]


_mats = {}


def swapped(orig, look, key, uv_name, uv_unit):
    """Copy of the asset material with its D / N / ORM images replaced by the look's set and its UVs rescaled
    from the asset's metric UV unit (uv_unit m) to the set's tile. Vertex masks, tints and mixes are kept."""
    ck = (orig.name, look, key, uv_unit)
    if ck in _mats:
        return _mats[ck]
    s = look_set(look, key)
    if s is None:
        return orig
    d, n, o, tile = s
    m = orig.copy()
    m.name = '%s__%s_%s' % (orig.name, look, key)
    nt = m.node_tree
    uv = nt.nodes.new('ShaderNodeUVMap')
    uv.uv_map = uv_name
    mp = nt.nodes.new('ShaderNodeMapping')
    k = uv_unit / tile
    mp.inputs['Scale'].default_value = (k, k, 1.0)
    nt.links.new(uv.outputs[0], mp.inputs[0])
    for nd in nt.nodes:
        if nd.bl_idname != 'ShaderNodeTexImage' or not nd.image:
            continue
        nm = os.path.splitext(nd.image.name)[0]
        kind = 'D' if (nm.endswith('_BC') or nm.endswith('_D')) else 'ORM' if nm.endswith('_ORM') else 'N' if nm.endswith('_N') else None
        if not kind:
            continue
        nd.image = img({'D': d, 'N': n, 'ORM': o}[kind], kind == 'D')
        nd.interpolation = 'Cubic' if kind == 'N' else 'Linear'
        nt.links.new(mp.outputs[0], nd.inputs[0])
    _mats[ck] = m
    return m


def fringe_for(orig, look):
    """The eave fringe card has no Flow version yet: multiply its colour by (look thatch mean / procedural
    thatch mean) so the eave does not step in colour."""
    ck = (orig.name, look, 'fringe')
    if ck in _mats:
        return _mats[ck]
    if look == 'CUR':
        return orig
    th = look_set(look, 'thatch')[0]
    ratio = mean_rgb(th) / np.maximum(mean_rgb(os.path.join(ROOT, 'art', 'textures', 'thatch', 'T_Thatch_BC.png')), 1e-3)
    m = orig.copy()
    m.name = '%s__%s' % (orig.name, look)
    nt = m.node_tree
    bsdf = nt.nodes['Principled BSDF']
    src = bsdf.inputs['Base Color'].links[0].from_socket
    mul = nt.nodes.new('ShaderNodeMix')
    mul.data_type = 'RGBA'
    mul.blend_type = 'MULTIPLY'
    mul.inputs['Factor'].default_value = 1.0
    mul.inputs[7].default_value = (*[float(min(2.0, r)) for r in ratio], 1.0)
    nt.links.new(src, mul.inputs[6])
    nt.links.new(mul.outputs[2], bsdf.inputs['Base Color'])
    _mats[ck] = m
    return m


def apply_look(look):
    for o in bpy.data.objects:
        part = o.get('style_part')
        if part not in ('shrine', 'house'):
            continue
        if 'orig_mats' not in o:
            o['orig_mats'] = [s.material.name if s.material else '' for s in o.material_slots]
        uv_name = o.data.uv_layers[0].name
        for s, on in zip(o.material_slots, o['orig_mats']):
            if not on:
                continue
            orig = bpy.data.materials[on]
            base = on.split('.')[0]
            new = orig
            if part == 'shrine':
                key = {'rubble': 'rubble', 'thatch': 'thatch', 'oak': 'planks'}.get(base)
                if key:
                    new = swapped(orig, look, key, uv_name, 2.0)
                elif base == 'fringe':
                    new = fringe_for(orig, look)
            else:
                wall, roof = o['style_wall'], o['style_roof']
                if base == 'M_Age1_Mudbrick':
                    if wall == 'plaster':
                        new = swapped(orig, look, 'plaster', uv_name, 2.0)
                    elif look != 'CUR':
                        new = swapped(orig, look, 'mudbrick', uv_name, 2.0)
                elif base == 'M_Age1_Thatch':
                    if roof == 'tiles' and look != 'CUR':      # the kit has no tile roof: CURRENT keeps thatch
                        new = swapped(orig, look, 'terracotta', uv_name, 2.0)
                    elif look != 'CUR':
                        new = swapped(orig, look, 'thatch', uv_name, 2.0)
                elif base == 'M_Age1_Render' and look != 'CUR':
                    new = swapped(orig, look, 'plaster', uv_name, 3.0)
            s.link = 'OBJECT'
            s.material = new
    g = bpy.data.objects['StyleGround']
    g.data.materials.clear()
    g.data.materials.append(ground_material(look))


def ground_material(look):
    name = 'StyleGround_%s' % look
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    def tex_set(key):
        d, n, o, tile = look_set(look, key)
        mp = nt.nodes.new('ShaderNodeMapping')
        mp.inputs['Scale'].default_value = (1 / tile, 1 / tile, 1)
        nt.links.new(tc.outputs['Object'], mp.inputs[0])
        out = []
        for p, col in ((d, True), (n, False), (o, False)):
            t = nt.nodes.new('ShaderNodeTexImage')
            t.image = img(p, col)
            nt.links.new(mp.outputs[0], t.inputs[0])
            out.append(t.outputs['Color'])
        return out
    gD, gN, gO = tex_set('grass')
    pD, pN, pO = tex_set('path')
    mk = nt.nodes.new('ShaderNodeTexImage')
    mk.image = img(os.path.join(HERE, 'scene', 'path_mask.png'), False)
    mk.extension = 'EXTEND'
    mmp = nt.nodes.new('ShaderNodeMapping')
    mmp.inputs['Location'].default_value = (0.5, 0.5, 0)
    mmp.inputs['Scale'].default_value = (0.01, 0.01, 1)
    nt.links.new(tc.outputs['Object'], mmp.inputs[0])
    nt.links.new(mmp.outputs[0], mk.inputs[0])
    # macro variation on the grass (identical in every look): 0.86-1.10 at 25-60 m
    nz = nt.nodes.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = 0.025
    nz.inputs['Detail'].default_value = 2.0
    nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
    mr = nt.nodes.new('ShaderNodeMapRange')
    mr.inputs['To Min'].default_value = 0.86
    mr.inputs['To Max'].default_value = 1.10
    mr.inputs['From Min'].default_value = 0.3
    mr.inputs['From Max'].default_value = 0.7
    nt.links.new(nz.outputs['Fac'], mr.inputs['Value'])
    gmul = nt.nodes.new('ShaderNodeMix')
    gmul.data_type = 'RGBA'
    gmul.blend_type = 'MULTIPLY'
    gmul.inputs['Factor'].default_value = 1.0
    nt.links.new(gD, gmul.inputs[6])
    nt.links.new(mr.outputs['Result'], gmul.inputs[7])
    def mix(a, b):
        x = nt.nodes.new('ShaderNodeMix')
        x.data_type = 'RGBA'
        nt.links.new(mk.outputs['Color'], x.inputs['Factor'])
        nt.links.new(a, x.inputs[6])
        nt.links.new(b, x.inputs[7])
        return x.outputs[2]
    nt.links.new(mix(gmul.outputs[2], pD), bsdf.inputs['Base Color'])
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(mix(gO, pO), sep.inputs[0])
    nt.links.new(sep.outputs[1], bsdf.inputs['Roughness'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nt.links.new(mix(gN, pN), nm.inputs['Color'])
    nt.links.new(nm.outputs[0], bsdf.inputs['Normal'])
    return m


def setup_light():
    sc = bpy.context.scene
    w = bpy.data.worlds.new('StyleSky')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    sky = nt.nodes.new('ShaderNodeTexSky')
    try:
        sky.sky_type = 'MULTIPLE_SCATTERING'
    except Exception:
        sky.sky_type = 'NISHITA'
    sky.sun_disc = False
    sky.sun_elevation = math.radians(SUN_EL)
    sky.sun_rotation = math.radians(180 + SUN_AZ)
    nt.links.new(sky.outputs[0], bg.inputs[0])
    nt.links.new(bg.outputs[0], out.inputs[0])
    bg.inputs[1].default_value = 0.22
    ld = bpy.data.lights.new('StyleSun', 'SUN')
    sun = bpy.data.objects.new('StyleSun', ld)
    sc.collection.objects.link(sun)
    ld.energy = 5.0
    ld.color = (1.0, 0.74, 0.50)
    ld.angle = math.radians(0.8)
    el, az = math.radians(SUN_EL), math.radians(SUN_AZ)
    d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
    sun.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()


def setup_camera(view, W, H):
    sc = bpy.context.scene
    dist, az, el, lens, tgt = VIEWS[view]
    cam = bpy.data.objects.get('StyleCam')
    if not cam:
        cam = bpy.data.objects.new('StyleCam', bpy.data.cameras.new('StyleCam'))
        sc.collection.objects.link(cam)
    cam.data.lens = lens
    cam.data.clip_start = 0.1
    cam.data.clip_end = 6000
    sc.camera = cam
    t = Vector(tgt)
    a, e = math.radians(az), math.radians(el)
    cam.location = t + Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e))) * dist
    cam.rotation_euler = (t - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.resolution_x, sc.render.resolution_y = W, H


def setup_render(samples):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        for dt in ('CUDA',):
            prefs.compute_device_type = dt
            prefs.get_devices()
            if any(d.type == dt for d in prefs.devices):
                break
        for d in prefs.devices:
            d.use = d.type == prefs.compute_device_type
        sc.cycles.device = 'GPU' if any(d.use for d in prefs.devices) else 'CPU'
    except Exception as e:
        print('STYLE_DEVICE_ERR', e)
    print('STYLE_DEVICE', sc.cycles.device)
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.view_settings.view_transform = 'AgX'
    looks = [i.identifier for i in sc.view_settings.bl_rna.properties['look'].enum_items]
    for l in ('AgX - Medium High Contrast', 'Medium High Contrast'):
        if l in looks:
            sc.view_settings.look = l
            break
    sc.render.image_settings.file_format = 'PNG'


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    f = dict(a[2:].split('=', 1) for a in argv if a.startswith('--') and '=' in a)
    out = os.path.abspath(f.get('out', os.path.join(HERE, 'renders')))
    looks = f.get('looks', 'CUR,PHOTO,P,S1,S2').split(',')
    views = f.get('views', 's300,s80,c15').split(',')
    W, H = [int(x) for x in f.get('res', '960,600').split(',')]
    os.makedirs(out, exist_ok=True)
    setup_render(int(f.get('samples', 96)))
    setup_light()
    for look in looks:
        if look != 'CUR' and not os.path.exists(os.path.join(MAPS, look, 'Rubble.json')):
            print('STYLE_SKIP_LOOK', look)
            continue
        apply_look(look)
        for view in views:
            setup_camera(view, W, H)
            sc = bpy.context.scene
            sc.render.filepath = os.path.join(out, '%s-%s.png' % (view, look))
            bpy.ops.render.render(write_still=True)
            print('STYLE_RENDER', sc.render.filepath)


main()
