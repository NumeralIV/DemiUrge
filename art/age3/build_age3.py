# DEMIURGE art - Age3 (Sail Age) kit builder. Generates textures, materials, every mesh with its LODs and
# damage states, FBX exports, the manifest and the source .blend.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/age3/build_age3.py -- [--textures] [--only Townhouse_A,Pier] [--no-export]
#
#   --textures   regenerate the texture sets even if they exist
#   --only       build a subset (the manifest keeps the other entries it already has)
#   --no-export  skip the FBX files (faster iteration on the .blend / previews)
import json
import math
import os
import random
import sys
import time

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import age3_tex as TX  # noqa: E402
import age3_geo as G  # noqa: E402
import age3_assets as A  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
TEXDIR = os.path.join(HERE, 'textures')
EXPDIR = os.path.join(HERE, 'export')
SRCDIR = os.path.join(HERE, 'source')
FAMILY = 'Age3'
TAGS = ['wall', 'roof', 'upper', 'base', 'prop', 'debris', 'column', 'drum', 'jib', 'wheel', 'quay', 'coping', 'deck',
        'pile', 'scaffold']
SCREEN = [1.0, 0.35, 0.12, 0.04]

# culture material instances: parameters the integration agent sets on MI_Age3_<Material>_<Culture>
CULTURES = {
    'Base':   dict(note='Sail-age default: cream limewash, honey stone, terracotta, lead',
                   PlasterTint=(1.0, 1.0, 1.0), StoneTint=(1.0, 1.0, 1.0), RoofBand=0, RoofTint=(1.0, 1.0, 1.0), PaintTint=(1.0, 1.0, 1.0)),
    'CultureA': dict(note='Cliff-Terrace people (warm red canyon): ochre-red plaster, deep red tiles, ochre paint',
                     PlasterTint=(1.08, 0.80, 0.62), StoneTint=(1.06, 0.92, 0.82), RoofBand=0, RoofTint=(0.92, 0.78, 0.72), PaintTint=(1.5, 1.0, 0.45)),
    'CultureB': dict(note='Lagoon-Weaver people: silver-grey limewash, moss-grey timber shingle roofs, saffron paint',
                     PlasterTint=(0.88, 0.92, 0.93), StoneTint=(0.92, 0.94, 0.95), RoofBand=2, RoofTint=(1.0, 1.0, 1.0), PaintTint=(1.9, 1.25, 0.3)),
    'CultureC': dict(note='Ring-Builder people (cool highland): pale cool limestone, blue-grey slate, blue paint',
                     PlasterTint=(0.90, 0.93, 0.98), StoneTint=(0.93, 0.95, 1.0), RoofBand=1, RoofTint=(1.0, 1.0, 1.05), PaintTint=(0.55, 0.8, 1.35)),
}


def log(*a):
    print('AGE3', *a, flush=True)


# ----------------------------------------------------------------------------------------------- textures and materials
def ensure_textures(force):
    need = [os.path.join(TEXDIR, 'T_Age3_%s_%s.png' % (m, c)) for m in ('Structure', 'Roof', 'Detail') for c in ('D', 'N', 'ORM')]
    if force or not all(os.path.exists(p) for p in need):
        t = time.time()
        info = TX.write_all(TEXDIR)
        log('textures written in %.0f s' % (time.time() - t))
        return info
    return None


def load_img(name, colour):
    p = os.path.join(TEXDIR, name)
    img = bpy.data.images.get(name) or bpy.data.images.load(p, check_existing=True)
    img.colorspace_settings.name = 'sRGB' if colour else 'Non-Color'
    return img


