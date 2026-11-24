# DEMIURGE - import every Blender asset family (art/*/manifest.json) into /Game/Assets/<Family>/.
#
# Two halves in one file:
#   PLAN     pure Python (no Unreal): reads every manifest, probes every FBX (material slots, LOD group), resolves
#            textures and material instances, and checks coverage. Run it anywhere:
#                python Unreal/Scripts/assets/import_assets.py --plan [--families age2,ships] [--json out.json]
#   EXECUTE  inside the editor (headless commandlet; import_assets.ps1 runs it):
#                UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<this file>" -unattended -nosplash
#            Options come from environment variables (the commandlet's -script= argument cannot carry them safely
#            through a path with spaces):
#                DMI_FAMILIES=age2,ships   only these families (default: all, then the shrine)
#                DMI_MATCH=House_A         only meshes whose name contains this text (comma list)
#                DMI_LIMIT=5               at most N meshes per family (quick checks)
#                DMI_LOD_PROFILE=game|art  LOD screen sizes: game (default; tuned for the god camera) or the manifests'
#                DMI_NANITE=1              enable Nanite on buildings, wonders, culture overlays and hard-surface props
#                DMI_SKELETAL=0            skip the animal skeletal meshes and their animations (default: import)
#                DMI_SHRINE=0              skip the shrine (art/tools/ue_import.py, run in the same session)
#                DMI_ONLY_MATS=1           textures and materials only
#                DMI_DRY=1                 plan inside the editor, import nothing
#
# What it creates (re-runnable: textures re-import in place, master graphs are rebuilt, instances are reused and
# re-parameterised, meshes are deleted and imported fresh at the same path):
#   /Game/Assets/Materials/MPC_DmAssets           shared frame: PlanetCenter, SunDir, NightOverride, Glow (the towns /
#                                                 nature actors write it once per frame; see docs/ASSET-INTEGRATION.md)
#   /Game/Assets/Materials/M_DmKit                opaque master for every family (towns custom-data contract)
#   /Game/Assets/Materials/M_DmKitFoliage         masked two-sided master (leaf cards, impostors, hedges, crops)
#   /Game/Assets/<Family>/Textures/T_*            D sRGB, N normal (green flipped: OpenGL sources), ORM / M linear masks
#   /Game/Assets/<Family>/Materials/MI_*          one base instance per family material (MI_<Family>_<Set>), and the
#                                                 culture / climate / biome instances named exactly as the manifests do
#   /Game/Assets/<Family>/Meshes/SM_*             static mesh, LOD0..LODn, collision, slots -> base instances
#   /Game/Assets/Animal/Skeletal/SK_*             skeletal animals with their animation takes
#   /Game/Assets/Shrine/SM_Shrine_*               via art/tools/ue_import.py (its own masters, /Game/Assets/Materials)
# Orientation: every mesh is normalised at import so that buildings face -Y (the towns factory convention: door /
# street side toward -Y, long axis along X) and movers (ships, land units, animals) face +X (the towns unit drawing
# and the nature critters move along +X). The yaw is calibrated per family on the first mesh whose manifest bounds
# are asymmetric (a probe import), so a family exported with other axis settings still comes out right.
# Logs "DMI ..." lines; writes <project>/Saved/AssetImport/report.json (per mesh: LODs, triangles, bounds, scale and
# facing checks, slots) and plan.json.
import json
import math
import os
import re
import sys
import time

# ============================================================================================== paths


def _find_root():
    env = os.environ.get('DMI_ROOT')
    if env and os.path.isdir(os.path.join(env, 'art')):
        return os.path.abspath(env)
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(8):
        if os.path.isdir(os.path.join(d, 'art')) and os.path.exists(os.path.join(d, 'AGENTS.md')):
            return d
        d = os.path.dirname(d)
    raise RuntimeError('cannot find the demiurge root (a folder with art/ and AGENTS.md); set DMI_ROOT')


ROOT = _find_root()
ART = os.path.join(ROOT, 'art')
DST = '/Game/Assets'
MASTERS = DST + '/Materials'

# ============================================================================================== families
# key: art folder; ue: /Game/Assets/<ue>; cls: LOD / collision / facing class
FAMILIES = {
    'age0': {'ue': 'Age0', 'cls': 'building'},
    'age1': {'ue': 'Age1', 'cls': 'building'},
    'age2': {'ue': 'Age2', 'cls': 'building'},
    'age3': {'ue': 'Age3', 'cls': 'building'},
    'age4': {'ue': 'Age4', 'cls': 'building'},
    'age5': {'ue': 'Age5', 'cls': 'building'},
    'age6': {'ue': 'Age6', 'cls': 'building'},
    'culture': {'ue': 'Culture', 'cls': 'building'},
    'wonders-a': {'ue': 'Wonder', 'cls': 'wonder'},
    'wonders-b': {'ue': 'Wonder', 'cls': 'wonder'},
    'props': {'ue': 'Prop', 'cls': 'prop'},
    'ships': {'ue': 'Ship', 'cls': 'mover'},
    'units-land': {'ue': 'Unit', 'cls': 'mover'},
    'animals': {'ue': 'Animal', 'cls': 'animal'},
    'trees': {'ue': 'Tree', 'cls': 'tree'},
}
ORDER = ['age0', 'age1', 'age2', 'age3', 'age4', 'age5', 'age6', 'culture', 'wonders-a', 'wonders-b', 'props',
         'ships', 'units-land', 'animals', 'trees']

# target front direction in Unreal (degrees of yaw: 0 = +X, 90 = +Y, 270 = -Y)
TARGET_FRONT = {'building': 270.0, 'wonder': 270.0, 'prop': 270.0, 'tree': 270.0, 'mover': 0.0, 'animal': 0.0}
# Facing calibration (see the header). FACING_A is the yaw that the plain FBX import adds on top of the visual
# identity mapping Blender (x, y, z) -> Unreal (x, -y, z); measured on the first asymmetric mesh of each family and
# defaulting to DEFAULT_A where a family has no usable bounds. Set an entry here to pin a family by hand.
FACING_PIN = {}
DEFAULT_A = 0.0

# LOD screen sizes (fraction of the screen the bounds sphere covers). The manifests' 1.0 / 0.35 / 0.12 / 0.04 were
# chosen for a 40 m look; the god camera sees a house from 0.3-3 km, where those values would put every building on
# its hull. The game profile switches by projected size instead (at 1080p: LOD1 below ~130 px, LOD2 below ~43 px,
# LOD3 below ~13 px), so detail follows the screen at every zoom.
GAME_SCREEN = {
    'building': [1.0, 0.12, 0.04, 0.012], 'wonder': [1.0, 0.16, 0.05, 0.015], 'prop': [1.0, 0.10, 0.035, 0.012],
    'mover': [1.0, 0.10, 0.035, 0.012], 'animal': [1.0, 0.08, 0.025, 0.008], 'tree': [1.0, 0.22, 0.075, 0.025],
}
COLLISION = {'building': 'box', 'wonder': 'box', 'prop': 'none', 'mover': 'none', 'animal': 'none', 'tree': 'none'}
NANITE_CLASSES = ('building', 'wonder', 'prop')

# ============================================================================================== small helpers


def load_json(p):
    with open(p, encoding='utf-8') as f:
        return json.load(f)


def stem(ref):
    """'art/age2/textures/T_Age2_Ashlar_D.png' / 'T_X_D.png' / 'T_X_D' -> 'T_X_D'."""
    return os.path.splitext(os.path.basename(str(ref)))[0]


def tex_kind(s):
    if s.endswith('_ORM'):
        return 'ORM'
    if s.endswith('_N') or s.endswith('ImpostorsN'):
        return 'N'
    if s.endswith('_M'):
        return 'M'
    return 'D'


def srgb_hex_to_linear(h):
    h = h.lstrip('#')
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return out


def rgb(v, default=(1.0, 1.0, 1.0)):
    if v is None:
        return list(default)
    if isinstance(v, str) and v.startswith('#'):
        return srgb_hex_to_linear(v)
    if isinstance(v, (list, tuple)) and len(v) >= 3:
        return [float(v[0]), float(v[1]), float(v[2])]
    return list(default)


def ratio(a, b):
    return [a[i] / b[i] if abs(b[i]) > 1e-6 else 1.0 for i in range(3)]


SEL = {'R': [1, 0, 0, 0], 'G': [0, 1, 0, 0], 'B': [0, 0, 1, 0], 'A': [0, 0, 0, 1], '': [0, 0, 0, 0]}

# ============================================================================================== FBX probe


_RX_MAT = re.compile(rb'([A-Za-z0-9_@.\-]{1,96})\x00\x01Material')
_RX_MODEL = re.compile(rb'([A-Za-z0-9_@.\-]{1,96})\x00\x01Model')


def probe_fbx(path):
    """Material slot names, model names and whether the file holds an FBX LodGroup (binary FBX only)."""
    with open(path, 'rb') as f:
        b = f.read()
    mats = []
    for m in _RX_MAT.finditer(b):
        n = m.group(1).decode('ascii', 'replace')
        if n not in mats:
            mats.append(n)
    models = [m.group(1).decode('ascii', 'replace') for m in _RX_MODEL.finditer(b)]
    return {
        'binary': b[:18] == b'Kaydara FBX Binary',
        'materials': mats,
        'lodgroup': b'LodGroup' in b,
        'lod_children': sorted(set(x for x in models if re.search(r'_LOD\d+$', x))),
        'sockets': sorted(set(x for x in models if x.startswith('SOCKET_'))),
        'ucx': sum(1 for x in models if x.startswith('UCX_')),
        'armature': 'Armature' in models,
    }

# ============================================================================================== plan model


class Plan:
    def __init__(self):
        self.textures = {}   # stem -> {file, kind, family_ue}
        self.mis = {}        # name -> {family_ue, parent, tex, scalars, vectors, note}
        self.meshes = []     # dicts
        self.skeletal = []   # dicts
        self.issues = []
        self.notes = []
        self.filtered = False   # a --match / --limit run: unused variant parents are expected

    def issue(self, fam, msg):
        self.issues.append('%s: %s' % (fam, msg))

    def to_json(self):
        return {'root': ROOT, 'textures': self.textures, 'material_instances': self.mis, 'meshes': self.meshes,
                'skeletal': self.skeletal, 'issues': self.issues, 'notes': self.notes}


def new_mi(parent, family_ue, tex=None, scalars=None, vectors=None, note=''):
    return {'family_ue': family_ue, 'parent': parent, 'tex': dict(tex or {}), 'scalars': dict(scalars or {}),
            'vectors': dict(vectors or {}), 'note': note}


