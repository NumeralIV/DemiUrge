# DEMIURGE art - Age4 materials: master materials (Blender preview equivalents of the UE masters) and the
# culture material-instance table that the manifest publishes for the integration agent.
#
# UE master material contract (what every M_Age4_* expects; the integration agent builds these in UE):
#   BaseColor = D * Tint, after Desaturate (lerp to luminance), then soot: BaseColor *= 1 - 0.8 * Col.G,
#               accent: where Col.B > 0.5 BaseColor = luminance(D) * 2 * AccentTint (painted doors / frames).
#   Normal = N (OpenGL, flip green on import).  ORM: R occlusion, G roughness (+ RoughnessAdd, + 0.3 * Col.G),
#   B metallic.  Night: Emissive = M.R * WindowColour * step(Col.R, LitFraction) * Night   (Detail only).
#   Macro variation: multiply BaseColor by a world-space noise (period ~25 m, +-8 %) to hide tiling.
import os

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
TEXDIR = os.path.normpath(os.path.join(HERE, '..', 'textures'))

MASTERS = {
    # material name: (texture set, tile metres or None for the trim UV atlas)
    'M_Age4_Brick': ('Brick', 1.8),
    'M_Age4_Slate': ('Slate', 2.0),
    'M_Age4_Stone': ('Stone', 2.4),
    'M_Age4_Gravel': ('Gravel', 4.0),
    'M_Age4_Trim': ('Trim', None),
}

# Culture instances. Cultures from docs/concept/settlements: Base (the shared Industry-age look),
# A Cliff-Terrace (warm rose-ochre, cobalt doors), B Lagoon-Weaver (silver-grey, moss, saffron),
# C Ring-Builder (pale stone, blue-grey slate).
CULTURES = ('Base', 'A', 'B', 'C')
CULTURE_PARAMS = {
    # Base brick toned to the concept's soot-darkened red-brown ("palette darkens and desaturates"); at
    # Tint 1 / Desaturate 0 it rendered as clean orange-red, brighter than the age-4 and factory concepts
    # (verifier change, 2026-11-19).
    'Base': {'Brick': dict(Tint=(0.84, 0.78, 0.76), Desaturate=0.12, RoughnessAdd=0.0),
             'Slate': dict(Tint=(1.0, 1.0, 1.0), Desaturate=0.0, RoughnessAdd=0.0),
             'Trim': dict(Tint=(1.0, 1.0, 1.0), Desaturate=0.0, RoughnessAdd=0.0, AccentTint=(0.20, 0.32, 0.24)),
             'Stone': dict(Tint=(1.0, 1.0, 1.0), Desaturate=0.0, RoughnessAdd=0.0),
             'Gravel': dict(Tint=(1.0, 1.0, 1.0), Desaturate=0.0, RoughnessAdd=0.0)},
    'A': {'Brick': dict(Tint=(1.22, 0.98, 0.84), Desaturate=0.15, RoughnessAdd=0.03),
          'Slate': dict(Tint=(2.2, 1.3, 0.95), Desaturate=0.6, RoughnessAdd=0.05),
          'Trim': dict(Tint=(1.08, 1.0, 0.92), Desaturate=0.0, RoughnessAdd=0.0, AccentTint=(0.10, 0.22, 0.62)),
          'Stone': dict(Tint=(1.15, 0.98, 0.86), Desaturate=0.1, RoughnessAdd=0.0),
          'Gravel': dict(Tint=(1.2, 0.95, 0.8), Desaturate=0.2, RoughnessAdd=0.0)},
    'B': {'Brick': dict(Tint=(0.92, 0.95, 0.98), Desaturate=0.7, RoughnessAdd=0.04),
          'Slate': dict(Tint=(1.55, 1.68, 1.5), Desaturate=0.5, RoughnessAdd=0.08),
          'Trim': dict(Tint=(0.95, 0.98, 1.0), Desaturate=0.2, RoughnessAdd=0.02, AccentTint=(0.85, 0.52, 0.12)),
          'Stone': dict(Tint=(0.95, 0.97, 1.0), Desaturate=0.5, RoughnessAdd=0.0),
          'Gravel': dict(Tint=(0.95, 0.97, 1.0), Desaturate=0.3, RoughnessAdd=0.0)},
    'C': {'Brick': dict(Tint=(1.02, 1.0, 0.98), Desaturate=0.1, RoughnessAdd=0.0, BaseColorTexture='T_Age4_BrickBuff_D'),
          'Slate': dict(Tint=(0.95, 1.12, 1.42), Desaturate=0.3, RoughnessAdd=-0.04),
          'Trim': dict(Tint=(0.97, 0.99, 1.02), Desaturate=0.1, RoughnessAdd=0.0, AccentTint=(0.20, 0.26, 0.36)),
          'Stone': dict(Tint=(1.08, 1.08, 1.06), Desaturate=0.4, RoughnessAdd=0.0),
          'Gravel': dict(Tint=(1.0, 1.0, 1.02), Desaturate=0.2, RoughnessAdd=0.0)},
}


