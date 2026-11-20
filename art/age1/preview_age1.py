# DEMIURGE art - Age1 preview renders (EEVEE, 1280 x 720, golden-hour sun at 20 degrees, cool sky fill).
# Run through the render slot tool (docs/ASSETS.md section 3):
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       art/age1/source/age1.blend --python art/age1/preview_age1.py -- houses,granary [--views 40m,300m,1km] [--samples 48]
# Writes shots/asset-age1-<set>-<view>.png
import os
import sys
import math
import random
import bpy
from mathutils import Vector, Euler

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path[:0] = [HERE]
SHOTS = os.path.join(ROOT, 'shots')

ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
SETS = ARGS[0].split(',') if ARGS and not ARGS[0].startswith('--') else ['houses']
VIEWS = ARGS[ARGS.index('--views') + 1].split(',') if '--views' in ARGS else None
SAMPLES = int(ARGS[ARGS.index('--samples') + 1]) if '--samples' in ARGS else 48
SUFFIX = ARGS[ARGS.index('--suffix') + 1] if '--suffix' in ARGS else ''

SUN_ELEV = 20.0
CAM_AZ = 35.0          # camera sits front-right of the subject; the sun comes from the camera's left


# --------------------------------------------------------------------------- scene

def setup_scene():
    sc = bpy.context.scene
    for c in sc.collection.children:
        if c.name == 'Age1':
            for vl in sc.view_layers:
                lc = vl.layer_collection.children.get('Age1')
                if lc:
                    lc.exclude = True
    pv = bpy.data.collections.new('Preview')
    sc.collection.children.link(pv)
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    e = sc.eevee
    e.taa_render_samples = SAMPLES
    try:
        e.use_raytracing = True
        e.ray_tracing_method = 'SCREEN'
    except Exception:
        pass
    e.use_shadows = True
    e.shadow_ray_count = 2
    e.shadow_step_count = 8
    try:
        e.use_fast_gi = True
        e.fast_gi_distance = 6.0
    except Exception:
        pass
    vs = sc.view_settings
    vs.view_transform = 'AgX'
    for look in ('AgX - Medium High Contrast', 'Medium High Contrast', 'AgX - Base Contrast'):
        try:
            vs.look = look
            break
        except Exception:
            continue
    vs.exposure = 0.0
    sc.render.film_transparent = False
    # world: sky gradient (warm haze at the horizon, cool zenith) + a little atmosphere
    w = bpy.data.worlds.new('PreviewSky')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    nt.links.new(tc.outputs['Generated'], sep.inputs[0])
    nrm = nt.nodes.new('ShaderNodeVectorMath')
    nrm.operation = 'NORMALIZE'
    nt.links.new(tc.outputs['Generated'], nrm.inputs[0])
    nt.links.new(nrm.outputs[0], sep.inputs[0])
    nt.links.new(sep.outputs['Z'], ramp.inputs[0])
    cr = ramp.color_ramp
    cr.elements[0].position = 0.0
    cr.elements[0].color = (0.62, 0.52, 0.42, 1)
    cr.elements[1].position = 0.45
    cr.elements[1].color = (0.20, 0.32, 0.52, 1)
    el = cr.elements.new(0.08)
    el.color = (0.48, 0.48, 0.50, 1)
    nt.links.new(ramp.outputs[0], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = 0.55
    nt.links.new(bg.outputs[0], out.inputs['Surface'])
    # sun
    sun = bpy.data.lights.new('Sun', 'SUN')
    sun.energy = 8.0
    sun.color = (1.0, 0.74, 0.47)
    sun.angle = math.radians(0.8)
    so = bpy.data.objects.new('Sun', sun)
    pv.objects.link(so)
    az = math.radians(CAM_AZ)
    fwd = Vector((-math.sin(az), math.cos(az), 0))
    left = Vector((-fwd.y, fwd.x, 0))
    sdir = (left * 0.92 + fwd * 0.38).normalized()
    sdir = Vector((sdir.x * math.cos(math.radians(SUN_ELEV)), sdir.y * math.cos(math.radians(SUN_ELEV)), math.sin(math.radians(SUN_ELEV))))
    so.rotation_euler = (-sdir).to_track_quat('-Z', 'Y').to_euler()
    return pv


def ground_material(name='PV_Ground', grass=True):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    n1 = nt.nodes.new('ShaderNodeTexNoise')
    n1.inputs['Scale'].default_value = 0.02
    n1.inputs['Detail'].default_value = 6.0
    n2 = nt.nodes.new('ShaderNodeTexNoise')
    n2.inputs['Scale'].default_value = 0.6
    n2.inputs['Detail'].default_value = 8.0
    nt.links.new(tc.outputs['Object'], n1.inputs['Vector'])
    nt.links.new(tc.outputs['Object'], n2.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    cr = ramp.color_ramp
    if grass:
        cr.elements[0].color = (0.05, 0.085, 0.025, 1)
        cr.elements[1].color = (0.15, 0.15, 0.06, 1)
    else:
        cr.elements[0].color = (0.10, 0.09, 0.07, 1)
        cr.elements[1].color = (0.16, 0.14, 0.11, 1)
    mix = nt.nodes.new('ShaderNodeMath')
    mix.operation = 'MULTIPLY_ADD'
    nt.links.new(n1.outputs['Fac'], mix.inputs[0])
    mix.inputs[1].default_value = 0.7
    nt.links.new(n2.outputs['Fac'], mix.inputs[2])
    mix2 = nt.nodes.new('ShaderNodeMath')
    mix2.operation = 'MULTIPLY'
    mix2.inputs[1].default_value = 0.75
    nt.links.new(mix.outputs[0], mix2.inputs[0])
    nt.links.new(mix2.outputs[0], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value = 0.95
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.3
    nt.links.new(n2.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs[0], bs.inputs['Normal'])
    return m


def water_material():
    m = bpy.data.materials.new('PV_Water')
    m.use_nodes = True
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (0.015, 0.04, 0.045, 1)
    bs.inputs['Roughness'].default_value = 0.06
    tc = nt.nodes.new('ShaderNodeTexCoord')
    n = nt.nodes.new('ShaderNodeTexNoise')
    n.inputs['Scale'].default_value = 0.8
    nt.links.new(tc.outputs['Object'], n.inputs['Vector'])
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.08
    nt.links.new(n.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs[0], bs.inputs['Normal'])
    return m


def plane(pv, name, x, y, z, sx, sy, mat):
    me = bpy.data.meshes.new(name)
    me.from_pydata([(-sx, -sy, 0), (sx, -sy, 0), (sx, sy, 0), (-sx, sy, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new(name, me)
    ob.location = (x, y, z)
    ob.data.materials.append(mat)
    pv.objects.link(ob)
    return ob


def figure(pv, x, y):
    """1.8 m scale figure."""
    m = bpy.data.materials.get('PV_Fig')
    if m is None:
        m = bpy.data.materials.new('PV_Fig')
        m.use_nodes = True
        m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.35, 0.06, 0.03, 1)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.22, depth=1.5, location=(x, y, 0.75))
    b = bpy.context.active_object
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.15, location=(x, y, 1.65))
    hd = bpy.context.active_object
    for o in (b, hd):
        o.data.materials.append(m)
        for c in o.users_collection:
            c.objects.unlink(o)
        pv.objects.link(o)


def place(pv, mesh, x, y, rot=0.0, lod=0, culture=None, crop=None, burnt=False, z=0.0):
    me = bpy.data.meshes.get('%s_LOD%d' % (mesh, lod)) or bpy.data.meshes.get('%s_LOD%d' % (mesh, 0))
    if me is None:
        print('missing', mesh)
        return None
    ob = bpy.data.objects.new('pv_%s' % mesh, me)
    ob.location = (x, y, z)
    ob.rotation_euler = (0, 0, rot)
    pv.objects.link(ob)
    swap = {}
    if culture and culture != 'Valley':
        swap = {'M_Age1_%s' % k: 'MI_Age1_%s_%s' % (k, culture) for k in ('Mudbrick', 'Render', 'Thatch', 'Trim')}
    if burnt:
        swap = {'M_Age1_%s' % k: 'MI_Age1_%s_Burnt' % k for k in ('Mudbrick', 'Render', 'Thatch', 'Trim', 'Earth', 'Soil')}
    if crop:
        swap['M_Age1_Crop'] = 'MI_Age1_Crop_%s' % crop
    if swap:
        for i, slot in enumerate(ob.material_slots):
            nm = slot.material.name if slot.material else None
            if nm in swap and bpy.data.materials.get(swap[nm]):
                slot.link = 'OBJECT'
                slot.material = bpy.data.materials[swap[nm]]
    return ob


def camera(target, dist, elev, az=CAM_AZ, lens=50.0):
    sc = bpy.context.scene
    cam = sc.camera
    if cam is None:
        cd = bpy.data.cameras.new('Cam')
        cam = bpy.data.objects.new('Cam', cd)
        sc.collection.objects.link(cam)
        sc.camera = cam
    a, e = math.radians(az), math.radians(elev)
    t = Vector(target)
    pos = t + Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e))) * dist
    cam.location = pos
    cam.rotation_euler = (t - pos).to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = lens
    cam.data.clip_start = 0.5
    cam.data.clip_end = 20000
    return cam


