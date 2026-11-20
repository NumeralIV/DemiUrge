# DEMIURGE art - Age2 preview rig (EEVEE, 1280x720): golden-hour sun at 20 deg from the left, cool sky
# fill, neutral ground. Views per asset: 40m (three-quarter, asset alone with a 1.8 m figure), 300m and
# 1km (high oblique over an Iron-age town built from the kit, centred on the asset). Extra sheets:
# <asset>-states (Intact / Construction / Damaged / Ruin), cultures (houses x 4 cultures), lods.
import os
import math
import random
import bpy
from mathutils import Vector, Matrix, Euler

import age2_kit as kit
import age2_assets as A
import age2_tex as tx

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'shots')

SLUG = {
    'House_A': 'house-a', 'House_B': 'house-b', 'House_C': 'house-c', 'House_D': 'house-d',
    'WallSeg': 'wall-seg', 'WallTower_A': 'wall-tower-a', 'WallTower_B': 'wall-tower-b', 'Gatehouse': 'gatehouse',
    'Market': 'market', 'Temple': 'temple', 'Academy': 'academy',
    'MarketStall_A': 'market-stall-a', 'MarketStall_B': 'market-stall-b', 'MarketStall_C': 'market-stall-c',
    'Ornament_A': 'ornament-a', 'Ornament_B': 'ornament-b', 'Ornament_C': 'ornament-c',
}


# ---------------------------------------------------------------------- scene


def setup_render(scene, samples=48):
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    ee = scene.eevee
    ee.taa_render_samples = samples
    try:
        ee.use_raytracing = True
        ee.ray_tracing_options.resolution_scale = '1'
    except Exception:
        pass
    try:
        ee.use_shadows = True
        ee.shadow_ray_count = 2
        ee.shadow_step_count = 8
        ee.shadow_resolution_scale = 1.0
    except Exception:
        pass
    try:
        ee.use_fast_gi = True
        ee.fast_gi_distance = 6.0
    except Exception:
        pass
    vs = scene.view_settings
    for vt in ('AgX', 'Filmic'):
        try:
            vs.view_transform = vt
            break
        except Exception:
            pass
    try:
        vs.look = 'AgX - Medium High Contrast'
    except Exception:
        pass
    vs.exposure = 0.0
    scene.render.image_settings.file_format = 'PNG'


def world(scene, strength=1.0):
    w = bpy.data.worlds.get('Age2Sky') or bpy.data.worlds.new('Age2Sky')
    w.use_nodes = True
    nt = w.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    # vertical gradient: warm haze at the horizon to cool blue overhead
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Generated'], sep.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = (0.95, 0.72, 0.50, 1)
    ramp.color_ramp.elements[1].position = 0.35
    ramp.color_ramp.elements[1].color = (0.30, 0.45, 0.72, 1)
    nt.links.new(sep.outputs['Z'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = strength
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])
    scene.world = w


def sun(scene, az_deg, el_deg=20.0, strength=4.2):
    ld = bpy.data.lights.get('Age2Sun') or bpy.data.lights.new('Age2Sun', 'SUN')
    ld.energy = strength
    ld.color = (1.0, 0.80, 0.58)
    ld.angle = math.radians(1.2)
    ob = bpy.data.objects.get('Age2Sun') or bpy.data.objects.new('Age2Sun', ld)
    if ob.name not in scene.collection.objects:
        scene.collection.objects.link(ob)
    az, el = math.radians(az_deg), math.radians(el_deg)
    d = Vector((math.cos(el) * math.sin(az), -math.cos(el) * math.cos(az), math.sin(el)))   # toward the sun
    ob.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    return ob


