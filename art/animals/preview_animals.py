# DEMIURGE art/animals/preview_animals.py - preview renders of the wildlife family (docs/ASSETS.md section 3).
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       --python art/animals/preview_animals.py -- --asset grazer-wildebeest --views close,40m,300m,1km,anim
#   --asset lineup renders every built asset side by side with a 1.8 m figure.
#
# Rig: sun 20 deg elevation, warm (golden hour), from the left of the camera; cool sky fill; neutral ground
# (savanna tan, sea for whale / fish / murmuration). EEVEE 1280x720. LODs are chosen by Unreal screen size
# (1.0 / 0.35 / 0.12 / 0.04) for every instance, so the far views show what the game would draw.
# Far views place a herd / flock / shoal like the concept art (docs/concept/units/wildlife-*.png).
# Output: shots/asset-animals-<key>-<view>.png
import bpy
import json
import math
import os
import sys
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SHOTS = os.path.join(ROOT, 'shots')
SRCDIR = os.path.join(HERE, 'source')
LENS = 50.0
RES = (1280, 720)
VFOV = 2 * math.atan(36.0 * RES[1] / RES[0] / 2 / LENS)
SEA = {'whale-humpback', 'fish-sardine'}


def log(*a):
    print('[preview]', *a, flush=True)


# --------------------------------------------------------------------------- scene

def newmat(name):
    m = bpy.data.materials.new(name)
    try:
        m.use_nodes = True
    except Exception:
        pass
    return m


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = RES
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    sc.render.film_transparent = False
    ee = sc.eevee
    for k, v in (('taa_render_samples', 48), ('use_raytracing', True), ('use_shadows', True), ('shadow_ray_count', 2),
                 ('shadow_step_count', 8), ('use_gtao', True), ('gtao_distance', 1.0)):
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
    w = bpy.data.worlds.new('Sky')
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes['Background']
    bg.inputs['Color'].default_value = (0.30, 0.40, 0.58, 1)
    bg.inputs['Strength'].default_value = 0.55
    return sc


def look(cam, target):
    d = Vector(target) - cam.location
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def camera(target, dist, az_deg, el_deg, clip=(0.1, 20000)):
    cd = bpy.data.cameras.new('Cam')
    cd.lens = LENS
    cd.sensor_width = 36
    cd.clip_start, cd.clip_end = clip
    cam = bpy.data.objects.new('Cam', cd)
    bpy.context.scene.collection.objects.link(cam)
    a, e = math.radians(az_deg), math.radians(el_deg)
    cam.location = Vector(target) + dist * Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))
    look(cam, target)
    bpy.context.scene.camera = cam
    return cam


def sun(cam_az_deg, el=20.0, strength=5.5, side=112):
    ld = bpy.data.lights.new('Sun', 'SUN')
    ld.energy = strength
    ld.color = (1.0, 0.80, 0.60)
    ld.angle = math.radians(1.2)
    o = bpy.data.objects.new('Sun', ld)
    bpy.context.scene.collection.objects.link(o)
    # from the left of the camera and a little behind the subject (rim light, as in the concept art)
    a = math.radians(cam_az_deg + side)
    e = math.radians(el)
    s = Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))
    o.rotation_euler = s.to_track_quat('Z', 'Y').to_euler()
    return o


