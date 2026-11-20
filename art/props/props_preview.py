# DEMIURGE props - preview renders (ASSETS.md section 3): golden-hour sun at 20 degrees, cool sky fill,
# neutral ground, EEVEE 1280 x 720, views at 40 m three-quarter, 300 m oblique and 1 km oblique.
#
#   python tools/render_slot.py -- blender.exe -b art/props/source/props.blend --python art/props/props_preview.py
#       -- <scene> [<view> ...]          scenes: rocks farm village lamps statues states night
#                                        views: 40m 300m 1km close (default: 40m 300m 1km)
# Writes shots/asset-props-<scene>-<view>.png. Each scene places LOD0 at 40 m, LOD1 at 300 m, LOD2 at 1 km
# (the LODs the game would show at those distances), with a 1.8 m figure for scale.
import math
import os
import random
import sys

import bpy
from mathutils import Euler, Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import props_lib as L  # noqa: E402

L.TEX_DIR = os.path.join(HERE, 'textures')
SHOTS = os.path.join(ROOT, 'shots')
INST = None


def instances():
    global INST
    if INST is None:
        import build_props as B
        INST = B.INSTANCES
    return INST


# --------------------------------------------------------------------------- rig
def rig(night=False):
    sc = bpy.data.scenes.new('Preview')
    if bpy.context.window:
        bpy.context.window.scene = sc
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Medium High Contrast'
    sc.view_settings.exposure = -0.3 if not night else 0.4
    ee = sc.eevee
    ee.taa_render_samples = 64
    for k, v in (('use_shadows', True), ('use_raytracing', True), ('shadow_ray_count', 2),
                 ('shadow_step_count', 8), ('use_volumetric_shadows', False)):
        if hasattr(ee, k):
            setattr(ee, k, v)
    if hasattr(ee, 'ray_tracing_options'):
        ee.ray_tracing_options.resolution_scale = '1'
    if hasattr(ee, 'fast_gi_method'):
        ee.fast_gi_method = 'GLOBAL_ILLUMINATION'
    w = bpy.data.worlds.new('PreviewSky')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    # cool sky fill: vertical gradient, pale warm near the horizon, blue above
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Generated'], sep.inputs[0])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    nt.links.new(sep.outputs[2], ramp.inputs[0])
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[1].position = 0.35
    if night:
        ramp.color_ramp.elements[0].color = (0.004, 0.006, 0.012, 1)
        ramp.color_ramp.elements[1].color = (0.001, 0.0015, 0.004, 1)
    else:
        ramp.color_ramp.elements[0].color = (0.62, 0.62, 0.62, 1)
        ramp.color_ramp.elements[1].color = (0.20, 0.32, 0.55, 1)
        e = ramp.color_ramp.elements.new(0.08)
        e.color = (0.55, 0.58, 0.64, 1)
    nt.links.new(ramp.outputs[0], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = 1.7 if not night else 1.0
    nt.links.new(bg.outputs[0], out.inputs[0])
    # sun: 20 degrees up, warm, from the left of the three-quarter camera
    sun = bpy.data.lights.new('Sun', 'SUN')
    sun.energy = 4.2 if not night else 0.0
    sun.color = (1.0, 0.80, 0.58)
    sun.angle = math.radians(1.2)
    so = bpy.data.objects.new('Sun', sun)
    sc.collection.objects.link(so)
    so.rotation_euler = Euler((math.radians(90 - 20), 0, math.radians(-78)), 'XYZ')
    if night:
        moon = bpy.data.lights.new('Moon', 'SUN')
        moon.energy = 0.02
        moon.color = (0.6, 0.7, 1.0)
        mo = bpy.data.objects.new('Moon', moon)
        sc.collection.objects.link(mo)
        mo.rotation_euler = Euler((math.radians(40), 0, math.radians(30)), 'XYZ')
    ground(sc)
    return sc


def ground(sc):
    me = bpy.data.meshes.new('Ground')
    s = 6000
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new('Ground', me)
    sc.collection.objects.link(ob)
    m = bpy.data.materials.new('PV_Ground')
    m.use_nodes = True
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    n1 = nt.nodes.new('ShaderNodeTexNoise')
    n1.inputs['Scale'].default_value = 0.08
    n1.inputs['Detail'].default_value = 8
    nt.links.new(tc.outputs['Object'], n1.inputs['Vector'])
    n2 = nt.nodes.new('ShaderNodeTexNoise')
    n2.inputs['Scale'].default_value = 1.5
    n2.inputs['Detail'].default_value = 6
    nt.links.new(tc.outputs['Object'], n2.inputs['Vector'])
    mx = nt.nodes.new('ShaderNodeMix')
    mx.data_type = 'FLOAT'
    nt.links.new(n1.outputs[0], mx.inputs[2])
    nt.links.new(n2.outputs[0], mx.inputs[3])
    mx.inputs[0].default_value = 0.4
    rp = nt.nodes.new('ShaderNodeValToRGB')
    rp.color_ramp.elements[0].color = (0.085, 0.085, 0.06, 1)
    rp.color_ramp.elements[1].color = (0.15, 0.14, 0.10, 1)
    nt.links.new(mx.outputs[0], rp.inputs[0])
    nt.links.new(rp.outputs[0], bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value = 0.95
    ob.data.materials.append(m)
    return ob


def camera(sc, target, dist, elev, azim, lens=50):
    cam = bpy.data.cameras.new('Cam')
    cam.lens = lens
    cam.clip_start = 0.5
    cam.clip_end = 20000
    co = bpy.data.objects.new('Cam', cam)
    sc.collection.objects.link(co)
    t = Vector(target)
    e, a = math.radians(elev), math.radians(azim)
    co.location = t + Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e))) * dist
    d = (t - co.location)
    co.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    sc.camera = co
    return co


