# DEMIURGE fx (C6): the particle materials. Re-runnable (deletes and recreates).
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nosplash
# Writes /Game/FX/M_Fx: unlit, two-sided, AlphaComposite (premultiplied: alpha 0 = additive, so glow
# and smoke share one sorted draw), soft depth fade scaled by each quad's size; and M_FxOverlay, the
# same without depth test, for flash halos (light scattered in the air in front of the terrain). The
# shading of every shape lives in Scripts/fx/fx.hlsl (one Custom node); Game/FX/DmFxRender.cpp writes
# the per-vertex data it reads. Scalar parameter Gain scales everything (exposure trim).
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/FX'
HERE = os.path.dirname(os.path.abspath(__file__))
FAILS = []


def hlsl(name):
    with open(os.path.join(HERE, 'fx', name), 'r', encoding='utf-8') as f:
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
        FAILS.append('%s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in))
        unreal.log_error('fx.py link failed: %s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in))
    return ok


def prop(m, a, a_out, p):
    ok = MEL.connect_material_property(a, a_out, p)
    if not ok:
        FAILS.append('%s.%s -> %s' % (a.get_name(), a_out, p))
        unreal.log_error('fx.py property link failed: %s.%s -> %s' % (a.get_name(), a_out, p))


def mask(m, src, x, y, r=False, g=False, b=False, a=False):
    k = node(m, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
    link(src, '', k, '')
    return k


def make(name, overlay):
    m = new_mat(name)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ALPHA_COMPOSITE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    # both variants skip the hardware depth test: M_Fx occludes itself in the shader (see below),
    # M_FxOverlay (flash halos) is never occluded
    m.set_editor_property('disable_depth_test', True)
    # No responsive AA (it stamps the whole quad, alpha 0 included, into TSR's stencil).
    for k, v in (('enable_responsive_aa', False), ('translucency_pass', unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF),
                 ('apply_cloud_fogging', False)):
        try:
            m.set_editor_property(k, v)
        except Exception as e:  # property names differ between versions: not fatal
            unreal.log_warning('fx.py: %s not set (%s)' % (k, e))

    uv = [node(m, unreal.MaterialExpressionTextureCoordinate, -1500, -300 + 90 * i, coordinate_index=i) for i in range(4)]
    vc = node(m, unreal.MaterialExpressionVertexColor, -1700, 100)
    vca = node(m, unreal.MaterialExpressionAppendVector, -1500, 100)
    link(vc, '', vca, 'A')
    link(vc, 'A', vca, 'B')
    tm = node(m, unreal.MaterialExpressionTime, -1500, 220)

    code = 'struct FFx {\n' + hlsl('fx.hlsl') + '\n};\nFFx F;\nreturn F.Shade(U0, U1, U2, U3, VC, Tm);\n'
    c = node(m, unreal.MaterialExpressionCustom, -1100, -100)
    c.set_editor_property('code', code)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    c.set_editor_property('description', 'FxShade')
    ins = []
    for n in ('U0', 'U1', 'U2', 'U3', 'VC', 'Tm'):
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    c.set_editor_property('inputs', ins)
    for i in range(4):
        link(uv[i], '', c, 'U%d' % i)
    link(vca, '', c, 'VC')
    link(tm, '', c, 'Tm')

    gain = node(m, unreal.MaterialExpressionScalarParameter, -900, 250, parameter_name='Gain', default_value=1.0)
    rgb = mask(m, c, -800, -100, r=True, g=True, b=True)
    al = mask(m, c, -800, 100, a=True)
    if overlay:
        k1 = rgb
        k3 = al
    else:
        # Occlusion in the shader instead of the hardware depth test, which hid ground effects that were
        # not far enough above the terrain. fade = saturate((SceneDepth - PixelDepth + Tolerance) /
        # FadeDistance): a quad fades out over U1.y km (the renderer writes 0.6 x the quad's half size, or
        # an explicit softness; km because UVs may be half precision) once it is more than Tolerance
        # behind the scene.
        size = mask(m, uv[1], -1300, 400, g=True)
        fd = node(m, unreal.MaterialExpressionMultiply, -1100, 400, const_b=100000.0)
        link(size, '', fd, 'A')
        sd = node(m, unreal.MaterialExpressionSceneDepth, -1300, 520)
        sdr = mask(m, sd, -1100, 520, r=True)
        pd = node(m, unreal.MaterialExpressionPixelDepth, -1300, 620)
        # tolerance = depth x (Bias + depth x BiasQ): the measured error grows with depth squared
        # (0.2-0.5 km at 15 km, 1-3 km at 60 km, > 13 km at 160 km). Far-side culling is done on the CPU.
        bias = node(m, unreal.MaterialExpressionScalarParameter, -1300, 700, parameter_name='OccludeBias', default_value=0.03)
        biasq = node(m, unreal.MaterialExpressionScalarParameter, -1300, 780, parameter_name='OccludeBiasQ', default_value=1.2e-8)
        pq = node(m, unreal.MaterialExpressionMultiply, -1100, 760)
        link(pd, '', pq, 'A')
        link(biasq, '', pq, 'B')
        tol = node(m, unreal.MaterialExpressionAdd, -950, 740)
        link(pq, '', tol, 'A')
        link(bias, '', tol, 'B')
        tolu = node(m, unreal.MaterialExpressionMultiply, -800, 700)
        link(pd, '', tolu, 'A')
        link(tol, '', tolu, 'B')
        d0 = node(m, unreal.MaterialExpressionSubtract, -800, 560)
        link(sdr, '', d0, 'A')
        link(pd, '', d0, 'B')
        diff = node(m, unreal.MaterialExpressionAdd, -650, 600)
        link(d0, '', diff, 'A')
        link(tolu, '', diff, 'B')
        dv = node(m, unreal.MaterialExpressionDivide, -650, 500)
        link(diff, '', dv, 'A')
        link(fd, '', dv, 'B')
        df = node(m, unreal.MaterialExpressionSaturate, -500, 500)
        link(dv, '', df, '')
        k1 = node(m, unreal.MaterialExpressionMultiply, -600, -100)
        link(rgb, '', k1, 'A')
        link(df, '', k1, 'B')
        k3 = node(m, unreal.MaterialExpressionMultiply, -600, 100)
        link(al, '', k3, 'A')
        link(df, '', k3, 'B')
    k2 = node(m, unreal.MaterialExpressionMultiply, -400, -100)
    link(k1, '', k2, 'A')
    link(gain, '', k2, 'B')
    prop(m, k2, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    prop(m, k3, '', unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    unreal.log('fx.py saved %s (%d link failures so far)' % (m.get_path_name(), len(FAILS)))


make('M_Fx', False)
make('M_FxOverlay', True)
for f in FAILS:
    unreal.log_error('fx.py FAILED ' + f)