def render(name):
    path = os.path.join(SHOTS, 'asset-age1-%s%s.png' % (name, SUFFIX))
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('[shot]', path, flush=True)


# --------------------------------------------------------------------------- preview sets
# Each returns (target, size) and the standard views are 40 m three-quarter, 300 m and 1 km oblique.

def set_houses(pv):
    for k, (v, x, y) in enumerate((('A', -6.5, 6.5), ('B', 6.5, 6.5), ('C', -6.5, -6.5), ('D', 6.5, -8.0))):
        place(pv, 'SM_Age1_House_%s_Intact' % v, x, y)
    figure(pv, 0.0, -11.5)
    return (0, -1.0, 2.0), 30


def set_house_states(pv):
    for k, st in enumerate(('Intact', 'Construction', 'Damaged', 'Ruin')):
        x = -21 + k * 14
        place(pv, 'SM_Age1_House_B_%s' % st, x, 3)
        place(pv, 'SM_Age1_House_A_%s' % st, x, -10)
    figure(pv, 0, -16)
    return (0, -3, 1.5), 60


def set_granary(pv):
    place(pv, 'SM_Age1_Granary_A_Intact', 0, 0)
    figure(pv, 6.0, -16.0)
    return (0, 0, 3.0), 38


def set_granary_states(pv):
    for k, st in enumerate(('Intact', 'Construction', 'Damaged', 'Ruin')):
        place(pv, 'SM_Age1_Granary_A_%s' % st, -48 + k * 32, 0)
    return (0, 0, 3.0), 140


