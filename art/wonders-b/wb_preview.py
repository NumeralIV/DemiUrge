# DEMIURGE art / wonders-b - preview renders (EEVEE, 1280x720). Run through the slot tool:
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       art/wonders-b/source/<asset>.blend --python art/wonders-b/wb_preview.py -- --asset <asset> [--views 40m,300m,1km,night,states,cultures]
#
# Rig per docs/ASSETS.md: warm sun at 20 deg elevation, cool sky fill, neutral ground, views at 40 m three-quarter,
# 300 m oblique and 1 km oblique. Trees at the tree sockets, the steam plumes and the 1.8 m figure are preview
# dressing only (not part of the exported assets).
import os
import sys
import json
import math
import random

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'shots')

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402
import wb_defs as D  # noqa: E402
import wb_geo as G  # noqa: E402
import wb_assets as A  # noqa: E402

DEG = math.pi / 180


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'asset': None, 'views': ['40m', '300m', '1km', 'night', 'states', 'cultures'], 'samples': 48, 'suffix': ''}
    i = 0
    while i < len(a):
        if a[i] == '--asset':
            o['asset'] = a[i + 1]
            i += 1
        elif a[i] == '--views':
            o['views'] = a[i + 1].split(',')
            i += 1
        elif a[i] == '--samples':
            o['samples'] = int(a[i + 1])
            i += 1
        elif a[i] == '--suffix':
            o['suffix'] = a[i + 1]
            i += 1
        i += 1
    return o


