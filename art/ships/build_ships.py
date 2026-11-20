# DEMIURGE ships - build every Ship-family asset: textures, materials, meshes (4 LODs x 3 states), FBX, .blend, manifest.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/ships/build_ships.py -- [options]
#     --only A,B      build only these asset keys (ReedBoat, Canoe, Galley, Cog, SailShip, Steamship, ContainerShip, FastFreighter)
#     --tex           (re)generate the trim-sheet textures (default: only when missing)
#     --no-export     skip FBX export
#     --no-manifest   skip manifest.json
#
# Outputs: art/ships/textures/T_Ship_*.png, art/ships/export/SM_Ship_<Asset>_A_<State>.fbx (LOD0..3 under a LodGroup),
#          art/ships/source/ships.blend, art/ships/manifest.json
import json
import math
import os
import sys
import time

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ship_tex  # noqa: E402
import ship_geo as g  # noqa: E402
import ship_defs as sd  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TEX = os.path.join(HERE, 'textures')
EXP = os.path.join(HERE, 'export')
SRC = os.path.join(HERE, 'source')
LOD_SCREEN = [1.0, 0.35, 0.12, 0.04]
SLOTS = ['Structure', 'Roof', 'Detail']
SHEET_OF_SLOT = ['Hull', 'Canvas', 'Detail']
KEBAB = {'ReedBoat': 'reed-boat', 'Canoe': 'canoe', 'Galley': 'galley', 'Cog': 'cog', 'SailShip': 'sail-ship',
         'Steamship': 'steamship', 'ContainerShip': 'container-ship', 'FastFreighter': 'fast-freighter'}


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = dict(only=None, tex=False, export=True, manifest=True)
    i = 0
    while i < len(a):
        if a[i] == '--only':
            o['only'] = a[i + 1].split(',')
            i += 1
        elif a[i] == '--tex':
            o['tex'] = True
        elif a[i] == '--no-export':
            o['export'] = False
        elif a[i] == '--no-manifest':
            o['manifest'] = False
        i += 1
    return o


def srgb_to_lin(h):
    h = h.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


# --------------------------------------------------------------------------- materials


def load_img(path, color):
    name = os.path.basename(path)
    img = bpy.data.images.get(name)
    if img is None:
        img = bpy.data.images.load(path)
    else:
        img.reload()
    img.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
    return img


def sock(node, ident, out=False):
    for s in (node.outputs if out else node.inputs):
        if s.identifier == ident:
            return s
    raise KeyError(ident)


