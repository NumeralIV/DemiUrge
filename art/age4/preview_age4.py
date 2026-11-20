# DEMIURGE art - Age4 preview renders (docs/ASSETS.md section 3). Run through the render slot tool:
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#          --python art/age4/preview_age4.py -- [--only Terrace_A,...] [--views close,40m,300m,1km,states,cultures,lods] [--district]
#
# Golden-hour rig: warm sun at 20 deg elevation from the left, cool sky fill, neutral ground, EEVEE 1280x720.
# Output: shots/asset-age4-<asset>-<view>.png (asset lower-case with hyphens).
import math
import os
import sys

import bpy
from mathutils import Vector, Euler

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, 'lib')
if LIB not in sys.path:
    sys.path.insert(0, LIB)
import a4mat   # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'shots')
BLEND = os.path.join(HERE, 'blend')


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'only': None, 'views': ['40m', '300m', '1km'], 'district': False, 'night': False}
    i = 0
    while i < len(a):
        if a[i] == '--only':
            o['only'] = a[i + 1].split(',')
            i += 1
        elif a[i] == '--views':
            o['views'] = a[i + 1].split(',')
            i += 1
        elif a[i] == '--district':
            o['district'] = True
        i += 1
    return o


def slug(key):
    return key.lower().replace('_', '-')


# --------------------------------------------------------------------------- rig

def rig(scene, night=False):
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    ee = scene.eevee
    ee.taa_render_samples = 48
    try:
        ee.use_raytracing = True
        ee.ray_tracing_method = 'SCREEN'
        ee.use_fast_gi = True
        ee.fast_gi_distance = 6.0
    except Exception:
        pass
    try:
        ee.use_shadows = True
        ee.shadow_ray_count = 2
        ee.shadow_step_count = 8
        ee.shadow_resolution_scale = 1.0
    except Exception:
        pass
    vs = scene.view_settings
    vs.view_transform = 'AgX'
    try:
        vs.look = 'AgX - Medium High Contrast'
    except Exception:
        pass
    vs.exposure = 0.0 if not night else 1.0
    # world: cool sky gradient (fill) + horizon warmth
    w = bpy.data.worlds.get('A4Sky') or bpy.data.worlds.new('A4Sky')
    scene.world = w
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
    nt.links.new(sep.outputs['Z'], ramp.inputs[0])
    cr = ramp.color_ramp
    if not night:
        cr.elements[0].position = 0.5
        cr.elements[0].color = (0.95, 0.72, 0.52, 1)
        cr.elements[1].position = 0.75
        cr.elements[1].color = (0.30, 0.44, 0.68, 1)
        bg.inputs['Strength'].default_value = 0.65
    else:
        cr.elements[0].position = 0.5
        cr.elements[0].color = (0.05, 0.06, 0.12, 1)
        cr.elements[1].position = 0.7
        cr.elements[1].color = (0.01, 0.015, 0.04, 1)
        bg.inputs['Strength'].default_value = 0.5
    nt.links.new(ramp.outputs[0], bg.inputs['Color'])
    nt.links.new(bg.outputs[0], out.inputs['Surface'])
    # sun
    sun_d = bpy.data.lights.get('A4Sun') or bpy.data.lights.new('A4Sun', 'SUN')
    sun_d.energy = 6.0 if not night else 0.05
    sun_d.color = (1.0, 0.74, 0.5) if not night else (0.6, 0.7, 1.0)
    sun_d.angle = math.radians(1.2)
    sun = bpy.data.objects.get('A4Sun') or bpy.data.objects.new('A4Sun', sun_d)
    if sun.name not in scene.collection.objects:
        scene.collection.objects.link(sun)
    # ground
    if 'A4Ground' not in bpy.data.objects:
        me = bpy.data.meshes.new('A4Ground')
        s = 6000.0
        me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
        g = bpy.data.objects.new('A4Ground', me)
        scene.collection.objects.link(g)
        m = bpy.data.materials.new('A4GroundMat')
        m.use_nodes = True
        bs = m.node_tree.nodes.get('Principled BSDF')
        tcn = m.node_tree.nodes.new('ShaderNodeTexCoord')
        nz = m.node_tree.nodes.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = 0.02
        nz.inputs['Detail'].default_value = 8.0
        m.node_tree.links.new(tcn.outputs['Object'], nz.inputs['Vector'])
        cr2 = m.node_tree.nodes.new('ShaderNodeValToRGB')
        cr2.color_ramp.elements[0].color = (0.10, 0.10, 0.085, 1)
        cr2.color_ramp.elements[1].color = (0.17, 0.165, 0.14, 1)
        m.node_tree.links.new(nz.outputs[0], cr2.inputs[0])
        m.node_tree.links.new(cr2.outputs[0], bs.inputs['Base Color'])
        bs.inputs['Roughness'].default_value = 0.95
        me.materials.append(m)
    cam_d = bpy.data.cameras.get('A4Cam') or bpy.data.cameras.new('A4Cam')
    cam_d.sensor_width = 36.0
    cam_d.clip_start = 0.5
    cam_d.clip_end = 8000.0
    cam = bpy.data.objects.get('A4Cam') or bpy.data.objects.new('A4Cam', cam_d)
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    scene.camera = cam
    return cam, sun