def ground(kind='savanna', size=8000):
    bpy.ops.mesh.primitive_plane_add(size=size, location=(0, 0, 0))
    g = bpy.context.active_object
    g.name = 'Ground'
    m = newmat('Ground')
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    n1 = nt.nodes.new('ShaderNodeTexNoise')
    n1.inputs['Scale'].default_value = 0.004
    n1.inputs['Detail'].default_value = 8
    n2 = nt.nodes.new('ShaderNodeTexNoise')
    n2.inputs['Scale'].default_value = 0.35
    nt.links.new(tc.outputs['Object'], n1.inputs['Vector'])
    nt.links.new(tc.outputs['Object'], n2.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    cols = {'savanna': ('#7d6a48', '#a48b5c'), 'neutral': ('#77705f', '#8d8571'), 'delta': ('#3d5a26', '#5f7a35')}[kind]
    for el, h in zip(ramp.color_ramp.elements, cols):
        c = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
        el.color = [x ** 2.2 for x in c] + [1]
    mixn = nt.nodes.new('ShaderNodeMath')
    mixn.operation = 'MULTIPLY_ADD'
    nt.links.new(n1.outputs['Fac'], mixn.inputs[0])
    mixn.inputs[1].default_value = 0.7
    nt.links.new(n2.outputs['Fac'], mixn.inputs[2])
    sc2 = nt.nodes.new('ShaderNodeMath')
    sc2.operation = 'MULTIPLY'
    sc2.inputs[1].default_value = 0.6
    nt.links.new(mixn.outputs[0], sc2.inputs[0])
    nt.links.new(sc2.outputs[0], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value = 0.95
    g.data.materials.append(m)
    return g


def sea(depth=6.0, size=8000):
    # sea floor (turquoise sand with dark reef patches) and a translucent water surface
    bpy.ops.mesh.primitive_plane_add(size=size, location=(0, 0, -depth))
    f = bpy.context.active_object
    m = newmat('SeaFloor')
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    n = nt.nodes.new('ShaderNodeTexNoise')
    n.inputs['Scale'].default_value = 0.02
    n.inputs['Detail'].default_value = 6
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nt.links.new(tc.outputs['Object'], n.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.36
    ramp.color_ramp.elements[1].position = 0.44
    ramp.color_ramp.elements[0].color = (0.03, 0.07, 0.05, 1)
    ramp.color_ramp.elements[1].color = (0.55, 0.62, 0.50, 1)
    nt.links.new(n.outputs['Fac'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], bs.inputs['Base Color'])
    f.data.materials.append(m)
    bpy.ops.mesh.primitive_plane_add(size=size, location=(0, 0, 0))
    w = bpy.context.active_object
    w.name = 'Water'
    m = newmat('Water')
    bs = m.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (0.0, 0.22, 0.25, 1)
    bs.inputs['Roughness'].default_value = 0.06
    bs.inputs['Alpha'].default_value = 0.38
    try:
        m.surface_render_method = 'BLENDED'
    except Exception:
        m.blend_method = 'BLEND'
    w.data.materials.append(m)


def figure(loc):
    """Neutral 1.8 m scale figure."""
    m = newmat('Figure')
    m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.5, 0.5, 0.5, 1)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.2, depth=1.45, location=(loc[0], loc[1], 0.725))
    b = bpy.context.active_object
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.14, location=(loc[0], loc[1], 1.66))
    h = bpy.context.active_object
    for o in (b, h):
        o.data.materials.append(m)


# --------------------------------------------------------------------------- assets

def load_asset(key):
    path = os.path.join(SRCDIR, key + '.blend')
    with bpy.data.libraries.load(path, link=False) as (src, dst):
        dst.objects = [n for n in src.objects]
        dst.actions = [n for n in src.actions]
    info = json.load(open(os.path.join(SRCDIR, key + '.json')))
    lods, sk, arm = [], None, None
    for o in dst.objects:
        if o is None:
            continue
        if o.type == 'MESH' and '_LOD' in o.name:
            lods.append(o)
        elif o.type == 'MESH' and o.name.startswith('SK_'):
            sk = o
        elif o.type == 'ARMATURE':
            arm = o
    lods.sort(key=lambda o: o.name)
    for o in lods:
        o.parent = None
        o.location = (0, 0, 0)
    co = np.array([v.co[:] for v in lods[0].data.vertices])
    rad = float(np.linalg.norm(co.max(0) - co.min(0)) / 2)
    return dict(info=info, lods=[o.data for o in lods], sk=sk, arm=arm, radius=rad, objs=[o for o in dst.objects if o])


def lod_for(A, dist):
    s = A['radius'] / max(dist * math.tan(VFOV / 2), 1e-6)
    th = [1.0, 0.35, 0.12, 0.04]
    n = len(A['lods'])
    for i in range(n):
        if i + 1 >= n or s >= th[i + 1]:
            return i
    return n - 1


def place(A, loc, yaw=0.0, scale=1.0, cam=None, lod=None, pitch=0.0, roll=0.0):
    if lod is None:
        d = (Vector(loc) - cam.location).length if cam else 40.0
        lod = lod_for(dict(A, radius=A['radius'] * scale), d)
    o = bpy.data.objects.new('i', A['lods'][lod])
    o.location = loc
    o.rotation_euler = (pitch, roll, yaw)
    o.scale = (scale, scale, scale)
    bpy.context.scene.collection.objects.link(o)
    return o


