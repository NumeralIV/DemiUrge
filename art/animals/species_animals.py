# DEMIURGE art/animals - species definitions for build_animals.py (family "Animal").
#
# Every animal is described as a skeleton (joints and bones) plus primitives tied to bones:
#   cone  = rounded cone between two points (legs, necks, tails),
#   ell   = rotated ellipsoid (body masses, heads, ears, fins, wings),
#   tube  = explicit low-poly tube along a polyline (horns, antlers, beaks, thin tails, bird legs);
#           tubes are not part of the signed-distance body, so they keep their shape at 700 triangles,
#   paint = a primitive used only to place colour (eyes, rump patches); no geometry.
# The body is the smooth union of the cone/ell primitives (signed distance field, meshed by OpenVDB),
# which is the high-poly bake source; LOD0 is a symmetric decimation of it plus the low tubes.
#
# Conventions (docs/ASSETS.md): metres, Z up, front faces -Y, left side of the animal is +X (suffix _L).
# Walkers: origin at the centre of the footprint on the ground. Birds and fish: origin at the body centre.
# Whale: origin on the sea surface with the back just breaking it (body axis 1.2 m below).
import math
import numpy as np


def V(*a):
    return np.array(a, np.float32)


def hexlin(h):
    h = h.lstrip('#')
    c = np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], np.float32)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4).astype(np.float32)


def rotm(deg):
    """XYZ Euler degrees -> 3x3 matrix whose columns are the local axes in world space."""
    x, y, z = [math.radians(d) for d in deg]
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]], np.float32)
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]], np.float32)
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]], np.float32)
    return rz @ ry @ rx


# --------------------------------------------------------------------------- signed distance primitives

def sd_roundcone(P, a, b, ra, rb):
    ba = b - a
    l2 = float(ba @ ba)
    rr = ra - rb
    a2 = l2 - rr * rr
    il2 = 1.0 / l2
    pa = P - a
    y = pa @ ba
    z = y - l2
    xv = pa * l2 - y[..., None] * ba
    x2 = np.einsum('...i,...i->...', xv, xv)
    y2 = y * y * l2
    z2 = z * z * l2
    k = math.copysign(1.0, rr) * rr * rr * x2 if rr != 0 else np.zeros_like(x2)
    d1 = np.sqrt(x2 + z2) * il2 - rb
    d2 = np.sqrt(x2 + y2) * il2 - ra
    d3 = (np.sqrt(np.maximum(x2 * a2 * il2, 0)) + y * rr) * il2 - ra
    return np.where(np.sign(z) * a2 * z2 > k, d1, np.where(np.sign(y) * a2 * y2 < k, d2, d3)).astype(np.float32)


def sd_ellipsoid(P, c, r, R):
    q = (P - c) @ R          # into local axes
    k0 = np.linalg.norm(q / r, axis=-1)
    k1 = np.linalg.norm(q / (r * r), axis=-1)
    return (k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)).astype(np.float32)


class Prim:
    def __init__(self, kind, bone, tag, k=0.0, sep=False, paint=False, **kw):
        self.kind, self.bone, self.tag, self.k, self.sep, self.paint = kind, bone, tag, k, sep, paint
        self.__dict__.update(kw)

    def sdf(self, P):
        if self.kind == 'cone':
            return sd_roundcone(P, self.a, self.b, self.ra, self.rb)
        if self.kind == 'ell':
            return sd_ellipsoid(P, self.c, self.r, self.R)
        if self.kind == 'tube':   # chain of round cones
            d = None
            for i in range(len(self.pts) - 1):
                di = sd_roundcone(P, self.pts[i], self.pts[i + 1], max(self.radii[i], 1e-4), max(self.radii[i + 1], 1e-4) * 0.999)
                d = di if d is None else np.minimum(d, di)
            return d
        raise ValueError(self.kind)

    def bbox(self):
        if self.kind == 'cone':
            r = max(self.ra, self.rb)
            return np.minimum(self.a, self.b) - r, np.maximum(self.a, self.b) + r
        if self.kind == 'ell':
            ext = np.abs(self.R) @ self.r
            return self.c - ext, self.c + ext
        pts = np.array(self.pts)
        r = max(self.radii)
        return pts.min(0) - r, pts.max(0) + r


class Spec:
    """One animal mesh: skeleton, primitives, colour, animation and export data."""

    def __init__(self, asset, variant, kind):
        self.asset, self.variant, self.kind = asset, variant, kind      # kind: quad | cat | bird | fish | whale
        self.name = 'SM_Animal_%s_%s' % (asset, variant)
        self.key = '%s-%s' % (asset.lower(), variant.lower())
        self.prims, self.bones = [], []
        self.vs = 0.01            # voxel size of the high-poly bake source (m)
        self.lod0 = 700           # LOD0 triangle target
        self.tex = 1024
        self.lods = (1.0, 0.4, 0.2)   # walkers: LOD2 at 20 % (verification 2026-11-19: at 12 % the collapse turns legs into skirts or drops them)
        self.sheen = 0.2
        self.noise = (0.004, 7.0, 0.0012, 45.0)   # (muscle amp m, freq /m, fur amp, fur freq)
        self.walker = True
        self.anims = []
        self.shape_keys = []
        self.coat = None
        self.biomes = {}
        self.notes = ''
        self.flock_scale = 1.0
        self.graze = (6, 70, -30)
        self.shag = {}

    # primitives -------------------------------------------------------------
    def cone(self, a, b, ra, rb, bone, tag='body', k=0.03):
        self.prims.append(Prim('cone', bone, tag, k, a=V(*a), b=V(*b), ra=ra, rb=rb))

    def ell(self, c, r, bone, tag='body', k=0.03, rot=(0, 0, 0), paint=False):
        self.prims.append(Prim('ell', bone, tag, k, paint=paint, c=V(*c), r=V(*r), R=rotm(rot)))

    def tube(self, pts, radii, bone, tag, sides=(10, 5), flat=1.0):
        self.prims.append(Prim('tube', bone, tag, 0.0, sep=True, pts=[V(*p) for p in pts], radii=list(radii), sides=sides, flat=flat))

    def paint(self, c, r, bone, tag, rot=(0, 0, 0)):
        self.ell(c, r, bone, tag, 0.0, rot, paint=True)

    def bone(self, name, head, tail, parent=None):
        self.bones.append((name, V(*head), V(*tail), parent))


SIDES = ((1.0, 'L'), (-1.0, 'R'))


# --------------------------------------------------------------------------- colour helpers

def _lat(seed):
    return np.random.default_rng(seed).random((32, 32, 32), dtype=np.float32) * 2 - 1


_LATS = {}


def vnoise(P, freq, seed=0):
    if seed not in _LATS:
        _LATS[seed] = _lat(seed)
    L = _LATS[seed]
    X = P * freq + seed * 3.17
    i = np.floor(X).astype(np.int64)
    f = (X - i).astype(np.float32)
    f = f * f * (3 - 2 * f)
    i %= 32
    j = (i + 1) % 32
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                ix = j[:, 0] if dx else i[:, 0]
                iy = j[:, 1] if dy else i[:, 1]
                iz = j[:, 2] if dz else i[:, 2]
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                out = out + w * L[ix, iy, iz]
    return out.astype(np.float32)


