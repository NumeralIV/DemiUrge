# DEMIURGE art - wonders-a: Blender preview materials (the UE materials are described in manifest.json).
import os

import bpy


def _img(path, colorspace):
    name = os.path.basename(path)
    img = bpy.data.images.get(name)
    if img is None:
        img = bpy.data.images.load(path, check_existing=True)
    img.colorspace_settings.name = colorspace
    return img


def hex_rgb(h):
    h = h.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


def make_material(set_name, tex_dir, emissive_tint=None, emit_strength=0.0, culture_tint=None, rel=True):
    """M_Wonder_<set_name> with D, N, ORM (and M when present) wired to a Principled BSDF."""
    name = 'M_Wonder_%s' % set_name
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    base = os.path.join(tex_dir, 'T_Wonder_%s_' % set_name)
    uvn = nt.nodes.new('ShaderNodeUVMap')
    uvn.uv_map = 'UVMap'
    d = nt.nodes.new('ShaderNodeTexImage')
    d.image = _img(base + 'D.png', 'sRGB')
    n = nt.nodes.new('ShaderNodeTexImage')
    n.image = _img(base + 'N.png', 'Non-Color')
    orm = nt.nodes.new('ShaderNodeTexImage')
    orm.image = _img(base + 'ORM.png', 'Non-Color')
    for t in (d, n, orm):
        nt.links.new(uvn.outputs['UV'], t.inputs['Vector'])
    col_out = d.outputs['Color']
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(orm.outputs['Color'], sep.inputs['Color'])
    has_m = os.path.exists(base + 'M.png')
    msep = None
    if has_m:
        mt = nt.nodes.new('ShaderNodeTexImage')
        mt.image = _img(base + 'M.png', 'Non-Color')
        nt.links.new(uvn.outputs['UV'], mt.inputs['Vector'])
        msep = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(mt.outputs['Color'], msep.inputs['Color'])
    if culture_tint is not None and msep is not None:
        mix = nt.nodes.new('ShaderNodeMix')
        mix.data_type = 'RGBA'
        mix.blend_type = 'MULTIPLY'
        nt.links.new(msep.outputs['Green'], mix.inputs['Factor'])
        nt.links.new(col_out, mix.inputs[6])
        mix.inputs[7].default_value = list(culture_tint) + [1.0]
        col_out = mix.outputs[2]
    nt.links.new(col_out, bsdf.inputs['Base Color'])
    nt.links.new(sep.outputs['Green'], bsdf.inputs['Roughness'])
    nt.links.new(sep.outputs['Blue'], bsdf.inputs['Metallic'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nt.links.new(n.outputs['Color'], nm.inputs['Color'])
    nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    if msep is not None:
        # emissive = D * M.R * tint * strength (strength driven per shot through the node label 'EMIT')
        mul = nt.nodes.new('ShaderNodeMix')
        mul.data_type = 'RGBA'
        mul.blend_type = 'MULTIPLY'
        mul.inputs['Factor'].default_value = 1.0
        nt.links.new(d.outputs['Color'], mul.inputs[6])
        tl = hex_rgb(emissive_tint) if emissive_tint else [1, 1, 1]
        mul.inputs[7].default_value = [1 + (c - 1) * 0.4 for c in tl] + [1.0]   # lerp(1, tint, 0.4)
        mul2 = nt.nodes.new('ShaderNodeMath')
        mul2.operation = 'MULTIPLY'
        mul2.label = 'EMIT'
        mul2.inputs[1].default_value = emit_strength
        nt.links.new(msep.outputs['Red'], mul2.inputs[0])
        nt.links.new(mul.outputs[2], bsdf.inputs['Emission Color'])
        nt.links.new(mul2.outputs['Value'], bsdf.inputs['Emission Strength'])
    return m


def set_emit(strength):
    for m in bpy.data.materials:
        if not m.use_nodes:
            continue
        for nd in m.node_tree.nodes:
            if nd.label == 'EMIT':
                nd.inputs[1].default_value = strength