def look(cam, target, az_deg, el_deg, dist, lens):
    az, el = math.radians(az_deg), math.radians(el_deg)
    t = Vector(target)
    pos = t + Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el))) * dist
    cam.location = pos
    d = (t - pos).normalized()
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = lens


def aim_sun(sun, cam_az, el=20.0, rel=-72.0):
    """Sun on the left of frame and a little behind the subject (golden-hour side/back light)."""
    az = math.radians(cam_az + rel)
    el = math.radians(el)
    d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))   # toward the sun
    sun.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()


def river(scene, z):
    if 'A4River' in bpy.data.objects:
        bpy.data.objects['A4River'].location.z = z
        return
    me = bpy.data.meshes.new('A4River')
    s = 6000.0
    me.from_pydata([(-s, -40, 0), (s, -40, 0), (s, 40, 0), (-s, 40, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new('A4River', me)
    ob.location.z = z
    m = bpy.data.materials.new('A4RiverMat')
    m.use_nodes = True
    bs = m.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (0.03, 0.045, 0.05, 1)
    bs.inputs['Roughness'].default_value = 0.08
    me.materials.append(m)
    scene.collection.objects.link(ob)


def figure(scene):
    if 'A4Figure' in bpy.data.objects:
        return bpy.data.objects['A4Figure']
    import bmesh
    me = bpy.data.meshes.new('A4Figure')
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=0.24, radius2=0.2, depth=1.45)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.725))
    sph = bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=0.13)
    bmesh.ops.translate(bm, verts=sph['verts'], vec=(0, 0, 1.67))
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new('A4Figure', me)
    m = bpy.data.materials.new('A4FigureMat')
    m.use_nodes = True
    m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.35, 0.12, 0.08, 1)
    me.materials.append(m)
    scene.collection.objects.link(ob)
    return ob


# --------------------------------------------------------------------------- scene helpers

def state_groups():
    """{state: (group empty, [lod objects])} from the loaded asset .blend."""
    out = {}
    for ob in bpy.data.objects:
        if ob.type == 'EMPTY' and ob.get('fbx_type') == 'LodGroup':
            st = ob.name.rsplit('_', 1)[1]
            lods = sorted([c for c in ob.children if c.type == 'MESH'], key=lambda o: o.name)
            out[st] = (ob, lods)
    return out


def show_only(objs):
    keep = set(o.name for o in objs)
    for ob in bpy.data.objects:
        if ob.type == 'MESH' and ob.name not in ('A4Ground', 'A4Figure') and not ob.name.startswith('A4'):
            ob.hide_render = ob.name not in keep


def bounds(objs):
    xs, ys, zs = [], [], []
    for ob in objs:
        for c in ob.bound_box:
            w = ob.matrix_world @ Vector(c)
            xs.append(w.x)
            ys.append(w.y)
            zs.append(w.z)
    return Vector((min(xs), min(ys), min(zs))), Vector((max(xs), max(ys), max(zs)))


def render(scene, path):
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('A4SHOT', path)