def make_material(mat_name, slot, culture='Base', night=0.0):
    cu = CULTURES[culture]
    m = bpy.data.materials.get(mat_name) or bpy.data.materials.new(mat_name)
    m.use_nodes = True
    m.use_backface_culling = True
    nt = m.node_tree
    nt.nodes.clear()
    N = nt.nodes
    L = nt.links
    out = N.new('ShaderNodeOutputMaterial')
    bsdf = N.new('ShaderNodeBsdfPrincipled')
    L.new(bsdf.outputs[0], out.inputs[0])
    uv = N.new('ShaderNodeUVMap')
    uv.uv_map = 'UVMap'
    vec = uv.outputs[0]
    if slot == 'Roof' and cu['RoofBand']:
        mp = N.new('ShaderNodeMapping')
        mp.inputs['Location'].default_value = (0.0, 0.25 * cu['RoofBand'], 0.0)
        L.new(uv.outputs[0], mp.inputs[0])
        vec = mp.outputs[0]
    base = 'T_Age3_' + slot
    tD = N.new('ShaderNodeTexImage')
    tD.image = load_img(base + '_D.png', True)
    tN = N.new('ShaderNodeTexImage')
    tN.image = load_img(base + '_N.png', False)
    tO = N.new('ShaderNodeTexImage')
    tO.image = load_img(base + '_ORM.png', False)
    for t in (tD, tN, tO):
        L.new(vec, t.inputs[0])
    has_m = slot in ('Structure', 'Detail')
    col = tD.outputs[0]
    # tint: Structure -> PlasterTint where M.G, StoneTint elsewhere; Roof -> RoofTint; Detail -> PaintTint where M.G
    if has_m:
        tM = N.new('ShaderNodeTexImage')
        tM.image = load_img(base + '_M.png', False)
        L.new(vec, tM.inputs[0])
        sepM = N.new('ShaderNodeSeparateColor')
        L.new(tM.outputs[0], sepM.inputs[0])
        tint_a = (cu['PlasterTint'] if slot == 'Structure' else cu['PaintTint'])
        tint_b = (cu['StoneTint'] if slot == 'Structure' else (1.0, 1.0, 1.0))
        mix = N.new('ShaderNodeMix')
        mix.data_type = 'RGBA'
        mix.inputs['A'].default_value = tint_b + (1.0,)
        mix.inputs['B'].default_value = tint_a + (1.0,)
        L.new(sepM.outputs[1], mix.inputs['Factor'])
        mul = N.new('ShaderNodeMix')
        mul.data_type = 'RGBA'
        mul.blend_type = 'MULTIPLY'
        mul.inputs['Factor'].default_value = 1.0
        L.new(col, mul.inputs['A'])
        L.new(mix.outputs['Result'], mul.inputs['B'])
        col = mul.outputs['Result']
    elif slot == 'Roof':
        mul = N.new('ShaderNodeMix')
        mul.data_type = 'RGBA'
        mul.blend_type = 'MULTIPLY'
        mul.inputs['Factor'].default_value = 1.0
        L.new(col, mul.inputs['A'])
        mul.inputs['B'].default_value = cu['RoofTint'] + (1.0,)
        col = mul.outputs['Result']
    # vertex colour (grime, water line, soot) multiplies the base colour
    va = N.new('ShaderNodeVertexColor')
    va.layer_name = 'Col'
    vm = N.new('ShaderNodeMix')
    vm.data_type = 'RGBA'
    vm.blend_type = 'MULTIPLY'
    vm.inputs['Factor'].default_value = 1.0
    L.new(col, vm.inputs['A'])
    L.new(va.outputs[0], vm.inputs['B'])
    col = vm.outputs['Result']
    # AO from ORM.R into the preview base colour (UE uses it for indirect light only)
    sep = N.new('ShaderNodeSeparateColor')
    L.new(tO.outputs[0], sep.inputs[0])
    ao = N.new('ShaderNodeMix')
    ao.data_type = 'RGBA'
    ao.blend_type = 'MULTIPLY'
    ao.inputs['Factor'].default_value = 0.6
    L.new(col, ao.inputs['A'])
    L.new(sep.outputs[0], ao.inputs['B'])
    L.new(ao.outputs['Result'], bsdf.inputs['Base Color'])
    L.new(sep.outputs[1], bsdf.inputs['Roughness'])
    L.new(sep.outputs[2], bsdf.inputs['Metallic'])
    nm = N.new('ShaderNodeNormalMap')
    nm.uv_map = 'UVMap'
    L.new(tN.outputs[0], nm.inputs['Color'])
    L.new(nm.outputs[0], bsdf.inputs['Normal'])
    if has_m and slot == 'Detail':
        em = N.new('ShaderNodeMix')
        em.data_type = 'RGBA'
        em.inputs['A'].default_value = (0, 0, 0, 1)
        em.inputs['B'].default_value = (1.0, 0.62, 0.3, 1)
        L.new(sepM.outputs[0], em.inputs['Factor'])
        L.new(em.outputs['Result'], bsdf.inputs['Emission Color'])
        bsdf.inputs['Emission Strength'].default_value = night
    m['culture'] = culture
    return m


def family_materials():
    mats = {}
    for slot in G.SLOTS:
        mats[slot] = make_material('M_%s_%s' % (FAMILY, slot), slot)
    return mats


def culture_materials():
    out = {}
    for cu in CULTURES:
        if cu == 'Base':
            continue
        for slot in G.SLOTS:
            nm = 'MI_%s_%s_%s' % (FAMILY, slot, cu)
            out[(slot, cu)] = make_material(nm, slot, cu)
            out[(slot, cu)].use_fake_user = True
    return out


# ----------------------------------------------------------------------------------------------- MB -> mesh
def grime(mb):
    """Per-corner vertex colour: darker at the foot of walls, wet and green below the water line, soot on debris."""
    cols = []
    for ids, slot, uv, cc, tag in mb.F:
        zs = [mb.V[i][2] for i in ids]
        zc = sum(zs) / len(zs)
        row = []
        for j, i in enumerate(ids):
            c = cc[j]
            z = mb.V[i][2]
            k = 1.0
            if tag in ('quay', 'coping', 'pile') or (tag == 'debris' and zc < A.WATER_Z + 0.5):
                if zc < A.WATER_Z + 0.25:
                    k = 0.55
                    row.append((0.48, 0.52, 0.42))
                    continue
                elif z < A.WATER_Z + 1.0:
                    k = 0.85
            elif slot == 0 and tag in ('wall', 'base', 'column'):
                t = max(0.0, min(1.0, (z + 0.3) / 2.2))
                k = 0.78 + 0.22 * t * t * (3 - 2 * t)
            row.append((c[0] * k, c[1] * k, c[2] * k))
        cols.append(row)
    return cols


