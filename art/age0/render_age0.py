# DEMIURGE art - Age0 preview renders (golden-hour rig of docs/ASSETS.md section 3). Always run through the slot tool:
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b art/age0/source/age0.blend
#          --python art/age0/render_age0.py -- <mode> [args]
#
# modes:  solo <Key,...|all>     one image per asset at 40 m three-quarter      shots/asset-age0-<asset>-40m.png
#         states <Key,...|all>   every state side by side, 40 m                  shots/asset-age0-<asset>-states-40m.png
#         camp                   camp + palisaded hamlet at 40 m, 300 m, 1 km    shots/asset-age0-camp-<view>.png
#         variants               climate styles and culture colours, 40 m        shots/asset-age0-variants-40m.png
#         lineup                 every intact asset in a row with a 1.8 m figure shots/asset-age0-lineup-40m.png
#         close <Key> [out]      a 14 m inspection view (scratch, not a deliverable)
# LOD per instance is chosen from its screen size exactly as Unreal would (1.0 / 0.35 / 0.12 / 0.04).
import os
import sys
import math
import random
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'shots')
LOD_SS = [0.35, 0.12, 0.04]
VFOV = None


def slug(key):
    a, v = key.split('_', 1)
    out = []
    for i, ch in enumerate(a):
        if ch.isupper() and i:
            out.append('-')
        out.append(ch.lower())
    return ''.join(out) + '-' + v.lower()


def groups():
    g = {}
    for ob in bpy.data.objects:
        if ob.type == 'EMPTY' and 'dm_asset' in ob:
            g.setdefault(ob['dm_asset'], {})[ob['dm_state']] = ob
    return g


# --------------------------------------------------------------------------- scene rig

def new_scene():
    # background mode has no window to switch scenes, so the file's own scene is reused: the asset collections are
    # unlinked from it (the objects stay in bpy.data and keep their parenting) and preview objects are added at the root.
    sc = bpy.context.scene
    for c in list(sc.collection.children):
        sc.collection.children.unlink(c)
    for o in list(sc.collection.objects):
        bpy.data.objects.remove(o)
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    ee = sc.eevee
    ee.taa_render_samples = 48
    try:
        ee.use_raytracing = True
        ee.ray_tracing_options.resolution_scale = '2'
    except Exception:
        pass
    ee.use_shadows = True
    try:
        ee.shadow_resolution_scale = 1.0
        ee.use_volumetric_shadows = True
        ee.volumetric_end = 4000.0
        ee.volumetric_tile_size = '8'
    except Exception:
        pass
    sc.view_settings.view_transform = 'AgX'
    for look in ('AgX - Medium High Contrast', 'Medium High Contrast'):
        try:
            sc.view_settings.look = look
            break
        except Exception:
            pass
    sc.view_settings.exposure = 0.0
    # world: cool sky fill + light haze
    w = bpy.data.worlds.new('PreviewSky')
    sc.world = w
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
    ramp.color_ramp.elements[0].position = 0.5
    ramp.color_ramp.elements[0].color = (0.62, 0.66, 0.72, 1)
    ramp.color_ramp.elements[1].position = 0.75
    ramp.color_ramp.elements[1].color = (0.2, 0.32, 0.55, 1)
    nt.links.new(ramp.outputs['Color'], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = 0.9
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])
    # golden-hour sun: 20 degrees up, from the left of frame and a little behind
    sun = bpy.data.lights.new('Sun', 'SUN')
    sun.energy = 4.2
    sun.color = (1.0, 0.76, 0.52)
    sun.angle = math.radians(1.2)
    so = bpy.data.objects.new('Sun', sun)
    sc.collection.objects.link(so)
    so.rotation_euler = (0, 0, 0)
    d = Vector((-0.85, 0.5, 0.0)).normalized()
    elev = math.radians(20)
    to_sun = Vector((d.x * math.cos(elev), d.y * math.cos(elev), math.sin(elev)))
    so.rotation_euler = (-to_sun).to_track_quat('-Z', 'Y').to_euler()
    return sc


