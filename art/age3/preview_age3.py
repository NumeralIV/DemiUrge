# DEMIURGE art - Age3 previews. Opens art/age3/source/age3.blend and renders, per asset, the three views of
# docs/ASSETS.md (40 m three-quarter, 300 m oblique, 1 km oblique; EEVEE 1280 x 720, golden-hour sun at 20
# degrees, cool sky fill, neutral ground) plus review sheets. Run it through the render slot tool:
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       art/age3/source/age3.blend --python art/age3/preview_age3.py -- [--assets A,B] [--views 40m,300m,1km]
#       [--sheets states,cultures,night,town] [--samples 32]
#
# The 300 m and 1 km views put the asset into a small harbour town assembled from the kit, and every instance
# shows the LOD that Unreal would pick at that distance (screen sizes 1.0 / 0.35 / 0.12 / 0.04).
import math
import os
import random
import sys

import bpy
from mathutils import Vector, Euler

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'shots')
sys.path.insert(0, HERE)
WATER_Z = -1.8
SCREEN = [1.0, 0.35, 0.12, 0.04]
ASSETS = ['Townhouse_A', 'Townhouse_B', 'Townhouse_C', 'Townhouse_D', 'Warehouse_A', 'Warehouse_B', 'Quay_Straight', 'Quay_Corner',
          'Quay_Stairs', 'Pier', 'PortCrane', 'Lighthouse', 'CivicDome']
DOCKS = {'Quay_Straight', 'Quay_Corner', 'Quay_Stairs', 'Pier'}


def mesh(asset, state='Intact', lod=0):
    return bpy.data.meshes.get('SM_Age3_%s_%s_LOD%d' % (asset, state, lod))


