# DEMIURGE TreeLab: imports the Blender tree families (art/trees) and builds their materials. Re-runnable.
#   powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/TreeLab/import.ps1
# (UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this> -unattended -nosplash -dpcvars=Interchange.FeatureFlags.Import.FBX=0)
#
# Writes under /Game/Trees:
#   Textures/T_TreeLeafAtlas, T_TreeImpostors (sRGB + alpha, alpha-coverage-preserving mips at 0.4),
#            T_Bark_<Species> (sRGB), T_Bark_<Species>_N (normal map, DirectX green)
#   Materials/M_TreeLeaf (masked TwoSidedFoliage, wind), M_TreeBark (opaque, wind)
#            MI_TreeLeaf, MI_TreeImpostor, MI_TreeBark_<Species>
#   <Species>/SM_Tree_<Species>_<v> (LOD0..2 geometry + LOD3 impostor, slots Bark/Leaves/Impostor)
import json
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = HERE                     # the repository root is the first parent that holds art/trees (works from any project)
while not os.path.isdir(os.path.join(ROOT, 'art', 'trees')) and os.path.dirname(ROOT) != ROOT:
    ROOT = os.path.dirname(ROOT)
ART = os.environ.get('TREES_ART', os.path.join(ROOT, 'art', 'trees'))
MAN = json.load(open(os.path.join(ART, 'export', 'manifest.json')))
SPECIES = ['broadleaf', 'conifer', 'palm', 'acacia', 'shrub']
ONLY = os.environ.get('TREES_ONLY', '')   # e.g. "broadleaf,palm" to re-import a subset of meshes

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
SES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
if SES is None:   # commandlet: editor subsystems are not initialised; the CDO's functions are stateless
    SES = unreal.get_default_object(unreal.StaticMeshEditorSubsystem)
MP = unreal.MaterialProperty
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
ERR = []


def log(m):
    unreal.log('TREEIMPORT ' + m)


try:
    for _cv in ('Interchange.FeatureFlags.Import.FBX 0', 'Interchange.FeatureFlags.Import.PNG 0', 'Interchange.FeatureFlags.Import.SyncToBrowser 0'):
        unreal.SystemLibrary.execute_console_command(None, _cv)
except Exception as e:
    log('could not set Interchange cvar from python: %s' % e)


def import_file(path, dest, name=None, options=None):
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', path)
    t.set_editor_property('destination_path', dest)
    if name:
        t.set_editor_property('destination_name', name)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', False)
    if options is not None:
        t.set_editor_property('options', options)
    AT.import_asset_tasks([t])
    objs = t.get_editor_property('imported_object_paths')
    if not objs:
        ERR.append('import failed: ' + path)
        return None
    return unreal.load_asset(objs[0])


# ---------------------------------------------------------------- textures
def tex(name, kind):
    t = import_file(os.path.join(ART, 'textures', name + '.png'), '/Game/Trees/Textures', name)
    if not t:
        return None
    if kind == 'foliage':
        t.set_editor_property('srgb', True)
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
        t.set_editor_property('do_scale_mips_for_alpha_coverage', True)
        t.set_editor_property('alpha_coverage_thresholds', unreal.Vector4(0.0, 0.0, 0.0, 0.4))
    elif kind == 'data':
        # linear data (the impostor normal atlas: view-frame normal as 0.5 + 0.5 n)
        t.set_editor_property('srgb', False)
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
    elif kind == 'normal':
        t.set_editor_property('srgb', False)
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
    else:
        t.set_editor_property('srgb', True)
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
    # never stream: in -game runs (and the first seconds of play) streamed atlases sit at low mips, which turns
    # leaves into smeared strokes and thins palm fronds into spikes
    t.set_editor_property('never_stream', True)
    EAL.save_loaded_asset(t)
    log('texture %s %dx%d' % (name, t.blueprint_get_size_x(), t.blueprint_get_size_y()))
    return t


T_LEAF = tex('T_TreeLeafAtlas', 'foliage')
T_IMP = tex('T_TreeImpostors', 'foliage')
T_IMPN = tex('T_TreeImpostorsN', 'data')   # verification addition: baked volumetric normals of the impostor views
T_BARK = {sp: (tex('T_Bark_%s' % sp.capitalize(), 'color'), tex('T_Bark_%s_N' % sp.capitalize(), 'normal')) for sp in SPECIES}


