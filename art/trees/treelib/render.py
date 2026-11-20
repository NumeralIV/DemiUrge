# DEMIURGE tree families: headless EEVEE helpers for baking leaf tiles and impostor views.
#
# Every bake is an orthographic EMISSION render (unlit albedo) with a transparent film and the "Standard" view
# transform, so the PNG holds sRGB albedo exactly and the alpha channel is the coverage mask.
import os
import bpy
import numpy as np
from . import imaging as im


def clear_scene():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)


def setup(res_x, res_y, samples=32):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    try:
        sc.eevee.taa_render_samples = samples
    except Exception:
        pass
    sc.render.resolution_x = res_x
    sc.render.resolution_y = res_y
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = True
    sc.render.filter_size = 0.75
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.view_settings.exposure = 0.0
    sc.view_settings.gamma = 1.0
    sc.display_settings.display_device = 'sRGB'
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.render.image_settings.color_depth = '8'
    if sc.world is None:
        sc.world = bpy.data.worlds.new('W')
    sc.world.color = (0, 0, 0)
    return sc


def ortho_camera(loc, rot, scale):
    sc = bpy.context.scene
    cam = bpy.data.objects.get('BakeCam')
    if cam is None:
        cd = bpy.data.cameras.new('BakeCam')
        cam = bpy.data.objects.new('BakeCam', cd)
        sc.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = scale
    cam.data.clip_start = 0.01
    cam.data.clip_end = 1000.0
    cam.location = loc
    cam.rotation_euler = rot
    sc.camera = cam
    return cam


def render(path):
    sc = bpy.context.scene
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return im.load_png(path)


def _nodes(mat):
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    return nt


def mat_vertex_emission(name='EmitCol'):
    """Unlit material: emission = linear FLOAT colour attribute 'Col'."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    nt = _nodes(m)
    a = nt.nodes.new('ShaderNodeAttribute')
    a.attribute_name = 'Col'
    a.attribute_type = 'GEOMETRY'
    e = nt.nodes.new('ShaderNodeEmission')
    o = nt.nodes.new('ShaderNodeOutputMaterial')
    nt.links.new(a.outputs['Color'], e.inputs['Color'])
    nt.links.new(e.outputs[0], o.inputs['Surface'])
    return m


def mat_bake_normal(name, leaf_image, kind):
    """Impostor normal bake (verification addition): emission = corner attribute 'BakeN' (the mesh's own shading
    normals, i.e. the leaf cards' volumetric normals, already expressed in the bake view's frame as 0.5 + 0.5 n);
    leaves use the same 0.4 alpha clip as the albedo bake so both atlases share one silhouette."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    nt = _nodes(m)
    a = nt.nodes.new('ShaderNodeAttribute')
    a.attribute_name = 'BakeN'
    a.attribute_type = 'GEOMETRY'
    e = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(a.outputs['Color'], e.inputs['Color'])
    o = nt.nodes.new('ShaderNodeOutputMaterial')
    if kind == 'leaf' and leaf_image is not None:
        uv = nt.nodes.new('ShaderNodeUVMap')
        uv.uv_map = 'UV0'
        tx = nt.nodes.new('ShaderNodeTexImage')
        tx.image = leaf_image
        tx.interpolation = 'Linear'
        nt.links.new(uv.outputs[0], tx.inputs[0])
        gt = nt.nodes.new('ShaderNodeMath')
        gt.operation = 'GREATER_THAN'
        nt.links.new(tx.outputs['Alpha'], gt.inputs[0])
        gt.inputs[1].default_value = 0.4
        tr = nt.nodes.new('ShaderNodeBsdfTransparent')
        mix = nt.nodes.new('ShaderNodeMixShader')
        nt.links.new(gt.outputs[0], mix.inputs[0])
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(e.outputs[0], mix.inputs[2])
        nt.links.new(mix.outputs[0], o.inputs['Surface'])
        try:
            m.surface_render_method = 'DITHERED'
        except Exception:
            pass
    else:
        nt.links.new(e.outputs[0], o.inputs['Surface'])
    m.use_backface_culling = False
    return m


def mat_bake_tree(name, image, kind):
    """Bake material for impostors, mirroring the Unreal shading of the albedo:
       bark:   tex.rgb * Col.rgb * lerp(0.55, 1, Col.a)            (opaque)
       leaves: tex.rgb * Col.rgb * lerp(0.28, 1, Col.a), clip at tex.a > 0.4
    Col is the BYTE colour attribute (Blender returns it linearised, i.e. pow(srgb, 2.2) like the Unreal material)."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    nt = _nodes(m)
    uv = nt.nodes.new('ShaderNodeUVMap')
    uv.uv_map = 'UV0'
    tx = nt.nodes.new('ShaderNodeTexImage')
    tx.image = image
    tx.interpolation = 'Linear'
    nt.links.new(uv.outputs[0], tx.inputs[0])
    ca = nt.nodes.new('ShaderNodeVertexColor')
    ca.layer_name = 'Col'
    mul = nt.nodes.new('ShaderNodeVectorMath')
    mul.operation = 'MULTIPLY'
    nt.links.new(tx.outputs['Color'], mul.inputs[0])
    nt.links.new(ca.outputs['Color'], mul.inputs[1])
    col_out = mul.outputs[0]
    # ao factor: leaves 0.28 + 0.72 * a, bark 0.55 + 0.45 * a (both as in the Unreal materials). Verification fix:
    # the bark bake ignored the AO, so impostor trunks/limbs were up to 2x brighter than LOD0-2 (pale sticks at LOD3).
    mr = nt.nodes.new('ShaderNodeMath')
    mr.operation = 'MULTIPLY_ADD'
    nt.links.new(ca.outputs['Alpha'], mr.inputs[0])
    mr.inputs[1].default_value = 0.72 if kind == 'leaf' else 0.45
    mr.inputs[2].default_value = 0.28 if kind == 'leaf' else 0.55
    mul2 = nt.nodes.new('ShaderNodeVectorMath')
    mul2.operation = 'SCALE'
    nt.links.new(col_out, mul2.inputs[0])
    nt.links.new(mr.outputs[0], mul2.inputs['Scale'])
    col_out = mul2.outputs[0]
    e = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(col_out, e.inputs['Color'])
    o = nt.nodes.new('ShaderNodeOutputMaterial')
    if kind == 'leaf':
        gt = nt.nodes.new('ShaderNodeMath')
        gt.operation = 'GREATER_THAN'
        nt.links.new(tx.outputs['Alpha'], gt.inputs[0])
        gt.inputs[1].default_value = 0.4
        tr = nt.nodes.new('ShaderNodeBsdfTransparent')
        mix = nt.nodes.new('ShaderNodeMixShader')
        nt.links.new(gt.outputs[0], mix.inputs[0])
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(e.outputs[0], mix.inputs[2])
        nt.links.new(mix.outputs[0], o.inputs['Surface'])
        try:
            m.surface_render_method = 'DITHERED'
        except Exception:
            pass
        m.use_backface_culling = False
    else:
        nt.links.new(e.outputs[0], o.inputs['Surface'])
    return m


def mat_placeholder(name):
    """Export-only material (Unreal slot name); the Unreal import script binds the real material instances."""
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)