def make_material(slot, sheet):
    """M_Ship_<Slot>: base = lerp(D, D * CultureTint, M.G); emissive = M.B * D * 8 + M.R * NightWindows * warm."""
    name = 'M_Ship_%s' % slot
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    N = nt.nodes.new
    L = nt.links.new
    out = N('ShaderNodeOutputMaterial')
    out.location = (900, 0)
    bs = N('ShaderNodeBsdfPrincipled')
    bs.location = (600, 0)
    base = os.path.join(TEX, 'T_Ship_%s' % sheet)
    tD = N('ShaderNodeTexImage'); tD.image = load_img(base + '_D.png', True); tD.location = (-900, 300)
    tN = N('ShaderNodeTexImage'); tN.image = load_img(base + '_N.png', False); tN.location = (-900, -300)
    tO = N('ShaderNodeTexImage'); tO.image = load_img(base + '_ORM.png', False); tO.location = (-900, 0)
    tM = N('ShaderNodeTexImage'); tM.image = load_img(base + '_M.png', False); tM.location = (-900, -600)
    for t in (tD, tN, tO, tM):
        t.interpolation = 'Linear'
        t.extension = 'REPEAT'
    sO = N('ShaderNodeSeparateColor'); sO.location = (-600, 0)
    sM = N('ShaderNodeSeparateColor'); sM.location = (-600, -600)
    L(tO.outputs['Color'], sO.inputs['Color'])
    L(tM.outputs['Color'], sM.inputs['Color'])
    at = N('ShaderNodeAttribute'); at.attribute_type = 'OBJECT'; at.attribute_name = 'culture_tint'; at.location = (-600, 400)
    mul = N('ShaderNodeMix'); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'; mul.location = (-300, 400)
    sock(mul, 'Factor_Float').default_value = 1.0
    L(tD.outputs['Color'], sock(mul, 'A_Color'))
    L(at.outputs['Color'], sock(mul, 'B_Color'))
    mix = N('ShaderNodeMix'); mix.data_type = 'RGBA'; mix.location = (0, 300)
    L(sM.outputs['Green'], sock(mix, 'Factor_Float'))
    L(tD.outputs['Color'], sock(mix, 'A_Color'))
    L(sock(mul, 'Result_Color', True), sock(mix, 'B_Color'))
    # ambient occlusion folded into base colour for the preview (UE uses ORM.R directly)
    ao = N('ShaderNodeMix'); ao.data_type = 'RGBA'; ao.blend_type = 'MULTIPLY'; ao.location = (300, 300)
    sock(ao, 'Factor_Float').default_value = 0.6
    L(sock(mix, 'Result_Color', True), sock(ao, 'A_Color'))
    L(sO.outputs['Red'], sock(ao, 'B_Color'))
    L(sock(ao, 'Result_Color', True), bs.inputs['Base Color'])
    L(sO.outputs['Green'], bs.inputs['Roughness'])
    L(sO.outputs['Blue'], bs.inputs['Metallic'])
    nm = N('ShaderNodeNormalMap'); nm.location = (300, -300); nm.uv_map = 'UVMap'
    L(tN.outputs['Color'], nm.inputs['Color'])
    L(nm.outputs['Normal'], bs.inputs['Normal'])
    # emission
    night = N('ShaderNodeAttribute'); night.attribute_type = 'OBJECT'; night.attribute_name = 'night'; night.location = (-300, -800)
    e1 = N('ShaderNodeMath'); e1.operation = 'MULTIPLY'; e1.location = (0, -700)
    L(sM.outputs['Red'], e1.inputs[0]); L(night.outputs['Fac'], e1.inputs[1])
    warm = N('ShaderNodeMix'); warm.data_type = 'RGBA'; warm.location = (300, -650)
    L(e1.outputs[0], sock(warm, 'Factor_Float'))
    sock(warm, 'A_Color').default_value = (0, 0, 0, 1)
    sock(warm, 'B_Color').default_value = (1.0, 0.62, 0.28, 1)
    glow = N('ShaderNodeMix'); glow.data_type = 'RGBA'; glow.location = (450, -500)
    L(sM.outputs['Blue'], sock(glow, 'Factor_Float'))
    L(sock(warm, 'Result_Color', True), sock(glow, 'A_Color'))
    L(tD.outputs['Color'], sock(glow, 'B_Color'))
    L(sock(glow, 'Result_Color', True), bs.inputs['Emission Color'])
    bs.inputs['Emission Strength'].default_value = 3.0
    L(bs.outputs['BSDF'], out.inputs['Surface'])
    return m


# --------------------------------------------------------------------------- meshes


def to_object(name, P, mats, coll):
    me = bpy.data.meshes.get(name)
    if me is not None:
        bpy.data.meshes.remove(me)
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in P.V], [], [f[0] for f in P.F])
    uv = me.uv_layers.new(name='UVMap')
    for poly, f in zip(me.polygons, P.F):
        poly.material_index = ship_tex.BANDS[f[1]][0]
        poly.use_smooth = f[3]
        for k, li in enumerate(poly.loop_indices):
            u, vl = f[2][k]
            uv.data[li].uv = g.band_uv(f[1], u, vl)
    for m in mats:
        me.materials.append(m)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-5)
    ngons = [f for f in bm.faces if len(f.verts) > 4]
    if ngons:
        bmesh.ops.triangulate(bm, faces=ngons, quad_method='BEAUTY', ngon_method='BEAUTY')
    # drop loose leftovers
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context='VERTS')
    bm.to_mesh(me)
    bm.free()
    me.set_sharp_from_angle(angle=math.radians(38))
    me.validate(clean_customdata=False)
    me.update()
    ob = bpy.data.objects.get(name)
    if ob is not None:
        bpy.data.objects.remove(ob)
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def tri_count(me):
    return sum(len(p.vertices) - 2 for p in me.polygons)