def figure(sc, loc, rot=0.0):
    """1.8 m scale figure: a neutral grey mannequin."""
    p = L.merge(L.lathe([(0.0, 0.0), (0.16, 0.02), (0.2, 0.85), (0.24, 1.35), (0.18, 1.48), (0.0, 1.5)], 12, 'Structure'),
                L.ico(0.12, 2, 'Structure').at((0, 0, 1.66)))
    me = _mesh('PV_Figure', p)
    m = bpy.data.materials.get('PV_Figure') or bpy.data.materials.new('PV_Figure')
    m.diffuse_color = (0.5, 0.5, 0.5, 1)
    m.use_nodes = True
    m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.22, 0.21, 0.2, 1)
    me.materials.append(m)
    ob = bpy.data.objects.new('Figure', me)
    sc.collection.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = (0, 0, rot)
    return ob


def _mesh(name, part):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in part.v], [], [tuple(f) for f in part.f])
    for p in me.polygons:
        p.use_smooth = True
    return me


# --------------------------------------------------------------------------- placing assets
_matcache = {}


def variant_mat(master, inst=None, asset=None):
    """Preview copy of a master material with an instance tint and, for rocks, the asset's baked maps."""
    key = (master, inst, asset)
    if key in _matcache:
        return _matcache[key]
    import build_props as B
    tint = tuple(instances().get(master, {}).get(inst, (1, 1, 1))) if inst else (1, 1, 1)
    m = bpy.data.materials.new('PV_%s_%s_%s' % (master, inst, asset))
    if master == 'M_Prop_Glow':
        c = instances()['M_Prop_Glow'].get(inst or 'Fire')
        L.setup_material(m, None, emit=(c[0], c[1], c[2], 18.0))
    else:
        tex = B.TEXSET[master]
        prm = B.INSTANCE_PARAMS.get(master, {}).get(inst, {})
        rough = prm.get('roughness', 1.0)
        metal = prm.get('metallic', 1.0)
        baked = None
        if asset and master == 'M_Prop_Rock':
            baked = 'T_Prop_' + asset.replace('_', '')
        L.setup_material(m, tex, tint=tint, rough_mul=rough, metal_mul=metal, masked=master in L.MASKED,
                         moss=master in ('M_Prop_Rock', 'M_Prop_RockTile'), baked=baked)
    _matcache[key] = m
    return m


_pvmesh = {}


def pv_mesh(asset, lod, inst):
    """Copy of the LOD mesh with preview materials (instance tints, baked rock maps) on its slots."""
    key = (asset, lod, repr(inst))
    if key in _pvmesh:
        return _pvmesh[key]
    src = bpy.data.objects.get('SM_Prop_%s_LOD%d' % (asset, lod)) or bpy.data.objects.get('SM_Prop_%s_LOD0' % asset)
    if src is None:
        print('PREVIEW missing', asset)
        return None
    me = src.data.copy()
    for i, m in enumerate(src.data.materials):
        master = m.name
        iname = inst.get(master) if isinstance(inst, dict) else inst
        if iname is not None and iname not in instances().get(master, {}):
            iname = None
        if iname is None:
            iname = DEFAULT_INST.get(master)
        me.materials[i] = variant_mat(master, iname, asset if master == 'M_Prop_Rock' else None)
    _pvmesh[key] = me
    return me


DEFAULT_INST = {'M_Prop_Metal': 'Iron', 'M_Prop_Cloth': 'Valley', 'M_Prop_Glow': 'Fire', 'M_Prop_Steel': 'Brushed'}


def place(sc, asset, loc, rot=0.0, scale=1.0, lod=0, inst=None, tilt=(0, 0)):
    """One object using SM_Prop_<asset>_LOD<lod>; inst = {master: instance} or a single instance name."""
    me = pv_mesh(asset, lod, inst)
    if me is None:
        return None
    ob = bpy.data.objects.new('PV_%s' % asset, me)
    sc.collection.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = (tilt[0], tilt[1], rot)
    ob.scale = (scale,) * 3 if not isinstance(scale, (tuple, list)) else scale
    return ob