# ---------------------------------------------------------------------------------------------- family presets
# How each family's masks and vertex colours map onto M_DmKit (see master_hlsl below for every parameter):
#   NightSel / CultureSel / AccentSel / MossSel / AlwaysSel / EmisSel : RGBA channel selectors on the M texture
#   VCSootSel / VCAOSel / VCCultureSel / VCMossSel / RandVCSel         : RGBA selectors on the vertex colour
#   RandMSel + RandInvert + NightBinary + UseRand                      : per-window random lit test
#   VCMul (multiply vertex RGB), VCDecode (vertex colour stored as sRGB bytes), TintChannel (0 none, 1 towns wall
#   tint, 2 towns roof tint from the instance custom data), CdMode (0 towns, 1 nature tree, 2 nature grazer)


def role_of(slot, material):
    s = (slot or '') + ' ' + (material or '')
    if 'Roof' in s or 'Slate' in s or 'Thatch' in s or 'Shingle' in s or 'Tile' in s:
        return 'Roof'
    if 'Detail' in s or 'Trim' in s:
        return 'Detail'
    return 'Structure'


def family_preset(fam, material, role, tex):
    hasM = 1.0 if tex.get('M') else 0.0
    sc = {'HasD': 1.0 if tex.get('D') else 0.0, 'HasM': hasM, 'HasORM': 1.0 if tex.get('ORM') else 0.0, 'HasN': 1.0 if tex.get('N') else 0.0,
          'TintChannel': {'Structure': 1.0, 'Roof': 2.0}.get(role, 0.0)}
    ve = {}
    if fam == 'age0':
        ve.update(NightSel=SEL['R'], CultureSel=SEL['G'], MossSel=SEL['B'], AlwaysSel=SEL['A'])
        sc.update(AlwaysStrength=4.0, MossAmount=0.35)
    elif fam == 'age1':
        ve.update(NightSel=SEL['R'])
        sc.update(VCMul=1.0, VCDecode=1.0)
    elif fam == 'age2':
        ve.update(NightSel=SEL['R'])
        sc.update(VCMul=1.0)
    elif fam == 'age3':
        ve.update(NightSel=SEL['R'] if role == 'Detail' else SEL[''], CultureSel=SEL['G'])
        sc.update(VCMul=1.0, VCDecode=1.0)
    elif fam == 'age4':
        ve.update(NightSel=SEL['R'], VCSootSel=SEL['G'], VCCultureSel=SEL['B'], RandVCSel=SEL['R'])
        sc.update(UseRand=1.0)
    elif fam == 'age5':
        ve.update(NightSel=SEL['R'], CultureSel=SEL['G'], RandMSel=SEL['R'], VCSootSel=SEL['R'], VCAOSel=SEL['G'])
        sc.update(UseRand=1.0, RandInvert=1.0, NightBinary=1.0)
    elif fam == 'age6':
        ve.update(NightSel=SEL['R'], CultureSel=SEL['G'], AccentSel=SEL['B'])
        sc.update(VCMul=1.0)
    elif fam == 'culture':
        ve.update(NightSel=SEL['R'], AccentSel=SEL['G'])
    elif fam == 'wonders-a':
        ve.update(NightSel=SEL['R'], CultureSel=SEL['G'])
        sc.update(DayGlow=0.12)
    elif fam == 'wonders-b':
        ve.update(NightSel=SEL['R'], EmisSel=SEL['G'], AccentSel=SEL['B'])
        sc.update(DayGlow=0.0)
    elif fam == 'props':
        ve.update(VCMossSel=SEL['A'])
        sc.update(VCMul=1.0, VCDecode=1.0, TintChannel=0.0)
    elif fam in ('ships', 'units-land'):
        ve.update(NightSel=SEL['R'], CultureSel=SEL['G'], AlwaysSel=SEL['B'])
        sc.update(AlwaysStrength=3.0, TintChannel=0.0)
    elif fam == 'animals':
        sc.update(CdMode=2.0, TintChannel=0.0)
    elif fam == 'trees':
        sc.update(CdMode=1.0, TintChannel=0.0)
    return sc, ve


# map the manifests' parameter spellings onto M_DmKit
_SCALAR_KEYS = {'RoughAdd': 'RoughAdd', 'RoughnessAdd': 'RoughAdd', 'rough_add': 'RoughAdd', 'roughness_offset': 'RoughAdd',
                'roughness_scale': 'RoughScale', 'RoughnessScale': 'RoughScale', 'saturation': 'Saturation',
                'Desaturate': 'Desaturate', 'MossAmount': 'MossAmount', 'metallic': 'MetalOverride',
                'roughness': 'RoughOverride', 'Night': 'NightStrength', 'NightGlow': 'NightStrength',
                'FireGlow': 'AlwaysStrength', 'RoofBand': 'VOffsetBand'}
_VECTOR_KEYS = {'Tint': 'Tint', 'tint': 'Tint', 'tint_linear': 'Tint', 'MossColor': 'MossColor', 'accent': 'AccentColor',
                'AccentTint': 'AccentColor', 'CultureTint': 'CultureColor', 'CultureColor': 'CultureColor',
                'BaseTint': 'Tint', 'EmissiveA': 'WindowColor', 'EmissiveB': 'EmisColor2'}
_TEX_KEYS = ('texture_override', 'BaseColorTexture')


def mi_params(src):
    sc, ve, tx = {}, {}, {}
    if not isinstance(src, dict):
        return sc, ve, tx
    flat = dict(src)
    if isinstance(src.get('params'), dict):
        flat.update(src['params'])
    for k, v in flat.items():
        if k in _SCALAR_KEYS and isinstance(v, (int, float)):
            sc[_SCALAR_KEYS[k]] = float(v)
        elif k in _VECTOR_KEYS and (isinstance(v, (list, tuple)) or (isinstance(v, str) and v.startswith('#'))):
            ve[_VECTOR_KEYS[k]] = rgb(v) + [0.0]
        elif k in _TEX_KEYS and v:
            if str(v).endswith('_*'):   # a whole texture set ('T_Age2_Slate_*')
                tx['SET'] = stem(str(v)[:-2])
            else:
                tx['D'] = stem(v)
    if 'VOffsetBand' in sc:
        sc['VOffset'] = 0.25 * sc.pop('VOffsetBand')
    if 'AccentColor' in ve and 'Tint' in ve and 'BaseTint' in flat:
        # wonders-b: BaseColor = D * lerp(BaseTint, AccentTint, M.B): the master multiplies Tint by AccentColor there
        ve['AccentColor'] = ratio(ve['AccentColor'], ve['Tint']) + [0.0]
    return sc, ve, tx


# ---------------------------------------------------------------------------------------------- generic walkers


def iter_meshes(fam, man):
    if fam == 'age4':
        for aname, a in man['assets'].items():
            for state, st in a.get('states', {}).items():
                e = {k: v for k, v in a.items() if k != 'states'}
                e.update(st)
                e['state'] = state
                if isinstance(st.get('height'), (int, float)):
                    e['height_m'] = float(st['height'])   # per-state height (the asset's is the Intact one)
                yield st['mesh'], e
        return
    ms = man.get('meshes')
    if isinstance(ms, dict):
        for k, v in ms.items():
            yield k, v
    elif isinstance(ms, list):
        for v in ms:
            yield v['name'], v


def find_fbx(fam, name, e):
    for k in ('fbx', 'export'):
        v = e.get(k)
        if isinstance(v, str) and v.lower().endswith('.fbx'):
            return os.path.join(ROOT, v) if not os.path.isabs(v) else v
    ex = e.get('exports')
    if isinstance(ex, dict):
        for v in ex.values():
            if isinstance(v, str) and stem(v) == name:
                return os.path.join(ROOT, v)
    return os.path.join(ART, fam, 'export', name + '.fbx')


def find_skeletal(fam, e):
    ex = e.get('exports')
    if isinstance(ex, dict):
        for v in ex.values():
            if isinstance(v, str) and os.path.basename(v).startswith('SK_') and v.lower().endswith('.fbx'):
                return os.path.join(ROOT, v)
    sk = e.get('skeletal_mesh')
    if sk:
        p = os.path.join(ART, fam, 'export', sk + '.fbx')
        if os.path.exists(p):
            return p
    return None


def bounds_of(fam, e):
    """Blender-space (min xyz, max xyz) in metres, or None."""
    if 'bounds_min' in e and 'bounds_max' in e:
        return list(map(float, e['bounds_min'])), list(map(float, e['bounds_max']))
    if 'bounds_lod0' in e and len(e['bounds_lod0']) == 6:
        b = e['bounds_lod0']
        return [b[0], b[1], b[2]], [b[3], b[4], b[5]]
    if 'bbox_m' in e and len(e['bbox_m']) == 6:
        b = e['bbox_m']
        return [b[0], b[1], b[2]], [b[3], b[4], b[5]]
    b = e.get('bounds_m')
    if isinstance(b, dict) and all(k in b for k in 'xyz'):
        return [b['x'][0], b['y'][0], b['z'][0]], [b['x'][1], b['y'][1], b['z'][1]]
    if isinstance(b, list) and len(b) == 6:
        if fam == 'age2':   # xmin, xmax, ymin, ymax, zmin, zmax
            return [b[0], b[2], b[4]], [b[1], b[3], b[5]]
        return [b[0], b[1], b[2]], [b[3], b[4], b[5]]
    return None


def tris_of(e):
    for k in ('triangles', 'triangles_per_lod', 'tris', 'tris_per_lod'):
        v = e.get(k)
        if isinstance(v, list):
            return [int(x) for x in v]
        if isinstance(v, dict):
            return [int(v[x]) for x in sorted(v) if x.startswith('LOD')]
    return []


def slot_map_of(e):
    """Manifest slot -> material or instance name (Structure / Roof / Detail ...)."""
    out = {}
    for key in ('materials', 'material_slots', 'slots'):
        v = e.get(key)
        if isinstance(v, dict):
            for s, m in v.items():
                if isinstance(m, str):
                    out[s] = m
        elif isinstance(v, list):
            for i, m in enumerate(v):
                if isinstance(m, dict) and 'slot' in m and isinstance(m.get('material'), str):
                    out[m['slot']] = m['material']
                elif isinstance(m, str) and m.startswith('M_'):
                    out[['Structure', 'Roof', 'Detail'][i] if i < 3 else 'Slot%d' % i] = m
    if isinstance(e.get('material_names'), list):
        for i, m in enumerate(e['material_names']):
            out.setdefault(['Structure', 'Roof', 'Detail'][i], m)
    return out


def height_of(e):
    for k in ('height_m', 'height'):
        if isinstance(e.get(k), (int, float)):
            return float(e[k])
    return None

# ---------------------------------------------------------------------------------------------- per-family registries


def texture_index(fam):
    d = os.path.join(ART, fam, 'textures')
    out = {}
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            if f.lower().endswith('.png'):
                out[stem(f)] = os.path.join(d, f)
    return out