def set_temple(pv):
    place(pv, 'SM_Age1_Temple_A_Intact', 0, 0)
    place(pv, 'SM_Age1_TempleCourt_A_Intact', 0, 0)
    figure(pv, 3.0, -27.0)
    return (0, -2, 5.0), 75


def set_temple_states(pv):
    for k, st in enumerate(('Intact', 'Construction', 'Damaged', 'Ruin')):
        place(pv, 'SM_Age1_Temple_A_%s' % st, -81 + k * 54, 0)
        place(pv, 'SM_Age1_TempleCourt_A_%s' % st, -81 + k * 54, 0) if False else None
    return (0, 0, 4.0), 230


def set_walls(pv):
    # an arc of rampart with a gate, a palisade run behind it
    place(pv, 'SM_Age1_Gate_A_Intact', 0, 0)
    xs = [(-13.6, 0.0, 0.0), (-23.4, 0.9, -0.18), (-33.0, 2.6, -0.18)]
    for (x, y, r) in xs:
        place(pv, 'SM_Age1_Rampart_Straight_Intact', x, y, r)
    # verifier: the End used to sit against the gate's own flank, facing it, which read as a broken wall;
    # now one straight segment continues the run and the End closes it
    place(pv, 'SM_Age1_Rampart_Straight_Intact', 15.0, 0.0, 0.0)
    place(pv, 'SM_Age1_Rampart_End_Intact', 25.0, 0.0, 0.0)
    for k in range(3):
        place(pv, 'SM_Age1_Palisade_Straight_Intact', 14 + k * 8.0, 12.0, 0.0)
    figure(pv, 2.0, -6.0)
    return (-2, 2, 2.5), 50