def add_haze(sc, density=0.00018, size=5000.0, height=600.0):
    """Aerial haze as a bounded volume box (a world volume blacks out the sky in EEVEE)."""
    me = bpy.data.meshes.new('Haze')
    s, h = size * 0.5, height
    v = [(-s, -s, -1), (s, -s, -1), (s, s, -1), (-s, s, -1), (-s, -s, h), (s, -s, h), (s, s, h), (-s, s, h)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    me.from_pydata(v, [], f)
    m = bpy.data.materials.new('HazeVol')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    vol = nt.nodes.new('ShaderNodeVolumePrincipled')
    vol.inputs['Density'].default_value = density
    vol.inputs['Color'].default_value = (0.8, 0.82, 0.88, 1)
    vol.inputs['Anisotropy'].default_value = 0.5
    nt.links.new(vol.outputs['Volume'], out.inputs['Volume'])
    me.materials.append(m)
    ob = bpy.data.objects.new('Haze', me)
    sc.collection.objects.link(ob)
    return ob


def ground_material(centres):
    m = bpy.data.materials.new('PreviewGround')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    N = nt.nodes.new; L = nt.links.new
    out = N('ShaderNodeOutputMaterial'); bs = N('ShaderNodeBsdfPrincipled')
    geo = N('ShaderNodeNewGeometry')
    noise = N('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value = 0.08; noise.inputs['Detail'].default_value = 8
    L(geo.outputs['Position'], noise.inputs['Vector'])
    noise2 = N('ShaderNodeTexNoise'); noise2.inputs['Scale'].default_value = 1.5; noise2.inputs['Detail'].default_value = 6
    L(geo.outputs['Position'], noise2.inputs['Vector'])
    grass = N('ShaderNodeValToRGB')
    L(noise.outputs['Fac'], grass.inputs['Fac'])
    grass.color_ramp.elements[0].color = (0.10, 0.14, 0.035, 1)
    grass.color_ramp.elements[1].color = (0.22, 0.25, 0.07, 1)
    # trampled earth near the settlements
    mask = None
    for (cx, cy, r0, r1) in centres:
        sub = N('ShaderNodeVectorMath'); sub.operation = 'SUBTRACT'; sub.inputs[1].default_value = (cx, cy, 0)
        L(geo.outputs['Position'], sub.inputs[0])
        ln = N('ShaderNodeVectorMath'); ln.operation = 'LENGTH'; L(sub.outputs['Vector'], ln.inputs[0])
        mr = N('ShaderNodeMapRange'); mr.inputs['From Min'].default_value = r0; mr.inputs['From Max'].default_value = r1
        mr.inputs['To Min'].default_value = 1.0; mr.inputs['To Max'].default_value = 0.0
        L(ln.outputs['Value'], mr.inputs['Value'])
        if mask is None:
            mask = mr
        else:
            mx = N('ShaderNodeMath'); mx.operation = 'MAXIMUM'
            L(mask.outputs[0], mx.inputs[0]); L(mr.outputs[0], mx.inputs[1]); mask = mx
    nm = N('ShaderNodeMath'); nm.operation = 'MULTIPLY_ADD'
    L(noise2.outputs['Fac'], nm.inputs[0]); nm.inputs[1].default_value = 0.9; nm.inputs[2].default_value = -0.45
    ad = N('ShaderNodeMath'); ad.operation = 'ADD'; ad.use_clamp = True
    L(mask.outputs[0], ad.inputs[0]); L(nm.outputs[0], ad.inputs[1])
    mix = N('ShaderNodeMix'); mix.data_type = 'RGBA'
    L(ad.outputs[0], mix.inputs['Factor']); L(grass.outputs['Color'], mix.inputs['A'])
    mix.inputs['B'].default_value = (0.2, 0.15, 0.09, 1)
    L(mix.outputs['Result'], bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value = 0.95
    L(bs.outputs['BSDF'], out.inputs['Surface'])
    return m


def add_ground(sc, size=6000.0, centres=((0, 0, 3, 14),), neutral=False):
    me = bpy.data.meshes.new('Ground')
    s = size * 0.5
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new('Ground', me)
    sc.collection.objects.link(ob)
    if neutral:
        m = bpy.data.materials.new('NeutralGround')
        m.use_nodes = True
        bs = m.node_tree.nodes['Principled BSDF']
        bs.inputs['Base Color'].default_value = (0.16, 0.16, 0.12, 1)
        bs.inputs['Roughness'].default_value = 0.95
    else:
        m = ground_material(centres)
    me.materials.append(m)
    return ob


def figure_mat():
    m = bpy.data.materials.get('PreviewFigure')
    if m:
        return m
    m = bpy.data.materials.new('PreviewFigure')
    m.use_nodes = True
    bs = m.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (0.045, 0.03, 0.02, 1)
    bs.inputs['Roughness'].default_value = 0.8
    return m


def add_figure(sc, x, y, yaw=0.0):
    """A 1.8 m standing figure (scale reference)."""
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=0.2, radius2=0.24, depth=1.45,
                          matrix=__import__('mathutils').Matrix.Translation((0, 0, 0.725)))
    bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=0.13,
                              matrix=__import__('mathutils').Matrix.Translation((0, 0, 1.64)))
    me = bpy.data.meshes.new('Figure')
    bm.to_mesh(me); bm.free()
    me.materials.append(figure_mat())
    ob = bpy.data.objects.new('Figure', me)
    ob.location = (x, y, 0)
    ob.rotation_euler = (0, 0, yaw)
    sc.collection.objects.link(ob)
    return ob


def tree_mats():
    if 'PreviewLeaf' in bpy.data.materials:
        return bpy.data.materials['PreviewLeaf'], bpy.data.materials['PreviewBark']
    lf = bpy.data.materials.new('PreviewLeaf'); lf.use_nodes = True
    b = lf.node_tree.nodes['Principled BSDF']; b.inputs['Base Color'].default_value = (0.03, 0.07, 0.025, 1); b.inputs['Roughness'].default_value = 0.85
    bk = bpy.data.materials.new('PreviewBark'); bk.use_nodes = True
    b = bk.node_tree.nodes['Principled BSDF']; b.inputs['Base Color'].default_value = (0.06, 0.045, 0.03, 1)
    return lf, bk


def tree_meshes():
    """Context only (not part of the family): a pine and a broadleaf stand-in."""
    import bmesh
    from mathutils import Matrix
    lf, bk = tree_mats()
    out = []
    for kind in ('pine', 'oak'):
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=False, segments=6, radius1=0.35, radius2=0.2, depth=4.0, matrix=Matrix.Translation((0, 0, 2.0)))
        nt = len(bm.faces)
        if kind == 'pine':
            for k in range(4):
                bmesh.ops.create_cone(bm, cap_ends=True, segments=9, radius1=3.2 - 0.6 * k, radius2=0.2, depth=4.5,
                                      matrix=Matrix.Translation((0, 0, 4.0 + 2.6 * k)))
        else:
            rnd = random.Random(3)
            for k in range(5):
                bmesh.ops.create_icosphere(bm, subdivisions=2, radius=3.0 + rnd.random(),
                                           matrix=Matrix.Translation((rnd.uniform(-2, 2), rnd.uniform(-2, 2), 7.0 + rnd.uniform(-1, 2))))
        me = bpy.data.meshes.new('Tree_' + kind)
        bm.to_mesh(me); bm.free()
        me.materials.append(bk); me.materials.append(lf)
        for i, p in enumerate(me.polygons):
            p.material_index = 0 if i < nt else 1
            p.use_smooth = kind == 'oak'
        out.append(me)
    return out


