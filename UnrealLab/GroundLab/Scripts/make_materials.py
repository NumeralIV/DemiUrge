# DEMIURGE surface lab: create the surface materials (land, ocean SLW, ocean opaque fallback).
# The shading lives in HLSL files under Scripts/hlsl, pasted into Custom nodes (one struct with
# member functions per node, the usual way to define functions in a Custom expression).
# UnrealEditor-Cmd.exe PlanetLab.uproject -run=pythonscript -script=<this file>
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/Surface'
HERE = os.path.dirname(os.path.abspath(__file__))


def hlsl(name):
    with open(os.path.join(HERE, 'hlsl', name), 'r', encoding='utf-8') as f:
        return f.read()


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
    return ok


def custom(m, x, y, code, inputs, outputs):
    c = node(m, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property('code', code)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for n in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    c.set_editor_property('inputs', ins)
    outs = []
    for n, t in outputs:
        co = unreal.CustomOutput()
        co.set_editor_property('output_name', n)
        co.set_editor_property('output_type', t)
        outs.append(co)
    c.set_editor_property('additional_outputs', outs)
    return c


def common_inputs(m, x, y):
    d = {}
    d['P'] = node(m, unreal.MaterialExpressionWorldPosition, x, y)
    d['Nv'] = node(m, unreal.MaterialExpressionVertexNormalWS, x, y + 80)
    vc = node(m, unreal.MaterialExpressionVertexColor, x - 300, y + 160)
    ap = node(m, unreal.MaterialExpressionAppendVector, x, y + 160)
    link(vc, '', ap, 'A')
    link(vc, 'A', ap, 'B')
    d['VC'] = ap
    for i in range(4):
        d['U%d' % i] = node(m, unreal.MaterialExpressionTextureCoordinate, x, y + 260 + 80 * i, coordinate_index=i)
    d['Tm'] = node(m, unreal.MaterialExpressionTime, x, y + 600)
    d['SunD'] = node(m, unreal.MaterialExpressionSkyAtmosphereLightDirection, x, y + 680)
    return d


def finish(m):
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    unreal.log('saved ' + m.get_path_name())


F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
COMMON = hlsl('common.hlsl')

# ---------------------------------------------------------------- land
for LAND_NAME, GROUND_ON in (('M_SurfLand', 1), ('M_SurfLandNoGround', 0)):
  m = new_mat(LAND_NAME)
  m.set_editor_property('tangent_space_normal', False)
  d = common_inputs(m, -1400, -300)
  names = ['P', 'Nv', 'VC', 'U0', 'U1', 'U2', 'U3', 'Tm', 'SunD', 'RiverTex', 'LandTex', 'TileH', 'TileF', 'TileO', 'TileE', 'TileN', 'Season', 'Age', 'GDebug']
  code = ('#define GROUND_ON %d\n' % GROUND_ON + 'struct FSurf {\n' + COMMON + hlsl('ground.hlsl') + hlsl('land.hlsl') + '\n};\nFSurf S;\n'
          'return S.Land(P, Nv, VC, U0, U1, U2, U3, Tm, SunD, RiverTex, RiverTexSampler, LandTex, LandTexSampler, '
          'TileH, TileHSampler, TileF, TileFSampler, TileO, TileE, TileN, Season, Age, GDebug, Rough, Nw, Emis, AO);\n')
  c = custom(m, -700, -200, code, names, [('Rough', F1), ('Nw', F3), ('Emis', F3), ('AO', F1)])
  # default for the river map: a linear 1x1-ish texture (the game sets the generated one)
  tpath = DIR + '/T_RiverDefault'
  if GROUND_ON:
      if EAL.does_asset_exist(tpath):
          EAL.delete_asset(tpath)
      tdef = AT.create_asset('T_RiverDefault', DIR, unreal.Texture2D, unreal.Texture2DFactoryNew())
      tdef.set_editor_property('srgb', False)
      tdef.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
      EAL.save_loaded_asset(tdef)
  d['RiverTex'] = node(m, unreal.MaterialExpressionTextureObjectParameter, -1400, 500, parameter_name='RiverTex', texture=tdef,
                       sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
  for i, tn in enumerate(['LandTex', 'TileH', 'TileF']):
      d[tn] = node(m, unreal.MaterialExpressionTextureObjectParameter, -1400, 600 + 100 * i, parameter_name=tn, texture=tdef,
                   sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
  for i, vn in enumerate(['TileO', 'TileE', 'TileN']):
      vp = node(m, unreal.MaterialExpressionVectorParameter, -1700, 900 + 120 * i, parameter_name=vn,
                default_value=unreal.LinearColor(0, 0, 1, 1000))
      ap4 = node(m, unreal.MaterialExpressionAppendVector, -1400, 900 + 120 * i)
      link(vp, '', ap4, 'A')
      link(vp, 'A', ap4, 'B')
      d[vn] = ap4
  d['Season'] = node(m, unreal.MaterialExpressionScalarParameter, -1400, 1300, parameter_name='Season', default_value=0.42)
  d['Age'] = node(m, unreal.MaterialExpressionScalarParameter, -1400, 1380, parameter_name='Age', default_value=0.35)
  d['GDebug'] = node(m, unreal.MaterialExpressionScalarParameter, -1400, 1460, parameter_name='GDebug', default_value=0.0)
  for n in names:
      link(d[n], '', c, n)
  MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
  MEL.connect_material_property(c, 'Rough', unreal.MaterialProperty.MP_ROUGHNESS)
  MEL.connect_material_property(c, 'Nw', unreal.MaterialProperty.MP_NORMAL)
  MEL.connect_material_property(c, 'Emis', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
  MEL.connect_material_property(c, 'AO', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
  spec = node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.35)
  MEL.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
  finish(m)


# ---------------------------------------------------------------- ocean
def water_mat(name, slw):
    m = new_mat(name)
    m.set_editor_property('tangent_space_normal', False)
    if slw:
        m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    d = common_inputs(m, -1400, -300)
    names = ['P', 'U0', 'U1', 'Tm', 'SunD']
    code = 'struct FSurf {\n' + COMMON + hlsl('water.hlsl') + '\n};\nFSurf S;\nreturn S.Water(P, U0, U1, Tm, SunD, Rough, Nw, Opac, Opq, AO);\n'
    c = custom(m, -700, -200, code, names, [('Rough', F1), ('Nw', F3), ('Opac', F1), ('Opq', F3), ('AO', F1)])
    for n in names:
        link(d[n], '', c, n)
    MEL.connect_material_property(c, 'Rough', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(c, 'Nw', unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(c, 'AO', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    spec = node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.25)
    MEL.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
    if slw:
        MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
        MEL.connect_material_property(c, 'Opac', unreal.MaterialProperty.MP_OPACITY)
        out = node(m, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, -200, 400)
        # coefficients per centimetre
        sc = node(m, unreal.MaterialExpressionVectorParameter, -600, 400, parameter_name='Scattering',
                  default_value=unreal.LinearColor(0.00002, 0.00007, 0.00010, 0))
        ab = node(m, unreal.MaterialExpressionVectorParameter, -600, 560, parameter_name='Absorption',
                  default_value=unreal.LinearColor(0.0040, 0.00065, 0.00025, 0))
        pg = node(m, unreal.MaterialExpressionConstant, -600, 720, r=0.3)
        for src, pin_names in ((sc, ['ScatteringCoefficients', 'Scattering Coefficients']),
                               (ab, ['AbsorptionCoefficients', 'Absorption Coefficients']),
                               (pg, ['PhaseG', 'Phase G'])):
            done = False
            for pn in pin_names:
                if MEL.connect_material_expressions(src, '', out, pn):
                    done = True
                    break
            if not done:
                unreal.log_error('SLW pin not found: %s' % pin_names)
    else:
        MEL.connect_material_property(c, 'Opq', unreal.MaterialProperty.MP_BASE_COLOR)
    finish(m)


water_mat('M_SurfWater', True)
water_mat('M_SurfWaterOpaque', False)
