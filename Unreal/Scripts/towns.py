# DEMIURGE C5 towns: material graphs for generated buildings, windows, light pools and the ground overlay.
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nosplash
# Re-runnable: existing assets are reused and their graphs rebuilt from scratch (deleting a referenced
# package fails unattended). Writes /Game/Towns/M_TownBody, M_TownGlass, M_TownGlow, M_TownGround.
# The shading lives in HLSL strings pasted into Custom nodes (one struct with member functions).
#
# Vertex / instance conventions (Game/Towns/DmTownsMesh.h, DmTownsKit.h):
#   body   VC.rgb sRGB albedo, VC.a tint channel (0 wall tint, 128 roof tint, 255 fixed); UV0 metric; UV1.x
#          surface pattern id, UV1.y height above the floor in metres; custom data 0..2 wall tint, 3..5 roof
#          tint, 6 lit fraction, 7 state (1 = burnt / ruined)
#   glass  VC.rgb lit colour, VC.a per-window random; lit when frac(a * 7.131 + PerInstanceRandom * 13.73) < lit
#   glow   VC.rgb additive colour, VC.a 255 = always on (flames, flickering), 0 = night only
#   ground VC.rgb albedo, VC.a street lamp level; UV1.x kind (0 field, 1 dirt / cobble road, 2 asphalt,
#          3 hedge, 4 plaza / quay), UV1.y field row angle or lamp spacing (m); UV0 = field metres or
#          (metres along, -1..1 across) on streets
# Shared parameters: PlanetCenter, SunDir (world, set every frame by ADmTowns), NightOverride (< 0: from the
# sun), Glow (intensity), DepthBias (ground overlay pulled toward the camera, fraction of the distance).
# M_TownGround is masked: fields and hedges dither out between FieldFadeNear and FieldFadeFar (uu, camera distance)
# and at night, so the planet's own farmland takes over from altitude (verifier change 2026-11-19).
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/Towns'
MP = unreal.MaterialProperty
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
ERRORS = []


def log(msg):
    unreal.log('TOWNMAT ' + msg)


def new_mat(name):
    path = DIR + '/' + name
    m = None
    if EAL.does_asset_exist(path):
        m = unreal.load_asset(path)
        if m:
            MEL.delete_all_material_expressions(m)
    if not m:
        m = AT.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    return m