def add_forest(sc, centre, r0, r1, n, seed=1, avoid=()):
    rnd = random.Random(seed)
    meshes = tree_meshes()
    placed = 0
    tries = 0
    while placed < n and tries < n * 5:
        tries += 1
        a = rnd.uniform(0, math.tau)
        r = r0 + (r1 - r0) * math.sqrt(rnd.random())
        x, y = centre[0] + math.cos(a) * r, centre[1] + math.sin(a) * r
        if any(math.hypot(x - ax, y - ay) < ar for ax, ay, ar in avoid):
            continue
        ob = bpy.data.objects.new('Tree', meshes[rnd.random() < 0.45])
        s = rnd.uniform(0.8, 1.6)
        ob.location = (x, y, 0); ob.scale = (s, s, s * rnd.uniform(0.9, 1.3)); ob.rotation_euler = (0, 0, rnd.uniform(0, 6.3))
        sc.collection.objects.link(ob)
        placed += 1


def camera(sc, target, dist, elev_deg, azim_deg, lens=50.0):
    global VFOV
    cam = bpy.data.cameras.new('Cam')
    cam.lens = lens
    cam.sensor_fit = 'HORIZONTAL'
    cam.clip_start = 0.5
    cam.clip_end = 20000
    ob = bpy.data.objects.new('Cam', cam)
    sc.collection.objects.link(ob)
    e, a = math.radians(elev_deg), math.radians(azim_deg)
    t = Vector(target)
    ob.location = t + Vector((math.cos(e) * math.cos(a), math.sin(e) * 0 + math.cos(e) * math.sin(a), math.sin(e))) * dist
    ob.rotation_euler = (t - ob.location).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = ob
    hfov = 2 * math.atan(36.0 / (2 * lens))
    VFOV = 2 * math.atan(math.tan(hfov / 2) * 720 / 1280)
    return ob


