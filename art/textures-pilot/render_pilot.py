# DEMIURGE texture pilot - BEFORE / AFTER renders of the shrine (Cycles, headless).
#   blender -b art/textures-pilot/scene/shrine_pilot.blend --python art/textures-pilot/render_pilot.py --
#           --out=<dir> [--views=v40,v300,close8] [--lights=golden,overcast] [--stone=StoneB] [--thatch=ThatchB]
#           [--samples=128] [--res=960,720] [--save]
# Renders <out>/<view>-<light>-<before|after>.png. BEFORE = the asset's own materials (procedural texture sets in
# art/textures, exactly as art/shrine/shrine.blend uses them). AFTER = the same materials with the Flow maps from
# art/textures-pilot/out swapped in on the walls + plinth (stone) and the roof (thatch); vertex masks (AO, stain,
# moss) are kept, so only the texture differs. --save writes the AFTER material setup into the .blend.
import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHRINE_DIR = os.path.join(ROOT, 'art', 'shrine')
OUT_TEX = os.path.join(HERE, 'out')
TILE_M = {'StoneA': 1.4, 'StoneB': 2.0, 'ThatchA': 1.0, 'ThatchB': 1.1}   # world size of one Flow tile
UV_M = 2.0      # the shrine's UV0 is metric: 1 UV unit = one 2 m tile of rubble / limestone / thatch

# view: distance m, azimuth deg (0 = straight at the front, + = toward the right wall), elevation deg, lens mm,
# target point
VIEWS = {
    'v40': (40.0, 30.0, 24.0, 135.0, (0.0, -0.3, 1.9)),       # 40 m three-quarter (asset preview / AssetLab hero)
    'v300': (300.0, 30.0, 30.0, 135.0, (0.0, -0.3, 1.9)),     # 300 m oblique (town view)
    'close8': (8.0, 34.0, 12.0, 50.0, (1.0, -1.2, 1.9)),      # close 8 m: wall, plinth and eave
}


def fix_paths():
    """A fresh copy of the shrine .blend refers to //..\\textures relative to art/shrine; the copy saved by --save
    (make_paths_relative) refers to //..\\..\\textures relative to scene/. Keep any path that resolves, else
    try it relative to art/shrine. (Independent check: the old version always re-based on art/shrine, which
    broke every procedural image of the saved scene, so the README's reproduce command rendered pink.)"""
    for im in bpy.data.images:
        fp = im.filepath
        if not fp or os.path.exists(bpy.path.abspath(fp)):
            continue
        if fp.startswith('//'):
            alt = os.path.normpath(os.path.join(SHRINE_DIR, fp[2:].replace('\\', '/')))
            if os.path.exists(alt):
                im.filepath = alt
                continue
        print('PILOT_MISSING_IMAGE', im.name, fp)


def show_only(coll='intact_LOD0'):
    keep = set(o.name for o in bpy.data.collections[coll].objects)
    for o in bpy.data.objects:
        if o.type in ('MESH', 'CURVE'):
            o.hide_render = o.name not in keep
    for c in bpy.data.collections:
        c.hide_render = not (c.name == coll or c.name == coll.rsplit('_', 1)[0])


def flow_material(base_name, set_name):
    """Copy of the asset material `base_name` with its BC / N / ORM images replaced by the Flow set and the UVs
    scaled from the 2 m metric UV to the Flow tile size."""
    src = bpy.data.materials[base_name]
    m = src.copy()
    m.name = '%s_flow_%s' % (base_name, set_name)
    nt = m.node_tree
    uv = nt.nodes.new('ShaderNodeUVMap')
    uv.uv_map = 'UV0'
    mp = nt.nodes.new('ShaderNodeMapping')
    k = UV_M / TILE_M[set_name]
    mp.inputs['Scale'].default_value = (k, k, 1.0)
    nt.links.new(uv.outputs[0], mp.inputs[0])
    for n in nt.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            nm = n.image.name
            kind = 'D' if '_BC' in nm else 'N' if '_N' in nm else 'ORM' if '_ORM' in nm else None
            if not kind:
                continue
            p = os.path.join(OUT_TEX, 'T_Shrine_%s_%s.png' % (set_name, kind))
            img = bpy.data.images.load(p, check_existing=True)
            img.colorspace_settings.name = 'sRGB' if kind == 'D' else 'Non-Color'
            n.image = img
            n.interpolation = 'Cubic' if kind == 'N' else 'Linear'
            nt.links.new(mp.outputs[0], n.inputs[0])
    return m


