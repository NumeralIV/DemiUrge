# DEMIURGE art/culture - material instances (definitions shared by the build, the manifest and the previews).
#
# Unreal: every MI_Culture_* is an instance of one family master M_Culture_Standard (parameters below), except
# MI_Culture_Water_A which parents the shared water material. In Blender the same definitions become
# Principled BSDF materials so the previews show the exported textures.
import os
import bpy

TEX_DIR_REL = 'art/culture/textures'

# name -> texture set, linear tint multiplier (RGB), roughness scale, notes
MI = {
    'MI_Culture_Plaster_A': ('PlasterA', (1.0, 1.0, 1.0), 1.0, 'rose-ochre lime plaster walls, roof slabs, parapets'),
    'MI_Culture_Dome_A': ('DomeA', (1.0, 1.0, 1.0), 1.0, 'lime-washed domes'),
    'MI_Culture_Rubble_A': ('RubbleA', (1.0, 1.0, 1.0), 1.0, 'red sandstone rubble: terraces, cisterns'),
    'MI_Culture_Rock_A': ('Rock', (1.32, 0.80, 0.62), 1.0, 'red canyon rock: the temple niche'),
    'MI_Culture_Water_A': (None, (0.10, 0.36, 0.33), 0.06, 'cistern water (parent: shared water material)'),
    'MI_Culture_Trim_A': ('Trim', (1.0, 1.0, 1.0), 1.0, 'trim sheet, accent (M.G) = cobalt'),
    'MI_Culture_Timber_B': ('TimberB', (1.0, 1.0, 1.0), 1.0, 'tarred piles, decks, boardwalks'),
    'MI_Culture_Shingle_B': ('ShingleB', (1.0, 1.0, 1.0), 1.0, 'silver shingle with moss'),
    'MI_Culture_Garden_B': ('GardenB', (1.0, 1.0, 1.0), 1.0, 'floating garden beds'),
    'MI_Culture_Trim_B': ('Trim', (1.0, 1.0, 1.0), 1.0, 'trim sheet, accent (M.G) = saffron'),
    'MI_Culture_Stone_C': ('StoneC', (1.0, 1.0, 1.0), 1.0, 'pale dressed limestone drums and spire'),
    'MI_Culture_Slate_C': ('SlateC', (1.0, 1.0, 1.0), 1.0, 'dark blue-grey slate cones'),
    'MI_Culture_SpireStone_C': ('StoneC', (0.72, 0.73, 0.76), 1.05, 'older, weathered grey limestone: the spire'),
    'MI_Culture_DryStone_C': ('DryStoneC', (1.0, 1.0, 1.0), 1.0, 'dry-stone ring walls'),
    'MI_Culture_Rock_C': ('Rock', (0.92, 0.95, 1.0), 1.0, 'grey moor stone: standing stones'),
    'MI_Culture_Trim_C': ('Trim', (1.0, 1.0, 1.0), 1.0, 'trim sheet, accent (M.G) = pale stone'),
}

# accent tint written to the trim's M.G mask (cobalt / saffron bands) per culture
ACCENT = {'A': (0.03, 0.08, 0.40), 'B': (0.58, 0.28, 0.03), 'C': (0.62, 0.58, 0.50)}


def tex_files(set_name, root):
    d = os.path.join(root, TEX_DIR_REL)
    out = {k: os.path.join(d, 'T_Culture_%s_%s.png' % (set_name, k)) for k in ('D', 'N', 'ORM')}
    m = os.path.join(d, 'T_Culture_%s_M.png' % set_name)
    if os.path.exists(m):
        out['M'] = m
    return out


def _img(path, color):
    name = os.path.basename(path)
    im = bpy.data.images.get(name)
    if im is None:
        im = bpy.data.images.load(path, check_existing=True)
    im.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
    return im


