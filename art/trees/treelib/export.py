# DEMIURGE tree families: per-species build (LOD 0-2 meshes, impostor bakes, LOD 3), FBX export, .blend source,
# impostor atlas packing and the manifest.
import json
import math
import os
import bpy
import numpy as np
from mathutils import Vector
from . import imaging as im
from . import render as rd
from . import species as spc
from . import tiles
from .mesh import MB, SEC_BARK, SEC_LEAF, SEC_IMP, to_blender

SPECIES = ('broadleaf', 'conifer', 'palm', 'acacia', 'shrub')
IMP_ATLAS = 2048
IMP_CELL = 256      # impostors are drawn below ~0.08 screen size (< ~90 px tall): 256 px cells are plenty
IMP_GRID = IMP_ATLAS // IMP_CELL
# LOD screen sizes written into the manifest and applied by the Unreal import script
SCREEN_SIZES = [1.0, 0.45, 0.20, 0.08]
SLOT_NAMES = ('Bark', 'Leaves', 'Impostor')


def mesh_name(sp, v):
    return 'SM_Tree_%s_%d' % (sp.capitalize(), v)


def imp_cell(sp, v, k):
    i = SPECIES.index(sp) * 12 + v * 3 + k
    col, row = i % IMP_GRID, i // IMP_GRID
    u0 = col / IMP_GRID
    v1 = 1.0 - row / IMP_GRID
    return i, (u0, v1 - 1.0 / IMP_GRID, u0 + 1.0 / IMP_GRID, v1)


def _materials(root, sp):
    tex = os.path.join(root, 'textures')

    def img(name):
        p = os.path.join(tex, name)
        if not os.path.exists(p):
            return None
        im_ = bpy.data.images.get(name)
        if im_ is None:
            im_ = bpy.data.images.load(p)
        im_.filepath = p
        im_.reload()
        return im_
    bark = rd.mat_bake_tree('Bark', img('T_Bark_%s.png' % sp.capitalize()), 'bark')
    leaves = rd.mat_bake_tree('Leaves', img('T_TreeLeafAtlas.png'), 'leaf')
    impi = img('T_TreeImpostors.png')
    imp = rd.mat_bake_tree('Impostor', impi, 'leaf') if impi else rd.mat_placeholder('Impostor')
    return [bark, leaves, imp]


def _link(ob, coll):
    coll.objects.link(ob)
    return ob


def _export_fbx(ob, path):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'MESH'}, global_scale=1.0,
                             apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', axis_forward='-Y', axis_up='Z',
                             use_mesh_modifiers=False, mesh_smooth_type='OFF', use_tspace=False, colors_type='SRGB',
                             prioritize_active_color=True, add_leaf_bones=False, bake_anim=False, use_custom_props=False,
                             path_mode='STRIP', embed_textures=False, use_triangles=True)


