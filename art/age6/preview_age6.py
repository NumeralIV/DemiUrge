# DEMIURGE art - Age6 preview renders (EEVEE, 1280x720). Run through the render slot tool:
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       art/age6/source/age6.blend --python art/age6/preview_age6.py -- [--shots towers,launch] [--fast]
#
# Rig per docs/ASSETS.md: sun 20 deg elevation, warm; cool sky fill; neutral ground; a 1.8 m figure.
# Views: 40 m three-quarter, 300 m oblique, 1 km oblique. LODs are picked by screen size, as in game.
# Output: shots/asset-age6-<asset>-<view>.png
import os
import sys
import math
import random

import bpy
from mathutils import Vector, Euler

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SHOTS = os.path.join(ROOT, 'shots')
SCREEN = [1.0, 0.35, 0.12, 0.04]
LENS = 35.0


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'shots': None, 'fast': False}
    for i, x in enumerate(a):
        if x == '--shots':
            o['shots'] = set(a[i + 1].split(','))
        if x == '--fast':
            o['fast'] = True
    return o


# --------------------------------------------------------------------------- scene rig


def rig(fast=False):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.resolution_percentage = 100
    ee = sc.eevee
    for k, v in (('taa_render_samples', 16 if fast else 64), ('use_raytracing', True), ('use_shadows', True),
                 ('shadow_ray_count', 2), ('shadow_step_count', 8), ('use_fast_gi', True),
                 ('fast_gi_distance', 30.0), ('volumetric_end', 6000.0), ('volumetric_tile_size', '8'),
                 ('use_volumetric_shadows', False)):
        try:
            setattr(ee, k, v)
        except (AttributeError, TypeError):
            pass
    try:
        ee.ray_tracing_method = 'SCREEN'
    except (AttributeError, TypeError):
        pass
    vs = sc.view_settings
    vs.view_transform = 'AgX'
    for look in ('AgX - Medium High Contrast', 'AgX - Base Contrast', 'None'):
        try:
            vs.look = look
            break
        except TypeError:
            continue
    vs.exposure = 0.0
    # world: cool sky gradient + light haze
    w = bpy.data.worlds.new('Age6Preview')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    N, L = nt.nodes, nt.links
    out = N.new('ShaderNodeOutputWorld')
    bg = N.new('ShaderNodeBackground')
    tc = N.new('ShaderNodeTexCoord')
    sep = N.new('ShaderNodeSeparateXYZ')
    ramp = N.new('ShaderNodeValToRGB')
    L.new(tc.outputs['Generated'], sep.inputs[0])
    L.new(sep.outputs['Z'], ramp.inputs['Fac'])
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = (0.60, 0.62, 0.62, 1)
    ramp.color_ramp.elements[1].position = 0.65
    ramp.color_ramp.elements[1].color = (0.20, 0.33, 0.55, 1)
    e = ramp.color_ramp.elements.new(0.5)
    e.color = (0.78, 0.70, 0.60, 1)
    L.new(ramp.outputs['Color'], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = 0.9
    L.new(bg.outputs['Background'], out.inputs['Surface'])
    # aerial haze: compositor mix toward a warm grey by the mist pass (a world volume renders black in EEVEE here)
    w.mist_settings.start = 120.0
    w.mist_settings.depth = 4200.0
    w.mist_settings.falloff = 'LINEAR'
    sc.view_layers[0].use_pass_mist = True
    ct = bpy.data.node_groups.new('Age6Haze', 'CompositorNodeTree')
    ct.interface.new_socket(name='Image', in_out='OUTPUT', socket_type='NodeSocketColor')
    rl = ct.nodes.new('CompositorNodeRLayers')
    go = ct.nodes.new('NodeGroupOutput')
    mf = ct.nodes.new('ShaderNodeMath')
    mf.operation = 'MULTIPLY'
    mf.inputs[1].default_value = 0.55
    ct.links.new(rl.outputs['Mist'], mf.inputs[0])
    mx = ct.nodes.new('ShaderNodeMix')
    mx.data_type = 'RGBA'
    mx.blend_type = 'MIX'
    sk = {s.identifier: s for s in mx.inputs}
    ct.links.new(mf.outputs[0], sk['Factor_Float'])
    ct.links.new(rl.outputs['Image'], sk['A_Color'])
    sk['B_Color'].default_value = (0.70, 0.66, 0.60, 1.0)
    ct.links.new([o for o in mx.outputs if o.identifier == 'Result_Color'][0], go.inputs[0])
    sc.compositing_node_group = ct
    sc.render.use_compositing = True
    # sun
    sd = bpy.data.lights.new('Sun', 'SUN')
    sd.energy = 4.2
    sd.color = (1.0, 0.80, 0.60)
    sd.angle = math.radians(1.2)
    sun = bpy.data.objects.new('Sun', sd)
    sc.collection.objects.link(sun)
    # ground
    me = bpy.data.meshes.new('Ground')
    s = 6000.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new('Ground', me)
    gm = bpy.data.materials.new('Ground')
    gm.use_nodes = True
    b = gm.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (0.20, 0.19, 0.17, 1)
    b.inputs['Roughness'].default_value = 0.95
    me.materials.append(gm)
    sc.collection.objects.link(g)
    # camera
    cd = bpy.data.cameras.new('Cam')
    cd.lens = LENS
    cd.clip_start = 0.5
    cd.clip_end = 20000
    cam = bpy.data.objects.new('Cam', cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    # 1.8 m figure
    fm = bpy.data.meshes.new('Figure')
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=0.24, radius2=0.2, depth=1.55)
    bmesh.ops.translate(bm, vec=(0, 0, 0.775), verts=bm.verts)
    hd = bmesh.ops.create_icosphere(bm, subdivisions=2, radius=0.13)
    bmesh.ops.translate(bm, vec=(0, 0, 1.67), verts=hd['verts'])
    bm.to_mesh(fm)
    bm.free()
    fmat = bpy.data.materials.new('Figure')
    fmat.use_nodes = True
    fmat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.6, 0.08, 0.05, 1)
    fm.materials.append(fmat)
    fig = bpy.data.objects.new('Figure', fm)
    sc.collection.objects.link(fig)
    return sun, cam, fig


