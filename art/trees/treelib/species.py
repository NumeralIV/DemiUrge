# DEMIURGE tree families: procedural growth of the five species and their geometric LODs.
#
# A species builder returns a Model: a skeleton of stems (tapered tubes, levels 0..3), leaf-card records,
# palm fronds and small extras, plus shading fields (ambient occlusion and the "volumetric" card normal).
# build_lod(model, lod) turns a model into a mesh builder for LOD 0, 1 or 2; the impostor (LOD 3) is made in
# export.py from renders of LOD 0. All sizes are real-world metres; the trunk base is at the origin.
import math
import numpy as np
from mathutils import Vector, Matrix, noise
from .mesh import MB, SEC_BARK, SEC_LEAF
from . import tiles
from .bark import BARK_TILE

UP = Vector((0.0, 0.0, 1.0))


def V(x, y, z):
    return Vector((float(x), float(y), float(z)))


def smooth(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def orth(d):
    a = UP if abs(d.z) < 0.9 else V(1, 0, 0)
    u = d.cross(a).normalized()
    v = d.cross(u).normalized()
    return u, v


def rot_about(vec, axis, ang):
    return Matrix.Rotation(ang, 3, axis.normalized()) @ vec


def dir_from(theta_from_up, az):
    s = math.sin(theta_from_up)
    return V(s * math.cos(az), s * math.sin(az), math.cos(theta_from_up))


class Rng:
    def __init__(self, seed):
        self.g = np.random.default_rng(seed)

    def u(self, a=0.0, b=1.0):
        return float(self.g.uniform(a, b))

    def i(self, a, b):
        return int(self.g.integers(a, b + 1))

    def vec(self, s=1.0):
        return Vector(self.g.normal(0, 1, 3).tolist()) * s

    def chance(self, p):
        return self.g.random() < p


# ------------------------------------------------------------------ skeleton
class Stem:
    def __init__(self, pts, rads, level, phase=0.0, path0=0.0, tint=(1, 1, 1), flare=0.0):
        self.pts = pts
        self.rads = rads
        self.level = level
        self.phase = phase
        self.path0 = path0
        self.tint = tint
        self.flare = flare
        self.cum = [0.0]
        for i in range(1, len(pts)):
            self.cum.append(self.cum[-1] + (pts[i] - pts[i - 1]).length)
        self.length = self.cum[-1]

    def at(self, t):
        s = t * self.length
        for i in range(1, len(self.pts)):
            if s <= self.cum[i] or i == len(self.pts) - 1:
                l = max(1e-6, self.cum[i] - self.cum[i - 1])
                f = min(1.0, max(0.0, (s - self.cum[i - 1]) / l))
                p = self.pts[i - 1].lerp(self.pts[i], f)
                d = (self.pts[i] - self.pts[i - 1]).normalized()
                r = self.rads[i - 1] + (self.rads[i] - self.rads[i - 1]) * f
                return p, d, r, s
        return self.pts[-1], (self.pts[-1] - self.pts[-2]).normalized(), self.rads[-1], self.length


def grow(start, d, length, r0, r1, seg, rng, trop=None, gnarl=0.0, taper=0.85, bend_fn=None):
    n = max(2, int(math.ceil(length / seg)))
    step = length / n
    pts = [start.copy()]
    rads = [r0]
    p = start.copy()
    d = d.normalized()
    for i in range(n):
        t = (i + 1) / n
        if trop is not None:
            d = d + trop * step
        if gnarl > 0:
            d = d + rng.vec(gnarl * math.sqrt(step))
        if bend_fn is not None:
            d = bend_fn(d, t)
        d.normalize()
        p = p + d * step
        pts.append(p.copy())
        rads.append(r1 + (r0 - r1) * (1.0 - t) ** taper)
    return pts, rads


class Ellipsoid:
    def __init__(self, c, rx, ry, rz, lump=0.0, seed=0.0):
        self.c, self.r = c, V(rx, ry, rz)
        self.lump = lump
        self.seed = V(seed * 3.1, seed * 1.7, seed * 2.3)

    def lumpf(self, d):
        if self.lump <= 0:
            return 1.0
        return 1.0 + self.lump * noise.noise(d * 2.2 + self.seed)

    def depth(self, p):
        q = p - self.c
        e = V(q.x / self.r.x, q.y / self.r.y, q.z / self.r.z)
        L = e.length
        if L < 1e-6:
            return 0.0
        return L / self.lumpf(e / L)

    def grad(self, p):
        q = p - self.c
        g = V(q.x / self.r.x ** 2, q.y / self.r.y ** 2, q.z / self.r.z ** 2)
        return g.normalized() if g.length > 1e-8 else UP.copy()

    def dist_to_surface(self, p, d):
        """Distance from p (inside) along unit d to the (lumpy) surface; 0 if p is outside."""
        q = p - self.c
        a = V(q.x / self.r.x, q.y / self.r.y, q.z / self.r.z)
        b = V(d.x / self.r.x, d.y / self.r.y, d.z / self.r.z)
        # lumpiness: scale radii by the lump factor in the direction of the probable exit point
        A = b.dot(b)
        B = 2 * a.dot(b)
        C = a.dot(a) - 1
        disc = B * B - 4 * A * C
        if disc < 0:
            return 0.0
        t = (-B + math.sqrt(disc)) / (2 * A)
        ex = (a + b * t)
        k = self.lumpf(ex.normalized()) if ex.length > 1e-6 else 1.0
        C2 = a.dot(a) - k * k
        disc = B * B - 4 * A * C2
        if disc < 0:
            return 0.0
        return max(0.0, (-B + math.sqrt(disc)) / (2 * A))


class Model:
    def __init__(self, species, variant, H, R):
        self.species, self.variant = species, variant
        self.H, self.R = H, R
        self.stems = []
        self.cards = []      # dicts
        self.fronds = []     # dicts (palm)
        self.nuts = []       # (centre, radius, tint)
        self.sway_k = 0.012
        self.bend_k = 0.02
        self.Lref = H * 0.4
        self.top_z = H * 0.6
        self.crown_c = V(0, 0, H * 0.6)
        self.crown_r = V(R, R, H * 0.3)
        self.bark_tile = BARK_TILE[species]
        self.lod_cards = [(1.0, 1.0), (0.42, 1.45), (0.13, 2.5)]
        self.sides = {0: (12, 8, 5), 1: (7, 5, 3), 2: (5, 3, 0), 3: (3, 0, 0)}
        self.notes = ''

    # shading fields, overridden per species with closures
    def ao_leaf(self, p, card):
        return 1.0

    def nrm_leaf(self, p, card, face):
        return face

    def ao_bark(self, p):
        h = max(0.0, p.z) / self.H
        return 0.55 + 0.45 * smooth(0.0, 0.5, 1.0 - h)


def add_card(model, base, dL, n_face, L, W, tile, rng, droop=0.15, extra=None):
    dL = dL.normalized()
    dW = dL.cross(n_face)
    if dW.length < 1e-4:
        dW = orth(dL)[0]
    dW.normalize()
    nf = dW.cross(dL).normalized()
    if nf.dot(n_face) < 0:
        nf = -nf
        dW = -dW
    c = {'base': base.copy(), 'dL': dL, 'dW': dW, 'nf': nf, 'L': L, 'W': W, 'tile': tile, 'droop': droop,
         'phase': 0.0, 'path': 0.0, 'tint': (1, 1, 1), 'id': len(model.cards)}
    if extra:
        c.update(extra)
    model.cards.append(c)
    return c


# Verification fix: fine branches inside the crown read as pale (broadleaf) or pink (spruce) sticks through LOD1/LOD2
# cards at 50-300 m. Bark under foliage is darker and greyer than a lit trunk: tint (sRGB, the material does pow 2.2).
TWIG_TINT = (0.78, 0.77, 0.76)               # broadleaf level 2-3 branches -> x0.58 linear
CONIFER_BRANCH_TINT = (0.66, 0.66, 0.66)     # live spruce/fir branches -> x0.40 linear


def card_tint(rng, warm=0.0):
    v = 0.92 + rng.u(-0.06, 0.05)
    return (min(1.0, v + 0.03 * warm + rng.u(-0.02, 0.02)), min(1.0, v + rng.u(-0.02, 0.02)), min(1.0, v - 0.04 * warm + rng.u(-0.03, 0.02)))


# ------------------------------------------------------------------ broadleaf
BROADLEAF = [
    # H, R, bole, mode, limbs, dbh, tiles, note
    dict(H=17.0, R=7.5, bole=0.34, mode='decurrent', limbs=4, dbh=0.62, tiles=('broadleaf_b', 'broadleaf_b', 'broadleaf_c'), note='mature oak, broad domed crown'),
    dict(H=23.0, R=6.2, bole=0.36, mode='excurrent', limbs=0, dbh=0.55, tiles=('broadleaf_a', 'broadleaf_a', 'broadleaf_c'), note='tall beech, leader to the top'),
    dict(H=11.0, R=3.9, bole=0.28, mode='excurrent', limbs=0, dbh=0.24, tiles=('broadleaf_a', 'broadleaf_c', 'broadleaf_c'), note='young tree'),
    dict(H=15.0, R=9.0, bole=0.25, mode='decurrent', limbs=5, dbh=0.9, tiles=('broadleaf_b', 'broadleaf_a', 'broadleaf_b'), note='old spreading oak'),
]


def build_broadleaf(v, seed):
    P = BROADLEAF[v]
    rng = Rng(seed)
    H, R = P['H'], P['R']
    m = Model('broadleaf', v, H, R)
    m.notes = P['note']
    hb = P['bole'] * H
    cb = hb * 0.82
    env = Ellipsoid(V(0, 0, (cb + H) / 2), R, R * rng.u(0.88, 1.0), (H - cb) / 2, lump=0.13, seed=seed % 97)
    m.crown_c, m.crown_r = env.c, env.r
    m.top_z = env.c.z
    m.Lref = R * 0.9
    m.sway_k = 0.010
    m.bend_k = 0.022
    S = (H / 17.0) ** 0.5
    r0 = P['dbh'] * 0.5
    lean = rng.vec(0.04)
    lean.z = 0
    # trunk
    if P['mode'] == 'decurrent':
        tl = hb + 0.4 + H * 0.06
        pts, rads = grow(V(0, 0, -0.4), UP + lean, tl, r0, r0 * 0.72, 0.6, rng, gnarl=0.03, taper=0.6)
    else:
        tl = H * 0.93
        pts, rads = grow(V(0, 0, -0.4), UP + lean, tl, r0, 0.03, 0.6, rng, gnarl=0.035, taper=0.9)
    trunk = Stem(pts, rads, 0, tint=(1, 1, 1), flare=0.6)
    m.stems.append(trunk)

    l1 = []
    if P['mode'] == 'decurrent':
        n = P['limbs']
        az0 = rng.u(0, 6.28)
        for k in range(n + 1):
            t = rng.u(0.8, 1.0)
            p, d, r, s = trunk.at(t)
            if k == n:      # central leader
                tgt = env.c + V(rng.u(-0.5, 0.5), rng.u(-0.5, 0.5), env.r.z * 0.85)
            else:
                az = az0 + k * 2 * math.pi / n + rng.u(-0.35, 0.35)
                rr = env.r.x * rng.u(0.6, 0.85)
                tgt = env.c + V(math.cos(az) * rr, math.sin(az) * rr, env.r.z * rng.u(-0.05, 0.55))
            dv = (tgt - p)
            ln = dv.length
            lp, lr = grow(p, dv.normalized(), ln, r * rng.u(0.55, 0.7), 0.035, 0.6, rng, trop=UP * 0.035, gnarl=0.06, taper=0.8)
            l1.append(Stem(lp, lr, 1, phase=rng.u(), path0=0.0))
    else:
        n = int((tl - hb) * 1.5)
        az = rng.u(0, 6.28)
        for k in range(n):
            t = (hb + (tl - hb - 0.6) * (k + rng.u(0.0, 0.6)) / n) / tl
            p, d, r, s = trunk.at(t)
            f = (p.z - hb) / max(1e-3, H - hb)
            az += math.radians(137.5) + rng.u(-0.3, 0.3)
            th = math.radians(72 - 40 * f + rng.u(-8, 8))
            dd = dir_from(th, az)
            ln = env.dist_to_surface(p, dd) * rng.u(0.85, 1.0)
            if ln < 0.6:
                continue
            lp, lr = grow(p, dd, ln, min(r * 0.7, 0.02 + ln * 0.022), 0.025, 0.6, rng, trop=UP * 0.04, gnarl=0.06, taper=0.8)
            l1.append(Stem(lp, lr, 1, phase=rng.u(), path0=0.0))
    m.stems += l1
    # level 2 and 3 + cards
    for limb in l1:
        n2 = int(limb.length * 1.25) + 1
        for k in range(n2):
            t = 0.22 + 0.73 * (k + rng.u(0, 0.8)) / n2
            p, d, r, s = limb.at(t)
            u, w = orth(d)
            a = rng.u(0, 6.28)
            ax = u * math.cos(a) + w * math.sin(a)
            dd = rot_about(d, ax, math.radians(rng.u(35, 62)))
            dd = (dd + UP * 0.25).normalized()
            want = limb.length * rng.u(0.35, 0.55) * (1.0 - 0.4 * t) + 0.8
            ln = min(want, env.dist_to_surface(p, dd) * rng.u(0.85, 1.0))
            if ln < 0.5:
                continue
            bp, br = grow(p, dd, ln, max(0.012, min(r * 0.6, 0.012 + ln * 0.02)), 0.012, 0.6, rng, trop=UP * 0.05, gnarl=0.1)
            b2 = Stem(bp, br, 2, phase=limb.phase, path0=s, tint=TWIG_TINT)
            m.stems.append(b2)
            clump_c, _, _, _ = b2.at(0.7)
            clump_r = 0.45 * ln + 0.7 * S
            n3 = int(ln * 2.0) + 1
            for j in range(n3):
                t3 = 0.2 + 0.8 * (j + rng.u(0, 0.8)) / n3
                p3, d3, r3, s3 = b2.at(t3)
                u3, w3 = orth(d3)
                a3 = rng.u(0, 6.28)
                d3n = rot_about(d3, u3 * math.cos(a3) + w3 * math.sin(a3), math.radians(rng.u(40, 65)))
                ln3 = min(rng.u(0.6, 1.4) * S, env.dist_to_surface(p3, d3n) + 0.4)
                if ln3 < 0.25:
                    continue
                tp, trd = grow(p3, d3n, ln3, 0.012, 0.005, 0.7, rng, trop=UP * 0.1, gnarl=0.15)
                tw = Stem(tp, trd, 3, phase=limb.phase, path0=s + s3, tint=TWIG_TINT)
                m.stems.append(tw)
                nc = 1 + int(ln3 / 0.8)
                for q in range(nc):
                    tq = 0.45 + 0.55 * (q + rng.u(0.3, 1.0)) / nc
                    pc, dc, _, sc = tw.at(tq)
                    rad = env.grad(pc)
                    dL = (dc * 0.55 + rad * 0.45 + UP * 0.15 + rng.vec(0.2)).normalized()
                    nfc = (UP * 0.6 + rad * 0.4 + rng.vec(0.45)).normalized()
                    sz = 1.2 * S * rng.u(0.85, 1.2)
                    outer = env.depth(pc) > 0.8
                    tl_ = P['tiles'][2] if (outer and rng.chance(0.35)) else P['tiles'][rng.i(0, 1)]
                    add_card(m, pc, dL, nfc, sz, sz, tl_, rng, droop=0.12,
                             extra={'phase': limb.phase, 'path': s + s3 + sc, 'clump': (clump_c, clump_r), 'tint': card_tint(rng, 1.0 if outer else 0.0)})
            # terminal card on the branch tip
            pc, dc, _, _ = b2.at(1.0)
            add_card(m, pc, (dc + UP * 0.3).normalized(), (UP + rng.vec(0.4)).normalized(), 1.2 * S, 1.2 * S, P['tiles'][0], rng,
                     extra={'phase': limb.phase, 'path': s + ln, 'clump': (clump_c, clump_r), 'tint': card_tint(rng, 1.0)})

    def ao_leaf(p, card):
        d = env.depth(p)
        a_tree = smooth(0.35, 1.02, d)
        cc, cr = card.get('clump', (env.c, env.r.x))
        a_cl = smooth(0.0, 1.0, (p - cc).length / cr)
        below = smooth(0.1, -0.9, (p.z - env.c.z) / env.r.z)
        return (0.22 + 0.78 * (0.6 * a_tree + 0.4 * a_cl)) * (1.0 - 0.3 * below)

    def nrm_leaf(p, card, face):
        cc, cr = card.get('clump', (env.c, env.r.x))
        g = env.grad(p)
        q = (p - cc)
        cl = q.normalized() if q.length > 1e-4 else g
        n = g * 0.5 + cl * 0.35 + face * 0.15 + UP * 0.1
        return n.normalized()

    m.ao_leaf, m.nrm_leaf = ao_leaf, nrm_leaf
    m.sides = {0: (12, 8, 5), 1: (7, 5, 3), 2: (4, 3, 0)}
    return m


# ------------------------------------------------------------------ conifer (spruce / fir)
CONIFER = [
    dict(H=22.0, Rb=3.9, cb=0.16, dbh=0.45, fir=False, dead=0.0, note='mature spruce'),
    dict(H=28.0, Rb=4.3, cb=0.36, dbh=0.62, fir=False, dead=1.0, note='old tall spruce, dead lower whorls'),
    dict(H=11.0, Rb=2.8, cb=0.03, dbh=0.20, fir=False, dead=0.0, note='young spruce, full to the ground'),
    dict(H=17.0, Rb=2.6, cb=0.10, dbh=0.32, fir=True, dead=0.0, note='narrow fir spire'),
]


def build_conifer(v, seed):
    P = CONIFER[v]
    rng = Rng(seed)
    H, Rb = P['H'], P['Rb']
    m = Model('conifer', v, H, Rb)
    m.notes = P['note']
    cb = P['cb'] * H
    m.crown_c = V(0, 0, cb + (H - cb) * 0.35)
    m.crown_r = V(Rb, Rb, (H - cb) * 0.5)
    m.top_z = cb + (H - cb) * 0.08    # impostor top card: low, hidden inside the spire (at 0.28 it read as a plate)
    m.Lref = Rb
    m.sway_k = 0.011
    m.bend_k = 0.03
    m.lod_cards = [(1.0, 1.0), (0.45, 1.4), (0.16, 2.3)]
    m.sides = {0: (10, 7, 5), 1: (4, 3, 0)}
    S = (H / 22.0) ** 0.35
    lean = rng.vec(0.02)
    lean.z = 0
    # verification fix: gnarl 0.012 -> 0.004. The random walk bent the spruce stems 1.6-4.8 deg off plumb (2.2 m at the top
    # of the 28 m tree, more with wind); spruce and fir stems are near plumb. Same number of random draws, so nothing else moves.
    pts, rads = grow(V(0, 0, -0.4), UP + lean, H + 0.4, P['dbh'] * 0.5, 0.012, 0.8, rng, gnarl=0.004, taper=1.0)
    trunk = Stem(pts, rads, 0, flare=0.45)
    m.stems.append(trunk)

    def crown_r(z):
        if z < cb:
            return 0.0
        return Rb * max(0.0, (H - z) / (H - cb)) ** 0.95

    # dead stubs below the live crown
    if P['dead'] > 0 or cb > 1.5:
        z = 1.8
        while z < cb:
            for k in range(rng.i(2, 4)):
                if not rng.chance(0.6):
                    continue
                az = rng.u(0, 6.28)
                th = math.radians(rng.u(95, 120))
                ln = rng.u(0.3, 1.3) * S
                p, _, _, _ = trunk.at((z + 0.4) / (H + 0.4))
                bp, br = grow(p, dir_from(th, az), ln, 0.03, 0.008, 0.5, rng, gnarl=0.1)
                m.stems.append(Stem(bp, br, 1, phase=rng.u(), tint=(0.75, 0.72, 0.7)))
            z += rng.u(0.4, 0.7)
    z = cb
    whorl_az = rng.u(0, 6.28)
    sp0 = 0.5 * (H / 22.0) ** 0.4
    while z < H - 0.35:
        f = (z - cb) / max(1e-3, H - cb)
        n = rng.i(4, 6)
        whorl_az += math.radians(137.5 / 2)
        for k in range(n + (1 if rng.chance(0.5) else 0)):
            inter = k >= n
            zz = z + (rng.u(0.15, 0.4) * sp0 if inter else 0.0)
            ff = (zz - cb) / max(1e-3, H - cb)
            az = whorl_az + k * 2 * math.pi / n + rng.u(-0.25, 0.25)
            if P['fir']:
                th = math.radians(88 - 22 * ff + rng.u(-6, 6))
            else:
                th = math.radians(100 - 38 * ff + rng.u(-7, 7))
            ln = max(0.2, crown_r(zz) * rng.u(0.85, 1.08) * (0.55 if inter else 1.0)) + 0.12
            p, _, rtr, _ = trunk.at((zz + 0.4) / (H + 0.4))
            dd = dir_from(th, az)

            def bend(d, t, _up=not P['fir']):
                # spruce: sag in the middle, tip turns up
                if _up:
                    return d + UP * (0.06 * t - 0.035)
                return d + UP * 0.01
            bp, br = grow(p, dd, ln, min(0.012 + ln * 0.018, rtr * 0.5), 0.006, 0.45, rng, gnarl=0.03, bend_fn=bend)
            b = Stem(bp, br, 1, phase=rng.u(), tint=CONIFER_BRANCH_TINT)
            m.stems.append(b)
            # cards along the branch
            step = 0.34 * S
            nc = max(1, int(b.length / step))
            for j in range(nc):
                t = (0.06 + (j + rng.u(0.0, 0.5)) / nc) * 0.94
                pc, dc, _, sc = b.at(t)
                rem = b.length * (1 - t)
                L = min(1.15, rem + 0.35) * S * rng.u(0.9, 1.15)
                Wd = (0.95 + 0.5 * (1 - t)) * S * rng.u(0.9, 1.1)
                roll = math.radians(rng.u(-20, 20))
                nfc = rot_about(UP, dc, roll)
                tile = 'conifer_b' if (ff > 0.72 or v == 2 and rng.chance(0.5)) else 'conifer_a'
                add_card(m, pc, (dc + rng.vec(0.08)).normalized(), nfc, L, Wd, tile, rng, droop=0.18,
                         extra={'phase': b.phase, 'path': sc, 'tint': card_tint(rng, 0.0)})
                if j % 2 == 1:   # a hanging side spray
                    sd = 1 if rng.chance(0.5) else -1
                    nfc2 = rot_about(UP, dc, sd * math.radians(rng.u(45, 65)))
                    add_card(m, pc, (dc - UP * 0.25).normalized(), nfc2, L * 0.85, Wd * 0.85, 'conifer_a', rng, droop=0.3,
                             extra={'phase': b.phase, 'path': sc, 'tint': card_tint(rng, 0.0)})
        z += sp0 * rng.u(0.85, 1.15) * (0.8 + 0.4 * (1 - f))
    # leader: crossed vertical cards at the top
    ptop, dtop, _, _ = trunk.at(1.0 - 1.25 * S / (H + 0.4))     # on the (leaning) trunk, not on the z axis
    for k in range(2):
        nfc = V(math.cos(k * math.pi / 2), math.sin(k * math.pi / 2), 0)
        add_card(m, ptop, dtop, nfc, 1.4 * S, 0.65 * S, 'conifer_b', rng, droop=0.0,
                 extra={'phase': 0.0, 'path': 0.0, 'tint': card_tint(rng, 0.5)})

    def ao_leaf(p, card):
        rc = crown_r(p.z) + 0.3
        d = math.hypot(p.x, p.y) / max(0.3, rc)
        f = (p.z - cb) / max(1e-3, H - cb)
        return (0.25 + 0.75 * smooth(0.15, 1.0, d)) * (0.72 + 0.28 * f)

    def nrm_leaf(p, card, face):
        r = V(p.x, p.y, 0)
        r = r.normalized() if r.length > 1e-3 else V(0, 0, 0)
        return (r * 1.0 + UP * 0.55 + face * 0.25).normalized()

    m.ao_leaf, m.nrm_leaf = ao_leaf, nrm_leaf
    return m


# ------------------------------------------------------------------ palm (coconut)
PALM = [
    dict(H=14.0, lean=1.6, n=20, note='coconut palm, gentle lean'),
    dict(H=8.5, lean=0.6, n=24, note='young palm, heavy crown'),
    dict(H=19.0, lean=3.4, n=18, note='tall leaning palm'),
    dict(H=11.5, lean=2.6, n=20, note='coastal palm, strong curve'),
]


def build_palm(v, seed):
    P = PALM[v]
    rng = Rng(seed)
    H = P['H']
    m = Model('palm', v, H, 4.5)
    m.notes = P['note']
    m.sway_k = 0.016
    m.bend_k = 0.06
    m.sides = {0: (12, 8, 5), 1: (4, 3, 0)}
    az = rng.u(0, 6.28)
    ld = V(math.cos(az), math.sin(az), 0)
    n = 16
    pts, rads = [], []
    for i in range(n + 1):
        f = i / n
        z = -0.3 + (H + 0.3) * f
        off = P['lean'] * f ** 1.8 - 0.12 * P['lean'] * max(0.0, f - 0.8) / 0.2
        wob = 0.08 * math.sin(f * 7.0 + seed)
        p = ld * off + ld.cross(UP) * wob + V(0, 0, z)
        pts.append(p)
        r = 0.15 + 0.03 * (1 - f) + 0.13 * math.exp(-max(0.0, z) / 0.6)
        rads.append(r)
    trunk = Stem(pts, rads, 0)
    m.stems.append(trunk)
    top = pts[-1]
    tdir = (pts[-1] - pts[-2]).normalized()
    # leaf-base bulb (frond sheaths)
    bp, br = grow(top - tdir * 0.6, tdir, 1.0, 0.2, 0.1, 0.25, rng, taper=1.6)
    br = [0.17 + 0.09 * math.sin(math.pi * min(1.0, i / (len(br) - 1) * 1.1)) for i in range(len(br))]
    m.stems.append(Stem(bp, br, 1, tint=(0.82, 0.78, 0.6)))
    crown = top + tdir * 0.15
    m.crown_c = crown.copy()
    m.top_z = crown.z
    nf = P['n']
    Lmax = 4.6 * (H / 14.0) ** 0.2
    m.R = Lmax * 0.85
    m.crown_r = V(m.R, m.R, 2.0)
    for i in range(nf):
        a = i / (nf - 1)
        azf = i * math.radians(137.5) + rng.u(-0.15, 0.15)
        elev = math.radians(68 - 75 * a ** 0.8 + rng.u(-6, 6))
        Lf = Lmax * (0.6 + 0.4 * min(1.0, a * 2.5)) * rng.u(0.9, 1.08)
        droop = math.radians(20 + 85 * a + rng.u(-8, 8))
        m.fronds.append(dict(base=crown + V(0, 0, -0.25 * a), az=azf, elev=elev, L=Lf, droop=droop,
                             fold=math.radians(28 + 14 * a), tile='palm_a' if i % 3 else 'palm_b', age=a,
                             phase=rng.u(), tint=card_tint(rng, 0.6 * a)))
    for i in range(2 if v in (1, 3) else 3):
        azf = rng.u(0, 6.28)
        m.fronds.append(dict(base=crown + V(0, 0, -0.45), az=azf, elev=math.radians(rng.u(-80, -68)), L=Lmax * rng.u(0.7, 0.85),
                             droop=math.radians(10), fold=math.radians(15), tile='palm_dead', age=1.0, phase=rng.u(),
                             tint=(0.95, 0.95, 0.95)))
    for k in range(rng.i(6, 11)):
        a = rng.u(0, 6.28)
        c = crown + V(math.cos(a) * 0.32, math.sin(a) * 0.32, rng.u(-0.6, -0.2))
        m.nuts.append((c, rng.u(0.1, 0.13), (0.62, 0.58, 0.32) if rng.chance(0.6) else (0.55, 0.42, 0.25)))

    def ao_leaf(p, card):
        d = (p - crown).length / Lmax
        age = card.get('age', 0.5)
        return (0.35 + 0.65 * smooth(0.05, 0.6, d)) * (1.0 - 0.3 * age)

    def nrm_leaf(p, card, face):
        r = (p - crown)
        r = r.normalized() if r.length > 1e-3 else UP
        return (r * 0.55 + face * 0.45 + UP * 0.25).normalized()

    m.ao_leaf, m.nrm_leaf = ao_leaf, nrm_leaf
    return m


# ------------------------------------------------------------------ acacia (umbrella thorn)
ACACIA = [
    dict(H=8.0, R=6.0, fork=0.3, limbs=3, stems=1, tiers=1, note='umbrella thorn, wide flat crown'),
    dict(H=11.0, R=6.5, fork=0.36, limbs=3, stems=1, tiers=2, note='tall two-tiered acacia'),
    dict(H=5.5, R=3.5, fork=0.28, limbs=2, stems=1, tiers=1, note='young acacia'),
    dict(H=7.0, R=5.5, fork=0.0, limbs=2, stems=2, tiers=1, note='multi-stemmed, lopsided'),
]


def build_acacia(v, seed):
    P = ACACIA[v]
    rng = Rng(seed)
    H, R = P['H'], P['R']
    m = Model('acacia', v, H, R)
    m.notes = P['note']
    zc = H * 0.86
    rz = H * 0.13
    off = V(rng.u(-0.6, 0.6), rng.u(-0.6, 0.6), 0) * (1.6 if v == 3 else 0.4)
    env = Ellipsoid(V(off.x, off.y, zc), R, R * rng.u(0.8, 0.95), rz, lump=0.12, seed=seed % 89)
    m.crown_c, m.crown_r = env.c, env.r
    m.top_z = H - rz * 0.6
    m.Lref = R
    m.sway_k = 0.008
    m.bend_k = 0.025
    S = 1.0
    limbs = []
    if P['stems'] == 1:
        hf = P['fork'] * H
        pts, rads = grow(V(0, 0, -0.4), UP + rng.vec(0.12), hf + 0.4, 0.06 * H ** 0.8 * 0.5 + 0.05, 0.12, 0.5, rng, gnarl=0.06, taper=0.5)
        trunk = Stem(pts, rads, 0, flare=0.3)
        m.stems.append(trunk)
        bases = [(trunk.pts[-1], trunk.rads[-1])] * P['limbs']
    else:
        bases = []
        for k in range(P['stems']):
            bases.append((V(rng.u(-0.15, 0.15), rng.u(-0.15, 0.15), -0.4), 0.13))
    az0 = rng.u(0, 6.28)
    nl = len(bases)
    for k, (p0, r0) in enumerate(bases):
        az = az0 + k * 2 * math.pi / nl + rng.u(-0.4, 0.4)
        rr = R * rng.u(0.35, 0.55)
        tgt = env.c + V(math.cos(az) * rr, math.sin(az) * rr, -rz * rng.u(0.8, 1.2))
        dv = tgt - p0
        lp, lr = grow(p0, dv.normalized(), dv.length, r0 * (0.8 if P['stems'] == 1 else 1.0), 0.05, 0.5, rng, gnarl=0.14, taper=0.7)
        limbs.append(Stem(lp, lr, 1 if P['stems'] == 1 else 0, phase=rng.u(), flare=0.0 if P['stems'] == 1 else 0.3))
    m.stems += limbs
    for limb in limbs:
        n2 = rng.i(5, 7)
        for k in range(n2):
            t = 0.5 + 0.5 * (k + rng.u(0, 1)) / n2
            p, d, r, s = limb.at(t)
            az = math.atan2(d.y, d.x) + rng.u(-1.9, 1.9)
            tier_low = P['tiers'] == 2 and k % 3 == 0
            rr = R * rng.u(0.55, 0.97) * (0.65 if tier_low else 1.0)
            tgt = env.c + V(math.cos(az) * rr, math.sin(az) * rr, -rz * rng.u(0.1, 0.6) - (H * 0.18 if tier_low else 0.0))
            dv = tgt - p
            if dv.length < 0.6:
                continue
            bp, br = grow(p, dv.normalized(), dv.length, max(0.02, r * 0.6), 0.012, 0.5, rng, gnarl=0.12)
            b2 = Stem(bp, br, 2, phase=limb.phase, path0=s)
            ln = b2.length
            m.stems.append(b2)
            n3 = int(ln * 1.3) + 2
            for j in range(n3):
                t3 = 0.35 + 0.65 * (j + rng.u(0, 1)) / n3
                p3, d3, r3, s3 = b2.at(t3)
                d3n = (d3 * 0.4 + UP * rng.u(0.7, 1.0) + rng.vec(0.3)).normalized()
                ln3 = rng.u(0.4, 0.9)
                tp, trd = grow(p3, d3n, ln3, 0.012, 0.005, 0.6, rng, gnarl=0.2)
                tw = Stem(tp, trd, 3, phase=limb.phase, path0=s + s3)
                m.stems.append(tw)
                cc = tw.pts[-1] + V(0, 0, 0.1)
                ncl = rng.i(6, 9)
                for q in range(ncl):
                    a_ = rng.u(0, 6.28)
                    rq = 0.9 * math.sqrt(rng.u(0, 1))
                    pc = cc + V(math.cos(a_) * rq, math.sin(a_) * rq, rng.u(-0.3, 0.2))
                    radial = V(pc.x - env.c.x, pc.y - env.c.y, 0)
                    radial = radial.normalized() if radial.length > 1e-3 else V(1, 0, 0)
                    dL = (radial * 0.7 + rng.vec(0.5)).normalized()
                    dL.z *= 0.3
                    nfc = (UP + rng.vec(0.28)).normalized()
                    sz = 1.0 * rng.u(0.85, 1.25)
                    add_card(m, pc - dL * sz * 0.4, dL, nfc, sz, sz, 'acacia_a' if rng.chance(0.6) else 'acacia_b', rng, droop=0.05,
                             extra={'phase': limb.phase, 'path': s + s3 + ln3, 'clump': (cc, 1.0), 'tint': card_tint(rng, 0.5)})

    def ao_leaf(p, card):
        cc, cr = card.get('clump', (env.c, 1.0))
        vert = smooth(-0.55, 0.35, p.z - cc.z)
        d = env.depth(p)
        return (0.3 + 0.7 * vert) * (0.75 + 0.25 * smooth(0.3, 1.0, d))

    def nrm_leaf(p, card, face):
        r = V(p.x - env.c.x, p.y - env.c.y, 0) / max(1.0, R)
        cc, _ = card.get('clump', (env.c, 1.0))
        q = p - cc
        cl = q.normalized() if q.length > 1e-3 else UP
        return (UP * 0.85 + r * 0.35 + cl * 0.3 + face * 0.15).normalized()

    m.ao_leaf, m.nrm_leaf = ao_leaf, nrm_leaf
    m.sides = {0: (10, 7, 5), 1: (7, 5, 3), 2: (4, 3, 0)}
    return m


# ------------------------------------------------------------------ shrub
SHRUB = [
    dict(H=1.8, R=1.3, stems=8, note='round bush'),
    dict(H=1.1, R=1.8, stems=10, note='low spreading bush'),
    dict(H=2.8, R=1.2, stems=7, note='tall upright shrub'),
    dict(H=0.9, R=0.8, stems=6, note='small bush'),
]


def build_shrub(v, seed):
    P = SHRUB[v]
    rng = Rng(seed)
    H, R = P['H'], P['R']
    m = Model('shrub', v, H, R)
    m.notes = P['note']
    env = Ellipsoid(V(0, 0, H * 0.5), R, R * rng.u(0.85, 1.0), H * 0.52, lump=0.18, seed=seed % 83)
    m.crown_c, m.crown_r = env.c, env.r
    m.top_z = H * 0.62
    m.Lref = H
    m.sway_k = 0.02
    m.bend_k = 0.04
    m.lod_cards = [(1.0, 1.0), (0.4, 1.5), (0.15, 2.4)]
    m.sides = {1: (5, 3, 3), 2: (3, 0, 0)}
    for k in range(P['stems']):
        az = k * 2 * math.pi / P['stems'] + rng.u(-0.3, 0.3)
        th = math.radians(rng.u(12, 55)) * (1.25 if R > H else 1.0)
        p0 = V(math.cos(az) * 0.08, math.sin(az) * 0.08, -0.1)
        dd = dir_from(th, az)
        ln = env.dist_to_surface(p0 + V(0, 0, 0.12), dd) * rng.u(0.8, 0.98)
        sp, sr = grow(p0, dd, ln, 0.022 * (H / 1.8) ** 0.5, 0.006, 0.3, rng, trop=UP * 0.1, gnarl=0.15)
        st = Stem(sp, sr, 1, phase=rng.u())
        m.stems.append(st)
        n2 = int(ln * 3) + 1
        for j in range(n2):
            t = 0.25 + 0.75 * (j + rng.u(0, 1)) / n2
            p, d, r, s = st.at(t)
            u, w = orth(d)
            a = rng.u(0, 6.28)
            d2 = rot_about(d, u * math.cos(a) + w * math.sin(a), math.radians(rng.u(35, 60)))
            ln2 = min(env.dist_to_surface(p, d2) * rng.u(0.8, 1.0), 0.9)
            if ln2 < 0.15:
                continue
            bp, br = grow(p, d2, ln2, 0.008, 0.004, 0.3, rng, trop=UP * 0.2, gnarl=0.2)
            b2 = Stem(bp, br, 2, phase=st.phase, path0=s)
            m.stems.append(b2)
            nc = 1 + int(ln2 / 0.12)
            for q in range(nc):
                tq = 0.3 + 0.7 * (q + rng.u(0, 1)) / nc
                pc, dc, _, sc = b2.at(tq)
                rad = env.grad(pc)
                dL = (dc * 0.5 + rad * 0.5 + rng.vec(0.25)).normalized()
                nfc = (rad * 0.6 + UP * 0.4 + rng.vec(0.5)).normalized()
                sz = 0.42 * rng.u(0.85, 1.2) * min(1.0, (H / 1.2) ** 0.3)
                add_card(m, pc, dL, nfc, sz, sz, 'shrub_a', rng, droop=0.1,
                         extra={'phase': st.phase, 'path': s + sc, 'tint': card_tint(rng, 0.3)})
        pc, dc, _, _ = st.at(1.0)
        add_card(m, pc, (dc + UP * 0.4).normalized(), (UP + rng.vec(0.3)).normalized(), 0.45, 0.45, 'shrub_a', rng,
                 extra={'phase': st.phase, 'path': ln, 'tint': card_tint(rng, 0.6)})

    def ao_leaf(p, card):
        d = env.depth(p)
        below = smooth(0.0, -1.0, (p.z - env.c.z) / env.r.z)
        return (0.25 + 0.75 * smooth(0.3, 1.0, d)) * (1.0 - 0.35 * below)

    def nrm_leaf(p, card, face):
        return (env.grad(p) * 0.8 + face * 0.2 + UP * 0.15).normalized()

    m.ao_leaf, m.nrm_leaf = ao_leaf, nrm_leaf
    return m


BUILDERS = {'broadleaf': build_broadleaf, 'conifer': build_conifer, 'palm': build_palm, 'acacia': build_acacia, 'shrub': build_shrub}
NUM_VARIANTS = 4


def build(species, variant):
    seed = 1000 + 97 * list(BUILDERS).index(species) + 13 * variant
    return BUILDERS[species](variant, seed)


# ------------------------------------------------------------------ meshing
def _hash01(i, salt):
    h = (i * 2654435761 + salt * 40503) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 2246822519) & 0xFFFFFFFF
    h ^= h >> 13
    return (h & 0xFFFFFF) / float(0x1000000)


