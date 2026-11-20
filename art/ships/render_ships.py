# DEMIURGE ships - preview renders (run through tools/render_slot.py).
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       art/ships/source/ships.blend --python art/ships/render_ships.py -- [--only A,B] [--views close,mid,far]
#       [--extra] [--lowres]
#
# Rig per docs/ASSETS.md §3: sun at 20 deg elevation, warm (golden hour) from the left of frame, cool sky fill,
# EEVEE 1280x720. Ground is open sea (the ships' ground) with the waterline at Z = 0.
# Views: close = 40 m three-quarter, mid = 300 m oblique, far = 1 km oblique. Mid and far views show the LOD
# Unreal would pick at that distance (screen sizes 1.0 / 0.35 / 0.12 / 0.04), and each image says which.
# --extra adds the line-up, culture, state and night sheets.
import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'shots')
sys.path.insert(0, HERE)
import ship_defs as sd  # noqa: E402

KEBAB = {'ReedBoat': 'reed-boat', 'Canoe': 'canoe', 'Galley': 'galley', 'Cog': 'cog', 'SailShip': 'sail-ship',
         'Steamship': 'steamship', 'ContainerShip': 'container-ship', 'FastFreighter': 'fast-freighter'}
LOD_SCREEN = [1.0, 0.35, 0.12, 0.04]
VFOV_MID = math.radians(30.0)


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = dict(only=None, views=['close', 'mid', 'far'], extra=False, lowres=False, noper=False)
    i = 0
    while i < len(a):
        if a[i] == '--only':
            o['only'] = a[i + 1].split(','); i += 1
        elif a[i] == '--views':
            o['views'] = a[i + 1].split(','); i += 1
        elif a[i] == '--extra':
            o['extra'] = True
        elif a[i] == '--extra-only':
            o['extra'] = True; o['noper'] = True
        elif a[i] == '--lowres':
            o['lowres'] = True
        i += 1
    return o


def lin(h):
    h = h.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


# --------------------------------------------------------------------------- rig


