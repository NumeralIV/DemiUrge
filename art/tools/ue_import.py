# DEMIURGE art pipeline - Unreal import (editor Python, headless):
#   UnrealEditor-Cmd.exe <project.uproject> -run=pythonscript -script="<root>/art/tools/ue_import.py" -unattended -nosplash
#   env DM_ASSETS=shrine,... (default: every art/export/*/<asset>.manifest.json)   DM_ONLY_MATS=1 (materials only)
# Re-runnable: textures are re-imported in place, material graphs are rebuilt; each mesh asset is deleted and imported
# fresh at the same path (a re-import over it kept stale FBX settings), so look meshes up by path (LoadObject), never
# by a hard reference saved in another asset.
#
# Creates, under /Game/Assets:
#   Textures/<Set>/T_<Set>_BC|N|ORM      BC sRGB; N normal map (green flipped: the PNGs are OpenGL +Y); ORM masks
#   Materials/M_DmAsset                  opaque master (textures x vertex Mask x per-instance custom data)
#   Materials/M_DmAssetFoliage           masked two-sided foliage cards (alpha in BC.a)
#   Materials/M_DmAssetGlow              additive unlit (flame cards)
#   Materials/Instances/MI_<Set>[_<Style>]   one per slot name "<set>@<style>" used by any manifest
#   <Asset>/SM_<Asset>_<Config>          static mesh: LOD0 + LOD1 + LOD2 (FBX), UCX convex collision, slots -> MIs
# Logs "DMASSET ..." lines with bounds (cm), triangle counts per LOD, slots and screen sizes.
import json
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
TEXROOT = os.path.join(ROOT, 'textures')
EXPORT = os.path.join(ROOT, 'export')
DST = '/Game/Assets'
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


class _SMES:
    """StaticMeshEditorSubsystem when it exists, else the (deprecated) EditorStaticMeshLibrary - the subsystem is
    not created in every commandlet."""

    _inst = None

    def __getattr__(self, k):
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        if sub is None:
            if _SMES._inst is None:
                _SMES._inst = unreal.new_object(unreal.StaticMeshEditorSubsystem)
            sub = _SMES._inst
        if hasattr(sub, k):
            return getattr(sub, k)
        return getattr(unreal.EditorStaticMeshLibrary, k)


SMES = _SMES()
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
ERR = []

# LOD screen sizes (fraction of the screen height covered by the bounds sphere): for the 9 m shrine at 40 deg FOV
# LOD1 takes over at ~200 m and LOD2 at ~830 m (see docs/lab/asset-pipeline.md, "LOD rules").
SCREEN = [1.0, 0.06, 0.015]


def log(m):
    unreal.log('DMASSET ' + m)


def camel(n):
    return ''.join(p.capitalize() for p in n.split('_'))


# ----------------------------------------------------------------------------------------------- textures


def import_texture(path, dest, kind):
    name = os.path.splitext(os.path.basename(path))[0]
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = dest
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    task.factory = unreal.TextureFactory()   # legacy factory: the Interchange path asserts in a commandlet (Slate)
    AT.import_asset_tasks([task])
    tex = unreal.load_asset(dest + '/' + name)
    if not tex:
        ERR.append('texture import failed ' + path)
        return None
    if kind == 'N':
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb', False)
        tex.set_editor_property('flip_green_channel', True)
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    elif kind == 'ORM':
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property('srgb', False)
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    else:
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property('srgb', True)
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    EAL.save_loaded_asset(tex)
    return tex


def texture_sets(names):
    out = {}
    for base in names:
        d = os.path.join(TEXROOT, base)
        sets = {}
        for kind in ('BC', 'N', 'ORM'):
            p = os.path.join(d, 'T_%s_%s.png' % (camel(base), kind))
            if os.path.exists(p):
                sets[kind] = import_texture(p, '%s/Textures/%s' % (DST, camel(base)), kind)
        out[base] = sets
        log('textures %s: %s' % (base, sorted(sets)))
    return out


# ----------------------------------------------------------------------------------------------- material graphs