# Extra instances (not cultures): heap types for the spoil meshes.
EXTRA = {
    'Coal': {'Gravel': dict(Tint=(0.26, 0.26, 0.27), Desaturate=0.9, RoughnessAdd=-0.3)},
    'Ore': {'Gravel': dict(Tint=(2.0, 0.85, 0.52), Desaturate=0.65, RoughnessAdd=0.0)},
}


def params(tset, culture):
    if culture in EXTRA:
        return EXTRA[culture].get(tset, CULTURE_PARAMS['Base'][tset])
    return CULTURE_PARAMS[culture][tset]


def mi_name(master, culture):
    return 'MI_Age4_%s_%s' % (master.replace('M_Age4_', ''), culture)


def tex_files(master):
    tset, _ = MASTERS[master]
    names = ['T_Age4_%s_D' % tset, 'T_Age4_%s_N' % tset, 'T_Age4_%s_ORM' % tset]
    if tset == 'Trim':
        names.append('T_Age4_Trim_M')
    return names


def _img(name, noncolor):
    path = os.path.join(TEXDIR, name + '.png')
    img = bpy.data.images.get(name + '.png')
    if img is None:
        img = bpy.data.images.load(path, check_existing=True)
    img.colorspace_settings.name = 'Non-Color' if noncolor else 'sRGB'
    return img