def radius(asset):
    me = mesh(asset)
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    c = Vector(((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, (max(zs) + max(min(zs), 0)) / 2))
    r = max((Vector((x, y, z)) - c).length for x, y, z in ((max(xs), max(ys), max(zs)), (min(xs), min(ys), max(zs)), (max(xs), min(ys), min(zs)), (min(xs), max(ys), max(0, min(zs)))))
    return c, r


# ----------------------------------------------------------------------------------------------- scene setup
def setup_scene(samples):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    try:
        sc.eevee.taa_render_samples = samples
        sc.eevee.use_shadows = True
        sc.eevee.use_raytracing = True
        sc.eevee.ray_tracing_options.resolution_scale = '2'
    except Exception:
        pass
    sc.view_settings.view_transform = 'AgX'
    try:
        sc.view_settings.look = 'AgX - Medium High Contrast'
    except Exception:
        pass
    sc.view_settings.exposure = 0.0
    # world: cool sky fill for lighting, a warm-to-blue gradient for what the camera sees
    w = bpy.data.worlds.get('Age3Sky') or bpy.data.worlds.new('Age3Sky')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    N, L = nt.nodes, nt.links
    out = N.new('ShaderNodeOutputWorld')
    fill = N.new('ShaderNodeBackground')
    fill.inputs[0].default_value = (0.30, 0.40, 0.58, 1)
    fill.inputs[1].default_value = 0.6
    tc = N.new('ShaderNodeTexCoord')
    sep = N.new('ShaderNodeSeparateXYZ')
    L.new(tc.outputs['Generated'], sep.inputs[0])
    ramp = N.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.45
    ramp.color_ramp.elements[0].color = (0.95, 0.72, 0.48, 1)
    ramp.color_ramp.elements[1].position = 0.75
    ramp.color_ramp.elements[1].color = (0.32, 0.45, 0.68, 1)
    norm_ = N.new('ShaderNodeVectorMath')
    norm_.operation = 'NORMALIZE'
    L.new(tc.outputs['Generated'], norm_.inputs[0])
    sep2 = N.new('ShaderNodeSeparateXYZ')
    L.new(tc.outputs['Window'], sep2.inputs[0])
    L.new(sep2.outputs['Y'], ramp.inputs[0])
    skyc = N.new('ShaderNodeBackground')
    L.new(ramp.outputs[0], skyc.inputs[0])
    skyc.inputs[1].default_value = 1.0
    lp = N.new('ShaderNodeLightPath')
    mix = N.new('ShaderNodeMixShader')
    L.new(lp.outputs['Is Camera Ray'], mix.inputs[0])
    L.new(fill.outputs[0], mix.inputs[1])
    L.new(skyc.outputs[0], mix.inputs[2])
    L.new(mix.outputs[0], out.inputs[0])
    sun = bpy.data.objects.get('Sun')
    if not sun:
        ld = bpy.data.lights.new('Sun', 'SUN')
        sun = bpy.data.objects.new('Sun', ld)
        sc.collection.objects.link(sun)
    sun.data.energy = 6.5
    sun.data.color = (1.0, 0.76, 0.52)
    sun.data.angle = math.radians(1.2)
    cam = bpy.data.objects.get('Cam')
    if not cam:
        cd = bpy.data.cameras.new('Cam')
        cam = bpy.data.objects.new('Cam', cd)
        sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.clip_start = 0.5
    cam.data.clip_end = 20000
    return sun, cam


def flat_mat(name, col, rough=0.9, spec=0.3, noise=None):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get('Principled BSDF')
    b.inputs['Base Color'].default_value = col + (1,)
    b.inputs['Roughness'].default_value = rough
    if noise:
        N, L = m.node_tree.nodes, m.node_tree.links
        tc = N.new('ShaderNodeTexCoord')
        nz = N.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = noise
        L.new(tc.outputs['Object'], nz.inputs['Vector'])
        mx = N.new('ShaderNodeMix')
        mx.data_type = 'RGBA'
        mx.blend_type = 'MULTIPLY'
        mx.inputs['Factor'].default_value = 0.5
        mx.inputs['A'].default_value = col + (1,)
        L.new(nz.outputs['Color'], mx.inputs['B'])
        L.new(mx.outputs['Result'], b.inputs['Base Color'])
    return m


def plane(name, x0, y0, x1, y1, z, mat, col):
    me = bpy.data.meshes.new(name)
    me.from_pydata([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], [], [(0, 1, 2, 3)])
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    col.objects.link(ob)
    return ob


def quad(name, pts, mat, col):
    me = bpy.data.meshes.new(name)
    me.from_pydata(pts, [], [tuple(range(len(pts)))])
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    col.objects.link(ob)
    return ob


def new_col(name):
    c = bpy.data.collections.get(name)
    if c:
        for o in list(c.objects):
            bpy.data.objects.remove(o)
    else:
        c = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(c)
    return c


def hide_kit():
    for c in bpy.data.collections:
        if c.name == 'Age3' or (c.name in ASSETS):
            for lc in bpy.context.view_layer.layer_collection.children:
                if lc.name == 'Age3':
                    lc.exclude = True


def ground_mats():
    g = flat_mat('Ground', (0.20, 0.19, 0.15), 0.95, noise=0.02)
    pav = flat_mat('Paving', (0.36, 0.32, 0.26), 0.9, noise=0.08)
    w = bpy.data.materials.get('Water') or bpy.data.materials.new('Water')
    w.use_nodes = True
    b = w.node_tree.nodes.get('Principled BSDF')
    b.inputs['Base Color'].default_value = (0.015, 0.045, 0.06, 1)
    b.inputs['Roughness'].default_value = 0.06
    N, L = w.node_tree.nodes, w.node_tree.links
    tc = N.new('ShaderNodeTexCoord')
    nz = N.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = 0.6
    bm_ = N.new('ShaderNodeBump')
    bm_.inputs['Strength'].default_value = 0.06
    L.new(tc.outputs['Object'], nz.inputs['Vector'])
    L.new(nz.outputs['Fac'], bm_.inputs['Height'])
    L.new(bm_.outputs['Normal'], b.inputs['Normal'])
    return g, pav, w


# ----------------------------------------------------------------------------------------------- hero (asset alone)
def build_hero(asset, state='Intact', lod=0, x=0.0, col=None, rot=0.0, culture=None, name=None):
    col = col or new_col('Hero')
    me = mesh(asset, state, lod)
    if culture:
        me = culture_copy(me, culture)
    ob = bpy.data.objects.new(name or ('H_%s_%s' % (asset, state)), me)
    ob.location = (x, 0, 0)
    ob.rotation_euler = (0, 0, rot)
    col.objects.link(ob)
    return ob


_cul_cache = {}


def culture_copy(me, culture):
    key = (me.name, culture)
    if key in _cul_cache:
        return _cul_cache[key]
    m2 = me.copy()
    for i, m in enumerate(m2.materials):
        slot = m.name.split('_')[2]
        mi = bpy.data.materials.get('MI_Age3_%s_%s' % (slot, culture))
        if mi:
            m2.materials[i] = mi
    _cul_cache[key] = m2
    return m2


def hero_env(asset, col, g, w, extent=400):
    if asset in DOCKS:
        back = {'Quay_Straight': 5.0, 'Quay_Corner': 5.0, 'Quay_Stairs': 5.0, 'Pier': 12.0}[asset]
        x1 = 5.0 if asset == 'Quay_Corner' else extent
        plane('G', -extent, back, extent, extent, -0.03, g, col)
        if asset == 'Quay_Corner':
            plane('G2', 5.0, 5.0, extent, extent, -0.03, g, col)
        if asset in ('Quay_Straight', 'Quay_Stairs'):
            plane('G3', -extent, -5.0, -10.0, back, -0.03, g, col)
            plane('G4', 10.0, -5.0, extent, back, -0.03, g, col)
        plane('W', -extent, -extent, extent, extent, WATER_Z, w, col)
        quad('Bed', [(-extent, -40, -6), (extent, -40, -6), (extent, back, -6), (-extent, back, -6)], g, col)
    else:
        plane('G', -extent, -extent, extent, extent, -0.03, g, col)


def figure(col, cr, asset):
    """A 1.8 m scale figure standing in front of the asset's left front corner."""
    me = bpy.data.meshes.get('Figure1p8')
    if not me:
        import bmesh
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=0.22, radius2=0.18, depth=1.5)
        bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.75))
        n = len(bm.verts)
        bmesh.ops.create_uvsphere(bm, u_segments=8, v_segments=6, radius=0.13)
        bmesh.ops.translate(bm, verts=bm.verts[n:], vec=(0, 0, 1.67))
        me = bpy.data.meshes.new('Figure1p8')
        bm.to_mesh(me)
        me.materials.append(flat_mat('FigureRed', (0.5, 0.06, 0.04), 0.7))
    c, r = cr
    mo = bpy.data.objects.get(next(iter(o.name for o in col.objects if o.name.startswith('H_')), ''))
    xs = [v.co.x for v in mo.data.vertices]
    ys = [v.co.y for v in mo.data.vertices]
    ob = bpy.data.objects.new('Figure', me)
    # verifier fix: in front of the facade, right of centre (it stood off the left corner, cut by the frame edge)
    ob.location = ((min(xs) + max(xs)) / 2 + 0.2 * (max(xs) - min(xs)), min(ys) - 1.2, 0.0) if asset not in DOCKS \
        else (min(xs) + 3.0, max(ys) - 3.0, 0.0)
    col.objects.link(ob)


