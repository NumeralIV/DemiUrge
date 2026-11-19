# DEMIURGE planet (C1): creates the planet surface materials under /Game/Planet (re-runnable).
#   UnrealEditor-Cmd.exe <Demiurge.uproject> -run=pythonscript -script=<this file> -unattended -nosplash -nullrhi
#   (or: powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/planet/mats.ps1)
# The shading lives in HLSL files under Scripts/planet (common, land, water), pasted into Custom nodes (one
# struct with member functions per node, the usual way to define functions in a Custom expression).
#   M_PlanetLand  : opaque, world-space normal; per-cell state from the planet maps (DmPlanetMaps.h)
#   M_PlanetWater : Single Layer Water (sea and lakes), per-pixel scattering for river plumes
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/Planet'
HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'planet')
ERRORS = []


def hlsl(name):
    with open(os.path.join(HERE, name), 'r', encoding='utf-8') as f:
        return f.read()


def new_asset(name, cls, factory):
    path = DIR + '/' + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return AT.create_asset(name, DIR, cls, factory)


def node(m, cls, x, y, **props):
    e = MEL.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def link(a, a_out, b, b_in):
    ok = MEL.connect_material_expressions(a, a_out, b, b_in)
    if not ok:
        msg = 'link failed: %s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in)
        ERRORS.append(msg)
        unreal.log_error(msg)
    return ok


def prop(e, out, p):
    ok = MEL.connect_material_property(e, out, p)
    if not ok:
        msg = 'property link failed: %s.%s -> %s' % (e.get_name(), out, p)
        ERRORS.append(msg)
        unreal.log_error(msg)
    return ok


F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3


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


def finish(m):
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    unreal.log('planet.py: saved ' + m.get_path_name())


# default texture for the texture parameters (the game sets the generated planet maps)
tdef = new_asset('T_PlanetDefault', unreal.Texture2D, unreal.Texture2DFactoryNew())
tdef.set_editor_property('srgb', False)
tdef.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
EAL.save_loaded_asset(tdef)