def ground_material(town_r=0.0):
    name = 'Age2Ground_%d' % int(town_r)
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bs = nt.nodes['Principled BSDF']
    bs.inputs['Roughness'].default_value = 0.95
    tc = nt.nodes.new('ShaderNodeTexCoord')
    n1 = nt.nodes.new('ShaderNodeTexNoise')
    n1.inputs['Scale'].default_value = 0.08
    n1.inputs['Detail'].default_value = 8
    nt.links.new(tc.outputs['Object'], n1.inputs['Vector'])
    n2 = nt.nodes.new('ShaderNodeTexNoise')
    n2.inputs['Scale'].default_value = 0.35
    n2.inputs['Detail'].default_value = 6
    nt.links.new(tc.outputs['Object'], n2.inputs['Vector'])
    grass = nt.nodes.new('ShaderNodeValToRGB')
    el = grass.color_ramp.elements
    el[0].position, el[0].color = 0.3, (0.085, 0.10, 0.04, 1)
    el[1].position, el[1].color = 0.75, (0.13, 0.125, 0.055, 1)
    nt.links.new(n1.outputs['Fac'], grass.inputs['Fac'])
    dirt = (0.15, 0.115, 0.08, 1)
    mixd = nt.nodes.new('ShaderNodeMix')
    mixd.data_type = 'RGBA'
    nt.links.new(grass.outputs['Color'], mixd.inputs[6])
    mixd.inputs[7].default_value = dirt
    # dirt patches + (town) beaten earth inside the walls and fields outside
    m2 = nt.nodes.new('ShaderNodeMapRange')
    m2.inputs['From Min'].default_value = 0.6
    m2.inputs['From Max'].default_value = 0.9
    m2.inputs['To Max'].default_value = 0.6
    nt.links.new(n2.outputs['Fac'], m2.inputs['Value'])
    fac = m2.outputs['Result']
    if town_r > 0:
        vl = nt.nodes.new('ShaderNodeVectorMath')
        vl.operation = 'LENGTH'
        nt.links.new(tc.outputs['Object'], vl.inputs[0])
        r1 = nt.nodes.new('ShaderNodeMapRange')
        r1.inputs['From Min'].default_value = town_r + 4
        r1.inputs['From Max'].default_value = town_r - 4
        nt.links.new(vl.outputs['Value'], r1.inputs['Value'])
        mx = nt.nodes.new('ShaderNodeMath')
        mx.operation = 'MAXIMUM'
        sc = nt.nodes.new('ShaderNodeMath')
        sc.operation = 'MULTIPLY'
        sc.inputs[1].default_value = 0.85
        nt.links.new(r1.outputs['Result'], sc.inputs[0])
        nt.links.new(sc.outputs[0], mx.inputs[0])
        nt.links.new(fac, mx.inputs[1])
        fac = mx.outputs[0]
        # fields: hedged rectangular plots (brick texture: bricks = fields, mortar = hedgerows)
        br = nt.nodes.new('ShaderNodeTexBrick')
        br.inputs['Scale'].default_value = 0.026
        br.inputs['Mortar Size'].default_value = 0.02
        br.inputs['Mortar Smooth'].default_value = 0.3
        br.inputs['Bias'].default_value = 0.0
        br.inputs['Brick Width'].default_value = 1.6
        br.inputs['Row Height'].default_value = 0.9
        br.offset = 0.37
        br.squash = 1.3
        br.inputs['Color1'].default_value = (0.08, 0.10, 0.038, 1)
        br.inputs['Color2'].default_value = (0.19, 0.16, 0.06, 1)
        br.inputs['Mortar'].default_value = (0.045, 0.06, 0.025, 1)
        mp = nt.nodes.new('ShaderNodeMapping')
        mp.inputs['Rotation'].default_value = (0, 0, 0.35)
        nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
        nt.links.new(mp.outputs['Vector'], br.inputs['Vector'])
        fr = br
        r2 = nt.nodes.new('ShaderNodeMapRange')
        r2.inputs['From Min'].default_value = town_r + 30
        r2.inputs['From Max'].default_value = town_r + 45
        nt.links.new(vl.outputs['Value'], r2.inputs['Value'])
        mf = nt.nodes.new('ShaderNodeMix')
        mf.data_type = 'RGBA'
        nt.links.new(r2.outputs['Result'], mf.inputs['Factor'])
        nt.links.new(grass.outputs['Color'], mf.inputs[6])
        nt.links.new(fr.outputs['Color'], mf.inputs[7])
        nt.links.new(mf.outputs[2], mixd.inputs[6])
    nt.links.new(fac, mixd.inputs['Factor'])
    nt.links.new(mixd.outputs[2], bs.inputs['Base Color'])
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.15
    nt.links.new(n2.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs['Normal'], bs.inputs['Normal'])
    return m