def set_wall_states(pv):
    for k, st in enumerate(('Intact', 'Construction', 'Damaged', 'Ruin')):
        y = 0
        x0 = -45 + k * 30
        place(pv, 'SM_Age1_Gate_A_%s' % st, x0, 0)
        place(pv, 'SM_Age1_Rampart_Straight_%s' % st, x0, 14)
        place(pv, 'SM_Age1_Palisade_Straight_%s' % st, x0, 24)
    return (0, 8, 2.0), 120


def set_fields(pv):
    for (v, x, y, crop) in (('Ripe', -13, 9, 'Ripe'), ('Growing', 13, 9, 'Green'), ('Ploughed', -13, -9, None), ('Stubble', 13, -9, 'Stubble')):
        place(pv, 'SM_Age1_Field_%s' % v, x, y, crop=crop)
    figure(pv, 0, -19)
    return (0, 0, 0.5), 52


def set_landing(pv):
    wm = water_material()
    plane(pv, 'Water', 0, -40, 0.0, 120, 45, wm)
    place(pv, 'SM_Age1_Landing_A_Intact', 0, 0)
    place(pv, 'SM_Age1_ReedBoat_A', -3.4, -6.0, math.radians(185))   # boats now face -Y: +90 deg vs before
    place(pv, 'SM_Age1_ReedBoat_B', 3.3, -9.2, math.radians(170))
    place(pv, 'SM_Age1_ReedBoat_A', 9.0, -3.0, math.radians(110), z=0.0)
    figure(pv, 0.0, 3.0)
    return (0, -1, 1.0), 30


def set_landing_states(pv):
    wm = water_material()
    plane(pv, 'Water', 0, -40, 0.0, 120, 45, wm)
    for k, st in enumerate(('Intact', 'Construction', 'Damaged', 'Ruin')):
        place(pv, 'SM_Age1_Landing_A_%s' % st, -27 + k * 18, 0)
    return (0, -2, 1.0), 80


def set_cultures(pv):
    for r, cul in enumerate(('Valley', 'CliffTerrace', 'LagoonWeaver', 'RingBuilder')):
        y = 21 - r * 14
        for c, v in enumerate('ABCD'):
            x = -21 + c * 14
            place(pv, 'SM_Age1_House_%s_Intact' % v, x, y, culture=cul)
            # ornaments on gable houses
            if v in ('A', 'D') and cul in ('LagoonWeaver', 'RingBuilder'):
                me = bpy.data.meshes.get('SM_Age1_House_%s_Intact_LOD0' % v)
                orn = 'SM_Age1_Orn_GableHorns' if cul == 'LagoonWeaver' else 'SM_Age1_Orn_Finial'
                import json
                man = json.load(open(os.path.join(HERE, 'manifest.json')))
                sk = man['meshes']['SM_Age1_House_%s_Intact' % v].get('sockets', {})
                for key, p in sk.items():
                    place(pv, orn, x + p[0], y + p[1], z=p[2] - 0.05, culture=cul)
    return (0, 0, 2.0), 70