def lod_for(grp, loc, cam_loc):
    kids = sorted([c for c in grp.children if c.type == 'MESH'], key=lambda c: c['dm_lod'])
    lod0 = kids[0]
    r = max(lod0.dimensions) * 0.5
    d = max(1.0, (Vector(loc) - cam_loc).length)
    ss = r / (d * math.tan(VFOV / 2))
    q = 0
    for t in LOD_SS:
        if ss < t:
            q += 1
    return kids[min(q, 3)], q


MATS = ('Structure', 'Roof', 'Detail')


def place(sc, grp, loc, yaw=0.0, cam_loc=None, lod=None, override=None, scale=1.0):
    if lod is None:
        src, q = lod_for(grp, loc, cam_loc)
    else:
        kids = sorted([c for c in grp.children if c.type == 'MESH'], key=lambda c: c['dm_lod'])
        src, q = kids[lod], lod
    ob = bpy.data.objects.new(src.name + '_inst', src.data)
    ob.location = loc
    ob.rotation_euler = (0, 0, yaw)
    ob.scale = (scale, 1.0, 1.0) if scale != 1.0 else (1, 1, 1)
    sc.collection.objects.link(ob)
    st = grp['dm_state']
    ov = override or ('Burnt' if st == 'Burnt' else None)
    if ov:
        for i, slot in enumerate(ob.material_slots):
            slot.link = 'OBJECT'
            slot.material = bpy.data.materials['MI_Age0_%s_%s' % (MATS[i], ov)]
    return ob, q


def culture_override(ob, colour_hex, climate=None):
    """Per-instance culture colour: copy the Detail material with a new CultureColor (what custom data does in UE)."""
    import build_age0 as B
    for i, slot in enumerate(ob.material_slots):
        base = slot.material
        if climate:
            base = bpy.data.materials['MI_Age0_%s_%s' % (MATS[i], climate)]
        if MATS[i] == 'Detail':
            nm = '%s_%s' % (base.name, colour_hex)
            m = bpy.data.materials.get(nm)
            if m is None:
                m = base.copy(); m.name = nm
                for n in m.node_tree.nodes:
                    if n.type == 'MIX' and n.blend_type == 'MULTIPLY' and n.inputs['Factor'].is_linked and 'Green' in n.inputs['Factor'].links[0].from_socket.name:
                        n.inputs['B'].default_value = (*B.hexlin(colour_hex), 1.0)
            base = m
        slot.link = 'OBJECT'
        slot.material = base


