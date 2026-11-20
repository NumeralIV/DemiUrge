# DEMIURGE art/age5 - preview renders (EEVEE, 1280x720) per docs/ASSETS.md section 3.
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/age5/preview_age5.py -- --set apartment-slab [--views near,mid,far] [--night]
#
# Golden-hour sun at 20 degrees (warm) from the camera's left, cool sky fill, neutral ground, light haze; views:
# near (three-quarter, framed, at least 40 m), close (exactly 40 m from the
# scale figure at the scene's front-left corner), mid (300 m oblique, 40 degrees down), far (1 km oblique).
# Output: shots/asset-age5-<set>-<view>.png
import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import build_age5 as B  # noqa: E402

SHOTS = os.path.join(ROOT, 'shots')


def find(asset):
    for a in B.ASSETS:
        if a['asset'] == asset:
            return a
    raise KeyError(asset)


def place(asset, variant, kind='Intact', x=0.0, y=0.0, yaw=0.0, culture='Neutral', night=0.0, lod=0, crown=False):
    a = find(asset)
    P = dict(dict(a['variants'])[variant])
    mats = B.materials(culture, night)
    coll = bpy.context.scene.collection
    root, objs, tris, sockets, S = B.build_object(a, variant, P, kind, mats, coll, lods=(lod,))
    root.location = (x, y, 0.0)
    root.rotation_euler = (0, 0, yaw)
    if crown and culture in B.CROWN_FOR:
        cs = [s for s in sockets if s['name'].startswith('SOCKET_Crown')]
        if cs:
            ca = find('Crown')
            Pc = dict(dict(ca['variants'])[culture])
            r2, o2, _, _, _ = B.build_object(ca, culture, Pc, 'Intact', mats, coll, lods=(lod,))
            p = cs[0]['pos_m']
            r2.parent = root
            r2.location = (p[0], p[1], p[2])
    return root, objs


def rig(night=False):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x = 1280
    sc.render.resolution_y = 720
    sc.render.film_transparent = False
    ee = sc.eevee
    for attr, val in (('taa_render_samples', 48), ('use_shadows', True), ('use_raytracing', True),
                      ('volumetric_end', 4000.0), ('volumetric_tile_size', '8'), ('use_volumetric_shadows', True)):
        try:
            setattr(ee, attr, val)
        except Exception:
            pass
    try:
        sc.view_settings.view_transform = 'AgX'
        sc.view_settings.look = 'AgX - Medium High Contrast'
    except Exception:
        pass
    sc.view_settings.exposure = -0.1 if not night else -0.6
    w = bpy.data.worlds.new('age5-preview')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    # physical sky (no sun disc: the sun lamp gives the direct light), a cool fill with a warm horizon
    global SKY
    if night:
        bg.inputs['Color'].default_value = (0.004, 0.006, 0.014, 1)
        bg.inputs['Strength'].default_value = 1.0
        SKY = None
    else:
        sky = nt.nodes.new('ShaderNodeTexSky')
        sky.sky_type = 'MULTIPLE_SCATTERING'
        sky.sun_disc = False
        sky.sun_elevation = math.radians(20.0)
        sky.air_density = 1.0
        sky.aerosol_density = 2.0
        nt.links.new(sky.outputs['Color'], bg.inputs['Color'])
        bg.inputs['Strength'].default_value = 0.075
        SKY = sky
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])
    # haze: a local volume box (a world volume would be infinite and black out the sky in EEVEE)
    if '--novol' not in sys.argv:
        hz = 1600.0
        vme = bpy.data.meshes.new('haze')
        S_ = 7000.0
        vv = [(x, y, z) for z in (-1.0, hz) for y in (-S_, S_) for x in (-S_, S_)]
        vme.from_pydata(vv, [], [[0, 1, 3, 2], [4, 6, 7, 5], [0, 4, 5, 1], [2, 3, 7, 6], [0, 2, 6, 4], [1, 5, 7, 3]])
        vob = bpy.data.objects.new('haze', vme)
        sc.collection.objects.link(vob)
        vm = bpy.data.materials.new('haze')
        vnt = vm.node_tree
        vnt.nodes.clear()
        vo = vnt.nodes.new('ShaderNodeOutputMaterial')
        vs = vnt.nodes.new('ShaderNodeVolumeScatter')
        vs.inputs['Density'].default_value = 0.00011 if not night else 0.00006
        vs.inputs['Color'].default_value = (0.78, 0.84, 0.96, 1)
        vnt.links.new(vs.outputs['Volume'], vo.inputs['Volume'])
        vme.materials.append(vm)
    # sun
    sd = bpy.data.lights.new('sun', 'SUN')
    sd.energy = 6.5 if not night else 0.05
    sd.color = (1.0, 0.70, 0.42) if not night else (0.6, 0.7, 1.0)
    sd.angle = math.radians(0.6)
    sun = bpy.data.objects.new('sun', sd)
    sc.collection.objects.link(sun)
    # ground
    me = bpy.data.meshes.new('ground')
    s = 6000.0
    me.from_pydata([(-s, -s, -0.02), (s, -s, -0.02), (s, s, -0.02), (-s, s, -0.02)], [], [[0, 1, 2, 3]])
    g = bpy.data.objects.new('ground', me)
    sc.collection.objects.link(g)
    gm = bpy.data.materials.new('ground')
    gm.use_nodes = True
    p = gm.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (0.07, 0.068, 0.055, 1) if not night else (0.015, 0.015, 0.015, 1)
    p.inputs['Roughness'].default_value = 0.95
    me.materials.append(gm)
    return sun