VILLAGE_SEED = 7


def set_village(pv):
    rng = random.Random(VILLAGE_SEED)
    wm = water_material()
    # river along the right side
    plane(pv, 'River', 175, 0, 0.01, 26, 600, wm)
    # rampart ring (ellipse) with the gate toward the river
    ax, ay = 92.0, 72.0
    # trodden earth inside the ring (the concept's village floor is bare earth and lanes, not lawn)
    em = bpy.data.materials.get('PV_Earth')
    if em is None:
        em = ground_material('PV_Earth', grass=False)
        cr = [n for n in em.node_tree.nodes if n.type == 'VALTORGB'][0].color_ramp
        cr.elements[0].color = (0.16, 0.10, 0.055, 1)
        cr.elements[1].color = (0.27, 0.18, 0.10, 1)
    me = bpy.data.meshes.new('pv_floor')
    ring = [(math.cos(2 * math.pi * k / 64) * (ax - 2.5), math.sin(2 * math.pi * k / 64) * (ay - 2.5), 0.0) for k in range(64)]
    me.from_pydata([(0, 0, 0)] + ring, [], [(0, 1 + k, 1 + (k + 1) % 64) for k in range(64)])
    fl = bpy.data.objects.new('pv_floor', me)
    fl.location = (0, 0, 0.03)      # above the ground plane by enough to avoid z-fighting at 1 km
    fl.data.materials.append(em)
    pv.objects.link(fl)
    gate_a = math.radians(-28)
    n = 52
    pts = [(math.cos(2 * math.pi * k / n) * ax, math.sin(2 * math.pi * k / n) * ay) for k in range(n)]
    for k in range(n):
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
        am = (a0 + a1) / 2
        d = math.atan2(math.sin(am - gate_a), math.cos(am - gate_a))
        if abs(d) < 0.11:
            continue
        p0, p1 = Vector((*pts[k], 0)), Vector((*pts[(k + 1) % n], 0))
        mid = (p0 + p1) / 2
        rot = math.atan2(p1.y - p0.y, p1.x - p0.x)
        L = (p1 - p0).length
        ob = place(pv, 'SM_Age1_Rampart_Straight_Intact', mid.x, mid.y, rot)
        ob.scale = (L / 10.0 * 1.04, 1.0, 1.0)
    gp = Vector((math.cos(gate_a) * ax, math.sin(gate_a) * ay, 0))
    tang = Vector((-math.sin(gate_a) * ax, math.cos(gate_a) * ay, 0))
    place(pv, 'SM_Age1_Gate_A_Intact', gp.x, gp.y, math.atan2(tang.y, tang.x))
    # temple at the centre, granary near the wall, houses in between
    place(pv, 'SM_Age1_Temple_A_Intact', 0, 0, 0.0)
    place(pv, 'SM_Age1_Granary_A_Intact', -48, 28, math.radians(-30))
    houses = []
    blocked = [(0, -5, 30), (-48, 28, 22)]
    tries = 0
    while len(houses) < 40 and tries < 4000:
        tries += 1
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.3, 0.86)
        x, y = math.cos(a) * ax * r, math.sin(a) * ay * r
        if any(math.hypot(x - bx, y - by) < br for bx, by, br in blocked):
            continue
        if any(math.hypot(x - hx, y - hy) < 13.0 for hx, hy, _ in houses):
            continue
        # lane from the gate to the centre stays clear
        t = max(0.0, min(1.0, (Vector((x, y, 0)).dot(gp)) / gp.length_squared))
        if (Vector((x, y, 0)) - gp * t).length < 7.0:
            continue
        houses.append((x, y, a))
    for (x, y, a) in houses:
        v = rng.choice('AABBCD')
        rot = math.atan2(-y, -x) + math.pi / 2 + rng.uniform(-0.35, 0.35)   # front toward the centre
        st = 'Intact' if rng.random() > 0.08 else rng.choice(('Construction', 'Damaged'))
        place(pv, 'SM_Age1_House_%s_%s' % (v, st), x, y, rot)
    # fields outside the ring
    for (x, y, rot, v, crop, s) in ((-150, 40, 0.3, 'Ripe', 'Ripe', 2.2), (-140, -55, -0.2, 'Ripe', 'Ripe', 2.6), (-30, -125, 0.1, 'Growing', 'Green', 2.4),
                                    (40, -120, -0.15, 'Ripe', 'Ripe', 2.0), (-60, 120, 0.25, 'Ploughed', None, 2.4), (40, 118, -0.1, 'Stubble', 'Stubble', 2.0),
                                    (-200, -10, 0.5, 'Growing', 'Green', 2.0)):
        ob = place(pv, 'SM_Age1_Field_%s' % v, x, y, rot, crop=crop)
        ob.scale = (s, s * 0.9, 1)
    # landing on the river
    place(pv, 'SM_Age1_Landing_A_Intact', 150, -40, math.radians(-90))
    place(pv, 'SM_Age1_ReedBoat_A', 157, -43, math.radians(95))
    place(pv, 'SM_Age1_ReedBoat_B', 160, -36, math.radians(80))
    return (10, -5, 0.0), 260