# ---------------------------------------------------------------- materials
def new_asset(name, path, cls, factory):
    full = path + '/' + name
    if EAL.does_asset_exist(full):
        a = unreal.load_asset(full)
        if a:
            return a
    return AT.create_asset(name, path, cls, factory)


class G:
    def __init__(self, m):
        self.m, self.x, self.y = m, -1800, -400

    def n(self, cls, **props):
        self.y += 120
        if self.y > 1500:
            self.y, self.x = -400, self.x + 320
        e = MEL.create_material_expression(self.m, cls, self.x, self.y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(self, a, ao, b, bi):
        if not MEL.connect_material_expressions(a, ao, b, bi):
            ERR.append('%s link %s.%s->%s' % (self.m.get_name(), a.get_name(), ao, bi))

    def out(self, e, pin, prop):
        if not MEL.connect_material_property(e, pin, prop):
            ERR.append('%s out %s' % (self.m.get_name(), prop))

    def scalar(self, name, v):
        return self.n(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(v))

    def vparam(self, name, r, g, b):
        return self.n(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(r, g, b, 0))

    def custom(self, code, inputs, otype, outputs=()):
        c = self.n(unreal.MaterialExpressionCustom)
        c.set_editor_property('code', code)
        c.set_editor_property('output_type', otype)
        ins = []
        for nm, _, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property('input_name', nm)
            ins.append(ci)
        c.set_editor_property('inputs', ins)
        outs = []
        for nm, t in outputs:
            co = unreal.CustomOutput()
            co.set_editor_property('output_name', nm)
            co.set_editor_property('output_type', t)
            outs.append(co)
        c.set_editor_property('additional_outputs', outs)
        for nm, src, pin in inputs:
            self.link(src, pin, c, nm)
        return c

    def tex(self, name, t, uvs, sampler):
        e = self.n(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name, texture=t, sampler_type=sampler)
        self.link(uvs, '', e, 'UVs')
        return e


WIND = r'''
struct FW {
float3 Up(float3 wp, float3 ctr) { return normalize(wp - ctr); }
float S(float t, float ph) { return (0.6 * sin(1.1 * t + ph) + 0.25 * sin(2.7 * t + 1.7 * ph) + 0.35) * (1.0 + 0.4 * sin(0.31 * t + 0.5 * ph)); }
float3 Wpo(float3 wp, float3 ctr, float3 wdir, float t, float pir, float2 uv0, float2 uv1, float2 uv2, float3 nw,
           float strength, float branchK, float flutterK, float speed)
{
    float3 up = Up(wp, ctr);
    float3 wd = wdir - up * dot(wdir, up); wd = dot(wd, wd) > 1e-6 ? normalize(wd) : float3(1, 0, 0);
    float tt = t * speed, ph = pir * 6.283;
    float s = S(tt, ph);
    float3 sway = wd * s * uv1.x * strength;
    float bp = uv2.y * 6.283 + ph;
    float b = (0.55 * sin(2.3 * tt + bp) + 0.3 * sin(3.7 * tt + 1.9 * bp)) * (0.55 + 0.45 * s);
    float3 branch = (wd * 0.5 + up * 0.85) * b * uv2.x * branchK;
    float fl = sin(8.0 * tt + 40.0 * (uv0.x + uv0.y) + 3.0 * ph + 6.283 * uv2.y) * uv1.y * flutterK;
    return sway + branch + nw * fl;
}
};
FW W;
'''


def wpo(g, flutter=True):
    return g.custom(WIND + 'return W.Wpo(WP, Ctr, WD, T, PIR, UV0, UV1, UV2, NW, Strength, BranchK, FlutterK, Speed);',
                    [('WP', g.n(unreal.MaterialExpressionWorldPosition), ''), ('Ctr', g.vparam('PlanetCenter', 0, 0, 0), ''),
                     ('WD', g.vparam('WindDir', 1.0, 0.4, 0.0), ''), ('T', g.n(unreal.MaterialExpressionTime), ''),
                     ('PIR', g.n(unreal.MaterialExpressionPerInstanceRandom), ''),
                     ('UV0', g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0), ''),
                     ('UV1', g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1), ''),
                     ('UV2', g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=2), ''),
                     ('NW', g.n(unreal.MaterialExpressionVertexNormalWS), ''),
                     ('Strength', g.scalar('WindStrength', 100.0), ''), ('BranchK', g.scalar('BranchStrength', 100.0), ''),
                     ('FlutterK', g.scalar('FlutterStrength', 100.0 if flutter else 0.0), ''), ('Speed', g.scalar('WindSpeed', 1.0), '')], F3)