def mesh_from_mb(mb, name):
    me = bpy.data.meshes.new(name)
    faces = [f[0] for f in mb.F]
    me.from_pydata(mb.V, [], faces)
    me.update()
    uv = me.uv_layers.new(name='UVMap')
    flat = []
    for f in mb.F:
        for u, v in f[2]:
            flat.extend((u, v))
    uv.data.foreach_set('uv', flat)
    ca = me.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    cl = []
    for row in grime(mb):
        for c in row:
            cl.extend((c[0], c[1], c[2], 1.0))
    ca.data.foreach_set('color', cl)
    me.polygons.foreach_set('material_index', [f[1] for f in mb.F])
    ta = me.attributes.new('tag', 'INT', 'FACE')
    ta.data.foreach_set('value', [TAGS.index(f[4]) if f[4] in TAGS else 0 for f in mb.F])
    me.update()
    return me


def bm_layers(bm):
    return bm.faces.layers.int.get('tag'), bm.loops.layers.uv.get('UVMap'), bm.loops.layers.color.get('Col')


def weld(bm):
    vs = set()
    for f in bm.faces:
        if f.material_index != 2:
            vs.update(f.verts)
    bmesh.ops.remove_doubles(bm, verts=list(vs), dist=0.0008)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges[:])


def delete_tags(bm, tags):
    tl, _, _ = bm_layers(bm)
    ids = {TAGS.index(t) for t in tags}
    dead = [f for f in bm.faces if f[tl] in ids]
    if dead:
        bmesh.ops.delete(bm, geom=dead, context='FACES')


def cut_sectors(bm, centre, heights, tilt, rng, tags=None, flat=False):
    """Remove everything above a jagged surface: one plane per angular sector around `centre`."""
    tl, _, _ = bm_layers(bm)
    ns = len(heights)
    sel = None if tags is None else {TAGS.index(t) for t in tags}
    for k in range(ns):
        a0 = 2 * math.pi * k / ns
        a1 = 2 * math.pi * (k + 1) / ns
        faces = []
        for f in bm.faces:
            if sel is not None and f[tl] not in sel:
                continue
            c = f.calc_center_median()
            a = math.atan2(c.y - centre[1], c.x - centre[0]) % (2 * math.pi)
            if a0 <= a < a1:
                faces.append(f)
        if not faces:
            continue
        am = (a0 + a1) / 2 + rng.uniform(-0.6, 0.6)
        if flat:
            no = (0.0, 0.0, 1.0)
        else:
            tx, ty = -math.sin(am), math.cos(am)          # tilt along the sector's tangent
            s = rng.choice((-1, 1)) * tilt
            no = G.norm((tx * s, ty * s, 1.0))
        co = (centre[0] + math.cos(am) * 3.0, centre[1] + math.sin(am) * 3.0, heights[k])
        geom = list(set(faces) | {e for f in faces for e in f.edges} | {v for f in faces for v in f.verts})
        bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-4, plane_co=co, plane_no=no, clear_outer=True)


def punch(bm, poly2d, zmin, zmax, tags):
    """Cut a hole: slice faces with `tags` along the polygon edges, then delete the pieces inside it."""
    tl, _, _ = bm_layers(bm)
    sel = {TAGS.index(t) for t in tags}
    n = len(poly2d)
    for i in range(n):
        a, b = poly2d[i], poly2d[(i + 1) % n]
        dx, dy = b[0] - a[0], b[1] - a[1]
        no = G.norm((dy, -dx, 0.0))
        faces = [f for f in bm.faces if f[tl] in sel and zmin - 1 < f.calc_center_median().z < zmax + 1]
        if not faces:
            return
        geom = list(set(faces) | {e for f in faces for e in f.edges} | {v for f in faces for v in f.verts})
        bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-4, plane_co=(a[0], a[1], 0.0), plane_no=no, clear_outer=False, clear_inner=False)
    dead = []
    for f in bm.faces:
        if f[tl] not in sel:
            continue
        c = f.calc_center_median()
        if zmin <= c.z <= zmax and _inside(poly2d, c.x, c.y):
            dead.append(f)
    if dead:
        bmesh.ops.delete(bm, geom=dead, context='FACES')


def _inside(poly, x, y):
    ins = False
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        if (a[1] > y) != (b[1] > y):
            xi = a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
            if x < xi:
                ins = not ins
    return ins


def soot(bm, centre, radius, zmin, strength=0.55):
    _, _, cl = bm_layers(bm)
    if cl is None:
        return
    for f in bm.faces:
        for lp in f.loops:
            p = lp.vert.co
            d = math.hypot(p.x - centre[0], p.y - centre[1])
            if p.z < zmin:
                continue
            k = max(0.0, 1.0 - d / radius) * min(1.0, (p.z - zmin) / 2.0)
            if k > 0:
                c = lp[cl]
                f_ = 1.0 - strength * k
                lp[cl] = (c[0] * f_, c[1] * f_ * 0.97, c[2] * f_ * 0.95, 1.0)


