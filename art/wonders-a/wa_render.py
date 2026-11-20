# DEMIURGE art - wonders-a: preview rig (docs/ASSETS.md section 3). EEVEE 1280x720, golden-hour sun at
# 20 degrees, cool sky fill, neutral ground, views at 40 m / 300 m / 1 km, plus a night view and a states sheet.
import math
import os

import bpy
import numpy as np
from mathutils import Vector, Euler

import wa_mat


def _node(nt, kind, **kw):
    n = nt.nodes.new(kind)
    for k, v in kw.items():
        setattr(n, k, v)
    return n


def setup(water=False, ground_hex='#6b675a'):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    sc.render.film_transparent = False
    ev = sc.eevee
    ev.taa_render_samples = 48
    try:
        ev.use_raytracing = True
        ev.ray_tracing_options.resolution_scale = '2'
    except Exception:
        pass
    try:
        ev.use_shadows = True
        ev.shadow_resolution_scale = 1.0
        ev.use_volumetric_shadows = False
        ev.volumetric_end = 4000.0
        ev.volumetric_start = 5.0
        ev.volumetric_tile_size = '8'
    except Exception:
        pass
    sc.view_settings.view_transform = 'AgX'
    for look in ('AgX - Medium High Contrast', 'Medium High Contrast', 'None'):
        try:
            sc.view_settings.look = look
            break
        except Exception:
            pass
    sc.view_settings.exposure = 0.0
    # world: cool sky gradient + faint warm haze volume
    w = bpy.data.worlds.get('WA_World') or bpy.data.worlds.new('WA_World')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = _node(nt, 'ShaderNodeOutputWorld')
    tc = _node(nt, 'ShaderNodeTexCoord')
    sep = _node(nt, 'ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Generated'], sep.inputs[0])
    ramp = _node(nt, 'ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = (0.62, 0.58, 0.52, 1)
    ramp.color_ramp.elements[1].position = 0.45
    ramp.color_ramp.elements[1].color = (0.20, 0.32, 0.55, 1)
    nt.links.new(sep.outputs['Z'], ramp.inputs['Fac'])
    bg = _node(nt, 'ShaderNodeBackground')
    bg.label = 'SKY'
    bg.inputs['Strength'].default_value = 1.0
    nt.links.new(ramp.outputs['Color'], bg.inputs['Color'])
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])
    vol = _node(nt, 'ShaderNodeVolumePrincipled')
    vol.label = 'HAZE'
    vol.inputs['Color'].default_value = (0.75, 0.8, 0.9, 1)
    vol.inputs['Density'].default_value = 0.00012
    vol.inputs['Anisotropy'].default_value = 0.5
    # (world volume haze turned the whole frame black in EEVEE headless; left unlinked)
    # sun
    sd = bpy.data.lights.get('WA_Sun') or bpy.data.lights.new('WA_Sun', 'SUN')
    sd.color = (1.0, 0.74, 0.5)
    sd.energy = 4.2
    sd.angle = math.radians(1.2)
    sun = bpy.data.objects.get('WA_Sun') or bpy.data.objects.new('WA_Sun', sd)
    if sun.name not in sc.collection.objects:
        sc.collection.objects.link(sun)
    # ground / water
    rig = bpy.data.collections.get('WA_Rig') or bpy.data.collections.new('WA_Rig')
    if rig.name not in sc.collection.children:
        sc.collection.children.link(rig)
    if 'WA_Ground' not in bpy.data.objects:
        me = bpy.data.meshes.new('WA_Ground')
        S = 12000.0
        z = -10.0 if water else 0.0
        me.from_pydata([(-S, -S, z), (S, -S, z), (S, S, z), (-S, S, z)], [], [(0, 1, 2, 3)])
        g = bpy.data.objects.new('WA_Ground', me)
        rig.objects.link(g)
        m = bpy.data.materials.new('WA_GroundMat')
        m.use_nodes = True
        b = m.node_tree.nodes['Principled BSDF']
        nz = m.node_tree.nodes.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = 0.004
        cr = m.node_tree.nodes.new('ShaderNodeValToRGB')
        c0 = wa_mat.hex_rgb(ground_hex)
        cr.color_ramp.elements[0].color = (c0[0] * 0.8, c0[1] * 0.85, c0[2] * 0.8, 1)
        cr.color_ramp.elements[1].color = (c0[0] * 1.15, c0[1] * 1.1, c0[2] * 1.0, 1)
        m.node_tree.links.new(nz.outputs['Fac'], cr.inputs['Fac'])
        m.node_tree.links.new(cr.outputs['Color'], b.inputs['Base Color'])
        b.inputs['Roughness'].default_value = 0.95
        me.materials.append(m)
        if water:
            me = bpy.data.meshes.new('WA_Water')
            me.from_pydata([(-S, -S, 0), (S, -S, 0), (S, S, 0), (-S, S, 0)], [], [(0, 1, 2, 3)])
            wo = bpy.data.objects.new('WA_Water', me)
            rig.objects.link(wo)
            m = bpy.data.materials.new('WA_WaterMat')
            m.use_nodes = True
            b = m.node_tree.nodes['Principled BSDF']
            b.inputs['Base Color'].default_value = wa_mat.hex_rgb('#2f5f78') + [1]
            b.inputs['Roughness'].default_value = 0.06
            nz = m.node_tree.nodes.new('ShaderNodeTexNoise')
            nz.inputs['Scale'].default_value = 0.35
            bump = m.node_tree.nodes.new('ShaderNodeBump')
            bump.inputs['Strength'].default_value = 0.08
            m.node_tree.links.new(nz.outputs['Fac'], bump.inputs['Height'])
            m.node_tree.links.new(bump.outputs['Normal'], b.inputs['Normal'])
            me.materials.append(m)
    return sun