def explicit_material_textures(fam, man):
    """material name -> [texture stems] wherever a manifest lists them."""
    reg = {}

    def add(mat, refs):
        if not isinstance(mat, str):
            return
        if isinstance(refs, dict):
            refs = list(refs.values())
        if isinstance(refs, list):
            ss = [stem(r) for r in refs if isinstance(r, str)]
            if ss:
                reg.setdefault(mat, [])
                for s in ss:
                    if s not in reg[mat]:
                        reg[mat].append(s)
    for sec in ('materials', 'masters'):
        v = man.get(sec)
        if isinstance(v, dict):
            for mat, d in v.items():
                if isinstance(d, dict):
                    add(mat, d.get('textures'))
    for _n, e in iter_meshes(fam, man):
        t = e.get('textures')
        if isinstance(t, dict) and all(isinstance(k, str) and k.startswith('M_') for k in t):
            for mat, refs in t.items():
                add(mat, refs)
        mats = e.get('materials')
        if isinstance(mats, list):
            for m in mats:
                if isinstance(m, dict) and m.get('textures'):
                    add(m.get('material'), m['textures'])
    return reg


def leftover_sets(fam, man, texidx, explicit):
    """Materials with no textures of their own: take the texture set a mesh lists that no other material of that mesh
    claims (props crops and foliage, the land units' shared trim sheet)."""
    out = {}
    for _n, e in iter_meshes(fam, man):
        t = e.get('textures')
        if not isinstance(t, list):
            continue
        stems = [stem(x) for x in t if isinstance(x, str)]
        sets = sorted(set(re.sub(r'_(D|N|ORM|M)$', '', x) for x in stems))
        mats = [v for v in slot_map_of(e).values() if v.startswith('M_')]
        claimed = set()
        for mat in mats:
            r = resolve_textures(fam, mat, texidx, explicit, '', fallback=False)
            if r.get('D'):
                claimed.add(re.sub(r'_(D|N|ORM|M)$', '', r['D']))
        free = [x for x in sets if x not in claimed and (x + '_D') in texidx]
        lacking = [mat for mat in mats if not resolve_textures(fam, mat, texidx, explicit, '', fallback=False).get('D')]
        if len(free) == 1:
            for mat in lacking:
                out.setdefault(mat, free[0])
        elif len(free) > 1 and len(lacking) == 1:
            out.setdefault(lacking[0], free[0])
    return out


def resolve_textures(fam, mat, texidx, explicit, fam_ue, fallback=True, leftovers=None):
    """{D,N,ORM,M: stem} for a family material."""
    out = {}
    refs = explicit.get(mat)
    if refs:
        for s in refs:
            k = tex_kind(s)
            if s in texidx and k not in out:
                out[k] = s
    if not out:
        base = mat[2:] if mat.startswith('M_') else mat
        for cand in (base, re.sub(r'Tile$', '', base)):
            for k in ('D', 'N', 'ORM', 'M'):
                s = 'T_%s_%s' % (cand, k)
                if s in texidx and k not in out:
                    out[k] = s
            if out:
                break
    if not out.get('D') and leftovers and mat in leftovers:
        base = leftovers[mat]
        for k in ('D', 'N', 'ORM', 'M'):
            if '%s_%s' % (base, k) in texidx:
                out.setdefault(k, '%s_%s' % (base, k))
    return out


# ============================================================================================== the planner


def plan_family(fam, P, only_match=None, limit=None):
    info = FAMILIES[fam]
    fam_ue, cls = info['ue'], info['cls']
    if fam == 'trees':
        return plan_trees(P, only_match, limit)
    man = load_json(os.path.join(ART, fam, 'manifest.json'))
    texidx = texture_index(fam)
    for s, p in texidx.items():
        P.textures[s] = {'file': p, 'kind': tex_kind(s), 'family_ue': fam_ue}
    explicit = explicit_material_textures(fam, man)
    leftovers = leftover_sets(fam, man, texidx, explicit)
    foliage_mats = {'M_Prop_Foliage', 'M_Prop_Crop'}
    if fam == 'animals':   # coat instances carry their own baked textures: define them before the meshes use them
        for _n, e in iter_meshes(fam, man):
            base = (e.get('materials') or {}).get('Structure')
            if base and base not in P.mis:
                t = e.get('textures') or {}
                tex = {k: stem(v) for k, v in t.items() if k in ('D', 'N', 'ORM')}
                sc, ve = family_preset(fam, base, 'Structure', tex)
                P.mis[base] = new_mi('master:opaque', fam_ue, tex, sc, ve, 'coat of %s' % _n)

    def base_mi_for_material(mat, role):
        name = 'MI_' + mat[2:]
        if name in P.mis:
            return name
        tex = resolve_textures(fam, mat, texidx, explicit, fam_ue, leftovers=leftovers)
        sc, ve = family_preset(fam, mat, role, tex)
        parent = 'master:foliage' if mat in foliage_mats else 'master:opaque'
        if mat == 'M_Wonder_LightShaft':   # additive light shaft: no textures (wonders-b manifest)
            d = (man.get('materials') or {}).get(mat, {}).get('params', {})
            P.mis[name] = new_mi('master:glow', fam_ue, {}, {'EmissiveDay': float(d.get('EmissiveDay', 6.0)), 'EmissiveNight': float(d.get('EmissiveNight', 10.0))},
                                 {'Color': rgb(d.get('Color', '#fff1c8')) + [0.0]}, 'additive light shaft')
            return name
        if mat == 'M_Prop_Glow':
            sc.update(GlowOnly=1.0, HasM=0.0)
            ve['GlowColor'] = [1.0, 0.55, 0.2, 0.0]
        if mat == 'M_Prop_Rock':   # baked normal / AO on UV0, tiling base colour on UV1 (props README)
            sc.update(BaseUV1=1.0, MossAmount=0.6)
        if fam == 'age6' and mat == 'M_Age6_RoofMetal':
            ve['Tint'] = [0.62, 0.62, 0.62, 0.0]
        if not tex.get('D') and mat != 'M_Prop_Glow':
            P.issue(fam, 'no base-colour texture for %s (textures %s)' % (mat, sorted(tex.values())))
        P.mis[name] = new_mi(parent, fam_ue, tex, sc, ve, 'base instance of %s' % mat)
        return name

    count = 0
    for name, e in iter_meshes(fam, man):
        if only_match and not any(m in name for m in only_match):
            continue
        if limit and count >= limit:
            break
        fbx = find_fbx(fam, name, e)
        if not os.path.exists(fbx):
            P.issue(fam, 'missing FBX for %s: %s' % (name, os.path.relpath(fbx, ROOT)))
            continue
        pr = probe_fbx(fbx)
        smap = slot_map_of(e)
        slots = {}
        for s in pr['materials']:
            target = s if s.startswith(('M_', 'MI_')) else smap.get(s)
            if not target:
                P.issue(fam, '%s: FBX slot %s has no material in the manifest' % (name, s))
                continue
            if target.startswith('MI_'):
                slots[s] = target
                if target not in P.mis:
                    P.issue(fam, '%s: slot %s -> %s is not defined' % (name, s, target))
            else:
                role = s if s in ('Structure', 'Roof', 'Detail') else next((k for k, v in smap.items() if v == target), role_of('', target))
                slots[s] = base_mi_for_material(target, role_of(role, target) if role not in ('Structure', 'Roof', 'Detail') else role)
        # props rocks: baked per-asset normal / AO on UV0, tiling base colour on UV1
        if fam == 'props' and any(v == 'MI_Prop_Rock' for v in slots.values()):
            asset = name.replace('SM_Prop_', '').replace('_', '')
            n, orm = 'T_Prop_%s_N' % asset, 'T_Prop_%s_ORM' % asset
            if n in texidx:
                child = 'MI_Prop_Rock_' + asset
                P.mis[child] = new_mi('MI_Prop_Rock', fam_ue, {'N': n, 'ORM': orm} if orm in texidx else {'N': n}, {}, {},
                                      'baked normal / AO of %s' % name)
                for s, v in list(slots.items()):
                    if v == 'MI_Prop_Rock':
                        slots[s] = child
        bb = bounds_of(fam, e)
        mesh = {
            'family': fam, 'family_ue': fam_ue, 'cls': cls, 'name': name, 'fbx': fbx, 'lod_files': [],
            'lodgroup': pr['lodgroup'], 'lod_children': len(pr['lod_children']), 'sockets': pr['sockets'],
            'ucx': pr['ucx'], 'slots': slots, 'fbx_slots': pr['materials'], 'height_m': height_of(e),
            'bounds_m': bb, 'tris': tris_of(e), 'art_screen': e.get('lod_screen_sizes') or e.get('lod_screen_size') or man.get('lod_screen_sizes'),
            'state': e.get('state'), 'asset': e.get('asset'),
        }
        if not pr['lodgroup']:
            P.issue(fam, '%s: no LodGroup in the FBX (imports LOD0 only)' % name)
        P.meshes.append(mesh)
        count += 1
        sk = find_skeletal(fam, e) if fam == 'animals' else None
        if sk:
            P.skeletal.append({'family': fam, 'family_ue': fam_ue, 'name': stem(sk), 'fbx': sk, 'slots': slots,
                               'animations': [a.get('name') for a in e.get('animations', []) if isinstance(a, dict)]})

    plan_variants(fam, man, P, fam_ue, texidx)


def add_variant(P, fam, fam_ue, name, parent_base, sc=None, ve=None, tx=None, note=''):
    if parent_base not in P.mis:
        msg = '%s: variant %s skipped: its parent %s is not used by any planned mesh' % (fam, name, parent_base)
        if P.filtered:
            P.notes.append(msg)
        else:
            P.issue(fam, msg)
        return
    if name == parent_base:
        return
    tx = dict(tx or {})
    if 'SET' in tx:
        base = tx.pop('SET')
        for k in ('D', 'N', 'ORM', 'M'):
            if '%s_%s' % (base, k) in P.textures:
                tx[k] = '%s_%s' % (base, k)
    P.mis[name] = new_mi(parent_base, fam_ue, tx, sc or {}, ve or {}, note)


