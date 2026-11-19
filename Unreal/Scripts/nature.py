# DEMIURGE C4 nature: material graphs for generated trees, grass, wildlife and the orbit canopy decal.
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nosplash
# Re-runnable: existing assets are reused and their graphs rebuilt from scratch (deleting a referenced
# package fails unattended). Writes /Game/Nature/M_NatureLeaf, M_NatureBark, M_NatureGrass,
# M_NatureCreature, M_NatureCanopy. Textures are painted in C++ at runtime (DmNatureTextures) and bound
# through dynamic instances; the defaults here only satisfy the sampler types.
# The shading lives in HLSL strings pasted into Custom nodes (a struct with member functions).
# NOISE must stay identical to DmNature::VNoise3 / Fbm3 and FDmNatureFields::CanopyValue (C++), so the
# instanced trees and the canopy decal agree on every forest edge.
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/Nature'
MP = unreal.MaterialProperty
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
ERRORS = []


def log(msg):
    unreal.log('NATUREMAT ' + msg)


def pick_tex(srgb):
    for p in ['/Engine/EngineResources/DefaultTexture', '/Engine/EngineMaterials/DefaultDiffuse',
              '/Engine/EngineResources/Black', '/Engine/EngineResources/WhiteSquareTexture',
              '/Engine/EngineMaterials/BlackSquareTexture', '/Engine/EngineMaterials/Grey',
              '/Engine/EngineMaterials/DefaultWhiteGrid', '/Engine/EngineMaterials/T_Default_Material_Grid_M',
              '/Engine/EngineMaterials/WeightMapPlaceholderTexture', '/Engine/EngineMaterials/DefaultCalibrationGrayscale',
              '/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq']:
        t = unreal.load_asset(p)
        if t and isinstance(t, unreal.Texture2D):
            try:
                s = t.get_editor_property('srgb')
                cs = t.get_editor_property('compression_settings')
            except Exception:
                continue
            if s == srgb and cs in (unreal.TextureCompressionSettings.TC_DEFAULT, unreal.TextureCompressionSettings.TC_MASKS,
                                    unreal.TextureCompressionSettings.TC_GRAYSCALE, unreal.TextureCompressionSettings.TC_BC7,
                                    unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP):
                return t
    return None