def hexc(h, a=1.0):
    h = h.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    c = [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
    return (c[0], c[1], c[2], a)


def polar(r, th, z=0.0):
    return Vector((r * math.cos(th), r * math.sin(th), z))


FRONT = -90 * DEG
# per asset: radius for framing, near view (target, camera azimuth deg, elevation deg), figure position
# (verifier: figure positions moved into the 40 m frames: Choir on the arcade walkway, Ear on the berm by the wall,
#  Stair on a terrace tread beside the falls; figure() drops them onto the surface below)
CFG = {
    'choir': dict(R=108.0, zc=25.0, near=(polar(66, -101 * DEG, 9.0), -165, 14), fig=polar(75.0, -112 * DEG, 10.2), ground_hole=0.0),
    'ear': dict(R=180.0, zc=30.0, near=(polar(131, 210 * DEG, 30.0), 180, 16), fig=polar(128.5, 214 * DEG, 14.0), ground_hole=0.0),
    'ladder': dict(R=205.0, zc=20.0, near=(polar(199, -112 * DEG, 13.0), -150, 12), fig=polar(206, -110 * DEG, 0.0), ground_hole=0.0),
    'sunwell': dict(R=150.0, zc=0.0, near=(polar(92, -97 * DEG, -3.0), -72, 30), fig=polar(104.5, -94 * DEG, 0.9), ground_hole=101.2),
    'stair': dict(R=155.0, zc=55.0, near=(polar(93.0 * A.st_warp(-92 * DEG, 44.0), -92 * DEG, 44.0), -128, 13),
                  fig=polar(97.0 * A.st_warp(-94 * DEG, 40.6), -94 * DEG, 40.6), ground_hole=0.0),
}


# ------------------------------------------------------------------------------------------ scene

def setup_render(samples):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    sc.render.film_transparent = False
    ee = sc.eevee
    ee.taa_render_samples = samples
    try:
        ee.use_raytracing = True
        ee.ray_tracing_method = 'SCREEN'
        ee.use_shadows = True
        ee.shadow_ray_count = 2
        ee.shadow_step_count = 8
        ee.use_fast_gi = True
        ee.fast_gi_distance = 30.0
        ee.volumetric_end = 4000.0
        ee.volumetric_tile_size = '8'
        ee.volumetric_samples = 48
        ee.use_volumetric_shadows = True
    except Exception as e:
        print('eevee setting', e)
    vs = sc.view_settings
    try:
        vs.view_transform = 'AgX'
        vs.look = 'AgX - Medium High Contrast'
    except Exception as e:
        print('look', e)
    vs.exposure = 0.0
    sc.render.use_compositing = True
    try:
        tree = bpy.data.node_groups.new('PreviewComp', 'CompositorNodeTree')
        sc.compositing_node_group = tree
        rl = tree.nodes.new('CompositorNodeRLayers')
        gl = tree.nodes.new('CompositorNodeGlare')
        try:
            gl.inputs['Type'].default_value = 'Bloom'
        except Exception as e:
            print('glare type', e)
        for nm, val in (('Threshold', 1.0), ('Strength', 0.3), ('Size', 0.6)):
            try:
                gl.inputs[nm].default_value = val
            except Exception:
                pass
        out = tree.nodes.new('NodeGroupOutput')
        tree.interface.new_socket('Image', in_out='OUTPUT', socket_type='NodeSocketColor')
        tree.links.new(rl.outputs['Image'], gl.inputs['Image'])
        tree.links.new(gl.outputs['Image'], out.inputs[0])
    except Exception as e:
        print('compositor', e)
        sc.render.use_compositing = False


def world(night=False, haze=True):
    w = bpy.data.worlds.get('PreviewWorld') or bpy.data.worlds.new('PreviewWorld')
    bpy.context.scene.world = w
    w.use_nodes = True
    nt = w.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Generated'], sep.inputs[0])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    nt.links.new(sep.outputs['Z'], ramp.inputs[0])
    cr = ramp.color_ramp
    if night:
        cr.elements[0].position, cr.elements[0].color = 0.0, hexc('#0a0e18')
        cr.elements[1].position, cr.elements[1].color = 0.35, hexc('#0b1428')
        bg.inputs['Strength'].default_value = 0.35
    else:
        cr.elements[0].position, cr.elements[0].color = 0.0, hexc('#d9c3a0')
        e = cr.elements.new(0.08)
        e.color = hexc('#c9c6c0')
        cr.elements[1].position, cr.elements[1].color = 0.55, hexc('#5f86bf')
        bg.inputs['Strength'].default_value = 0.65
    nt.links.new(ramp.outputs[0], bg.inputs['Color'])
    nt.links.new(bg.outputs[0], out.inputs['Surface'])
    hz = bpy.data.objects.get('PreviewHaze')
    if hz:
        bpy.data.objects.remove(hz)
    if haze:
        # a bounded haze box (an EEVEE world volume is infinite and would swallow the sky and the sun)
        m = bpy.data.materials.get('PreviewHazeMat') or bpy.data.materials.new('PreviewHazeMat')
        m.use_nodes = True
        mt = m.node_tree
        for n in list(mt.nodes):
            mt.nodes.remove(n)
        mo = mt.nodes.new('ShaderNodeOutputMaterial')
        vol = mt.nodes.new('ShaderNodeVolumePrincipled')
        vol.inputs['Density'].default_value = 0.00011 if not night else 0.00008
        vol.inputs['Color'].default_value = hexc('#e8e0d2') if not night else hexc('#304060')
        vol.inputs['Anisotropy'].default_value = 0.5
        mt.links.new(vol.outputs[0], mo.inputs['Volume'])
        me = bpy.data.meshes.new('PreviewHaze')
        import bmesh
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        bm.to_mesh(me)
        bm.free()
        me.materials.append(m)
        ob = bpy.data.objects.new('PreviewHaze', me)
        ob.scale = (7000.0, 7000.0, 700.0)
        ob.location = (0, 0, 300.0)
        bpy.context.scene.collection.objects.link(ob)


def sun_for(cam_az_deg, night=False):
    for o in [o for o in bpy.data.objects if o.type == 'LIGHT']:
        bpy.data.objects.remove(o)
    az = (cam_az_deg + 215.0) * DEG
    el = 20.0 * DEG if not night else 35 * DEG
    ld = bpy.data.lights.new('Sun', 'SUN')
    ld.energy = 5.5 if not night else 0.06
    ld.color = hexc('#ffcf9a')[:3] if not night else hexc('#9fb0ff')[:3]
    ld.angle = 1.2 * DEG
    ob = bpy.data.objects.new('Sun', ld)
    bpy.context.scene.collection.objects.link(ob)
    d = Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))
    ob.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    return ob