def plan_variants(fam, man, P, fam_ue, texidx):
    """Culture / climate / biome instances, named exactly as each manifest names them."""
    mi = man.get('material_instances')
    if fam in ('age0', 'age1', 'age2', 'age4') and isinstance(mi, dict):
        for name, d in mi.items():
            parent = d.get('parent', '')
            sc, ve, tx = mi_params(d)
            add_variant(P, fam, fam_ue, name, 'MI_' + parent[2:], sc, ve, tx, d.get('use') or d.get('culture') or '')
    elif fam == 'age3':
        for cul, d in man.get('cultures', {}).items():
            stone = rgb(d.get('StoneTint'))
            add_variant(P, fam, fam_ue, 'MI_Age3_Structure_' + cul, 'MI_Age3_Structure', {},
                        {'Tint': stone + [0.0], 'CultureColor': ratio(rgb(d.get('PlasterTint')), stone) + [0.0]}, None, d.get('note', ''))
            add_variant(P, fam, fam_ue, 'MI_Age3_Roof_' + cul, 'MI_Age3_Roof', {'VOffset': 0.25 * float(d.get('RoofBand', 0))},
                        {'Tint': rgb(d.get('RoofTint')) + [0.0]})
            add_variant(P, fam, fam_ue, 'MI_Age3_Detail_' + cul, 'MI_Age3_Detail', {}, {'CultureColor': rgb(d.get('PaintTint')) + [0.0]})
    elif fam == 'age5':
        for cul, d in man.get('cultures', {}).items():
            for slot in ('Structure', 'Roof', 'Detail'):
                add_variant(P, fam, fam_ue, 'MI_Age5_%s_%s' % (slot, cul), 'MI_Age5_' + slot, {'RoughAdd': float(d.get('RoughnessAdd', 0.0))},
                            {'Tint': rgb(d.get(slot + 'Tint')) + [0.0]})
    elif fam == 'age6':
        tinted = [m for m, d in man.get('materials', {}).items() if isinstance(d, dict) and d.get('culture_tinted')]
        for cul, d in man.get('culture_instances', {}).items():
            for m in tinted:
                base = 'MI_' + m[2:]
                if 'GreenRoof' in m:
                    ve = {'Tint': rgb(d.get('roof')) + [0.0]}
                else:
                    ve = {'CultureColor': rgb(d.get('tint')) + [0.0], 'AccentColor': rgb(d.get('accent')) + [0.0]}
                add_variant(P, fam, fam_ue, '%s_%s' % (base, cul), base, {}, ve, None, d.get('note', ''))
    elif fam == 'animals':
        for name, e in iter_meshes(fam, man):
            base = (e.get('materials') or {}).get('Structure')
            for biome, d in (e.get('biome_variants') or {}).items():
                add_variant(P, fam, fam_ue, '%s_%s' % (base, biome), base, {'RoughAdd': float(d.get('roughness_offset', 0.0))},
                            {'Tint': rgb(d.get('tint')) + [0.0]})
    elif fam == 'culture':
        pass  # handled up front (plan_culture_bases): its instances carry their own textures
    elif fam == 'props':
        for mat, variants in (mi or {}).items():
            for var, d in variants.items():
                sc, ve, tx = mi_params(d)
                if mat == 'M_Prop_Glow':
                    ve = {'GlowColor': rgb(d.get('tint')) + [0.0]}
                add_variant(P, fam, fam_ue, 'MI_%s_%s' % (mat[2:], var), 'MI_' + mat[2:], sc, ve, tx)
    elif fam == 'ships':
        for k, hx in man.get('cultures', {}).items():
            for slot in ('Structure', 'Roof', 'Detail'):
                add_variant(P, fam, fam_ue, 'MI_Ship_%s_%s' % (slot, k), 'MI_Ship_' + slot, {}, {'CultureColor': srgb_hex_to_linear(hx) + [0.0]})
    elif fam == 'units-land':
        for k, hx in man.get('cultures', {}).items():
            add_variant(P, fam, fam_ue, 'MI_Unit_Detail_%s' % k, 'MI_Unit_Detail', {}, {'CultureColor': srgb_hex_to_linear(hx) + [0.0]})
    elif fam == 'wonders-a':
        styles = (man.get('culture_variants') or {}).get('styles', {})
        for mat in man.get('materials', {}):
            for st, d in styles.items():
                add_variant(P, fam, fam_ue, 'MI_%s_%s' % (mat[2:], st), 'MI_' + mat[2:], {'RoughAdd': float(d.get('rough_add', 0.0))},
                            {'Tint': rgb(d.get('tint')) + [0.0]})
    elif fam == 'wonders-b':
        for mat, d in man.get('materials', {}).items():
            base = 'MI_' + mat[2:]
            if base in P.mis:
                sc, ve, _tx = mi_params(d.get('params') or {})
                P.mis[base]['vectors'].update(ve)
            for cul, inst in (d.get('instances') or {}).items():
                sc, ve, tx = mi_params(inst)
                add_variant(P, fam, fam_ue, inst.get('name') or '%s_%s' % (base, cul), base, sc, ve, tx, inst.get('label', ''))


def plan_culture_bases(P):
    """art/culture instances are complete materials (own textures): define them before the meshes reference them."""
    fam, fam_ue = 'culture', 'Culture'
    man = load_json(os.path.join(ART, fam, 'manifest.json'))
    texidx = texture_index(fam)
    for name, d in man.get('material_instances', {}).items():
        tex = {k: stem(v) for k, v in (d.get('textures') or {}).items() if k in ('D', 'N', 'ORM', 'M')}
        for k, s in tex.items():
            if s not in texidx:
                P.issue(fam, '%s: texture %s not found' % (name, s))
        role = 'Roof' if any(x in name for x in ('Dome', 'Shingle', 'Slate')) else ('Detail' if 'Trim' in name else 'Structure')
        sc, ve = family_preset(fam, name, role, tex)
        p_sc, p_ve, _ = mi_params(d)
        sc.update(p_sc)
        ve.update(p_ve)
        if 'shared water' in str(d.get('parent', '')):
            sc.update(RoughOverride=0.06, MetalOverride=0.0)
            ve.setdefault('Tint', [0.2, 0.45, 0.45, 0.0])
        P.mis[name] = new_mi('master:opaque', fam_ue, tex, sc, ve, d.get('note', ''))


def plan_trees(P, only_match=None, limit=None):
    fam, fam_ue = 'trees', 'Tree'
    man = load_json(os.path.join(ART, 'trees', 'export', 'manifest.json'))
    texidx = texture_index(fam)
    for s, p in texidx.items():
        P.textures[s] = {'file': p, 'kind': tex_kind(s), 'family_ue': fam_ue}
    P.mis['MI_Tree_Leaves'] = new_mi('master:foliage', fam_ue, {'D': 'T_TreeLeafAtlas'}, {'CdMode': 1.0, 'HasN': 0.0}, {}, 'shared leaf atlas')
    P.mis['MI_Tree_Impostor'] = new_mi('master:foliage', fam_ue, {'D': 'T_TreeImpostors', 'N': 'T_TreeImpostorsN'}, {'CdMode': 1.0, 'HasN': 1.0}, {},
                                       'impostor atlas (LOD3 cards)')
    screen = man.get('screen_sizes')
    count = 0
    for sp, variants in man['species'].items():
        Sp = sp.capitalize()
        bark = 'MI_Tree_Bark_' + Sp
        tex = {'D': 'T_Bark_' + Sp, 'N': 'T_Bark_%s_N' % Sp}
        sc, ve = family_preset(fam, bark, 'Structure', tex)
        sc['HasORM'] = 0.0
        P.mis[bark] = new_mi('master:opaque', fam_ue, tex, sc, ve, 'bark of %s' % sp)
        for v, e in variants.items():
            name = e['mesh']
            if only_match and not any(m in name for m in only_match):
                continue
            if limit and count >= limit:
                break
            d = os.path.join(ART, 'trees', 'export', Sp)
            fbx = os.path.join(d, name + '.fbx')
            if not os.path.exists(fbx):
                P.issue(fam, 'missing FBX %s' % fbx)
                continue
            lods = [os.path.join(d, '%s_LOD%d.fbx' % (name, i)) for i in (1, 2, 3)]
            lods = [p for p in lods if os.path.exists(p)]
            pr = probe_fbx(fbx)
            slots = {}
            for s in pr['materials']:
                slots[s] = {'Bark': bark, 'Leaves': 'MI_Tree_Leaves', 'Impostor': 'MI_Tree_Impostor'}.get(s)
                if not slots[s]:
                    P.issue(fam, '%s: unknown slot %s' % (name, s))
            P.meshes.append({
                'family': fam, 'family_ue': fam_ue, 'cls': 'tree', 'name': name, 'fbx': fbx, 'lod_files': lods, 'lodgroup': False,
                'lod_children': 0, 'sockets': [], 'ucx': 0, 'slots': {k: v for k, v in slots.items() if v}, 'fbx_slots': pr['materials'],
                'height_m': e.get('height'), 'bounds_m': (e.get('bounds_min'), e.get('bounds_max')) if e.get('bounds_min') else None,
                'tris': e.get('tris_per_lod') or [], 'art_screen': screen, 'state': None, 'asset': sp, 'note': e.get('note'),
            })
            count += 1


def build_plan(families=None, only_match=None, limit=None):
    P = Plan()
    P.filtered = bool(only_match or limit)
    fams = [f for f in ORDER if not families or f in families]
    if 'culture' in fams:
        plan_culture_bases(P)
    for f in fams:
        try:
            plan_family(f, P, only_match, limit)
        except Exception as ex:   # a broken manifest must not stop the other families
            P.issue(f, 'planner failed: %r' % (ex,))
    # every referenced texture must exist
    for n, mi in P.mis.items():
        for k, s in mi['tex'].items():
            if s not in P.textures:
                P.issue(mi['family_ue'], '%s: texture %s (%s) not on disk' % (n, s, k))
    used = set()
    for m in P.meshes + P.skeletal:
        used.update(m['slots'].values())
    P.notes.append('%d meshes, %d skeletal, %d material instances (%d used by mesh slots), %d textures' % (
        len(P.meshes), len(P.skeletal), len(P.mis), len(used), len(P.textures)))
    return P

# ============================================================================================== the master shader (HLSL)

