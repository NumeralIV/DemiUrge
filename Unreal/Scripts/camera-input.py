# DEMIURGE camera-input (C3): materials of the brush ring (Game/Input/DmBrushRing).
# Re-runnable (deletes and recreates). Writes /Game/CameraInput/M_BrushRing and M_BrushRingXRay:
# unlit translucent, emissive = Color x Glow x vertex R (0 = dark contrast band), opacity = vertex alpha x a soft cross-ribbon
# profile (UV0.y 0..1 across the ribbon) x Opacity. The XRay twin ignores depth, so the part of the ring
# hidden behind relief still shows faintly.
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nosplash
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/CameraInput'


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


def try_set(m, prop, value):
    try:
        m.set_editor_property(prop, value)
    except Exception as ex:  # property names move between engine versions: never fail the script on one
        unreal.log_warning('camera-input.py: %s not set (%s)' % (prop, ex))


def ring_material(name, xray):
    m = new_mat(name)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    try_set(m, 'disable_depth_test', xray)
    try_set(m, 'enable_responsive_aa', True)      # thin bright lines: keep TSR from smearing them
    try_set(m, 'use_translucency_vertex_fog', False)
    try_set(m, 'translucency_pass', unreal.MaterialTranslucencyPass.MTP_AFTER_DOF)

    # RGB comes from the Color parameter (vertex colours are stored as 8-bit sRGB and read back raw, which
    # washes saturated colours out); vertex R is a 0..1 mask (0 = the dark contrast band under the ring)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 0)
    col = node(m, unreal.MaterialExpressionVectorParameter, -900, -200, parameter_name='Color',
               default_value=unreal.LinearColor(1.0, 0.8, 0.3, 1.0))
    glow = node(m, unreal.MaterialExpressionScalarParameter, -900, 200, parameter_name='Glow', default_value=1.0)
    cg = node(m, unreal.MaterialExpressionMultiply, -650, -100)
    link(col, '', cg, 'A')
    link(glow, '', cg, 'B')
    em = node(m, unreal.MaterialExpressionMultiply, -500, 0)
    link(cg, '', em, 'A')
    link(vc, 'R', em, 'B')
    MEL.connect_material_property(em, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # soft profile across the ribbon: (1 - |2v - 1|) ^ 1.3
    tc = node(m, unreal.MaterialExpressionTextureCoordinate, -1300, 400)
    v = node(m, unreal.MaterialExpressionComponentMask, -1100, 400, r=False, g=True, b=False, a=False)
    link(tc, '', v, '')
    two = node(m, unreal.MaterialExpressionMultiply, -950, 400, const_b=2.0)
    link(v, '', two, 'A')
    sub = node(m, unreal.MaterialExpressionSubtract, -800, 400, const_b=1.0)
    link(two, '', sub, 'A')
    ab = node(m, unreal.MaterialExpressionAbs, -650, 400)
    link(sub, '', ab, '')
    om = node(m, unreal.MaterialExpressionOneMinus, -520, 400)
    link(ab, '', om, '')
    pw = node(m, unreal.MaterialExpressionPower, -380, 400, const_exponent=1.3)
    link(om, '', pw, 'Base')

    op = node(m, unreal.MaterialExpressionScalarParameter, -380, 560, parameter_name='Opacity', default_value=0.3 if xray else 1.0)
    a1 = node(m, unreal.MaterialExpressionMultiply, -200, 400)
    link(pw, '', a1, 'A')
    link(vc, 'A', a1, 'B')
    a2 = node(m, unreal.MaterialExpressionMultiply, -60, 400)
    link(a1, '', a2, 'A')
    link(op, '', a2, 'B')
    MEL.connect_material_property(a2, '', unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    unreal.log('camera-input.py: saved ' + m.get_path_name())


ring_material('M_BrushRing', False)
ring_material('M_BrushRingXRay', True)
unreal.log('camera-input.py: done')