def fbm(P, freq, octaves=4, seed=0):
    s, a, t = 0.0, 1.0, 0.0
    for o in range(octaves):
        s = s + a * vnoise(P, freq * (2 ** o), seed + o * 11)
        t += a
        a *= 0.5
    return s / t


def sst(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def mix(a, b, t):
    t = np.asarray(t, np.float32)
    return a + (b - a) * t[..., None]


def tagw(T, *names):
    w = 0.0
    for n in names:
        if n in T:
            w = w + T[n]
    return np.clip(w, 0, 1) if not isinstance(w, float) else np.zeros(1, np.float32)


# --------------------------------------------------------------------------- ungulate legs

def ungulate_legs(S, W, yF, yH, zS, zH, rF, rH, sc=1.0, hoof='#1c1915'):
    for s, sx in SIDES:
        x = s * W
        Sj = (x, yF, zS)
        E = (x * 0.97, yF + 0.04 * sc, zS * 0.64)
        K = (x * 0.93, yF + 0.005 * sc, zS * 0.35)
        F = (x * 0.93, yF + 0.012 * sc, zS * 0.085)
        Hf = (x * 0.93, yF - 0.018 * sc, rF[4])
        S.cone(Sj, E, rF[0], rF[1], 'F%s_upper' % sx, 'leg', 0.05 * sc)
        S.cone(E, K, rF[1] * 0.8, rF[2], 'F%s_lower' % sx, 'leg', 0.02 * sc)
        S.cone(K, F, rF[2], rF[3], 'F%s_foot' % sx, 'shin', 0.012 * sc)
        S.cone(F, Hf, rF[3], rF[4], 'F%s_foot' % sx, 'hoof', 0.008 * sc)
        S.bone('F%s_upper' % sx, Sj, E, 'chest')
        S.bone('F%s_lower' % sx, E, K, 'F%s_upper' % sx)
        S.bone('F%s_foot' % sx, K, (Hf[0], Hf[1], 0.0), 'F%s_lower' % sx)
        H = (x, yH, zH)
        St = (x * 1.04, yH - 0.10 * sc, zH * 0.64)
        Hk = (x * 0.92, yH + 0.12 * sc, zH * 0.42)
        F2 = (x * 0.92, yH + 0.075 * sc, zH * 0.085)
        Hf2 = (x * 0.92, yH + 0.05 * sc, rH[4])
        S.cone(H, St, rH[0], rH[1], 'H%s_thigh' % sx, 'leg', 0.06 * sc)
        S.cone(St, Hk, rH[1] * 0.75, rH[2], 'H%s_shin' % sx, 'leg', 0.02 * sc)
        S.cone(Hk, F2, rH[2], rH[3], 'H%s_foot' % sx, 'shin', 0.012 * sc)
        S.cone(F2, Hf2, rH[3], rH[4], 'H%s_foot' % sx, 'hoof', 0.008 * sc)
        S.bone('H%s_thigh' % sx, H, St, 'pelvis')
        S.bone('H%s_shin' % sx, St, Hk, 'H%s_thigh' % sx)
        S.bone('H%s_foot' % sx, Hk, (Hf2[0], Hf2[1], 0.0), 'H%s_shin' % sx)


def quad_anims(S, cycle=24, swing=22.0, knee=38.0, graze=True, idle=False):
    S.anims.append(('Walk', cycle, 'walk', dict(swing=swing, knee=knee)))
    if graze:
        S.anims.append(('Graze', 48, 'graze', {}))
    if idle:
        S.anims.append(('Idle', 48, 'idle', {}))
    S.shape_keys = [('HeadDown', 'graze_pose')] if graze else []


# --------------------------------------------------------------------------- grazers

def grazer_wildebeest():
    S = Spec('Grazer', 'Wildebeest', 'quad')
    S.vs = 0.008
    S.lod0 = 720
    sc = 1.0
    # spine
    S.bone('root', (0, 0, 0), (0, -0.3, 0))
    S.bone('pelvis', (0, 0.46, 0.98), (0, 0.0, 1.0), 'root')
    S.bone('chest', (0, 0.0, 1.0), (0, -0.45, 1.05), 'pelvis')
    S.bone('neck', (0, -0.45, 1.05), (0, -0.90, 1.22), 'chest')
    S.bone('head', (0, -0.90, 1.22), (0, -1.22, 0.92), 'neck')
    S.bone('tail', (0, 0.68, 1.02), (0, 0.78, 0.50), 'pelvis')
    # body masses (deep forequarters, sloping back)
    S.ell((0, -0.38, 0.98), (0.24, 0.40, 0.32), 'chest', 'body', 0.10)
    S.ell((0, -0.44, 1.16), (0.12, 0.30, 0.15), 'chest', 'body', 0.10)
    S.ell((0, 0.04, 0.90), (0.225, 0.42, 0.26), 'pelvis', 'body', 0.12)
    S.ell((0, 0.40, 0.93), (0.20, 0.27, 0.24), 'pelvis', 'body', 0.10)
    S.ell((0, 0.50, 1.03), (0.14, 0.18, 0.11), 'pelvis', 'body', 0.08)
    # neck, beard, mane
    S.cone((0, -0.64, 1.06), (0, -0.88, 1.20), 0.17, 0.11, 'neck', 'body', 0.08)
    S.ell((0, -0.78, 0.99), (0.03, 0.09, 0.10), 'neck', 'beard', 0.07, rot=(-20, 0, 0))
    S.ell((0, -0.68, 1.22), (0.03, 0.22, 0.065), 'neck', 'mane', 0.03, rot=(28, 0, 0))
    # head: long downward face, broad muzzle
    S.cone((0, -0.92, 1.22), (0, -1.17, 0.97), 0.10, 0.07, 'head', 'head', 0.04)
    S.ell((0, -1.19, 0.93), (0.08, 0.07, 0.07), 'head', 'muzzle', 0.035)
    S.ell((0, -0.93, 1.28), (0.095, 0.07, 0.06), 'head', 'head', 0.03)
    for s, sx in SIDES:
        S.ell((s * 0.12, -0.90, 1.29), (0.075, 0.025, 0.033), 'head', 'ear', 0.015, rot=(0, s * -25, s * 15))
        S.paint((s * 0.085, -1.00, 1.18), (0.02, 0.02, 0.017), 'head', 'eye')
        S.tube([(s * 0.05, -0.93, 1.31), (s * 0.16, -0.925, 1.31), (s * 0.25, -0.93, 1.29), (s * 0.31, -0.955, 1.34), (s * 0.29, -0.985, 1.43)],
               [0.042, 0.036, 0.028, 0.019, 0.006], 'head', 'horn', sides=(10, 5))
    S.tube([(0, 0.66, 1.03), (0, 0.74, 0.90), (0, 0.765, 0.72), (0, 0.775, 0.56)], [0.035, 0.024, 0.018, 0.016], 'tail', 'tail', sides=(8, 4))
    S.ell((0, 0.78, 0.55), (0.045, 0.045, 0.16), 'tail', 'tuft', 0.02)
    ungulate_legs(S, 0.13, -0.42, 0.46, 0.92, 0.94, (0.085, 0.06, 0.034, 0.026, 0.032), (0.12, 0.075, 0.034, 0.026, 0.032), sc)
    quad_anims(S, 24, 22, 40)
    S.graze = (8, 75, -37)

    base, dark, stripe = hexlin('#46372a'), hexlin('#171310'), hexlin('#2b2119')

    def coat(P, N, T):
        n = fbm(P, 6.0, 4, 3)
        c = base * (0.9 + 0.2 * n)[:, None]
        # silver-grey cast on the flanks, warm brown on the back
        c = mix(c, hexlin('#5a5047') * (0.9 + 0.2 * n)[:, None], sst(0.98, 0.8, P[:, 2]) * 0.35)
        # brindle: dark vertical bands on neck and forequarters
        band = 0.5 + 0.5 * np.sin(P[:, 1] * 2 * math.pi / 0.075 + 2.5 * fbm(P, 9.0, 3, 5))
        bandm = sst(0.55, 0.85, band) * sst(0.25, -0.2, P[:, 1]) * sst(0.68, 0.85, P[:, 2]) * (1 - tagw(T, 'leg', 'shin'))
        c = mix(c, stripe, bandm * 0.75)
        # darker lower legs, black face, mane, beard and tail tuft
        c = mix(c, hexlin('#3d3128'), tagw(T, 'shin') * 0.8)
        c = mix(c, dark, np.clip(tagw(T, 'mane', 'beard', 'tuft', 'muzzle') + tagw(T, 'head') * 0.85, 0, 1))
        c = mix(c, hexlin('#2b2420'), tagw(T, 'ear') * 0.7)
        c = mix(c, hexlin('#26211c'), tagw(T, 'horn'))
        c = mix(c, hexlin('#171412'), tagw(T, 'hoof', 'eye'))
        rough = 0.82 - 0.35 * tagw(T, 'horn', 'eye') - 0.15 * tagw(T, 'hoof', 'muzzle')
        return c, rough, np.zeros(len(P), np.float32)
    S.coat = coat
    S.biomes = {'Savanna': ((1.0, 1.0, 1.0), 0.0), 'Steppe': ((1.06, 1.0, 0.92), 0.02), 'Dry': ((1.12, 1.05, 0.95), 0.05)}
    S.notes = 'Concept wildebeest-like migration animal (wildlife-herds.png). Herds of 100-400.'
    return S


def grazer_antelope():
    S = Spec('Grazer', 'Antelope', 'quad')
    S.vs = 0.006
    S.lod0 = 700
    sc = 0.75
    S.bone('root', (0, 0, 0), (0, -0.25, 0))
    S.bone('pelvis', (0, 0.33, 0.78), (0, 0.0, 0.80), 'root')
    S.bone('chest', (0, 0.0, 0.80), (0, -0.30, 0.80), 'pelvis')
    S.bone('neck', (0, -0.30, 0.80), (0, -0.63, 1.08), 'chest')
    S.bone('head', (0, -0.63, 1.08), (0, -0.84, 0.98), 'neck')
    S.bone('tail', (0, 0.47, 0.86), (0, 0.55, 0.68), 'pelvis')
    S.ell((0, -0.26, 0.77), (0.155, 0.29, 0.205), 'chest', 'body', 0.08)
    S.ell((0, 0.03, 0.76), (0.145, 0.31, 0.175), 'pelvis', 'body', 0.08)
    S.ell((0, 0.30, 0.78), (0.145, 0.20, 0.185), 'pelvis', 'body', 0.07)
    S.cone((0, -0.42, 0.84), (0, -0.61, 1.05), 0.095, 0.058, 'neck', 'body', 0.05)
    S.cone((0, -0.64, 1.10), (0, -0.82, 1.00), 0.062, 0.036, 'head', 'head', 0.03)
    S.ell((0, -0.825, 0.99), (0.038, 0.045, 0.038), 'head', 'muzzle', 0.025)
    for s, sx in SIDES:
        S.ell((s * 0.075, -0.62, 1.165), (0.07, 0.018, 0.032), 'head', 'ear', 0.012, rot=(0, s * -30, s * 20))
        S.paint((s * 0.05, -0.70, 1.10), (0.016, 0.016, 0.013), 'head', 'eye')
        S.tube([(s * 0.028, -0.645, 1.15), (s * 0.062, -0.625, 1.24), (s * 0.05, -0.585, 1.33), (s * 0.085, -0.57, 1.42), (s * 0.075, -0.59, 1.49)],
               [0.022, 0.018, 0.014, 0.010, 0.004], 'head', 'horn', sides=(8, 4))
        # black stripe down the rear thigh
        S.paint((s * 0.12, 0.42, 0.74), (0.04, 0.025, 0.13), 'pelvis', 'stripe')
    S.tube([(0, 0.47, 0.87), (0, 0.52, 0.79), (0, 0.55, 0.69)], [0.024, 0.02, 0.012], 'tail', 'tail', sides=(8, 4))
    ungulate_legs(S, 0.095, -0.30, 0.33, 0.70, 0.72, (0.06, 0.04, 0.022, 0.017, 0.021), (0.085, 0.05, 0.022, 0.017, 0.021), sc)
    quad_anims(S, 20, 24, 42)
    S.graze = (6, 86, -36)

    back, flank, belly = hexlin('#a86b3c'), hexlin('#c6935f'), hexlin('#ece4d4')

    def coat(P, N, T):
        n = fbm(P, 8.0, 4, 7)
        z0 = 0.70 + 0.02 * n
        c = mix(back, flank, sst(0.86, 0.70, P[:, 2]))
        c = c * (0.92 + 0.16 * n)[:, None]
        # crisp white belly line, white under the tail and on the throat
        bel = sst(z0 - 0.01, z0 - 0.05, P[:, 2]) * (1 - tagw(T, 'shin', 'hoof', 'leg') * 0.6)
        c = mix(c, belly, bel)
        c = mix(c, belly, sst(1.0, 0.94, P[:, 2]) * tagw(T, 'head', 'body') * sst(-0.6, -0.7, P[:, 1]) * 0.8)
        c = mix(c, hexlin('#6e4529'), tagw(T, 'shin') * 0.5)
        c = mix(c, hexlin('#1e1915'), tagw(T, 'stripe', 'eye', 'hoof'))
        c = mix(c, hexlin('#2a2119'), tagw(T, 'horn'))
        c = mix(c, hexlin('#4b3626'), tagw(T, 'muzzle') * 0.7)
        c = mix(c, hexlin('#1e1915'), tagw(T, 'tail') * sst(0.75, 0.70, P[:, 2]))
        rough = 0.78 - 0.3 * tagw(T, 'horn', 'eye')
        return c, rough, np.zeros(len(P), np.float32)
    S.coat = coat
    S.biomes = {'Savanna': ((1.0, 1.0, 1.0), 0.0), 'Arid': ((1.1, 1.04, 0.92), 0.03), 'Woodland': ((0.9, 0.86, 0.84), 0.0)}
    S.notes = 'Impala-like antelope with lyre horns; smaller, brighter herds on savanna and dry woodland.'
    return S


def grazer_bison():
    S = Spec('Grazer', 'Bison', 'quad')
    S.vs = 0.011
    S.lod0 = 760
    sc = 1.35
    S.noise = (0.006, 5.0, 0.003, 22.0)
    S.shag = {'mane': (0.014, 20.0)}
    S.bone('root', (0, 0, 0), (0, -0.4, 0))
    S.bone('pelvis', (0, 0.62, 1.08), (0, 0.0, 1.15), 'root')
    S.bone('chest', (0, 0.0, 1.15), (0, -0.82, 1.20), 'pelvis')
    S.bone('neck', (0, -0.82, 1.20), (0, -1.10, 1.02), 'chest')
    S.bone('head', (0, -1.10, 1.02), (0, -1.34, 0.56), 'neck')
    S.bone('tail', (0, 0.90, 1.22), (0, 0.97, 0.72), 'pelvis')
    S.ell((0, -0.45, 1.12), (0.34, 0.55, 0.48), 'chest', 'body', 0.14)
    S.ell((0, -0.52, 1.50), (0.22, 0.44, 0.27), 'chest', 'mane', 0.18)
    S.ell((0, 0.10, 1.06), (0.30, 0.50, 0.36), 'pelvis', 'body', 0.14)
    S.ell((0, 0.58, 1.06), (0.25, 0.30, 0.30), 'pelvis', 'body', 0.12)
    S.ell((0, -0.66, 1.06), (0.37, 0.28, 0.48), 'chest', 'mane', 0.10)
    S.cone((0, -0.85, 1.16), (0, -1.06, 0.98), 0.29, 0.23, 'neck', 'mane', 0.12)
    S.cone((0, -1.10, 0.96), (0, -1.30, 0.63), 0.21, 0.13, 'head', 'head', 0.06)
    S.ell((0, -1.32, 0.58), (0.125, 0.10, 0.10), 'head', 'muzzle', 0.05)
    S.ell((0, -1.16, 0.64), (0.075, 0.12, 0.17), 'head', 'beard', 0.05, rot=(-15, 0, 0))
    S.ell((0, -1.13, 1.08), (0.19, 0.12, 0.12), 'head', 'mane', 0.05)
    for s, sx in SIDES:
        S.ell((s * 0.20, -0.52, 0.74), (0.11, 0.14, 0.22), 'F%s_upper' % sx, 'mane', 0.06)
        S.ell((s * 0.21, -1.05, 0.98), (0.07, 0.03, 0.04), 'head', 'ear', 0.02, rot=(0, s * -20, 0))
        S.paint((s * 0.16, -1.21, 0.88), (0.022, 0.022, 0.018), 'head', 'eye')
        S.tube([(s * 0.15, -1.11, 1.07), (s * 0.27, -1.11, 1.10), (s * 0.32, -1.13, 1.19), (s * 0.29, -1.16, 1.27)],
               [0.05, 0.04, 0.026, 0.008], 'head', 'horn', sides=(10, 5))
    S.tube([(0, 0.86, 1.25), (0, 0.93, 1.05), (0, 0.95, 0.82)], [0.04, 0.03, 0.02], 'tail', 'tail', sides=(8, 4))
    S.ell((0, 0.955, 0.76), (0.05, 0.05, 0.11), 'tail', 'tuft', 0.02)
    ungulate_legs(S, 0.19, -0.55, 0.60, 1.00, 1.00, (0.14, 0.10, 0.058, 0.045, 0.05), (0.15, 0.10, 0.055, 0.042, 0.048), sc)
    quad_anims(S, 30, 18, 32)
    S.graze = (3, 28, 12)

    body, cape, dark = hexlin('#33251b'), hexlin('#7a5636'), hexlin('#1d1611')

    def coat(P, N, T):
        n = fbm(P, 5.0, 4, 13)
        c = body * (0.88 + 0.24 * n)[:, None]
        capew = np.clip(tagw(T, 'mane') * 1.2, 0, 1)
        c = mix(c, cape * (0.85 + 0.3 * fbm(P, 14.0, 3, 17))[:, None], capew)
        c = mix(c, dark, np.clip(tagw(T, 'head', 'beard', 'tuft', 'shin') * 0.85 + tagw(T, 'muzzle'), 0, 1))
        c = mix(c, hexlin('#140f0c'), tagw(T, 'horn', 'hoof', 'eye'))
        rough = 0.88 - 0.4 * tagw(T, 'horn', 'eye')
        return c, rough, np.zeros(len(P), np.float32)
    S.coat = coat
    S.biomes = {'Steppe': ((1.0, 1.0, 1.0), 0.0), 'Tundra': ((1.08, 1.05, 1.02), 0.0), 'Prairie': ((1.12, 1.02, 0.9), 0.0)}
    S.notes = 'Heavy humped steppe bison for grassland, prairie and tundra herds.'
    return S


# --------------------------------------------------------------------------- deer

def deer(stag=True):
    S = Spec('Deer', 'Stag' if stag else 'Doe', 'quad')
    S.vs = 0.007
    S.lod0 = 780 if stag else 640
    sc = 1.0 if stag else 0.88
    g = sc

    def p(x, y, z):
        return (x * g, y * g, z * g)
    S.bone('root', (0, 0, 0), p(0, -0.3, 0))
    S.bone('pelvis', p(0, 0.42, 1.02), p(0, 0.0, 1.03), 'root')
    S.bone('chest', p(0, 0.0, 1.03), p(0, -0.42, 1.05), 'pelvis')
    S.bone('neck', p(0, -0.42, 1.05), p(0, -0.82, 1.45), 'chest')
    S.bone('head', p(0, -0.82, 1.45), p(0, -1.10, 1.38), 'neck')
    S.bone('tail', p(0, 0.60, 1.12), p(0, 0.66, 0.98), 'pelvis')
    S.ell(p(0, -0.36, 1.00), (0.20 * g, 0.36 * g, 0.26 * g), 'chest', 'body', 0.10 * g)
    S.ell(p(0, 0.02, 0.98), (0.19 * g, 0.38 * g, 0.235 * g), 'pelvis', 'body', 0.10 * g)
    S.ell(p(0, 0.38, 1.01), (0.18 * g, 0.25 * g, 0.24 * g), 'pelvis', 'body', 0.09 * g)
    if stag:
        S.cone(p(0, -0.58, 1.10), p(0, -0.80, 1.42), 0.16 * g, 0.10 * g, 'neck', 'body', 0.07)
        S.ell(p(0, -0.70, 1.13), (0.12, 0.14, 0.19), 'neck', 'mane', 0.06, rot=(32, 0, 0))
    else:
        S.cone(p(0, -0.58, 1.08), p(0, -0.82, 1.36), 0.12 * g, 0.072 * g, 'neck', 'body', 0.06)
    hz = 0.0 if stag else -0.07
    S.cone(p(0, -0.84, 1.50 + hz), p(0, -1.07, 1.40 + hz), 0.083 * g, 0.046 * g, 'head', 'head', 0.03)
    S.ell(p(0, -1.08, 1.385 + hz), (0.046 * g, 0.056 * g, 0.046 * g), 'head', 'muzzle', 0.025)
    for s, sx in SIDES:
        S.ell(p(s * 0.085, -0.84, 1.585 + hz), (0.075 * g if stag else 0.062 * g, 0.02 * g, 0.033 * g), 'head', 'ear', 0.012, rot=(0, s * -35, s * 25))
        S.paint(p(s * 0.06, -0.93, 1.50 + hz), (0.018, 0.018, 0.015), 'head', 'eye')
        if stag:
            S.tube([(s * 0.05, -0.86, 1.58), (s * 0.12, -0.84, 1.70), (s * 0.20, -0.81, 1.86), (s * 0.25, -0.79, 2.00), (s * 0.255, -0.81, 2.12)],
                   [0.03, 0.026, 0.022, 0.018, 0.010], 'head', 'antler', sides=(8, 4))
            S.tube([(s * 0.10, -0.85, 1.66), (s * 0.12, -0.97, 1.70), (s * 0.115, -1.05, 1.77)], [0.018, 0.013, 0.005], 'head', 'antler', sides=(6, 3))
            S.tube([(s * 0.19, -0.815, 1.84), (s * 0.22, -0.93, 1.90)], [0.015, 0.005], 'head', 'antler', sides=(6, 3))
            S.tube([(s * 0.25, -0.79, 2.05), (s * 0.31, -0.86, 2.15)], [0.013, 0.004], 'head', 'antler', sides=(6, 3))
    S.tube([p(0, 0.59, 1.12), p(0, 0.645, 1.03), p(0, 0.66, 0.97)], [0.035 * g, 0.03 * g, 0.015 * g], 'tail', 'tail', sides=(8, 4))
    S.paint(p(0, 0.60, 1.03), (0.15 * g, 0.12 * g, 0.17 * g), 'pelvis', 'rump')
    ungulate_legs(S, 0.115 * g, -0.40 * g, 0.42 * g, 0.92 * g, 0.95 * g,
                  tuple(r * g for r in (0.075, 0.05, 0.026, 0.02, 0.024)), tuple(r * g for r in (0.10, 0.06, 0.027, 0.02, 0.024)), sc)
    quad_anims(S, 24, 22, 40)
    S.graze = (6, 100, -34)
    red = hexlin('#8a5634' if stag else '#94603c')

    def coat(P, N, T):
        n = fbm(P, 7.0, 4, 21)
        c = red * (0.9 + 0.2 * n)[:, None]
        c = mix(c, hexlin('#5b4130'), (tagw(T, 'mane') + sst(-0.45 * g, -0.75 * g, P[:, 1]) * tagw(T, 'body') * 0.6).clip(0, 1) * 0.8)
        c = mix(c, hexlin('#b1916d'), sst(0.86 * g, 0.76 * g, P[:, 2]) * (1 - tagw(T, 'leg', 'shin')) * 0.7)
        c = mix(c, hexlin('#5c4535'), tagw(T, 'shin', 'leg') * sst(0.6 * g, 0.4 * g, P[:, 2]) * 0.8)
        c = mix(c, hexlin('#dccaa4'), np.clip(tagw(T, 'rump') * 1.3, 0, 1))
        c = mix(c, hexlin('#4a3628'), tagw(T, 'muzzle', 'ear') * 0.6)
        c = mix(c, hexlin('#16120f'), tagw(T, 'eye', 'hoof'))
        ant = tagw(T, 'antler')
        tip = sst(1.95 * g, 2.12 * g, P[:, 2]) + sst(-0.95, -1.05, P[:, 1]) * ant
        c = mix(c, mix(hexlin('#5e4a36'), hexlin('#e0d6bd'), np.clip(tip, 0, 1)), ant)
        rough = 0.8 - 0.3 * tagw(T, 'eye') - 0.2 * ant
        return c, rough, np.zeros(len(P), np.float32)
    S.coat = coat
    S.biomes = {'Temperate': ((1.0, 1.0, 1.0), 0.0), 'Boreal': ((0.86, 0.86, 0.9), 0.0), 'Highland': ((0.95, 0.9, 0.86), 0.02)}
    S.notes = 'Red-deer-like; groups of 3-6 on rich temperate grassland and forest edges (SPEC-RENDER 9).'
    return S


# --------------------------------------------------------------------------- big cat

def bigcat(maned=False):
    S = Spec('BigCat', 'Lion' if maned else 'Lioness', 'cat')
    S.vs = 0.008
    S.lod0 = 780 if maned else 720
    g = 1.12 if maned else 1.0

    def p(x, y, z):
        return (x * g, y * g, z * g)
    S.bone('root', (0, 0, 0), p(0, -0.3, 0))
    S.bone('pelvis', p(0, 0.45, 0.80), p(0, 0.0, 0.80), 'root')
    S.bone('chest', p(0, 0.0, 0.80), p(0, -0.52, 0.86), 'pelvis')
    S.bone('neck', p(0, -0.52, 0.86), p(0, -0.74, 0.93), 'chest')
    S.bone('head', p(0, -0.74, 0.93), p(0, -0.99, 0.86), 'neck')
    S.bone('tail', p(0, 0.62, 0.86), p(0, 0.95, 0.50), 'pelvis')
    S.bone('tail2', p(0, 0.95, 0.50), p(0, 1.18, 0.25), 'tail')
    S.ell(p(0, -0.32, 0.80), (0.195 * g, 0.34 * g, 0.235 * g), 'chest', 'body', 0.10)
    S.ell(p(0, 0.06, 0.76), (0.17 * g, 0.38 * g, 0.18 * g), 'pelvis', 'body', 0.10)
    S.ell(p(0, 0.40, 0.79), (0.175 * g, 0.24 * g, 0.215 * g), 'pelvis', 'body', 0.09)
    S.cone(p(0, -0.52, 0.86), p(0, -0.71, 0.93), 0.155 * g, 0.125 * g, 'neck', 'body', 0.06)
    S.ell(p(0, -0.80, 0.93), (0.14 * g, 0.15 * g, 0.13 * g), 'head', 'head', 0.06)
    S.ell(p(0, -0.93, 0.885), (0.085 * g, 0.075 * g, 0.068 * g), 'head', 'muzzle', 0.045)
    S.ell(p(0, -0.91, 0.83), (0.06 * g, 0.06 * g, 0.035 * g), 'head', 'chin', 0.03)
    S.ell(p(0, -0.79, 1.0), (0.11 * g, 0.10 * g, 0.06 * g), 'head', 'head', 0.04)
    for s, sx in SIDES:
        S.ell(p(s * 0.12, -0.38, 0.86), (0.07 * g, 0.13 * g, 0.18 * g), 'chest', 'body', 0.06)
        S.ell(p(s * 0.085, -0.76, 1.03), (0.04 * g, 0.022 * g, 0.045 * g), 'head', 'ear', 0.02, rot=(0, s * -20, 0))
        S.paint(p(s * 0.065, -0.90, 0.955), (0.018, 0.018, 0.014), 'head', 'eye')
        x = s * 0.12 * g
        # front leg: digitigrade, heavy forearm, broad paw
        Sj, E, Wr = p(s * 0.12, -0.40, 0.76), p(s * 0.12, -0.35, 0.44), p(s * 0.115, -0.41, 0.11)
        Pw = p(s * 0.115, -0.46, 0.035)
        S.cone(Sj, E, 0.10 * g, 0.078 * g, 'F%s_upper' % sx, 'leg', 0.05)
        S.cone(E, Wr, 0.07 * g, 0.052 * g, 'F%s_lower' % sx, 'leg', 0.025)
        S.ell(Pw, (0.06 * g, 0.08 * g, 0.04 * g), 'F%s_foot' % sx, 'paw', 0.035)
        S.bone('F%s_upper' % sx, Sj, E, 'chest')
        S.bone('F%s_lower' % sx, E, Wr, 'F%s_upper' % sx)
        S.bone('F%s_foot' % sx, Wr, (Pw[0], Pw[1] - 0.06 * g, 0.0), 'F%s_lower' % sx)
        H, Kn, Hk = p(s * 0.12, 0.42, 0.78), p(s * 0.125, 0.30, 0.48), p(s * 0.115, 0.52, 0.20)
        Pw2 = p(s * 0.115, 0.46, 0.032)
        S.cone(H, Kn, 0.14 * g, 0.08 * g, 'H%s_thigh' % sx, 'leg', 0.06)
        S.cone(Kn, Hk, 0.068 * g, 0.044 * g, 'H%s_shin' % sx, 'leg', 0.025)
        S.cone(Hk, (Pw2[0], Pw2[1] + 0.02 * g, 0.06 * g), 0.044 * g, 0.042 * g, 'H%s_foot' % sx, 'leg', 0.02)
        S.ell(Pw2, (0.054 * g, 0.072 * g, 0.036 * g), 'H%s_foot' % sx, 'paw', 0.03)
        S.bone('H%s_thigh' % sx, H, Kn, 'pelvis')
        S.bone('H%s_shin' % sx, Kn, Hk, 'H%s_thigh' % sx)
        S.bone('H%s_foot' % sx, Hk, (Pw2[0], Pw2[1] - 0.05 * g, 0.0), 'H%s_shin' % sx)
    S.tube([p(0, 0.60, 0.87), p(0, 0.78, 0.76), p(0, 0.95, 0.52), p(0, 1.06, 0.33), p(0, 1.16, 0.25)],
           [0.04 * g, 0.03 * g, 0.025 * g, 0.022 * g, 0.02 * g], 'tail', 'tail', sides=(8, 4))
    S.ell(p(0, 1.19, 0.245), (0.035 * g, 0.06 * g, 0.035 * g), 'tail2', 'tuft', 0.015)
    if maned:
        S.ell(p(0, -0.64, 0.95), (0.235 * g, 0.20 * g, 0.27 * g), 'neck', 'mane', 0.08, rot=(-15, 0, 0))
        S.ell(p(0, -0.52, 0.84), (0.20 * g, 0.18 * g, 0.26 * g), 'neck', 'mane', 0.08)
        S.ell(p(0, -0.72, 0.99), (0.20 * g, 0.08 * g, 0.20 * g), 'head', 'mane', 0.05)
        S.ell(p(0, -0.10, 0.60), (0.08 * g, 0.28 * g, 0.04 * g), 'pelvis', 'mane', 0.06)
        S.shag = {'mane': (0.022 * g, 16.0)}
    S.anims.append(('Walk', 30, 'walk', dict(swing=24, knee=46)))
    S.anims.append(('Idle', 48, 'idle', {}))
    S.shape_keys = [('Stalk', 'graze_pose')]      # head and neck lowered, the stalking look
    S.graze = (3, 28, -12)
    tawny, belly = hexlin('#b48955'), hexlin('#dccaa6')

    def coat(P, N, T):
        n = fbm(P, 6.0, 4, 31)
        c = tawny * (0.92 + 0.16 * n)[:, None]
        c = mix(c, hexlin('#9c7446'), sst(0.86 * g, 0.98 * g, P[:, 2]) * tagw(T, 'body') * 0.6)
        c = mix(c, belly, sst(0.70 * g, 0.62 * g, P[:, 2]) * tagw(T, 'body') * 0.8)
        c = mix(c, belly, np.clip(tagw(T, 'muzzle', 'chin') * 0.8, 0, 1))
        c = mix(c, hexlin('#3a2a1c'), tagw(T, 'ear') * 0.7)
        c = mix(c, hexlin('#1c140e'), tagw(T, 'tuft', 'eye'))
        c = mix(c, hexlin('#7d5a3c'), tagw(T, 'paw') * 0.3)
        if maned:
            m = tagw(T, 'mane')
            mc = mix(hexlin('#7a5432'), hexlin('#2e1f14'), sst(0.95 * g, 0.65 * g, P[:, 2]) * 0.7 + 0.3 * fbm(P, 12.0, 3, 33))
            c = mix(c, mc, m)
        rough = 0.82 - 0.4 * tagw(T, 'eye')
        return c, rough, np.zeros(len(P), np.float32)
    S.coat = coat
    S.biomes = {'Savanna': ((1.0, 1.0, 1.0), 0.0), 'Desert': ((1.12, 1.06, 0.95), 0.02), 'Snow': ((1.25, 1.28, 1.35), 0.0)}
    S.notes = 'Generic big cat (lion-like). Prides of 1-4 near herds; Snow tint gives a pale mountain cat.'
    return S


# --------------------------------------------------------------------------- birds

def bird_starling():
    S = Spec('Bird', 'Starling', 'bird')
    S.walker = False
    S.vs = 0.0016
    S.lod0 = 200
    S.tex = 512
    S.lods = (1.0, 0.45, 0.2)
    S.sheen = 0.0
    S.noise = (0.0005, 60.0, 0.0002, 400.0)
    S.bone('root', (0, 0, 0), (0, -0.05, 0))
    S.bone('body', (0, 0.06, 0), (0, -0.06, 0.005), 'root')
    S.bone('head', (0, -0.06, 0.005), (0, -0.12, 0.01), 'body')
    S.bone('tail', (0, 0.06, 0), (0, 0.11, 0), 'body')
    S.ell((0, 0, 0), (0.028, 0.07, 0.03), 'body', 'body', 0.01)
    S.ell((0, -0.072, 0.012), (0.021, 0.024, 0.021), 'head', 'head', 0.012)
    S.tube([(0, -0.092, 0.011), (0, -0.121, 0.006)], [0.0065, 0.0015], 'head', 'beak', sides=(6, 3))
    S.ell((0, 0.085, 0.002), (0.022, 0.035, 0.0035), 'tail', 'tail', 0.008)
    for s, sx in SIDES:
        S.bone('wing_%s' % sx, (s * 0.02, -0.005, 0.01), (s * 0.09, 0.0, 0.012), 'body')
        S.bone('hand_%s' % sx, (s * 0.09, 0.0, 0.012), (s * 0.20, 0.03, 0.012), 'wing_%s' % sx)
        S.ell((s * 0.055, -0.002, 0.009), (0.045, 0.034, 0.0045), 'wing_%s' % sx, 'wing', 0.012)
        S.ell((s * 0.125, 0.008, 0.011), (0.05, 0.026, 0.0038), 'hand_%s' % sx, 'wing', 0.008, rot=(0, 0, s * 14))
        S.ell((s * 0.165, 0.028, 0.012), (0.042, 0.014, 0.003), 'hand_%s' % sx, 'wing', 0.008, rot=(0, 0, s * 30))
        S.paint((s * 0.014, -0.08, 0.018), (0.005, 0.005, 0.005), 'head', 'eye')
    S.anims = [('Flap', 8, 'flap', dict(amp=45)), ('Glide', 24, 'glide', {})]
    S.shape_keys = [('WingUp', 'wing_up'), ('WingDown', 'wing_down')]
    S.flock_scale = 3.0

    def coat(P, N, T):
        n = fbm(P, 90.0, 3, 41)
        c = hexlin('#191b18') * (0.9 + 0.2 * n)[:, None]
        # iridescent green-purple sheen on the back, pale speckles below
        c = mix(c, hexlin('#233127'), sst(0.0, 0.02, P[:, 2]) * tagw(T, 'body', 'head') * 0.6)
        sp = sst(0.55, 0.75, vnoise(P, 260.0, 43)) * sst(0.0, -0.015, P[:, 2])
        c = mix(c, hexlin('#a49a86'), sp * 0.6)
        c = mix(c, hexlin('#d2a93a'), tagw(T, 'beak'))
        c = mix(c, hexlin('#2a2622'), tagw(T, 'wing') * sst(0.12, 0.2, np.abs(P[:, 0])) * 0.5)
        rough = 0.42 + 0.2 * tagw(T, 'wing')
        return c, rough, np.zeros(len(P), np.float32)
    S.coat = coat
    S.biomes = {'Default': ((1.0, 1.0, 1.0), 0.0), 'Rook': ((0.8, 0.8, 0.85), 0.05)}
    S.notes = 'Dark flock bird for murmurations (40-80+ instances, about 2 px each). Real size 0.4 m span; the stand-ins exaggerate to ~1.9 m (flock_scale 3-4 reads at game zoom).'
    return S


def bird_egret():
    S = Spec('Bird', 'Egret', 'bird')
    S.walker = False
    S.vs = 0.004
    S.lod0 = 320
    S.tex = 512
    S.sheen = 0.2
    S.noise = (0.001, 20.0, 0.0004, 150.0)
    S.bone('root', (0, 0, 0), (0, -0.1, 0))
    S.bone('body', (0, 0.16, 0), (0, -0.14, 0.03), 'root')
    S.bone('head', (0, -0.14, 0.03), (0, -0.40, 0.05), 'body')
    S.bone('tail', (0, 0.12, 0), (0, 0.52, -0.04), 'body')
    S.ell((0, 0, 0), (0.065, 0.16, 0.065), 'body', 'body', 0.02)
    S.cone((0, -0.12, 0.025), (0, -0.20, 0.065), 0.045, 0.028, 'head', 'body', 0.02)
    S.ell((0, -0.235, 0.065), (0.026, 0.048, 0.028), 'head', 'head', 0.012)
    S.tube([(0, -0.27, 0.063), (0, -0.40, 0.05)], [0.011, 0.002], 'head', 'beak', sides=(6, 3))
    S.ell((0, 0.17, 0.0), (0.05, 0.08, 0.009), 'tail', 'tail', 0.015)
    for s, sx in SIDES:
        S.bone('wing_%s' % sx, (s * 0.04, 0.0, 0.02), (s * 0.32, 0.01, 0.02), 'body')
        S.bone('hand_%s' % sx, (s * 0.32, 0.01, 0.02), (s * 0.72, 0.05, 0.02), 'wing_%s' % sx)
        S.ell((s * 0.18, 0.0, 0.02), (0.165, 0.115, 0.008), 'wing_%s' % sx, 'wing', 0.02)
        S.ell((s * 0.50, 0.03, 0.022), (0.22, 0.085, 0.006), 'hand_%s' % sx, 'wing', 0.012, rot=(0, 0, s * 8))
        S.tube([(s * 0.02, 0.12, -0.02), (s * 0.022, 0.40, -0.04), (s * 0.022, 0.53, -0.045)], [0.009, 0.007, 0.005], 'tail', 'leg', sides=(6, 3))
        S.paint((s * 0.02, -0.25, 0.078), (0.008, 0.008, 0.008), 'head', 'eye')
    S.anims = [('Flap', 24, 'flap', dict(amp=32)), ('Glide', 24, 'glide', {})]
    S.shape_keys = [('WingUp', 'wing_up'), ('WingDown', 'wing_down')]
    S.flock_scale = 1.3

    def coat(P, N, T):
        n = fbm(P, 30.0, 3, 51)
        c = hexlin('#eeebe3') * (0.95 + 0.06 * n)[:, None]
        c = mix(c, hexlin('#d9b23a'), tagw(T, 'beak'))
        c = mix(c, hexlin('#161514'), tagw(T, 'leg', 'eye'))
        rough = 0.62 - 0.2 * tagw(T, 'beak', 'eye')
        return c, rough, np.zeros(len(P), np.float32)
    S.coat = coat
    S.biomes = {'Default': ((1.0, 1.0, 1.0), 0.0), 'Grey': ((0.62, 0.64, 0.68), 0.0)}
    S.notes = 'White wader for the loose V over deltas (5-9 birds, about 4 px each). Grey tint gives a heron.'
    return S


# --------------------------------------------------------------------------- sea

def fish_sardine():
    S = Spec('Fish', 'Sardine', 'fish')
    S.walker = False
    S.vs = 0.0009
    S.lod0 = 160
    S.tex = 512
    S.lods = (1.0, 0.45, 0.2)
    S.sheen = 0.0
    S.noise = (0.0002, 80.0, 0.00008, 500.0)
    S.bone('root', (0, 0, 0), (0, -0.05, 0))
    S.bone('head', (0, -0.04, 0), (0, -0.125, 0), 'root')
    S.bone('body', (0, 0.04, 0), (0, -0.04, 0), 'root')
    S.bone('tail0', (0, 0.04, 0), (0, 0.10, 0), 'body')
    S.bone('tail1', (0, 0.10, 0), (0, 0.16, 0), 'tail0')
    S.ell((0, -0.02, 0), (0.018, 0.095, 0.03), 'body', 'body', 0.008)
    S.ell((0, -0.088, 0.0), (0.016, 0.035, 0.025), 'head', 'head', 0.01)
    S.cone((0, 0.045, 0.0), (0, 0.112, 0.0), 0.016, 0.0045, 'tail0', 'body', 0.008)
    S.ell((0, -0.005, 0.028), (0.003, 0.022, 0.012), 'body', 'fin', 0.006, rot=(-20, 0, 0))
    for s, sx in SIDES:
        S.ell((0, 0.128, s * 0.017), (0.003, 0.026, 0.011), 'tail1', 'fin', 0.008, rot=(s * -48, 0, 0))
        S.ell((s * 0.014, -0.06, -0.014), (0.010, 0.014, 0.0028), 'head', 'fin', 0.006, rot=(0, s * 30, 20))
        S.paint((s * 0.014, -0.10, 0.006), (0.006, 0.006, 0.006), 'head', 'eye')
    S.anims = [('Swim', 12, 'swim', dict(amp=16))]
    S.shape_keys = [('TailLeft', 'tail_left'), ('TailRight', 'tail_right')]
    S.flock_scale = 3.0

    def coat(P, N, T):
        n = fbm(P, 120.0, 3, 61)
        top = sst(-0.002, 0.016, P[:, 2] + 0.003 * n)
        c = mix(hexlin('#c4ccd0'), hexlin('#1f4555'), top)
        c = mix(c, hexlin('#e6e8e6'), sst(-0.012, -0.025, P[:, 2]))
        spots = sst(0.6, 0.8, vnoise(P * V(1, 1, 3), 140.0, 63)) * sst(0.004, 0.0, np.abs(P[:, 2] - 0.009)) * sst(0.05, 0.0, P[:, 1])
        c = mix(c, hexlin('#1d2a30'), spots * 0.8)
        fin = tagw(T, 'fin')
        c = mix(c, hexlin('#7f8d93'), fin * 0.7)
        c = mix(c, hexlin('#0d0f10'), tagw(T, 'eye'))
        metal = (1 - top) * 0.75 * (1 - fin)
        rough = 0.28 + 0.25 * fin
        return c, rough, metal.astype(np.float32)
    S.coat = coat
    S.biomes = {'Default': ((1.0, 1.0, 1.0), 0.0), 'Tropical': ((0.9, 1.05, 1.08), 0.0)}
    S.notes = 'Silver shoal fish (bait ball 30-50 px). Real 0.25 m; the stand-ins use 0.75-1.15 m, so instance at flock_scale 3.'
    return S


def whale_humpback():
    S = Spec('Whale', 'Humpback', 'whale')
    S.walker = False
    S.vs = 0.035
    S.lod0 = 800
    S.lods = (1.0, 0.4, 0.12, 0.05)
    S.sheen = 0.0
    S.noise = (0.02, 1.2, 0.006, 8.0)
    z = -1.2      # body axis below the sea surface (origin)

    def p(x, y, zz):
        return (x, y, zz + z)
    S.bone('root', (0, 0, 0), (0, -1.0, 0))
    S.bone('body', p(0, -1.0, 0), p(0, -4.0, 0.05), 'root')
    S.bone('head', p(0, -4.0, 0.05), p(0, -7.2, -0.1), 'body')
    S.bone('tail0', p(0, -1.0, 0), p(0, 2.6, 0.08), 'root')
    S.bone('tail1', p(0, 2.6, 0.08), p(0, 4.8, 0.12), 'tail0')
    S.bone('tail2', p(0, 4.8, 0.12), p(0, 6.3, 0.15), 'tail1')
    S.bone('fluke', p(0, 6.3, 0.15), p(0, 7.6, 0.15), 'tail2')
    S.ell(p(0, -5.3, 0.1), (1.0, 1.7, 0.6), 'head', 'head', 0.7)
    S.ell(p(0, -6.45, 0.05), (0.62, 1.0, 0.36), 'head', 'head', 0.45)
    S.ell(p(0, -4.7, -0.42), (1.25, 2.4, 0.86), 'head', 'throat', 0.8)
    S.ell(p(0, -2.4, 0.0), (1.58, 3.2, 1.5), 'body', 'body', 1.0)
    S.ell(p(0, 1.0, 0.05), (1.22, 3.0, 1.18), 'tail0', 'body', 1.0)
    S.cone(p(0, 3.4, 0.1), p(0, 6.25, 0.15), 0.92, 0.26, 'tail1', 'body', 0.6)
    S.ell(p(0, 5.0, 0.12), (0.32, 1.7, 0.72), 'tail2', 'body', 0.4)
    S.ell(p(0, 2.55, 1.18), (0.09, 0.48, 0.30), 'tail0', 'body', 0.25, rot=(-28, 0, 0))
    for s, sx in SIDES:
        S.bone('fin_%s' % sx, p(s * 1.3, -3.4, -0.6), p(s * 4.6, -2.0, -1.4), 'body')
        S.ell(p(s * 1.25, 6.9, 0.15), (1.45, 0.45, 0.07), 'fluke', 'fluke', 0.22, rot=(0, 0, s * -18))
        S.ell(p(s * 2.95, -2.75, -1.0), (1.85, 0.36, 0.085), 'fin_%s' % sx, 'fin', 0.25, rot=(0, s * 15, s * -22))
        for i in range(5):     # knobbed leading edge of the flipper
            t = (i + 0.5) / 5.0
            S.ell(p(s * (1.4 + 3.1 * t), -3.18 + 1.2 * t, -0.55 - 0.66 * t), (0.13, 0.13, 0.09), 'fin_%s' % sx, 'fin', 0.08)
        for i in range(5):     # tubercles on the rostrum
            S.ell(p(s * (0.22 + 0.06 * i), -6.9 + 0.55 * i, 0.36 + 0.07 * i), (0.09, 0.09, 0.07), 'head', 'knob', 0.06)
        S.paint(p(s * 1.18, -4.2, -0.32), (0.08, 0.08, 0.08), 'head', 'eye')
    S.anims = [('Swim', 72, 'whale_swim', {})]
    S.shape_keys = [('FlukeUp', 'fluke_up'), ('FlukeDown', 'fluke_down')]

    def coat(P, N, T):
        n = fbm(P, 0.8, 4, 71)
        zr = P[:, 2] - z
        back = hexlin('#22303a') * (0.88 + 0.24 * n)[:, None]
        white = hexlin('#d9dddb')
        # white belly with a ragged boundary, pleated throat, white flippers and fluke undersides
        bl = sst(-0.35 + 0.35 * n, -0.75 + 0.35 * n, zr)
        c = mix(back, white * (0.9 + 0.1 * n)[:, None], bl * 0.95)
        pleat = 0.5 + 0.5 * np.sin(P[:, 0] * 2 * math.pi / 0.22)
        c = mix(c, hexlin('#9aa3a6'), tagw(T, 'throat') * sst(-0.3, -0.6, zr) * pleat * 0.6)
        fin = tagw(T, 'fin')
        c = mix(c, mix(white, back, sst(-0.15, 0.25, N[:, 2]) * 0.45), fin)
        fl = tagw(T, 'fluke')
        c = mix(c, white * 0.95, fl * sst(0.1, -0.4, N[:, 2]) * sst(0.2, 0.7, vnoise(P, 2.5, 73)))
        scars = sst(0.72, 0.85, vnoise(P, 6.0, 75)) * (1 - bl)
        c = mix(c, hexlin('#8c9595'), scars * 0.5)
        c = mix(c, hexlin('#a7a79d'), tagw(T, 'knob') * 0.6)
        c = mix(c, hexlin('#0c0f11'), tagw(T, 'eye'))
        rough = 0.32 + 0.12 * n + 0.2 * tagw(T, 'knob')
        return c, rough, np.zeros(len(P), np.float32)
    S.coat = coat
    S.biomes = {'Default': ((1.0, 1.0, 1.0), 0.0), 'Cold': ((0.85, 0.9, 0.95), 0.0)}
    S.notes = 'Humpback-like; mother 14 m, calf = same mesh at scale 0.45. Rare (SPEC-RENDER: "a few whales").'
    return S


def all_specs():
    return [grazer_wildebeest(), grazer_antelope(), grazer_bison(), deer(True), deer(False), bigcat(False), bigcat(True),
            bird_starling(), bird_egret(), whale_humpback(), fish_sardine()]


def get(key):
    for s in all_specs():
        if s.key == key:
            return s
    raise KeyError(key)