def wind_uv(model, p, path, phase):
    z = max(0.0, p.z)
    sway = model.sway_k * model.H * (z / model.H) ** 2
    bend = model.bend_k * model.Lref * min(2.0, (path / max(0.5, model.Lref))) ** 1.5 if path > 0 else 0.0
    return sway, bend, phase


def bark_ao(model, st, pos):
    """Bark AO. Branches (level >= 1) inside the crown also take the crown's occlusion (the leaf AO field, 0.22 deep
    inside -> 1 at the surface). Verification fix: with only the height term, limbs and twigs deep in the crown were
    unoccluded and read as pale/pink sticks and conifer 'fishbones' through the cards at 50-300 m."""
    ao = model.ao_bark(pos)
    # the trunk takes it too once it is inside the crown (above the crown envelope's base), so the conifer stem does
    # not show as a lit red-brown line through LOD2/LOD3 spires; the bole below the crown keeps the height term only
    k = 1.0 if st.level >= 1 else smooth(0.0, 1.5, pos.z - (model.crown_c.z - model.crown_r.z))
    if k > 0.0:
        a_leaf = max(0.0, min(1.0, model.ao_leaf(pos, {})))
        ao *= 1.0 - k + k * (0.3 + 0.7 * a_leaf * a_leaf)
    return ao