def rig(o):
    scn = bpy.context.scene
    scn.render.engine = 'BLENDER_EEVEE'
    scn.render.resolution_x = 640 if o['lowres'] else 1280
    scn.render.resolution_y = 360 if o['lowres'] else 720
    scn.render.resolution_percentage = 100
    scn.render.film_transparent = False
    ee = scn.eevee
    ee.taa_render_samples = 24 if o['lowres'] else 48
    for k, v in (('use_raytracing', True), ('use_shadows', True), ('shadow_ray_count', 2), ('shadow_step_count', 8),
                 ('use_fast_gi', True), ('volumetric_tile_size', '8')):
        try:
            setattr(ee, k, v)
        except Exception:
            pass
    try:
        ee.ray_tracing_options.resolution_scale = '2'
    except Exception:
        pass
    scn.view_settings.view_transform = 'AgX'
    try:
        scn.view_settings.look = 'AgX - Medium High Contrast'
    except Exception:
        pass
    scn.view_settings.exposure = -0.2
    # world: cool sky gradient with a warm horizon
    w = bpy.data.worlds.get('ShipPreviewSky') or bpy.data.worlds.new('ShipPreviewSky')
    scn.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    nt.links.new(tc.outputs['Generated'], sep.inputs[0])
    nt.links.new(sep.outputs['Z'], ramp.inputs['Fac'])
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = (0.85, 0.55, 0.32, 1)
    ramp.color_ramp.elements[1].position = 0.22
    ramp.color_ramp.elements[1].color = (0.16, 0.30, 0.60, 1)
    nt.links.new(ramp.outputs['Color'], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = 0.75
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])
    # sun
    sun = bpy.data.objects.get('PreviewSun')
    if sun is None:
        sun = bpy.data.objects.new('PreviewSun', bpy.data.lights.new('PreviewSun', 'SUN'))
        scn.collection.objects.link(sun)
    sun.data.energy = 5.0
    sun.data.color = (1.0, 0.70, 0.43)
    sun.data.angle = math.radians(1.2)
    sun.data.use_shadow = True
    # sea
    sea = bpy.data.objects.get('PreviewSea')
    if sea is None:
        me = bpy.data.meshes.new('PreviewSea')
        s = 8000.0
        me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
        sea = bpy.data.objects.new('PreviewSea', me)
        scn.collection.objects.link(sea)
        m = bpy.data.materials.new('PreviewSea')
        m.use_nodes = True
        t = m.node_tree
        bs = t.nodes['Principled BSDF']
        bs.inputs['Base Color'].default_value = (*lin('#123a4e'), 1)
        bs.inputs['Roughness'].default_value = 0.12
        bs.inputs['Specular IOR Level'].default_value = 0.5
        # wave normal: two noise layers in world space
        tc = t.nodes.new('ShaderNodeTexCoord')
        n1 = t.nodes.new('ShaderNodeTexNoise')
        n1.inputs['Scale'].default_value = 0.35
        n1.inputs['Detail'].default_value = 6.0
        mp = t.nodes.new('ShaderNodeMapping')
        mp.inputs['Scale'].default_value = (1.0, 2.2, 1.0)
        t.links.new(tc.outputs['Object'], mp.inputs['Vector'])
        t.links.new(mp.outputs['Vector'], n1.inputs['Vector'])
        bump = t.nodes.new('ShaderNodeBump')
        bump.inputs['Strength'].default_value = 0.25
        bump.inputs['Distance'].default_value = 0.3
        t.links.new(n1.outputs['Fac'], bump.inputs['Height'])
        t.links.new(bump.outputs['Normal'], bs.inputs['Normal'])
        me.materials.append(m)
    # 1.8 m scale figure (pale, so it reads against the dark crew)
    fig = bpy.data.objects.get('ScaleFigure')
    if fig is None:
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.22, depth=1.55, location=(0, 0, 0.775))
        body = bpy.context.active_object
        bpy.ops.mesh.primitive_uv_sphere_add(segments=8, ring_count=6, radius=0.13, location=(0, 0, 1.67))
        head = bpy.context.active_object
        bpy.ops.object.select_all(action='DESELECT')
        body.select_set(True)
        head.select_set(True)
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.join()
        fig = bpy.context.active_object
        fig.name = 'ScaleFigure'
        m = bpy.data.materials.new('ScaleFigure')
        m.use_nodes = True
        m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.75, 0.75, 0.78, 1)
        fig.data.materials.append(m)
    cam = bpy.data.objects.get('PreviewCam')
    if cam is None:
        cam = bpy.data.objects.new('PreviewCam', bpy.data.cameras.new('PreviewCam'))
        scn.collection.objects.link(cam)
    scn.camera = cam
    cam.data.sensor_fit = 'VERTICAL'
    cam.data.sensor_height = 24.0
    cam.data.clip_start = 0.5
    cam.data.clip_end = 20000.0
    return sun, sea, fig, cam


def aim(cam, target, az_deg, el_deg, dist, vfov):
    a, e = math.radians(az_deg), math.radians(el_deg)
    d = Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e)))
    cam.location = Vector(target) + d * dist
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens_unit = 'FOV'
    cam.data.angle = 2 * math.atan(math.tan(vfov * 0.5) * 16 / 9) if cam.data.sensor_fit != 'VERTICAL' else vfov
    return d


def light_from_left(sun, cam_dir):
    """Sun low (20 deg) from the left of frame and a little beyond the subject (warm rim on the hulls)."""
    f = Vector((-cam_dir.x, -cam_dir.y, 0)).normalized()          # camera forward, horizontal
    left = Vector((-f.y, f.x, 0))
    s = (left * 0.85 + f * 0.45).normalized()
    el = math.radians(20.0)
    sd_ = Vector((s.x * math.cos(el), s.y * math.cos(el), math.sin(el)))
    sun.rotation_euler = sd_.to_track_quat('Z', 'Y').to_euler()


# --------------------------------------------------------------------------- scene control


