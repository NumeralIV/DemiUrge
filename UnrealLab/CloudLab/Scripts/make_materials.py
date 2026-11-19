# DEMIURGE graphics lab: create the planet materials.
# UnrealEditor-Cmd.exe PlanetLab.uproject -run=pythonscript -script=<this file>
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/Lab'


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
    MEL.connect_material_expressions(a, a_out, b, b_in)


def srgb_vertex_color(m, x, y):
    vc = node(m, unreal.MaterialExpressionVertexColor, x, y)
    pw = node(m, unreal.MaterialExpressionPower, x + 250, y, const_exponent=2.2)
    link(vc, '', pw, 'Base')
    return pw


def uv_channel(m, x, y, r=False, g=False):
    tc = node(m, unreal.MaterialExpressionTextureCoordinate, x, y)
    mask = node(m, unreal.MaterialExpressionComponentMask, x + 200, y, r=r, g=g, b=False, a=False)
    link(tc, '', mask, '')
    return mask


def night_factor(m, x, y, sharp=6.0, bias=0.1):
    nrm = node(m, unreal.MaterialExpressionVertexNormalWS, x, y)
    ld = node(m, unreal.MaterialExpressionSkyAtmosphereLightDirection, x, y + 120)
    dp = node(m, unreal.MaterialExpressionDotProduct, x + 220, y)
    link(nrm, '', dp, 'A')
    link(ld, '', dp, 'B')
    mul = node(m, unreal.MaterialExpressionMultiply, x + 420, y, const_b=-sharp)
    link(dp, '', mul, 'A')
    add = node(m, unreal.MaterialExpressionAdd, x + 600, y, const_b=bias)
    link(mul, '', add, 'A')
    sat = node(m, unreal.MaterialExpressionSaturate, x + 780, y)
    link(add, '', sat, '')
    return sat


def finish(m):
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    unreal.log('saved ' + m.get_path_name())


# ---- land
m = new_mat('M_PlanetLand')
m.set_editor_property('two_sided', True)
base = srgb_vertex_color(m, -900, -200)
MEL.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
rough = node(m, unreal.MaterialExpressionConstant, -300, 0, r=0.88)
MEL.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
spec = node(m, unreal.MaterialExpressionConstant, -300, 80, r=0.3)
MEL.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
lights = uv_channel(m, -1300, 300, r=True)
night = night_factor(m, -1300, 450)
lm = node(m, unreal.MaterialExpressionMultiply, -300, 300)
link(lights, '', lm, 'A')
link(night, '', lm, 'B')
col = node(m, unreal.MaterialExpressionConstant3Vector, -300, 450, constant=unreal.LinearColor(1.0, 0.56, 0.24, 1.0))
inten = node(m, unreal.MaterialExpressionScalarParameter, -300, 560, parameter_name='CityLights', default_value=3.0)
c1 = node(m, unreal.MaterialExpressionMultiply, -100, 450)
link(col, '', c1, 'A')
link(inten, '', c1, 'B')
em = node(m, unreal.MaterialExpressionMultiply, 100, 300)
link(lm, '', em, 'A')
link(c1, '', em, 'B')
MEL.connect_material_property(em, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(m)

# ---- ocean
m = new_mat('M_Ocean')
m.set_editor_property('two_sided', True)
base = srgb_vertex_color(m, -900, -200)
MEL.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
ice = uv_channel(m, -900, 100, r=True)
rl = node(m, unreal.MaterialExpressionLinearInterpolate, -300, 100, const_a=0.1, const_b=0.75)
link(ice, '', rl, 'Alpha')
MEL.connect_material_property(rl, '', unreal.MaterialProperty.MP_ROUGHNESS)
spec = node(m, unreal.MaterialExpressionConstant, -300, 260, r=0.5)
MEL.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
finish(m)