class G:
    def __init__(self, m):
        self.m = m
        self.x = -2000
        self.y = -400

    def n(self, cls, **props):
        self.y += 110
        if self.y > 1600:
            self.y = -400
            self.x += 300
        e = MEL.create_material_expression(self.m, cls, self.x, self.y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(self, a, a_out, b, b_in):
        if not MEL.connect_material_expressions(a, a_out, b, b_in):
            ERRORS.append('%s: link %s.%s -> %s.%s' % (self.m.get_name(), a.get_name(), a_out, b.get_name(), b_in))

    def out(self, e, pin, prop):
        if not MEL.connect_material_property(e, pin, prop):
            ERRORS.append('%s: output %s -> %s' % (self.m.get_name(), pin, prop))

    def scalar(self, name, v):
        return self.n(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(v))

    def vparam(self, name, r, g, b, a=0.0):
        return self.n(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(r, g, b, a))

    def custom(self, code, inputs, out_type, outputs=()):
        c = self.n(unreal.MaterialExpressionCustom)
        c.set_editor_property('code', code)
        c.set_editor_property('output_type', out_type)
        ins = []
        for name, _src, _pin in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property('input_name', name)
            ins.append(ci)
        c.set_editor_property('inputs', ins)
        outs = []
        for name, t in outputs:
            co = unreal.CustomOutput()
            co.set_editor_property('output_name', name)
            co.set_editor_property('output_type', t)
            outs.append(co)
        c.set_editor_property('additional_outputs', outs)
        for name, src, pin in inputs:
            self.link(src, pin, c, name)
        return c

    def wpos(self):
        return self.n(unreal.MaterialExpressionWorldPosition)

    def pir(self):
        return self.n(unreal.MaterialExpressionPerInstanceRandom)

    def cdata(self, i, default=0.0):
        e = self.n(unreal.MaterialExpressionPerInstanceCustomData, data_index=i)
        try:
            e.set_editor_property('const_default_value', float(default))
        except Exception:
            pass
        return e

    def uv(self, i):
        return self.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=i)

    def vcol(self):
        return self.n(unreal.MaterialExpressionVertexColor)

    def time(self):
        return self.n(unreal.MaterialExpressionTime)

    def vi(self, src, pin=''):
        # PerInstanceCustomData / PerInstanceRandom read 0 in the pixel shader of instanced (non-Nanite) meshes
        # here: evaluate them in the vertex shader and interpolate
        v = self.n(unreal.MaterialExpressionVertexInterpolator)
        self.link(src, pin, v, 'VS')
        return v

    def inst(self):
        # (wall tint, roof tint, (lit, state, random)); read in the pixel shader through the GPU scene. The
        # component only uploads custom data if its material was compiled when it registered (ADmTowns rebuilds
        # its components once the shader compiler is idle, for editor runs right after a material change)
        cd = [self.cdata(i, 1.0 if i < 6 else (0.4 if i == 6 else 0.0)) for i in range(8)]
        p = self.pir()
        w = self.append3(cd[0], cd[1], cd[2])
        r = self.append3(cd[3], cd[4], cd[5])
        l = self.append3(cd[6], cd[7], p)
        return w, r, l

    def append3(self, a, b, c):
        ab = self.n(unreal.MaterialExpressionAppendVector)
        self.link(a, '', ab, 'A')
        self.link(b, '', ab, 'B')
        abc = self.n(unreal.MaterialExpressionAppendVector)
        self.link(ab, '', abc, 'A')
        self.link(c, '', abc, 'B')
        return abc


def finish(m):
    MEL.layout_material_expressions(m)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log('saved ' + m.get_path_name())


LIB = r'''
float H21(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }
float VN(float2 p)
{
    float2 i = floor(p), f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(H21(i), H21(i + float2(1, 0)), u.x), lerp(H21(i + float2(0, 1)), H21(i + float2(1, 1)), u.x), u.y);
}
float Fbm(float2 p) { float s = 0.0, a = 0.5; for (int o = 0; o < 3; o++) { s += a * VN(p); p = p * 2.03 + 17.1; a *= 0.5; } return s / 0.875; }
float Fade(float2 uv, float period)
{
    // patterns smaller than ~2 pixels fade out instead of aliasing (a house is a few pixels at the game's zoom)
    float fw = max(length(ddx(uv)), length(ddy(uv)));
    return saturate(1.5 - 2.0 * fw / period);
}
float Night(float3 wp, float3 ctr, float3 sun, float ov)
{
    float3 up = normalize(wp - ctr);
    return ov >= 0.0 ? saturate(ov) : saturate((0.05 - dot(up, normalize(sun))) * 6.0);
}
'''