# ----------------------------------------------------------------------------------------------- the harbour town
TOWN = []   # (asset, x, y, rot)


def town_layout():
    rng = random.Random(7)
    T = []
    # quay frontage along y = -20 (front face), x from -140 to 80, a stair segment and a crane
    for i, x in enumerate(range(-130, 80, 20)):
        T.append(('Quay_Stairs' if x == -50 else 'Quay_Straight', x, -15.0, 0.0))
    T.append(('Quay_Corner', 85.0, -15.0, 0.0))
    # the mole: two rows back to back running south from the corner, closed at the end
    for y in (-30, -50, -70, -90, -110):
        T.append(('Quay_Straight', 95.0, float(y), math.pi / 2))
        T.append(('Quay_Straight', 85.0, float(y), -math.pi / 2))
    T.append(('Quay_Straight', 85.0, -125.0, 0.0))
    T.append(('Lighthouse', 85.0, -122.0, 0.0))
    # piers into the basin
    T.append(('Pier', -10.0, -32.0, 0.0))
    T.append(('Pier', 40.0, -32.0, 0.0))
    T.append(('PortCrane', 18.0, -16.0, 0.0))
    T.append(('PortCrane', -95.0, -16.0, 0.0))
    # warehouses behind the quay apron
    T.append(('Warehouse_A', -30.0, -1.0, 0.0))
    T.append(('Warehouse_B', 6.0, 1.0, 0.0))
    T.append(('Warehouse_A', 40.0, -1.0, 0.0))
    T.append(('Warehouse_B', -62.0, 1.0, 0.0))
    T.append(('Warehouse_A', -100.0, -1.0, 0.0))
    # civic dome on a square
    T.append(('CivicDome', 0.0, 88.0, 0.0))
    # town blocks: streets every 30 m north, cross streets every ~44 m
    widths = {'Townhouse_A': 6.6, 'Townhouse_B': 10.0, 'Townhouse_C': 9.0, 'Townhouse_D': 13.0}
    depths = {'Townhouse_A': 11.0, 'Townhouse_B': 10.0, 'Townhouse_C': 8.5, 'Townhouse_D': 13.0}
    kinds = list(widths)
    for by in (22.0, 50.0, 128.0, 156.0, 184.0):
        for bx0 in (-150.0, -104.0, -58.0, -12.0, 34.0, 80.0):
            if 40 < by < 140 and -30 < bx0 < 30:
                continue          # the square around the dome
            for side, rot in ((0, 0.0), (1, math.pi)):
                x = bx0 + 1.0
                while x < bx0 + 40.0:
                    k = rng.choice(kinds)
                    w_ = widths[k]
                    if x + w_ > bx0 + 41.0:
                        break
                    d = depths[k]
                    y = by - 13.0 + d / 2 if side == 0 else by + 13.0 - d / 2
                    T.append((k, x + w_ / 2, y, rot))
                    x += w_ + rng.choice((0.0, 0.0, 0.5, 1.5))
    # dome square flanks
    for x in (-38.0, 38.0):
        for y in (70.0, 88.0, 106.0):
            k = rng.choice(kinds)
            T.append((k, x, y, math.pi / 2 if x > 0 else -math.pi / 2))
    return T