def make_material(master, culture='Base', name=None, night=0.0):
    """Blender preview material equivalent to the UE master + culture instance."""
    tset, tile = MASTERS[master]
    prm = params(tset, culture)
    name = name or (master if culture == 'Base' else mi_name(master, culture))
    m = bpy.data.materials.get(name)
    if m is not None:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    L = nt.links
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    L.new(bsdf.outputs[0], out.inputs[0])
    uv = nt.nodes.new('ShaderNodeUVMap')
    uv.uv_map = 'UVMap'
    dname = prm.get('BaseColorTexture', 'T_Age4_%s_D' % tset)
    td = nt.nodes.new('ShaderNodeTexImage')
    td.image = _img(dname, False)
    tn = nt.nodes.new('ShaderNodeTexImage')
    tn.image = _img('T_Age4_%s_N' % tset, True)
    to = nt.nodes.new('ShaderNodeTexImage')
    to.image = _img('T_Age4_%s_ORM' % tset, True)
    for t in (td, tn, to):
        t.interpolation = 'Cubic' if tile is None else 'Linear'
        L.new(uv.outputs[0], t.inputs[0])
    va = nt.nodes.new('ShaderNodeVertexColor')
    va.layer_name = 'Col'
    sep_c = nt.nodes.new('ShaderNodeSeparateColor')
    L.new(va.outputs[0], sep_c.inputs[0])
    sep_o = nt.nodes.new('ShaderNodeSeparateColor')
    L.new(to.outputs[0], sep_o.inputs[0])

    # desaturate + tint
    bw = nt.nodes.new('ShaderNodeRGBToBW')
    L.new(td.outputs[0], bw.inputs[0])
    mixd = nt.nodes.new('ShaderNodeMix')
    mixd.data_type = 'RGBA'
    mixd.inputs[0].default_value = prm.get('Desaturate', 0.0)
    L.new(td.outputs[0], mixd.inputs[6])
    L.new(bw.outputs[0], mixd.inputs[7])
    tint = nt.nodes.new('ShaderNodeMix')
    tint.data_type = 'RGBA'
    tint.blend_type = 'MULTIPLY'
    tint.inputs[0].default_value = 1.0
    L.new(mixd.outputs[2], tint.inputs[6])
    tint.inputs[7].default_value = (*prm.get('Tint', (1, 1, 1)), 1.0)
    cur = tint.outputs[2]
    # accent (Col.B) - painted doors etc.
    if 'AccentTint' in prm:
        acc = nt.nodes.new('ShaderNodeMix')
        acc.data_type = 'RGBA'
        acc.blend_type = 'MULTIPLY'
        acc.inputs[0].default_value = 1.0
        L.new(bw.outputs[0], acc.inputs[6])
        a = prm['AccentTint']
        acc.inputs[7].default_value = (a[0] * 2.6, a[1] * 2.6, a[2] * 2.6, 1.0)
        gt = nt.nodes.new('ShaderNodeMath')
        gt.operation = 'GREATER_THAN'
        gt.inputs[1].default_value = 0.5
        L.new(sep_c.outputs[2], gt.inputs[0])
        mx = nt.nodes.new('ShaderNodeMix')
        mx.data_type = 'RGBA'
        L.new(gt.outputs[0], mx.inputs[0])
        L.new(cur, mx.inputs[6])
        L.new(acc.outputs[2], mx.inputs[7])
        cur = mx.outputs[2]
    # soot (Col.G)
    sm = nt.nodes.new('ShaderNodeMath')
    sm.operation = 'MULTIPLY_ADD'
    sm.inputs[1].default_value = -0.8
    sm.inputs[2].default_value = 1.0
    L.new(sep_c.outputs[1], sm.inputs[0])
    so = nt.nodes.new('ShaderNodeMix')
    so.data_type = 'RGBA'
    so.blend_type = 'MULTIPLY'
    so.inputs[0].default_value = 1.0
    L.new(cur, so.inputs[6])
    L.new(sm.outputs[0], so.inputs[7])
    cur = so.outputs[2]
    # macro variation (object-independent world noise) and a mild AO multiply
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nz = nt.nodes.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = 0.04
    nz.inputs['Detail'].default_value = 3.0
    L.new(tc.outputs['Object'], nz.inputs['Vector'])
    mr = nt.nodes.new('ShaderNodeMapRange')
    mr.inputs[3].default_value = 0.84
    mr.inputs[4].default_value = 1.14
    L.new(nz.outputs[0], mr.inputs[0])
    mv = nt.nodes.new('ShaderNodeMix')
    mv.data_type = 'RGBA'
    mv.blend_type = 'MULTIPLY'
    mv.inputs[0].default_value = 1.0
    L.new(cur, mv.inputs[6])
    L.new(mr.outputs[0], mv.inputs[7])
    ao = nt.nodes.new('ShaderNodeMix')
    ao.data_type = 'RGBA'
    ao.blend_type = 'MULTIPLY'
    ao.inputs[0].default_value = 0.6
    L.new(mv.outputs[2], ao.inputs[6])
    L.new(sep_o.outputs[0], ao.inputs[7])
    L.new(ao.outputs[2], bsdf.inputs['Base Color'])
    # roughness
    ra = nt.nodes.new('ShaderNodeMath')
    ra.operation = 'ADD'
    ra.inputs[1].default_value = prm.get('RoughnessAdd', 0.0)
    L.new(sep_o.outputs[1], ra.inputs[0])
    rs = nt.nodes.new('ShaderNodeMath')
    rs.operation = 'MULTIPLY_ADD'
    rs.inputs[1].default_value = 0.3
    L.new(sep_c.outputs[1], rs.inputs[0])
    L.new(ra.outputs[0], rs.inputs[2])
    rs.use_clamp = True
    L.new(rs.outputs[0], bsdf.inputs['Roughness'])
    L.new(sep_o.outputs[2], bsdf.inputs['Metallic'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nm.uv_map = 'UVMap'
    nm.inputs['Strength'].default_value = 1.0
    L.new(tn.outputs[0], nm.inputs['Color'])
    L.new(nm.outputs[0], bsdf.inputs['Normal'])
    if tset == 'Trim' and night > 0:
        tm = nt.nodes.new('ShaderNodeTexImage')
        tm.image = _img('T_Age4_Trim_M', True)
        L.new(uv.outputs[0], tm.inputs[0])
        sm2 = nt.nodes.new('ShaderNodeSeparateColor')
        L.new(tm.outputs[0], sm2.inputs[0])
        lit = nt.nodes.new('ShaderNodeMath')
        lit.operation = 'LESS_THAN'
        lit.inputs[1].default_value = 0.6
        L.new(sep_c.outputs[0], lit.inputs[0])
        e = nt.nodes.new('ShaderNodeMath')
        e.operation = 'MULTIPLY'
        L.new(sm2.outputs[0], e.inputs[0])
        L.new(lit.outputs[0], e.inputs[1])
        bsdf.inputs['Emission Color'].default_value = (1.0, 0.62, 0.3, 1.0)
        e2 = nt.nodes.new('ShaderNodeMath')
        e2.operation = 'MULTIPLY'
        e2.inputs[1].default_value = night
        L.new(e.outputs[0], e2.inputs[0])
        L.new(e2.outputs[0], bsdf.inputs['Emission Strength'])
    return m


def slot_materials(slots, culture='Base', night=0.0):
    """slots: [structure master, roof master, detail master] -> Blender materials."""
    return [make_material(m, culture, night=night) if culture == 'Base' and night == 0 else
            make_material(m, culture, name=(mi_name(m, culture) + ('_night' if night else '')), night=night)
            for m in slots]


def manifest_instances():
    out = {}
    for master, (tset, tile) in MASTERS.items():
        for c in CULTURES:
            prm = dict(CULTURE_PARAMS[c][tset])
            out[mi_name(master, c)] = {'parent': master, 'culture': c, 'params': {k: (list(v) if isinstance(v, tuple) else v) for k, v in prm.items()}}
    for extra, sets in EXTRA.items():
        for tset, prm in sets.items():
            master = 'M_Age4_' + tset
            out[mi_name(master, extra)] = {'parent': master, 'variant': extra, 'params': {k: (list(v) if isinstance(v, tuple) else v) for k, v in prm.items()}}
    return out


def manifest_masters():
    out = {}
    for master, (tset, tile) in MASTERS.items():
        out[master] = {'textures': tex_files(master), 'tile_metres': tile,
                       'uv': 'world-scale planar (1 UV = %s m)' % tile if tile else 'trim-sheet atlas (bands tile in U)'}
    return out