def tube(mb, model, st, sides, stride):
    idx = list(range(0, len(st.pts), stride))
    if idx[-1] != len(st.pts) - 1:
        idx.append(len(st.pts) - 1)
    pts = [st.pts[i] for i in idx]
    rads = [st.rads[i] for i in idx]
    cums = [st.cum[i] for i in idx]
    tu, tv = model.bark_tile
    kar = max(1, round(2 * math.pi * rads[0] / tu))
    # parallel-transported frame along averaged tangents
    segd = [(pts[i + 1] - pts[i]).normalized() for i in range(len(pts) - 1)]
    tans = [segd[0]] + [(segd[i - 1] + segd[i]).normalized() for i in range(1, len(segd))] + [segd[-1]]
    u, _ = orth(tans[0])
    rings = []
    for i, p in enumerate(pts):
        if i > 0:
            q = tans[i - 1].rotation_difference(tans[i])
            u = q @ u
            u = (u - tans[i] * u.dot(tans[i])).normalized()
        w = tans[i].cross(u).normalized()
        ring = []
        for j in range(sides + 1):
            a = 2 * math.pi * j / sides
            n = u * math.cos(a) + w * math.sin(a)
            r = rads[i]
            if st.flare > 0:
                hz = max(0.0, p.z + 0.4)
                r *= 1.0 + st.flare * math.exp(-hz / 0.55) * (1.0 + 0.35 * math.cos(5 * a + 1.3))
            pos = p + n * r
            sway, bend, ph = wind_uv(model, pos, st.path0 + cums[i] if st.level > 0 else 0.0, st.phase)
            ao = bark_ao(model, st, pos)
            ring.append(mb.v(pos, n, (j / sides * kar, cums[i] / tv), (st.tint[0], st.tint[1], st.tint[2], ao), (sway, 0.0), (bend, ph)))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for j in range(sides):
            a, b, c, d = rings[i][j], rings[i][j + 1], rings[i + 1][j + 1], rings[i + 1][j]
            mid = (Vector(mb.P[a]) + Vector(mb.P[c])) * 0.5
            out = mid - pts[i]
            mb.quad(SEC_BARK, a, b, c, d, out)
    # close the tip with a small cap point when the stem ends thick enough to see
    if rads[-1] > 0.02:
        tipp = pts[-1] + (pts[-1] - pts[-2]).normalized() * rads[-1] * 0.5
        # same wind rule as the rings: the trunk (level 0) carries no branch bend. (Verification fix: the cap used
        # path0 + length for every level, so trunk caps got up to 1.1 m of bend and pulled a spike out of the crown in wind.)
        sway, bend, ph = wind_uv(model, tipp, st.path0 + cums[-1] if st.level > 0 else 0.0, st.phase)
        t = mb.v(tipp, (pts[-1] - pts[-2]).normalized(), (0.5 * kar, cums[-1] / tv), (st.tint[0], st.tint[1], st.tint[2], bark_ao(model, st, tipp)), (sway, 0), (bend, ph))
        for j in range(sides):
            mb.tri(SEC_BARK, rings[-1][j], rings[-1][j + 1], t, (pts[-1] - pts[-2]))