def render(path):
    sc = bpy.context.scene
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    log('wrote', os.path.relpath(path, ROOT))


# --------------------------------------------------------------------------- compositions

def herd_layout(n, length, width, rng, curve=0.25, spacing=None):
    """Points along a gently curving band, heading along the band (-Y local)."""
    pts = []
    for i in range(n):
        u = rng.random()
        v = (rng.random() - 0.5) * 2
        # denser in the middle of the band
        v = math.copysign(abs(v) ** 1.4, v)
        y = (u - 0.5) * length
        x = v * width * 0.5 * (0.6 + 0.4 * math.sin(u * math.pi)) + curve * length * math.sin(u * math.pi * 1.3)
        dxdy = curve * length * math.cos(u * math.pi * 1.3) * math.pi * 1.3 / length
        yaw = math.atan2(dxdy, 1.0) + (rng.random() - 0.5) * 0.5
        pts.append((x, y, -yaw))
    return pts


def scene_land(A, key, view, rng):
    info = A['info']
    kind = info['kind']
    asset = info['asset']
    fs = 1.0
    if view in ('close', 'side'):
        h = info['height_m']
        L = max(info['size_m'])
        tgt = (0, 0, h * 0.5)
        az = -58 if view == 'close' else 0
        cam = camera(tgt, L * 1.9 + 1.0, az, 12 if view == 'close' else 4)
        sun(az, side=60)
        ground('neutral')
        place(A, (0, 0, 0), 0, 1, lod=0)
        figure((-0.3, info['size_m'][1] * 0.5 + 0.7, 0))
        return
    if view == '40m':
        cam = camera((0, 0, 0.8), 40, -58, 28)
        sun(-58, side=75)
        ground('savanna' if asset != 'Deer' else 'neutral')
        place(A, (0, 0, 0), 0, 1, cam)
        figure((2.4, 1.5, 0))
        group = {'Grazer': 7, 'Deer': 4, 'BigCat': 2}.get(asset, 3)
        for i in range(group):
            a = rng.random() * 6.28
            r = 4 + rng.random() * 7
            place(A, (r * math.cos(a), r * math.sin(a) + 3, 0), (rng.random() - 0.5) * 0.8, 0.92 + 0.16 * rng.random(), cam)
        return
    dist = 300 if view == '300m' else 1000
    el = 40 if view == '300m' else 45
    cam = camera((0, 0, 0), dist, -60, el)
    sun(-60)
    ground('savanna' if asset in ('Grazer', 'BigCat') else 'neutral')
    if asset == 'Grazer':
        n, length, width = (420, 150, 26) if dist == 300 else (1600, 420, 40)
        for x, y, yaw in herd_layout(n, length, width, rng):
            place(A, (x, y, 0), yaw, 0.9 + 0.2 * rng.random(), cam)
    elif asset == 'Deer':
        groups = 6 if dist == 300 else 14
        for gi in range(groups):
            cx, cy = (rng.random() - 0.5) * dist * 0.5, (rng.random() - 0.5) * dist * 0.35
            for i in range(3 + int(rng.random() * 4)):
                place(A, (cx + rng.normal() * 4, cy + rng.normal() * 4, 0), rng.random() * 6.28, 0.9 + 0.2 * rng.random(), cam)
    else:
        prides = 3 if dist == 300 else 6
        for gi in range(prides):
            cx, cy = (rng.random() - 0.5) * dist * 0.5, (rng.random() - 0.5) * dist * 0.35
            for i in range(2 + int(rng.random() * 3)):
                place(A, (cx + rng.normal() * 3, cy + rng.normal() * 3, 0), rng.random() * 6.28, 0.9 + 0.2 * rng.random(), cam)