def apply_after(stone, thatch):
    # back to the asset's own (DATA) slots first: in the scene saved by --save the slots are already OBJECT-linked
    # to a *_flow_* material, so the name tests below would never match and e.g. --stone=StoneA would silently
    # render the saved StoneB set (independent check).
    apply_before()
    ms = flow_material('rubble', stone)
    mp = flow_material('limestone', stone)    # plinth only: steps and altar keep the fitted limestone
    mt = flow_material('thatch', thatch)
    for o in bpy.data.collections['intact_LOD0'].objects:
        part = o.name.split('.')[1]
        for s in o.material_slots:
            if not s.material:
                continue
            if part == 'walls' and s.material.name == 'rubble':
                s.link = 'OBJECT'; s.material = ms
            elif part == 'plinth' and s.material.name == 'limestone':
                s.link = 'OBJECT'; s.material = mp
            elif part.startswith('roof') and s.material.name == 'thatch':
                s.link = 'OBJECT'; s.material = mt


def apply_before():
    for o in bpy.data.collections['intact_LOD0'].objects:
        for s in o.material_slots:
            if s.link == 'OBJECT':
                s.link = 'DATA'


def setup_ground():
    g = bpy.data.objects.get('ground')
    if g:
        g.hide_render = True
    if 'PilotGround' in bpy.data.objects:
        # the scene saved by --save already has it, and show_only() has just hidden every mesh outside the
        # LOD0 collection, the ground included (independent check: re-runs rendered with no ground)
        bpy.data.objects['PilotGround'].hide_render = False
        return
    me = bpy.data.meshes.new('PilotGround')
    s = 1500.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new('PilotGround', me)
    bpy.context.scene.collection.objects.link(ob)
    m = bpy.data.materials.new('pilot_ground')
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nz = nt.nodes.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = 0.08
    nz.inputs['Detail'].default_value = 8.0
    nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.42
    ramp.color_ramp.elements[0].color = (0.16, 0.115, 0.075, 1)    # beaten earth
    ramp.color_ramp.elements[1].position = 0.62
    ramp.color_ramp.elements[1].color = (0.12, 0.12, 0.05, 1)      # dry grass
    nt.links.new(nz.outputs['Fac'], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], p.inputs['Base Color'])
    p.inputs['Roughness'].default_value = 0.95
    me.materials.append(m)