def set_lods(pv):
    """Verifier sheet: each column is one LOD (0..3), rows are the main meshes."""
    rows = (('SM_Age1_House_B_Intact', 0), ('SM_Age1_House_C_Intact', 1), ('SM_Age1_Granary_A_Intact', 2), ('SM_Age1_Temple_A_Intact', 3), ('SM_Age1_Gate_A_Intact', 4))
    ys = (-30, -12, 12, 50, 82)
    for (mesh, r) in rows:
        for lod in range(4):
            if bpy.data.meshes.get('%s_LOD%d' % (mesh, lod)):
                place(pv, mesh, -60 + lod * 40, ys[r], lod=lod)
    return (0, 22, 3.0), 120


SETS_FN = {'lods': set_lods, 'houses': set_houses, 'house-states': set_house_states, 'granary': set_granary, 'granary-states': set_granary_states,
           'temple': set_temple, 'temple-states': set_temple_states, 'walls': set_walls, 'wall-states': set_wall_states,
           'fields': set_fields, 'landing': set_landing, 'landing-states': set_landing_states, 'cultures': set_cultures,
           'village': set_village}

STATE_SETS = {'lods', 'house-states', 'granary-states', 'temple-states', 'wall-states', 'landing-states', 'cultures'}


def main():
    os.makedirs(SHOTS, exist_ok=True)
    for name in SETS:
        # fresh scene state per set
        sc = bpy.context.scene
        pv = bpy.data.collections.get('Preview')
        if pv is None:
            pv = setup_scene()
            gm = ground_material()
            plane(pv, 'Ground', 0, 0, -0.02, 4000, 4000, gm)
        else:
            for o in list(pv.objects):
                if o.name.startswith('pv_') or o.name.startswith('Cylinder') or o.name.startswith('Sphere') or o.name in ('Water', 'River'):
                    bpy.data.objects.remove(o)
        target, size = SETS_FN[name](pv)
        views = VIEWS or (['near'] if name in STATE_SETS else ['40m', '300m', '1km'])
        for v in views:
            if v == '40m':
                camera(target, max(40.0, size * 1.45), 30.0)
            elif v == 'near':
                camera(target, size * 1.45, 34.0)
            elif v == '300m':
                camera(target, 300.0, 40.0)
            elif v == '1km':
                camera(target, 1000.0, 40.0)
            elif v == 'top':
                camera(target, size * 2.0, 70.0)
            render('%s-%s' % (name, v))


main()