def fit_dist(lo, hi, lens, el, margin=1.25):
    r = (hi - lo).length * 0.5
    hfov = 2 * math.atan(18.0 / lens)
    vfov = 2 * math.atan(math.tan(hfov / 2) * 720 / 1280)
    return r * margin / math.sin(min(hfov, vfov) / 2)


def fit_frame(lo, hi, target, az_deg, el_deg, lens, fill=0.86):
    """Distance at which the 8 corners of the box lo..hi just fill `fill` of the 16:9 frame when the camera
    looks at `target` from (az, el). The bounding-sphere fit left assets at a third of the frame width, too
    small to judge close-up detail (verifier fix, 2026-11-19)."""
    az, el = math.radians(az_deg), math.radians(el_deg)
    t = Vector(target)
    back = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
    fwd = -back
    right = fwd.cross(Vector((0, 0, 1))).normalized()
    up = right.cross(fwd)
    tx = 18.0 / lens
    ty = tx * 720 / 1280
    corners = [Vector((x, y, z)) for x in (lo.x, hi.x) for y in (lo.y, hi.y) for z in (lo.z, hi.z)]

    def fits(d):
        pos = t + back * d
        for c in corners:
            v = c - pos
            z = v.dot(fwd)
            if z <= 0.5:
                return False
            if abs(v.dot(right) / (z * tx)) > fill or abs(v.dot(up) / (z * ty)) > fill:
                return False
        return True
    a, b = 1.0, 20000.0
    for _ in range(60):
        m = 0.5 * (a + b)
        if fits(m):
            b = m
        else:
            a = m
    return b


def apply_culture(objs, culture):
    for ob in objs:
        me = ob.data
        for i, m in enumerate(list(me.materials)):
            base = m.name.split('_night')[0]
            for master in a4mat.MASTERS:
                if base == master or base.startswith(a4mat.mi_name(master, '')):
                    me.materials[i] = a4mat.make_material(master, culture) if culture != 'Base' else a4mat.make_material(master)