class G:
    def __init__(self, m):
        self.m = m
        self.y = 0

    def n(self, cls, **props):
        self.y += 120
        e = MEL.create_material_expression(self.m, cls, -1200, self.y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(self, a, a_out, b, b_in):
        if not MEL.connect_material_expressions(a, a_out, b, b_in):
            ERR.append('link %s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in))

    def out(self, e, pin, prop):
        if not MEL.connect_material_property(e, pin, prop):
            ERR.append('out %s.%s -> %s' % (e.get_name(), pin, prop))

    def scalar(self, name, v):
        return self.n(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(v))

    def vec(self, name, r, g, b, a=0.0):
        return self.n(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(r, g, b, a))

    def tex(self, name, default, stype):
        e = self.n(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name)
        if default:
            e.set_editor_property('texture', default)
        e.set_editor_property('sampler_type', stype)
        return e

    def cdata(self, i, d):
        e = self.n(unreal.MaterialExpressionPerInstanceCustomData, data_index=i)
        try:
            e.set_editor_property('const_default_value', float(d))
        except Exception:
            pass
        return e

    def append3(self, a, b, c):
        ab = self.n(unreal.MaterialExpressionAppendVector)
        self.link(a, '', ab, 'A')
        self.link(b, '', ab, 'B')
        abc = self.n(unreal.MaterialExpressionAppendVector)
        self.link(ab, '', abc, 'A')
        self.link(c, '', abc, 'B')
        return abc

    def custom(self, code, inputs, out_type, outputs=()):
        e = self.n(unreal.MaterialExpressionCustom)
        e.set_editor_property('code', code)
        e.set_editor_property('output_type', out_type)
        ins = []
        for nm, _s, _p in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property('input_name', nm)
            ins.append(ci)
        e.set_editor_property('inputs', ins)
        outs = []
        for nm, t in outputs:
            co = unreal.CustomOutput()
            co.set_editor_property('output_name', nm)
            co.set_editor_property('output_type', t)
            outs.append(co)
        e.set_editor_property('additional_outputs', outs)
        for nm, src, pin in inputs:
            self.link(src, pin, e, nm)
        return e


def new_material(path, name):
    full = path + '/' + name
    m = None
    if EAL.does_asset_exist(full):
        m = unreal.load_asset(full)
        if m:
            MEL.delete_all_material_expressions(m)
    if not m:
        m = AT.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())
    return m


def finish(m):
    MEL.layout_material_expressions(m)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log('material ' + m.get_path_name())


SURF = r'''
// vertex mask: the legacy FBX import stores the FBX value as the byte (FColor -> FLinearColor -> ToFColor(true)
// round-trips), so the shader reads the value written in Blender; DecodeMask = 1 only for sRGB-encoded sources
// VCRGB comes from a real VertexColor node: without a connected one the interpolator is not compiled and
// Parameters.VertexColor reads 0 (no AO -> black shadows, no masks, no fire light)
float4 vm = float4(VCRGB, VC.a);
if (Decode > 0.5) vm.rgb = (vm.rgb <= 0.04045) ? vm.rgb / 12.92 : pow((vm.rgb + 0.055) / 1.055, 2.4);
float ao_v = lerp(1.0, vm.r, AOStr);
float3 base = BC.rgb * Tint;
// culture colour slot (cloth): keep the woven texture's value, take the hue from the owner's culture colour
// (normalised by the culture colour's own luminance, so every culture's cloth has the wool's value: a fixed
// reference made the yellow culture's cloth 1.9x and the vermilion one 0.55x the wool's brightness)
float lum = dot(base, float3(0.2126, 0.7152, 0.0722));
float clum = max(dot(Culture, float3(0.2126, 0.7152, 0.0722)), 0.02);
// ...but no dyed cloth is brighter than CultureMax in any channel, so dark saturated cultures (vermilion) do not
// turn neon at the wool's value
float3 ccol = Culture * (lum * CultureValue / clum);
ccol *= min(1.0, CultureMax / max(max(ccol.r, ccol.g), max(ccol.b, 1e-3)));
base = lerp(base, ccol, CultureAmt);
// per-instance town tint (custom data 0..2 wall, 3..5 roof), applied softly so the texture still reads
float3 it = (TintChannel > 1.5) ? RoofT : ((TintChannel > 0.5) ? WallT : float3(1, 1, 1));
base *= lerp(float3(1, 1, 1), it, TintAmt);
float rough = ORM.g;
float cav = 1.0 - ORM.r;   // texture cavity: growth and stain settle in the joints
// stain (G): soot / char / wetness / crack darkening, by style
float st = saturate(vm.g * (1.0 + cav * StainCavity));
base = lerp(base, lerp(base * StainMul, StainColor, StainColorAmt), st);
rough = lerp(rough, StainRough, st * StainRoughAmt);
// growth (B): moss / lichen / weed / dust, concentrated in cavities and on top faces
float gr = saturate(vm.b * GrowthAmt * (1.0 - GrowthCavity + GrowthCavity * saturate(cav * 4.0 + 0.15)));
float3 gcol = GrowthColor * (0.75 + 0.5 * BC.g / max(dot(BC.rgb, float3(0.333, 0.333, 0.333)), 0.05) * 0.5);
base = lerp(base, gcol, gr);
rough = lerp(rough, GrowthRough, gr);
// settlement state (custom data 7): burnt / ruined towns darken and lose colour
float lumb = dot(base, float3(0.2126, 0.7152, 0.0722));
base = lerp(base, lumb.xxx * float3(0.55, 0.5, 0.46), saturate(State) * 0.6);
AO = ORM.r * ao_v;
Rough = saturate(rough);
// emission: fire light baked in mask A (interior lit by the altar fire) and emissive texture masks (embers)
float lit = saturate(Lit * 2.0);
Emis = base * FireColor * (vm.a * FireStrength * lit * ao_v) + EmisColor * (BC.a * EmisMaskAmt * EmisStrength * lit);
Spec = Specular;
return base * lerp(1.0, ao_v, 0.65);
'''


def build_masters(defaults):
    path = DST + '/Materials'
    out = {}
    # ------------------------------------------------ opaque master
    m = new_material(path, 'M_DmAsset')
    m.set_editor_property('used_with_instanced_static_meshes', True)
    m.set_editor_property('used_with_static_lighting', True)
    g = G(m)
    bc = g.tex('BC', defaults['BC'], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    nm = g.tex('N', defaults['N'], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    orm = g.tex('ORM', defaults['ORM'], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    vc = g.n(unreal.MaterialExpressionVertexColor)
    vca = g.custom('return Parameters.VertexColor;', [], unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    cd = [g.cdata(i, 1.0 if i < 6 else (1.0 if i == 6 else 0.0)) for i in range(8)]
    cul = [g.cdata(8, 0.787), g.cdata(9, 0.349), g.cdata(10, 0.0)]   # #e69f00 Umari (linear) when the towns pass no culture colour
    wall = g.append3(cd[0], cd[1], cd[2])
    roof = g.append3(cd[3], cd[4], cd[5])
    culture = g.append3(*cul)
    p = {
        'Tint': g.vec('Tint', 1, 1, 1), 'TintChannel': g.scalar('TintChannel', 0), 'TintAmt': g.scalar('TintAmt', 0.3),
        'CultureAmt': g.scalar('CultureAmt', 0), 'CultureValue': g.scalar('CultureValue', 1.0), 'CultureMax': g.scalar('CultureMax', 0.7),
        'StainMul': g.vec('StainMul', 0.12, 0.10, 0.09), 'StainColor': g.vec('StainColor', 0.02, 0.018, 0.016), 'StainColorAmt': g.scalar('StainColorAmt', 0),
        'StainRough': g.scalar('StainRough', 0.95), 'StainRoughAmt': g.scalar('StainRoughAmt', 1.0), 'StainCavity': g.scalar('StainCavity', 0.5),
        'GrowthColor': g.vec('GrowthColor', 0.07, 0.10, 0.02), 'GrowthAmt': g.scalar('GrowthAmt', 1.0), 'GrowthRough': g.scalar('GrowthRough', 0.9),
        'GrowthCavity': g.scalar('GrowthCavity', 0.6),
        'AOStr': g.scalar('VertexAO', 1.0), 'Decode': g.scalar('DecodeMask', 0.0),
        'FireColor': g.vec('FireColor', 1.0, 0.42, 0.12), 'FireStrength': g.scalar('FireStrength', 4.0),
        'EmisColor': g.vec('EmisColor', 1.0, 0.32, 0.06), 'EmisMaskAmt': g.scalar('EmisMaskAmt', 0.0), 'EmisStrength': g.scalar('EmisStrength', 8.0),
        'Specular': g.scalar('Specular', 0.35),
    }
    ins = [('BC', bc, ''), ('ORM', orm, ''), ('VC', vca, ''), ('VCRGB', vc, ''), ('WallT', wall, ''), ('RoofT', roof, ''), ('Culture', culture, ''),
           ('Lit', cd[6], ''), ('State', cd[7], '')] + [(k, v, '') for k, v in p.items()]
    # BC alpha (emissive mask) needs the RGBA output
    ins[0] = ('BC', g.append3(bc, bc, bc) if False else bc, '')
    bc4 = g.n(unreal.MaterialExpressionAppendVector)
    g.link(bc, 'RGB', bc4, 'A')
    g.link(bc, 'A', bc4, 'B')
    ins[0] = ('BC', bc4, '')
    s = g.custom(SURF, ins, F3, [('AO', F1), ('Rough', F1), ('Emis', F3), ('Spec', F1)])
    g.out(s, '', MP.MP_BASE_COLOR)
    g.out(s, 'AO', MP.MP_AMBIENT_OCCLUSION)
    g.out(s, 'Rough', MP.MP_ROUGHNESS)
    g.out(s, 'Emis', MP.MP_EMISSIVE_COLOR)
    g.out(s, 'Spec', MP.MP_SPECULAR)
    g.out(nm, 'RGB', MP.MP_NORMAL)
    finish(m)
    out['opaque'] = m
    # ------------------------------------------------ masked foliage cards
    m = new_material(path, 'M_DmAssetFoliage')
    m.set_editor_property('used_with_instanced_static_meshes', True)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('opacity_mask_clip_value', 0.4)
    m.set_editor_property('dithered_lod_transition', True)
    g = G(m)
    bc = g.tex('BC', defaults['BCA'], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    vca = g.custom('return Parameters.VertexColor;', [], unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    vc = g.n(unreal.MaterialExpressionVertexColor)
    tint = g.vec('Tint', 1, 1, 1)
    tr = g.vec('Transmission', 0.6, 0.75, 0.25)
    gr_col = g.vec('GrowthColor', 0.07, 0.10, 0.02)
    st_mul = g.vec('StainMul', 0.4, 0.4, 0.4)
    fol = g.custom(r'''
float4 vm = float4(VCRGB, VC.a);
float3 b = BC.rgb * Tint * lerp(0.45, 1.0, vm.r);
b = lerp(b, b * StainMul, vm.g * 0.5);
SSS = b * Trans;
return b;''', [('BC', bc, 'RGB'), ('VC', vca, ''), ('VCRGB', vc, ''), ('Tint', tint, ''), ('Trans', tr, ''), ('StainMul', st_mul, '')], F3, [('SSS', F3)])
    _ = gr_col
    g.out(fol, '', MP.MP_BASE_COLOR)
    g.out(fol, 'SSS', MP.MP_SUBSURFACE_COLOR)
    g.out(bc, 'A', MP.MP_OPACITY_MASK)
    r = g.scalar('Roughness', 0.8)
    g.out(r, '', MP.MP_ROUGHNESS)
    sp = g.scalar('Specular', 0.1)
    g.out(sp, '', MP.MP_SPECULAR)
    finish(m)
    out['foliage'] = m
    # ------------------------------------------------ additive glow (flames)
    m = new_material(path, 'M_DmAssetGlow')
    m.set_editor_property('used_with_instanced_static_meshes', True)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    g = G(m)
    bc = g.tex('BC', defaults['BC'], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    lit = g.cdata(6, 1.0)
    col = g.vec('Color', 1.0, 0.8, 0.6)
    k = g.scalar('Strength', 12.0)
    tm = g.n(unreal.MaterialExpressionTime)
    pr = g.n(unreal.MaterialExpressionPerInstanceRandom)
    uv = g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    gl = g.custom(r'''
float fl = 0.82 + 0.12 * sin(T * 13.0 + R * 40.0) + 0.06 * sin(T * 29.0 + R * 7.0);
return BC.rgb * Col * K * fl * saturate(Lit * 2.0);''', [('BC', bc, 'RGB'), ('Lit', lit, ''), ('Col', col, ''), ('K', k, ''), ('T', tm, ''), ('R', pr, ''), ('UV', uv, '')], F3)
    g.out(gl, '', MP.MP_EMISSIVE_COLOR)
    finish(m)
    out['glow'] = m
    return out


# ----------------------------------------------------------------------------------------------- material instances

# per texture set: tint channel (0 none, 1 wall, 2 roof), culture slot, emissive mask
SET_PARAMS = {
    'rubble': {'TintChannel': 1, 'TintAmt': 0.25}, 'plaster': {'TintChannel': 1, 'TintAmt': 0.3}, 'reed_panel': {'TintChannel': 1, 'TintAmt': 0.2},
    'dressed_stone': {'TintChannel': 1, 'TintAmt': 0.25},
    'thatch': {'TintChannel': 2, 'TintAmt': 0.2, 'GrowthAmt': 0.6}, 'thatch_sodden': {'TintChannel': 2, 'TintAmt': 0.1},
    'shingle': {'TintChannel': 2, 'TintAmt': 0.2}, 'slate': {'TintChannel': 2, 'TintAmt': 0.2, 'Specular': 0.45},
    'wool': {'CultureAmt': 1.0, 'CultureValue': 1.0},
    'ember': {'EmisMaskAmt': 1.0, 'EmisStrength': 10.0, 'VertexAO': 0.3},
    'oak_charred': {'EmisMaskAmt': 1.0, 'EmisStrength': 0.25, 'EmisColor': (1.0, 0.22, 0.04)},
    'bone': {'Specular': 0.4},
}
# weathering styles (slot suffix "@style"): what the stain (G) and growth (B) channels mean
STYLE_PARAMS = {
    '': {},
    'wet': {'StainMul': (0.42, 0.42, 0.40), 'StainRough': 0.22, 'StainCavity': 0.0, 'GrowthColor': (0.045, 0.07, 0.018), 'GrowthRough': 0.35, 'GrowthCavity': 0.3},
    'dust': {'StainMul': (0.22, 0.2, 0.18), 'StainRough': 0.95, 'GrowthColor': (0.48, 0.41, 0.30), 'GrowthAmt': 0.45, 'GrowthCavity': 0.0, 'GrowthRough': 0.95},
    'ruin': {'StainMul': (0.6, 0.6, 0.58), 'GrowthColor': (0.085, 0.11, 0.03), 'GrowthCavity': 0.45, 'GrowthAmt': 1.0},
    'ash': {'EmisMaskAmt': 0.0, 'Tint': (0.9, 0.9, 0.9)},
    'antler': {'Tint': (0.70, 0.58, 0.45)},
}


def mi_name(slot):
    base, _, style = slot.partition('@')
    return 'MI_' + camel(base) + ('_' + camel(style) if style else '')


def make_instance(slot, masters, texsets):
    base, _, style = slot.partition('@')
    parent = masters['glow'] if base == 'flame' else (masters['foliage'] if base in ('foliage', 'fringe') else masters['opaque'])
    path = DST + '/Materials/Instances'
    name = mi_name(slot)
    full = path + '/' + name
    mi = unreal.load_asset(full) if EAL.does_asset_exist(full) else AT.create_asset(name, path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    MEL.clear_all_material_instance_parameters(mi)
    ts = texsets.get(base, {})
    for k in ('BC', 'N', 'ORM'):
        if ts.get(k):
            MEL.set_material_instance_texture_parameter_value(mi, k, ts[k])
    params = dict(SET_PARAMS.get(base, {}))
    params.update(STYLE_PARAMS.get(style, {}))
    if base == 'thatch' and style == '':
        params.setdefault('GrowthColor', (0.06, 0.07, 0.025))
    if base == 'flame':
        params = {}
    if base == 'fringe':
        params = {'Transmission': (0.55, 0.42, 0.2)}
    for k, v in params.items():
        if isinstance(v, tuple):
            MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
        else:
            MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


# ----------------------------------------------------------------------------------------------- meshes


def fbx_task(path, dest, name, collision):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = dest
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = False
    ui.import_materials = False
    ui.import_textures = False
    ui.import_animations = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    d = ui.static_mesh_import_data
    d.combine_meshes = True
    d.auto_generate_collision = False
    d.one_convex_hull_per_ucx = True
    d.generate_lightmap_u_vs = False
    d.remove_degenerates = True
    d.build_nanite = False
    d.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
    d.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    d.convert_scene = True
    d.convert_scene_unit = True     # the FBX declares metres (Blender FBX_SCALE_UNITS): convert to cm
    d.import_uniform_scale = 1.0
    d.reorder_material_to_fbx_order = True
    task.options = ui
    f = unreal.FbxFactory()
    task.factory = f
    return task


def import_mesh(name, entry, mis, asset):
    dest = '%s/%s' % (DST, camel(asset))
    lods = [os.path.join(ROOT, p) for p in entry['lods']]
    if EAL.does_asset_exist(dest + '/' + name):
        # a re-import over an existing mesh keeps that mesh's old FBX import settings: start clean
        EAL.delete_asset(dest + '/' + name)
    AT.import_asset_tasks([fbx_task(lods[0], dest, name, True)])
    sm = unreal.load_asset(dest + '/' + name)
    if not sm:
        ERR.append('mesh import failed ' + name)
        return None
    for i, p in enumerate(lods[1:], 1):
        r = SMES.import_lod(sm, i, p)
        if r != i:
            ERR.append('import_lod %s %d -> %s' % (name, i, r))
    ok = SMES.set_lod_screen_sizes(sm, SCREEN[:SMES.get_lod_count(sm)])
    if not ok:
        ERR.append('%s: set_lod_screen_sizes failed' % name)
    # slots -> material instances (by slot name; LOD sections reference the same slots)
    mats = sm.get_editor_property('static_materials')
    names = []
    for i, sl in enumerate(mats):
        slot = str(sl.get_editor_property('material_slot_name'))
        names.append(slot)
        key = slot
        mi = mis.get(key)
        if mi is None:
            # FBX material names may have been sanitised ('@' -> '_')
            for k, v in mis.items():
                if k.replace('@', '_').lower() == key.lower():
                    mi = v
            if mi is None:
                ERR.append('%s: no material for slot %s' % (name, slot))
                continue
        sm.set_material(i, mi)
    ns = sm.get_editor_property('nanite_settings')
    ns.enabled = False
    sm.set_editor_property('nanite_settings', ns)
    body = sm.get_editor_property('body_setup')
    if body:
        body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    EAL.save_loaded_asset(sm)
    b = sm.get_bounds()
    ext = b.box_extent
    org = b.origin
    tris = []
    for l in range(SMES.get_lod_count(sm)):
        t = 0
        k = 0
        while True:
            try:
                sec = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(sm, l, k)
            except Exception:
                break
            if not sec or not sec[1]:
                break
            t += len(sec[1]) // 3
            k += 1
            if k > 64:
                break
        tris.append(t)
    log('auto screen size: %s' % sm.is_lod_screen_size_auto_computed())
    log('mesh %s lods %d tris %s screen %s hulls %d slots %s origin (%.0f %.0f %.0f) extent (%.0f %.0f %.0f) cm' % (
        name, SMES.get_lod_count(sm), tris, [round(x, 4) for x in SMES.get_lod_screen_sizes(sm)], SMES.get_convex_collision_count(sm), names,
        org.x, org.y, org.z, ext.x, ext.y, ext.z))
    return sm


def main():
    # legacy FBX path: FbxImportUI options (UCX, vertex colours, normals) are honoured only with Interchange FBX off
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
    want = [a for a in os.environ.get('DM_ASSETS', '').split(',') if a]
    manifests = {}
    for asset in sorted(os.listdir(EXPORT)) if os.path.isdir(EXPORT) else []:
        p = os.path.join(EXPORT, asset, asset + '.manifest.json')
        if os.path.exists(p) and (not want or asset in want):
            manifests[asset] = json.load(open(p))
    slots = set()
    for man in manifests.values():
        for e in man['meshes'].values():
            slots.update(e.get('all_slots') or e['slots'])
    # LOD1/LOD2 may use slots LOD0 does not; collect from the texture manifest too
    bases = sorted(set(s.split('@')[0] for s in slots))
    texsets = texture_sets(bases)
    d = {'BC': texsets['limestone']['BC'], 'N': texsets['limestone']['N'], 'ORM': texsets['limestone']['ORM'],
         'BCA': texsets.get('foliage', {}).get('BC') or texsets['limestone']['BC']}
    masters = build_masters(d)
    mis = {s: make_instance(s, masters, texsets) for s in sorted(slots)}
    log('instances %d: %s' % (len(mis), sorted(mi_name(s) for s in slots)))
    if os.environ.get('DM_ONLY_MATS'):
        return
    for asset, man in manifests.items():
        for name, e in man['meshes'].items():
            import_mesh(name, e, mis, asset)
    if ERR:
        for e in ERR:
            unreal.log_error('DMASSET ERROR ' + e)
    else:
        log('import finished without errors')


main()