_gn = None


def _gn_tree():
    global _gn
    if _gn:
        return _gn
    ng = bpy.data.node_groups.new('PV_Scatter', 'GeometryNodeTree')
    ng.interface.new_socket('Geometry', in_out='INPUT', socket_type='NodeSocketGeometry')
    ng.interface.new_socket('Object', in_out='INPUT', socket_type='NodeSocketObject')
    ng.interface.new_socket('Geometry', in_out='OUTPUT', socket_type='NodeSocketGeometry')
    gi = ng.nodes.new('NodeGroupInput')
    go = ng.nodes.new('NodeGroupOutput')
    oi = ng.nodes.new('GeometryNodeObjectInfo')
    oi.transform_space = 'ORIGINAL'
    oi.inputs['As Instance'].default_value = True
    ng.links.new(gi.outputs['Object'], oi.inputs['Object'])
    iop = ng.nodes.new('GeometryNodeInstanceOnPoints')
    ng.links.new(gi.outputs['Geometry'], iop.inputs['Points'])
    ng.links.new(oi.outputs['Geometry'], iop.inputs['Instance'])
    rz = ng.nodes.new('GeometryNodeInputNamedAttribute')
    rz.data_type = 'FLOAT'
    rz.inputs['Name'].default_value = 'rz'
    cx = ng.nodes.new('ShaderNodeCombineXYZ')
    ng.links.new(rz.outputs['Attribute'], cx.inputs['Z'])
    e2r = ng.nodes.new('FunctionNodeEulerToRotation')
    ng.links.new(cx.outputs[0], e2r.inputs[0])
    ng.links.new(e2r.outputs[0], iop.inputs['Rotation'])
    sa = ng.nodes.new('GeometryNodeInputNamedAttribute')
    sa.data_type = 'FLOAT'
    sa.inputs['Name'].default_value = 'sc'
    ng.links.new(sa.outputs['Attribute'], iop.inputs['Scale'])
    ng.links.new(iop.outputs['Instances'], go.inputs['Geometry'])
    _gn = ng
    return ng


def scatter(sc, asset, xf, lod=0, inst=None):
    """Many instances of one asset: xf = [(x, y, z, rot_z, scale), ...] via geometry-node instancing."""
    if not xf:
        return None
    me = pv_mesh(asset, lod, inst)
    if me is None:
        return None
    src = bpy.data.objects.new('PVsrc_%s' % asset, me)
    sc.collection.objects.link(src)
    src.location = (0, 0, -5000)
    src.hide_render = True
    src.hide_viewport = True
    pm = bpy.data.meshes.new('PVpts_%s' % asset)
    pm.from_pydata([(t[0], t[1], t[2]) for t in xf], [], [])
    a = pm.attributes.new('rz', 'FLOAT', 'POINT')
    a.data.foreach_set('value', [t[3] for t in xf])
    b = pm.attributes.new('sc', 'FLOAT', 'POINT')
    b.data.foreach_set('value', [t[4] for t in xf])
    ob = bpy.data.objects.new('PVscatter_%s' % asset, pm)
    sc.collection.objects.link(ob)
    mod = ob.modifiers.new('scatter', 'NODES')
    ng = _gn_tree().copy()
    oi = [n for n in ng.nodes if n.bl_idname == 'GeometryNodeObjectInfo'][0]
    for l in list(ng.links):
        if l.to_node == oi:
            ng.links.remove(l)
    oi.inputs['Object'].default_value = src
    mod.node_group = ng
    return ob


def soil(sc, x0, y0, x1, y1, colr=(0.11, 0.075, 0.05)):
    me = bpy.data.meshes.new('PV_soil')
    me.from_pydata([(x0, y0, 0.02), (x1, y0, 0.02), (x1, y1, 0.02), (x0, y1, 0.02)], [], [(0, 1, 2, 3)])
    key = 'PV_soil_%.2f_%.2f_%.2f' % colr
    m = bpy.data.materials.get(key)
    if m is None:
        m = bpy.data.materials.new(key)
        m.use_nodes = True
        nt = m.node_tree
        bs = nt.nodes['Principled BSDF']
        nz = nt.nodes.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = 0.6
        tc = nt.nodes.new('ShaderNodeTexCoord')
        nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
        rp = nt.nodes.new('ShaderNodeValToRGB')
        rp.color_ramp.elements[0].color = (colr[0] * 0.75, colr[1] * 0.75, colr[2] * 0.75, 1)
        rp.color_ramp.elements[1].color = (colr[0] * 1.2, colr[1] * 1.2, colr[2] * 1.2, 1)
        nt.links.new(nz.outputs[0], rp.inputs[0])
        nt.links.new(rp.outputs[0], bs.inputs['Base Color'])
        bs.inputs['Roughness'].default_value = 0.95
    me.materials.append(m)
    ob = bpy.data.objects.new('PV_soil', me)
    sc.collection.objects.link(ob)
    return ob