def roof_underside(bm, centre, radius, off=0.06, zmin=-1e9):
    """Verifier fix: the roofs are single-sided, so through the burnt hole of the damaged state the far slopes were
    seen from behind and culled (you looked through the roof). Add a charred, inward-facing copy of the roof
    covering around the hole."""
    tl, uvl, cl = bm_layers(bm)
    rid = TAGS.index('roof')
    faces = [f for f in bm.faces if f[tl] == rid and f.material_index == 1
             and math.hypot(f.calc_center_median().x - centre[0], f.calc_center_median().y - centre[1]) < radius
             and f.calc_center_median().z > zmin]
    if not faces:
        return
    ret = bmesh.ops.duplicate(bm, geom=faces)
    nf = [g for g in ret['geom'] if isinstance(g, bmesh.types.BMFace)]
    nv = [g for g in ret['geom'] if isinstance(g, bmesh.types.BMVert)]
    for f in nf:
        f.normal_update()
    moves = {}
    for v in nv:
        n = [f.normal for f in v.link_faces if f in set(nf)]
        if n:
            m = sum((x for x in n[1:]), n[0].copy()) / len(n)
            moves[v] = m
    for v, m in moves.items():
        v.co -= m * off
    bmesh.ops.reverse_faces(bm, faces=nf)
    u0, v0, u1, v1 = TX.cell_uv('charred')
    xs = [v.co.x for v in nv]
    ys = [v.co.y for v in nv]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    for f in nf:
        f.material_index = 2
        for lp in f.loops:
            p = lp.vert.co
            lp[uvl].uv = (u0 + (p.x - x0) / max(x1 - x0, 1e-3) * (u1 - u0), v0 + (p.y - y0) / max(y1 - y0, 1e-3) * (v1 - v0))
            if cl is not None:
                lp[cl] = (0.5, 0.47, 0.45, 1.0)


def swap_cell(bm, from_cell, to_cell, fraction, rng):
    _, uvl, _ = bm_layers(bm)
    fu0, fv0, fu1, fv1 = TX.cell_uv(from_cell)
    tu0, tv0, tu1, tv1 = TX.cell_uv(to_cell)
    for f in bm.faces:
        if f.material_index != 2:
            continue
        us = [lp[uvl].uv for lp in f.loops]
        cu = sum(u.x for u in us) / len(us)
        cv = sum(u.y for u in us) / len(us)
        if fu0 <= cu <= fu1 and fv0 <= cv <= fv1 and rng.random() < fraction:
            for lp in f.loops:
                u = lp[uvl].uv
                lp[uvl].uv = (tu0 + (u.x - fu0), tv0 + (u.y - fv0))


# ----------------------------------------------------------------------------------------------- state builders
def offset_poly(poly, off):
    n = len(poly)
    out = []
    for i in range(n):
        a, b, c = poly[i - 1], poly[i], poly[(i + 1) % n]
        n0 = G.norm((b[1] - a[1], -(b[0] - a[0]), 0.0))
        n1 = G.norm((c[1] - b[1], -(c[0] - b[0]), 0.0))
        m = G.norm((n0[0] + n1[0], n0[1] + n1[1], 0.0))
        k = 1.0 / max(0.4, m[0] * n1[0] + m[1] * n1[1])
        out.append((b[0] + m[0] * off * k, b[1] + m[1] * off * k))
    return out


def scaffold(mb, foot, ztop, lod, rng, off=1.0):
    if lod >= 3:
        return
    ring = offset_poly(foot, off)
    inner = offset_poly(foot, 0.15)
    n = len(ring)
    lifts = []
    z = 2.0
    while z < ztop - 0.3:
        lifts.append(z)
        z += 2.0
    lifts.append(ztop)
    sides = 4 if lod == 0 else 3
    for i in range(n):
        a, b = ring[i], ring[(i + 1) % n]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        k = max(1, int(round(L / (2.4 if lod < 2 else 6.0))))
        for j in range(k + 1 if i == n - 1 else k):
            t = j / k
            x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            if lod <= 1:
                mb.cyl((x, y, 0), 0.07, 0.06, A.FOOT, ztop + 1.0, sides, 'beam', mode='axis', cap_top=False, tag='scaffold')
            else:
                mb.box((x, y, (ztop + 1.0 + A.FOOT) / 2), 0.07, 0.07, (ztop + 1.0 - A.FOOT) / 2, 'beam', top=False, mode='fit', tag='scaffold')
        for z in (lifts if lod <= 1 else lifts[-1:]):
            mb.beam((a[0], a[1], z), (b[0], b[1], z), 0.08, 0.08, 'beam', tag='scaffold', caps=False)
        if lod == 0:
            mb.beam((a[0], a[1], 0.2), (b[0], b[1], min(ztop, 0.2 + L * 0.6)), 0.06, 0.06, 'beam', tag='scaffold', caps=False)
        # board walks on every other lift
        if lod <= 1:
            ia, ib = inner[i], inner[(i + 1) % n]
            for z in lifts[1::2] + [lifts[-1]]:
                q = [(ia[0], ia[1], z + 0.05), (ib[0], ib[1], z + 0.05), (b[0], b[1], z + 0.05), (a[0], a[1], z + 0.05)]
                if G.newell(q)[2] < 0:
                    q = list(reversed(q))
                mb.poly(q, 'planks', 'fit', tag='scaffold')
                mb.poly(list(reversed(q)), 'planks', 'fit', tag='scaffold')