def scene_bird(A, key, view, rng):
    info = A['info']
    fs = info['flock_scale']
    starling = info['variant'] == 'Starling'
    if view == 'close':
        L = max(info['size_m'])
        cam = camera((0, 0, 0), L * 1.7 + 0.2, -60, 28)
        sun(-60, side=60)
        ground('neutral')
        bpy.data.objects['Ground'].location.z = -0.6 * L
        place(A, (0, 0, 0), 0, 1, lod=0)
        return
    if view == '40m':
        cam = camera((0, 0, 12), 40, -60, 30)
        sun(-60)
        ground('delta' if not starling else 'neutral')
        figure((3, 3, 0))
        if starling:
            for i in range(60):
                p = rng.normal(size=3) * (5, 4, 2.2)
                place(A, (p[0], p[1], 12 + p[2]), -0.5 + rng.normal() * 0.4, fs, cam, pitch=rng.normal() * 0.2, roll=rng.normal() * 0.3)
        else:
            for i in range(9):
                k = i - 4
                place(A, (abs(k) * 2.4 * fs, k * 1.8 * fs, 12 + rng.normal() * 0.3), math.radians(-90), fs, cam, roll=rng.normal() * 0.08)
        return
    dist = 300 if view == '300m' else 1000
    cam = camera((0, 0, 40), dist, -60, 38 if dist == 300 else 42)
    sun(-60)
    if starling:
        sea(depth=6)
        n = 3500 if dist == 300 else 7000
        R = (60, 30, 18) if dist == 300 else (170, 75, 40)
        for i in range(n):
            # swirling murmuration: dense core, ragged edge
            u = rng.normal(size=3)
            u /= np.linalg.norm(u)
            r = rng.random() ** 0.6
            p = u * r * np.array(R)
            p[2] += 0.25 * p[0] * math.sin(p[1] / 25)
            place(A, (p[0], p[1], 45 + p[2]), math.atan2(p[0], -p[1]) + 1.4, fs, cam, pitch=rng.normal() * 0.3, roll=rng.normal() * 0.3)
    else:
        ground('delta')
        vs = 3 if dist == 300 else 7
        for v in range(vs):
            cx, cy = (rng.random() - 0.5) * dist * 0.35, (rng.random() - 0.5) * dist * 0.25
            for i in range(7):
                k = i - 3
                place(A, (cx + abs(k) * 2.4 * fs, cy + k * 1.8 * fs, 25 + rng.normal()), math.radians(-90), fs, cam)


def scene_sea(A, key, view, rng):
    info = A['info']
    whale = info['asset'] == 'Whale'
    fs = info['flock_scale']
    if view == 'close':
        L = max(info['size_m'])
        if whale:
            cam = camera((0, 0.5, -1.2), L * 1.75, -55, 22)
            sun(-55, side=60)
            ground('neutral')
            bpy.data.objects['Ground'].location.z = -4.0
            place(A, (0, 0, 0), 0, 1, lod=0)
        else:
            cam = camera((0, 0, 0), L * 1.7 + 0.1, -60, 20)
            sun(-60, side=60)
            ground('neutral')
            bpy.data.objects['Ground'].location.z = -0.15
            place(A, (0, 0, 0), 0, 1, lod=0)
        return
    dist = {'40m': 40, '300m': 300, '1km': 1000}[view]
    el = {'40m': 35, '300m': 60, '1km': 62}[view]
    cam = camera((0, 0, -1), dist, -55, el)
    sun(-55, el=20)
    sea(depth=10)
    if whale:
        place(A, (0, 0, -0.45), 0.4, 1.0, cam)
        place(A, (6.5, 7, -0.9), 0.5, 0.45, cam)
        if dist >= 300:
            place(A, (dist * 0.18, -dist * 0.1, -0.5), 2.4, 1.0, cam)
    else:
        balls = [(0, 0)] if dist < 1000 else [(0, 0), (140, 90)]
        n = 700 if dist == 40 else 1400
        for bx, by in balls:
            Rb = 7.0 * fs / 3 if dist == 40 else 12.0
            for i in range(n):
                a = rng.random() * 6.283
                r = Rb * (0.35 + 0.65 * rng.random() ** 0.7)
                z = -1.2 - abs(rng.normal()) * 1.6
                # front is local -Y, so yaw a + pi points the fish along the circle's tangent (-sin a, cos a): a
                # milling bait ball as in wildlife-sea.png (the earlier a + pi/2 pointed every fish radially outward)
                place(A, (bx + r * math.cos(a), by + r * math.sin(a), z), a + math.pi + rng.normal() * 0.2, fs, cam,
                      pitch=rng.normal() * 0.15)