def lod_for(view):
    return {'40m': 0, 'close': 0, '300m': 1, '1km': 2}.get(view, 0)


VIEWS = {   # distance, elevation, azimuth, lens
    'close': (14, 22, 35, 50), '40m': (40, 28, 35, 35), '300m': (300, 40, 30, 50), '1km': (1000, 40, 30, 50),
}


# --------------------------------------------------------------------------- scenes
def scene_rocks(sc, view, rng):
    lod = lod_for(view)
    rocks = ['Rock_A', 'Rock_B', 'Rock_C', 'Rock_D', 'Rock_E', 'Rock_F', 'Rock_G', 'Rock_H']
    # the eight rocks in a loose arc in front, the four cliffs as a ridge behind
    for i, r in enumerate(rocks):
        a = math.radians(-70 + i * 20)
        place(sc, r, (math.sin(a) * 9, -math.cos(a) * 4 + 2, 0), rot=rng.uniform(0, 6.28), lod=lod)
    for c, pos, b in (('Cliff_A', (-13, 24), 'Granite'), ('Cliff_C', (12, 28), 'Sandstone'),
                      ('Cliff_B', (-33, 32), 'Granite'), ('Cliff_D', (34, 34), 'Basalt')):
        place(sc, c, (pos[0], pos[1], -0.3), rot=rng.uniform(-0.3, 0.3), lod=lod, inst={'M_Prop_Rock': b})
    figure(sc, (1.5, -3, 0))
    if view in ('300m', '1km'):
        # a rocky hillside: cliffs and boulders scattered over a few hundred metres
        for k in range(110):
            r = rng.choice(rocks + ['Cliff_A', 'Cliff_B', 'Cliff_C', 'Cliff_D'] * 2)
            x, y = rng.uniform(-260, 260), rng.uniform(-200, 260)
            if abs(x) < 40 and abs(y) < 40:
                continue
            s = rng.uniform(0.7, 1.4) * (1.6 if r.startswith('Rock') else 1.0)
            place(sc, r, (x, y, -0.3), rot=rng.uniform(0, 6.28), scale=s, lod=lod,
                  inst={'M_Prop_Rock': rng.choice(['Granite', 'Granite', 'Sandstone'])})
    return (0, 17, 5.5) if view in ('40m', 'close') else (0, 30, 0)


def _run_line(sc, asset, x0, y, n, rot=0.0, lod=0, inst=None, damaged_at=None):
    """A run of n 4 m tiles along X starting at x0 (tile centres at x0 + 2 + 4k)."""
    for k in range(n):
        a = asset if damaged_at is None or k != damaged_at else asset + '_Damaged'
        place(sc, a, (x0 + 2 + 4 * k, y, 0), rot=rot, lod=lod, inst=inst)