def _bake_views(root, sp, v, ob, model, log):
    """Renders the three impostor views of LOD 0 and returns card geometry for LOD 3."""
    out = os.path.join(root, 'build', 'impostor')
    os.makedirs(out, exist_ok=True)
    co = np.empty(len(ob.data.vertices) * 3, np.float32)
    ob.data.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    co = co[co[:, 2] > -0.25]
    mn, mx = co.min(0), co.max(0)
    mn[2] = 0.0
    cx, cy, cz = (mn + mx) * 0.5
    res = IMP_CELL * 2
    rd.setup(res, res, samples=32)
    views = []
    specs = [
        ('side_a', (cx, mn[1] - 60.0, cz), (math.radians(90), 0.0, 0.0), max(mx[0] - mn[0], mx[2] - mn[2]) * 1.03, (0, 2)),
        ('side_b', (mx[0] + 60.0, cy, cz), (math.radians(90), 0.0, math.radians(90)), max(mx[1] - mn[1], mx[2] - mn[2]) * 1.03, (1, 2)),
        ('top', (cx, cy, mx[2] + 60.0), (0.0, 0.0, 0.0), max(mx[0] - mn[0], mx[1] - mn[1]) * 1.03, (0, 1)),
    ]
    # normal bake (verification addition): the mesh's shading normals (the cards' volumetric normals) in each view's
    # frame (image right, image up, towards the camera), so LOD3 is lit like LOD0-2 instead of by a guessed normal
    me = ob.data
    cn = np.empty(len(me.loops) * 3, np.float32)
    me.corner_normals.foreach_get('vector', cn)
    cn = cn.reshape(-1, 3)
    cn /= np.maximum(np.linalg.norm(cn, axis=1, keepdims=True), 1e-8)
    if 'BakeN' in me.color_attributes:
        me.color_attributes.remove(me.color_attributes['BakeN'])
    bn = me.color_attributes.new('BakeN', 'FLOAT_COLOR', 'CORNER')
    frames = [((1, 0, 0), (0, 0, 1), (0, -1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)), ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
    leaf_img = bpy.data.images.get('T_TreeLeafAtlas.png')
    nmats = [rd.mat_bake_normal('BakeN_Bark', None, 'bark'), rd.mat_bake_normal('BakeN_Leaves', leaf_img, 'leaf')]
    for k, (nm, loc, rot, S, axes) in enumerate(specs):
        rd.ortho_camera(loc, rot, S)
        img = rd.render(os.path.join(out, '_r_%s_%d_%d.png' % (sp, v, k)))
        img = im.downsample2(img)
        im.save_png(os.path.join(out, '%s_%d_%d.png' % (sp, v, k)), img)
        R = np.array(frames[k], np.float32).T          # columns: right, up, towards camera
        val = np.concatenate([0.5 + 0.5 * (cn @ R), np.ones((len(cn), 1), np.float32)], axis=1)
        bn.data.foreach_set('color', val.ravel())
        keep = [me.materials[i] for i in range(len(me.materials))]
        me.materials[0], me.materials[1] = nmats[0], nmats[1]
        nimg = rd.render(os.path.join(out, '_r_%s_%d_%dn.png' % (sp, v, k)))
        for i, mm in enumerate(keep):
            me.materials[i] = mm
        im.save_png(os.path.join(out, '%s_%d_%dn.png' % (sp, v, k)), im.downsample2(nimg))
        a = img[..., 3] > 0.03
        rows = np.where(a.any(1))[0]
        cols = np.where(a.any(0))[0]
        if not len(rows):
            rows = cols = np.array([0, IMP_CELL - 1])
        r0, r1 = max(0, rows[0] - 2), min(IMP_CELL - 1, rows[-1] + 2)
        c0, c1 = max(0, cols[0] - 2), min(IMP_CELL - 1, cols[-1] + 2)
        ca = (cx, cy, cz)
        a0, a1 = axes
        h0 = ca[a0] - S / 2 + c0 / IMP_CELL * S
        h1 = ca[a0] - S / 2 + (c1 + 1) / IMP_CELL * S
        g0 = ca[a1] - S / 2 + r0 / IMP_CELL * S
        g1 = ca[a1] - S / 2 + (r1 + 1) / IMP_CELL * S
        views.append(dict(k=k, axes=axes, h=(h0, h1), g=(g0, g1), px=(c0, r0, c1 + 1, r1 + 1),
                          cover=float((img[..., 3] > 0.4).mean())))
    return views, (mn, mx)


def _impostor_lod(model, sp, v, views, bounds):
    mb = MB()
    mn, mx = bounds
    cc = model.crown_c
    cr = model.crown_r
    for view in views:
        k = view['k']
        _, (u0, v0, u1, v1) = imp_cell(sp, v, k)
        c0, r0, c1, r1 = view['px']
        du, dv = (u1 - u0) / IMP_CELL, (v1 - v0) / IMP_CELL
        e = 0.5 / IMP_ATLAS
        U0, U1 = u0 + c0 * du + e, u0 + c1 * du - e
        V0, V1 = v0 + r0 * dv + e, v0 + r1 * dv - e
        h0, h1 = view['h']
        g0, g1 = view['g']
        corners = []
        for (hh, gg, uu, vv) in ((h0, g0, U0, V0), (h1, g0, U1, V0), (h1, g1, U1, V1), (h0, g1, U0, V1)):
            if k == 0:
                p = Vector((hh, 0.0, gg))
            elif k == 1:
                p = Vector((0.0, hh, gg))
            else:
                p = Vector((hh, gg, model.top_z))
            corners.append((p, (uu, vv)))
        face = Vector((0, -1, 0)) if k == 0 else (Vector((1, 0, 0)) if k == 1 else Vector((0, 0, 1)))
        for side in (1, -1):
            ids = []
            for p, uv in corners:
                q = p - cc
                rad = Vector((q.x / max(0.5, cr.x), q.y / max(0.5, cr.y), q.z / max(0.5, cr.z)))
                if k == 2:
                    n = (rad * 0.8 + Vector((0, 0, 1.0))).normalized()
                else:
                    n = (rad + Vector((0, 0, 0.35))).normalized()
                sway, bend, ph = spc.wind_uv(model, p, 0.0, 0.0)
                # UV2 for the impostor material: x = signed offset from the trunk axis along the card (m),
                # y = card code (1 side XZ, 2 side YZ, 0 top); the side cards turn about the trunk to face the camera
                code = (1.0, 2.0, 0.0)[k]
                hoff = p.x if k == 0 else (p.y if k == 1 else 0.0)
                ids.append(mb.v(p, n, uv, (1.0, 1.0, 1.0, 1.0), (sway, 0.0), (hoff, code)))
            mb.quad(SEC_IMP, ids[0], ids[1], ids[2], ids[3], face * side)
    return mb


def build_species(root, species, variants, log):
    man_path = os.path.join(root, 'export', 'manifest.json')
    man = {}
    if os.path.exists(man_path):
        with open(man_path) as f:
            man = json.load(f)
    man.setdefault('species', {})
    for sp in species:
        rd.clear_scene()
        for c in list(bpy.data.collections):
            bpy.data.collections.remove(c)
        mats = _materials(root, sp)
        top = bpy.data.collections.new(sp.capitalize())
        bpy.context.scene.collection.children.link(top)
        vlist = variants or list(range(spc.NUM_VARIANTS))
        entry = man['species'].setdefault(sp, {})
        for v in vlist:
            model = spc.build(sp, v)
            name = mesh_name(sp, v)
            coll = bpy.data.collections.new(name)
            top.children.link(coll)
            lods = [spc.build_lod(model, l) for l in range(3)]
            obs = []
            for l, mb in enumerate(lods):
                ob = to_blender(mb, name + ('' if l == 0 else '_LOD%d' % l), mats)
                _link(ob, coll)
                obs.append(ob)
            # impostor bake: only this variant's LOD 0 is visible
            hidden = [o for o in bpy.context.scene.objects if o is not obs[0] and o.type == 'MESH' and not o.hide_render]
            for o in hidden:
                o.hide_render = True
            views, bounds = _bake_views(root, sp, v, obs[0], model, log)
            for o in hidden:
                o.hide_render = False
            imp = _impostor_lod(model, sp, v, views, bounds)
            ob3 = to_blender(imp, name + '_LOD3', mats)
            _link(ob3, coll)
            obs.append(ob3)
            exp = os.path.join(root, 'export', sp.capitalize())
            for l, ob in enumerate(obs):
                _export_fbx(ob, os.path.join(exp, (name if l == 0 else '%s_LOD%d' % (name, l)) + '.fbx'))
            # lay the variant out in the .blend (LODs side by side, variants in rows)
            for l, ob in enumerate(obs):
                ob.location = (l * (model.R * 2.5 + 4), v * (model.R * 2.5 + 6), 0)
            mn, mx = bounds
            tris = [[m_.ntris(s) for s in (0, 1, 2)] for m_ in lods] + [[imp.ntris(s) for s in (0, 1, 2)]]
            entry[str(v)] = dict(
                mesh=name, note=model.notes, height=round(float(mx[2]), 2),
                crown_radius=round(float(max(mx[0] - mn[0], mx[1] - mn[1]) * 0.5), 2),
                bounds_min=[round(float(x), 2) for x in mn], bounds_max=[round(float(x), 2) for x in mx],
                tris_per_lod=[sum(t) for t in tris], tris_per_lod_sections=tris,
                cards=len(model.cards), fronds=len(model.fronds), top_card_z=round(model.top_z, 2),
                impostor_cells=[imp_cell(sp, v, k)[0] for k in range(3)],
                impostor_coverage=[round(vw['cover'], 3) for vw in views])
            log('%s: H %.1f m, crown r %.1f m, tris/LOD %s, cards %d' % (name, mx[2], entry[str(v)]['crown_radius'],
                                                                         entry[str(v)]['tris_per_lod'], len(model.cards)))
        src = os.path.join(root, 'source')
        os.makedirs(src, exist_ok=True)
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(src, sp + '.blend'), compress=True, relative_remap=True)
    man['screen_sizes'] = SCREEN_SIZES
    man['slots'] = list(SLOT_NAMES)
    man['units'] = 'metres in Blender/FBX; Unreal centimetres (FBX unit scale handles the x100)'
    man['impostor_atlas'] = dict(size=IMP_ATLAS, cell=IMP_CELL, layout='cell = species*12 + variant*3 + view (0 side XZ, 1 side YZ, 2 top); col = cell % 8, row from top = cell // 8')
    man['leaf_atlas'] = {k: dict(rect_uv_blender=[round(x, 5) for x in tiles.uv_rect(k)], metres=tiles.tile_size(k)) for k in tiles.LAYOUT}
    os.makedirs(os.path.dirname(man_path), exist_ok=True)
    with open(man_path, 'w') as f:
        json.dump(man, f, indent=1)