def sun_from(sun, az_deg, el_deg=20.0):
    """az: compass direction the light comes FROM (0 = +Y, 90 = +X)."""
    az, el = math.radians(az_deg), math.radians(el_deg)
    d = Vector((math.sin(az) * math.cos(el), math.cos(az) * math.cos(el), math.sin(el)))
    sun.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()


def aim(cam, pos, target):
    cam.location = pos
    cam.rotation_euler = (Vector(target) - Vector(pos)).to_track_quat('-Z', 'Y').to_euler()


# --------------------------------------------------------------------------- placement


SOURCE = {}


def source_objs():
    for ob in bpy.data.objects:
        n = ob.name
        if n.startswith('SM_Age6_') and '_LOD' in n:
            base, lod = n.rsplit('_LOD', 1)
            SOURCE.setdefault(base, {})[int(lod)] = ob
            ob.hide_render = True
            ob.hide_viewport = True


PLACED = []


def place(mesh, x, y, rot=0.0, culture=None, z=0.0, sz=1.0):
    src = SOURCE.get(mesh)
    if not src:
        print('missing', mesh)
        return None
    obs = []
    for lod, so in src.items():
        ob = bpy.data.objects.new('%s@%d_%d' % (mesh, len(PLACED), lod), so.data)
        ob.location = (x, y, z)
        ob.rotation_euler = (0, 0, math.radians(rot))
        ob.scale = (1, 1, sz)
        bpy.context.scene.collection.objects.link(ob)
        if culture and culture != 'Default':
            for i, slot in enumerate(ob.material_slots):
                base = slot.material.name if slot.material else ''
                mi = 'MI_Age6_%s_%s' % (base[len('M_Age6_'):], culture)
                if mi in bpy.data.materials:
                    slot.link = 'OBJECT'
                    slot.material = bpy.data.materials[mi]
        obs.append((lod, ob))
    PLACED.append((mesh, obs))
    return obs


def clear_placed():
    for mesh, obs in PLACED:
        for lod, ob in obs:
            bpy.data.objects.remove(ob, do_unlink=True)
    PLACED.clear()


def choose_lods(cam):
    vf = 2 * math.atan(36.0 * 9 / 16 / 2 / cam.data.lens)
    for mesh, obs in PLACED:
        ob0 = dict(obs)[0]
        bb = [ob0.matrix_world @ Vector(c) for c in ob0.bound_box]
        c = sum(bb, Vector()) / 8
        r = max((p - c).length for p in bb)
        d = max(1.0, (c - cam.location).length)
        s = 2 * r / (2 * d * math.tan(vf / 2))
        pick = 3
        for k in range(4):
            if s >= SCREEN[k] * 0.999 or k == 3:
                pick = k
                break
        for lod, ob in obs:
            ob.hide_render = lod != pick