def build_town(col, g, pav, w):
    global TOWN
    TOWN = town_layout()
    objs = []
    for i, (a, x, y, r) in enumerate(TOWN):
        if not mesh(a):
            continue
        ob = bpy.data.objects.new('T%03d_%s' % (i, a), mesh(a, 'Intact', 0))
        ob.location = (x, y, 0)
        ob.rotation_euler = (0, 0, r)
        col.objects.link(ob)
        objs.append((ob, a))
    # land north of the quay line, east of the mole root; water everywhere; sloped banks west
    plane('TG', -3000, -20.0, 3000, 3000, -0.03, g, col)
    plane('TP', -145, -10.0, 85, 205, -0.02, pav, col)
    plane('TW', -3000, -3000, 3000, 3000, WATER_Z, w, col)
    quad('TBank', [(-3000, -32, -5.0), (-140, -32, -5.0), (-140, -20, -0.03), (-3000, -20, -0.03)], g, col)
    quad('TBank2', [(100, -32, -5.0), (3000, -32, -5.0), (3000, -20, -0.03), (100, -20, -0.03)], g, col)
    quad('TBed', [(-3000, -3000, -7.0), (3000, -3000, -7.0), (3000, -20, -7.0), (-3000, -20, -7.0)], g, col)
    return objs


def pick_lods(objs, cam, vfov):
    for ob, a in objs:
        c, r = RAD[a]
        wc = ob.matrix_world @ c
        d = (wc - cam.location).length
        ss = (2 * r) / max(1e-3, 2 * d * math.tan(vfov / 2))
        lod = 0 if ss > SCREEN[1] else (1 if ss > SCREEN[2] else (2 if ss > SCREEN[3] else 3))
        ob.data = mesh(a, 'Intact', lod)


# ----------------------------------------------------------------------------------------------- cameras and sun
def aim(cam, target, dist, azim_deg, elev_deg, lens=50.0):
    a = math.radians(azim_deg)
    e = math.radians(elev_deg)
    d = Vector((math.cos(e) * math.sin(a), -math.cos(e) * math.cos(a), math.sin(e)))
    cam.location = Vector(target) + d * dist
    cam.data.lens = lens
    look = (Vector(target) - cam.location).normalized()
    cam.rotation_euler = look.to_track_quat('-Z', 'Y').to_euler()


def place_sun(sun, azim_deg):
    # sun 20 degrees up, coming from the camera's left and a little behind the subject
    a = math.radians(azim_deg - 115.0)
    e = math.radians(20.0)
    d = Vector((math.cos(e) * math.sin(a), -math.cos(e) * math.cos(a), math.sin(e)))
    sun.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()