MASTER_HLSL = r'''
// M_DmKit surface (asset-integration). Inputs: D (rgba), ORM, M (masks), VC (vertex colour), Cd0..Cd7 (towns
// instance data), WP (world position), frame (PlanetCenter, SunDir, NightOverride, Glow) and the parameters.
float4 vc = VC;
if (VCDecode > 0.5) vc.rgb = select(vc.rgb <= 0.04045, vc.rgb / 12.92, pow((vc.rgb + 0.055) / 1.055, 2.4));   // (towns4) select: Nanite compiles HLSL 2021, which rejects a vector ternary
float4 m = M * HasM;
float3 base = lerp(float3(1, 1, 1), D.rgb, HasD) * Tint.rgb;
if (GlowOnly > 0.5) base = GlowColor.rgb * 0.15;
// saturation (age2 'saturation', age4 'Desaturate')
float l0 = dot(base, float3(0.2126, 0.7152, 0.0722));
base = lerp(l0.xxx, base, Saturation * (1.0 - Desaturate));
// culture colour on the culture mask (M or vertex), accent colour on the accent mask
float cm = saturate(dot(m, CultureSel) + dot(vc, VCCultureSel));
base *= lerp(float3(1, 1, 1), CultureColor.rgb, cm * CultureAmt);
float am = saturate(dot(m, AccentSel));
base *= lerp(float3(1, 1, 1), AccentColor.rgb, am);
// vertex colour multiplier (grime, variation)
base *= lerp(float3(1, 1, 1), vc.rgb, VCMul);
// towns instance tints (custom data 0..2 wall, 3..5 roof), soft so the texture reads
float3 wallT = float3(Cd0, Cd1, Cd2), roofT = float3(Cd3, Cd4, Cd5);
if (CdMode < 0.5)
{
    float3 it = (TintChannel > 1.5) ? roofT : ((TintChannel > 0.5) ? wallT : float3(1, 1, 1));
    base *= lerp(float3(1, 1, 1), it, TintAmt);
}
else if (CdMode < 1.5)
{
    // nature trees: 0 burn, 1 snow, 2 dry
    base = lerp(base, base * float3(1.15, 0.95, 0.6), saturate(Cd2) * 0.5);
    base = lerp(base, base * 0.12, saturate(Cd0));
}
else
{
    base *= float3(Cd0, Cd1, Cd2);   // nature grazer coat tint
}
// moss / grime (M or vertex), stronger on faces that look up
float mo = saturate(dot(m, MossSel) + dot(vc, VCMossSel)) * MossAmount * saturate(0.35 + Up);
base = lerp(base, MossColor.rgb * (0.6 + 0.8 * l0), mo);
// soot / char (vertex), burnt or ruined state (custom data 7, towns only)
float soot = saturate(dot(vc, VCSootSel));
base = lerp(base, base * 0.12, soot);
float st = (CdMode < 0.5) ? saturate(Cd7) : 0.0;
float lb = dot(base, float3(0.2126, 0.7152, 0.0722));
base = lerp(base, lb.xxx * float3(0.55, 0.5, 0.46), st * 0.6);
// roughness, metallic, occlusion
float3 orm = lerp(float3(1.0, 0.8, 0.0), ORM.rgb, HasORM);
Rough = saturate(orm.g * RoughScale + RoughAdd);
if (RoughOverride >= 0.0) Rough = RoughOverride;
Metal = saturate(orm.b * MetalScale);
if (MetalOverride >= 0.0) Metal = MetalOverride;
float aov = lerp(1.0, dot(vc, VCAOSel), saturate(dot(VCAOSel, float4(1, 1, 1, 1))));
AO = orm.r * aov;
// night: the sun below this point's horizon on the round planet (or the override), windows by instance lit fraction
float3 up = normalize(WP - Ctr);
float night = (Ov >= 0.0) ? saturate(Ov) : saturate((0.05 - dot(up, normalize(Sun))) * 6.0);
float lit = (CdMode < 0.5) ? saturate(Cd6) : 0.0;
float nm = saturate(dot(m, NightSel));
if (NightBinary > 0.5) nm = nm > 0.01 ? 1.0 : 0.0;
float r = saturate(dot(m, RandMSel) + dot(vc, RandVCSel));
if (RandInvert > 0.5) r = 1.0 - r;
float on = (UseRand > 0.5) ? ((r < lit) ? 1.0 : 0.0) : lit;
float3 wcol = lerp(WindowColor.rgb, EmisColor2.rgb, saturate(dot(m, EmisSel)));
float dayk = lerp(DayGlow, 1.0, night);
Emis = wcol * nm * on * dayk * NightStrength * Glow * (1.0 - st);
Emis += D.rgb * saturate(dot(m, AlwaysSel)) * AlwaysStrength * (1.0 - st);
if (GlowOnly > 0.5) Emis = GlowColor.rgb * GlowStrength * night * Glow * (1.0 - st);
return base;
'''

FOLIAGE_HLSL = r'''
// M_DmKitFoliage (asset-integration): leaf cards, impostors, hedges, crops. D.a is the mask.
float3 b = D.rgb * Tint.rgb;
b = lerp(b, b * float3(1.2, 0.92, 0.55), saturate(Cd2) * 0.6);   // dry / autumn (nature custom data 2)
b = lerp(b, b * 0.1, saturate(Cd0));                             // burnt (nature custom data 0)
float snow = saturate(Cd1) * saturate(Up * 1.5);
b = lerp(b, float3(0.85, 0.88, 0.92), snow);
SSS = b * Transmission.rgb * (1.0 - snow);
return b;
'''

# master parameters: name -> default (scalars) or [r, g, b, a] (vectors)
OPAQUE_SCALARS = {
    'HasD': 1.0, 'HasM': 0.0, 'HasORM': 1.0, 'HasN': 1.0, 'BaseUV1': 0.0, 'VOffset': 0.0, 'RoughAdd': 0.0, 'RoughScale': 1.0,
    'RoughOverride': -1.0, 'MetalScale': 1.0, 'MetalOverride': -1.0, 'Saturation': 1.0, 'Desaturate': 0.0,
    'VCMul': 0.0, 'VCDecode': 0.0, 'NightBinary': 0.0, 'UseRand': 0.0, 'RandInvert': 0.0, 'NightStrength': 3.0,
    'DayGlow': 0.0, 'AlwaysStrength': 0.0, 'MossAmount': 0.0, 'CultureAmt': 1.0, 'TintChannel': 0.0,
    'TintAmt': 0.3, 'CdMode': 0.0, 'GlowOnly': 0.0, 'GlowStrength': 6.0, 'Specular': 0.4,
}
OPAQUE_VECTORS = {
    'Tint': [1, 1, 1, 0], 'CultureColor': [1, 1, 1, 0], 'AccentColor': [1, 1, 1, 0], 'MossColor': [0.08, 0.10, 0.04, 0],
    'WindowColor': [1.0, 0.62, 0.28, 0], 'EmisColor2': [1.0, 0.45, 0.2, 0], 'GlowColor': [1.0, 0.55, 0.2, 0],
    'NightSel': [0, 0, 0, 0], 'CultureSel': [0, 0, 0, 0], 'AccentSel': [0, 0, 0, 0], 'MossSel': [0, 0, 0, 0],
    'AlwaysSel': [0, 0, 0, 0], 'EmisSel': [0, 0, 0, 0], 'RandMSel': [0, 0, 0, 0], 'VCSootSel': [0, 0, 0, 0],
    'VCAOSel': [0, 0, 0, 0], 'VCCultureSel': [0, 0, 0, 0], 'VCMossSel': [0, 0, 0, 0], 'RandVCSel': [0, 0, 0, 0],
}
GLOW_HLSL = r'''
// M_DmKitGlow (asset-integration): additive light shafts. Vertex colour R fades the shaft, the view angle softens it.
float night = (Ov >= 0.0) ? saturate(Ov) : saturate((0.05 - dot(normalize(WP - Ctr), normalize(Sun))) * 6.0);
float facing = saturate(abs(dot(normalize(N), normalize(V))));
return Color.rgb * lerp(EmissiveDay, EmissiveNight, night) * VC.r * facing * facing * Glow;
'''

FOLIAGE_SCALARS = {'HasN': 0.0, 'CdMode': 1.0, 'Roughness': 0.8, 'Specular': 0.15}
FOLIAGE_VECTORS = {'Tint': [1, 1, 1, 0], 'Transmission': [0.55, 0.7, 0.25, 0]}

# ============================================================================================== executor (Unreal)