def night(on):
    for m in bpy.data.materials:
        if m.node_tree and 'NightGlow' in m.node_tree.nodes:
            m.node_tree.nodes['NightGlow'].outputs[0].default_value = 1.6 if on else 0.0


# --------------------------------------------------------------------------- shots


def views_for(centre, radius, front_az=200.0, h_target=None):
    """Three views around `centre` (x, y): 40 m three-quarter, 300 m and 1 km obliques.
    front_az: compass direction (deg) from the centre toward the camera."""
    cx, cy = centre
    ht = h_target if h_target is not None else 10.0
    out = {}
    a = math.radians(front_az)
    dvec = Vector((math.sin(a), math.cos(a), 0))
    p = Vector((cx, cy, 0)) + dvec * (radius + 40.0)
    p.z = 14.0
    out['40m'] = (p, Vector((cx, cy, ht)))
    for name, dist, el in (('300m', 300.0, 35.0), ('1km', 1000.0, 40.0)):
        e = math.radians(el)
        p = Vector((cx, cy, 0)) + dvec * dist * math.cos(e)
        p.z = dist * math.sin(e)
        out[name] = (p, Vector((cx, cy, ht * 0.5)))
    return out


def render(path, cam, sun, pos, target, sun_az):
    aim(cam, pos, target)
    sun_from(sun, sun_az)
    choose_lods(cam)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('wrote', path)


def shot_group(name, layout, centre, radius, fig_at, front_az=200.0, h40=None, views=('40m', '300m', '1km'), sun_rel=-115.0):
    sun, cam, fig = RIG
    clear_placed()
    for item in layout:
        place(*item)
    fig.location = (fig_at[0], fig_at[1], 0)
    v = views_for(centre, radius, front_az, h40)
    for k in views:
        pos, tgt = v[k]
        render(os.path.join(SHOTS, 'asset-age6-%s-%s.png' % (name, k)), cam, sun, pos, tgt, front_az + sun_rel)


def S(asset, state='Intact'):
    return 'SM_Age6_%s_%s' % (asset, state)


def shot_towers():
    xs = [-150, -75, 0, 75, 150]
    lay = [(S('ArcologyTower_' + v), x, 0, 0) for v, x in zip('ABCDE', xs)]
    sun, cam, fig = RIG
    shot_group('towers', lay, (0, 0), 160, (-150 - 10, -22), h40=60.0, views=('300m', '1km'))
    # 40 m close-up of the hero tower's base terraces
    clear_placed()
    place(S('ArcologyTower_E'), 0, 0, 0)
    fig.location = (-6, -30, 0)
    a = math.radians(215)
    pos = Vector((math.sin(a) * 64, math.cos(a) * 64, 16.0))
    render(os.path.join(SHOTS, 'asset-age6-towers-40m.png'), cam, sun, pos, Vector((0, 0, 30)), 215 - 115)


def shot_blocks():
    lay = [(S('GreenBlock_A'), -75, 0, 0), (S('GreenBlock_B'), -25, 0, 0), (S('GreenBlock_C'), 25, 0, 0), (S('GreenBlock_D'), 80, 0, 0)]
    shot_group('greenblocks', lay, (0, 0), 100, (-30, -18), h40=10.0, views=('300m', '1km'))
    sun, cam, fig = RIG
    clear_placed()
    place(S('GreenBlock_B'), 0, 0, 0)
    place(S('GreenBlock_C'), 48, 6, 0)
    fig.location = (-4, -18, 0)
    a = math.radians(210)
    pos = Vector((math.sin(a) * 58, math.cos(a) * 58, 14.0))
    render(os.path.join(SHOTS, 'asset-age6-greenblocks-40m.png'), cam, sun, pos, Vector((8, 0, 9)), 210 - 115)


def shot_maglev():
    lay = [(S('MaglevStation_A'), 0, 0, 0)]
    for k in range(1, 4):
        lay.append((S('MaglevTrack_Straight'), 45 + 40 * k - 20, 0, 0))
        lay.append((S('MaglevTrack_Straight'), -45 - 40 * k + 20, 0, 0))
    # a curve after the straights on +X
    R = 160.0
    th = 40.0 / R
    lay.append((S('MaglevTrack_Curve'), 165 + R * math.sin(th / 2), -R * (1 - math.cos(th / 2)), 0))
    shot_group('maglev', lay, (0, 0), 70, (-6, -32), h40=9.0)