TEX_SRGB = pick_tex(True)
TEX_LIN = pick_tex(False)
log('default textures srgb=%s linear=%s' % (TEX_SRGB.get_path_name() if TEX_SRGB else None, TEX_LIN.get_path_name() if TEX_LIN else None))


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

    def const(self, v):
        return self.n(unreal.MaterialExpressionConstant, r=float(v))

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

    # common inputs
    def wpos(self):
        return self.n(unreal.MaterialExpressionWorldPosition)

    def time(self):
        return self.n(unreal.MaterialExpressionTime)

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

    def nrm(self):
        return self.n(unreal.MaterialExpressionVertexNormalWS)

    def vcol(self):
        return self.n(unreal.MaterialExpressionVertexColor)

    def tex(self, name, srgb, uvs):
        t = self.n(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name,
                   texture=TEX_SRGB if srgb else TEX_LIN,
                   sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        self.link(uvs, '', t, 'UVs')
        return t

    def texobj(self, name, srgb):
        return self.n(unreal.MaterialExpressionTextureObjectParameter, parameter_name=name,
                      texture=TEX_SRGB if srgb else TEX_LIN,
                      sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)


def finish(m):
    MEL.layout_material_expressions(m)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log('saved ' + m.get_path_name())


# ---------------------------------------------------------------- HLSL (inside "struct FNat { ... };")
NOISE = r'''
uint NHash(int x, int y, int z) { uint h = (uint(x) * 0x8da6b343u) ^ (uint(y) * 0xd8163841u) ^ (uint(z) * 0xcb1ab31fu); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return h; }
float NH01(int x, int y, int z) { return float(NHash(x, y, z) & 0xFFFFFFu) / 16777216.0; }
float VNoise3(float3 p)
{
    float3 fl = floor(p); int3 i = int3(fl); float3 t = p - fl; float3 u = t * t * (3.0 - 2.0 * t);
    float a = lerp(NH01(i.x, i.y, i.z), NH01(i.x + 1, i.y, i.z), u.x);
    float b = lerp(NH01(i.x, i.y + 1, i.z), NH01(i.x + 1, i.y + 1, i.z), u.x);
    float c = lerp(NH01(i.x, i.y, i.z + 1), NH01(i.x + 1, i.y, i.z + 1), u.x);
    float d = lerp(NH01(i.x, i.y + 1, i.z + 1), NH01(i.x + 1, i.y + 1, i.z + 1), u.x);
    return lerp(lerp(a, b, u.y), lerp(c, d, u.y), u.z) * 2.0 - 1.0;
}
float Fbm3(float3 p, int oct)
{
    float s = 0.0, a = 0.5, n = 0.0;
    for (int o = 0; o < oct; o++) { s += a * VNoise3(p); n += a; a *= 0.5; p = p * 2.03 + float3(17.1, 31.7, 47.3); }
    return s / n;
}
float CanopyValue(float R, float3 dirPlanet)
{
    float3 pm = dirPlanet * 100000.0;
    float n = 0.5 * Fbm3(pm / 520.0, 4) + 0.12 * Fbm3(pm / 90.0 + 3.1, 2);
    return smoothstep(0.42, 0.62, R + 0.6 * n);
}
'''

WIND = r'''
float3 WindDir(float3 wp, float3 ctr, float3 axis)
{
    float3 up = normalize(wp - ctr);
    float3 e = cross(axis, up); float el = length(e); e = el > 1e-4 ? e / el : float3(1, 0, 0);
    float3 nn = cross(up, e);
    return normalize(e * 0.9 + nn * 0.4);
}
float WindS(float t, float ph)
{
    return (0.6 * sin(1.1 * t + ph) + 0.25 * sin(2.7 * t + 1.7 * ph) + 0.35) * (1.0 + 0.4 * sin(0.31 * t + 0.5 * ph));
}
'''


def nat(body, call):
    return 'struct FNat {\n' + NOISE + WIND + body + '\n};\nFNat N;\n' + call


# ---------------------------------------------------------------- leaves
LEAF = r'''
float3 Leaf(float4 tex, float3 vrgb, float ao, float pir, float3 tintA, float3 tintB, float bright, float burn, float snow, float dry,
            float3 nw, float3 wp, float3 ctr, float3 transm, float transScale, out float3 sss, out float aoOut)
{
    float3 base = tex.rgb * pow(max(vrgb, 0.0), 2.2) * lerp(tintA, tintB, pir);
    base *= lerp(0.28, 1.0, ao) * bright;
    base = lerp(base, base * float3(1.45, 1.08, 0.42), saturate(dry) * 0.65);      // dry season / savanna
    base = lerp(base, float3(0.025, 0.02, 0.016), saturate(burn));                 // charred
    float3 up = normalize(wp - ctr);
    float s = saturate(snow) * saturate(dot(nw, up) * 1.6 - 0.1);
    base = lerp(base, float3(0.62, 0.65, 0.70), s * 0.85);
    sss = base * transm * transScale * (1.0 - s);
    aoOut = ao;
    return base;
}
'''
m = new_mat('M_NatureLeaf')
m.set_editor_property('used_with_instanced_static_meshes', True)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
m.set_editor_property('opacity_mask_clip_value', 0.4)
m.set_editor_property('dithered_lod_transition', True)
g = G(m)
uv0 = g.uv(0)
tx = g.tex('Atlas', True, uv0)
vc = g.vcol()
vca = g.custom('return Parameters.VertexColor.a;', [], F1)
wp = g.wpos()
ctr = g.vparam('PlanetCenter', 0, 0, 0)
leaf = g.custom(nat(LEAF, 'return N.Leaf(Tex, VC, AO, PIR, TintA, TintB, Bright, Burn, Snow, Dry, Nw, WP, Ctr, Transm, TransScale, SSS, AOut);'),
                [('Tex', tx, 'RGBA'), ('VC', vc, ''), ('AO', vca, ''), ('PIR', g.pir(), ''),
                 ('TintA', g.vparam('TintA', 0.86, 0.94, 1.0), ''), ('TintB', g.vparam('TintB', 1.14, 1.06, 0.78), ''),
                 ('Bright', g.scalar('Brightness', 0.85), ''), ('Burn', g.cdata(0), ''), ('Snow', g.cdata(1), ''), ('Dry', g.cdata(2), ''),
                 ('Nw', g.nrm(), ''), ('WP', wp, ''), ('Ctr', ctr, ''),
                 ('Transm', g.vparam('Transmission', 0.75, 0.9, 0.3), ''), ('TransScale', g.scalar('TransmissionScale', 1.0), '')],
                F3, [('SSS', F3), ('AOut', F1)])
g.out(leaf, '', MP.MP_BASE_COLOR)
g.out(leaf, 'SSS', MP.MP_SUBSURFACE_COLOR)
g.out(leaf, 'AOut', MP.MP_AMBIENT_OCCLUSION)
g.out(tx, 'A', MP.MP_OPACITY_MASK)
g.out(g.scalar('LeafRough', 0.85), '', MP.MP_ROUGHNESS)
g.out(g.scalar('LeafSpec', 0.04), '', MP.MP_SPECULAR)
LEAFWPO = r'''
float3 Bill(float3 wp, float3 ctr, float3 cam, float3 dW, float2 uv2, float2 uv3, float2 uv4, float amount)
{
    // camera-facing clump cards of the far LODs (DmNatureTrees BillboardCard): UV2/UV3.x = offset from this corner to
    // the card centre (m, mesh space; dW is the same offset in world space, instance scale included), UV3.y = 1 + s + 2t,
    // UV4 = card width / length (m). The card is rebuilt around its centre facing the camera, its top towards the sky.
    if (uv3.y < 0.5) return float3(0, 0, 0);
    float code = uv3.y - 1.0;
    float s = fmod(code, 2.0);
    float t = floor(code * 0.5 + 0.01);
    float dl = length(float3(uv2, uv3.x)) * 100.0;
    float sc = dl > 1e-3 ? length(dW) / dl : 1.0;
    float3 C = wp + dW;
    float3 up = normalize(C - ctr);
    float3 f = cam - C; float fl = length(f); f = fl > 1.0 ? f / fl : up;
    float3 r = cross(up, f); float rl = length(r);
    r = rl > 0.03 ? r / rl : normalize(cross(f, abs(f.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
    float3 u = cross(f, r);
    float3 Pn = C + (r * (s - 0.5) * uv4.x + u * (t - 0.5) * uv4.y) * (100.0 * sc);
    return (Pn - wp) * amount;
}
float3 Wpo(float3 wp, float3 ctr, float3 axis, float t, float pir, float2 uv0, float2 uv1, float3 nw, float strength, float speed, float flutter,
           float3 cam, float3 dW, float2 uv2, float2 uv3, float2 uv4, float bill)
{
    float tt = t * speed; float ph = pir * 6.283;
    float3 sway = WindDir(wp, ctr, axis) * WindS(tt, ph) * uv1.x * strength;
    float fl = sin(8.0 * tt + 40.0 * (uv0.x + uv0.y) + 3.0 * ph) * uv1.y * flutter;
    return sway + nw * fl + Bill(wp, ctr, cam, dW, uv2, uv3, uv4, bill);
}
'''
dloc = g.custom('return float3(A.xy, B.x) * 100.0;', [('A', g.uv(2), ''), ('B', g.uv(3), '')], F3)
dw = g.n(unreal.MaterialExpressionTransform,
         transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
         transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
g.link(dloc, '', dw, '')
wpo = g.custom(nat(LEAFWPO, 'return N.Wpo(WP, Ctr, Axis, T, PIR, UV0, UV1, Nw, Strength, Speed, Flutter, Cam, DW, UV2, UV3, UV4, Bill);'),
               [('WP', g.wpos(), ''), ('Ctr', g.vparam('PlanetCenter', 0, 0, 0), ''), ('Axis', g.vparam('PlanetAxis', 0, 0, 1), ''),
                ('T', g.time(), ''), ('PIR', g.pir(), ''), ('UV0', g.uv(0), ''), ('UV1', g.uv(1), ''), ('Nw', g.nrm(), ''),
                ('Strength', g.scalar('WindStrength', 100.0), ''), ('Speed', g.scalar('WindSpeed', 1.0), ''), ('Flutter', g.scalar('FlutterStrength', 60.0), ''),
                ('Cam', g.n(unreal.MaterialExpressionCameraPositionWS), ''), ('DW', dw, ''),
                ('UV2', g.uv(2), ''), ('UV3', g.uv(3), ''), ('UV4', g.uv(4), ''), ('Bill', g.scalar('Billboard', 1.0), '')],
               F3)
g.out(wpo, '', MP.MP_WORLD_POSITION_OFFSET)
finish(m)

# ---------------------------------------------------------------- bark
BARK = r'''
float3 Bark(float3 tex, float3 vrgb, float burn, float snow, float3 nw, float3 wp, float3 ctr)
{
    float3 base = tex * pow(max(vrgb, 0.0), 2.2);
    base = lerp(base, float3(0.018, 0.015, 0.013), saturate(burn) * 0.92);
    float3 up = normalize(wp - ctr);
    float s = saturate(snow) * saturate(dot(nw, up) * 2.0 - 0.6);
    return lerp(base, float3(0.62, 0.65, 0.70), s * 0.8);
}
'''
m = new_mat('M_NatureBark')
m.set_editor_property('used_with_instanced_static_meshes', True)
m.set_editor_property('dithered_lod_transition', True)
g = G(m)
tile = g.n(unreal.MaterialExpressionAppendVector)
g.link(g.scalar('BarkTileU', 0.8), '', tile, 'A')
g.link(g.scalar('BarkTileV', 0.45), '', tile, 'B')
uvb = g.n(unreal.MaterialExpressionMultiply)
g.link(g.uv(0), '', uvb, 'A')
g.link(tile, '', uvb, 'B')
tx = g.tex('BarkTex', True, uvb)
tn = g.tex('BarkN', False, uvb)
bark = g.custom(nat(BARK, 'return N.Bark(Tex, VC, Burn, Snow, Nw, WP, Ctr);'),
                [('Tex', tx, ''), ('VC', g.vcol(), ''), ('Burn', g.cdata(0), ''), ('Snow', g.cdata(1), ''), ('Nw', g.nrm(), ''),
                 ('WP', g.wpos(), ''), ('Ctr', g.vparam('PlanetCenter', 0, 0, 0), '')], F3)
g.out(bark, '', MP.MP_BASE_COLOR)
nrmc = g.custom('float2 xy = T.rg * 2.0 - 1.0; return float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));', [('T', tn, 'RGBA')], F3)
g.out(nrmc, '', MP.MP_NORMAL)
g.out(g.const(0.85), '', MP.MP_ROUGHNESS)
g.out(g.const(0.25), '', MP.MP_SPECULAR)
BARKWPO = r'''
float3 Wpo(float3 wp, float3 ctr, float3 axis, float t, float pir, float2 uv1, float strength, float speed)
{
    float tt = t * speed; float ph = pir * 6.283;
    return WindDir(wp, ctr, axis) * WindS(tt, ph) * uv1.x * strength;
}
'''
wpo = g.custom(nat(BARKWPO, 'return N.Wpo(WP, Ctr, Axis, T, PIR, UV1, Strength, Speed);'),
               [('WP', g.wpos(), ''), ('Ctr', g.vparam('PlanetCenter', 0, 0, 0), ''), ('Axis', g.vparam('PlanetAxis', 0, 0, 1), ''),
                ('T', g.time(), ''), ('PIR', g.pir(), ''), ('UV1', g.uv(1), ''),
                ('Strength', g.scalar('WindStrength', 100.0), ''), ('Speed', g.scalar('WindSpeed', 1.0), '')], F3)
g.out(wpo, '', MP.MP_WORLD_POSITION_OFFSET)
finish(m)

# ---------------------------------------------------------------- grass
GRASS = r'''
float3 Grass(float3 vrgb, float ao, float pir, float dry, float snow, float3 tintA, float3 tintB, out float3 sss)
{
    float3 base = pow(max(vrgb, 0.0), 2.2) * lerp(tintA, tintB, pir);
    base = lerp(base, base * float3(1.4, 1.1, 0.55), saturate(dry) * 0.5);
    base *= lerp(0.45, 1.0, ao);
    base = lerp(base, float3(0.6, 0.62, 0.66), saturate(snow) * 0.7);
    sss = base * float3(0.7, 0.85, 0.3);
    return base;
}
'''
m = new_mat('M_NatureGrass')
m.set_editor_property('used_with_instanced_static_meshes', True)
m.set_editor_property('two_sided', True)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
g = G(m)
vca = g.custom('return Parameters.VertexColor.a;', [], F1)
gr = g.custom(nat(GRASS, 'return N.Grass(VC, AO, PIR, Dry, Snow, TintA, TintB, SSS);'),
              [('VC', g.vcol(), ''), ('AO', vca, ''), ('PIR', g.pir(), ''), ('Dry', g.cdata(2), ''), ('Snow', g.cdata(1), ''),
               ('TintA', g.vparam('TintA', 0.85, 0.95, 0.95), ''), ('TintB', g.vparam('TintB', 1.12, 1.05, 0.8), '')], F3, [('SSS', F3)])
g.out(gr, '', MP.MP_BASE_COLOR)
g.out(gr, 'SSS', MP.MP_SUBSURFACE_COLOR)
g.out(g.const(0.8), '', MP.MP_ROUGHNESS)
g.out(g.const(0.05), '', MP.MP_SPECULAR)
wpo = g.custom(nat(BARKWPO, 'return N.Wpo(WP, Ctr, Axis, T, PIR, UV1, Strength, Speed) * 1.5;'),
               [('WP', g.wpos(), ''), ('Ctr', g.vparam('PlanetCenter', 0, 0, 0), ''), ('Axis', g.vparam('PlanetAxis', 0, 0, 1), ''),
                ('T', g.time(), ''), ('PIR', g.pir(), ''), ('UV1', g.uv(1), ''),
                ('Strength', g.scalar('WindStrength', 100.0), ''), ('Speed', g.scalar('WindSpeed', 1.0), '')], F3)
g.out(wpo, '', MP.MP_WORLD_POSITION_OFFSET)
finish(m)

# ---------------------------------------------------------------- creatures (herds, birds, fish): motion in the material
CREATURE = r'''
float3 Offset(float3 lp, float t, float pir, float rM, float spd, float2 uv1, float mode)
{
    float ph = pir * 6.283;
    if (mode < 0.5)
    {
        // grazer: head down to graze and up again, a slow drift forward and back
        float graze = smoothstep(0.25, 0.85, 0.5 + 0.5 * sin(0.45 * t + ph * 3.0));
        float3 o = float3(18.0 * graze * uv1.x, 0.0, -38.0 * graze * uv1.x);
        o.x += 70.0 * sin(0.035 * t + ph);
        return o;
    }
    float R = max(rM, 0.5) * 100.0;
    float3 P = lp;
    float sg = spd >= 0.0 ? 1.0 : -1.0;
    if (mode > 1.5) P.x += sin(t * 9.0 + ph * 4.0) * 7.0 * uv1.x;      // fish: tail beat
    P.y *= sg;                                                          // flying / swimming the other way round
    float th = sg * abs(spd) * 100.0 / R * t;
    float3 Q = P + float3(R, 0.0, 0.0);
    float c = cos(th), s = sin(th);
    float3 Rq = float3(c * Q.x - s * Q.y, s * Q.x + c * Q.y, Q.z) - float3(R, 0.0, 0.0);
    if (mode < 1.5)
    {
        // bird: wing beats in bouts, gliding between, a slow rise and fall
        float bout = smoothstep(-0.2, 0.4, sin(0.35 * t + ph * 2.0));
        Rq.z += sin(t * 6.5 + ph * 5.0) * 32.0 * uv1.x * bout + abs(uv1.x) * 6.0;
        Rq.z += 350.0 * sin(0.17 * t + ph * 2.0);
    }
    else
    {
        // fish: just under the surface, leaping clear every few seconds
        float cyc = frac(t * 0.11 + pir * 3.7);
        float leap = cyc < 0.1 ? sin(cyc / 0.1 * 3.14159) : 0.0;
        Rq.z += -55.0 + 150.0 * leap;
    }
    return Rq - lp;
}
'''
m = new_mat('M_NatureCreature')
m.set_editor_property('used_with_instanced_static_meshes', True)
m.set_editor_property('two_sided', True)
g = G(m)
tint = g.n(unreal.MaterialExpressionAppendVector)
ap2 = g.n(unreal.MaterialExpressionAppendVector)
c0, c1, c2 = g.cdata(0, 1.0), g.cdata(1, 1.0), g.cdata(2, 1.0)
g.link(c0, '', ap2, 'A')
g.link(c1, '', ap2, 'B')
g.link(ap2, '', tint, 'A')
g.link(c2, '', tint, 'B')
mode = g.scalar('AnimMode', 0.0)
col = g.custom('float3 v = pow(max(VC, 0.0), 2.2); float3 k = Mode < 0.5 ? Tint : Tint.zzz; return v * k;',
               [('VC', g.vcol(), ''), ('Tint', tint, ''), ('Mode', mode, '')], F3)
g.out(col, '', MP.MP_BASE_COLOR)
g.out(g.const(0.75), '', MP.MP_ROUGHNESS)
g.out(g.const(0.3), '', MP.MP_SPECULAR)
off = g.custom(nat(CREATURE, 'return N.Offset(LP, T, PIR, RM, Spd, UV1, Mode);'),
               [('LP', g.n(unreal.MaterialExpressionLocalPosition), ''), ('T', g.time(), ''), ('PIR', g.pir(), ''),
                ('RM', g.cdata(0, 0.0), ''), ('Spd', g.cdata(1, 0.0), ''), ('UV1', g.uv(1), ''), ('Mode', g.scalar('AnimMode', 0.0), '')], F3)
tr = g.n(unreal.MaterialExpressionTransform,
         transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
         transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
g.link(off, '', tr, '')
g.out(tr, '', MP.MP_WORLD_POSITION_OFFSET)
finish(m)

# ---------------------------------------------------------------- canopy decal (the forests from orbit)
CANOPY = r'''
float3 Canopy(float3 wp, float3 ctr, float3 ax, float3 ay, float3 az, float seaR, float strength, Texture2D Tex, SamplerState TexSampler,
              Texture2D Clim, SamplerState ClimSampler, float3 farTint, float farStrength, out float opacity, out float rough)
{
    float3 rel = wp - ctr;
    float r = length(rel);
    float3 p = float3(dot(rel, ax), dot(rel, ay), dot(rel, az)) / max(r, 1.0);
    float2 uv = float2(atan2(p.y, p.x) / 6.2831853 + 0.5, acos(clamp(p.z, -1.0, 1.0)) / 3.14159265);
    float2 dx = ddx(uv), dy = ddy(uv);
    dx.x -= round(dx.x); dy.x -= round(dy.x);                 // the longitude seam
    float4 C = Tex.SampleGrad(TexSampler, uv, dx, dy);
    float4 K = Clim.SampleGrad(ClimSampler, uv, dx, dy);
    float fp = max(length(ddx(wp)), length(ddy(wp))) * 0.01;   // pixel footprint, metres
    float3 pm = p * 100000.0;
    // the forest edge (CPU twin: FDmNatureFields::CanopyValue), averaged once the noise is below a pixel
    float can = CanopyValue(C.r, p);
    float canFar = smoothstep(0.36, 0.66, C.r);   // the mean of the noisy edge below over its noise spread
    can = lerp(can, canFar, smoothstep(120.0, 600.0, fp));
    // colours (linear albedo): broadleaf, conifer, tropical, dry woodland, snow-laden
    // linear albedo of a closed canopy seen from above (sunlit crowns and their shadows averaged): Earth's broadleaf
    // forest is ~0.04-0.06 in green, conifers darker and bluer, tropical forest deep and saturated, dry woodland olive
    float3 col = float3(0.040, 0.068, 0.022);
    col = lerp(col, float3(0.022, 0.042, 0.024), C.g);
    col = lerp(col, float3(0.026, 0.060, 0.016), C.b);
    col = lerp(col, float3(0.062, 0.064, 0.030), C.a * 0.8);
    // stands, clumps and crowns, each faded when it is smaller than a few pixels
    float stand = Fbm3(pm / 230.0 + 5.0, 3);
    col *= lerp(1.0, 0.78 + 0.44 * (0.5 + 0.5 * stand), smoothstep(600.0, 60.0, fp));
    float clump = Fbm3(pm / 38.0 + 9.0, 2);
    col *= lerp(1.0, 0.8 + 0.4 * (0.5 + 0.5 * clump), smoothstep(25.0, 4.0, fp));
    float crown = VNoise3(pm / 6.5);
    col *= lerp(0.86, 0.62 + 0.72 * (0.5 + 0.5 * crown), smoothstep(5.0, 0.8, fp));
    // close up the instanced trees are the canopy: the decal becomes the shaded forest floor under them. The tree
    // tiles thin to 1/2, 1/4, 1/8 beyond 1.5, 3 and 5 km (a pixel is ~0.55 m per km of distance at 1080p), so the
    // decal must be canopy again by then: floor below ~1 km, canopy from ~4.5 km (it was floor out to ~16 km, so
    // thinned forests read as scattered dots on bare ground in the 5 km view)
    float3 floorC = float3(0.052, 0.050, 0.030) * (0.8 + 0.4 * (0.5 + 0.5 * Fbm3(pm / 2.5 + 2.0, 2)));
    col = lerp(floorC, col, smoothstep(0.6, 2.5, fp));
    // snow on the crowns of cold forests (taiga in winter)
    col = lerp(col, float3(0.40, 0.42, 0.46), K.b * 0.55);
    // seen from high up (a pixel over ~30-120 m: about 60 km and beyond) the decal IS the forest: the planet's own forest
    // tint is a lighter, bluer green that the scaled atmosphere's haze turns teal-grey; there the decal takes over at
    // farStrength with a sap-green farTint (less blue) so canopy reads as dark green forest, as in descent-1
    float far = smoothstep(30.0, 120.0, fp);
    col *= lerp(float3(1.0, 1.0, 1.0), farTint, far);
    // where the tree tiles are thinned (beyond ~1.5 km) the decal stands in for the missing crowns: full strength
    // over the planet's own (lighter) forest tint, and the shade between crowns (the instanced trees read darker
    // than a flat canopy albedo, so a lighter decal made them look like dots on a lawn)
    float own = smoothstep(0.6, 2.5, fp);
    col *= lerp(1.0, 0.78, own * (1.0 - far));
    float above = saturate((r - seaR - 60.0) / 140.0);
    opacity = saturate(can * lerp(strength, farStrength, max(own, far)) * 0.92 * above);
    rough = 0.9;
    return col;
}
'''
m = new_mat('M_NatureCanopy')
m.set_editor_property('material_domain', unreal.MaterialDomain.MD_DEFERRED_DECAL)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
# decal blend mode: the default (DBM_Translucent; the property is protected from Python)
g = G(m)
can = g.custom(nat(CANOPY, 'return N.Canopy(WP, Ctr, AX, AY, AZ, SeaR, Strength, Tex, TexSampler, Clim, ClimSampler, FarTint, FarStrength, Opacity, Rough);'),
               [('WP', g.wpos(), ''), ('Ctr', g.vparam('PlanetCenter', 0, 0, 0), ''),
                ('AX', g.vparam('PlanetX', 1, 0, 0), ''), ('AY', g.vparam('PlanetY', 0, 1, 0), ''), ('AZ', g.vparam('PlanetZ', 0, 0, 1), ''),
                ('SeaR', g.scalar('SeaRadius', 1.0e7), ''), ('Strength', g.scalar('Strength', 1.0), ''),
                ('Tex', g.texobj('CanopyTex', False), ''), ('Clim', g.texobj('ClimateTex', False), ''),
                ('FarTint', g.vparam('FarTint', 1, 1, 1), ''), ('FarStrength', g.scalar('FarStrength', 1.0), '')],
               F3, [('Opacity', F1), ('Rough', F1)])
g.out(can, '', MP.MP_BASE_COLOR)
g.out(can, 'Opacity', MP.MP_OPACITY)
g.out(can, 'Rough', MP.MP_ROUGHNESS)
g.out(g.const(0.0), '', MP.MP_METALLIC)
g.out(g.const(0.3), '', MP.MP_SPECULAR)
finish(m)

if ERRORS:
    for e in ERRORS:
        unreal.log_error('NATUREMAT ' + e)
    log('DONE WITH %d ERRORS' % len(ERRORS))
else:
    log('DONE OK')