def roots():
    out = {}
    for ob in bpy.data.objects:
        if ob.type == 'EMPTY' and ob.name.startswith('SM_Ship_'):
            out[ob.name] = ob
    return out


def show_only(visible):
    """visible: dict root name -> (lod, location, culture hex or None, night)"""
    for name, r in roots().items():
        v = visible.get(name)
        for ch in r.children:
            lod = int(ch.name[-1])
            on = v is not None and v[0] == lod
            ch.hide_render = not on
            ch.hide_viewport = not on
            if on:
                if v[2]:
                    ch['culture_tint'] = lin(v[2])
                else:
                    ch['culture_tint'] = tuple(r['culture_tint'])
                ch['night'] = v[3]
                ch.data.update()
        if v is not None:
            r.location = v[1]
            r.rotation_euler = (0, 0, v[4] if len(v) > 4 else 0.0)
        else:
            r.location = (0, 0, -500)


def pick_lod(root, dist, vfov):
    L = root['length']
    H = root['height']
    radius = 0.5 * math.sqrt(L * L + H * H)
    size = (2 * radius) / (2 * dist * math.tan(vfov * 0.5))
    lod = 0
    for i, s in enumerate(LOD_SCREEN):
        if size <= s:
            lod = i
    return lod, size


def render(path):
    scn = bpy.context.scene
    scn.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('wrote', os.path.relpath(path, ROOT))


VIEWS = {
    # name: (distance, azimuth, elevation, vfov)
    'close': (40.0, 52.0, 24.0, math.radians(58.0)),
    'mid': (300.0, 48.0, 34.0, VFOV_MID),
    'far': (1000.0, 48.0, 40.0, VFOV_MID),
}


def per_asset(o, sun, sea, fig, cam):
    rs = roots()
    for key, fn, display, ages, cult, kit in sd.ASSETS:
        if o['only'] and key not in o['only']:
            continue
        name = 'SM_Ship_%s_A_Intact' % key
        r = rs[name]
        for view in o['views']:
            dist, az, el, vfov = VIEWS[view]
            if view == 'close':
                tgt = Vector(r['close_target'])
                L = r['length']
                # small craft: keep 40 m but narrow the lens so the boat fills a useful part of the frame
                vfov = min(vfov, max(math.radians(14.0), 2 * math.atan(max(L, 8.0) * 0.75 / dist)))
                lod = 0
            else:
                tgt = Vector((0, 0, r['height'] * 0.25))
                lod, size = pick_lod(r, dist, vfov)
            show_only({name: (lod, (0, 0, 0), None, 0.0)})
            fig.hide_render = view != 'close'
            fig.location = Vector(r['scale_pos'])
            d = aim(cam, tgt, az, el, dist, vfov)
            light_from_left(sun, d)
            render(os.path.join(SHOTS, 'asset-ships-%s-%s.png' % (KEBAB[key], view)))
            if view != 'close':
                print('  %s %s uses LOD%d (screen size %.3f)' % (key, view, lod, size))