def get_coll(name, parent):
    c = bpy.data.collections.get(name)
    if c is None:
        c = bpy.data.collections.new(name)
        parent.children.link(c)
    return c


def export_fbx(path, root_empty, objs):
    for o in objs:
        o.hide_set(False)
    bpy.ops.object.select_all(action='DESELECT')
    root_empty.select_set(True)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = root_empty
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'EMPTY', 'MESH'},
                             apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS', bake_space_transform=False,
                             mesh_smooth_type='FACE', use_tspace=True, use_mesh_modifiers=False, add_leaf_bones=False,
                             bake_anim=False, path_mode='STRIP', embed_textures=False, use_custom_props=False,
                             axis_forward='-Z', axis_up='Y')
    for o in objs[1:]:
        o.hide_set(True)


def main():
    o = args()
    t0 = time.time()
    for d in (TEX, EXP, SRC):
        os.makedirs(d, exist_ok=True)
    have = all(os.path.exists(os.path.join(TEX, 'T_Ship_%s_%s.png' % (s, k))) for s in ship_tex.SHEETS for k in ('D', 'N', 'ORM', 'M'))
    if o['tex'] or not have:
        for line in ship_tex.build_all(TEX):
            print(line)
        print('textures %.1fs' % (time.time() - t0))
    blend = os.path.join(SRC, 'ships.blend')
    if o['only'] and os.path.exists(blend):
        bpy.ops.wm.open_mainfile(filepath=blend)
    else:
        bpy.ops.wm.read_factory_settings(use_empty=True)
    scn = bpy.context.scene
    scn.unit_settings.system = 'METRIC'
    scn.unit_settings.scale_length = 1.0
    mats = [make_material(s, SHEET_OF_SLOT[i]) for i, s in enumerate(SLOTS)]
    top = get_coll('Ships', scn.collection)
    man_path = os.path.join(HERE, 'manifest.json')
    manifest = {}
    if os.path.exists(man_path):
        try:
            manifest = json.load(open(man_path, encoding='utf-8'))
        except Exception:
            manifest = {}
    meshes = manifest.get('meshes', {})
    for key, fn, display, ages, cult, kit in sd.ASSETS:
        if o['only'] and key not in o['only']:
            continue
        acoll = get_coll('Ship_%s' % key, top)
        for state in sd.STATES:
            mesh_name = 'SM_Ship_%s_A_%s' % (key, state)
            scoll = get_coll(mesh_name, acoll)
            for ob in list(scoll.objects):
                bpy.data.objects.remove(ob)
            root = bpy.data.objects.new(mesh_name, None)
            root.empty_display_size = 2.0
            root['fbx_type'] = 'LodGroup'
            scoll.objects.link(root)
            objs, tris, info0 = [], [], None
            bb = None
            for lod in range(4):
                P, info = sd.build(key, lod, state)
                if lod == 0:
                    info0 = info
                ob = to_object('%s_LOD%d' % (mesh_name, lod), P, mats, scoll)
                ob.parent = root
                ob['culture_tint'] = srgb_to_lin(sd.CULTURES[cult])
                ob['night'] = 0.0
                if lod > 0:
                    ob.hide_render = True
                    ob.hide_set(True)
                objs.append(ob)
                tris.append(tri_count(ob.data))
                if lod == 0:
                    xs = [v.co for v in ob.data.vertices]
                    bb = ([min(p[i] for p in xs) for i in range(3)], [max(p[i] for p in xs) for i in range(3)])
            root['culture_tint'] = srgb_to_lin(sd.CULTURES[cult])
            root['culture_index'] = cult
            root['scale_pos'] = list(info0['scale_pos'])
            root['close_target'] = list(info0.get('close_target', (0.0, 0.0, info0['deck'] + 1.5)))
            root['length'] = bb[1][1] - bb[0][1]
            root['height'] = bb[1][2]
            print('%-34s tris %s  (%.0f%% %.0f%% %.0f%%)' % (mesh_name, tris, 100 * tris[1] / tris[0], 100 * tris[2] / tris[0], 100 * tris[3] / tris[0]))
            if o['export']:
                export_fbx(os.path.join(EXP, mesh_name + '.fbx'), root, objs)
            used = sorted({SLOTS[p.material_index] for p in objs[0].data.polygons})
            meshes[mesh_name] = dict(
                asset=key, display=display, variant='A', state=state, family='Ship',
                sim_ages=ages, towns_kit_slot=kit,
                footprint_m=[round(bb[1][0] - bb[0][0], 2), round(bb[1][1] - bb[0][1], 2)],
                height_m=round(bb[1][2], 2), draft_m=round(-bb[0][2], 2),
                bounds_min=[round(x, 2) for x in bb[0]], bounds_max=[round(x, 2) for x in bb[1]],
                deck_z_m=round(info0['deck'], 2),
                triangles={'LOD%d' % i: t for i, t in enumerate(tris)}, lod_screen_sizes=LOD_SCREEN,
                materials={s: 'M_Ship_%s' % s for s in SLOTS if s in used},
                material_slots_order=SLOTS,
                textures={'M_Ship_%s' % s: ['T_Ship_%s_%s' % (SHEET_OF_SLOT[i], k) for k in ('D', 'N', 'ORM', 'M')] for i, s in enumerate(SLOTS) if s in used},
                states=['Intact', 'Damaged', 'Ruin'],
                culture_variants=['MI_Ship_%s_C%d' % (s, k) for s in SLOTS if s in used for k in range(6)],
                default_culture_for_previews='C%d %s' % (cult, sd.CULTURES[cult]),
                fbx='art/ships/export/%s.fbx' % mesh_name,
                pivot='centre of the waterplane at the waterline (Z = 0); bow toward -Y in Blender',
                licence='original', script='art/ships/build_ships.py')
    bpy.ops.wm.save_as_mainfile(filepath=blend, compress=True)
    if os.path.exists(blend + '1'):
        os.remove(blend + '1')
    if o['manifest']:
        manifest = dict(
            family='Ship', generated=time.strftime('%Y-%m-%d'), script='art/ships/build_ships.py',
            blender='5.2', units='metres (1 BU = 1 m); FBX exported with FBX_SCALE_UNITS so Unreal sees centimetres',
            axes='Z up, bow toward -Y in Blender; FBX axis_forward -Z, axis_up Y (Blender defaults)',
            waterline='Z = 0 is the sea surface; hulls continue below it (draft_m)',
            lods='LOD0..LOD3 as children of a LodGroup node in each FBX; screen sizes 1.0 / 0.35 / 0.12 / 0.04',
            material_contract=dict(
                slots=SLOTS,
                textures='T_Ship_<Sheet>_D (sRGB), _N (OpenGL normal, flip green in UE), _ORM (AO, roughness, metallic), _M (linear)',
                mask_channels='M.R night-window emissive, M.G culture-tint mask, M.B always-on emissive (engine glow, uses D as colour)',
                base_colour='BaseColor = lerp(D, D * CultureTint, M.G)',
                emissive='Emissive = (M.B * D + M.R * NightFactor * (1.0, 0.62, 0.28)) * 3',
                culture_instances='MI_Ship_<Slot>_C<k> set CultureTint to DM.CULTURE_COLORS[k] (linear)',
                uv='trim sheets: V stays inside one band per face; U tiles (texture Wrap on U)'),
            cultures={'C%d' % k: h for k, h in enumerate(sd.CULTURES)},
            bands={k: dict(slot=SLOTS[v[0]], y0_px=v[1], h_px=v[2]) for k, v in ship_tex.BANDS.items()},
            previews={k: ['shots/asset-ships-%s-%s.png' % (KEBAB[k], v) for v in ('close', 'mid', 'far')] +
                      ['shots/asset-ships-states-%s.png' % KEBAB[k]] for k in KEBAB},
            preview_sheets=['shots/asset-ships-lineup-ages.png', 'shots/asset-ships-night.png'] +
                           ['shots/asset-ships-cultures-%s.png' % k for k in ('sail-ship', 'galley', 'container-ship')],
            meshes=dict(sorted(meshes.items())),
            totals=dict(meshes=len(meshes), triangles_lod0=sum(m['triangles']['LOD0'] for m in meshes.values())))
        with open(man_path, 'w', encoding='utf-8') as f:
            json.dump(manifest, f, indent=1)
    print('done %.1fs' % (time.time() - t0))


if __name__ == '__main__':
    main()
