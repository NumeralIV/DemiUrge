# LifeLab: material graphs for generated vegetation, terrain, water and buildings.
# UnrealEditor-Cmd.exe PlanetLab.uproject -run=pythonscript -script=<this file> -unattended -nosplash
# Textures are painted in C++ at runtime and bound through dynamic material instances; the defaults here
# only satisfy the sampler types.
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
DIR = '/Game/Life'
MP = unreal.MaterialProperty


def pick_tex(srgb):
    for p in ['/Engine/EngineResources/DefaultTexture', '/Engine/EngineMaterials/DefaultDiffuse',
              '/Engine/EngineResources/Black', '/Engine/EngineResources/WhiteSquareTexture',
              '/Engine/EngineMaterials/BlackSquareTexture', '/Engine/EngineMaterials/Grey',
              '/Engine/EngineMaterials/DefaultWhiteGrid', '/Engine/EngineMaterials/T_Default_Material_Grid_M',
              '/Engine/EngineMaterials/DefaultBloomKernel', '/Engine/EngineResources/DefaultTextureCube',
              '/Engine/EngineMaterials/WeightMapPlaceholderTexture', '/Engine/EngineMaterials/DefaultCalibrationGrayscale',
              '/Engine/ArtTools/RenderToTexture/Textures/127grey', '/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq',
              '/Engine/EngineMaterials/MiniFont']:
        t = unreal.load_asset(p)
        if t and isinstance(t, unreal.Texture2D):
            try:
                s = t.get_editor_property('srgb')
                cs = t.get_editor_property('compression_settings')
            except Exception:
                continue
            unreal.log('LIFEMAT tex %s srgb=%s cs=%s' % (p, s, cs))
            if s == srgb and cs in (unreal.TextureCompressionSettings.TC_DEFAULT, unreal.TextureCompressionSettings.TC_MASKS,
                                    unreal.TextureCompressionSettings.TC_GRAYSCALE, unreal.TextureCompressionSettings.TC_EDITOR_ICON,
                                    unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP, unreal.TextureCompressionSettings.TC_BC7):
                return t
    return None


TEX_SRGB = pick_tex(True)
TEX_LIN = pick_tex(False)
unreal.log('LIFEMAT default srgb=%s linear=%s' % (TEX_SRGB.get_path_name() if TEX_SRGB else None, TEX_LIN.get_path_name() if TEX_LIN else None))


def new_mat(name):
    path = DIR + '/' + name
    m = None
    if EAL.does_asset_exist(path):
        # deleting can fail when the asset is referenced: reuse it and rebuild its graph from scratch
        m = unreal.load_asset(path)
        if m:
            MEL.delete_all_material_expressions(m)
    if not m:
        m = AT.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('used_with_instanced_static_meshes', True)
    return m