def common_inputs(m, x, y, scalars, textures):
    d = {}
    d['Pw'] = node(m, unreal.MaterialExpressionWorldPosition, x, y)
    d['NvW'] = node(m, unreal.MaterialExpressionVertexNormalWS, x, y + 80)
    d['VC'] = node(m, unreal.MaterialExpressionVertexColor, x, y + 160)
    for i in range(4):
        d['U%d' % i] = node(m, unreal.MaterialExpressionTextureCoordinate, x, y + 260 + 80 * i, coordinate_index=i)
    d['Tm'] = node(m, unreal.MaterialExpressionTime, x, y + 600)
    d['SunW'] = node(m, unreal.MaterialExpressionSkyAtmosphereLightDirection, x, y + 680)
    for i, n in enumerate(['RX4', 'RY4', 'RZ4', 'Ctr4']):
        pn = n[:-1] if n != 'Ctr4' else 'Center'
        dv = unreal.LinearColor(1 if n == 'RX4' else 0, 1 if n == 'RY4' else 0, 1 if n == 'RZ4' else 0, 0)
        d[n] = node(m, unreal.MaterialExpressionVectorParameter, x, y + 780 + 90 * i, parameter_name=pn, default_value=dv)
    for i, (n, pn, dv) in enumerate(scalars):
        d[n] = node(m, unreal.MaterialExpressionScalarParameter, x, y + 1200 + 70 * i, parameter_name=pn, default_value=dv)
    for i, n in enumerate(textures):
        d[n] = node(m, unreal.MaterialExpressionTextureObjectParameter, x, y + 1700 + 90 * i, parameter_name=n, texture=tdef,
                    sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return d


COMMON = hlsl('common.hlsl')

# ---------------------------------------------------------------- land
m = new_asset('M_PlanetLand', unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property('tangent_space_normal', False)
scalars = [('SeaR', 'SeaR', 1.0e7), ('Heat', 'Heat', 0.0), ('OverOn', 'Overlay', 0.0), ('Dbg', 'Debug', 0.0), ('Age', 'Age', 3.0)]
textures = ['MapA', 'MapB', 'MapC', 'MapD', 'MapE', 'RiverTex']
d = common_inputs(m, -1600, -400, scalars, textures)
names = ['Pw', 'NvW', 'VC', 'VCA', 'U0', 'U1', 'U2', 'U3', 'Tm', 'SunW', 'RX4', 'RY4', 'RZ4', 'Ctr4', 'SeaR', 'Heat', 'OverOn', 'Dbg', 'Age'] + textures
args = ', '.join(names[:19]) + ', ' + ', '.join('%s, %sSampler' % (t, t) for t in textures)
code = ('struct FSurf {\n' + COMMON + hlsl('land.hlsl') + '\n};\nFSurf S;\nreturn S.Land(' + args +
        ', Rough, Nw, Emis, AO);\n')
c = custom(m, -700, -200, code, names, [('Rough', F1), ('Nw', F3), ('Emis', F3), ('AO', F1)])
for n in names:
    if n == 'VCA':
        link(d['VC'], 'A', c, n)   # vertex colour alpha: 1 = debug colour, 0 = road data in R/G
    else:
        link(d[n], '', c, n)
prop(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
prop(c, 'Rough', unreal.MaterialProperty.MP_ROUGHNESS)
prop(c, 'Nw', unreal.MaterialProperty.MP_NORMAL)
prop(c, 'Emis', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
prop(c, 'AO', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
spec = node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.35)
prop(spec, '', unreal.MaterialProperty.MP_SPECULAR)
finish(m)

# ---------------------------------------------------------------- water (sea and lakes)
m = new_asset('M_PlanetWater', unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property('tangent_space_normal', False)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
textures = ['MapB', 'MapD']
d = common_inputs(m, -1600, -400, [], textures)
names = ['Pw', 'U0', 'U1', 'Tm', 'SunW', 'RX4', 'RY4', 'RZ4', 'Ctr4'] + textures
args = ', '.join(names[:9]) + ', ' + ', '.join('%s, %sSampler' % (t, t) for t in textures)
code = ('struct FSurf {\n' + COMMON + hlsl('water.hlsl') + '\n};\nFSurf S;\nreturn S.Water(' + args +
        ', Rough, Nw, Opac, Opq, AO, Scat, Absorb);\n')
c = custom(m, -700, -200, code, names, [('Rough', F1), ('Nw', F3), ('Opac', F1), ('Opq', F3), ('AO', F1), ('Scat', F3), ('Absorb', F3)])
for n in names:
    link(d[n], '', c, n)
prop(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
prop(c, 'Rough', unreal.MaterialProperty.MP_ROUGHNESS)
prop(c, 'Nw', unreal.MaterialProperty.MP_NORMAL)
prop(c, 'AO', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
prop(c, 'Opac', unreal.MaterialProperty.MP_OPACITY)
spec = node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.25)
prop(spec, '', unreal.MaterialProperty.MP_SPECULAR)
out = node(m, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, -200, 400)
pg = node(m, unreal.MaterialExpressionConstant, -600, 720, r=0.3)
for src, pin, pin_names in ((c, 'Scat', ['ScatteringCoefficients', 'Scattering Coefficients']),
                            (c, 'Absorb', ['AbsorptionCoefficients', 'Absorption Coefficients']),
                            (pg, '', ['PhaseG', 'Phase G'])):
    done = False
    for pn in pin_names:
        if MEL.connect_material_expressions(src, pin, out, pn):
            done = True
            break
    if not done:
        ERRORS.append('SLW pin not found: %s' % pin_names)
        unreal.log_error('SLW pin not found: %s' % pin_names)
finish(m)

# ---------------------------------------------------------------- seabed under deep water (cheap, never seen
# directly: it gives the water a scene depth to integrate its absorption against)
m = new_asset('M_PlanetSeabed', unreal.Material, unreal.MaterialFactoryNew())
bc = node(m, unreal.MaterialExpressionConstant3Vector, -400, 0, constant=unreal.LinearColor(0.10, 0.10, 0.08, 1.0))
prop(bc, '', unreal.MaterialProperty.MP_BASE_COLOR)
rg = node(m, unreal.MaterialExpressionConstant, -400, 200, r=1.0)
prop(rg, '', unreal.MaterialProperty.MP_ROUGHNESS)
finish(m)

unreal.log('planet.py: done, %d errors' % len(ERRORS))
for e in ERRORS:
    unreal.log_error('planet.py: ' + e)