def shot_solar():
    lay = [(S('SolarArray_A'), -26, 0, 0), (S('SolarArray_B'), 26, 0, 0), (S('SolarArray_A'), -26, 34, 0), (S('SolarArray_B'), 26, 34, 0)]
    shot_group('solar', lay, (0, 10), 40, (-8, -20), h40=2.0, views=('40m', '300m'))
    lay = []
    for i in range(4):
        for j in range(3):
            lay.append((S('SolarArray_A' if (i + j) % 3 else 'SolarArray_B'), -75 + 50 * i, -35 + 34 * j, 0))
    shot_group('solar', lay, (0, 0), 120, (0, -60), views=('1km',))


def hall_pos():
    """The manifest's suggested hall placement (doors at the end of three crawlerway segments)."""
    try:
        import json
        with open(os.path.join(HERE, 'manifest.json'), encoding='utf-8') as f:
            hp = json.load(f)['launch_site_layout']['SM_Age6_AssemblyHall']
        return hp[0], hp[1], hp[3]
    except Exception:
        return 0.0, -289.0, 180.0


def launch_layout(state='Intact', rocket=True):
    lay = [(S('LaunchPad', state), 0, 0, 0)]
    if rocket and state in ('Intact', 'Damaged'):
        lay.append(('SM_Age6_Rocket_Intact', 0, 0, 0, None, 9.0))
    hx, hy, hr = hall_pos()
    lay.append((S('AssemblyHall', state), hx, hy, hr))
    lay.append((S('ControlBunker', state), -150, -170, 160))
    for k in range(3):
        lay.append(('SM_Age6_Crawlerway_Intact', 0, -92 - 60 * k, 90))
    return lay


def shot_launch():
    sun, cam, fig = RIG
    shot_group('launch', launch_layout(), (0, -140), 200, (-20, -64), views=('1km',), front_az=255)
    a = math.radians(300)
    e = math.radians(35)
    c = Vector((0, hall_pos()[1], 0))
    pos = c + Vector((math.sin(a) * 300 * math.cos(e), math.cos(a) * 300 * math.cos(e), 300 * math.sin(e)))
    render(os.path.join(SHOTS, 'asset-age6-launch-hall-300m.png'), cam, sun, pos, c + Vector((0, 0, 40)), 300 - 115)
    clear_placed()
    for item in launch_layout():
        place(*item)
    fig.location = (-12, -66, 0)
    a = math.radians(225)
    pos = Vector((math.sin(a) * 90, math.cos(a) * 90, 20.0))
    render(os.path.join(SHOTS, 'asset-age6-launch-40m.png'), cam, sun, pos, Vector((0, 0, 32)), 225 - 115)
    # the pad as the hero at the concept's 300 m, 40 deg view (landmarks/building-launch.png)
    # concept framing (verification 2026-11-19): camera on the fuel-sphere side (-X) so the rocket and
    # gantry stand on the left, the hall sits to the right behind the crawlerway, spheres in the foreground;
    # low sun from the front left as in the concept
    a = math.radians(262)
    e = math.radians(36)
    c = Vector((10, -150, 0))
    pos = c + Vector((math.sin(a) * 470 * math.cos(e), math.cos(a) * 470 * math.cos(e), 470 * math.sin(e)))
    render(os.path.join(SHOTS, 'asset-age6-launch-pad-300m.png'), cam, sun, pos, c + Vector((0, 0, 28)), 262 - 230)


def shot_states():
    sun, cam, fig = RIG
    clear_placed()
    st = ['Intact', 'Construction', 'Damaged', 'Ruin']
    for i, s in enumerate(st):
        place(S('ArcologyTower_B', s), -105 + 70 * i, 40, 0)
        place(S('GreenBlock_A', s), -105 + 70 * i, -30, 0)
        place(S('MaglevTrack_Straight', s), -105 + 70 * i, -70, 0)
    fig.location = (0, -90, 0)
    a = math.radians(200)
    e = math.radians(30)
    pos = Vector((math.sin(a) * 380 * math.cos(e), math.cos(a) * 380 * math.cos(e), 380 * math.sin(e)))
    render(os.path.join(SHOTS, 'asset-age6-states-300m.png'), cam, sun, pos, Vector((0, 0, 30)), 200 - 115)
    clear_placed()
    for i, s in enumerate(st):
        place(S('LaunchPad', s), -225 + 150 * i, 0, 0)
        place(S('AssemblyHall', s), -225 + 150 * i, -150, 0) if False else None
    pos = Vector((math.sin(a) * 700 * math.cos(e), math.cos(a) * 700 * math.cos(e), 700 * math.sin(e)))
    render(os.path.join(SHOTS, 'asset-age6-states-launch-300m.png'), cam, sun, pos, Vector((0, 0, 20)), 200 - 115)