def figure(x, y):
    """1.8 m scale figure."""
    bpy.ops.mesh.primitive_cylinder_add(radius=0.22, depth=1.45, location=(x, y, 0.725))
    body = bpy.context.active_object
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.14, location=(x, y, 1.66))
    head = bpy.context.active_object
    m = bpy.data.materials.new('figure')
    m.use_nodes = True
    m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.6, 0.06, 0.04, 1)
    for o in (body, head):
        o.data.materials.append(m)


def scene_bounds():
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in bpy.context.scene.objects:
        if o.type != 'MESH' or o.name in ('ground', 'haze') or o.hide_render:
            continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector((min(lo.x, w.x), min(lo.y, w.y), min(lo.z, w.z)))
            hi = Vector((max(hi.x, w.x), max(hi.y, w.y), max(hi.z, w.z)))
    return lo, hi


def camera(view, lo, hi, sun, az_deg=32.0):
    sc = bpy.context.scene
    cd = bpy.data.cameras.new('cam')
    cam = bpy.data.objects.new('cam', cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    centre = (lo + hi) * 0.5
    size = hi - lo
    az = math.radians(az_deg)
    if view == 'top':
        cd.type = 'ORTHO'
        cd.ortho_scale = max(size.x, size.y * 16 / 9) * 1.1
        cam.location = Vector((centre.x, centre.y, hi.z + 500.0))
        cam.rotation_euler = (0, 0, 0)
        cd.clip_end = 8000.0
        saz = az - math.radians(100.0)
        sel = math.radians(40.0)
        sdir = Vector((math.sin(saz) * math.cos(sel), -math.cos(saz) * math.cos(sel), math.sin(sel)))
        sun.rotation_euler = (-sdir).to_track_quat('-Z', 'Y').to_euler()
        return cam
    if view == 'close':
        # exactly 40 m from the scale figure's corner of the scene (ASSETS.md close-up distance)
        el = math.radians(20.0)
        cd.lens = 35.0
        target = Vector((FIG[0] + 8.0, FIG[1] + 8.0, 7.0))
        dist = 40.0
    elif view == 'near':
        el = math.radians(16.0)
        cd.lens = 28.0
        target = Vector((centre.x, centre.y, lo.z + size.z * 0.42))
        rad = 0.5 * size.length
        vfov = 2 * math.atan(0.5 * 36.0 * 9 / 16 / cd.lens)
        hfov = 2 * math.atan(0.5 * 36.0 / cd.lens)
        dist = max(40.0, 1.08 * max(0.5 * size.z / math.tan(vfov / 2) + 0.5 * max(size.x, size.y),
                                    0.5 * math.hypot(size.x, size.y) / math.tan(hfov / 2) + 0.3 * size.z))
        dist = max(40.0, min(dist, rad / math.sin(min(vfov, hfov) / 2) * 1.02))
    else:
        el = math.radians(40.0)
        cd.lens = 35.0
        target = Vector((centre.x, centre.y, lo.z + size.z * 0.3))
        dist = 300.0 if view == 'mid' else 1000.0
    d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
    cam.location = target + d * dist
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    cd.clip_start = 1.0
    cd.clip_end = 8000.0
    # sun from the camera's left, slightly behind the subject, 20 degrees up
    saz = az - math.radians(100.0)
    sel = math.radians(20.0)
    sdir = Vector((math.sin(saz) * math.cos(sel), -math.cos(saz) * math.cos(sel), math.sin(sel)))
    sun.rotation_euler = (-sdir).to_track_quat('-Z', 'Y').to_euler()
    if SKY is not None:
        SKY.sun_rotation = math.atan2(sdir.x, sdir.y)
    return cam


def row(items, gap=20.0, y=0.0):
    """Place builders in a row along X with gaps; items: list of kwargs for place()."""
    x = 0.0
    out = []
    for kw in items:
        r, objs = place(x=0.0, y=y, **kw)
        lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
        for c in objs[0].bound_box:
            lo = Vector((min(lo.x, c[0]), min(lo.y, c[1]), min(lo.z, c[2])))
            hi = Vector((max(hi.x, c[0]), max(hi.y, c[1]), max(hi.z, c[2])))
        r.location.x = x - lo.x
        x += hi.x - lo.x + gap
        out.append(r)
    for r in out:
        r.location.x -= (x - gap) * 0.5
    return out


SETS = {}
SKY = None
FIG = (0.0, 0.0)


def set_(name):
    def deco(fn):
        SETS[name] = fn
        return fn
    return deco


def heights(asset):
    return [dict(asset=asset, variant=v) for v in ('Low', 'Mid', 'High')]


for _a, _n in (('ApartmentSlab', 'apartment-slab'), ('ApartmentPoint', 'apartment-point'), ('ApartmentBrick', 'apartment-brick'),
               ('OfficeGlass', 'office-glass'), ('OfficeRibbon', 'office-ribbon'), ('OfficeRound', 'office-round')):
    SETS[_n] = (lambda a=_a: row(heights(a), gap=18.0))


@set_('plant')
def _plant():
    place('PowerPlant', 'Std', x=0.0, y=0.0)
    place('CoolingTower', 'Std', x=-25.0, y=120.0)
    place('CoolingTower', 'Std', x=65.0, y=125.0)
    place('Switchyard', 'Std', x=105.0, y=20.0)
    for i in range(3):
        place('Pylon', 'Std', x=150.0 + 45.0 * i, y=20.0 + 30.0 * i, yaw=0.58)


@set_('university')
def _uni():
    place('University', 'Std')


@set_('highway')
def _hw():
    place('HighwayInterchange', 'Std')
    for k in range(3):
        place('HighwayViaduct', 'Std', x=150.0 + 60.0 * k)
        place('HighwayViaduct', 'Std', x=-150.0 - 60.0 * k)
    for k in range(2):
        place('HighwayGrade', 'Std', x=0.0, y=150.0 + 60.0 * k, yaw=math.pi / 2)


@set_('states-housing')
def _st_h():
    y = 0.0
    row([dict(asset='ApartmentSlab', variant='Mid', kind=k) for k in B.ALL4], gap=22.0, y=40.0)
    row([dict(asset='OfficeGlass', variant='Low', kind=k) for k in B.ALL4], gap=22.0, y=-40.0)


@set_('states-plant')
def _st_p():
    row([dict(asset='CoolingTower', variant='Std', kind=k) for k in B.ALL4], gap=20.0, y=80.0)
    row([dict(asset='PowerPlant', variant='Std', kind=k) for k in B.ALL4], gap=25.0, y=-60.0)


@set_('states-civic')
def _st_c():
    row([dict(asset='University', variant='Std', kind=k) for k in B.ALL4], gap=25.0, y=0.0)
    row([dict(asset='HighwayViaduct', variant='Std', kind=k) for k in B.ALL4], gap=20.0, y=-90.0)


@set_('cultures')
def _cul():
    row([dict(asset='ApartmentPoint', variant='Low', culture=c, crown=True) for c in B.CULTURES], gap=25.0, y=40.0)
    row([dict(asset='OfficeRibbon', variant='Low', culture=c, crown=True) for c in B.CULTURES], gap=25.0, y=-40.0)


def district(night=0.0):
    """A small Atomic-age district for the game-distance check against age-5-atomic.png."""
    rng = __import__('age5_geo').Rng(5)
    kinds = [('ApartmentSlab', 'Low'), ('ApartmentSlab', 'Mid'), ('ApartmentSlab', 'High'), ('ApartmentPoint', 'Low'),
             ('ApartmentPoint', 'Mid'), ('ApartmentBrick', 'Low'), ('ApartmentBrick', 'Mid'), ('ApartmentBrick', 'High'),
             ('OfficeGlass', 'Low'), ('OfficeGlass', 'Mid'), ('OfficeRibbon', 'Low'), ('OfficeRibbon', 'Mid'),
             ('OfficeRound', 'Low'), ('ApartmentPoint', 'High'), ('OfficeGlass', 'High'), ('OfficeRibbon', 'High')]
    cultures = list(B.CULTURES)
    for gx in range(-3, 4):
        for gy in range(-2, 3):
            if (gx, gy) in ((2, 1), (2, 2)):
                continue
            centre = abs(gx) + abs(gy) <= 2
            k = kinds[rng.i(8 if centre else 0, len(kinds) - 1 if centre else 9)]
            place(k[0], k[1], x=gx * 62.0 + rng.u(-4, 4), y=gy * 58.0 + rng.u(-4, 4), yaw=(math.pi / 2) * rng.i(0, 1) if k[0].startswith('ApartmentSlab') else 0.0,
                  night=night)
    place('University', 'Std', x=150.0, y=90.0, night=night)
    place('HighwayViaduct', 'Std', x=-60.0, y=-170.0, night=night)
    place('HighwayViaduct', 'Std', x=0.0, y=-170.0, night=night)
    place('HighwayViaduct', 'Std', x=60.0, y=-170.0, night=night)
    place('HighwayViaduct', 'Std', x=120.0, y=-170.0, night=night)
    place('HighwayViaduct', 'Std', x=-120.0, y=-170.0, night=night)


SETS['district'] = district


def main(argv):
    name = argv[argv.index('--set') + 1]
    views = argv[argv.index('--views') + 1].split(',') if '--views' in argv else ['near', 'mid', 'far']
    night = '--night' in argv
    tag = argv[argv.index('--tag') + 1] if '--tag' in argv else ''
    if '--small' in argv:
        bpy.context.scene.render.resolution_percentage = 40
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    sun = rig(night)
    if name == 'district':
        district(night=0.32 if night else 0.0)
    else:
        SETS[name]()
    lo, hi = scene_bounds()
    global FIG
    FIG = (lo.x - 2.0, lo.y - 3.0)
    figure(*FIG)
    lo, hi = scene_bounds()
    for v in views:
        camera(v, lo, hi, sun)
        out = os.path.join(SHOTS, 'asset-age5-%s%s%s-%s.png' % (name, '-night' if night else '', tag, v))
        bpy.context.scene.render.filepath = out
        bpy.ops.render.render(write_still=True)
        print('[age5-preview] wrote', out, flush=True)
        bpy.data.objects.remove(bpy.context.scene.camera, do_unlink=True)


if __name__ == '__main__':
    main(sys.argv[sys.argv.index('--') + 1:])
