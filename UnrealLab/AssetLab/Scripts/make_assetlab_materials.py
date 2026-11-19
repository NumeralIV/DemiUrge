# AssetLab-only materials: M_LabWater (a calm shallow sea surface for the drowned-state shots).
#   UnrealEditor-Cmd.exe PlanetLab.uproject -run=pythonscript -script=Scripts/make_assetlab_materials.py
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
path, name = '/Game/AssetLab', 'M_LabWater'
if EAL.does_asset_exist(path + '/' + name):
    m = unreal.load_asset(path + '/' + name)
    MEL.delete_all_material_expressions(m)
else:
    m = AT.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
m.set_editor_property('two_sided', False)


def n(cls, **kw):
    e = MEL.create_material_expression(m, cls, -600, 0)
    for k, v in kw.items():
        e.set_editor_property(k, v)
    return e


col = n(unreal.MaterialExpressionVectorParameter, parameter_name='Color', default_value=unreal.LinearColor(0.11, 0.2, 0.17, 1))
MEL.connect_material_property(col, '', MP.MP_BASE_COLOR)
r = n(unreal.MaterialExpressionConstant, r=0.04)
MEL.connect_material_property(r, '', MP.MP_ROUGHNESS)
sp = n(unreal.MaterialExpressionConstant, r=0.6)
MEL.connect_material_property(sp, '', MP.MP_SPECULAR)
# opacity: more opaque at grazing angles (fresnel) and with depth below the surface
fr = n(unreal.MaterialExpressionFresnel, exponent=4.0, base_reflect_fraction=0.35)
df = n(unreal.MaterialExpressionDepthFade, fade_distance_default=220.0)
cu = n(unreal.MaterialExpressionCustom, code='return saturate(0.35 + 0.45 * D + 0.4 * F);', output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
ins = []
for nm in ('D', 'F'):
    ci = unreal.CustomInput()
    ci.set_editor_property('input_name', nm)
    ins.append(ci)
cu.set_editor_property('inputs', ins)
MEL.connect_material_expressions(df, '', cu, 'D')
MEL.connect_material_expressions(fr, '', cu, 'F')
MEL.connect_material_property(cu, '', MP.MP_OPACITY)
MEL.layout_material_expressions(m)
MEL.recompile_material(m)
EAL.save_loaded_asset(m)
unreal.log('ASSETLABMAT saved ' + m.get_path_name())