def shot_cultures():
    sun, cam, fig = RIG
    clear_placed()
    cs = ['Default', 'CliffTerrace', 'LagoonWeaver', 'RingBuilder']
    for i, c in enumerate(cs):
        x = -120 + 80 * i
        place(S('ArcologyTower_A'), x, 30, 0, c)
        place(S('GreenBlock_C'), x, -30, 0, c)
    a = math.radians(200)
    e = math.radians(28)
    pos = Vector((math.sin(a) * 360 * math.cos(e), math.cos(a) * 360 * math.cos(e), 360 * math.sin(e)))
    render(os.path.join(SHOTS, 'asset-age6-cultures-300m.png'), cam, sun, pos, Vector((0, 0, 40)), 200 - 115)


def district_layout(seed=6):
    rng = random.Random(seed)
    lay = []
    towers = ['A', 'B', 'C', 'D', 'E']
    blocks = ['A', 'B', 'C', 'D']
    # 7 x 6 grid of 130 x 100 m superblocks; towers dense in the core, garden blocks outward
    for i in range(-3, 4):
        for j in range(-3, 3):
            cx, cy = i * 130.0, j * 100.0 + 50
            r = math.hypot(i * 1.3, j + 0.5)
            if abs(i) <= 0 and j in (-1, 0):
                continue          # central park (old temple site)
            if r < 2.2:
                lay.append((S('ArcologyTower_' + rng.choice(towers)), cx - 30, cy + rng.uniform(-10, 10), rng.choice((0, 90))))
                lay.append((S('GreenBlock_' + rng.choice(blocks)), cx + 32, cy + rng.uniform(-14, 14), rng.choice((0, 180))))
            elif r < 3.6:
                if rng.random() < 0.35:
                    lay.append((S('ArcologyTower_' + rng.choice('ABCD')), cx - 30, cy, rng.choice((0, 90))))
                else:
                    lay.append((S('GreenBlock_' + rng.choice(blocks)), cx - 32, cy - 18, rng.choice((0, 180))))
                lay.append((S('GreenBlock_' + rng.choice(blocks)), cx + 32, cy + 16, rng.choice((0, 180))))
            else:
                lay.append((S('GreenBlock_' + rng.choice(blocks)), cx, cy, rng.choice((0, 180))))
    # maglev line through the city on y = -205 with the station
    lay.append((S('MaglevStation_A'), 0, -205, 0))
    for k in range(1, 9):
        lay.append((S('MaglevTrack_Straight'), 45 + 40 * k - 20, -205, 0))
        lay.append((S('MaglevTrack_Straight'), -45 - 40 * k + 20, -205, 0))
    for i in range(3):
        for j in range(2):
            lay.append((S('SolarArray_A'), 470 + 50 * i, 120 + 34 * j, 0))
    return lay


def shot_district():
    sun, cam, fig = RIG
    clear_placed()
    for item in district_layout():
        place(*item)
    # parks and streets as simple tinted planes so the district reads like a city
    a = math.radians(200)
    e = math.radians(40)
    pos = Vector((math.sin(a) * 1000 * math.cos(e), math.cos(a) * 1000 * math.cos(e), 1000 * math.sin(e)))
    render(os.path.join(SHOTS, 'asset-age6-district-1km.png'), cam, sun, pos, Vector((0, 0, 0)), 200 - 115)
    night(True)
    sun.data.energy = 0.0
    bpy.context.scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.015
    render(os.path.join(SHOTS, 'asset-age6-district-night-1km.png'), cam, sun, pos, Vector((0, 0, 0)), 200 - 115)
    night(False)
    sun.data.energy = 4.2
    bpy.context.scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.9


ALL = {
    'towers': shot_towers, 'greenblocks': shot_blocks, 'maglev': shot_maglev, 'solar': shot_solar,
    'launch': shot_launch, 'states': shot_states, 'cultures': shot_cultures, 'district': shot_district,
}

o = args()
source_objs()
RIG = rig(o['fast'])
os.makedirs(SHOTS, exist_ok=True)
for k, f in ALL.items():
    if o['shots'] and k not in o['shots']:
        continue
    f()
print('PREVIEW DONE')