def make(name, root, night=0.0):
    """Blender preview material for an MI (idempotent)."""
    m = bpy.data.materials.get(name)
    if m:
        return m
    set_name, tint, rough, _ = MI[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bs = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bs.outputs[0], out.inputs[0])
    if set_name is None:   # water
        bs.inputs['Base Color'].default_value = (*tint, 1.0)
        bs.inputs['Roughness'].default_value = rough
        bs.inputs['IOR'].default_value = 1.33
        return m
    f = tex_files(set_name, root)
    uv = nt.nodes.new('ShaderNodeUVMap')
    uv.uv_map = 'UVMap'
    td = nt.nodes.new('ShaderNodeTexImage')
    td.image = _img(f['D'], True)
    tn = nt.nodes.new('ShaderNodeTexImage')
    tn.image = _img(f['N'], False)
    to = nt.nodes.new('ShaderNodeTexImage')
    to.image = _img(f['ORM'], False)
    for t in (td, tn, to):
        nt.links.new(uv.outputs[0], t.inputs[0])
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(to.outputs[0], sep.inputs[0])
    # base = D * tint * lerp(0.55, 1, AO)
    mul = nt.nodes.new('ShaderNodeMix')
    mul.data_type = 'RGBA'
    mul.blend_type = 'MULTIPLY'
    mul.inputs['Factor'].default_value = 1.0
    nt.links.new(td.outputs[0], mul.inputs['A'])
    mul.inputs['B'].default_value = (*tint, 1.0)
    ao = nt.nodes.new('ShaderNodeMapRange')
    ao.inputs['To Min'].default_value = 0.55
    nt.links.new(sep.outputs[0], ao.inputs['Value'])
    mul2 = nt.nodes.new('ShaderNodeMix')
    mul2.data_type = 'RGBA'
    mul2.blend_type = 'MULTIPLY'
    mul2.inputs['Factor'].default_value = 1.0
    nt.links.new(mul.outputs['Result'], mul2.inputs['A'])
    nt.links.new(ao.outputs[0], mul2.inputs['B'])
    nt.links.new(mul2.outputs['Result'], bs.inputs['Base Color'])
    rm = nt.nodes.new('ShaderNodeMath')
    rm.operation = 'MULTIPLY'
    rm.inputs[1].default_value = rough
    nt.links.new(sep.outputs[1], rm.inputs[0])
    nt.links.new(rm.outputs[0], bs.inputs['Roughness'])
    nt.links.new(sep.outputs[2], bs.inputs['Metallic'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nt.links.new(tn.outputs[0], nm.inputs['Color'])
    nt.links.new(nm.outputs[0], bs.inputs['Normal'])
    if 'M' in f and night > 0:
        tm = nt.nodes.new('ShaderNodeTexImage')
        tm.image = _img(f['M'], False)
        nt.links.new(uv.outputs[0], tm.inputs[0])
        sm = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(tm.outputs[0], sm.inputs[0])
        bs.inputs['Emission Color'].default_value = (1.0, 0.55, 0.22, 1.0)
        em = nt.nodes.new('ShaderNodeMath')
        em.operation = 'MULTIPLY'
        em.inputs[1].default_value = night
        nt.links.new(sm.outputs[0], em.inputs[0])
        nt.links.new(em.outputs[0], bs.inputs['Emission Strength'])
    return m


def mats_for(slots, root):
    """slots: {'Structure': MI, ...} -> list of 3 Blender materials in slot order (missing -> a grey dummy)."""
    out = []
    for s in ('Structure', 'Roof', 'Detail'):
        if s in slots:
            out.append(make(slots[s], root))
        else:
            d = bpy.data.materials.get('_unused') or bpy.data.materials.new('_unused')
            out.append(d)
    return out


# Host-kit wall overrides: the Age2 kit's wall materials tile in metres like ours, so a culture can swap the
# wall texture set (parent = the host master, texture parameters swapped). Age3 uses banded atlases, so it
# keeps its own CultureA/B/C parameters instead.
HOST_OVERRIDE = {
    'A': ('PlasterA', 'rose-ochre lime plaster: the whole cube reads as plaster, as in culture-a.png'),
    'B': ('TimberB', 'tarred plank walls under the shingle roofs'),
    'C': ('StoneC', 'pale dressed limestone'),
}
HOST_WALL_MATS = {'Age2': ('M_Age2_Rubble', 'M_Age2_Ashlar', 'M_Age2_Limestone')}