def clear_scene(sc):
    for o in list(sc.collection.objects):
        bpy.data.objects.remove(o)


def render(sc, path):
    sc.render.filepath = path
    if bpy.context.window:
        bpy.context.window.scene = sc
    bpy.ops.render.render(write_still=True)
    print('WROTE', path, flush=True)


def lod_report(counts):
    return ' '.join('L%d:%d' % (k, counts.get(k, 0)) for k in range(4))


# --------------------------------------------------------------------------- modes

ORDER = ['Hut_A', 'Hut_B', 'Hut_C', 'LeanTo_A', 'Longhouse_A', 'Longhouse_B', 'FirePit_A', 'DryingRack_A', 'DryingRack_B',
         'Palisade_Seg', 'Palisade_Gate', 'StoragePit_A', 'StoragePit_B', 'StandingStones_A', 'StandingStone_A',
         'Ornament_Pennant', 'Ornament_Totem']


def first_state(gs):
    return gs.get('Intact') or list(gs.values())[0]


def mode_solo(keys, dist=40.0, tag='40m', elev=28.0, lens=50.0, out=None):
    G = groups()
    for key in keys:
        sc = new_scene()
        add_ground(sc, centres=((0, 0, 2, 9),))
        grp = first_state(G[key])
        size = max(c.dimensions.length for c in grp.children)
        cam = camera(sc, (0, 0, 1.0 if size < 20 else 2.0), dist, elev, -112.0, lens)
        _, q = place(sc, grp, (0, 0, 0), 0.0, cam.location)
        lod0 = [c for c in grp.children if c['dm_lod'] == 0][0]
        add_figure(sc, lod0.dimensions.x * 0.5 + 1.2, -lod0.dimensions.y * 0.5 - 0.8, 0.4)
        add_forest(sc, (0, 0), 60, 160, 120, seed=7)
        p = out or os.path.join(SHOTS, 'asset-age0-%s-%s.png' % (slug(key), tag))
        render(sc, p)
        print('LOD used', key, q)
        clear_scene(sc)


def mode_states(keys):
    G = groups()
    for key in keys:
        gs = G[key]
        sts = [s for s in ('Intact', 'Construction', 'Damaged', 'Ruin', 'Burnt') if s in gs]
        if len(sts) < 2:
            continue
        sc = new_scene()
        add_ground(sc, centres=((0, 0, 3, 30),))
        lod0 = [c for c in gs['Intact'].children if c['dm_lod'] == 0][0]
        span = max(lod0.dimensions.x, lod0.dimensions.y) + 2.5
        xs = [(i - (len(sts) - 1) * 0.5) * span for i in range(len(sts))]
        width = span * len(sts)
        dist = max(40.0, width * 1.55)
        cam = camera(sc, (0, 0, 1.0), dist, 26.0, -92.0, 50.0)
        for x, st in zip(xs, sts):
            place(sc, gs[st], (x, 0, 0), 0.0, cam.location)
        add_figure(sc, xs[0] - span * 0.35, -lod0.dimensions.y * 0.5 - 1.0)
        add_forest(sc, (0, 0), width * 0.8 + 40, width * 0.8 + 140, 140, seed=9)
        render(sc, os.path.join(SHOTS, 'asset-age0-%s-states-40m.png' % slug(key)))
        clear_scene(sc)