def run_unreal():
    import unreal

    t_start = time.time()
    env = os.environ
    families = [f for f in env.get('DMI_FAMILIES', '').split(',') if f]
    match = [m for m in env.get('DMI_MATCH', '').split(',') if m]
    limit = int(env.get('DMI_LIMIT', '0') or 0) or None
    profile = env.get('DMI_LOD_PROFILE', 'game')
    nanite = env.get('DMI_NANITE', '0') == '1'
    skeletal = env.get('DMI_SKELETAL', '1') != '0'
    do_shrine = env.get('DMI_SHRINE', '1') != '0' and (not families or 'shrine' in families)
    only_mats = env.get('DMI_ONLY_MATS') == '1'
    dry = env.get('DMI_DRY') == '1'
    fams = [f for f in families if f != 'shrine'] or (None if not families else [])
    if families and fams == []:
        fams = ['__none__']

    AT = unreal.AssetToolsHelpers.get_asset_tools()
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    MP = unreal.MaterialProperty
    F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
    F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
    ERR = []
    WARN = []

    def log(m):
        unreal.log('DMI ' + m)

    def err(m):
        ERR.append(m)
        unreal.log_error('DMI ERROR ' + m)

    def warn(m):
        WARN.append(m)
        unreal.log_warning('DMI WARN ' + m)

    saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    outdir = os.path.join(saved, 'AssetImport')
    os.makedirs(outdir, exist_ok=True)

    P = build_plan(fams if fams else None, match or None, limit)
    with open(os.path.join(outdir, 'plan.json'), 'w', encoding='utf-8') as f:
        json.dump(P.to_json(), f, indent=1)
    log('plan: %s; %d issues' % ('; '.join(P.notes), len(P.issues)))
    for i in P.issues:
        warn('plan ' + i)
    if dry:
        return

    # legacy FBX path: FbxImportUI options (LOD groups, vertex colours, normals, UCX) apply only with Interchange FBX off
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')

    class _SMES:
        _inst = None

        def __getattr__(self, k):
            sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            if sub is None:
                if _SMES._inst is None:
                    _SMES._inst = unreal.new_object(unreal.StaticMeshEditorSubsystem)
                sub = _SMES._inst
            if hasattr(sub, k):
                return getattr(sub, k)
            return getattr(unreal.EditorStaticMeshLibrary, k)
    SMES = _SMES()

    def setp(obj, names, value):
        for n in names:
            try:
                obj.set_editor_property(n, value)
                return True
            except Exception:
                continue
        return False

    # ------------------------------------------------------------------ material parameter collection
    mpc_path = MASTERS + '/MPC_DmAssets'
    mpc = unreal.load_asset(mpc_path) if EAL.does_asset_exist(mpc_path) else None
    if not mpc:
        try:
            mpc = AT.create_asset('MPC_DmAssets', MASTERS, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
        except Exception as ex:
            mpc = None
            warn('cannot create MPC_DmAssets (%r): masters use plain parameters' % (ex,))
    if mpc:
        try:
            vp = []
            for n, d in (('PlanetCenter', (0.0, 0.0, 0.0, 0.0)), ('SunDir', (0.0, 0.0, 1.0, 0.0))):
                p = unreal.CollectionVectorParameter()
                p.set_editor_property('parameter_name', n)
                p.set_editor_property('default_value', unreal.LinearColor(*d))
                vp.append(p)
            sp = []
            for n, d in (('NightOverride', -1.0), ('Glow', 1.0)):
                p = unreal.CollectionScalarParameter()
                p.set_editor_property('parameter_name', n)
                p.set_editor_property('default_value', d)
                sp.append(p)
            mpc.set_editor_property('vector_parameters', vp)
            mpc.set_editor_property('scalar_parameters', sp)
            EAL.save_loaded_asset(mpc)
            log('collection ' + mpc.get_path_name())
        except Exception as ex:
            warn('MPC parameters failed (%r): masters use plain parameters' % (ex,))
            mpc = None

    # ------------------------------------------------------------------ textures
    tex_assets = {}
    t0 = time.time()
    for s, t in sorted(P.textures.items()):
        dest = '%s/%s/Textures' % (DST, t['family_ue'])
        task = unreal.AssetImportTask()
        task.filename = t['file']
        task.destination_path = dest
        task.destination_name = s
        task.replace_existing = True
        task.automated = True
        task.save = False
        task.factory = unreal.TextureFactory()   # the Interchange path asserts in a commandlet
        AT.import_asset_tasks([task])
        tex = unreal.load_asset(dest + '/' + s)
        if not tex:
            err('texture import failed ' + t['file'])
            continue
        k = t['kind']
        if k == 'N':
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('flip_green_channel', True)
            tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
        elif k in ('ORM', 'M'):
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        else:
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
            tex.set_editor_property('srgb', True)
            tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        EAL.save_loaded_asset(tex)
        tex_assets[s] = tex
    log('textures %d in %.0f s' % (len(tex_assets), time.time() - t0))

    def first_tex(kind, prefer=None):
        if prefer and prefer in tex_assets:
            return tex_assets[prefer]
        for s, t in sorted(P.textures.items()):
            if t['kind'] == kind and s in tex_assets:
                return tex_assets[s]
        return None

    # ------------------------------------------------------------------ master graphs
    class G:
        def __init__(self, m):
            self.m = m
            self.y = -600
            self.x = -2400

        def n(self, cls, **props):
            self.y += 110
            if self.y > 2400:
                self.y = -600
                self.x += 320
            e = MEL.create_material_expression(self.m, cls, self.x, self.y)
            for k, v in props.items():
                e.set_editor_property(k, v)
            return e

        def link(self, a, a_out, b, b_in):
            if not MEL.connect_material_expressions(a, a_out, b, b_in):
                err('link %s.%s -> %s.%s in %s' % (a.get_name(), a_out, b.get_name(), b_in, self.m.get_name()))

        def out(self, e, pin, prop):
            if not MEL.connect_material_property(e, pin, prop):
                err('out %s.%s -> %s in %s' % (e.get_name(), pin, prop, self.m.get_name()))

        def scalar(self, name, v):
            return self.n(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(v))

        def vec4(self, name, v):
            p = self.n(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*[float(x) for x in v]))
            a = self.n(unreal.MaterialExpressionAppendVector)
            self.link(p, '', a, 'A')
            self.link(p, 'A', a, 'B')
            return a

        def tex(self, name, default, stype, uv=None):
            e = self.n(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name)
            if default:
                e.set_editor_property('texture', default)
            e.set_editor_property('sampler_type', stype)
            if uv is not None:
                self.link(uv, '', e, 'UVs')
            return e

        def rgba(self, t):
            a = self.n(unreal.MaterialExpressionAppendVector)
            self.link(t, 'RGB', a, 'A')
            self.link(t, 'A', a, 'B')
            return a

        def cdata(self, i, d):
            e = self.n(unreal.MaterialExpressionPerInstanceCustomData, data_index=i)
            try:
                e.set_editor_property('const_default_value', float(d))
            except Exception:
                pass
            return e

        def frame(self, name, is_vec, default):
            if mpc:
                e = self.n(unreal.MaterialExpressionCollectionParameter)
                e.set_editor_property('collection', mpc)
                e.set_editor_property('parameter_name', name)
                return e
            if is_vec:
                return self.n(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*default))
            return self.scalar(name, default)

        def custom(self, code, inputs, out_type, outputs=()):
            e = self.n(unreal.MaterialExpressionCustom)
            e.set_editor_property('code', code)
            e.set_editor_property('output_type', out_type)
            ins = []
            for nm, _s, _p in inputs:
                ci = unreal.CustomInput()
                ci.set_editor_property('input_name', nm)
                ins.append(ci)
            e.set_editor_property('inputs', ins)
            outs = []
            for nm, t in outputs:
                co = unreal.CustomOutput()
                co.set_editor_property('output_name', nm)
                co.set_editor_property('output_type', t)
                outs.append(co)
            e.set_editor_property('additional_outputs', outs)
            for nm, src, pin in inputs:
                self.link(src, pin, e, nm)
            return e

    def new_material(name):
        full = MASTERS + '/' + name
        m = None
        if EAL.does_asset_exist(full):
            m = unreal.load_asset(full)
            if m:
                MEL.delete_all_material_expressions(m)
        if not m:
            m = AT.create_asset(name, MASTERS, unreal.Material, unreal.MaterialFactoryNew())
        return m

    def finish(m):
        MEL.layout_material_expressions(m)
        MEL.recompile_material(m)
        EAL.save_loaded_asset(m)
        log('material ' + m.get_path_name())

    def uv_graph(g):
        """UV0 (or UV1 when BaseUV1) with VOffset added to V: the D / ORM / M lookup."""
        u0 = g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
        u1 = g.n(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1)
        b1 = g.scalar('BaseUV1', 0.0)
        vo = g.scalar('VOffset', 0.0)
        c = g.custom('return lerp(U0, U1, B1) + float2(0.0, VO);', [('U0', u0, ''), ('U1', u1, ''), ('B1', b1, ''), ('VO', vo, '')],
                     unreal.CustomMaterialOutputType.CMOT_FLOAT2)
        c0 = g.custom('return U0 + float2(0.0, VO);', [('U0', u0, ''), ('VO', vo, '')], unreal.CustomMaterialOutputType.CMOT_FLOAT2)
        return c, c0

    S = unreal.MaterialSamplerType
    masters = {}
    if not dry:
        # ---------------- opaque
        m = new_material('M_DmKit')
        for flag in ('used_with_instanced_static_meshes', 'used_with_skeletal_mesh', 'used_with_static_lighting', 'used_with_nanite'):
            setp(m, [flag], True)
        g = G(m)
        uvb, uv0 = uv_graph(g)
        d = g.tex('D', first_tex('D'), S.SAMPLERTYPE_COLOR, uvb)
        nrm = g.tex('N', first_tex('N'), S.SAMPLERTYPE_NORMAL, uv0)
        orm = g.tex('ORM', first_tex('ORM'), S.SAMPLERTYPE_MASKS, uv0)
        mk = g.tex('M', first_tex('M'), S.SAMPLERTYPE_MASKS, uvb)
        vcn = g.n(unreal.MaterialExpressionVertexColor)
        vca = g.custom('return Parameters.VertexColor;', [], F4)
        vc = g.custom('return float4(VCRGB, VCA.a);', [('VCRGB', vcn, ''), ('VCA', vca, '')], F4)
        wp = g.n(unreal.MaterialExpressionWorldPosition)
        nws = g.n(unreal.MaterialExpressionVertexNormalWS)
        ctr = g.frame('PlanetCenter', True, (0.0, 0.0, 0.0, 0.0))
        sun = g.frame('SunDir', True, (0.0, 0.0, 1.0, 0.0))
        ov = g.frame('NightOverride', False, -1.0)
        glow = g.frame('Glow', False, 1.0)
        upc = g.custom('return dot(normalize(N), normalize(WP - Ctr));', [('N', nws, ''), ('WP', wp, ''), ('Ctr', ctr, '')], F1)
        cds = [g.cdata(i, 1.0 if i < 6 else (0.6 if i == 6 else 0.0)) for i in range(8)]
        ins = [('D', g.rgba(d), ''), ('ORM', orm, 'RGB'), ('M', g.rgba(mk), ''), ('VC', vc, ''), ('WP', wp, ''),
               ('Ctr', ctr, ''), ('Sun', sun, ''), ('Ov', ov, ''), ('Glow', glow, ''), ('Up', upc, '')]
        ins += [('Cd%d' % i, cds[i], '') for i in range(8)]
        for k, v in OPAQUE_SCALARS.items():
            if k in ('BaseUV1', 'VOffset', 'HasN', 'Specular'):
                continue
            ins.append((k, g.scalar(k, v), ''))
        for k, v in OPAQUE_VECTORS.items():
            ins.append((k, g.vec4(k, v), ''))
        s = g.custom(MASTER_HLSL, ins, F3, [('AO', F1), ('Rough', F1), ('Metal', F1), ('Emis', F3)])
        g.out(s, '', MP.MP_BASE_COLOR)
        g.out(s, 'AO', MP.MP_AMBIENT_OCCLUSION)
        g.out(s, 'Rough', MP.MP_ROUGHNESS)
        g.out(s, 'Metal', MP.MP_METALLIC)
        g.out(s, 'Emis', MP.MP_EMISSIVE_COLOR)
        hasn = g.scalar('HasN', 1.0)
        nsel = g.custom('return lerp(float3(0, 0, 1), N, HasN);', [('N', nrm, 'RGB'), ('HasN', hasn, '')], F3)
        g.out(nsel, '', MP.MP_NORMAL)
        g.out(g.scalar('Specular', 0.4), '', MP.MP_SPECULAR)
        finish(m)
        masters['master:opaque'] = m
        # ---------------- masked foliage
        m = new_material('M_DmKitFoliage')
        for flag in ('used_with_instanced_static_meshes', 'used_with_static_lighting'):
            setp(m, [flag], True)
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        m.set_editor_property('two_sided', True)
        m.set_editor_property('opacity_mask_clip_value', 0.4)
        setp(m, ['dithered_lod_transition'], True)
        g = G(m)
        dleaf = first_tex('D', 'T_TreeLeafAtlas') or first_tex('D')
        d = g.tex('D', dleaf, S.SAMPLERTYPE_COLOR)
        nrm = g.tex('N', first_tex('N'), S.SAMPLERTYPE_NORMAL)
        wp = g.n(unreal.MaterialExpressionWorldPosition)
        nws = g.n(unreal.MaterialExpressionVertexNormalWS)
        ctr = g.frame('PlanetCenter', True, (0.0, 0.0, 0.0, 0.0))
        upc = g.custom('return dot(normalize(N), normalize(WP - Ctr));', [('N', nws, ''), ('WP', wp, ''), ('Ctr', ctr, '')], F1)
        cds = [g.cdata(i, 0.0) for i in range(3)]
        ins = [('D', g.rgba(d), ''), ('Up', upc, ''), ('Tint', g.vec4('Tint', FOLIAGE_VECTORS['Tint']), ''),
               ('Transmission', g.vec4('Transmission', FOLIAGE_VECTORS['Transmission']), '')] + [('Cd%d' % i, cds[i], '') for i in range(3)]
        fo = g.custom(FOLIAGE_HLSL, ins, F3, [('SSS', F3)])
        g.out(fo, '', MP.MP_BASE_COLOR)
        g.out(fo, 'SSS', MP.MP_SUBSURFACE_COLOR)
        g.out(d, 'A', MP.MP_OPACITY_MASK)
        hasn = g.scalar('HasN', 0.0)
        nsel = g.custom('return lerp(float3(0, 0, 1), N, HasN);', [('N', nrm, 'RGB'), ('HasN', hasn, '')], F3)
        g.out(nsel, '', MP.MP_NORMAL)
        g.out(g.scalar('Roughness', 0.8), '', MP.MP_ROUGHNESS)
        g.out(g.scalar('Specular', 0.15), '', MP.MP_SPECULAR)
        finish(m)
        masters['master:foliage'] = m
        # ---------------- additive glow (light shafts)
        m = new_material('M_DmKitGlow')
        setp(m, ['used_with_instanced_static_meshes'], True)
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
        m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        m.set_editor_property('two_sided', True)
        g = G(m)
        vcn = g.n(unreal.MaterialExpressionVertexColor)
        wp = g.n(unreal.MaterialExpressionWorldPosition)
        nws = g.n(unreal.MaterialExpressionVertexNormalWS)
        cv = g.n(unreal.MaterialExpressionCameraVectorWS)
        ins = [('VC', vcn, ''), ('WP', wp, ''), ('N', nws, ''), ('V', cv, ''),
               ('Ctr', g.frame('PlanetCenter', True, (0.0, 0.0, 0.0, 0.0)), ''), ('Sun', g.frame('SunDir', True, (0.0, 0.0, 1.0, 0.0)), ''),
               ('Ov', g.frame('NightOverride', False, -1.0), ''), ('Glow', g.frame('Glow', False, 1.0), ''),
               ('Color', g.vec4('Color', [1.0, 0.95, 0.8, 0.0]), ''), ('EmissiveDay', g.scalar('EmissiveDay', 6.0), ''),
               ('EmissiveNight', g.scalar('EmissiveNight', 10.0), '')]
        gl = g.custom(GLOW_HLSL, ins, F3)
        g.out(gl, '', MP.MP_EMISSIVE_COLOR)
        finish(m)
        masters['master:glow'] = m

    # ------------------------------------------------------------------ material instances (parents first)
    mi_assets = {}

    def make_mi(name, depth=0):
        if name in mi_assets:
            return mi_assets[name]
        spec = P.mis[name]
        par = spec['parent']
        parent = masters.get(par) if par.startswith('master:') else (make_mi(par, depth + 1) if par in P.mis and depth < 6 else None)
        if parent is None:
            err('instance %s: parent %s missing' % (name, par))
            return None
        path = '%s/%s/Materials' % (DST, spec['family_ue'])
        full = path + '/' + name
        mi = unreal.load_asset(full) if EAL.does_asset_exist(full) else AT.create_asset(
            name, path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, parent)
        MEL.clear_all_material_instance_parameters(mi)
        for k, s in spec['tex'].items():
            t = tex_assets.get(s)
            if t is None:
                err('instance %s: texture %s missing' % (name, s))
                continue
            MEL.set_material_instance_texture_parameter_value(mi, k, t)
        for k, v in spec['scalars'].items():
            MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
        for k, v in spec['vectors'].items():
            vv = list(v) + [0.0] * (4 - len(v))
            MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(vv[0], vv[1], vv[2], vv[3]))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        mi_assets[name] = mi
        return mi

    t0 = time.time()
    for n in sorted(P.mis):
        make_mi(n)
    log('material instances %d in %.0f s' % (len(mi_assets), time.time() - t0))
    if only_mats:
        _finish_report(P, outdir, [], ERR, WARN, t_start, log)
        return

    # ------------------------------------------------------------------ meshes
    def fbx_task(path, dest, name, lods, yaw, skel=False):
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = dest
        task.destination_name = name
        task.replace_existing = True
        task.automated = True
        task.save = False
        ui = unreal.FbxImportUI()
        ui.import_mesh = True
        ui.import_materials = False
        ui.import_textures = False
        setp(ui, ['automated_import_should_detect_type'], False)
        if skel:
            ui.import_as_skeletal = True
            ui.import_animations = True
            ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
            d = ui.skeletal_mesh_import_data
            setp(d, ['import_morph_targets'], True)
            setp(ui.anim_sequence_import_data, ['import_bone_tracks'], True)
            setp(ui.anim_sequence_import_data, ['convert_scene', 'convert_scene_unit'], True)
        else:
            ui.import_as_skeletal = False
            ui.import_animations = False
            ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
            d = ui.static_mesh_import_data
            d.combine_meshes = True
            d.auto_generate_collision = False
            d.one_convex_hull_per_ucx = True
            d.generate_lightmap_u_vs = False
            d.remove_degenerates = True
            setp(d, ['build_nanite'], False)
            setp(d, ['import_mesh_lo_ds', 'import_mesh_lods'], bool(lods))
        d.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
        d.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
        d.convert_scene = True
        d.convert_scene_unit = True     # every family declares metres or centimetres in the FBX: the result is cm
        d.import_uniform_scale = 1.0
        d.import_rotation = unreal.Rotator(roll=0.0, pitch=0.0, yaw=float(yaw))
        setp(d, ['reorder_material_to_fbx_order'], True)
        task.options = ui
        task.factory = unreal.FbxFactory()
        return task

    def import_static(e, dest, name, yaw):
        full = dest + '/' + name
        if EAL.does_asset_exist(full):
            EAL.delete_asset(full)   # a re-import over a mesh keeps its old FBX settings: start clean
        AT.import_asset_tasks([fbx_task(e['fbx'], dest, name, e.get('lodgroup'), yaw)])
        sm = unreal.load_asset(full)
        if not sm:
            return None
        for i, p in enumerate(e.get('lod_files') or [], 1):
            r = SMES.import_lod(sm, i, p)
            if r != i:
                err('%s: import_lod %d -> %s' % (name, i, r))
        return sm

    def blender_to_ue_bounds(bb, a):
        """Expected Unreal bounds (cm, min / max xy) of Blender bounds bb under visual identity (x, -y) plus yaw a."""
        (x0, y0, _z0), (x1, y1, _z1) = bb
        pts = [(x, -y) for x in (x0, x1) for y in (y0, y1)]
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        r = [(100.0 * (x * ca - y * sa), 100.0 * (x * sa + y * ca)) for x, y in pts]
        return min(p[0] for p in r), min(p[1] for p in r), max(p[0] for p in r), max(p[1] for p in r)

    def bounds_err(exp, box):
        mn, mx = box.min, box.max
        got = (mn.x, mn.y, mx.x, mx.y)
        size = max(mx.x - mn.x, mx.y - mn.y, 1.0)
        return sum(abs(exp[i] - got[i]) for i in range(4)) / size

    def asymmetric(bb):
        if not bb or not bb[0] or not bb[1]:
            return False
        (x0, y0, _), (x1, y1, _) = bb
        sx, sy = max(x1 - x0, 1e-3), max(y1 - y0, 1e-3)
        return abs(x0 + x1) / sx > 0.06 and abs(y0 + y1) / sy > 0.06

    facing_a = {}

    def calibrate(fam, meshes):
        """Import one asymmetric mesh with no yaw and find the yaw the FBX import adds (0 / 90 / 180 / 270)."""
        if fam in FACING_PIN:
            return FACING_PIN[fam]
        cand = next((m for m in meshes if asymmetric(m['bounds_m'])), None)
        if not cand:
            log('facing %s: no asymmetric bounds in the manifest, default %.0f' % (fam, facing_a.get('_default', DEFAULT_A)))
            return None
        sm = import_static(cand, DST + '/_Probe', 'SM_Probe', 0.0)
        if not sm:
            warn('facing %s: probe import failed' % fam)
            return None
        box = sm.get_bounding_box()
        scores = sorted((bounds_err(blender_to_ue_bounds(cand['bounds_m'], a), box), a) for a in (0.0, 90.0, 180.0, 270.0))
        # a mirrored import would match the un-flipped bounds instead: report it loudly
        flips = []
        for a in (0.0, 90.0, 180.0, 270.0):
            (x0, y0, z0), (x1, y1, z1) = cand['bounds_m']
            flips.append((bounds_err(blender_to_ue_bounds(((x0, -y1, z0), (x1, -y0, z1)), a), box), a))
        flips.sort()
        EAL.delete_asset(DST + '/_Probe/SM_Probe')
        log('facing %s: probe %s -> yaw %.0f (err %.3f, next %.3f; mirrored-hypothesis best %.3f)' % (
            fam, cand['name'], scores[0][1], scores[0][0], scores[1][0], flips[0][0]))
        if flips[0][0] + 0.02 < scores[0][0]:
            err('facing %s: the import looks MIRRORED (bounds match the unflipped hypothesis)' % fam)
        if scores[0][0] > 0.08:
            warn('facing %s: probe bounds do not match any yaw well (err %.3f)' % (fam, scores[0][0]))
            return None
        return scores[0][1]

    def tri_count(sm, lod):
        for fn in ('get_num_triangles',):
            try:
                return int(getattr(sm, fn)(lod))
            except Exception:
                pass
        try:
            return int(SMES.get_number_verts(sm, lod))   # vertices when triangles are not exposed
        except Exception:
            return -1

    report = []
    by_family = {}
    for m in P.meshes:
        by_family.setdefault(m['family'], []).append(m)
    t_mesh = time.time()
    for fam in ORDER:
        meshes = by_family.get(fam, [])
        if not meshes:
            continue
        a = calibrate(fam, meshes) if fam != 'trees' else 0.0
        if a is None:
            a = facing_a.get('_default', DEFAULT_A)
        else:
            facing_a.setdefault('_default', a)
        facing_a[fam] = a
        cls = FAMILIES[fam]['cls']
        # Blender front -Y -> +Y after the visual identity (90 deg), then the import adds a: front at 90 + a
        yaw = (TARGET_FRONT[cls] - (90.0 + a)) % 360.0
        if fam == 'trees':
            yaw = 0.0
        log('family %s: %d meshes, import yaw %.0f (probe a %.0f, class %s)' % (fam, len(meshes), yaw, a, cls))
        tf = time.time()
        for e in meshes:
            dest = '%s/%s/Meshes' % (DST, e['family_ue'])
            tm0 = time.time()
            sm = import_static(e, dest, e['name'], yaw)
            rec = {'name': e['name'], 'family': fam, 'path': dest + '/' + e['name'], 'yaw': yaw}
            if not sm:
                err('mesh import failed %s' % e['name'])
                rec['ok'] = False
                report.append(rec)
                continue
            # slots -> instances
            # every edit of a static mesh rebuilds it (seconds under load): skip edits that change nothing
            mats = sm.get_editor_property('static_materials')
            names = []
            want = []
            for i, sl in enumerate(mats):
                slot = str(sl.get_editor_property('material_slot_name'))
                names.append(slot)
                mi_name = e['slots'].get(slot)
                if mi_name is None:
                    for k, v in e['slots'].items():   # sanitised FBX names ('@' -> '_', case)
                        if k.replace('@', '_').lower() == slot.lower():
                            mi_name = v
                mi = mi_assets.get(mi_name) if mi_name else None
                if mi is None:
                    err('%s: no instance for slot %s (%s)' % (e['name'], slot, mi_name))
                    continue
                want.append((i, slot, mi))
            # one write of fresh structs (one rebuild); per-slot set_material (one rebuild each) when it does not stick
            try:
                fresh = []
                by_i = {i: mi for i, _s, mi in want}
                for i, sl in enumerate(mats):
                    ns = unreal.StaticMaterial()
                    ns.set_editor_property('material_slot_name', sl.get_editor_property('material_slot_name'))
                    ns.set_editor_property('material_interface', by_i.get(i) or sl.get_editor_property('material_interface'))
                    for k in ('imported_material_slot_name', 'uv_channel_data'):
                        try:
                            ns.set_editor_property(k, sl.get_editor_property(k))
                        except Exception:
                            pass
                    fresh.append(ns)
                sm.set_editor_property('static_materials', fresh)
            except Exception as ex:
                warn('%s: batch slot write failed %r' % (e['name'], ex))
            now = sm.get_editor_property('static_materials')
            for i, _slot, mi in want:
                cur = now[i].get_editor_property('material_interface') if i < len(now) else None
                if cur is None or cur.get_path_name() != mi.get_path_name():
                    sm.set_material(i, mi)
            n_lods = SMES.get_lod_count(sm)
            if profile == 'art' and e.get('art_screen'):
                sizes = [float(x) for x in e['art_screen']][:n_lods]
            else:
                sizes = GAME_SCREEN[cls][:n_lods]
            while len(sizes) < n_lods:
                sizes.append(sizes[-1] * 0.33)
            if n_lods > 1 and not SMES.set_lod_screen_sizes(sm, sizes):
                warn('%s: set_lod_screen_sizes failed' % e['name'])
            # collision: one box for buildings and wonders (camera / picking traces), none for the rest (instances
            # never carry physics bodies: the factories set NoCollision on their components anyway)
            col = COLLISION[cls]
            if e.get('ucx'):
                col = 'ucx'
            elif col == 'box':
                try:
                    SMES.remove_collisions(sm)
                    SMES.add_simple_collisions(sm, unreal.ScriptCollisionShapeType.BOX)
                except Exception as ex:
                    warn('%s: box collision failed %r' % (e['name'], ex))
            body = sm.get_editor_property('body_setup')
            if body:
                body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
            # Nanite (opt-in): building-scale opaque meshes only
            want_nanite = nanite and cls in NANITE_CLASSES and not any('Foliage' in v or 'Crop' in v for v in e['slots'].values())
            if want_nanite:   # imported with Nanite off: touch the settings only to turn it on
                try:
                    ns = sm.get_editor_property('nanite_settings')
                    ns.enabled = True
                    try:
                        SMES.set_nanite_settings(sm, ns, apply_changes=True)
                    except Exception:
                        sm.set_editor_property('nanite_settings', ns)
                except Exception as ex:
                    warn('%s: nanite settings %r' % (e['name'], ex))
            EAL.save_loaded_asset(sm)
            unset = [str(sl.get_editor_property('material_slot_name')) for sl in sm.get_editor_property('static_materials')
                     if not sl.get_editor_property('material_interface') or 'WorldGrid' in sl.get_editor_property('material_interface').get_path_name()]
            if unset:
                err('%s: slots without an instance after import: %s' % (e['name'], unset))
            # checks: scale (height against the manifest), facing (bounds against the manifest), LODs, triangles
            box = sm.get_bounding_box()
            h_cm = box.max.z
            rec.update({'ok': True, 'lods': n_lods, 'screen': [round(x, 4) for x in sizes], 'slots': names,
                        'bounds_cm': [round(box.min.x), round(box.min.y), round(box.min.z), round(box.max.x), round(box.max.y), round(box.max.z)],
                        'collision': col, 'nanite': want_nanite, 'sockets': len(e.get('sockets') or [])})
            rec['tris'] = [tri_count(sm, l) for l in range(n_lods)]
            if e.get('height_m'):
                # manifests give either the top above the pivot or the full Z extent (overlays, stilts): take the closer
                sc = min((h_cm / (100.0 * e['height_m']), (box.max.z - box.min.z) / (100.0 * e['height_m'])), key=lambda x: abs(x - 1.0))
                rec['scale_check'] = round(sc, 3)
                if abs(sc - 1.0) > 0.06:
                    warn('%s: height %.2f m in Unreal vs %.2f m in the manifest (x%.3f)' % (e['name'], h_cm / 100.0, e['height_m'], sc))
            if e.get('bounds_m') and asymmetric(e['bounds_m']) and fam != 'trees':
                exp = blender_to_ue_bounds(e['bounds_m'], (90.0 + a + yaw) - 90.0)
                fe = bounds_err(exp, box)
                rec['facing_err'] = round(fe, 3)
                if fe > 0.08:
                    warn('%s: bounds do not match the expected facing (err %.3f)' % (e['name'], fe))
            want_lods = (e.get('lod_children') or 0) or (1 + len(e.get('lod_files') or []))
            if want_lods and n_lods != want_lods:
                warn('%s: %d LODs imported, the FBX has %d' % (e['name'], n_lods, want_lods))
            report.append(rec)
            rec['seconds'] = round(time.time() - tm0, 1)
            log('mesh %s lods %d tris %s screen %s slots %s bounds %s cm (%.1f s)' % (e['name'], n_lods, rec['tris'], rec['screen'], names, rec['bounds_cm'], rec['seconds']))
        log('family %s done in %.0f s' % (fam, time.time() - tf))
    log('meshes %d in %.0f s' % (len(report), time.time() - t_mesh))

    # ------------------------------------------------------------------ skeletal animals (morph targets, takes)
    if skeletal:
        for e in P.skeletal:
            dest = '%s/%s/Skeletal' % (DST, e['family_ue'])
            full = dest + '/' + e['name']
            if EAL.does_asset_exist(full):
                EAL.delete_asset(full)
            a = facing_a.get('animals', facing_a.get('_default', DEFAULT_A))
            yaw = (TARGET_FRONT['animal'] - (90.0 + a)) % 360.0
            try:
                AT.import_asset_tasks([fbx_task(e['fbx'], dest, e['name'], False, yaw, skel=True)])
            except Exception as ex:
                err('skeletal %s: %r' % (e['name'], ex))
                continue
            sk = unreal.load_asset(full)
            if not sk:
                err('skeletal import failed %s' % e['name'])
                continue
            try:
                mats = sk.get_editor_property('materials')
                for i, sl in enumerate(mats):
                    mi = mi_assets.get(e['slots'].get(str(sl.get_editor_property('material_slot_name'))) or '')
                    if mi:
                        sl.set_editor_property('material_interface', mi)
                sk.set_editor_property('materials', mats)
            except Exception as ex:
                warn('skeletal %s materials %r' % (e['name'], ex))
            EAL.save_loaded_asset(sk)
            anims = [x for x in EAL.list_assets(dest, recursive=False) if e['name'] in x and 'Anim' in x]
            log('skeletal %s anims %d' % (e['name'], len(anims)))
            report.append({'name': e['name'], 'family': 'animals', 'path': full, 'ok': True, 'skeletal': True, 'anims': len(anims)})

    # ------------------------------------------------------------------ the shrine (its own pipeline, same session)
    if do_shrine:
        src = os.path.join(ART, 'tools', 'ue_import.py')
        if os.path.exists(src):
            try:
                code = compile(open(src, encoding='utf-8').read(), src, 'exec')
                exec(code, {'__file__': src, '__name__': 'dm_shrine_import'})
                log('shrine imported via art/tools/ue_import.py')
            except Exception as ex:
                err('shrine import failed: %r' % (ex,))

    EAL.save_directory(DST, only_if_is_dirty=True, recursive=True)
    _finish_report(P, outdir, report, ERR, WARN, t_start, log, facing_a)