def ground(hole):
    mat = bpy.data.materials.new('PreviewGround')
    mat.use_nodes = True
    nt = mat.node_tree
    bs = nt.nodes['Principled BSDF']
    noise = nt.nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value = 0.004
    noise.inputs['Detail'].default_value = 8
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nt.links.new(tc.outputs['Object'], noise.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = hexc('#56683f')
    ramp.color_ramp.elements[1].color = hexc('#7a8458')
    nt.links.new(noise.outputs['Fac'], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value = 0.95
    mb = G.MB()
    sb = G.SB(0, 0, 50.0, 12.5)
    old = G.CTX.vmul
    G.CTX.vmul = 400.0
    G.revolve(mb, [(6000.0, -0.02, sb), (max(hole, 0.0), -0.02, None)], 128)
    G.CTX.vmul = old
    ob = G.to_object(mb, 'PreviewGround', [mat])
    return ob


def ground_grid(pits, S, n, half, hole):
    """Square tiles of side S (centres on a grid, offset by S/2 when half) with a round hole where a pit centre
    falls, plus a far ring of ground outside. Used when a pit-type asset is shown several times."""
    ground(0.0)
    g = bpy.data.objects['PreviewGround']
    bpy.data.objects.remove(g)
    mat = bpy.data.materials['PreviewGround']
    sb = G.SB(0, 0, 50.0, 12.5)
    old = G.CTX.vmul
    G.CTX.vmul = 400.0
    mb = G.MB()
    k = 1.0 / (math.sqrt(2.0) - 1.0)
    rng = [i + (0.5 if half else 0.0) for i in range(-n, n + (0 if half else 1))]
    for ix in rng:
        for iy in rng:
            c = Vector((ix * S, iy * S, 0))
            h = hole if any((Vector(p) - c).length < 1.0 for p in pits) else 0.0
            sq = lambda th: (1.0 / max(abs(math.cos(th)), abs(math.sin(th))) - 1.0) * k
            G.revolve(mb, [(S / 2, -0.02, sb), (h, -0.02, None)], 64, alt=[(S / 2 * math.sqrt(2.0), -0.02), (h, -0.02)], morph=sq,
                      xf=G.xform(0, (c.x, c.y, 0)))
    ext = (max(abs(v) for v in rng) + 0.5) * S
    G.revolve(mb, [(9000.0, -0.3, sb), (ext * 0.999, -0.3, None)], 128)
    G.CTX.vmul = old
    return G.to_object(mb, 'PreviewGround', [mat])


def figure(pos):
    # verifier fix: stand the figure on whatever surface is under (x, y) (it was buried in the Ear's berm and
    # hidden behind a chimney pedestal on the Choir, so most 40 m views showed no scale figure at all)
    dg = bpy.context.evaluated_depsgraph_get()
    hit, loc, _n, _i, _o, _m = bpy.context.scene.ray_cast(dg, Vector((pos.x, pos.y, pos.z + 60.0)), Vector((0, 0, -1)))
    if hit:
        pos = Vector((pos.x, pos.y, loc.z))
    mat = bpy.data.materials.new('PreviewFigure')
    mat.use_nodes = True
    mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = hexc('#b04030')
    mb = G.MB()
    sb = G.SB(0, 0, 1.0, 1.0)
    G.revolve(mb, [(0.0, 0.0, sb), (0.22, 0.05, sb), (0.24, 1.45, sb), (0.12, 1.52, sb), (0.12, 1.58, sb), (0.11, 1.8, sb), (0.0, 1.8, None)], 10,
              xf=G.xform(0, (pos.x, pos.y, pos.z)))
    return G.to_object(mb, 'PreviewFigure18m', [mat])


def tree_mats():
    out = {}
    for name, c1, c2 in (('leaf', '#3f5a2a', '#6f8a40'), ('cypress', '#2e4424', '#4f6a35'), ('bark', '#4a3a2a', '#5a4a38')):
        m = bpy.data.materials.new('PreviewTree_' + name)
        m.use_nodes = True
        nt = m.node_tree
        bs = nt.nodes['Principled BSDF']
        vor = nt.nodes.new('ShaderNodeTexVoronoi')
        vor.inputs['Scale'].default_value = 3.5
        ramp = nt.nodes.new('ShaderNodeValToRGB')
        ramp.color_ramp.elements[0].color = hexc(c1)
        ramp.color_ramp.elements[1].color = hexc(c2)
        nt.links.new(vor.outputs['Distance'], ramp.inputs[0])
        nt.links.new(ramp.outputs[0], bs.inputs['Base Color'])
        bs.inputs['Roughness'].default_value = 0.85
        bump = nt.nodes.new('ShaderNodeBump')
        bump.inputs['Strength'].default_value = 0.6
        nt.links.new(vor.outputs['Distance'], bump.inputs['Height'])
        nt.links.new(bump.outputs[0], bs.inputs['Normal'])
        out[name] = m
    return out


_TREE_MESH = {}


def tree_mesh(kind, mats):
    if kind in _TREE_MESH:
        return _TREE_MESH[kind]
    mb = G.MB()
    sbl = G.SB(0, 0, 4.0, 1.0)
    sbt = G.SB(1, 0, 4.0, 1.0)
    if 'cypress' in kind:
        G.tube(mb, sbt, [(0, 0, 0), (0, 0, 2.0)], 0.3, sides=5)
        G.blob(mb, sbl, (0, 0, 7.5), 2.0, 3, sub=2, rough=0.12, stretch=(1, 1, 3.4))
    elif 'small' in kind:
        G.tube(mb, sbt, [(0, 0, 0), (0, 0, 2.0)], 0.2, sides=5)
        G.blob(mb, sbl, (0, 0, 3.4), 2.2, 5, sub=2, rough=0.22, squash=0.85)
    else:
        G.tube(mb, sbt, [(0, 0, 0), (0, 0, 3.5)], 0.35, sides=5)
        G.blob(mb, sbl, (0, 0, 6.2), 3.6, 7, sub=2, rough=0.25, squash=0.8)
        G.blob(mb, sbl, (1.6, 0.8, 7.6), 2.4, 8, sub=1, rough=0.25)
    ob = G.to_object(mb, 'PreviewTreeProto_' + kind, [mats['cypress' if 'cypress' in kind else 'leaf'], mats['bark']])
    ob.hide_render = True
    _TREE_MESH[kind] = ob.data
    return ob.data


def place_trees(sockets, offset=Vector((0, 0, 0)), coll=None):
    mats = tree_mats() if 'PreviewTree_leaf' not in bpy.data.materials else {k: bpy.data.materials['PreviewTree_' + k] for k in ('leaf', 'cypress', 'bark')}
    rnd = random.Random(5)
    for i, t in enumerate(sockets):
        me = tree_mesh(t.get('kind', 'broadleaf'), mats)
        ob = bpy.data.objects.new('PreviewTree', me)
        ob.location = Vector(t['pos']) + offset
        ob.rotation_euler = (0, 0, rnd.uniform(0, 6.28))
        s = rnd.uniform(0.8, 1.2)
        ob.scale = (s, s, s)
        (coll or bpy.context.scene.collection).objects.link(ob)


def steam(sockets, offset=Vector((0, 0, 0)), coll=None):
    """Preview-only volumetric plumes leaning together downwind."""
    mat = bpy.data.materials.get('PreviewSteam')
    if mat is None:
        mat = bpy.data.materials.new('PreviewSteam')
        mat.use_nodes = True
        nt = mat.node_tree
        for n in list(nt.nodes):
            nt.nodes.remove(n)
        out = nt.nodes.new('ShaderNodeOutputMaterial')
        vol = nt.nodes.new('ShaderNodeVolumePrincipled')
        vol.inputs['Color'].default_value = hexc('#f0ece6')
        noise = nt.nodes.new('ShaderNodeTexNoise')
        noise.inputs['Scale'].default_value = 0.035
        noise.inputs['Detail'].default_value = 6
        tc = nt.nodes.new('ShaderNodeTexCoord')
        nt.links.new(tc.outputs['Object'], noise.inputs['Vector'])
        mr = nt.nodes.new('ShaderNodeMapRange')
        mr.inputs['From Min'].default_value = 0.42
        mr.inputs['From Max'].default_value = 0.75
        mr.inputs['To Max'].default_value = 0.09
        nt.links.new(noise.outputs['Fac'], mr.inputs['Value'])
        nt.links.new(mr.outputs['Result'], vol.inputs['Density'])
        vol.inputs['Anisotropy'].default_value = 0.4
        nt.links.new(vol.outputs[0], out.inputs['Volume'])
    wind = Vector((0.55, 0.85, 0)).normalized()
    sb = G.SB(0, 0, 10.0, 10.0)
    for i, s in enumerate(sockets):
        p0 = Vector(s['pos']) + offset
        mb = G.MB()
        pts, radii = [], []
        for k in range(14):
            t = k / 13.0
            z = p0.z + 260.0 * t
            drift = wind * (210.0 * t ** 1.5) + (Vector((0, 0, 0)) - Vector((p0.x, p0.y, 0))) * 0.35 * t
            pts.append(tuple(Vector((p0.x, p0.y, z)) + drift))
            radii.append(2.6 + 42.0 * t ** 1.2)
        G.tube(mb, sb, pts, 2.6, sides=10, caps=True, radii=radii)
        ob = G.to_object(mb, 'PreviewSteam', [mat], collection=coll)


# ------------------------------------------------------------------------------------------ asset placement

def manifest():
    return json.load(open(os.path.join(HERE, 'manifest.json')))


def mesh_ob(name):
    return bpy.data.objects.get(name + '_LOD0')


def place(name, offset=Vector((0, 0, 0)), mats=None, coll=None):
    src = mesh_ob(name)
    ob = bpy.data.objects.new(name + '_preview', src.data)
    ob.location = offset
    if mats:
        for i, m in enumerate(mats):
            ob.material_slots[i].link = 'OBJECT'
            ob.material_slots[i].material = m
    (coll or bpy.context.scene.collection).objects.link(ob)
    return ob


def place_ornaments(entry, culture, offset=Vector((0, 0, 0)), mats=None):
    orn = entry['culture_variants'][culture]['ornament_mesh']
    src = mesh_ob(orn)
    for t in entry['ornament_sockets']['transforms']:
        ob = bpy.data.objects.new(orn + '_inst', src.data)
        ob.location = Vector(t['pos']) + offset
        ob.rotation_euler = (0, 0, t.get('rot_z_deg', 0.0) * DEG)
        if mats:
            for i, m in enumerate(mats):
                if i < len(ob.material_slots):
                    ob.material_slots[i].link = 'OBJECT'
                    ob.material_slots[i].material = m
        bpy.context.scene.collection.objects.link(ob)


def place_extras(asset, state, offset=Vector((0, 0, 0))):
    if asset == 'ladder':
        place('SM_Wonder_Ladder_Tether_%s' % state, offset)
        if state == 'Intact':
            src = mesh_ob('SM_Wonder_Ladder_Climber')
            for z in (160.0, 520.0, 1100.0):
                ob = bpy.data.objects.new('Climber', src.data)
                ob.location = offset + Vector((0, 0, z))
                bpy.context.scene.collection.objects.link(ob)
    if asset == 'sunwell' and state == 'Intact':
        place('SM_Wonder_Sunwell_Beam', offset)


def hide_sources():
    for o in bpy.data.objects:
        if o.type in ('MESH', 'EMPTY') and not o.name.endswith('_preview'):
            o.hide_render = True
            o.hide_viewport = True      # also out of the depsgraph, so the figure ray cast only sees placed meshes


def set_emission(night):
    for m in bpy.data.materials:
        if m.node_tree and 'EmStrength' in m.node_tree.nodes:
            v = m.get('dm_EmissiveNight' if night else 'dm_EmissiveDay', 0.0)
            m.node_tree.nodes['EmStrength'].inputs[1].default_value = v
        if m.name == 'M_Wonder_LightShaft' and 'Em' in m.node_tree.nodes:
            m.node_tree.nodes['Em'].inputs['Strength'].default_value = 10.0 if night else 6.0


def camera(loc, target, lens):
    cd = bpy.data.cameras.get('PreviewCam') or bpy.data.cameras.new('PreviewCam')
    cd.lens = lens
    cd.sensor_width = 36.0
    cd.clip_start = 0.5
    cd.clip_end = 20000.0
    ob = bpy.data.objects.get('PreviewCam') or bpy.data.objects.new('PreviewCam', cd)
    if ob.name not in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = (target - loc).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.camera = ob
    return ob


def orbit(target, dist, az_deg, el_deg):
    az, el = az_deg * DEG, el_deg * DEG
    return target + Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el))) * dist