BODY = r'''
float3 Body(float3 vrgb, float va, float2 uv0, float2 uv1, float3 wallT, float3 roofT, float state, float pir, out float rough, out float spec, out float ao)
{
    float wallW = saturate(1.0 - va * 4.0);
    float roofW = saturate(1.0 - abs(va - 0.502) * 4.0);
    float3 base = pow(max(vrgb, 0.0), 2.2) * lerp(float3(1, 1, 1), wallT, wallW) * lerp(float3(1, 1, 1), roofT, roofW);
    int p = (int)round(uv1.x);
    float v = 0.0; rough = 0.82; spec = 0.35;
    float big = Fbm(uv0 * 0.35 + pir * 31.0) - 0.5;
    if (p == 0 || p == 12) { v = 0.10 * big; rough = 0.85; }
    else if (p == 1)
    {
        float2 c = float2(uv0.x / 0.9 + 0.5 * floor(uv0.y / 0.45), uv0.y / 0.45);
        float h = H21(floor(c)) - 0.5;
        float m = 1.0 - smoothstep(0.0, 0.08, min(frac(c.x), frac(c.y)));
        v = (0.16 * h - 0.18 * m) * Fade(uv0, 0.45) + 0.10 * big; rough = 0.9;
    }
    else if (p == 2) { float row = floor(uv0.y / 0.3); float h = H21(float2(row, floor(uv0.x / 0.6 + 0.5 * row))) - 0.5; v = 0.14 * h * Fade(uv0, 0.3) + 0.08 * big; rough = 0.88; }
    else if (p == 3) { float h = H21(float2(floor(uv0.x / 0.22), 3.1)) - 0.5; v = 0.18 * h * Fade(uv0, 0.22) + 0.10 * big; rough = 0.8; }
    else if (p == 4) { float pan = 1.0 - smoothstep(0.0, 0.03, min(frac(uv0.x / 3.0), frac(uv0.y / 3.0))); v = -0.12 * pan * Fade(uv0, 3.0) + 0.06 * big; rough = 0.9; }
    else if (p == 5) { float s = VN(float2(uv0.x * 3.0, uv0.y * 0.4)) - 0.5; v = 0.18 * s * Fade(uv0, 0.33) + 0.14 * big; rough = 0.97; spec = 0.2; }
    else if (p == 6)
    {
        float rr = frac(uv0.y / 0.34);
        float h = H21(float2(floor(uv0.x / 0.3 + 0.5 * floor(uv0.y / 0.34)), floor(uv0.y / 0.34))) - 0.5;
        v = ((rr - 0.5) * 0.22 + 0.12 * h) * Fade(uv0, 0.34) + 0.10 * big; rough = 0.7; spec = 0.4;
    }
    else if (p == 7) { float rr = frac(uv0.y / 0.25); v = ((rr - 0.5) * 0.12 + 0.08 * (H21(floor(uv0 / float2(0.4, 0.25))) - 0.5)) * Fade(uv0, 0.25) + 0.06 * big; rough = 0.6; spec = 0.45; }
    else if (p == 8) { float s = 1.0 - smoothstep(0.0, 0.05, abs(frac(uv0.x / 0.6) - 0.5) * 2.0 - 0.9); v = 0.10 * s * Fade(uv0, 0.6) + 0.05 * big; rough = 0.45; spec = 0.55; }
    else if (p == 9)
    {
        float g = 1.0 - smoothstep(0.0, 0.05, min(frac(uv0.x / 1.5), frac(uv0.y / 3.6)));
        base = lerp(base * 0.35, base * 0.9, g * Fade(uv0, 1.5)); v = 0.04 * big; rough = 0.12; spec = 0.7;
    }
    else if (p == 10) { v = 0.12 * big; rough = 0.95; spec = 0.2; }
    else if (p == 11) { v = 0.16 * big + 0.05 * (VN(uv0 * 1.3) - 0.5); rough = 0.95; spec = 0.25; }
    base *= saturate(1.0 + v);
    // foot of the walls: earth splash and contact occlusion (UV1.y = metres above the floor)
    float foot = saturate((uv1.y + 0.2) / 1.4);
    ao = lerp(0.5, 1.0, foot);
    base *= lerp(0.7, 1.0, foot);
    // burnt / ruined
    base = lerp(base, base * 0.22 + float3(0.025, 0.022, 0.02), saturate(state) * 0.85);
    rough = lerp(rough, 0.95, saturate(state));
    return min(base, float3(0.85, 0.85, 0.85));
}
'''

