# CloudLab: create the weather-driven volumetric cloud material (and the star sky material).
# UnrealEditor-Cmd.exe PlanetLab.uproject -run=pythonscript -script=<this file> -unattended -nosplash -nullrhi
# Re-runnable: deletes and recreates /Game/CloudLab/M_WeatherClouds and /Game/CloudLab/M_StarSky.
import unreal, os

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/CloudLab'
HERE = os.path.dirname(os.path.abspath(__file__))


def new_mat(name):
    path = DIR + '/' + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return AT.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())


def node(m, cls, x, y, **props):
    e = MEL.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def link(a, a_out, b, b_in):
    ok = MEL.connect_material_expressions(a, a_out, b, b_in)
    if not ok:
        unreal.log_error('link failed: %s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in))


def scalar(m, name, val, x, y):
    return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=val)


def custom(m, x, y, code, inputs, out_type, extra_outputs=()):
    c = node(m, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property('code', code)
    c.set_editor_property('output_type', out_type)
    ins = []
    for nm in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', nm)
        ins.append(ci)
    c.set_editor_property('inputs', ins)
    outs = []
    for nm, t in extra_outputs:
        co = unreal.CustomOutput()
        co.set_editor_property('output_name', nm)
        co.set_editor_property('output_type', t)
        outs.append(co)
    if outs:
        c.set_editor_property('additional_outputs', outs)
    return c


def mask(m, src, x, y, r=False, g=False, b=False, a=False):
    k = node(m, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
    link(src, '', k, '')
    return k


def finish(m):
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    unreal.log('saved ' + m.get_path_name())


# ---------------------------------------------------------------- clouds
CLOUD_HLSL = open(os.path.join(HERE, 'cloud_density.hlsl')).read()

m = new_mat('M_WeatherClouds')
m.set_editor_property('material_domain', unreal.MaterialDomain.MD_VOLUME)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('used_with_volumetric_cloud', True)

wp = node(m, unreal.MaterialExpressionWorldPosition, -1600, -400)
csa = node(m, unreal.MaterialExpressionCloudSampleAttribute, -1600, -250)
tm = node(m, unreal.MaterialExpressionTime, -1600, -120)
weather = node(m, unreal.MaterialExpressionTextureObjectParameter, -1600, 0, parameter_name='Weather',
               texture=unreal.load_asset('/Engine/EngineSky/VolumetricClouds/T_Volume_PerlinWorley_Balanced'))
shape = node(m, unreal.MaterialExpressionTextureObjectParameter, -1600, 200, parameter_name='ShapeTex',
             texture=unreal.load_asset('/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseShape128'))
detail = node(m, unreal.MaterialExpressionTextureObjectParameter, -1600, 400, parameter_name='DetailTex',
              texture=unreal.load_asset('/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseErosion32'))

params = [('LonOffset', 0.0), ('CoverGain', 1.0), ('CoverBias', 0.0), ('ShapeKm', 6.0), ('DetailKm', 1.6),
          ('DetailAmt', 0.35), ('MacroAmt', 0.35), ('NoiseAmp', 0.4), ('EdgeSharp', 4.0), ('VeilStart', 0.3), ('VeilAmt', 0.015), ('TowerBase', 0.55), ('TowerGain', 1.2), ('TopMip', 3.0), ('MidAmt', 0.6), ('CanopyAmt', 0.35), ('SkyAmb', 0.8), ('LodNearKm', 30.0), ('LodFarKm', 250.0), ('FarNoise', 0.35), ('FarSharp', 0.5), ('WindKmS', 0.002), ('ThickBoost', 2.0), ('MacroKm', 24.0)]
inputs = ['WP', 'CamPos', 'Alt', 'ShadowDist', 'Time', 'SunDir', 'Weather', 'ShapeTex', 'DetailTex'] + [p[0] for p in params]
cu = custom(m, -900, 0, CLOUD_HLSL, inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT4)
link(wp, '', cu, 'WP')
camp = node(m, unreal.MaterialExpressionCameraPositionWS, -1600, -500)
link(camp, '', cu, 'CamPos')
link(csa, 'NormAltitudeInLayer', cu, 'Alt')
link(csa, 'ShadowSampleDistance', cu, 'ShadowDist')
link(tm, '', cu, 'Time')
sund = node(m, unreal.MaterialExpressionSkyAtmosphereLightDirection, -1600, -60)
link(sund, '', cu, 'SunDir')
link(weather, '', cu, 'Weather')
link(shape, '', cu, 'ShapeTex')
link(detail, '', cu, 'DetailTex')
for i, (nm, v) in enumerate(params):
    link(scalar(m, nm, v, -1300, 600 + i * 90), '', cu, nm)

dens = mask(m, cu, -600, -200, r=True)
alb = mask(m, cu, -600, -50, g=True)
ao = mask(m, cu, -600, 100, b=True)
cons = mask(m, cu, -600, 250, a=True)

ext = scalar(m, 'ExtinctionPerM', 0.06, -600, -350)
em = node(m, unreal.MaterialExpressionMultiply, -350, -250)
link(dens, '', em, 'A'); link(ext, '', em, 'B')
e3 = node(m, unreal.MaterialExpressionAppendVector, -150, -300)
e2 = node(m, unreal.MaterialExpressionAppendVector, -250, -300)
link(em, '', e2, 'A'); link(em, '', e2, 'B')
link(e2, '', e3, 'A'); link(em, '', e3, 'B')
MEL.connect_material_property(e3, '', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)

albc = node(m, unreal.MaterialExpressionVectorParameter, -600, -500, parameter_name='Albedo',
            default_value=unreal.LinearColor(0.96, 0.96, 0.97, 1.0))
am = node(m, unreal.MaterialExpressionMultiply, -350, -80)
link(albc, '', am, 'A'); link(alb, '', am, 'B')
MEL.connect_material_property(am, '', unreal.MaterialProperty.MP_BASE_COLOR)
MEL.connect_material_property(ao, '', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

adv = node(m, unreal.MaterialExpressionVolumetricAdvancedMaterialOutput, -150, 200)
adv.set_editor_property('multi_scattering_approximation_octave_count', 2)
adv.set_editor_property('ray_march_volume_shadow', True)
adv.set_editor_property('ground_contribution', False)
# Phase and multiple scattering are scalar parameters so a dynamic instance can tune them per scene.
for i, (pin, nm, v) in enumerate([('PhaseG', 'PhaseG', 0.6), ('PhaseG2', 'PhaseG2', -0.25), ('PhaseBlend', 'PhaseBlend', 0.35),
                                  ('MultiScatteringContribution', 'MSContribution', 0.5),
                                  ('MultiScatteringOcclusion', 'MSOcclusion', 0.5),
                                  ('MultiScatteringEccentricity', 'MSEccentricity', 0.6)]):
    link(scalar(m, nm, v, -400, 400 + i * 90), '', adv, pin)
link(cons, '', adv, 'ConservativeDensity')
finish(m)

# ---------------------------------------------------------------- star sky (unlit, on a huge inward-facing sphere)
STAR_HLSL = open(os.path.join(HERE, 'star_sky.hlsl')).read()
m = new_mat('M_StarSky')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('two_sided', True)
# not is_sky: an ordinary unlit far sphere, so the atmosphere dims it near the limb
cv = node(m, unreal.MaterialExpressionCameraVectorWS, -900, 0)
sb = scalar(m, 'StarBrightness', 1.0, -900, 150)
cu = custom(m, -500, 0, STAR_HLSL, ['V', 'Bright'], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(cv, '', cu, 'V'); link(sb, '', cu, 'Bright')
MEL.connect_material_property(cu, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(m)

# ---------------------------------------------------------------- moon (lit, albedo from object-space noise)
MOON_HLSL = open(os.path.join(HERE, 'moon_albedo.hlsl')).read()
m = new_mat('M_Moon')
lp = node(m, unreal.MaterialExpressionLocalPosition, -900, 0)
cu = custom(m, -500, 0, MOON_HLSL, ['LP'], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(lp, '', cu, 'LP')
MEL.connect_material_property(cu, '', unreal.MaterialProperty.MP_BASE_COLOR)
MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.95), '', unreal.MaterialProperty.MP_ROUGHNESS)
MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 300, r=0.2), '', unreal.MaterialProperty.MP_SPECULAR)
finish(m)