def node(m, cls, x, y, **props):
    e = MEL.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def link(a, a_out, b, b_in):
    ok = MEL.connect_material_expressions(a, a_out, b, b_in)
    if not ok:
        unreal.log_warning('LIFEMAT link failed %s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in))


def out(m, e, prop, pin=''):
    MEL.connect_material_property(e, pin, prop)


class G:
    """Small graph helper that lays nodes out left to right."""

    def __init__(self, m):
        self.m = m
        self.x = -2400
        self.y = 0

    def n(self, cls, **props):
        self.y += 90
        if self.y > 2600:
            self.y = 0
            self.x += 260
        return node(self.m, cls, self.x, self.y, **props)

    def const(self, v):
        return self.n(unreal.MaterialExpressionConstant, r=float(v))

    def vec(self, r, g, b):
        return self.n(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(r, g, b, 1.0))

    def scalar(self, name, v):
        return self.n(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(v))

    def vparam(self, name, r, g, b):
        return self.n(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(r, g, b, 1.0))

    def bin(self, cls, a, b, a_out='', b_out=''):
        e = self.n(cls)
        if isinstance(a, (int, float)):
            e.set_editor_property('const_a', float(a))
        else:
            link(a, a_out, e, 'A')
        if isinstance(b, (int, float)):
            e.set_editor_property('const_b', float(b))
        else:
            link(b, b_out, e, 'B')
        return e

    def mul(self, a, b, ao='', bo=''):
        return self.bin(unreal.MaterialExpressionMultiply, a, b, ao, bo)

    def add(self, a, b, ao='', bo=''):
        return self.bin(unreal.MaterialExpressionAdd, a, b, ao, bo)

    def sub(self, a, b, ao='', bo=''):
        return self.bin(unreal.MaterialExpressionSubtract, a, b, ao, bo)

    def lerp(self, a, b, t, ao='', bo='', to=''):
        e = self.n(unreal.MaterialExpressionLinearInterpolate)
        if isinstance(a, (int, float)):
            e.set_editor_property('const_a', float(a))
        else:
            link(a, ao, e, 'A')
        if isinstance(b, (int, float)):
            e.set_editor_property('const_b', float(b))
        else:
            link(b, bo, e, 'B')
        if isinstance(t, (int, float)):
            e.set_editor_property('const_alpha', float(t))
        else:
            link(t, to, e, 'Alpha')
        return e

    def un(self, cls, src, so='', **props):
        e = self.n(cls, **props)
        link(src, so, e, '')
        return e

    def sin(self, a, ao=''):
        return self.un(unreal.MaterialExpressionSine, a, ao, period=6.283185307)

    def sat(self, a, ao=''):
        return self.un(unreal.MaterialExpressionSaturate, a, ao)

    def mask(self, a, ao='', r=False, g=False, b=False, al=False):
        return self.un(unreal.MaterialExpressionComponentMask, a, ao, r=r, g=g, b=b, a=al)

    def pow(self, a, e, ao=''):
        p = self.n(unreal.MaterialExpressionPower, const_exponent=float(e))
        link(a, ao, p, 'Base')
        return p

    def append(self, a, b, ao='', bo=''):
        e = self.n(unreal.MaterialExpressionAppendVector)
        link(a, ao, e, 'A')
        link(b, bo, e, 'B')
        return e

    def uv(self, idx=0):
        return self.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=idx)

    def tex(self, name, srgb, uvs=None, uvo=''):
        t = self.n(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name,
                   texture=TEX_SRGB if srgb else TEX_LIN,
                   sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        if uvs is not None:
            link(uvs, uvo, t, 'UVs')
        return t

    def vcol_linear(self):
        """Returns (float4 vertex colour incl. alpha via a Custom node, linearised RGB)."""
        vc = self.n(unreal.MaterialExpressionVertexColor)
        rgb = self.pow(vc, 2.2)
        c = self.n(unreal.MaterialExpressionCustom, code='return Parameters.VertexColor;',
                   output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4, description='VertexColorRGBA')
        c.set_editor_property('inputs', [])
        return c, rgb

    def normal_xy(self, t, chans):
        """Two packed channels of a linear texture -> tangent normal."""
        if chans == 'rg':
            xy = self.mask(t, 'RGBA', r=True, g=True)
        else:
            xy = self.mask(t, 'RGBA', b=True, al=True)
        xy2 = self.sub(self.mul(xy, 2.0), 1.0)
        dn = self.n(unreal.MaterialExpressionDeriveNormalZ)
        link(xy2, '', dn, 'InXY')
        return dn


def wind(g, flutter):
    t = g.n(unreal.MaterialExpressionTime)
    tt = g.mul(t, g.scalar('WindSpeed', 1.0))
    pr = g.n(unreal.MaterialExpressionPerInstanceRandom)
    ph = g.mul(pr, 6.283)
    s1 = g.mul(g.sin(g.add(g.mul(tt, 1.1), ph)), 0.6)
    s2 = g.mul(g.sin(g.add(g.mul(tt, 2.7), g.mul(ph, 1.7))), 0.25)
    s = g.add(g.add(s1, s2), 0.35)
    gust = g.add(g.mul(g.sin(g.add(g.mul(tt, 0.31), g.mul(ph, 0.5))), 0.4), 1.0)
    s = g.mul(s, gust)
    uv1 = g.uv(1)
    w = g.mask(uv1, '', r=True)
    f = g.mask(uv1, '', g=True)
    d = g.mask(g.vparam('WindDir', 1.0, 0.4, 0.0), '', r=True, g=True, b=True)
    strength = g.scalar('WindStrength', 100.0)
    sway = g.mul(g.mul(d, g.mul(s, w)), strength)
    if not flutter:
        return sway
    uv0 = g.uv(0)
    u = g.mask(uv0, '', r=True)
    v = g.mask(uv0, '', g=True)
    p2 = g.add(g.add(g.mul(g.add(u, v), 40.0), g.mul(ph, 3.0)), g.mul(tt, 8.0))
    fl = g.mul(g.mul(g.sin(p2), f), g.scalar('FlutterStrength', 60.0))
    nrm = g.n(unreal.MaterialExpressionVertexNormalWS)
    return g.add(sway, g.mul(nrm, fl))


def finish(m):
    MEL.layout_material_expressions(m)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    unreal.log('LIFEMAT saved ' + m.get_path_name())


# ------------------------------------------------------------------ leaves
m = new_mat('M_LifeLeaf')
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
m.set_editor_property('opacity_mask_clip_value', 0.4)
m.set_editor_property('dithered_lod_transition', True)
g = G(m)
tx = g.tex('Atlas', True, g.uv(0))
vc, vcl = g.vcol_linear()
ao = g.mask(vc, '', al=True)
pr = g.n(unreal.MaterialExpressionPerInstanceRandom)
hue = g.lerp(g.vparam('TintA', 0.86, 0.94, 1.0), g.vparam('TintB', 1.14, 1.06, 0.78), pr)
base = g.mul(g.mul(g.mask(tx, 'RGB', r=True, g=True, b=True), vcl), hue)
base = g.mul(base, g.lerp(0.28, 1.0, ao))
base = g.mul(base, g.scalar('Brightness', 1.0))
out(m, base, MP.MP_BASE_COLOR)
sss = g.mul(g.mul(base, g.vparam('Transmission', 0.85, 1.0, 0.35)), g.scalar('TransmissionScale', 1.0))
out(m, sss, MP.MP_SUBSURFACE_COLOR)
out(m, g.scalar('LeafRough', 0.6), MP.MP_ROUGHNESS)
out(m, g.scalar('LeafSpec', 0.35), MP.MP_SPECULAR)
out(m, tx, MP.MP_OPACITY_MASK, 'A')
out(m, ao, MP.MP_AMBIENT_OCCLUSION)
out(m, wind(g, True), MP.MP_WORLD_POSITION_OFFSET)
finish(m)

# ------------------------------------------------------------------ bark
m = new_mat('M_LifeBark')
m.set_editor_property('dithered_lod_transition', True)
g = G(m)
uvb = g.mul(g.uv(0), g.n(unreal.MaterialExpressionConstant2Vector, r=0.8, g=0.45))
tx = g.tex('BarkTex', True, uvb)
tn = g.tex('BarkN', False, uvb)
vc, vcl = g.vcol_linear()
base = g.mul(g.mask(tx, 'RGB', r=True, g=True, b=True), vcl)
out(m, base, MP.MP_BASE_COLOR)
out(m, g.normal_xy(tn, 'rg'), MP.MP_NORMAL)
out(m, g.const(0.85), MP.MP_ROUGHNESS)
out(m, g.const(0.3), MP.MP_SPECULAR)
out(m, wind(g, False), MP.MP_WORLD_POSITION_OFFSET)
finish(m)

# ------------------------------------------------------------------ terrain
m = new_mat('M_LifeTerrain')
g = G(m)
uv0 = g.uv(0)   # metres
uv1 = g.uv(1)   # land-use texture coordinates
vc, vcl = g.vcol_linear()
canopy = g.mask(vc, '', al=True)
gfine = g.tex('GroundTex', False, g.mul(uv0, 1.0 / 9.0))
gmid = g.tex('GroundTex', False, g.mul(uv0, 1.0 / 61.0))
gfar = g.tex('GroundTex', False, g.mul(uv0, 1.0 / 433.0))
d1 = g.lerp(0.80, 1.18, gfine, to='R')
d2 = g.lerp(0.86, 1.12, gmid, to='R')
d3 = g.lerp(0.84, 1.14, gfar, to='A')
gnear = g.tex('GroundTex', False, g.mul(uv0, 1.0 / 2.3))
d0 = g.lerp(0.88, 1.1, gnear, to='R')
detail = g.mul(g.mul(g.mul(d1, d2), d3), d0)
natural = g.mul(vcl, detail)
# forest canopy seen from above: crown clumps 10-ish m across, darker gaps
crown = g.tex('GroundTex', False, g.mul(uv0, 1.0 / 130.0))
crown2 = g.tex('GroundTex', False, g.mul(uv0, 1.0 / 47.0))
cc = g.lerp(0.62, 1.18, g.mul(crown, crown2, 'G', 'G'))
canopy_col = g.mul(g.mul(vcl, cc), d3)
ground = g.lerp(natural, canopy_col, canopy)
# land use: R dirt road, G paved, B field, A yard / built ground
lu = g.tex('LandUse', False, uv1)
ft = g.tex('FieldTex', True, uv1)
ang = g.mul(g.mask(ft, 'A', r=True), 3.14159)
ca = g.n(unreal.MaterialExpressionCosine, period=6.283185307)
link(ang, '', ca, '')
sa = g.sin(ang)
u = g.mask(uv0, '', r=True)
v = g.mask(uv0, '', g=True)
along = g.add(g.mul(u, ca), g.mul(v, sa))
rows = g.add(g.mul(g.sin(g.mul(along, 6.283 / 1.6)), 0.5), 0.5)
rowsk = g.lerp(1.06, 0.84, g.mul(rows, g.scalar('RowContrast', 1.0)))
field = g.mul(g.mul(g.pow(g.mask(ft, 'RGB', r=True, g=True, b=True), 1.0), rowsk), g.mul(d1, d3))
ground = g.lerp(ground, field, lu, to='B')
yard = g.mul(g.vec(0.20, 0.17, 0.12), detail)
ground = g.lerp(ground, yard, lu, to='A')
dirt = g.mul(g.vec(0.22, 0.17, 0.11), g.mul(d1, d2))
ground = g.lerp(ground, dirt, lu, to='R')
cob = g.tex('GroundTex', False, g.mul(uv0, 1.0 / 3.0))
paved = g.mul(g.vparam('PavedColor', 0.16, 0.155, 0.15), g.lerp(0.75, 1.15, cob, to='B'))
ground = g.lerp(ground, paved, lu, to='G')
out(m, ground, MP.MP_BASE_COLOR)
out(m, g.lerp(0.92, 0.7, lu, to='G'), MP.MP_ROUGHNESS)
out(m, g.const(0.35), MP.MP_SPECULAR)
out(m, g.lerp(1.0, 0.75, canopy), MP.MP_AMBIENT_OCCLUSION)
finish(m)

# ------------------------------------------------------------------ water
m = new_mat('M_LifeWater')
g = G(m)
uv0 = g.uv(0)
t = g.n(unreal.MaterialExpressionTime)
pan1 = g.add(g.mul(uv0, 1.0 / 23.0), g.mul(t, g.n(unreal.MaterialExpressionConstant2Vector, r=0.011, g=0.006)))
pan2 = g.add(g.mul(uv0, 1.0 / 8.3), g.mul(t, g.n(unreal.MaterialExpressionConstant2Vector, r=-0.016, g=0.012)))
n1 = g.tex('WaterN', False, pan1)
n2 = g.tex('WaterN', False, pan2)
xy = g.add(g.mask(n1, 'RGBA', r=True, g=True), g.mask(n2, 'RGBA', r=True, g=True))
xy = g.mul(g.sub(xy, 1.0), g.scalar('Ripple', 0.5))
dn = g.n(unreal.MaterialExpressionDeriveNormalZ)
link(xy, '', dn, 'InXY')
vc, vcl = g.vcol_linear()
foam = g.mask(vc, '', al=True)
base = g.lerp(vcl, g.vec(0.55, 0.6, 0.6), foam)
out(m, base, MP.MP_BASE_COLOR)
out(m, dn, MP.MP_NORMAL)
out(m, g.lerp(0.04, 0.5, foam), MP.MP_ROUGHNESS)
out(m, g.const(0.35), MP.MP_SPECULAR)
finish(m)

# ------------------------------------------------------------------ walls (UV1.x = style 0 plaster, 1 stone, 2 brick, 3 wood, 4 concrete)
m = new_mat('M_LifeWall')
g = G(m)
uvw = g.mul(g.uv(0), 0.25)
tx = g.tex('WallTex', False, uvw)
tn = g.tex('WallN', False, uvw)
style = g.mask(g.uv(1), '', r=True)


def band(g, s, k):
    return g.sat(g.sub(1.0, g.un(unreal.MaterialExpressionAbs, g.sub(s, float(k)))))


wpl = band(g, style, 0)
wst = band(g, style, 1)
wbr = band(g, style, 2)
wwd = band(g, style, 3)
wcn = band(g, style, 4)
msk = g.add(g.add(g.mul(tx, wpl, 'R'), g.mul(tx, wst, 'G')), g.add(g.mul(tx, wbr, 'B'), g.mul(tx, wwd, 'A')))
msk = g.add(msk, g.mul(g.lerp(0.9, 1.04, tx, to='R'), wcn))
vc, vcl = g.vcol_linear()
base = g.mul(vcl, msk)
out(m, base, MP.MP_BASE_COLOR)
nst = g.normal_xy(tn, 'rg')
nbr = g.normal_xy(tn, 'ba')
flat = g.vec(0.0, 0.0, 1.0)
nn = g.lerp(g.lerp(flat, nst, wst), nbr, wbr)
out(m, nn, MP.MP_NORMAL)
out(m, g.lerp(0.88, 0.7, wcn), MP.MP_ROUGHNESS)
out(m, g.const(0.35), MP.MP_SPECULAR)
finish(m)

# ------------------------------------------------------------------ roofs (UV1.x = style 0 thatch, 1 clay tile, 2 slate, 3 metal/tar)
m = new_mat('M_LifeRoof')
g = G(m)
uvr = g.mul(g.uv(0), 0.25)
tx = g.tex('RoofTex', False, uvr)
tn = g.tex('RoofN', False, uvr)
style = g.mask(g.uv(1), '', r=True)
wth = band(g, style, 0)
wtl = band(g, style, 1)
wsl = band(g, style, 2)
wmt = band(g, style, 3)
msk = g.add(g.add(g.mul(tx, wth, 'R'), g.mul(tx, wtl, 'G')), g.add(g.mul(tx, wsl, 'B'), g.mul(tx, wmt, 'A')))
vc, vcl = g.vcol_linear()
out(m, g.mul(vcl, msk), MP.MP_BASE_COLOR)
nth = g.normal_xy(tn, 'rg')
ntl = g.normal_xy(tn, 'ba')
flat = g.vec(0.0, 0.0, 1.0)
out(m, g.lerp(g.lerp(flat, nth, wth), ntl, wtl), MP.MP_NORMAL)
out(m, g.lerp(g.lerp(0.9, 0.62, wsl), 0.45, wmt), MP.MP_ROUGHNESS)
out(m, g.lerp(0.35, 0.5, wsl), MP.MP_SPECULAR)
finish(m)

# ------------------------------------------------------------------ windows: dark glass by day, vertex-colour glow at night
m = new_mat('M_LifeWindow')
g = G(m)
vc, vcl = g.vcol_linear()
out(m, g.vec(0.025, 0.03, 0.035), MP.MP_BASE_COLOR)
out(m, g.const(0.12), MP.MP_ROUGHNESS)
out(m, g.const(0.6), MP.MP_SPECULAR)
glow = g.mul(g.mul(vcl, g.scalar('Night', 0.0)), g.scalar('Glow', 6.0))
out(m, glow, MP.MP_EMISSIVE_COLOR)
finish(m)

# ------------------------------------------------------------------ lamps and other emissive bits
m = new_mat('M_LifeLamp')
g = G(m)
vc, vcl = g.vcol_linear()
out(m, g.mul(vcl, 0.3), MP.MP_BASE_COLOR)
out(m, g.const(0.5), MP.MP_ROUGHNESS)
out(m, g.mul(g.mul(vcl, g.scalar('Night', 0.0)), g.scalar('Glow', 30.0)), MP.MP_EMISSIVE_COLOR)
finish(m)

# ------------------------------------------------------------------ glow pools: additive, unlit, invisible by day
m = new_mat('M_LifeGlow')
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
g = G(m)
vc, vcl = g.vcol_linear()
out(m, g.mul(g.mul(vcl, g.scalar('Night', 0.0)), g.scalar('Glow', 1.5)), MP.MP_EMISSIVE_COLOR)
finish(m)

unreal.log('LIFEMAT done')