def aim_sun(sun, cam_az_deg, elev_deg=20.0):
    """Sun from the left of the view and a little behind the subject (concept framing)."""
    az = math.radians(cam_az_deg)
    fwd = Vector((-math.sin(az), math.cos(az), 0))
    left = Vector((-fwd.y, fwd.x, 0))
    d = (left * 0.85 + fwd * 0.5).normalized()
    e = math.radians(elev_deg)
    to_sun = Vector((d.x * math.cos(e), d.y * math.cos(e), math.sin(e)))
    sun.rotation_euler = (-to_sun).to_track_quat('-Z', 'Y').to_euler()


def camera(spec, name='WA_Cam'):
    target, dist, az, el, focal = spec
    az, el = math.radians(az), math.radians(el)
    t = Vector(target)
    pos = t + Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el))) * dist
    cd = bpy.data.cameras.get(name) or bpy.data.cameras.new(name)
    cd.lens = focal
    cd.sensor_width = 36.0
    cd.clip_start = 0.5
    cd.clip_end = 20000.0
    cam = bpy.data.objects.get(name) or bpy.data.objects.new(name, cd)
    if cam.name not in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.link(cam)
    cam.location = pos
    cam.rotation_euler = (t - pos).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.camera = cam
    return cam


def day(sun):
    sun.data.energy = 4.2
    sun.data.color = (1.0, 0.74, 0.5)
    w = bpy.context.scene.world.node_tree
    for n in w.nodes:
        if n.label == 'SKY':
            n.inputs['Strength'].default_value = 1.0
        if n.label == 'HAZE':
            n.inputs['Density'].default_value = 0.00012
    bpy.context.scene.view_settings.exposure = 0.0


def night(sun):
    sun.data.energy = 0.08
    sun.data.color = (0.6, 0.7, 1.0)
    w = bpy.context.scene.world.node_tree
    for n in w.nodes:
        if n.label == 'SKY':
            n.inputs['Strength'].default_value = 0.035
        if n.label == 'HAZE':
            n.inputs['Density'].default_value = 0.00005
    bpy.context.scene.view_settings.exposure = 1.0


def show_only(objs):
    keep = set(o.name for o in objs)
    for o in bpy.context.scene.objects:
        if o.type == 'MESH' and not o.name.startswith('WA_'):
            hide = o.name not in keep
            o.hide_render = hide
            o.hide_viewport = hide


def render_to(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('[render]', path)


def sheet(paths, out_path, cols=2):
    """2x2 contact sheet of 1280x720 renders, each scaled to half size."""
    W, H = 1280, 720
    canvas = np.zeros((H, W, 4), np.float32)
    canvas[..., 3] = 1
    for k, p in enumerate(paths):
        img = bpy.data.images.load(p, check_existing=False)
        a = np.array(img.pixels[:], np.float32).reshape(img.size[1], img.size[0], 4)
        bpy.data.images.remove(img)
        a = a.reshape(H // 2, 2, W // 2, 2, 4).mean(axis=(1, 3))
        r, c = k // cols, k % cols
        # pixels are bottom-up: row 0 = bottom; put first image top-left
        y0 = H // 2 * (1 - r)
        canvas[y0:y0 + H // 2, c * W // 2:(c + 1) * W // 2] = a
    canvas[H // 2 - 1:H // 2 + 1, :, :3] = 0.05
    canvas[:, W // 2 - 1:W // 2 + 1, :3] = 0.05
    img = bpy.data.images.new('_sheet', W, H, alpha=True)
    img.pixels.foreach_set(canvas.ravel())
    img.filepath_raw = out_path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)
    print('[sheet]', out_path)