def site_materials(mb, foot, lod, rng):
    """Stacks of cut stone and timber beside the building site."""
    if lod >= 2:
        return
    xs = [p[0] for p in foot]
    ys = [p[1] for p in foot]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    sx, sy = x1 + 2.6, (y0 + y1) / 2
    for i in range(3):
        for j in range(2 - (i // 2)):
            mb.box((sx + j * 1.0, sy + i * 0.0, 0.25 + i * 0.5), 0.45, 0.9, 0.25, 'ashlar', mode='fit', tag='debris')
    for i in range(4):
        mb.beam((x0 - 0.5 + i * 0.3, y0 - 2.4, 0.15 + (i % 2) * 0.25), (x0 + 4.5 + i * 0.3, y0 - 2.4 - 0.3 * (i % 2), 0.15 + (i % 2) * 0.25), 0.25, 0.25, 'beam', tag='debris')


def build_variant(asset, fn, kind, state, lod, seed):
    st = state.lower()
    rng = random.Random(seed * 31)
    mb, info = fn(lod, st)
    me = mesh_from_mb(mb, 'tmp')
    bm = bmesh.new()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)
    weld(bm)
    extra = G.MB(seed + 7)
    foot = info['footprint']
    eave = info.get('eave', 6.0)
    base = info.get('base', 0.0)
    cx = sum(p[0] for p in foot) / len(foot)
    cy = sum(p[1] for p in foot) / len(foot)
    walls_bm = None
    thick = {'lighthouse': 0.7, 'civic': 0.8}.get(kind, 0.45)
    if kind in ('quay', 'pier'):
        pass
    elif st == 'construction':
        delete_tags(bm, ['roof', 'upper', 'jib', 'wheel', 'drum'] + (['column'] if kind == 'civic' else []))
        frac = {'lighthouse': 0.45, 'crane': 0.55, 'civic': 0.45}.get(kind, 0.58)
        zc = base + (eave - base) * frac
        hs = [zc + rng.uniform(-1.0, 1.0) for _ in range(3)]
        cut_sectors(bm, (cx, cy), hs, 0.0, rng, flat=True)
        if kind == 'civic' and lod <= 2:
            # column drums rising
            for x in (-9.0, -5.4, -1.8, 1.8, 5.4, 9.0):
                h = rng.uniform(1.5, 5.0)
                extra.cyl((x, -16.0, 0), 0.62, 0.6, 1.5, 1.5 + h, 10 if lod == 0 else 6, 'dressed', mode='axis', tag='column')
        walls_bm = split_tag(bm, ['wall', 'drum'])
        if kind != 'crane':
            scaffold(extra, foot if kind != 'civic' else G.rect_path(-14.0, -8.0, 14.0, 15.0), max(hs) + 0.6, lod, rng,
                     off=1.0 if kind != 'lighthouse' else 0.8)
            site_materials(extra, foot, lod, rng)
        else:
            site_materials(extra, foot, lod, rng)
    elif st == 'ruin':
        delete_tags(bm, ['roof', 'upper', 'jib', 'wheel'])
        lo, hi = {'lighthouse': (0.25, 0.55), 'crane': (0.2, 0.45), 'civic': (0.35, 0.7)}.get(kind, (0.3, 0.68))
        hs = [base + (eave - base) * rng.uniform(lo, hi) for _ in range(6)]
        cut_sectors(bm, (cx, cy), hs, 0.35, rng)
        walls_bm = split_tag(bm, ['wall', 'drum'])
        span = max(info.get('W', 8), info.get('D', 8))
        if lod <= 2:
            rubble_mound = A.rubble_mound
            rubble_mound(extra, cx, cy, base, span * 0.22, min(2.4, 0.18 * span), rng, lod, band='rubble', blocks=4, beams=2, charred=True)
            for _ in range(3 if lod == 0 else 2):
                p = foot[rng.randrange(len(foot))]
                ang = math.atan2(p[1] - cy, p[0] - cx)
                rubble_mound(extra, p[0] + math.cos(ang) * 1.2, p[1] + math.sin(ang) * 1.2, 0.0, rng.uniform(1.0, 1.8), rng.uniform(0.5, 0.9),
                             rng, lod, band='rubble', blocks=3, beams=1, charred=True)
    elif st == 'damaged':
        planes = info.get('roof_planes') or []
        if kind == 'civic':
            planes = planes[:2]
        if planes and kind != 'lighthouse':
            pl = planes[rng.randrange(len(planes))] if kind != 'civic' else planes[0]
            e0, e1, r1, r0 = pl[0], pl[1], pl[2], pl[3]
            tu = rng.uniform(0.3, 0.7)
            tv = rng.uniform(0.35, 0.6)
            ea = G.lerp3(e0, e1, tu)
            ra = G.lerp3(r0, r1, tu)
            hc = G.lerp3(ea, ra, tv)
            span = min(G.length(G.sub(e1, e0)), G.length(G.sub(r0, e0)) * 2)
            rad = max(1.2, min(3.2, span * 0.22))
            poly = [(hc[0] + math.cos(a) * rad * rng.uniform(0.75, 1.2), hc[1] + math.sin(a) * rad * rng.uniform(0.75, 1.2))
                    for a in [2 * math.pi * i / 7 for i in range(7)]]
            punch(bm, poly, eave - 1.0, 1e3, ['roof'])
            # charred rafters across the hole, burnt attic floor below
            ax = G.norm(G.sub(e1, e0))
            for k in range(-3, 4):
                t = tu + k * 0.55 / max(G.length(G.sub(e1, e0)), 1.0)
                if not 0.02 < t < 0.98:
                    continue
                pa = G.lerp3(e0, e1, t)
                pb = G.lerp3(r0, r1, t)
                pa = (pa[0], pa[1], pa[2] - 0.18)
                pb = (pb[0], pb[1], pb[2] - 0.18)
                if rng.random() < 0.35:
                    pb = G.lerp3(pa, pb, rng.uniform(0.35, 0.7))
                if lod <= 1 or k % 2 == 0:
                    extra.beam(pa, pb, 0.14, 0.18, cell='charred', tag='roof', caps=lod == 0)
            if lod <= 1:
                fz = eave - 0.25
                q = [(p[0], p[1], fz) for p in offset_poly(poly, 0.4)]
                if G.newell(q)[2] < 0:
                    q = list(reversed(q))
                extra.apoly(q, 'charred', tag='roof')
            soot(bm, hc, rad * 2.2, eave - 3.5, 0.6)
            if lod <= 2:
                roof_underside(bm, hc, rad * 3.0)
        if kind == 'civic' and 'ridge_z' in info:
            # verifier: the gable-roof hole hid under the drum and the damaged temple read as intact from 300 m;
            # also break the dome on the front-left shoulder (lead and ribs), with a charred lining and soot
            zd1 = info['ridge_z'] + 5.2
            ang = -2.2
            dc = (4.2 * math.cos(ang), 3.5 + 4.2 * math.sin(ang))
            dpoly = [(dc[0] + math.cos(a) * 2.4 * rng.uniform(0.75, 1.2), dc[1] + math.sin(a) * 2.4 * rng.uniform(0.75, 1.2))
                     for a in [2 * math.pi * i / 7 for i in range(7)]]
            punch(bm, dpoly, zd1 + 0.8, 1e3, ['roof', 'wall'])
            soot(bm, dc, 5.5, zd1 - 1.0, 0.6)
            if lod <= 2:
                # line the whole dome and close its spring line with a charred floor, so the hole does not show
                # the sky through the culled far side
                roof_underside(bm, (0.0, 3.5), 9.0, zmin=zd1 - 0.3)
                nd = 24
                extra.apoly([(7.3 * math.cos(2 * math.pi * j / nd), 3.5 + 7.3 * math.sin(2 * math.pi * j / nd), zd1 + 0.05) for j in range(nd)],
                            'charred', tag='roof')
        if kind == 'lighthouse':
            swap_cell(bm, 'lantern', 'window_broken', 0.6, rng)
            soot(bm, (0.0, 0.0), 6.0, eave - 5.0, 0.5)
        if kind == 'crane':
            delete_tags(bm, ['jib'])
            jt = info['jib_tip']
            j0 = (0.0, -2.0, 10.9)
            mid = G.lerp3(j0, jt, 0.35)
            extra.beam(j0, mid, 0.38, 0.48, cell='charred', tag='jib')
            extra.beam((0.6, -6.0, 0.3), (1.8, -12.5, 0.9), 0.38, 0.48, 'beam', tag='debris')
        if lod <= 2:
            for _ in range(2):
                p = foot[rng.randrange(len(foot))]
                ang = math.atan2(p[1] - cy, p[0] - cx)
                A.rubble_mound(extra, p[0] + math.cos(ang) * 1.0, p[1] + math.sin(ang) * 1.0, 0.0, rng.uniform(0.8, 1.3), rng.uniform(0.4, 0.7),
                               rng, lod, band='rubble', blocks=3, beams=1, charred=True)
    # thicken the cut walls (rims fill the openings and the broken tops)
    me_out = bpy.data.meshes.new('out')
    pieces = []
    if walls_bm is not None and kind == 'crane':
        wm = bpy.data.meshes.new('walls')
        walls_bm.to_mesh(wm)
        walls_bm.free()
        pieces.append(wm)
        walls_bm = None
        wm = None
    if walls_bm is not None:
        wm = bpy.data.meshes.new('walls')
        walls_bm.to_mesh(wm)
        walls_bm.free()
        pieces.append(solidify(wm, thick))
        bpy.data.meshes.remove(wm)
        if st == 'ruin':
            pass
    rest = bpy.data.meshes.new('rest')
    bm.to_mesh(rest)
    bm.free()
    pieces.append(rest)
    if extra.F:
        em = mesh_from_mb(extra, 'extra')
        pieces.append(em)
    out = bmesh.new()
    for p in pieces:
        out.from_mesh(p)
    if st == 'ruin':
        soot(out, (cx, cy), max(info.get('W', 8), info.get('D', 8)) * 0.9, base + 0.5, 0.45)
    # verifier fix: the bisect / delete passes left loose vertices and edges (71 of 192 meshes), which inflated the
    # bounds (e.g. Lighthouse_Construction 23 m instead of 13.6 m) and so the manifest heights and Unreal's bounds
    loose_e = [e for e in out.edges if not e.link_faces]
    if loose_e:
        bmesh.ops.delete(out, geom=loose_e, context='EDGES')
    loose_v = [v for v in out.verts if not v.link_faces]
    if loose_v:
        bmesh.ops.delete(out, geom=loose_v, context='VERTS')
    # verifier fix: pivot at the centre of the design footprint (the civic temple's was 1.25 m off in Y);
    # docks keep their own frame so the quay segments tile
    if kind not in ('quay', 'pier') and (abs(cx) > 1e-3 or abs(cy) > 1e-3):
        bmesh.ops.translate(out, verts=out.verts, vec=(-cx, -cy, 0.0))
    out.to_mesh(me_out)
    out.free()
    for p in pieces:
        bpy.data.meshes.remove(p)
    return me_out, info


def split_tag(bm, tags):
    """Move faces with `tags` into a new bmesh (returned); they are deleted from bm."""
    tl, _, _ = bm_layers(bm)
    ids = {TAGS.index(t) for t in tags}
    me = bpy.data.meshes.new('split')
    bm.to_mesh(me)
    nb = bmesh.new()
    nb.from_mesh(me)
    bpy.data.meshes.remove(me)
    tl2 = nb.faces.layers.int.get('tag')
    bmesh.ops.delete(nb, geom=[f for f in nb.faces if f[tl2] not in ids], context='FACES')
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f[tl] in ids], context='FACES')
    return nb