def _finish_report(P, outdir, report, ERR, WARN, t_start, log, facing=None):
    rep = {'when': time.strftime('%Y-%m-%d %H:%M:%S'), 'seconds': round(time.time() - t_start), 'plan_notes': P.notes,
           'plan_issues': P.issues, 'errors': ERR, 'warnings': WARN, 'facing': facing or {}, 'meshes': report}
    with open(os.path.join(outdir, 'report.json'), 'w', encoding='utf-8') as f:
        json.dump(rep, f, indent=1)
    log('report %s: %d meshes, %d errors, %d warnings, %.0f s' % (os.path.join(outdir, 'report.json'), len(report), len(ERR), len(WARN), time.time() - t_start))
    log('RESULT %s' % ('PASS' if not ERR else 'FAIL'))

# ============================================================================================== entry


def _cli():
    import argparse
    ap = argparse.ArgumentParser(description='Plan the DEMIURGE asset import (no Unreal needed).')
    ap.add_argument('--plan', action='store_true', help='print the coverage summary')
    ap.add_argument('--families', default='')
    ap.add_argument('--match', default='')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--json', default='', help='write the full plan here')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    fams = [f for f in a.families.split(',') if f] or None
    P = build_plan(fams, [m for m in a.match.split(',') if m] or None, a.limit or None)
    by = {}
    for m in P.meshes:
        by.setdefault(m['family'], []).append(m)
    print('DEMIURGE asset import plan  (root %s)' % ROOT)
    for f in ORDER:
        ms = by.get(f, [])
        if not ms:
            continue
        lod = sum(1 for m in ms if m['lodgroup'] or m['lod_files'])
        bb = sum(1 for m in ms if m['bounds_m'])
        mis = sorted(set(v for m in ms for v in m['slots'].values()))
        print('  %-11s %4d meshes  %4d with LODs  %4d with bounds  %3d slot instances  -> /Game/Assets/%s' % (
            f, len(ms), lod, bb, len(mis), FAMILIES[f]['ue']))
    print('  skeletal    %4d' % len(P.skeletal))
    skipped = [n for n in P.notes if 'variant' in n and 'skipped' in n]
    for n in P.notes:
        if n not in skipped or a.verbose:
            print('  ' + n)
    if skipped and not a.verbose:
        print('  %d variants skipped (their base material is not used by the selected meshes)' % len(skipped))
    print('issues: %d' % len(P.issues))
    for i in P.issues[: (None if a.verbose else 40)]:
        print('  - ' + i)
    if a.verbose:
        for n, mi in sorted(P.mis.items()):
            print('  MI %-44s parent %-28s tex %s' % (n, mi['parent'], mi['tex']))
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(P.to_json(), f, indent=1)
    return 0 if not P.issues else 1


try:
    import unreal  # noqa: F401
    _IN_UNREAL = True
except ImportError:
    _IN_UNREAL = False

if _IN_UNREAL and __name__ != 'dmi_plan_only':
    run_unreal()
elif __name__ == '__main__':
    sys.exit(_cli())