def scene_anim(A, key):
    """Four frames of the first action (and the second, if any) on the skinned mesh, stitched 2x2."""
    sk, arm = A['sk'], A['arm']
    for o in (sk, arm):
        bpy.context.scene.collection.objects.link(o)
    info = A['info']
    L = max(info['size_m'])
    walker = info['kind'] in ('quad', 'cat')
    tgt = (0, 0, info['height_m'] * 0.5 if walker else 0)
    caz = 0 if walker else -60
    camera(tgt, L * 1.7 + 0.3, caz, 6 if walker else 30)
    sun(caz, side=60)
    ground('neutral')
    if info['kind'] in ('whale',):
        bpy.data.objects['Ground'].location.z = -4.5
    elif not walker:
        bpy.data.objects['Ground'].location.z = -0.5 * L
    acts = [a['name'] for a in info['animations']]
    frames = []
    for an in acts[:2]:
        act = bpy.data.actions[an]
        arm.animation_data.action = act
        nf = [a['frames'] for a in info['animations'] if a['name'] == an][0]
        for k in range(4 if len(acts) == 1 else 2):
            frames.append((act.name, int(round(k * nf / (4 if len(acts) == 1 else 2)))))
    sc = bpy.context.scene
    sc.render.resolution_x, sc.render.resolution_y = 640, 360
    tiles = []
    tmp = os.path.join(bpy.app.tempdir, 'animtile.png')
    for an, f in frames:
        arm.animation_data.action = bpy.data.actions[an]
        sc.frame_set(f)
        sc.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp)
        a = np.array(img.pixels[:], np.float32).reshape(360, 640, 4)
        bpy.data.images.remove(img)
        tiles.append(a)
    while len(tiles) < 4:
        tiles.append(np.zeros_like(tiles[0]))
    top = np.concatenate([tiles[2], tiles[3]], 1)
    bot = np.concatenate([tiles[0], tiles[1]], 1)
    full = np.concatenate([bot, top], 0)
    out = bpy.data.images.new('anim', 1280, 720, alpha=False)
    out.pixels.foreach_set(full.ravel())
    out.filepath_raw = os.path.join(SHOTS, 'asset-animals-%s-anim.png' % key)
    out.file_format = 'PNG'
    out.save()
    log('wrote anim', key)


def lineup(keys):
    reset()
    assets = [load_asset(k) for k in keys]
    land = [A for A in assets if A['info']['kind'] in ('quad', 'cat')]
    x = 0.0
    figure((-0.8, 0, 0))
    for A in land:
        w = A['info']['size_m'][1]          # length (the animals are turned side-on)
        place(A, (x + w * 0.5 + 0.3, 0, 0), math.radians(-90), 1, lod=0)
        x += w + 0.5
    birds = [A for A in assets if A['info']['kind'] in ('bird', 'fish')]
    bx = 0.6
    for A in birds:
        place(A, (bx, -2.2, 1.9), math.radians(-90), 1, lod=0)
        bx += max(A['info']['size_m'][:2]) + 0.6
    cx = (x - 1.3) * 0.5
    camera((cx, 0, 0.9), (x + 3.0) * 1.45, -90, 6)
    sun(-90)
    ground('neutral')
    render(os.path.join(SHOTS, 'asset-animals-lineup-close.png'))


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts = {}
    for i in range(0, len(argv) - 1, 2):
        opts[argv[i].lstrip('-')] = argv[i + 1]
    keys = opts.get('asset', 'all').split(',')
    views = opts.get('views', 'close,40m,300m,1km,anim').split(',')
    built = sorted(f[:-5] for f in os.listdir(SRCDIR) if f.endswith('.json') and os.path.exists(os.path.join(SRCDIR, f[:-5] + '.blend')))
    if keys == ['all']:
        keys = built
    if keys == ['lineup']:
        lineup(built)
        return
    for key in keys:
        for view in views:
            reset()
            A = load_asset(key)
            rng = np.random.default_rng(7)
            if view == 'anim':
                scene_anim(A, key)
                continue
            kind = A['info']['kind']
            if kind in ('quad', 'cat'):
                scene_land(A, key, view, rng)
            elif kind == 'bird':
                scene_bird(A, key, view, rng)
            else:
                scene_sea(A, key, view, rng)
            render(os.path.join(SHOTS, 'asset-animals-%s-%s.png' % (key, view)))


if __name__ == '__main__':
    main()