GLASS = r'''
float3 Glass(float3 vrgb, float va, float lit, float state, float pir, float3 wp, float3 ctr, float3 sun, float ov, float glow, out float3 emis, out float rough)
{
    float night = Night(wp, ctr, sun, ov);
    float r = frac(va * 7.131 + pir * 13.73);
    float on = (r < lit) ? 1.0 : 0.0;
    emis = pow(max(vrgb, 0.0), 2.2) * on * night * glow * (1.0 - saturate(state));
    rough = 0.12;
    return float3(0.016, 0.019, 0.024);
}
'''

GLOW = r'''
float3 Glow(float3 vrgb, float va, float state, float pir, float t, float3 wp, float3 ctr, float3 sun, float ov, float glow)
{
    float night = Night(wp, ctr, sun, ov);
    float flick = 0.82 + 0.12 * sin(t * 9.0 + pir * 40.0) + 0.06 * sin(t * 23.0 + pir * 17.0);
    float k = va > 0.5 ? flick * (0.35 + 0.65 * night) : night;
    return pow(max(vrgb, 0.0), 2.2) * k * glow * (1.0 - saturate(state));
}
'''

GROUND = r'''
float3 Ground(float3 vrgb, float va, float2 uv0, float2 uv1, float3 wp, float3 ctr, float3 sun, float ov, float glow, float3 cam, float fadeA, float fadeB, float2 spos, float fi, out float rough, out float3 emis, out float mask)
{
    int k = (int)round(uv1.x);
    float3 base = pow(max(vrgb, 0.0), 2.2);
    float night = Night(wp, ctr, sun, ov);
    rough = 0.9; emis = float3(0, 0, 0); mask = 1.0;
    // fields and hedges hand over to the planet's own farmland with distance (dithered, resolved by TSR): drawn
    // from orbit-scale views as well they read as bright confetti on top of it (verifier fix)
    if (k == 0 || k == 3)
    {
        // ... and with the night: unlit crops are invisible anyway, while an opaque field hid the planet's night
        // ground under it as a black slab
        float fade = saturate((fadeB - length(wp - cam)) / max(fadeB - fadeA, 1.0)) * (1.0 - night);
        mask = fade > H21(spos + fi * float2(17.13, 31.71)) ? 1.0 : 0.0;
    }
    if (k == 0)
    {
        float a = uv1.y;
        float d = dot(uv0, float2(cos(a), sin(a)));
        float rows = sin(d * 6.2832 / 1.8) * 0.5 + 0.5;
        float n = Fbm(uv0 * 0.05) - 0.5;
        base *= 1.0 + (rows - 0.5) * 0.24 * Fade(uv0, 1.8) + 0.18 * n + 0.08 * (VN(uv0 * 0.3) - 0.5);
    }
    else if (k == 1 || k == 2)
    {
        float across = abs(uv0.y);
        base *= 1.0 + 0.12 * (VN(uv0 * float2(0.4, 2.0)) - 0.5) - 0.14 * smoothstep(0.7, 1.0, across);
        if (k == 2)
        {
            float dash = step(across, 0.06) * step(frac(uv0.x / 9.0), 0.5);
            base = lerp(base, float3(0.45, 0.45, 0.4), dash * 0.6 * Fade(uv0, 1.0));
            rough = 0.75;
        }
        float s = max(uv1.y, 1.0);
        float ph = frac(uv0.x / s) - 0.5;
        float pool = exp(-ph * ph * s * s / 40.0) * (1.0 - 0.5 * across * across);
        // pools under the lamps with only a faint wash between them (a continuous 10% floor made every street a
        // glowing tube at night; verifier fix)
        emis = float3(1.0, 0.6, 0.28) * (0.025 + pool) * va * night * glow;
    }
    else if (k == 3) { base *= 1.0 + 0.35 * (VN(uv0 * 0.5) - 0.5); rough = 0.95; }
    else
    {
        base *= 1.0 + 0.10 * (Fbm(uv0 * 0.2) - 0.5) + 0.06 * (VN(uv0 * 2.0) - 0.5);
        rough = 0.8;
        // plazas and quays: lamps stand along the edge (uv0.y: 0 centre .. 1 rim, -1..1 across a quay); a uniform
        // glow read as a lit disc from above (verifier fix)
        float rim = smoothstep(0.55, 1.0, abs(uv0.y));
        emis = float3(1.0, 0.65, 0.32) * (0.15 + 0.45 * rim) * va * night * glow;
    }
    return min(base, float3(0.85, 0.85, 0.85));
}
'''