def emit_card(mb, model, c, scale=1.0, pull=0.0):
    rect = tiles.uv_rect(c['tile'])
    L, W = c['L'] * scale, c['W'] * scale
    base = c['base']
    if scale != 1.0:
        centre = base + c['dL'] * c['L'] * 0.5
        if pull > 0:
            centre = centre.lerp(model.crown_c, pull)
        base = centre - c['dL'] * L * 0.5
    rows = []
    for k, t in enumerate((0.0, 0.5, 1.0)):
        sag = -UP * c['droop'] * L * t * t
        row = []
        for s in (-0.5, 0.5):
            p = base + c['dL'] * (L * t) + c['dW'] * (W * s) + sag
            n = model.nrm_leaf(p, c, c['nf'])
            ao = model.ao_leaf(p, c)
            sway, bend, ph = wind_uv(model, p, c['path'] + L * t * 0.5, c['phase'])
            flutter = 0.03 * L * t
            uv = (rect[0] + (s + 0.5) * (rect[2] - rect[0]), rect[1] + t * (rect[3] - rect[1]))
            col = (c['tint'][0], c['tint'][1], c['tint'][2], max(0.0, min(1.0, ao)))
            row.append((p, n, uv, col, (sway, flutter), (bend, ph)))
        rows.append(row)
    for side in (1, -1):
        ids = [[mb.v(*vv) for vv in row] for row in rows]
        for k in range(2):
            for j in range(1):
                a, b, cc, d = ids[k][j], ids[k][j + 1], ids[k + 1][j + 1], ids[k + 1][j]
                mb.quad(SEC_LEAF, a, b, cc, d, c['nf'] * side)