def solidify(me, thick):
    ob = bpy.data.objects.new('_solid', me)
    bpy.context.scene.collection.objects.link(ob)
    md = ob.modifiers.new('S', 'SOLIDIFY')
    md.thickness = thick
    md.offset = -1.0
    md.use_rim = True
    md.use_even_offset = True
    md.use_quality_normals = True
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    out = bpy.data.meshes.new_from_object(ev)
    bpy.data.objects.remove(ob)
    return out


# ----------------------------------------------------------------------------------------------- finish, check, export
def finish_mesh(me, name, mats):
    me.name = name
    used = sorted({p.material_index for p in me.polygons})
    remap = {s: i for i, s in enumerate(used)}
    idx = [remap[p.material_index] for p in me.polygons]
    me.materials.clear()
    for s in used:
        me.materials.append(mats[G.SLOTS[s]])
    me.polygons.foreach_set('material_index', idx)
    try:
        me.shade_smooth()
        me.set_sharp_from_angle(angle=math.radians(38))
    except Exception as e:  # older API fallback
        for p in me.polygons:
            p.use_smooth = False
    me.update()
    return [G.SLOTS[s] for s in used]


def tri_count(me):
    return sum(len(p.vertices) - 2 for p in me.polygons)


def check_mesh(me):
    bm = bmesh.new()
    bm.from_mesh(me)
    bnd = sum(1 for e in bm.edges if e.is_boundary)
    nm = sum(1 for e in bm.edges if not e.is_manifold and not e.is_boundary)
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    uvl = bm.loops.layers.uv.get('UVMap')
    vmin = min(lp[uvl].uv.y for f in bm.faces for lp in f.loops) if bm.faces else 0
    vmax = max(lp[uvl].uv.y for f in bm.faces for lp in f.loops) if bm.faces else 0
    bm.free()
    return dict(bounds=[round(min(xs), 2), round(min(ys), 2), round(min(zs), 2), round(max(xs), 2), round(max(ys), 2), round(max(zs), 2)],
                boundary_edges=bnd, nonmanifold_edges=nm, v_range=[round(vmin, 3), round(vmax, 3)])