def mode_lineup():
    G = groups()
    sc = new_scene()
    add_ground(sc, centres=((0, 0, 5, 60),))
    keys = [k for k in ORDER if k in G and k not in ('Palisade_Seg', 'Palisade_Gate', 'StandingStones_A', 'Longhouse_B')]
    x = 0.0
    pos = []
    for k in keys:
        lod0 = [c for c in first_state(G[k]).children if c['dm_lod'] == 0][0]
        w = lod0.dimensions.x
        pos.append((k, x + w * 0.5))
        x += w + 1.5
    off = x * 0.5
    cam = camera(sc, (0, 0, 1.0), max(40.0, x * 1.5), 24.0, -92.0, 50.0)
    for k, px in pos:
        place(sc, first_state(G[k]), (px - off, 0, 0), 0.0, cam.location)
    add_figure(sc, -off - 1.0, -1.0)
    add_forest(sc, (0, 0), x * 0.6 + 30, x * 0.6 + 140, 160, seed=11)
    render(sc, os.path.join(SHOTS, 'asset-age0-lineup-40m.png'))


def mode_variants():
    """Hut A, Hut C, lean-to and the gate in the four climate styles; pennants in the six culture colours."""
    import build_age0 as B
    G = groups()
    sc = new_scene()
    add_ground(sc, centres=((0, 0, 4, 40),))
    cam = camera(sc, (0, 0, 1.5), 62.0, 30.0, -96.0, 50.0)
    climates = ['Temperate', 'Arid', 'Tropical', 'Cold']
    for i, cl in enumerate(climates):
        x = (i - 1.5) * 9.5
        for j, key in enumerate(('Hut_A', 'Hut_C')):
            ob, _ = place(sc, G[key]['Intact'], (x, j * 8.0 - 2.0, 0), 0.0, cam.location)
            culture_override(ob, B.CULTURE_COLORS[i], climate=cl)
    for i, col in enumerate(B.CULTURE_COLORS):
        x = (i - 2.5) * 6.0
        ob, _ = place(sc, G['Ornament_Pennant']['Intact'], (x, -9.0, 0), 0.0, cam.location)
        culture_override(ob, col)
    add_figure(sc, -17, -7)
    add_forest(sc, (0, 0), 70, 180, 160, seed=13)
    render(sc, os.path.join(SHOTS, 'asset-age0-variants-40m.png'))


def camp_layout(G):
    """The concept camp: ~15 huts in a loose ring round one hearth, racks to the west, pits, a shrine ring of stones to the
    north-east; a palisaded longhouse hamlet 110 m east (the camp's next step) so every asset is in the far views."""
    rnd = random.Random(5)
    items = []
    items.append(('FirePit_A', 'Intact', (0, 0), 0.0))
    huts = ['Hut_A'] * 8 + ['Hut_B'] * 3 + ['Hut_C'] * 3 + ['LeanTo_A'] * 2
    rnd.shuffle(huts)
    n = len(huts)
    for k, h in enumerate(huts):
        a = k * 2.399963 + rnd.uniform(-0.15, 0.15)
        r = 11.0 + 3.9 * math.sqrt(k + 1) + rnd.uniform(-1.0, 1.0)
        x, y = math.cos(a) * r, math.sin(a) * r
        yaw = math.atan2(y, x) - math.pi / 2               # door (local -Y) toward the fire (was pointing away)
        st = 'Intact'
        if k == 5:
            st = 'Construction'
        if k == 11:
            st = 'Damaged'
        items.append((h, st, (x, y), yaw))
    for k, (x, y) in enumerate(((-24, 3), (-25, -4), (-21, -10))):
        items.append(('DryingRack_A' if k != 1 else 'DryingRack_B', 'Intact', (x, y), rnd.uniform(-0.4, 0.4) + math.pi / 2))
    for k, (x, y) in enumerate(((16, -22), (19, -19), (21, -24))):
        items.append(('StoragePit_A' if k != 1 else 'StoragePit_B', 'Intact', (x, y), rnd.uniform(0, 6.3)))
    items.append(('StandingStones_A', 'Intact', (-10, 40), 0.0))
    items.append(('StandingStone_A', 'Intact', (12, 30), 0.4))
    items.append(('Ornament_Totem', 'Intact', (3.5, 2.5), 0.0))
    items.append(('Ornament_Pennant', 'Intact', (-3.0, 3.2), 0.0))
    # hamlet: palisade ring with a gate facing the camp, two longhouses
    hc = (110.0, 10.0)
    Rp = 26.0
    nseg = int(round(math.tau * Rp / 8.0))
    for k in range(nseg):
        a = math.pi + math.tau * k / nseg
        x, y = hc[0] + math.cos(a) * Rp, hc[1] + math.sin(a) * Rp
        yaw = a + math.pi / 2                               # outer face (-Y local) points away from the centre
        key = 'Palisade_Gate' if k == 0 else 'Palisade_Seg'
        st = 'Intact'
        if k in (5, 6):
            st = 'Damaged'
        if k == 11:
            st = 'Burnt'
        items.append((key, st, (x, y), yaw))
    items.append(('Longhouse_B', 'Intact', (hc[0] + 2, hc[1] + 8), 0.08))
    items.append(('Longhouse_A', 'Intact', (hc[0] - 3, hc[1] - 9), -0.12))
    items.append(('Longhouse_A', 'Ruin', (hc[0] + 12, hc[1] - 4), 1.45))
    items.append(('FirePit_A', 'Intact', (hc[0] - 8, hc[1] + 0), 0.0))
    items.append(('StoragePit_A', 'Intact', (hc[0] + 15, hc[1] + 12), 0.0))
    return items, hc