def extras(o, sun, sea, fig, cam):
    rs = roots()
    fig.hide_render = True
    # 1) age line-up: every ship side by side in its default culture, bows toward the camera's left
    keys = [a[0] for a in sd.ASSETS]
    vis = {}
    xs = 0.0
    for k in keys:
        bm = max(rs['SM_Ship_%s_A_Intact' % k]['length'] * 0.18, 8.0)
        vis['SM_Ship_%s_A_Intact' % k] = [0, [xs, 0, 0], None, 0.0, 0.0]
        xs += bm + 26.0
    for v in vis.values():
        v[1][0] -= xs * 0.5 - 13.0
    show_only({k: tuple(v) for k, v in vis.items()})
    d = aim(cam, (0, 0, 8), 28.0, 22.0, 260.0, math.radians(38.0))
    light_from_left(sun, d)
    render(os.path.join(SHOTS, 'asset-ships-lineup-ages.png'))
    # 2) the culture-colour check: one hull in all six colours
    culture_sheet(rs, sun, cam)
    # 3) states: Intact, Damaged, Ruin side by side
    for key in keys:
        L = rs['SM_Ship_%s_A_Intact' % key]['length']
        spread = {}
        for j, st in enumerate(sd.STATES):
            spread['SM_Ship_%s_A_%s' % (key, st)] = (0, ((j - 1) * max(L * 0.42, 12.0), 0, 0), None, 0.0, 0.0)
        show_only(spread)
        d = aim(cam, (0, 0, rs['SM_Ship_%s_A_Intact' % key]['height'] * 0.3), 22.0, 26.0, max(L * 1.55, 24.0),
                math.radians(40.0))
        light_from_left(sun, d)
        render(os.path.join(SHOTS, 'asset-ships-states-%s.png' % KEBAB[key]))
    # 4) night: the steamship and the sailing ship with lit windows
    sun.data.energy = 0.25
    sun.data.color = (0.55, 0.65, 1.0)
    bpy.context.scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.08
    show_only({'SM_Ship_Steamship_A_Intact': (0, (-30, 0, 0), None, 1.0, 0.0),
               'SM_Ship_SailShip_A_Intact': (0, (25, 10, 0), None, 1.0, 0.0),
               'SM_Ship_FastFreighter_A_Intact': (0, (95, 40, 0), None, 1.0, 0.0)})
    d = aim(cam, (20, 10, 6), 40.0, 18.0, 150.0, math.radians(40.0))
    light_from_left(sun, d)
    render(os.path.join(SHOTS, 'asset-ships-night.png'))
    sun.data.energy = 5.0
    sun.data.color = (1.0, 0.70, 0.43)
    bpy.context.scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.75


def culture_sheet(rs, sun, cam):
    """Same hull in all six culture colours: three images of two ships each would waste slots, so the culture sheet
    is rendered as six passes of one ship composited into a 3x2 grid."""
    import numpy as np
    scn = bpy.context.scene
    for key in ('SailShip', 'Galley', 'ContainerShip'):
        tiles = []
        name = 'SM_Ship_%s_A_Intact' % key
        r = rs[name]
        L = r['length']
        for i, hexc in enumerate(sd.CULTURES):
            show_only({name: (0, (0, 0, 0), hexc, 0.0, 0.0)})
            d = aim(cam, (0, 0, r['height'] * 0.3), 50.0, 30.0, max(L * 1.25, 30.0), math.radians(42.0))
            light_from_left(sun, d)
            p = os.path.join(SHOTS, '_tmp_ship_culture_%d.png' % i)
            scn.render.resolution_x, scn.render.resolution_y = 640, 360
            render(p)
            img = bpy.data.images.load(p)
            px = np.array(img.pixels[:], dtype=np.float32).reshape(360, 640, 4)
            tiles.append(px)
            bpy.data.images.remove(img)
            os.remove(p)
        scn.render.resolution_x, scn.render.resolution_y = 1280, 720
        rows = []
        for rr in range(2):
            rows.append(np.concatenate(tiles[rr * 3:(rr + 1) * 3], axis=1))
        sheet = np.concatenate(rows[::-1], axis=0)    # Blender pixel rows start at the bottom
        out = bpy.data.images.new('culture_sheet', sheet.shape[1], sheet.shape[0], alpha=True)
        out.colorspace_settings.name = 'Non-Color'
        out.pixels.foreach_set(sheet.ravel())
        out.filepath_raw = os.path.join(SHOTS, 'asset-ships-cultures-%s.png' % KEBAB[key])
        out.file_format = 'PNG'
        out.save()
        bpy.data.images.remove(out)
        print('wrote shots/asset-ships-cultures-%s.png' % KEBAB[key])


def main():
    o = args()
    sun, sea, fig, cam = rig(o)
    if not o['noper']:
        per_asset(o, sun, sea, fig, cam)
    if o['extra']:
        extras(o, sun, sea, fig, cam)


if __name__ == '__main__':
    main()
