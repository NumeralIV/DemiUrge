# DEMIURGE art pipeline - quick Cycles previews of an asset .blend in the reference-sheet framing
# (three-quarter front, turned ~30 deg so the right side shows, warm key from the left, #c8c8c8 backdrop).
#   blender.exe -b art/<asset>/<asset>.blend --python art/tools/render_preview.py -- <out_dir> [configs...] [--lod=N]
#     [--view=az,el,dist] [--res=W,H] [--samples=N]
# Renders <out_dir>/<config>[_lodN].png. Iteration tool; the judged renders are the Unreal ones (AssetLab).
import math
import os
import sys

import bpy
from mathutils import Vector


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    out = argv[0]
    flags = dict(a[2:].split('=') for a in argv[1:] if a.startswith('--') and '=' in a)
    cfgs = [a for a in argv[1:] if not a.startswith('--')]
    lod = int(flags.get('lod', 0))
    az, el, dist = [float(x) for x in flags.get('view', '30,18,17').split(',')]
    W, H = [int(x) for x in flags.get('res', '960,720').split(',')]
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type = 'CUDA'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type == 'CUDA'
        sc.cycles.device = 'GPU'
    except Exception:
        pass
    sc.cycles.samples = int(flags.get('samples', 96))
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = W, H
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Medium High Contrast' if 'AgX - Medium High Contrast' in [i.identifier for i in sc.view_settings.bl_rna.properties['look'].enum_items] else 'None'
    # backdrop + soft sky
    w = sc.world or bpy.data.worlds.new('W')
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes.get('Background')
    bg.inputs[0].default_value = (0.58, 0.58, 0.58, 1)
    bg.inputs[1].default_value = 0.55
    # key light (warm, from the front-left, low) and camera
    sun = bpy.data.objects.get('PreviewSun')
    if not sun:
        ld = bpy.data.lights.new('PreviewSun', 'SUN')
        sun = bpy.data.objects.new('PreviewSun', ld)
        sc.collection.objects.link(sun)
    sun.data.energy = 4.0
    sun.data.color = (1.0, 0.86, 0.7)
    sun.data.angle = math.radians(2.0)
    sun.rotation_euler = (math.radians(55), 0, math.radians(-120))
    cam = bpy.data.objects.get('PreviewCam')
    if not cam:
        cd = bpy.data.cameras.new('PreviewCam')
        cam = bpy.data.objects.new('PreviewCam', cd)
        sc.collection.objects.link(cam)
    cam.data.lens = 50
    sc.camera = cam
    tgt = Vector((0, -0.3, 1.9))
    a, e = math.radians(az), math.radians(el)
    cam.location = tgt + Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e))) * dist
    cam.rotation_euler = (tgt - cam.location).to_track_quat('-Z', 'Y').to_euler()
    ground = bpy.data.objects.get('ground')
    if ground:
        ground.hide_render = False
        if not ground.data.materials:
            m = bpy.data.materials.new('ground_grey')
            m.use_nodes = True
            m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.58, 0.58, 0.58, 1)
            m.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.9
            ground.data.materials.append(m)
    allc = [c for c in bpy.data.collections if '_LOD' in c.name]
    names = sorted(set(c.name.rsplit('_LOD', 1)[0] for c in allc))
    cfgs = cfgs or names
    os.makedirs(out, exist_ok=True)
    for cfg in cfgs:
        for c in allc:
            show = c.name == '%s_LOD%d' % (cfg, lod)
            c.hide_render = not show
            for o in c.objects:
                o.hide_render = not show
        for c in bpy.data.collections:
            if c.name.startswith('export_'):
                for o in c.objects:
                    o.hide_render = True
        water = bpy.data.objects.get('PreviewWater')
        if cfg == 'drowned':
            if not water:
                me = bpy.data.meshes.new('PreviewWater')
                s = 12
                me.from_pydata([(-s, -s, 1.8), (s, -s, 1.8), (s, s, 1.8), (-s, s, 1.8)], [], [(0, 1, 2, 3)])
                water = bpy.data.objects.new('PreviewWater', me)
                sc.collection.objects.link(water)
                m = bpy.data.materials.new('water')
                m.use_nodes = True
                p = m.node_tree.nodes['Principled BSDF']
                p.inputs['Base Color'].default_value = (0.13, 0.22, 0.19, 1)
                p.inputs['Roughness'].default_value = 0.05
                p.inputs['Transmission Weight'].default_value = 0.85
                me.materials.append(m)
            water.hide_render = False
        elif water:
            water.hide_render = True
        sc.render.filepath = os.path.join(out, cfg + ('' if lod == 0 else '_lod%d' % lod) + '.png')
        bpy.ops.render.render(write_still=True)
        print('PREVIEW', sc.render.filepath)


main()