def master_leaf():
    m = new_asset('M_TreeLeaf', '/Game/Trees/Materials', unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(m)
    m.set_editor_property('used_with_instanced_static_meshes', True)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property('opacity_mask_clip_value', 0.4)
    m.set_editor_property('dithered_lod_transition', True)
    m.set_editor_property('two_sided', False)
    g = G(m)
    uv0 = g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    tx = g.tex('Atlas', T_LEAF, uv0, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    vc = g.n(unreal.MaterialExpressionVertexColor)
    vca = g.custom('return Parameters.VertexColor.a;', [], F1)
    leaf = g.custom(r'''
float3 base = Tex.rgb * pow(max(VC.rgb, 0.0), 2.2) * lerp(TintA, TintB, PIR);
base *= lerp(0.28, 1.0, AO) * Bright;
base = lerp(base, base * float3(1.45, 1.08, 0.42), saturate(Dry) * 0.65);
// autumn accents (biome sheet: 1 crown in 30 turns copper/yellow): broadleaf atlas cells only (row 0 cols 0-1, row 1 col 3)
bool broad = (UV.y < 0.25 && UV.x < 0.5) || (UV.y >= 0.25 && UV.y < 0.5 && UV.x >= 0.75);
float au = (broad && frac(PIR * 13.71) < Autumn) ? 0.55 + 0.45 * frac(PIR * 7.31) : 0.0;
base = lerp(base, base * lerp(float3(2.3, 1.15, 0.30), float3(2.0, 1.6, 0.35), frac(PIR * 3.3)), au);
SSS = base * Transm * TransScale;
AOut = lerp(1.0, AO, AOStrength);
return base;''',
        [('Tex', tx, 'RGBA'), ('UV', uv0, ''), ('Autumn', g.scalar('Autumn', 0.035), ''),
         ('VC', vc, ''), ('AO', vca, ''), ('PIR', g.n(unreal.MaterialExpressionPerInstanceRandom), ''),
         ('TintA', g.vparam('TintA', 0.74, 0.86, 0.82), ''), ('TintB', g.vparam('TintB', 1.00, 0.99, 0.78), ''),
         ('Bright', g.scalar('Brightness', 0.78), ''), ('Dry', g.scalar('Dry', 0.0), ''),
         ('Transm', g.vparam('Transmission', 0.75, 0.9, 0.3), ''), ('TransScale', g.scalar('TransmissionScale', 1.0), ''),
         ('AOStrength', g.scalar('AOStrength', 1.0), '')],
        F3, [('SSS', F3), ('AOut', F1)])
    g.out(leaf, '', MP.MP_BASE_COLOR)
    g.out(leaf, 'SSS', MP.MP_SUBSURFACE_COLOR)
    g.out(leaf, 'AOut', MP.MP_AMBIENT_OCCLUSION)
    g.out(tx, 'A', MP.MP_OPACITY_MASK)
    g.out(g.scalar('LeafRough', 0.82), '', MP.MP_ROUGHNESS)
    g.out(g.scalar('LeafSpec', 0.05), '', MP.MP_SPECULAR)
    g.out(wpo(g, True), '', MP.MP_WORLD_POSITION_OFFSET)
    MEL.layout_material_expressions(m)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    return m


def master_bark():
    m = new_asset('M_TreeBark', '/Game/Trees/Materials', unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(m)
    m.set_editor_property('used_with_instanced_static_meshes', True)
    m.set_editor_property('dithered_lod_transition', True)
    g = G(m)
    uv0 = g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    tx = g.tex('BarkTex', T_BARK['broadleaf'][0], uv0, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    tn = g.tex('BarkN', T_BARK['broadleaf'][1], uv0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    vc = g.n(unreal.MaterialExpressionVertexColor)
    vca = g.custom('return Parameters.VertexColor.a;', [], F1)
    b = g.custom('float3 c = Tex * pow(max(VC.rgb, 0.0), 2.2) * Bright; return c * lerp(0.55, 1.0, AO);',
                 [('Tex', tx, 'RGB'), ('VC', vc, ''), ('AO', vca, ''), ('Bright', g.scalar('Brightness', 1.0), '')], F3)
    g.out(b, '', MP.MP_BASE_COLOR)
    g.out(tn, 'RGB', MP.MP_NORMAL)
    g.out(g.scalar('BarkRough', 0.88), '', MP.MP_ROUGHNESS)
    g.out(g.scalar('BarkSpec', 0.3), '', MP.MP_SPECULAR)
    g.out(wpo(g, False), '', MP.MP_WORLD_POSITION_OFFSET)
    MEL.layout_material_expressions(m)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    return m


def mi(name, parent, textures=None, scalars=None):
    inst = new_asset(name, '/Game/Trees/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, parent)
    for k, v in (textures or {}).items():
        MEL.set_material_instance_texture_parameter_value(inst, k, v)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(inst, k, v)
    MEL.update_material_instance(inst)
    EAL.save_loaded_asset(inst)
    return inst


IMPOSTOR = r"""
struct FI {
// Yaw-only camera-facing impostor (art/trees LOD3): side cards turn about the trunk axis to face the camera; one
// of the two side views is kept per instance (PerInstanceRandom), optionally mirrored. UV2 = (offset along the card
// in metres, code 1/2 side card, 0 top card).
float3 Bill(float3 wp, float3 cam, float3 lx, float3 ly, float3 lz, float2 uv2, float pir, float3 opos)
{
    if (uv2.y < 0.5)
    {
        // top card: only for steep views (it fills the crown seen from above, where the side cards are edge-on);
        // for oblique views it collapses onto the trunk axis, otherwise it shows as a pale plate inside the crown
        float3 upv = normalize(lz);
        float k = smoothstep(0.5, 0.7, dot(normalize(cam - wp), upv));
        float3 axisPt = opos + upv * dot(wp - opos, upv);
        return (axisPt - wp) * (1.0 - k);
    }
    float3 axis = uv2.y < 1.5 ? lx : -ly;      // FBX import mirrors Blender +Y to Unreal -Y
    float3 c = wp - axis * uv2.x;
    float3 up = normalize(lz);
    float3 f = cam - c; f -= up * dot(f, up);
    float fl = length(f);
    f = fl > 1.0 ? f / fl : normalize(axis);
    float3 r = cross(f, up);
    bool keepA = frac(pir * 7.13) < 0.5;
    bool mine = (uv2.y < 1.5) == keepA;
    float flip = frac(pir * 3.71) < 0.5 ? 1.0 : -1.0;
    float h = mine ? uv2.x * flip : 0.0;
    return c + r * h * length(axis) - wp;
}
float3 Nrm(float3 nt, float2 uv2, float3 cam, float3 wp, float3 lx, float3 ly, float3 lz, float pir)
{
    // baked normal (art/trees T_TreeImpostorsN): the LOD0 cards' volumetric normals in the view's frame
    // (x image right, y image up, z towards the bake camera), so LOD3 is lit like LOD0-2
    float3 n = nt * 2.0 - 1.0;
    float3 up = normalize(lz);
    if (uv2.y < 0.5)    // top card: image right = +X, image up = Blender +Y = Unreal -Y, towards camera = up
        return normalize(normalize(lx) * n.x - normalize(ly) * n.y + up * n.z);
    float3 f = cam - wp; f -= up * dot(f, up); f = length(f) > 1.0 ? normalize(f) : normalize(lx);
    float3 r = cross(f, up);
    float flip = frac(pir * 3.71) < 0.5 ? 1.0 : -1.0;   // mirrored instances (as in Bill)
    return normalize(r * n.x * flip + up * n.y + f * n.z);
}
};
FI I;
"""


def master_impostor():
    m = new_asset('M_TreeImpostor', '/Game/Trees/Materials', unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(m)
    m.set_editor_property('used_with_instanced_static_meshes', True)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property('opacity_mask_clip_value', 0.4)
    m.set_editor_property('dithered_lod_transition', True)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('tangent_space_normal', False)
    g = G(m)
    uv0 = g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    uv2 = g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=2)
    tx = g.tex('Atlas', T_IMP, uv0, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    pir = g.n(unreal.MaterialExpressionPerInstanceRandom)
    cam = g.n(unreal.MaterialExpressionCameraPositionWS)

    def lvec(x, y, z):
        c = g.n(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(x, y, z, 0))
        t = g.n(unreal.MaterialExpressionTransform, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        g.link(c, '', t, '')
        return t
    lx, ly, lz = lvec(100, 0, 0), lvec(0, 100, 0), lvec(0, 0, 100)
    base = g.custom('float3 b = Tex.rgb * lerp(TintA, TintB, PIR) * Bright;'
                    ' bool broad = UV.y < 0.125 || (UV.y < 0.25 && UV.x < 0.5);'          # broadleaf impostor cells 0-11
                    ' float au = (broad && frac(PIR * 13.71) < Autumn) ? 0.55 + 0.45 * frac(PIR * 7.31) : 0.0;'
                    ' b = lerp(b, b * lerp(float3(2.3, 1.15, 0.30), float3(2.0, 1.6, 0.35), frac(PIR * 3.3)), au);'
                    ' SSS = b * Transm * TransScale; return b;',
                    [('Tex', tx, 'RGBA'), ('UV', uv0, ''), ('Autumn', g.scalar('Autumn', 0.035), ''),
                     ('PIR', pir, ''), ('TintA', g.vparam('TintA', 0.74, 0.86, 0.82), ''),
                     ('TintB', g.vparam('TintB', 1.00, 0.99, 0.78), ''), ('Bright', g.scalar('Brightness', 0.78), ''),
                     ('Transm', g.vparam('Transmission', 0.75, 0.9, 0.3), ''), ('TransScale', g.scalar('TransmissionScale', 1.0), '')],
                    F3, [('SSS', F3)])
    g.out(base, '', MP.MP_BASE_COLOR)
    g.out(base, 'SSS', MP.MP_SUBSURFACE_COLOR)
    g.out(tx, 'A', MP.MP_OPACITY_MASK)
    g.out(g.scalar('LeafRough', 0.85), '', MP.MP_ROUGHNESS)
    g.out(g.scalar('LeafSpec', 0.04), '', MP.MP_SPECULAR)
    txn = g.tex('NormalAtlas', T_IMPN, uv0, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    nrm = g.custom(IMPOSTOR + 'return I.Nrm(NT, UV2, Cam, WP, LX, LY, LZ, PIR);',
                   [('NT', txn, 'RGB'), ('UV2', uv2, ''), ('Cam', cam, ''), ('WP', g.n(unreal.MaterialExpressionWorldPosition), ''),
                    ('LX', lx, ''), ('LY', ly, ''), ('LZ', lz, ''), ('PIR', pir, '')], F3)
    g.out(nrm, '', MP.MP_NORMAL)
    sway = g.custom(WIND + 'return W.Wpo(WP, Ctr, WD, T, PIR, float2(0, 0), UV1, float2(0, 0), float3(0, 0, 1), Strength, 0, 0, Speed);',
                    [('WP', g.n(unreal.MaterialExpressionWorldPosition), ''), ('Ctr', g.vparam('PlanetCenter', 0, 0, 0), ''),
                     ('WD', g.vparam('WindDir', 1.0, 0.4, 0.0), ''), ('T', g.n(unreal.MaterialExpressionTime), ''),
                     ('PIR', pir, ''), ('UV1', g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1), ''),
                     ('Strength', g.scalar('WindStrength', 100.0), ''), ('Speed', g.scalar('WindSpeed', 1.0), '')], F3)
    bill = g.custom(IMPOSTOR + 'return I.Bill(WP, Cam, LX, LY, LZ, UV2, PIR, OPos) + Sway;',
                    [('WP', g.n(unreal.MaterialExpressionWorldPosition), ''), ('Cam', cam, ''), ('LX', lx, ''), ('LY', ly, ''),
                     ('LZ', lz, ''), ('UV2', uv2, ''), ('PIR', pir, ''), ('OPos', g.n(unreal.MaterialExpressionObjectPositionWS), ''),
                     ('Sway', sway, '')], F3)
    g.out(bill, '', MP.MP_WORLD_POSITION_OFFSET)
    MEL.layout_material_expressions(m)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    return m


M_LEAF = master_leaf()
M_IMPOSTOR = master_impostor()
M_BARK = master_bark()
MI_LEAF = mi('MI_TreeLeaf', M_LEAF, {'Atlas': T_LEAF})
MI_IMP = mi('MI_TreeImpostor', M_IMPOSTOR, {'Atlas': T_IMP, 'NormalAtlas': T_IMPN}, {'Brightness': 0.78})   # = M_TreeLeaf: both apply the same crown AO (verification: 0.7 with baked normals left LOD3 ~8% darker than LOD2)
MI_BARK = {sp: mi('MI_TreeBark_%s' % sp.capitalize(), M_BARK, {'BarkTex': T_BARK[sp][0], 'BarkN': T_BARK[sp][1]}) for sp in SPECIES}
log('materials done')


# ---------------------------------------------------------------- meshes
def fbx_options():
    o = unreal.FbxImportUI()
    o.set_editor_property('import_mesh', True)
    o.set_editor_property('import_as_skeletal', False)
    o.set_editor_property('import_materials', False)
    o.set_editor_property('import_textures', False)
    o.set_editor_property('import_animations', False)
    o.set_editor_property('automated_import_should_detect_type', False)
    o.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    s = o.get_editor_property('static_mesh_import_data')
    for k, v in (('combine_meshes', True), ('generate_lightmap_u_vs', False), ('auto_generate_collision', False),
                 ('remove_degenerates', False), ('build_nanite', False), ('convert_scene', True), ('import_uniform_scale', 1.0),
                 ('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS),
                 ('vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)):
        try:
            s.set_editor_property(k, v)
        except Exception as e:
            ERR.append('fbx option %s: %s' % (k, e))
    return o


SCREEN = MAN.get('screen_sizes', [1.0, 0.3, 0.1, 0.035])
summary = []
for sp in SPECIES:
    if ONLY and sp not in ONLY.split(','):
        continue
    for v_str, info in sorted(MAN['species'].get(sp, {}).items()):
        name = info['mesh']
        d = os.path.join(ART, 'export', sp.capitalize())
        dest = '/Game/Trees/' + sp.capitalize()
        mesh = import_file(os.path.join(d, name + '.fbx'), dest, name, fbx_options())
        if not mesh:
            continue
        for l in (1, 2, 3):
            r = SES.import_lod(mesh, l, os.path.join(d, '%s_LOD%d.fbx' % (name, l)))
            if r != l:
                ERR.append('%s import_lod %d returned %s' % (name, l, r))
        nl = SES.get_lod_count(mesh)
        for l in range(nl):
            bs = SES.get_lod_build_settings(mesh, l)
            for k, val in (('use_full_precision_u_vs', True), ('recompute_normals', False), ('recompute_tangents', True),
                           ('use_mikk_t_space', True), ('remove_degenerates', False), ('generate_lightmap_u_vs', False),
                           ('distance_field_resolution_scale', 0.0)):
                try:
                    bs.set_editor_property(k, val)
                except Exception as e:
                    ERR.append('build setting %s: %s' % (k, e))
            SES.set_lod_build_settings(mesh, l, bs)
        SES.set_lod_screen_sizes(mesh, SCREEN[:nl])
        # materials by slot name
        mats = mesh.get_editor_property('static_materials')
        names = []
        for i, sm in enumerate(mats):
            sn = str(sm.get_editor_property('material_slot_name'))
            names.append(sn)
            if sn.startswith('Bark'):
                mesh.set_material(i, MI_BARK[sp])
            elif sn.startswith('Leaves'):
                mesh.set_material(i, MI_LEAF)
            elif sn.startswith('Impostor'):
                mesh.set_material(i, MI_IMP)
            else:
                ERR.append('%s: unknown slot %s' % (name, sn))
        EAL.save_loaded_asset(mesh)
        b = mesh.get_bounds()
        ext = b.box_extent
        tris = []
        for l in range(nl):
            try:
                tris.append(mesh.get_num_triangles(l))
            except Exception:
                tris.append(-1)
        o = b.origin
        line = '%s lods=%d slots=%s origin_cm=(%.0f,%.0f,%.0f) extent_cm=(%.0f,%.0f,%.0f) tris=%s' % (name, nl, names, o.x, o.y, o.z, ext.x, ext.y, ext.z, tris)
        summary.append(line)
        log(line)

EAL.save_directory('/Game/Trees', only_if_is_dirty=False, recursive=True)
for e in ERR:
    log('ERROR ' + e)
log('DONE meshes=%d errors=%d' % (len(summary), len(ERR)))