BIAS = r'''
float3 d = Cam - WP; float L = length(d);
return d / max(L, 1.0) * min(L * Bias, 3000.0);
'''


def lib(body, call):
    return 'struct FTown {\n' + LIB + body + '\n};\nFTown T;\n' + call


# ---------------------------------------------------------------- body
m = new_mat('M_TownBody')
m.set_editor_property('used_with_instanced_static_meshes', True)
m.set_editor_property('used_with_static_lighting', True)
g = G(m)
vc = g.vcol()
vca = g.custom('return Parameters.VertexColor.a;', [], F1)
uv0 = g.uv(0)
uv1 = g.uv(1)
wall, roof, ls = g.inst()
body = g.custom(lib(BODY, 'return T.Body(VC, VA, UV0, UV1, WallT, RoofT, LS.y, LS.z, Rough, Spec, AO);'),
                [('VC', vc, ''), ('VA', vca, ''), ('UV0', uv0, ''), ('UV1', uv1, ''), ('WallT', wall, ''), ('RoofT', roof, ''), ('LS', ls, '')],
                F3, [('Rough', F1), ('Spec', F1), ('AO', F1)])
g.out(body, '', MP.MP_BASE_COLOR)
g.out(body, 'Rough', MP.MP_ROUGHNESS)
g.out(body, 'Spec', MP.MP_SPECULAR)
g.out(body, 'AO', MP.MP_AMBIENT_OCCLUSION)
finish(m)

# ---------------------------------------------------------------- glass (windows, lamps)
m = new_mat('M_TownGlass')
m.set_editor_property('used_with_instanced_static_meshes', True)
m.set_editor_property('used_with_static_lighting', True)
g = G(m)
vc = g.vcol()
vca = g.custom('return Parameters.VertexColor.a;', [], F1)
_w, _r, ls = g.inst()
wp = g.wpos()
ctr = g.vparam('PlanetCenter', 0, 0, 0)
sun = g.vparam('SunDir', 1, 0, 0)
ov = g.scalar('NightOverride', -1.0)
glow = g.scalar('Glow', 3.0)
gl = g.custom(lib(GLASS, 'return T.Glass(VC, VA, LS.x, LS.y, LS.z, WP, Ctr, Sun, Ov, GlowK, Emis, Rough);'),
              [('VC', vc, ''), ('VA', vca, ''), ('LS', ls, ''), ('WP', wp, ''), ('Ctr', ctr, ''), ('Sun', sun, ''), ('Ov', ov, ''), ('GlowK', glow, '')],
              F3, [('Emis', F3), ('Rough', F1)])
g.out(gl, '', MP.MP_BASE_COLOR)
g.out(gl, 'Emis', MP.MP_EMISSIVE_COLOR)
g.out(gl, 'Rough', MP.MP_ROUGHNESS)
spec = g.n(unreal.MaterialExpressionConstant, r=0.6)
g.out(spec, '', MP.MP_SPECULAR)
finish(m)

