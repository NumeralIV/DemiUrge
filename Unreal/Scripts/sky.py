# DEMIURGE sky (C2): creates the sky module's materials under /Game/Sky (SPEC-UNREAL 0.3).
#   UnrealEditor-Cmd.exe Demiurge.uproject -run=pythonscript -script=<this file> -unattended -nosplash -nullrhi
# Re-runnable: deletes and recreates every asset it makes. The HLSL lives next to it in Scripts/sky/.
#   M_SkyClouds  volumetric cloud material (Volume domain): cover + warp weather maps, cell texture, 3D noise
#   M_SkyStars   unlit star field / milky way / comet rain on the world-fixed sky sphere
#   M_SkyMoon    lit moon surface (maria, crater rims)
#   M_SkyAurora  unlit additive aurora curtains
import unreal, os

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/Sky'
HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sky')
FAILS = []


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
    if not MEL.connect_material_expressions(a, a_out, b, b_in):
        FAILS.append('%s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in))
        unreal.log_error('sky.py link failed: %s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in))


def prop(src, out, p):
    if not MEL.connect_material_property(src, out, p):
        FAILS.append('%s.%s -> %s' % (src.get_name(), out, p))
        unreal.log_error('sky.py property link failed: %s -> %s' % (src.get_name(), p))


def scalar(m, name, val, x, y):
    return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=val)


def vector(m, name, val, x, y):
    return node(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                default_value=unreal.LinearColor(val[0], val[1], val[2], val[3]))


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
    unreal.log('sky.py saved ' + m.get_path_name())


def hlsl(name):
    with open(os.path.join(HERE, name)) as f:
        return f.read()


# ---------------------------------------------------------------- clouds
def make_clouds(name, molten):
    m = new_mat(name)
    m.set_editor_property('material_domain', unreal.MaterialDomain.MD_VOLUME)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('used_with_volumetric_cloud', True)

    # 2D maps: the default texture's sampler type must match the runtime textures (TC_VectorDisplacementmap, linear)
    TEX2D = unreal.load_asset('/Engine/EngineSky/VolumetricClouds/T_Volume_PerlinWorley_Balanced')
    wp = node(m, unreal.MaterialExpressionWorldPosition, -2000, -600)
    camp = node(m, unreal.MaterialExpressionCameraPositionWS, -2000, -700)
    csa = node(m, unreal.MaterialExpressionCloudSampleAttribute, -2000, -500)
    sund = node(m, unreal.MaterialExpressionSkyAtmosphereLightDirection, -2000, -400)
    texs = [('WeatherA', TEX2D), ('WeatherB', TEX2D), ('WarpA', TEX2D), ('WarpB', TEX2D), ('Fine', TEX2D),
            ('ShapeTex', unreal.load_asset('/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseShape128')),
            ('DetailTex', unreal.load_asset('/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseErosion32'))]
    scalars = [('Yaw', 0.0), ('Blend', 0.0), ('SkyTime', 0.0), ('FlowPeriod', 30.0), ('WindTrade', 0.004), ('WindWest', 0.006),
               ('WindPolar', 0.002), ('CoverScale', 1.0), ('CoverGain', 1.0), ('CoverBias', 0.0), ('NfGain', 1.0),
               ('ShapeKm', 6.0), ('DetailKm', 1.6), ('DetailAmt', 0.35), ('MacroAmt', 0.35), ('NoiseAmp', 0.4), ('EdgeSharp', 4.0),
               ('VeilStart', 0.3), ('VeilAmt', 0.015), ('TowerBase', 0.55), ('TowerGain', 1.2), ('TopMip', 3.0), ('MidAmt', 0.6),
               ('CanopyAmt', 0.35), ('CanopyLo', 0.5), ('SkyAmb', 0.8), ('LodNearKm', 12.0), ('LodFarKm', 160.0), ('FarNoise', 0.15), ('FarSharp', 0.5),
               ('WindKmS', 0.002), ('ThickBoost', 2.0), ('MacroKm', 24.0), ('FineMipBias', 0.0), ('FineMipKm', 300.0),
               ('FineMipBoost', 0.3), ('CirrusGain', 1.0), ('CirrusLo', 0.8), ('CirrusDens', 0.025), ('CirrusLenKm', 10.0), ('CirrusWidKm', 0.5), ('ThinDens', 0.45), ('NightAmb', 0.0)]
    vectors = []
    inputs = ['WP', 'CamPos', 'Alt', 'ShadowDist', 'SunDir'] + [t[0] for t in texs] + [s[0] for s in scalars] + [v[0] for v in vectors]
    cu = custom(m, -1100, 0, hlsl('cloud_density.hlsl'), inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                extra_outputs=[('Glow', unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
    link(wp, '', cu, 'WP')
    link(camp, '', cu, 'CamPos')
    link(csa, 'NormAltitudeInLayer', cu, 'Alt')
    link(csa, 'ShadowSampleDistance', cu, 'ShadowDist')
    link(sund, '', cu, 'SunDir')
    for i, (nm, t) in enumerate(texs):
        link(node(m, unreal.MaterialExpressionTextureObjectParameter, -2000, -250 + i * 160, parameter_name=nm, texture=t), '', cu, nm)
    for i, (nm, v) in enumerate(scalars):
        link(scalar(m, nm, v, -1600, 600 + i * 80), '', cu, nm)
    for i, (nm, v) in enumerate(vectors):
        link(vector(m, nm, v, -1900, 600 + i * 110), 'RGBA', cu, nm)

    dens = mask(m, cu, -700, -200, r=True)
    alb = mask(m, cu, -700, -50, g=True)
    ao = mask(m, cu, -700, 100, b=True)
    cons = mask(m, cu, -700, 250, a=True)
    ext = scalar(m, 'ExtinctionPerM', 0.06, -700, -350)
    em = node(m, unreal.MaterialExpressionMultiply, -450, -250)
    link(dens, '', em, 'A'); link(ext, '', em, 'B')
    e2 = node(m, unreal.MaterialExpressionAppendVector, -300, -300)
    e3 = node(m, unreal.MaterialExpressionAppendVector, -150, -300)
    link(em, '', e2, 'A'); link(em, '', e2, 'B')
    link(e2, '', e3, 'A'); link(em, '', e3, 'B')
    prop(e3, '', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)          # "Extinction" for volume materials
    albc = vector(m, 'Albedo', (0.94, 0.96, 1.0, 1.0), -700, -500)   # a touch cool: sunlight through the scaled air yellows the tops
    am = node(m, unreal.MaterialExpressionMultiply, -450, -80)
    link(albc, '', am, 'A'); link(alb, '', am, 'B')
    prop(am, '', unreal.MaterialProperty.MP_BASE_COLOR)
    prop(ao, '', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    adv = node(m, unreal.MaterialExpressionVolumetricAdvancedMaterialOutput, -150, 200)
    adv.set_editor_property('multi_scattering_approximation_octave_count', 2)
    adv.set_editor_property('ray_march_volume_shadow', True)
    adv.set_editor_property('ground_contribution', False)
    for i, (pin, nm, v) in enumerate([('PhaseG', 'PhaseG', 0.6), ('PhaseG2', 'PhaseG2', -0.25), ('PhaseBlend', 'PhaseBlend', 0.35),
                                      ('MultiScatteringContribution', 'MSContribution', 0.75),  # 0.5 left dense storm cores dark and dotted at low sun; 0.8 flattens noon
                                      ('MultiScatteringOcclusion', 'MSOcclusion', 0.35),
                                      ('MultiScatteringEccentricity', 'MSEccentricity', 0.6)]):
        link(scalar(m, nm, v, -450, 400 + i * 90), '', adv, pin)
    link(cons, '', adv, 'ConservativeDensity')
    if molten:
        # glow of the magma under the ash veil: emissive weighted toward the cloud base (costs ms: molten only)
        gc = vector(m, 'GlowColor', (1.0, 0.32, 0.08, 1.0), -700, 700)
        gh = scalar(m, 'Heat', 1.0, -700, 820)
        gg = scalar(m, 'GlowGain', 0.012, -700, 900)
        m1 = node(m, unreal.MaterialExpressionMultiply, -450, 700)
        link(cu, 'Glow', m1, 'A'); link(gh, '', m1, 'B')
        m2 = node(m, unreal.MaterialExpressionMultiply, -350, 760)
        link(m1, '', m2, 'A'); link(gg, '', m2, 'B')
        m3 = node(m, unreal.MaterialExpressionMultiply, -250, 700)
        link(gc, '', m3, 'A'); link(m2, '', m3, 'B')
        prop(m3, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(m)



make_clouds('M_SkyClouds', False)
make_clouds('M_SkyCloudsMolten', True)

# ---------------------------------------------------------------- stars (unlit, far inward-facing sphere; not is_sky)
m = new_mat('M_SkyStars')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('two_sided', True)
cv = node(m, unreal.MaterialExpressionCameraVectorWS, -900, 0)
tm = node(m, unreal.MaterialExpressionTime, -900, 100)
cu = custom(m, -500, 0, hlsl('star_sky.hlsl'), ['V', 'Bright', 'Time', 'Twinkle', 'Galaxy', 'CometRain'],
            unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(cv, '', cu, 'V'); link(tm, '', cu, 'Time')
link(scalar(m, 'StarBrightness', 3.0, -900, 200), '', cu, 'Bright')
link(scalar(m, 'Twinkle', 0.12, -900, 300), '', cu, 'Twinkle')
link(scalar(m, 'Galaxy', 1.0, -900, 400), '', cu, 'Galaxy')
link(scalar(m, 'CometRain', 0.0, -900, 500), '', cu, 'CometRain')
prop(cu, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(m)

# ---------------------------------------------------------------- moon (lit; albedo from object-space noise)
# Unlit, shaded here from the sun direction: a lit material 2,500 km out picks up the sun's cloud-shadow map
# (clamped at its edge) and went black in some views.
m = new_mat('M_SkyMoon')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
lp = node(m, unreal.MaterialExpressionLocalPosition, -900, 0)
cu = custom(m, -500, 0, hlsl('moon_albedo.hlsl'), ['LP'], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(lp, '', cu, 'LP')
nw = node(m, unreal.MaterialExpressionVertexNormalWS, -900, 200)
sdm = vector(m, 'SunDir', (1.0, 0.0, 0.0, 0.0), -900, 300)
shade = custom(m, -300, 100, 'float l = dot(normalize(N), normalize(S)); float lit = saturate(l) * smoothstep(-0.03, 0.08, l); return A * (lit * Gain + Earthshine);',
               ['A', 'N', 'S', 'Gain', 'Earthshine'], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(cu, '', shade, 'A'); link(nw, '', shade, 'N')
link(mask(m, sdm, -700, 300, r=True, g=True, b=True), '', shade, 'S')
link(scalar(m, 'SunGain', 3.2, -900, 420), '', shade, 'Gain')        # 10 lux / pi: a Lambertian moon under the sun
link(scalar(m, 'Earthshine', 0.02, -900, 500), '', shade, 'Earthshine')
prop(shade, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(m)

# ---------------------------------------------------------------- aurora (unlit, additive, two-sided)
m = new_mat('M_SkyAurora')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('two_sided', True)
uvn = node(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
wpa = node(m, unreal.MaterialExpressionWorldPosition, -900, 100)
tm = node(m, unreal.MaterialExpressionTime, -900, 200)
cu = custom(m, -500, 0, hlsl('aurora.hlsl'), ['UV', 'WP', 'Time', 'SunDir', 'Strength'], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(uvn, '', cu, 'UV'); link(wpa, '', cu, 'WP'); link(tm, '', cu, 'Time')
sd = vector(m, 'SunDir', (1.0, 0.0, 0.0, 0.0), -900, 300)
link(mask(m, sd, -700, 300, r=True, g=True, b=True), '', cu, 'SunDir')
link(scalar(m, 'Strength', 1.0, -900, 400), '', cu, 'Strength')
prop(cu, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(m)

if FAILS:
    unreal.log_error('sky.py: %d links failed' % len(FAILS))
else:
    unreal.log('sky.py: all materials built, no failed links')

# ---------------------------------------------------------------- lightning flash glow (unlit, additive, two-sided)
m = new_mat('M_SkyFlash')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('two_sided', True)
uvn = node(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
cu = custom(m, -500, 0, hlsl('flash.hlsl'), ['UV', 'Color', 'Intensity', 'Seed'], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(uvn, '', cu, 'UV')
link(vector(m, 'Color', (0.75, 0.82, 1.0, 1.0), -900, 100), 'RGBA', cu, 'Color')
link(scalar(m, 'Intensity', 0.0, -900, 250), '', cu, 'Intensity')
link(scalar(m, 'Seed', 0.0, -900, 350), '', cu, 'Seed')
prop(cu, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(m)
unreal.log('sky.py: flash material done (%d failed links in total)' % len(FAILS))