def export_fbx(objs, group, path):
    bpy.ops.object.select_all(action='DESELECT')
    group.select_set(True)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = group
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'EMPTY', 'MESH'}, apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', mesh_smooth_type='FACE', use_tspace=True, use_mesh_modifiers=True,
                             add_leaf_bones=False, bake_anim=False, path_mode='STRIP', embed_textures=False, use_custom_props=False,
                             axis_forward='-Z', axis_up='Y', colors_type='SRGB', use_triangles=True)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    force_tex = '--textures' in argv
    no_export = '--no-export' in argv
    only = None
    for i, a in enumerate(argv):
        if a == '--only':
            only = set(argv[i + 1].split(','))
    t0 = time.time()
    os.makedirs(EXPDIR, exist_ok=True)
    os.makedirs(SRCDIR, exist_ok=True)
    tex_info = ensure_textures(force_tex)
    # fresh scene
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = family_materials()
    culture_materials()
    man_path = os.path.join(HERE, 'manifest.json')
    man = {}
    if os.path.exists(man_path):
        with open(man_path) as f:
            man = json.load(f)
    meshes = man.get('meshes', {})
    root_col = bpy.data.collections.new('Age3')
    bpy.context.scene.collection.children.link(root_col)
    for asset, (fn, kind, states, desc) in A.ASSETS.items():
        if only and asset not in only:
            continue
        col = bpy.data.collections.new(asset)
        root_col.children.link(col)
        seed = sum(ord(c) for c in asset)
        for state in states:
            name = 'SM_%s_%s_%s' % (FAMILY, asset, state)
            group = bpy.data.objects.new(name, None)
            group['fbx_type'] = 'LodGroup'
            col.objects.link(group)
            lods = []
            tris = []
            info0 = None
            slots_used = set()
            checks = []
            for lod in range(4):
                me, info = build_variant(asset, fn, kind, state, lod, seed)
                slots = finish_mesh(me, '%s_LOD%d' % (name, lod), mats)
                slots_used.update(slots)
                ob = bpy.data.objects.new('%s_LOD%d' % (name, lod), me)
                ob.parent = group
                col.objects.link(ob)
                lods.append(ob)
                tris.append(tri_count(me))
                checks.append(check_mesh(me))
                if lod == 0:
                    info0 = info
            # keep LOD objects of later states out of the way in the .blend (FBX is exported from the origin)
            path = os.path.join(EXPDIR, name + '.fbx')
            if not no_export:
                export_fbx(lods, group, path)
            b = checks[0]['bounds']
            entry = dict(asset=asset, family=FAMILY, state=state, kind=kind, description=desc,
                         footprint_m=[round(b[3] - b[0], 2), round(b[4] - b[1], 2)],
                         height_m=round(b[5], 2), bounds_lod0=b,
                         tris=tris, lod_ratio=[round(t / max(tris[0], 1), 3) for t in tris], lod_screen_size=SCREEN,
                         materials=[s for s in G.SLOTS if s in slots_used],
                         material_names=['M_%s_%s' % (FAMILY, s) for s in G.SLOTS if s in slots_used],
                         textures=['T_Age3_%s_%s' % (s, c) for s in G.SLOTS if s in slots_used for c in (('D', 'N', 'ORM', 'M') if s != 'Roof' else ('D', 'N', 'ORM'))],
                         states=states, culture_variants=list(CULTURES), roof_band_swappable=kind in ('house', 'warehouse', 'crane'),
                         fbx='art/age3/export/%s.fbx' % name, licence='original', script='art/age3/build_age3.py',
                         checks=dict(lod0=checks[0], lod3=checks[3]))
            # verifier: the bounds above include scaffolding, site stacks and debris; also record the design
            # footprint (walls / quay block / pier deck), identical in every state, for placement
            fp = info0['footprint']
            entry['footprint_design_m'] = [round(max(p[0] for p in fp) - min(p[0] for p in fp), 2),
                                           round(max(p[1] for p in fp) - min(p[1] for p in fp), 2)]
            for k in ('eave', 'ridge', 'water_z', 'light'):
                if info0 and k in info0:
                    entry[k] = info0[k] if not isinstance(info0[k], float) else round(info0[k], 2)
            meshes[name] = entry
            log(name, 'tris', tris, 'slots', entry['materials'], 'bnd', [c['boundary_edges'] for c in checks])
    man = dict(family=FAMILY, generated=time.strftime('%Y-%m-%d'), units='metres (1 BU = 1 m; Unreal imports x100)',
               axes='Z up, front faces -Y (street / water side)', pivot='centre of the footprint at ground level; quays and pier: top surface at Z=0',
               water_z=A.WATER_Z,
               lod_import='each FBX holds an Empty with fbx_type=LodGroup and its children <mesh>_LOD0..LOD3 (Unreal: Import Mesh LODs)',
               materials={'M_Age3_Structure': 'trim sheet: plaster, ashlar, rusticated quay stone, planks, beam, dressed stone, rubble',
                          'M_Age3_Roof': 'four covering bands: terracotta (0), slate (1), timber shingle (2), lead (3); RoofBand shifts UV V by 0.25 * k',
                          'M_Age3_Detail': '4x4 atlas: windows, doors, arcade, lantern glass, iron, rope, canvas, gilt, crate, charred, boarded, broken window'},
               material_parameters={'Structure': 'PlasterTint (where T_Age3_Structure_M.G), StoneTint (elsewhere)',
                                    'Roof': 'RoofBand (0-3, V offset 0.25 * k; only on meshes with roof_band_swappable), RoofTint',
                                    'Detail': 'PaintTint (where T_Age3_Detail_M.G); night emissive = T_Age3_Detail_M.R'},
               vertex_colour='Col (corner, sRGB bytes): multiply into base colour (ground grime, wet quay foot below the water line, soot on damaged states)',
               textures={'dir': 'art/age3/textures', 'size': 1024, 'srgb': 'D only', 'normal': 'OpenGL (+Y), flip green on import to Unreal',
                         'bands': {k: dict(slot=v[0], v0=round(v[1], 4), v1=round(v[2], 4), metres_tall=v[3], metres_wide=v[4]) for k, v in TX.BANDS.items()},
                         'detail_cells': TX.CELLS},
               cultures=CULTURES, meshes=meshes,
               mapping_hint={'Townhouse': 'EDmTownMesh::Townhouse variants (Age 3): A, B, C, D', 'Warehouse': 'EDmTownMesh::Warehouse variant 0 (Age 3): A, B',
                             'Pier': 'EDmTownMesh::Pier variant 0 (deck at Z=0 here, pivot at the centre, not the shore end)',
                             'Quay': 'port frontage segments (no stand-in id yet)', 'PortCrane': 'EDmTownMesh::Crane (Age 3)',
                             'Lighthouse': 'EDmTownMesh::Lighthouse (Age 3)', 'CivicDome': 'EDmTownMesh::TempleClassic variant 1 (Age 3 domed temple)'})
    with open(man_path, 'w') as f:
        json.dump(man, f, indent=1)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(SRCDIR, 'age3.blend'), compress=True)
    log('done in %.0f s' % (time.time() - t0))


if __name__ == '__main__':
    main()