def render(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('AGE3SHOT', path, flush=True)


RAD = {}


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    assets = ASSETS
    views = ['40m', '300m', '1km']
    sheets = []
    samples = 32
    for i, a in enumerate(argv):
        if a == '--assets':
            assets = argv[i + 1].split(',')
        if a == '--views':
            views = [v for v in argv[i + 1].split(',') if v]
        if a == '--sheets':
            sheets = [v for v in argv[i + 1].split(',') if v]
        if a == '--samples':
            samples = int(argv[i + 1])
    sun, cam = setup_scene(samples)
    hide_kit()
    g, pav, w = ground_mats()
    for a in ASSETS:
        if mesh(a):
            RAD[a] = radius(a)
    vfov = 2 * math.atan(36.0 * 720 / 1280 / 2 / 50.0)
    vfov35 = 2 * math.atan(36.0 * 720 / 1280 / 2 / 35.0)
    hero = new_col('Hero')
    town = new_col('Town')
    tobjs = build_town(town, g, pav, w)
    lc = {c.name: c for c in bpy.context.view_layer.layer_collection.children}
    hero_pos = {}
    for ob, a in tobjs:
        hero_pos.setdefault(a, ob)
    for ob, a in tobjs:
        if a == 'Townhouse_A' and ob.location.y < 40:
            hero_pos['Townhouse_A'] = ob
            break
    for asset in assets:
        if asset not in RAD:
            continue
        c, r = RAD[asset]
        for v in views:
            if v != '40m' and asset not in hero_pos:
                continue
            if v == '40m':
                lc['Hero'].exclude = False
                lc['Town'].exclude = True
                new_col('Hero')
                build_hero(asset)
                hero_env(asset, bpy.data.collections['Hero'], g, w)
                # 40 m from the subject with a 35 mm lens; big buildings back off until they fit the frame
                # 40 m from the subject, lens chosen to fit; big buildings back off until a 24 mm lens fits them
                vf24 = 2 * math.atan(10.125 / 24.0)
                dist = max(40.0, r * 1.1 / math.tan(vf24 / 2))
                lens = max(24.0, min(85.0, 10.125 / (r * 1.1 / dist)))
                az = -35.0 if asset != 'Pier' else -55.0
                aim(cam, c, dist, az, 24.0 if asset not in DOCKS else 30.0, lens)
                figure(bpy.data.collections['Hero'], RAD[asset], asset)
                place_sun(sun, az)
            else:
                lc['Hero'].exclude = True
                lc['Town'].exclude = False
                hob = hero_pos[asset]
                tgt = hob.matrix_world @ c
                dist = 300.0 if v == '300m' else 1000.0
                aim(cam, tgt, dist, -30.0, 40.0, 50.0)
                place_sun(sun, -30.0)
                pick_lods(tobjs, cam, vfov)
            render(os.path.join(SHOTS, 'asset-age3-%s-%s.png' % (asset.lower().replace('_', '-'), v)))
    if 'states' in sheets:
        for asset in assets:
            if asset not in RAD:
                continue
            lc['Hero'].exclude = False
            lc['Town'].exclude = True
            col = new_col('Hero')
            c, r = RAD[asset]
            states = ['Intact', 'Construction', 'Damaged', 'Ruin'] if asset not in DOCKS else ['Intact', 'Damaged', 'Ruin']
            step = r * 2.3
            for i, s in enumerate(states):
                if mesh(asset, s):
                    build_hero(asset, s, 0, x=(i - (len(states) - 1) / 2) * step, col=col)
            hero_env(asset, col, g, w, extent=2000)
            span = step * len(states)
            dist = max(40.0, span * 0.95)
            aim(cam, (c.x, c.y, c.z * 0.7), dist, -25.0, 30.0, 35.0)
            place_sun(sun, -25.0)
            render(os.path.join(SHOTS, 'asset-age3-%s-states.png' % asset.lower().replace('_', '-')))
    if 'cultures' in sheets:
        lc['Hero'].exclude = False
        lc['Town'].exclude = True
        col = new_col('Hero')
        houses = ['Townhouse_A', 'Townhouse_B', 'Townhouse_C', 'Townhouse_D', 'Warehouse_B']
        cus = [None, 'CultureA', 'CultureB', 'CultureC']
        for j, cu in enumerate(cus):
            x = -36.0
            for k, a in enumerate(houses):
                ob = build_hero(a, 'Intact', 0, col=col, culture=cu, name='C_%s_%s' % (a, cu))
                ob.location = (x + RAD[a][1] * 0.6, j * 22.0, 0)
                x += RAD[a][1] * 1.5 + 4.0
        hero_env('Townhouse_A', col, g, w, extent=2000)
        aim(cam, (4.0, 33.0, 4.0), 115.0, -22.0, 38.0, 42.0)
        place_sun(sun, -22.0)
        render(os.path.join(SHOTS, 'asset-age3-cultures.png'))
    if 'night' in sheets or 'town' in sheets:
        lc['Hero'].exclude = True
        lc['Town'].exclude = False
        aim(cam, (-10.0, 40.0, 0.0), 520.0, -28.0, 38.0, 40.0)
        place_sun(sun, -28.0)
        pick_lods(tobjs, cam, vfov)
        if 'town' in sheets:
            render(os.path.join(SHOTS, 'asset-age3-town-500m.png'))
        if 'night' in sheets:
            sun.data.energy = 0.0
            bpy.data.worlds['Age3Sky'].node_tree.nodes['Background'].inputs[1].default_value = 0.03
            for m in bpy.data.materials:
                if m.name.startswith(('M_Age3_Detail', 'MI_Age3_Detail')):
                    for n in m.node_tree.nodes:
                        if n.type == 'BSDF_PRINCIPLED':
                            n.inputs['Emission Strength'].default_value = 6.0
            render(os.path.join(SHOTS, 'asset-age3-town-night.png'))


if __name__ == '__main__':
    main()