def asset_views(key, views):
    path = os.path.join(BLEND, key + '.blend')
    bpy.ops.wm.open_mainfile(filepath=path)
    scene = bpy.context.scene
    cam, sun = rig(scene)
    groups = state_groups()
    fig = figure(scene)
    intact = groups['Intact'][1][0]
    for g, lods in groups.values():
        g.location = (0, 0, 0)
    lo, hi = bounds([intact])
    centre = (lo + hi) * 0.5
    size = hi - lo
    fig.location = (lo.x - 1.5, lo.y - 1.5, 0)
    ground = bpy.data.objects['A4Ground']
    ground.location.z = 0.0
    if lo.z < -2.0:     # bridges: drop the ground to the pier feet and add a river
        ground.location.z = lo.z
        fig.location = (lo.x - 1.5, lo.y - 1.5, lo.z)
        river(scene, lo.z + 3.0)
    fig.hide_render = False
    for view in views:
        out = os.path.join(SHOTS, 'asset-age4-%s-%s.png' % (slug(key), view))
        if view.startswith('st-'):
            st = view[3:]
            if st not in groups:
                continue
            show_only([groups[st][1][0]])
            lens = 35.0
            tgt = (centre.x, centre.y, (lo.z + hi.z) * 0.5)
            d = max(40.0, fit_frame(lo, hi, tgt, 35, 32, lens))
            look(cam, tgt, 35, 32, d, lens)
            aim_sun(sun, 35)
            render(scene, out)
        elif view == 'close':
            # verifier addition: camera 40 m from the front-left of the street facade, figure at the door line
            show_only([intact])
            zt = min(size.z * 0.3, 5.0) if lo.z > -2.0 else 1.0
            tx = lo.x + min(size.x * 0.3, 14.0)
            fig.location = (tx - 2.0, lo.y - 1.2, 0.0) if lo.z > -2.0 else (tx, 0.0, 0.25)   # bridges: on the deck
            look(cam, (tx, lo.y, zt), 35, 16, 40.0, 35.0)
            aim_sun(sun, 35)
            render(scene, out)
            fig.location = (lo.x - 1.5, lo.y - 1.5, lo.z if lo.z < -2.0 else 0)
        elif view in ('40m', '300m', '1km'):
            show_only([intact])
            if view == '40m':
                lens = 35.0
                tgt = (centre.x, centre.y, (lo.z + hi.z) * 0.5)
                d = max(40.0, fit_frame(lo, hi, tgt, 35, 22, lens))
                look(cam, tgt, 35, 22, d, lens)
                aim_sun(sun, 35)
            else:
                d = 300.0 if view == '300m' else 1000.0
                look(cam, (centre.x, centre.y, size.z * 0.2), 30, 40, d, 50.0)
                aim_sun(sun, 30)
            render(scene, out)
        elif view == 'states':
            objs = []
            x = 0.0
            order = [s for s in ('Intact', 'Construction', 'Damaged', 'Ruin') if s in groups]
            step = size.x + max(8.0, size.x * 0.25)
            for i, st in enumerate(order):
                g, lods = groups[st]
                g.location = ((i - (len(order) - 1) / 2) * step, 0, 0)
                objs.append(lods[0])
            bpy.context.view_layer.update()
            show_only(objs)
            l2, h2 = bounds(objs)
            c2 = (l2 + h2) * 0.5
            lens = 35.0
            look(cam, (c2.x, c2.y, (l2.z + h2.z) * 0.5), 20, 30, fit_frame(l2, h2, (c2.x, c2.y, (l2.z + h2.z) * 0.5), 20, 30, lens, 0.92), lens)
            aim_sun(sun, 20)
            render(scene, out)
            for g, lods in groups.values():
                g.location = (0, 0, 0)
        elif view == 'lods':
            g, lods = groups['Intact']
            objs = []
            step = size.x + max(6.0, size.x * 0.2)
            copies = []
            for i, ob in enumerate(lods):
                c = ob.copy()
                c.parent = None
                c.matrix_world = ob.matrix_world.copy()
                c.location.x += (i - 1.5) * step
                scene.collection.objects.link(c)
                copies.append(c)
            bpy.context.view_layer.update()
            show_only(copies)
            l2, h2 = bounds(copies)
            c2 = (l2 + h2) * 0.5
            look(cam, (c2.x, c2.y, (l2.z + h2.z) * 0.5), 20, 30, fit_frame(l2, h2, (c2.x, c2.y, (l2.z + h2.z) * 0.5), 20, 30, 35.0, 0.92), 35.0)
            aim_sun(sun, 20)
            render(scene, out)
            for c in copies:
                bpy.data.objects.remove(c)
        elif view == 'cultures':
            copies = []
            step = size.x + max(6.0, size.x * 0.2)
            for i, cul in enumerate(a4mat.CULTURES):
                c = intact.copy()
                c.data = intact.data.copy()
                c.parent = None
                c.matrix_world = intact.matrix_world.copy()
                c.location.x += (i - 1.5) * step
                scene.collection.objects.link(c)
                apply_culture([c], cul)
                copies.append(c)
                orn = bpy.data.objects.get('SM_Age4_Ornament_%s_LOD0' % cul)
            bpy.context.view_layer.update()
            show_only(copies)
            l2, h2 = bounds(copies)
            c2 = (l2 + h2) * 0.5
            look(cam, (c2.x, c2.y, (l2.z + h2.z) * 0.5), 20, 32, fit_frame(l2, h2, (c2.x, c2.y, (l2.z + h2.z) * 0.5), 20, 32, 35.0, 0.92), 35.0)
            aim_sun(sun, 20)
            render(scene, out)
            for c in copies:
                bpy.data.objects.remove(c)
    fig.hide_render = True


def main():
    o = args()
    os.makedirs(SHOTS, exist_ok=True)
    keys = o['only']
    if keys is None:
        keys = sorted(f[:-6] for f in os.listdir(BLEND) if f.endswith('.blend'))
    for key in keys:
        if key != 'none' and os.path.exists(os.path.join(BLEND, key + '.blend')):
            asset_views(key, o['views'])
    if o['district']:
        import a4district
        a4district.render_district(SHOTS, rig, look, aim_sun, render)


main()