# ---------------------------------------------------------------- glow (additive light pools, flames)
m = new_mat('M_TownGlow')
m.set_editor_property('used_with_instanced_static_meshes', True)
m.set_editor_property('used_with_static_lighting', True)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
g = G(m)
vc = g.vcol()
vca = g.custom('return Parameters.VertexColor.a;', [], F1)
_w, _r, ls = g.inst()
tm = g.time()
wp = g.wpos()
ctr = g.vparam('PlanetCenter', 0, 0, 0)
sun = g.vparam('SunDir', 1, 0, 0)
ov = g.scalar('NightOverride', -1.0)
glow = g.scalar('Glow', 1.6)
gw = g.custom(lib(GLOW, 'return T.Glow(VC, VA, LS.y, LS.z, Tm, WP, Ctr, Sun, Ov, GlowK);'),
              [('VC', vc, ''), ('VA', vca, ''), ('LS', ls, ''), ('Tm', tm, ''), ('WP', wp, ''), ('Ctr', ctr, ''), ('Sun', sun, ''), ('Ov', ov, ''), ('GlowK', glow, '')], F3)
g.out(gw, '', MP.MP_EMISSIVE_COLOR)
finish(m)

# ---------------------------------------------------------------- ground overlay (fields, streets, plazas)
m = new_mat('M_TownGround')
m.set_editor_property('used_with_static_lighting', True)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
m.set_editor_property('opacity_mask_clip_value', 0.5)
g = G(m)
vc = g.vcol()
vca = g.custom('return Parameters.VertexColor.a;', [], F1)
uv0 = g.uv(0)
uv1 = g.uv(1)
wp = g.wpos()
ctr = g.vparam('PlanetCenter', 0, 0, 0)
sun = g.vparam('SunDir', 1, 0, 0)
ov = g.scalar('NightOverride', -1.0)
glow = g.scalar('Glow', 1.4)
camf = g.n(unreal.MaterialExpressionCameraPositionWS)
fa = g.scalar('FieldFadeNear', 700000.0)
fb = g.scalar('FieldFadeFar', 1200000.0)
gr = g.custom(lib(GROUND, 'return T.Ground(VC, VA, UV0, UV1, WP, Ctr, Sun, Ov, GlowK, Cam, FadeA, FadeB, Parameters.SvPosition.xy, (float)View.StateFrameIndexMod8, Rough, Emis, Mask);'),
              [('VC', vc, ''), ('VA', vca, ''), ('UV0', uv0, ''), ('UV1', uv1, ''), ('WP', wp, ''), ('Ctr', ctr, ''), ('Sun', sun, ''), ('Ov', ov, ''), ('GlowK', glow, ''),
               ('Cam', camf, ''), ('FadeA', fa, ''), ('FadeB', fb, '')],
              F3, [('Rough', F1), ('Emis', F3), ('Mask', F1)])
g.out(gr, 'Mask', MP.MP_OPACITY_MASK)
g.out(gr, '', MP.MP_BASE_COLOR)
g.out(gr, 'Rough', MP.MP_ROUGHNESS)
g.out(gr, 'Emis', MP.MP_EMISSIVE_COLOR)
spec = g.n(unreal.MaterialExpressionConstant, r=0.3)
g.out(spec, '', MP.MP_SPECULAR)
cam = g.n(unreal.MaterialExpressionCameraPositionWS)
wp2 = g.wpos()
bias = g.scalar('DepthBias', 0.0008)
wpo = g.custom(BIAS, [('Cam', cam, ''), ('WP', wp2, ''), ('Bias', bias, '')], F3)
g.out(wpo, '', MP.MP_WORLD_POSITION_OFFSET)
finish(m)

if ERRORS:
    for e in ERRORS:
        unreal.log_error('TOWNMAT ERROR ' + e)
else:
    log('all materials built without link errors')

# ---------------------------------------------------------------- debug: per-instance custom data probe (towns-cdtest)
m = new_mat('M_TownDebug')
m.set_editor_property('used_with_instanced_static_meshes', True)
g = G(m)
c0 = g.cdata(0, 1.0)
c1 = g.cdata(1, 0.0)
c2 = g.cdata(2, 1.0)
dbg = g.append3(c0, c1, c2)
g.out(dbg, '', MP.MP_EMISSIVE_COLOR)
z = g.n(unreal.MaterialExpressionConstant, r=0.0)
g.out(z, '', MP.MP_BASE_COLOR)
finish(m)