def render(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('wrote', path)


def clear_preview():
    for o in list(bpy.data.objects):
        if o.name.endswith('_preview') or o.name.startswith(('PreviewTree', 'PreviewSteam', 'PreviewGround', 'PreviewFigure', 'Climber')) or o.name.endswith('_inst'):
            if not o.name.startswith('PreviewTreeProto'):
                bpy.data.objects.remove(o)


def main():
    o = args()
    asset = o['asset']
    task_id, token = D.ASSETS[asset][0], D.ASSETS[asset][1]
    man = manifest()['meshes']
    cfg = CFG[asset]
    setup_render(o['samples'])
    hide_sources()
    intact = 'SM_Wonder_%s_Intact' % token
    entry = man[intact]
    os.makedirs(SHOTS, exist_ok=True)

    def shot(view):
        return os.path.join(SHOTS, 'asset-wonders-b-%s-%s%s.png' % (task_id, view, o['suffix']))

    def single_scene(state='Intact', trees=True):
        clear_preview()
        name = 'SM_Wonder_%s_%s' % (token, state)
        place(name)
        e = man[name]
        if e['ornament_sockets']['transforms']:
            place_ornaments(e, 'A')
        place_extras(asset, state)
        if trees:
            place_trees(e['tree_sockets'])
        if asset == 'choir':
            steam([f for f in e['fx_sockets'] if f['kind'] == 'steam_plume'])
        ground(cfg['ground_hole'])

    R, zc = cfg['R'], cfg['zc']
    centre = Vector((0, 0, zc))
    for view in o['views']:
        night = view == 'night'
        if view in ('40m', '300m', '1km', 'night'):
            single_scene()
            world(night=night, haze=view != '40m')
            set_emission(night)
            if view == '40m':
                tgt, az, el = cfg['near']
                figure(cfg['fig'])
                cam_loc = orbit(tgt, 40.0, az, el)
                camera(cam_loc, tgt, 24.0)
                sun_for(az)
            elif view in ('300m', 'night'):
                az, el = -112.0, 32.0
                dist = 300.0
                hf = 2 * math.atan(1.45 * R / dist)
                lens = max(16.0, min(50.0, 18.0 / math.tan(hf / 2)))
                camera(orbit(centre, dist + R * 0.35, az, el), centre, lens)
                sun_for(az, night)
            else:
                az, el = -112.0, 38.0
                camera(orbit(centre, 1000.0, az, el), centre, 35.0)
                sun_for(az)
            render(shot(view))
        elif view == 'states':
            clear_preview()
            S = 2.25 * R
            offs = [Vector((-S / 2, S / 2, 0)), Vector((S / 2, S / 2, 0)), Vector((-S / 2, -S / 2, 0)), Vector((S / 2, -S / 2, 0))]
            for st, off in zip(D.STATES, offs):
                name = 'SM_Wonder_%s_%s' % (token, st)
                place(name, off)
                e = man[name]
                if e['ornament_sockets']['transforms']:
                    place_ornaments(e, 'A', off)
                place_extras(asset, st, off)
            hole = cfg['ground_hole']
            if hole:
                ground_grid([tuple(v) for v in offs], S, 3, True, hole)
            else:
                ground(0.0)
            world(haze=True)
            set_emission(False)
            az, el = -100.0, 42.0
            camera(orbit(Vector((0, 0, zc * 0.5)), 2.3 * S + 300, az, el), Vector((0, 0, zc * 0.5)), 35.0)
            sun_for(az)
            render(shot('states'))
        elif view == 'cultures':
            clear_preview()
            S = 2.3 * R
            mats = {c: [bpy.data.materials['MI_Wonder_%s%s_%s' % (token, sl, c)] for sl in D.SLOTS] for c in D.CULTURES}
            for i, c in enumerate(D.CULTURES):
                off = Vector(((i - 1) * S, 0, 0))
                place(intact, off, mats[c])
                place_ornaments(entry, c, off, mats[c])
                place_extras(asset, 'Intact', off)
            if cfg['ground_hole']:
                ground_grid([((i - 1) * S, 0.0, 0.0) for i in range(3)], S, 3, False, cfg['ground_hole'])
            else:
                ground(0.0)
            world(haze=True)
            set_emission(False)
            az, el = -95.0, 30.0
            camera(orbit(Vector((0, 0, zc)), 1.75 * S + 250, az, el), Vector((0, 0, zc)), 35.0)
            sun_for(az)
            render(shot('cultures'))


main()
