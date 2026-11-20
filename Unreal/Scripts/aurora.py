# DEMIURGE aurora (wave 3 'aurora' track): creates /Game/Sky/Aurora/M_Aurora (SPEC-UNREAL 0.3).
#   UnrealEditor-Cmd.exe Demiurge.uproject -run=pythonscript -script=<this file> -unattended -nosplash -nullrhi
# Re-runnable: deletes and recreates the material. The HLSL is Scripts/aurora/aurora.hlsl (a struct with member
# functions pasted into one Custom node). Parameters are set every frame by ADmAurora (Game/Sky/DmSkyAurora.cpp):
#   PC                  the planet's centre in world space (uu)
#   PX, PY, PZ          the planet's axes in world space (the planet turns under a fixed sun)
#   SunDir              direction to the sun, world space
#   PoleN/EN/NN, PoleS/ES/NS   magnetic poles and their tangent bases, planet frame
#   Shape               (sea radius km, curtain base km, slab top km, cos of the cap's angular radius)
#   Anim                (boost 0..1, strength, march steps, activity floor)
import unreal, os

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/Sky/Aurora'   # under /Game/Sky so the packaged build cooks it (DefaultGame.ini DirectoriesToAlwaysCook)
HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'aurora')
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
        unreal.log_error('aurora.py link failed: %s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in))


def try_set(obj, prop, val):
    try:
        obj.set_editor_property(prop, val)
    except Exception as e:  # property names move between engine versions: log, do not fail
        unreal.log_warning('aurora.py: cannot set %s (%s)' % (prop, e))


def vec3(m, name, val, x, y):
    p = node(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
             default_value=unreal.LinearColor(val[0], val[1], val[2], val[3]))
    k = node(m, unreal.MaterialExpressionComponentMask, x + 220, y, r=True, g=True, b=True, a=False)
    link(p, '', k, '')
    return k


def vec4(m, name, val, x, y):
    return node(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                default_value=unreal.LinearColor(val[0], val[1], val[2], val[3]))


with open(os.path.join(HERE, 'aurora.hlsl')) as f:
    BODY = f.read()

IN3 = ['WP', 'Cam', 'PC', 'PX', 'PY', 'PZ', 'SunDir', 'PoleN', 'EN', 'NN', 'PoleS', 'ES', 'NS']
IN4 = ['Shape', 'Anim']
IN1 = ['T']
ARGS = IN3 + IN4 + IN1
code = 'struct FAur {\n' + BODY + '\n};\nFAur F;\nreturn F.Shade(Parameters, ' + ', '.join(ARGS) + ');\n'

m = new_mat('M_Aurora')
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('two_sided', True)
try_set(m, 'use_translucency_vertex_fog', False)   # "Apply Fogging": the march does its own falloff
try_set(m, 'apply_cloud_fogging', False)
try_set(m, 'used_with_static_lighting', False)

cu = node(m, unreal.MaterialExpressionCustom, -400, 0)
cu.set_editor_property('code', code)
cu.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
cu.set_editor_property('description', 'Aurora march')
ins = []
for nm in ARGS:
    ci = unreal.CustomInput()
    ci.set_editor_property('input_name', nm)
    ins.append(ci)
cu.set_editor_property('inputs', ins)

y = -800
link(node(m, unreal.MaterialExpressionWorldPosition, -900, y), '', cu, 'WP'); y += 100
link(node(m, unreal.MaterialExpressionCameraPositionWS, -900, y), '', cu, 'Cam'); y += 100
defaults = {
    'PC': (0, 0, 0, 0), 'PX': (1, 0, 0, 0), 'PY': (0, 1, 0, 0), 'PZ': (0, 0, 1, 0), 'SunDir': (1, 0, 0, 0),
    'PoleN': (0, 0, 1, 0), 'EN': (1, 0, 0, 0), 'NN': (0, 1, 0, 0),
    'PoleS': (0, 0, -1, 0), 'ES': (1, 0, 0, 0), 'NS': (0, -1, 0, 0),
}
for nm in IN3[2:]:
    link(vec3(m, nm, defaults[nm], -1200, y), '', cu, nm); y += 100
link(vec4(m, 'Shape', (100.0, 3.0, 14.0, 0.64), -1000, y), 'RGBA', cu, 'Shape'); y += 100
link(vec4(m, 'Anim', (0.0, 0.0, 40.0, 0.0), -1000, y), 'RGBA', cu, 'Anim'); y += 100
link(node(m, unreal.MaterialExpressionTime, -900, y), '', cu, 'T'); y += 100

if not MEL.connect_material_property(cu, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    FAILS.append('emissive')
MEL.recompile_material(m)
EAL.save_loaded_asset(m)

if FAILS:
    unreal.log_error('aurora.py: %d links failed: %s' % (len(FAILS), ', '.join(FAILS)))
else:
    unreal.log('aurora.py: saved %s, no failed links' % m.get_path_name())