def mode_camp(views):
    G = groups()
    items, hc = camp_layout(G)
    for tag, dist, elev, azim, target, lens in views:
        sc = new_scene()
        add_ground(sc, centres=((0, 0, 4, 34), (hc[0], hc[1], 6, 32)))
        cam = camera(sc, target, dist, elev, azim, lens)
        counts = {}
        for key, st, (x, y), yaw in items:
            gs = G[key]
            grp = gs.get(st) or first_state(gs)
            _, q = place(sc, grp, (x, y, 0), yaw, cam.location)
            counts[q] = counts.get(q, 0) + 1
        rnd = random.Random(2)
        for k in range(9):
            a = rnd.uniform(0, 6.3); r = rnd.uniform(4, 9)
            add_figure(sc, math.cos(a) * r, math.sin(a) * r, a)
        add_forest(sc, (45, 0), 70, 650, 2600, seed=21, avoid=((0, 0, 45), (hc[0], hc[1], 42), (55, 5, 30)))
        if dist > 100:
            add_haze(sc)
        render(sc, os.path.join(SHOTS, 'asset-age0-camp-%s.png' % tag))
        print('LODS', tag, lod_report(counts), flush=True)
        clear_scene(sc)


def main():
    sys.path.insert(0, HERE)
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else ['solo', 'all']
    mode = a[0]
    G = groups()
    keys = ORDER if len(a) < 2 or a[1] == 'all' else a[1].split(',')
    keys = [k for k in keys if k in G]
    if mode == 'solo':
        mode_solo(keys)
    elif mode == 'states':
        mode_states(keys)
    elif mode == 'lineup':
        mode_lineup()
    elif mode == 'variants':
        mode_variants()
    elif mode == 'close':
        out = a[2] if len(a) > 2 else None
        mode_solo(keys, dist=14.0 if 'Longhouse' not in keys[0] and 'Stones' not in keys[0] else 30.0, tag='close', elev=22.0, lens=50.0, out=out)
    elif mode == 'camp':
        which = a[1].split(',') if len(a) > 1 and a[1] != 'all' else ['40m', '300m', '1km']
        V = {'40m': ('40m', 40.0, 30.0, -105.0, (2.0, 2.0, 1.0), 40.0),
             '300m': ('300m', 300.0, 40.0, -100.0, (50.0, 8.0, 0.0), 50.0),
             '1km': ('1km', 1000.0, 40.0, -100.0, (50.0, 8.0, 0.0), 50.0)}
        mode_camp([V[w] for w in which])


main()