def emit_frond(mb, model, f, segs):
    """Palm frond: V-folded strip along a drooping rachis, emitted front and back."""
    rect = tiles.uv_rect(f['tile'])
    az, elev, L = f['az'], f['elev'], f['L']
    hd = V(math.cos(az), math.sin(az), 0)
    side0 = UP.cross(hd).normalized()
    s0 = 0.06
    rows = []
    p = f['base'].copy()
    W = 1.25 * L / 5.0
    # rachis polyline
    pts = []
    for k in range(segs + 1):
        s = k / segs
        e = elev - f['droop'] * s ** 1.3
        d = hd * math.cos(e) + UP * math.sin(e)
        pts.append((s, p.copy(), d))
        p = p + d * (L / segs)
    for (s, pr, d) in pts:
        side = side0
        upl = side.cross(d).normalized()
        if upl.z < 0:
            upl = -upl
        w = W * (math.sin(math.pi * min(1.0, max(0.0, (s - s0) / (1 - s0)) * 1.0)) ** 0.55 if s > s0 else 0.0)
        fold = f['fold']
        row = []
        for u in (-1.0, 0.0, 1.0):
            pos = pr + side * (u * w * math.cos(fold)) - upl * (abs(u) * w * math.sin(fold))
            face = upl
            n = model.nrm_leaf(pos, f, face)
            ao = model.ao_leaf(pos, f)
            sway, bend, ph = wind_uv(model, pos, 0.0, f['phase'])
            bend = model.bend_k * L * s ** 1.5
            flutter = 0.06 * s * abs(u)
            uv = (rect[0] + (u * 0.5 + 0.5) * (rect[2] - rect[0]), rect[1] + s * (rect[3] - rect[1]))
            row.append((pos, n, uv, (f['tint'][0], f['tint'][1], f['tint'][2], max(0, min(1, ao))), (sway, flutter), (bend, ph)))
        rows.append((row, upl))
    for sidek in (1, -1):
        ids = [[mb.v(*vv) for vv in row] for row, _ in rows]
        for k in range(segs):
            upl = rows[k][1]
            for j in range(2):
                a, b, c, d = ids[k][j], ids[k][j + 1], ids[k + 1][j + 1], ids[k + 1][j]
                mb.quad(SEC_LEAF, a, b, c, d, upl * sidek)