def setup_light(light):
    sc = bpy.context.scene
    w = sc.world or bpy.data.worlds.new('PilotWorld')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    nt.links.new(bg.outputs[0], out.inputs[0])
    sun = bpy.data.objects.get('PilotSun')
    if not sun:
        ld = bpy.data.lights.new('PilotSun', 'SUN')
        sun = bpy.data.objects.new('PilotSun', ld)
        sc.collection.objects.link(sun)
    for o in bpy.data.objects:
        if o.type == 'LIGHT' and o is not sun:
            o.hide_render = True
    if light == 'golden':
        sky = nt.nodes.new('ShaderNodeTexSky')
        sky.sky_type = 'MULTIPLE_SCATTERING'
        sky.sun_disc = False
        sky.sun_elevation = math.radians(7.0)
        sky.sun_rotation = math.radians(-140.0)
        nt.links.new(sky.outputs[0], bg.inputs[0])
        bg.inputs[1].default_value = 0.14
        el, az = math.radians(8.0), math.radians(-55.0)        # low sun from the front-left
        sun.data.energy = 6.5
        sun.data.color = (1.0, 0.66, 0.40)
        sun.data.angle = math.radians(0.6)
        sun.hide_render = False
        sc.view_settings.exposure = 0.0
    else:   # overcast: no direct sun, bright even sky with a slightly lighter zenith
        grad = nt.nodes.new('ShaderNodeTexGradient')
        tc = nt.nodes.new('ShaderNodeTexCoord')
        sep = nt.nodes.new('ShaderNodeSeparateXYZ')
        nt.links.new(tc.outputs['Generated'], sep.inputs[0])
        ramp = nt.nodes.new('ShaderNodeValToRGB')
        ramp.color_ramp.elements[0].position = 0.0
        ramp.color_ramp.elements[0].color = (0.50, 0.52, 0.55, 1)
        ramp.color_ramp.elements[1].position = 1.0
        ramp.color_ramp.elements[1].color = (0.95, 0.97, 1.0, 1)
        nt.links.new(sep.outputs[2], ramp.inputs[0])
        nt.links.new(ramp.outputs[0], bg.inputs[0])
        bg.inputs[1].default_value = 1.6
        el, az = math.radians(50.0), math.radians(-30.0)
        sun.data.energy = 0.0
        sun.hide_render = True
        sc.view_settings.exposure = 0.0
    d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
    sun.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()


def setup_camera(view, W, H):
    sc = bpy.context.scene
    dist, az, el, lens, tgt = VIEWS[view]
    cam = bpy.data.objects.get('PilotCam')
    if not cam:
        cd = bpy.data.cameras.new('PilotCam')
        cam = bpy.data.objects.new('PilotCam', cd)
        sc.collection.objects.link(cam)
    cam.data.lens = lens
    cam.data.clip_start = 0.1
    cam.data.clip_end = 5000
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
        prefs.compute_device_type = 'CUDA'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type == 'CUDA'
        sc.cycles.device = 'GPU' if any(d.use for d in prefs.devices) else 'CPU'
    except Exception as e:
        print('PILOT_DEVICE_ERR', e)
    print('PILOT_DEVICE', sc.cycles.device, [(d.name, d.type, d.use) for d in prefs.devices])
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'AgX'
    looks = [i.identifier for i in sc.view_settings.bl_rna.properties['look'].enum_items]
    for l in ('AgX - Medium High Contrast', 'Medium High Contrast'):
        if l in looks:
            sc.view_settings.look = l
            break
    sc.render.image_settings.file_format = 'PNG'


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    f = dict(a[2:].split('=', 1) for a in argv if a.startswith('--') and '=' in a)
    out = os.path.abspath(f['out'])        # Blender resolves a bare relative path against the drive root
    views = f.get('views', 'v40,v300,close8').split(',')
    lights = f.get('lights', 'golden,overcast').split(',')
    variants = f.get('variants', 'before,after').split(',')
    W, H = [int(x) for x in f.get('res', '960,720').split(',')]
    os.makedirs(out, exist_ok=True)
    fix_paths()
    show_only()
    setup_ground()
    setup_render(int(f.get('samples', 128)))
    stone, thatch = f.get('stone', 'StoneB'), f.get('thatch', 'ThatchB')
    for var in variants:
        if var not in ('before', 'after'):
            continue                       # --variants=none: only --save
        if var == 'after':
            apply_after(stone, thatch)
        else:
            apply_before()
        for light in lights:
            setup_light(light)
            for view in views:
                setup_camera(view, W, H)
                sc = bpy.context.scene
                sc.render.filepath = os.path.join(out, '%s-%s-%s.png' % (view, light, var))
                bpy.ops.render.render(write_still=True)
                print('PILOT_RENDER', sc.render.filepath)
    if '--save' in argv:
        apply_after(stone, thatch)
        setup_light('golden')
        setup_camera('v40', W, H)
        bpy.ops.file.make_paths_relative()
        bpy.ops.wm.save_mainfile()
        print('PILOT_SAVED', bpy.data.filepath)


main()