def _field(xf_lists, kind, x0, y0, w, d, rng):
    """Tile a w x d field (rows along X) with 4 m crop tiles."""
    nx, ny = int(w // 4), int(d // 4)
    for i in range(nx):
        for j in range(ny):
            xf_lists.setdefault(kind, []).append((x0 + 2 + 4 * i, y0 + 2 + 4 * j, 0.0, 0.0, 1.0))


def scene_farm(sc, view, rng):
    """Fields of grain, vegetables and vines, edged by hedgerows, wattle, rails and dry-stone walls; hay."""
    lod = lod_for(view)
    big = view in ('300m', '1km')
    nfx, nfy = (2, 2) if not big else ((5, 4) if view == '300m' else (10, 8))
    fw, fd, lane = 40, 28, 4
    kinds = ['Crop_Grain', 'Crop_Grain', 'Crop_Veg', 'Crop_Grain', 'Crop_Vine', 'Hay_Stook', 'Crop_Grain', 'Fallow']
    insts = {'Crop_Grain': ['Ripe', 'Ripe', 'Green', 'Barley']}
    xf = {}
    edges = {}
    ox = -(nfx * (fw + lane)) / 2
    oy = -8 if not big else -(nfy * (fd + lane)) / 2
    for i in range(nfx):
        for j in range(nfy):
            x0 = ox + i * (fw + lane)
            y0 = oy + j * (fd + lane)
            if not big and i == 0 and j == 0:
                kind = 'Crop_Grain'
            elif not big:
                kind = ['Crop_Veg', 'Hay_Stook', 'Crop_Grain'][(i + j * 2 - 1) % 3]
            else:
                kind = rng.choice(kinds)
            if kind == 'Crop_Grain':
                ins = rng.choice(insts['Crop_Grain']) if big else 'Ripe'
                soil(sc, x0, y0, x0 + fw, y0 + fd, (0.16, 0.12, 0.06))
                _field(xf, ('Crop_Grain', ins), x0, y0, fw, fd, rng)
            elif kind == 'Crop_Veg':
                soil(sc, x0, y0, x0 + fw, y0 + fd)
                _field(xf, ('Crop_Veg', 'Cabbage'), x0, y0, fw, fd, rng)
            elif kind == 'Crop_Vine':
                soil(sc, x0, y0, x0 + fw, y0 + fd, (0.12, 0.09, 0.06))
                for r in range(int(fd // 2)):
                    for k in range(int(fw // 4)):
                        xf.setdefault(('Crop_Vine', None), []).append((x0 + 2 + 4 * k, y0 + 1 + 2 * r, 0, 0, 1))
            elif kind == 'Hay_Stook':
                soil(sc, x0, y0, x0 + fw, y0 + fd, (0.2, 0.16, 0.09))
                for r in range(int(fd // 5)):
                    for k in range(int(fw // 5)):
                        xf.setdefault(('Hay_Stook', None), []).append(
                            (x0 + 2.5 + 5 * k + rng.uniform(-0.6, 0.6), y0 + 2.5 + 5 * r + rng.uniform(-0.6, 0.6), 0,
                             rng.uniform(0, 6.28), rng.uniform(0.9, 1.1)))
                for k in range(rng.randint(1, 3)):
                    xf.setdefault(('Hay_Stack', None), []).append(
                        (x0 + rng.uniform(4, fw - 4), y0 + rng.uniform(4, fd - 4), 0, rng.uniform(0, 6.28), rng.uniform(0.9, 1.1)))
            # field edges: hedgerows on the long sides, a fence or wall on the short sides (by region)
            region = (i + j) % 3 if big else 0
            hed = 'Hedge_A'
            short = ['Fence_Wattle', 'Fence_Rail', 'Fence_DryStone'][region] if big else 'Fence_Wattle'
            for k in range(fw // 4):
                h = 'Hedge_B' if rng.random() < 0.18 else hed
                edges.setdefault((h, 'Summer'), []).append((x0 + 2 + 4 * k, y0 - lane / 2, 0, rng.choice((0, math.pi)), 1))
            for k in range(fd // 4):
                f = short if not (not big and k == 3) else short + '_Damaged'
                edges.setdefault((f, None), []).append((x0 - lane / 2, y0 + 2 + 4 * k, 0, math.pi / 2, 1))
    for (asset, ins), lst in list(xf.items()) + list(edges.items()):
        scatter(sc, asset, lst, lod, {'M_Prop_Crop': ins, 'M_Prop_Foliage': ins} if ins else None)
    if not big:
        place(sc, 'Hay_Stack', (-12, -16, 0), rot=0.4, lod=lod)
        place(sc, 'Hay_Stack', (-6.5, -19, 0), rot=1.4, scale=0.85, lod=lod)
        place(sc, 'Cart_Hand', (-1, -16, 0), rot=2.2, lod=lod)
        place(sc, 'Hay_Bales', (8, -18, 0), rot=0.3, lod=lod)
        place(sc, 'Sign_Fingerpost', (-24, -13, 0), rot=0.5, lod=lod)
        place(sc, 'Rock_E', (-20, -18, 0), rot=1.0, lod=lod)
        figure(sc, (-3, -13.5, 0))
        figure(sc, (-9, -14, 0), 1.0)
        return (-4, -6, 1.0)
    return (0, 0, 0)


def scene_village(sc, view, rng):
    """A market square: stalls in three culture colours, goods, barrels, carts, a statue, signs and lamps."""
    lod = lod_for(view)
    cultures = ['Valley', 'CliffTerrace', 'LagoonWeaver', 'RingBuilder']
    pave = soil(sc, -22, -18, 22, 18, (0.25, 0.22, 0.18))
    stalls = [(-12, 8, 0), (-6, 8, 0), (0, 8.3, 0), (6, 8, 0), (12, 8, 0),
              (-12, -8, math.pi), (-6, -8, math.pi), (6, -8.2, math.pi), (12, -8, math.pi)]
    for k, (x, y, r) in enumerate(stalls):
        st = 'Market_Stall' if k not in (4,) else 'Market_Stall_Damaged'
        if k == 8:
            st = 'Market_Stall_Burnt'
        place(sc, st, (x, y, 0), rot=r + rng.uniform(-0.05, 0.05), lod=lod,
              inst={'M_Prop_Cloth': cultures[k % 4], 'M_Prop_Stone': 'Terracotta'})
    place(sc, 'Statue_Figure', (0, 0, 0), rot=0.0, lod=lod, inst={'M_Prop_Stone': 'Marble'})
    for (a, x, y, r, ins) in (('Goods_Sacks', -9, 5, 0.3, {'M_Prop_Cloth': 'Burlap'}), ('Goods_Pots', -3, 5.4, 0.0, {'M_Prop_Stone': 'Terracotta'}),
                              ('Goods_Baskets', 3, 5.2, 0.5, None), ('Barrel', 9.2, 5.5, 0, None),
                              ('Barrel_Stack', 15.5, 6.5, 1.57, None), ('Crate_Stack', 15.5, 10.5, 0.2, None),
                              ('Goods_Sacks', -15.5, -5.5, 2.0, {'M_Prop_Cloth': 'Burlap'}), ('Barrel', -3.2, -5.6, 0, None),
                              ('Barrel', -2.4, -6.1, 0, None), ('Goods_Pots', 2.5, -5.5, 1.0, {'M_Prop_Stone': 'Terracotta'}),
                              ('Cart_Hand', -17, 1, 1.2, None), ('Cart_Wagon', 18, -2, 1.5, {'M_Prop_Cloth': 'Undyed'}),
                              ('Sign_Fingerpost', -20, -14, 0.3, None), ('Sign_Milestone', 20, 14, 2.6, None),
                              ('Lamp_Age3', -8.5, 0, 0, None), ('Lamp_Age3', 8.5, 0, math.pi, None)):
        place(sc, a, (x, y, 0), rot=r, lod=lod, inst=ins)
    for (x, y, r) in ((-4, 1.5, 0.3), (3.5, 2.5, 2.0), (-10, 4, 1.0), (10.5, -4.5, 4.0), (1, -3.5, 3.3)):
        figure(sc, (x, y, 0), r)
    if view in ('300m', '1km'):
        # the town around the square: lanes of carts, barrels, crates and lamps out to 300 m
        lst = {}
        for k in range(260):
            a = rng.choice(['Barrel', 'Barrel_Stack', 'Crate_Stack', 'Cart_Hand', 'Goods_Sacks', 'Lamp_Age3', 'Hay_Stack'])
            x, y = rng.uniform(-260, 260), rng.uniform(-200, 260)
            if abs(x) < 26 and abs(y) < 22:
                continue
            lst.setdefault(a, []).append((x, y, 0, rng.uniform(0, 6.28), 1.0))
        for a, l in lst.items():
            scatter(sc, a, l, lod)
    return (0, 0, 1.0)


LAMPS = ['Lamp_Age0', 'Lamp_Age1', 'Lamp_Age2', 'Lamp_Age3', 'Lamp_Age4', 'Lamp_Age5', 'Lamp_Age6']
LAMP_INST = [{'M_Prop_Glow': 'Fire'}, {'M_Prop_Glow': 'Fire', 'M_Prop_Stone': 'Terracotta'}, {'M_Prop_Glow': 'Fire'},
             {'M_Prop_Glow': 'Oil'}, {'M_Prop_Glow': 'Gas', 'M_Prop_Metal': 'PaintedGreen'},
             {'M_Prop_Glow': 'Sodium', 'M_Prop_Steel': 'Galvanised'}, {'M_Prop_Glow': 'LED', 'M_Prop_Steel': 'PaintedWhite'}]


def scene_lamps(sc, view, rng, night=False):
    lod = lod_for(view)
    if view in ('40m', 'close'):
        for k, (a, ins) in enumerate(zip(LAMPS, LAMP_INST)):
            place(sc, a, (-12 + k * 4, 0, 0), rot=0.0, lod=lod, inst=ins)
        figure(sc, (-10, -1.2, 0))
        figure(sc, (2, -1.5, 0), 0.5)
        return (0, 0, 2.5)
    # a street grid of one age per block row, lamps every 18 m along the streets
    lst = {}
    for k, (a, ins) in enumerate(zip(LAMPS, LAMP_INST)):
        y = -330 + k * 100
        for side in (-1, 1):
            for i in range(-22, 23):
                lst.setdefault((a, repr(ins)), (ins, []))[1].append((i * 18.0, y + side * 7, 0, 0 if side < 0 else math.pi, 1))
        soil(sc, -400, y - 5, 400, y + 5, (0.08, 0.08, 0.08))
    for (a, _), (ins, l) in lst.items():
        scatter(sc, a, l, lod, ins)
    return (0, 0, 0)


def scene_night(sc, view, rng):
    return scene_lamps(sc, view, rng, night=True)


STATUES = ['Statue_Totem', 'Statue_Idol', 'Statue_Figure', 'Statue_Obelisk', 'Statue_Monument']
ST_INST = [None, {'M_Prop_Stone': 'Valley'}, {'M_Prop_Stone': 'Marble'}, {'M_Prop_Stone': 'Valley'},
           {'M_Prop_Steel': 'Brushed', 'M_Prop_Stone': 'Concrete'}]


def scene_statues(sc, view, rng):
    lod = lod_for(view)
    for i, (a, ins) in enumerate(zip(STATUES, ST_INST)):
        for j, st in enumerate(('', '_Damaged', '_Ruin')):
            place(sc, a + st, (-16 + i * 8, 10 - j * 9, 0), rot=0.0, lod=lod, inst=ins)
    figure(sc, (-11, -9, 0))
    if view in ('300m', '1km'):
        lst = {}
        for k in range(120):
            a = rng.choice(STATUES)
            x, y = rng.uniform(-260, 260), rng.uniform(-200, 260)
            if abs(x) < 30 and abs(y) < 25:
                continue
            lst.setdefault(a, []).append((x, y, 0, rng.uniform(0, 6.28), 1.0))
        for i, a in enumerate(STATUES):
            scatter(sc, a, lst.get(a, []), lod, ST_INST[i])
    return (0, 1, 2.5)


def scene_lineup(sc, view, rng):
    """Every small prop in rows for close inspection (not a game view)."""
    rows = [
        ['Barrel', 'Barrel_Stack', 'Crate_Stack', 'Goods_Sacks', 'Goods_Pots', 'Goods_Baskets', 'Sign_Cairn',
         'Sign_Fingerpost', 'Sign_Milestone', 'Sign_Street'],
        ['Hay_Stack', 'Hay_Bales', 'Hay_Stook', 'Cart_Hand', 'Cart_Wagon', 'Market_Stall', 'Market_Stall_Damaged',
         'Market_Stall_Burnt'],
        ['Fence_Wattle', 'Fence_Wattle_Damaged', 'Fence_Rail', 'Fence_Rail_Damaged', 'Fence_DryStone',
         'Fence_DryStone_Damaged', 'Fence_Iron', 'Fence_Iron_Damaged'],
        ['Hedge_A', 'Hedge_B', 'Crop_Grain', 'Crop_Veg', 'Crop_Vine'],
    ]
    insts = {'Goods_Sacks': {'M_Prop_Cloth': 'Burlap'}, 'Goods_Pots': {'M_Prop_Stone': 'Terracotta'},
             'Crop_Veg': {'M_Prop_Foliage': 'Cabbage'}, 'Fence_Iron': {'M_Prop_Metal': 'PaintedGreen'},
             'Market_Stall': {'M_Prop_Stone': 'Terracotta'}}
    y = 0
    for r, row in enumerate(rows):
        x = -20
        w = 4.6 if r < 2 else 5.0
        for a in row:
            place(sc, a, (x, y, 0), rot=0.0, lod=0, inst=insts.get(a.replace('_Damaged', '').replace('_Burnt', '')))
            x += w if r != 1 else (4.6 if 'Market' not in a and 'Wagon' not in a else 5.6)
        y += 7 if r < 3 else 6
    figure(sc, (-22, -1.5, 0))
    return (0, 9, 1.0)


# --------------------------------------------------------------------------- verification views (verifier, 2026-11-19)
# Each asset in a column, LOD0 / LOD1 / LOD2 (/ LOD3) in rows front to back, all at the same scale: shows collapsed
# or missing parts in the LODs and lets the 1.8 m figure check scale. Scenes 'lods-<group>', view 'close'.
LOD_GROUPS = {
    'fields': ['Fence_Wattle', 'Fence_Rail', 'Fence_DryStone', 'Fence_DryStone_Damaged', 'Fence_Iron', 'Hedge_A',
               'Hedge_B', 'Crop_Grain', 'Crop_Veg', 'Crop_Vine'],
    'goods': ['Hay_Stack', 'Hay_Bales', 'Hay_Stook', 'Barrel', 'Barrel_Stack', 'Crate_Stack', 'Cart_Hand',
              'Cart_Wagon', 'Market_Stall', 'Market_Stall_Damaged', 'Goods_Sacks', 'Goods_Pots', 'Goods_Baskets'],
    'signs': ['Sign_Cairn', 'Sign_Fingerpost', 'Sign_Milestone', 'Sign_Street', 'Lamp_Age0', 'Lamp_Age1', 'Lamp_Age2',
              'Lamp_Age3', 'Lamp_Age4', 'Lamp_Age5', 'Lamp_Age6'],
    'statues': ['Statue_Totem', 'Statue_Idol', 'Statue_Figure', 'Statue_Figure_Ruin', 'Statue_Obelisk',
                'Statue_Obelisk_Ruin', 'Statue_Monument', 'Market_Stall_Burnt'],
    'rocks': ['Rock_A', 'Rock_B', 'Rock_C', 'Rock_D', 'Rock_E', 'Rock_F', 'Rock_G', 'Rock_H'],
    'cliffs': ['Cliff_A', 'Cliff_B', 'Cliff_C', 'Cliff_D'],
}


def _width(asset):
    o = bpy.data.objects.get('SM_Prop_%s_LOD0' % asset)
    return max(1.0, o.dimensions.x) if o else 2.0


def scene_lods(sc, view, rng, group='goods'):
    assets = LOD_GROUPS[group]
    nl = 4 if group == 'cliffs' else 3
    gap = 1.2 if group not in ('cliffs',) else 4.0
    xs, x = [], 0.0
    for a in assets:
        w = _width(a)
        xs.append(x + w / 2)
        x += w + gap
    depth = max(max(bpy.data.objects['SM_Prop_%s_LOD0' % a].dimensions.y for a in assets) + gap, 3.0)
    for a, cx in zip(assets, xs):
        for l in range(nl):
            if bpy.data.objects.get('SM_Prop_%s_LOD%d' % (a, l)) is None:
                continue
            ins = {'M_Prop_Glow': 'Gas', 'M_Prop_Stone': 'Valley'} if a.startswith(('Lamp', 'Statue')) else None
            place(sc, a, (cx - x / 2, l * depth, 0), lod=l, inst=ins)
    figure(sc, (-x / 2 - 1.0, 0, 0))
    span = max(x, nl * depth)
    return (0, (nl - 1) * depth / 2, 0.5), span


def scene_bakes(sc, view, rng):
    """Baked rocks and cliffs at LOD0, close, sun raking from the left: shows bake errors and crack read."""
    for (a, loc, b) in (('Rock_F', (-7, -4), 'Granite'), ('Rock_G', (-2, -4.5), 'Granite'), ('Rock_H', (3, -4), 'Granite'),
                        ('Rock_A', (8, -4), 'Granite'), ('Cliff_B', (-9, 10), 'Granite'), ('Cliff_C', (12, 12), 'Sandstone')):
        place(sc, a, (loc[0], loc[1], 0 if a.startswith('Rock') else -0.3), rot=0.4, lod=0, inst={'M_Prop_Rock': b})
    figure(sc, (0.5, -7, 0))
    return (0, 4, 3.0)


def run(scene_name, views):
    if scene_name.startswith('lods-'):
        grp = scene_name[5:]
        for view in views:
            sc = rig()
            tgt, span = scene_lods(sc, view, random.Random(7), grp)
            camera(sc, tgt, span * 0.95, 32, 20, 35)
            sc.render.filepath = os.path.join(SHOTS, 'asset-props-verify-lods-%s.png' % grp)
            bpy.ops.render.render(write_still=True, scene=sc.name)
            print('PREVIEW wrote', sc.render.filepath)
            bpy.data.scenes.remove(sc)
        return
    if scene_name == 'bakes':
        sc = rig()
        tgt = scene_bakes(sc, 'close', random.Random(7))
        camera(sc, tgt, 30, 24, 25, 35)
        sc.render.filepath = os.path.join(SHOTS, 'asset-props-verify-bakes.png')
        bpy.ops.render.render(write_still=True, scene=sc.name)
        print('PREVIEW wrote', sc.render.filepath)
        bpy.data.scenes.remove(sc)
        return
    _run(scene_name, views)


def _run(scene_name, views):
    rng = random.Random(7)
    for view in views:
        sc = rig(night=(scene_name == 'night'))
        fn = globals()['scene_' + scene_name]
        tgt = fn(sc, view, random.Random(7))
        d, el, az, lens = VIEWS[view]
        if scene_name == 'lineup':
            d, el, az, lens = 36, 38, 18, 35
        camera(sc, tgt, d, el, az, lens)
        if scene_name == 'night' and view in ('40m', 'close'):
            for k, h in enumerate((1.85, 1.5, 2.75, 2.35, 3.55, 8.8, 6.85)):
                li = bpy.data.lights.new('L%d' % k, 'POINT')
                li.energy = (60, 80, 120, 60, 90, 900, 600)[k]
                c = instances()['M_Prop_Glow'][['Fire', 'Fire', 'Fire', 'Oil', 'Gas', 'Sodium', 'LED'][k]]
                li.color = c
                li.shadow_soft_size = 0.1
                lo = bpy.data.objects.new('L%d' % k, li)
                sc.collection.objects.link(lo)
                lo.location = (-12 + k * 4, (0, 0, 0, -0.68, 0, -2.3, -0.45)[k], h)
        path = os.path.join(SHOTS, 'asset-props-%s-%s.png' % (scene_name, view))
        sc.render.filepath = path
        bpy.context.window_manager  # noqa
        bpy.ops.render.render(write_still=True, scene=sc.name)
        print('PREVIEW wrote', path)
        bpy.data.scenes.remove(sc)


if __name__ == '__main__':
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    name = a[0] if a else 'rocks'
    views = a[1:] or ['40m', '300m', '1km']
    run(name, views)