def emit_nut(mb, model, c, r, tint):
    # octahedron subdivided once, normalised (32 tris)
    verts = [V(1, 0, 0), V(-1, 0, 0), V(0, 1, 0), V(0, -1, 0), V(0, 0, 1), V(0, 0, -1)]
    faces = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
    for (a, b, cc) in faces:
        A, B, C = verts[a], verts[b], verts[cc]
        ab, bc, ca = (A + B).normalized(), (B + C).normalized(), (C + A).normalized()
        for tri in ((A, ab, ca), (ab, B, bc), (ca, bc, C), (ab, bc, ca)):
            ids = []
            for q in tri:
                pos = c + q * r
                sway, bend, ph = wind_uv(model, pos, 0.0, 0.0)
                ids.append(mb.v(pos, q, (0.5, 0.5), (tint[0], tint[1], tint[2], 0.7), (sway, 0), (0, 0)))
            mb.tri(SEC_BARK, ids[0], ids[1], ids[2], (tri[0] + tri[1] + tri[2]))


def build_lod(model, lod):
    mb = MB()
    stride = (1, 2, 3)[lod]
    for st in model.stems:
        sides_t = model.sides.get(st.level)
        if not sides_t:
            continue
        sides = sides_t[lod]
        if sides < 3:
            continue
        if lod >= 1 and st.level >= 2 and max(st.rads) < 0.02:
            continue
        tube(mb, model, st, sides, stride if len(st.pts) > 3 else 1)
    keep, scale = model.lod_cards[lod]
    for c in model.cards:
        if _hash01(c['id'], 7) < keep:
            emit_card(mb, model, c, scale, pull=0.12 if lod == 2 else 0.0)
    segs = (9, 6, 4)[lod]
    for f in model.fronds:
        if lod == 2 and f['tile'] == 'palm_dead' and _hash01(int(f['az'] * 1000), 3) < 0.5:
            continue
        emit_frond(mb, model, f, segs)
    if lod <= 1:
        for (c, r, t) in model.nuts:
            emit_nut(mb, model, c, r, t)
    return mb