def pack_impostors(root, log):
    src = os.path.join(root, 'build', 'impostor')
    atlas = np.zeros((IMP_ATLAS, IMP_ATLAS, 4), np.float32)
    n = 0
    for sp in SPECIES:
        for v in range(spc.NUM_VARIANTS):
            for k in range(3):
                p = os.path.join(src, '%s_%d_%d.png' % (sp, v, k))
                if not os.path.exists(p):
                    continue
                img = im.dilate(im.load_png(p))
                i, _ = imp_cell(sp, v, k)
                col, row = i % IMP_GRID, i // IMP_GRID
                y1 = IMP_ATLAS - row * IMP_CELL
                atlas[y1 - IMP_CELL:y1, col * IMP_CELL:(col + 1) * IMP_CELL] = img
                n += 1
    im.save_png(os.path.join(root, 'textures', 'T_TreeImpostors.png'), atlas)
    # normal atlas (verification addition): same cells; view-frame normal as 0.5 + 0.5 n, stored linear (no sRGB)
    natlas = np.zeros((IMP_ATLAS, IMP_ATLAS, 4), np.float32)
    natlas[..., :3] = 0.5
    for sp in SPECIES:
        for v in range(spc.NUM_VARIANTS):
            for k in range(3):
                p = os.path.join(src, '%s_%d_%dn.png' % (sp, v, k))
                if not os.path.exists(p):
                    continue
                img = im.dilate(im.load_png(p))           # renders hold sRGB-encoded emission: dilate averages linearly
                img[..., :3] = im.srgb_to_lin(img[..., :3])
                i, _ = imp_cell(sp, v, k)
                col, row = i % IMP_GRID, i // IMP_GRID
                y1 = IMP_ATLAS - row * IMP_CELL
                natlas[y1 - IMP_CELL:y1, col * IMP_CELL:(col + 1) * IMP_CELL] = img
    natlas[..., 3] = 1.0
    im.save_png(os.path.join(root, 'textures', 'T_TreeImpostorsN.png'), natlas)
    # review sheet: rows used so far, over sky grey, half resolution
    used = max(1, max([imp_cell(sp, v, 2)[0] for sp in SPECIES for v in range(spc.NUM_VARIANTS)
                        if os.path.exists(os.path.join(src, '%s_%d_2.png' % (sp, v)))] + [0]) // IMP_GRID + 1)
    sheet = atlas[IMP_ATLAS - used * IMP_CELL:].copy()
    sheet = im.downsample2(sheet)
    clip = (sheet[..., 3:4] > 0.4).astype(np.float32)
    sheet[..., :3] = sheet[..., :3] * clip + np.array([0.62, 0.7, 0.78], np.float32) * (1 - clip)
    sheet[..., 3] = 1
    im.save_png(os.path.join(src, 'review_sheet.png'), sheet)
    log('impostor atlas: %d views' % n)