def ground(scene, size=4000.0, town_r=0.0):
    me = bpy.data.meshes.get('Age2Ground') or bpy.data.meshes.new('Age2Ground')
    me.clear_geometry()
    s = size / 2
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.get('Age2Ground') or bpy.data.objects.new('Age2Ground', me)
    if ob.name not in scene.collection.objects:
        scene.collection.objects.link(ob)
    me.materials.clear()
    me.materials.append(ground_material(town_r))
    return ob


def camera(scene, target, az_deg, el_deg, dist, lens=50.0):
    cd = bpy.data.cameras.get('Age2Cam') or bpy.data.cameras.new('Age2Cam')
    cd.lens = lens
    cd.clip_start = 0.5
    cd.clip_end = 20000
    ob = bpy.data.objects.get('Age2Cam') or bpy.data.objects.new('Age2Cam', cd)
    if ob.name not in scene.collection.objects:
        scene.collection.objects.link(ob)
    az, el = math.radians(az_deg), math.radians(el_deg)
    t = Vector(target)
    pos = t + Vector((math.cos(el) * math.sin(az), -math.cos(el) * math.cos(az), math.sin(el))) * dist
    ob.location = pos
    ob.rotation_euler = (t - pos).to_track_quat('-Z', 'Y').to_euler()
    scene.camera = ob
    return ob


def new_scene(name):
    sc = bpy.data.scenes.get(name)
    if sc:
        bpy.data.scenes.remove(sc)
    sc = bpy.data.scenes.new(name)
    bpy.context.window_manager  # noqa
    setup_render(sc)
    world(sc)
    return sc


def place(scene, me, x, y, yaw=0.0, name=None, sx=1.0):
    ob = bpy.data.objects.new(name or me.name, me)
    ob.location = (x, y, 0)
    ob.rotation_euler = (0, 0, yaw)
    ob.scale = (sx, 1, 1)
    scene.collection.objects.link(ob)
    return ob


def render(scene, path):
    scene.render.filepath = path
    win = bpy.context.window
    if win:
        win.scene = scene
    bpy.ops.render.render(write_still=True, scene=scene.name)
    print('  wrote', os.path.relpath(path, ROOT))


def figure_mesh(material_fn):
    me = bpy.data.meshes.get('Age2Figure')
    if me:
        return me
    b = kit.Builder(0, 'Intact', 1, {0: 4.0, 1: 3.0})
    b.figure(0, 0)
    me = kit.to_mesh(b, 'Age2Figure', {0: 4.0, 1: 3.0})
    me.materials.clear()
    me.materials.append(material_fn('Trim'))
    for p in me.polygons:
        p.material_index = 0
    return me


# ---------------------------------------------------------------------- town layout


def town_layout(seed=5):
    """Returns (placements, wall_radius): list of (asset, x, y, yaw, sx). Gate faces -Y at the bottom."""
    rnd = random.Random(seed)
    P = []
    R = 112.0
    civic = {'Temple': (6.0, 18.0, 0.0), 'Market': (-34.0, -22.0, 0.0), 'Academy': (44.0, -30.0, 0.0)}
    for k, (x, y, yaw) in civic.items():
        P.append((k, x, y, yaw, 1.0))
    excl = [(6.0, 18.0, 26.0), (-34.0, -22.0, 23.0), (44.0, -30.0, 30.0)]
    # wall ring: towers every ~36 m, gatehouse at the bottom (-Y)
    nt = 20
    pts = []
    for i in range(nt):
        a = -math.pi / 2 + 2 * math.pi * i / nt
        pts.append(Vector((R * math.cos(a), R * math.sin(a), 0)))
    for i in range(nt):
        p = pts[i]
        a = math.atan2(p.y, p.x)
        yaw = a + math.pi / 2     # local -Y (front) points outward
        if i == 0:
            P.append(('Gatehouse', p.x, p.y, yaw, 1.0))
            half = 13.0
        else:
            P.append(('WallTower_B' if i % 4 == 2 else 'WallTower_A', p.x, p.y, yaw, 1.0))
            half = 4.5
        q = pts[(i + 1) % nt]
        d = q - p
        Lc = d.length
        nxt_half = 13.0 if (i + 1) % nt == 0 else 4.5
        L = Lc - half - nxt_half
        n = max(1, int(round(L / 16.0)))
        seg = L / n
        dn = d.normalized()
        for k in range(n):
            c = p + dn * (half + seg * (k + 0.5))
            P.append(('WallSeg', c.x, c.y, math.atan2(dn.y, dn.x), (seg + 0.6) / 16.0))
    # houses on curving streets (rings) inside the wall
    houses = ['House_A', 'House_B', 'House_C', 'House_D']
    wts = [0.34, 0.3, 0.2, 0.16]
    r = 24.0
    while r < R - 16:
        circ = 2 * math.pi * r
        n = int(circ / 11.0)
        off = rnd.random()
        for i in range(n):
            a = 2 * math.pi * (i + off + rnd.uniform(-0.2, 0.2)) / n
            rr = r + rnd.uniform(-1.5, 1.5)
            x, y = rr * math.cos(a), rr * math.sin(a)
            if any((x - ex) ** 2 + (y - ey) ** 2 < er * er for ex, ey, er in excl):
                continue
            if abs(x) < 7.0 and y < -20:       # main street from the gate
                continue
            if abs(y - 18) < 4.0 and x > 30:   # cross street
                continue
            u = rnd.random()
            h = houses[0]
            acc = 0
            for hn, w in zip(houses, wts):
                acc += w
                if u <= acc:
                    h = hn
                    break
            yaw = a + math.pi / 2 + (math.pi if (int(r) // 12) % 2 else 0.0) + rnd.uniform(-0.12, 0.12)
            P.append((h, x, y, yaw, 1.0))
        r += 10.5 + rnd.uniform(-0.6, 0.6)
    return P, R


# ---------------------------------------------------------------------- shots

VIEW_STUDIO = dict(az=-38.0, el=24.0)


def fit_dist(bb, lens=50.0, margin=1.35):
    w = max(bb[1] - bb[0], bb[3] - bb[2])
    h = bb[5]
    hfov = 2 * math.atan(18.0 / lens)
    return max(40.0, margin * max(w, h * 1.6) / (2 * math.tan(hfov / 2)) + w * 0.5)


def render_all(rows, only, material_fn):
    os.makedirs(SHOTS, exist_ok=True)
    want = set(only or [])

    def wanted(tag):
        return not want or tag in want

    intact = {a: r['Intact'] for a, r in rows.items() if 'Intact' in r}
    # ---- studio 40 m views
    for asset, r in intact.items():
        slug = SLUG[asset]
        if not (wanted(slug) or wanted(slug + '-40m') or wanted('40m')):
            continue
        sc = new_scene('studio')
        sun(sc, VIEW_STUDIO['az'] - 70)
        ground(sc, 600)
        place(sc, r['lods'][0], 0, 0)
        bb = r['bounds']
        fig = place(sc, figure_mesh(material_fn), bb[0] - 1.2, bb[2] - 1.5, 0.0, 'figure')
        d = fit_dist(bb)
        tz = bb[5] * 0.35
        camera(sc, (0, 0, tz), VIEW_STUDIO['az'], VIEW_STUDIO['el'], d)
        render(sc, os.path.join(SHOTS, 'asset-age2-%s-40m.png' % slug))
    # ---- states sheets
    for asset, r in rows.items():
        slug = SLUG[asset]
        if len(r) < 4 or not (wanted(slug) or wanted(slug + '-states') or wanted('states')):
            continue
        sc = new_scene('states')
        sun(sc, VIEW_STUDIO['az'] - 70)
        ground(sc, 1200)
        bb = r['Intact']['bounds']
        w = (bb[1] - bb[0]) * 1.25 + 4
        for i, st in enumerate(kit.STATES):
            place(sc, r[st]['lods'][0], (i - 1.5) * w, 0)
        span = w * 4
        d = max(60.0, span / 0.72 * 1.02)
        camera(sc, (0, 0, bb[5] * 0.25), 0.0, 30.0, d, lens=50)
        render(sc, os.path.join(SHOTS, 'asset-age2-%s-states.png' % slug))
    # ---- town views
    need_town = any(wanted(SLUG[a]) or wanted(SLUG[a] + '-300m') or wanted(SLUG[a] + '-1km') for a in intact) or wanted('town') or wanted('300m') or wanted('1km')
    if need_town and all(a in intact for a in ('House_A', 'House_B', 'House_C', 'House_D', 'WallSeg', 'WallTower_A', 'WallTower_B', 'Gatehouse', 'Temple', 'Market', 'Academy')):
        sc = new_scene('town')
        sun(sc, -40 - 70 + 10)
        layout, R = town_layout()
        ground(sc, 6000, R)
        pos = {}
        for (a, x, y, yaw, sxx) in layout:
            place(sc, intact[a]['lods'][0], x, y, yaw, sx=sxx)
            pos.setdefault(a, (x, y))
        pos['MarketStall_A'] = pos['MarketStall_B'] = pos['MarketStall_C'] = pos['Market']
        for asset in intact:
            slug = SLUG[asset]
            x, y = pos.get(asset, (0, 0))
            for view, dist, el in (('300m', 300.0, 40.0), ('1km', 1000.0, 40.0)):
                if not (wanted(slug) or wanted(slug + '-' + view) or wanted(view) or wanted('town')):
                    continue
                camera(sc, (x, y, 0), -40.0, el, dist, lens=50)
                render(sc, os.path.join(SHOTS, 'asset-age2-%s-%s.png' % (slug, view)))
    # ---- cultures sheet: the four houses in four cultures
    if wanted('cultures') and all(h in intact for h in ('House_A', 'House_B', 'House_C', 'House_D')):
        sc = new_scene('cultures')
        sun(sc, VIEW_STUDIO['az'] - 70)
        ground(sc, 800)
        cults = ['Valley', 'A', 'B', 'C']
        for j, cul in enumerate(cults):
            for i, h in enumerate(('House_A', 'House_B', 'House_C', 'House_D')):
                src = intact[h]['lods'][0]
                me = src.copy()
                me.name = '%s_%s' % (src.name, cul)
                for k, m in enumerate(src.materials):
                    base = m.name.replace('M_Age2_', '')
                    me.materials[k] = material_fn(base, cul)
                ox, oy = (i - 1.5) * 15.0, (j - 1.5) * 15.0
                place(sc, me, ox, oy, 0.0)
                orn = {'A': 'Ornament_A', 'B': 'Ornament_B', 'C': 'Ornament_C'}.get(cul)
                if orn and orn in intact:
                    for (p0, p1) in intact[h].get('ridges', []):
                        p0, p1 = Vector(p0), Vector(p1)
                        d = (p1 - p0)
                        yaw = math.atan2(d.y, d.x)
                        for pp, yy in ((p0, yaw - math.pi / 2), (p1, yaw + math.pi / 2)):
                            o = place(sc, intact[orn]['lods'][0], ox + pp.x, oy + pp.y, yy)
                            o.location.z = pp.z - 0.1
        camera(sc, (0, -1, 0), -25.0, 38.0, 122.0, lens=50)
        render(sc, os.path.join(SHOTS, 'asset-age2-cultures.png'))
    # ---- night sheet (window mask), houses
    if wanted('night') and all(h in intact for h in ('House_A', 'House_B', 'House_C', 'House_D')):
        sc = new_scene('night')
        world(sc, 0.12)
        ld = sun(sc, -110, 25, 0.35)
        ld.data.color = (0.55, 0.65, 1.0)
        ground(sc, 800)
        for i, h in enumerate(('House_A', 'House_B', 'House_C', 'House_D')):
            src = intact[h]['lods'][0]
            me = src.copy()
            for k, m in enumerate(src.materials):
                if m.name == 'M_Age2_Trim':
                    me.materials[k] = material_fn('Trim', 'Valley', night=6.0)
            place(sc, me, (i - 1.5) * 15.0, 0, 0.0)
        camera(sc, (0, 0, 2), -20.0, 22.0, 75.0)
        render(sc, os.path.join(SHOTS, 'asset-age2-night.png'))
    # ---- LOD sheet
    for asset, r in intact.items():
        slug = SLUG[asset]
        if not (wanted(slug + '-lods') or wanted('lods')):
            continue
        sc = new_scene('lods')
        sun(sc, VIEW_STUDIO['az'] - 70)
        ground(sc, 1200)
        bb = r['bounds']
        w = (bb[1] - bb[0]) * 1.2 + 3
        n = len(r['lods'])
        for i, me in enumerate(r['lods']):
            place(sc, me, (i - (n - 1) / 2) * w, 0)
        camera(sc, (0, 0, bb[5] * 0.3), 0.0, 26.0, max(30.0, w * n / 0.72 * 1.02))
        render(sc, os.path.join(SHOTS, 'asset-age2-%s-lods.png' % slug))
