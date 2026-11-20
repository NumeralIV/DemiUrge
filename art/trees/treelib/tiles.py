# DEMIURGE tree families: the leaf-card atlas. Every tile is a small procedural 3D model (twig, needle spray,
# compound leaf, palm frond) built here and rendered top-down, unlit, by EEVEE; then downsampled with
# premultiplied alpha, push-pull dilated and packed into T_TreeLeafAtlas (2048 x 2048, sRGB + alpha mask).
#
# Tile frame: x across in [-W/2, W/2], y along the twig in [0, H] metres; the twig base sits at the bottom
# centre of the tile (UV v = 0 side). A card in the tree maps its base to the tile bottom and its tip to the top.
import math
import os
import numpy as np
from . import imaging as im
from . import render as rd

ATLAS = 2048
CELL = 512

# name -> (col, row from TOP, cols, rows, world W, world H (metres the tile represents))
LAYOUT = {
    'broadleaf_a': (0, 0, 1, 1, 1.20, 1.20),
    'broadleaf_b': (1, 0, 1, 1, 1.20, 1.20),
    'conifer_a':   (2, 0, 1, 1, 0.80, 0.80),
    'conifer_b':   (3, 0, 1, 1, 0.80, 0.80),
    'acacia_a':    (0, 1, 1, 1, 0.80, 0.80),
    'shrub_a':     (1, 1, 1, 1, 0.45, 0.45),
    'acacia_b':    (2, 1, 1, 1, 0.80, 0.80),
    'broadleaf_c': (3, 1, 1, 1, 1.20, 1.20),
    'palm_a':      (0, 2, 1, 2, 2.50, 5.00),
    'palm_b':      (1, 2, 1, 2, 2.50, 5.00),
    'palm_dead':   (2, 2, 1, 2, 2.50, 5.00),
    # (3, 2)-(3, 3) spare
}


def uv_rect(name):
    """Blender-convention UV rectangle (u0, v0, u1, v1) of a tile, inset by half a texel of the smallest mip we care
    about (8 texels) to avoid bleeding between tiles."""
    c, r, nc, nr = LAYOUT[name][:4]
    g = ATLAS // CELL
    u0 = c / g
    u1 = (c + nc) / g
    v1 = 1.0 - r / g
    v0 = v1 - nr / g
    e = 6.0 / ATLAS
    return (u0 + e, v0 + e, u1 - e, v1 - e)


def tile_size(name):
    return LAYOUT[name][4], LAYOUT[name][5]


# ------------------------------------------------------------------ geometry accumulator
class Acc:
    def __init__(self):
        self.P = []
        self.C = []
        self.T = []

    def vert(self, x, y, z, col_lin):
        self.P.append((x, y, z))
        self.C.append(col_lin)
        return len(self.P) - 1

    def tri(self, a, b, c):
        self.T.append((a, b, c))

    def fit(self, W, H, margin=0.02):
        """Uniformly shrinks the model (about the twig base) so it fits inside the tile frame."""
        P = np.array(self.P, np.float32)
        if not len(P):
            return
        sx = (W * 0.5 - margin * W) / max(1e-6, np.abs(P[:, 0]).max())
        sy = (H - margin * H) / max(1e-6, P[:, 1].max())
        s = min(1.0, sx, sy)
        if s < 1.0:
            P[:, :2] *= s
            self.P = [tuple(p) for p in P]

    def to_object(self, name):
        import bpy
        P = np.array(self.P, np.float32)
        T = np.array(self.T, np.int32)
        me = bpy.data.meshes.new(name)
        me.vertices.add(len(P))
        me.vertices.foreach_set('co', P.ravel())
        me.loops.add(len(T) * 3)
        me.loops.foreach_set('vertex_index', T.ravel())
        me.polygons.add(len(T))
        me.polygons.foreach_set('loop_start', np.arange(0, len(T) * 3, 3, dtype=np.int32))
        me.update(calc_edges=True)
        at = me.color_attributes.new('Col', 'FLOAT_COLOR', 'POINT')
        C = np.array(self.C, np.float32)
        C4 = np.concatenate([C, np.ones((len(C), 1), np.float32)], axis=1)
        at.data.foreach_set('color', C4.ravel())
        ob = bpy.data.objects.new(name, me)
        return ob


def lin(c255, k=1.0):
    return tuple(float(v) for v in im.srgb_to_lin(np.clip(np.array(c255, np.float32) / 255.0 * k, 0, 1)))


def jitter_col(rng, c255, val=0.12, hue=0.08):
    c = np.array(c255, np.float32)
    v = 1.0 + rng.uniform(-val, val)
    # hue-ish: trade red against blue a little, keeps green dominant
    h = rng.uniform(-hue, hue)
    c = c * v
    c[0] *= 1.0 + h
    c[2] *= 1.0 - h
    return np.clip(c, 0, 255)


# ------------------------------------------------------------------ primitives
def stem(acc, pts, w0, w1, col, z):
    """Flat tapered strip along a 2D polyline."""
    n = len(pts)
    left, right = [], []
    for i, (x, y) in enumerate(pts):
        if i < n - 1:
            dx, dy = pts[i + 1][0] - x, pts[i + 1][1] - y
        else:
            dx, dy = x - pts[i - 1][0], y - pts[i - 1][1]
        ln = math.hypot(dx, dy) or 1.0
        px, py = -dy / ln, dx / ln
        w = (w0 + (w1 - w0) * i / max(1, n - 1)) * 0.5
        k = 0.85 + 0.3 * (i / max(1, n - 1))
        c = lin(col, k)
        left.append(acc.vert(x + px * w, y + py * w, z, c))
        right.append(acc.vert(x - px * w, y - py * w, z, c))
    for i in range(n - 1):
        acc.tri(left[i], right[i], right[i + 1])
        acc.tri(left[i], right[i + 1], left[i + 1])


def polyline(x0, y0, ang, length, bend, n=6):
    """2D curve from (x0, y0) heading `ang` (radians, 0 = +y) with total turn `bend` (radians)."""
    pts = [(x0, y0)]
    x, y = x0, y0
    for i in range(n):
        a = ang + bend * (i + 0.5) / n
        x += math.sin(a) * length / n
        y += math.cos(a) * length / n
        pts.append((x, y))
    return pts


def leaf_outline(shape, s):
    if shape == 'ovate':        # beech / generic
        w = (s ** 0.55) * ((1 - s) ** 0.85)
        return w / 0.403
    if shape == 'lobed':        # oak
        w = (s ** 0.6) * ((1 - s) ** 0.7) / 0.43
        return w * (0.62 + 0.38 * abs(math.sin(math.pi * (s * 4.5 + 0.15))))
    if shape == 'elliptic':     # shrub (privet/box-like)
        return math.sin(math.pi * s) ** 0.75
    if shape == 'lance':        # palm leaflet / willow
        return (s ** 0.35) * ((1 - s) ** 0.9) / 0.53
    return math.sin(math.pi * s)


def leaf(acc, x, y, ang, L, W, shape, col255, z, rng, ns=10, midrib=True, shade=1.0):
    """One leaf blade lying in the tile plane, base at (x, y), pointing at `ang`."""
    us = (-1.0, -0.55, -0.12, 0.0, 0.12, 0.55, 1.0) if midrib else (-1.0, 0.0, 1.0)
    dx, dy = math.sin(ang), math.cos(ang)
    px, py = dy, -dx
    base = jitter_col(rng, col255)
    bend = rng.uniform(-0.25, 0.25)
    rows = []
    for i in range(ns + 1):
        s = i / ns
        w = W * 0.5 * leaf_outline(shape, s)
        a2 = bend * s * s
        cx = x + (dx * math.cos(a2) - px * math.sin(a2)) * s * L
        cy = y + (dy * math.cos(a2) - py * math.sin(a2)) * s * L
        row = []
        for u in us:
            k = shade * (0.86 + 0.14 * s)                      # darker towards the petiole
            k *= 1.0 - 0.08 * abs(u)                           # margin a touch darker
            c = base * k
            if midrib and abs(u) < 0.05:
                c = c * 1.18 + np.array([10.0, 8.0, 0.0])      # pale midrib
            if u < 0:
                c = c * 0.96                                   # two-tone halves
            row.append(acc.vert(cx + px * u * w, cy + py * u * w, z + 0.002 * abs(u), lin(c)))
        rows.append(row)
    for i in range(ns):
        for j in range(len(us) - 1):
            a, b, c, d = rows[i][j], rows[i][j + 1], rows[i + 1][j + 1], rows[i + 1][j]
            acc.tri(a, b, c)
            acc.tri(a, c, d)


def needle(acc, x, y, ang, L, W, col_lin, z):
    dx, dy = math.sin(ang), math.cos(ang)
    px, py = dy * W * 0.5, -dx * W * 0.5
    a = acc.vert(x + px, y + py, z, col_lin)
    b = acc.vert(x - px, y - py, z, col_lin)
    c = acc.vert(x + dx * L, y + dy * L, z, col_lin)
    acc.tri(a, b, c)


# ------------------------------------------------------------------ tile models
def twig_broadleaf(variant, rng):
    W, H = 0.70, 0.70
    acc = Acc()
    shape = {'a': 'ovate', 'b': 'lobed', 'c': 'ovate'}[variant]
    col = {'a': (62, 88, 38), 'b': (54, 76, 34), 'c': (84, 106, 44)}[variant]
    stem_col = (78, 62, 46)
    main = polyline(0.0, 0.0, rng.uniform(-0.12, 0.12), H * 0.80, rng.uniform(-0.3, 0.3), 8)
    stem(acc, main, 0.010, 0.004, stem_col, 0.0)
    nshoot = 9 if variant != 'b' else 8
    side = 1 if rng.random() < 0.5 else -1
    for k in range(nshoot):
        t = 0.12 + 0.78 * k / (nshoot - 1)
        i = min(len(main) - 2, int(t * (len(main) - 1)))
        f = t * (len(main) - 1) - i
        x0 = main[i][0] + (main[i + 1][0] - main[i][0]) * f
        y0 = main[i][1] + (main[i + 1][1] - main[i][1]) * f
        side = -side
        ang = side * math.radians(rng.uniform(38, 60))
        ln = H * (0.36 - 0.18 * t) * rng.uniform(0.85, 1.15)
        sh = polyline(x0, y0, ang, ln, -side * rng.uniform(0.1, 0.4), 5)
        stem(acc, sh, 0.005, 0.002, stem_col, 0.001)
        # leaves along the shoot (clustered at the tip for oak)
        nl = rng.integers(5, 8)
        for j in range(nl):
            s = (0.35 + 0.65 * (j + 1) / nl) if variant != 'b' else (0.75 + 0.25 * (j + 1) / nl)
            p = int(s * (len(sh) - 1))
            p = min(p, len(sh) - 2)
            lx, ly = sh[p]
            sd = 1 if j % 2 == 0 else -1
            la = ang + sd * math.radians(rng.uniform(35, 70))
            L = H * rng.uniform(0.17, 0.22) * (1.12 if variant == 'b' else 1.0)
            Wd = L * (0.55 if variant != 'b' else 0.58)
            z = rng.uniform(0.01, 0.05)
            leaf(acc, lx, ly, la, L, Wd, shape, col, z, rng, shade=0.72 + 0.28 * (z - 0.01) / 0.04)
        # terminal leaf
        lx, ly = sh[-1]
        L = H * rng.uniform(0.16, 0.21)
        z = rng.uniform(0.03, 0.06)
        leaf(acc, lx, ly, ang + rng.uniform(-0.2, 0.2), L, L * 0.55, shape, col, z, rng, shade=0.8 + 0.2 * (z - 0.03) / 0.03)
    # leaves on the main axis and its tip
    for j in range(4):
        t = 0.3 + 0.2 * j
        p = min(len(main) - 2, int(t * (len(main) - 1)))
        lx, ly = main[p]
        sd = 1 if j % 2 else -1
        L = H * rng.uniform(0.15, 0.19)
        z = rng.uniform(0.0, 0.04)
        leaf(acc, lx, ly, sd * math.radians(rng.uniform(25, 50)), L, L * 0.55, shape, col, z, rng, shade=0.7 + 0.3 * z / 0.04)
    lx, ly = main[-1]
    L = H * 0.2
    leaf(acc, lx, ly, rng.uniform(-0.2, 0.2), L, L * 0.55, shape, col, 0.06, rng)
    acc.fit(W, H)
    return acc


def model_broadleaf(variant, rng):
    """A 1.2 m spray: a branchlet carrying five 0.7 m leafy twigs in a fan (true leaf size on 1.2 m cards)."""
    W, H = tile_size('broadleaf_' + variant)
    acc = Acc()
    stem(acc, polyline(0.0, 0.0, rng.uniform(-0.05, 0.05), H * 0.45, rng.uniform(-0.2, 0.2), 5), 0.016, 0.008, (78, 62, 46), 0.0)
    fan = [(-0.95, 0.10, 0.62), (0.9, 0.18, 0.62), (-0.35, 0.30, 0.72), (0.45, 0.36, 0.70), (0.0, 0.44, 0.78)]
    for k, (ang, y0, sc) in enumerate(fan):
        t = twig_broadleaf(variant, rng)
        a = ang + rng.uniform(-0.12, 0.12)
        ca, sa = math.cos(a), math.sin(a)
        base = len(acc.P)
        zoff = 0.08 * k
        for (x, y, z), c in zip(t.P, t.C):
            x *= sc
            y *= sc
            acc.P.append((x * ca + y * sa, y0 * H - x * sa + y * ca, z + zoff))
            acc.C.append(c)
        for (i, j, l) in t.T:
            acc.T.append((i + base, j + base, l + base))
    acc.fit(W, H)
    return acc


def model_shrub(rng):
    W, H = tile_size('shrub_a')
    acc = Acc()
    col = (58, 84, 38)
    stem_col = (84, 70, 54)
    main = polyline(0.0, 0.0, rng.uniform(-0.1, 0.1), H * 0.85, rng.uniform(-0.3, 0.3), 8)
    stem(acc, main, 0.007, 0.003, stem_col, 0.0)
    for k in range(12):
        t = 0.06 + 0.88 * k / 11
        p = min(len(main) - 2, int(t * (len(main) - 1)))
        x0, y0 = main[p]
        for sd in (-1, 1):
            ang = sd * math.radians(rng.uniform(40, 65))
            ln = H * (0.30 - 0.15 * t) * rng.uniform(0.8, 1.2)
            sh = polyline(x0, y0, ang, ln, -sd * rng.uniform(0.0, 0.5), 4)
            stem(acc, sh, 0.003, 0.0015, stem_col, 0.001)
            for j in range(6):      # opposite pairs
                s = 0.2 + 0.8 * (j + 1) / 6
                q = min(len(sh) - 2, int(s * (len(sh) - 1)))
                lx, ly = sh[q]
                for sd2 in (-1, 1):
                    L = H * rng.uniform(0.10, 0.14)
                    z = rng.uniform(0.0, 0.05)
                    leaf(acc, lx, ly, ang + sd2 * math.radians(rng.uniform(40, 70)), L, L * 0.5, 'elliptic', col, z, rng,
                         ns=6, shade=0.7 + 0.3 * z / 0.05)
    acc.fit(W, H)
    return acc


def model_conifer(variant, rng):
    W, H = tile_size('conifer_' + variant)
    acc = Acc()
    old = (34, 52, 32) if variant == 'a' else (32, 50, 38)
    mid = (46, 68, 40) if variant == 'a' else (44, 66, 46)
    new = (78, 102, 50) if variant == 'a' else (70, 98, 62)
    twig_col = (92, 70, 50)
    dense = 1.0 if variant == 'a' else 1.35

    def needles_along(pts, spacing, Lr, z0, tip_frac=0.14):
        # cumulative length
        segs = []
        tot = 0.0
        for i in range(len(pts) - 1):
            l = math.dist(pts[i], pts[i + 1])
            segs.append((tot, l))
            tot += l
        s = 0.0
        side = 1
        while s < tot:
            # locate
            for i, (s0, l) in enumerate(segs):
                if s <= s0 + l:
                    break
            f = (s - s0) / max(l, 1e-6)
            x = pts[i][0] + (pts[i + 1][0] - pts[i][0]) * f
            y = pts[i][1] + (pts[i + 1][1] - pts[i][1]) * f
            ax = math.atan2(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
            u = s / tot
            tip = u > 1.0 - tip_frac
            base = new if tip else (mid if u > 0.35 else old)
            c = jitter_col(rng, base, 0.15, 0.05)
            z = z0 + rng.uniform(0, 0.01)
            k = 0.75 + 0.25 * (z - z0) / 0.01
            for sd in (side, -side) if rng.random() < 0.8 else (side,):
                ang = ax + sd * math.radians(rng.uniform(48, 78))
                L = Lr * rng.uniform(0.8, 1.15) * (0.85 if tip else 1.0)
                needle(acc, x, y, ang, L, 0.0028, lin(c, k), z)
            # a few needles pointing forward on top of the axis (seen end-on: short)
            if rng.random() < 0.35:
                needle(acc, x, y, ax + rng.uniform(-0.5, 0.5), Lr * 0.5, 0.0028, lin(jitter_col(rng, base), 1.0), z0 + 0.012)
            side = -side
            s += spacing / dense
        return tot

    main = polyline(0.0, 0.0, rng.uniform(-0.05, 0.05), H * 0.93, rng.uniform(-0.15, 0.15), 10)
    stem(acc, main, 0.008, 0.002, twig_col, 0.0)
    needles_along(main, 0.0035, 0.026, 0.004)
    n = 17 if variant == 'a' else 20
    side = 1
    for k in range(n):
        t = 0.03 + 0.92 * k / (n - 1)
        p = min(len(main) - 2, int(t * (len(main) - 1)))
        x0, y0 = main[p]
        side = -side
        ln = (W * 0.46) * ((1.0 - t) ** (0.8 if variant == 'a' else 0.6)) * rng.uniform(0.85, 1.1) + 0.03
        ang = side * math.radians(rng.uniform(52, 68) if variant == 'a' else rng.uniform(40, 55))
        br = polyline(x0, y0, ang, ln, -side * rng.uniform(0.1, 0.35), 6)
        stem(acc, br, 0.004, 0.0015, twig_col, 0.001)
        needles_along(br, 0.0034, 0.024, 0.003)
        # tertiary twiglets
        m = int(ln / 0.05)
        for j in range(m):
            s = 0.15 + 0.75 * j / max(1, m)
            q = min(len(br) - 2, int(s * (len(br) - 1)))
            sd = 1 if j % 2 else -1
            a2 = ang + sd * math.radians(rng.uniform(40, 60))
            tw = polyline(br[q][0], br[q][1], a2, ln * (0.42 - 0.3 * s) * rng.uniform(0.8, 1.2), 0.0, 3)
            needles_along(tw, 0.0036, 0.021, 0.002)
    acc.fit(W, H)
    return acc


def model_acacia(variant, rng):
    W, H = tile_size('acacia_' + variant)
    acc = Acc()
    col = (90, 102, 52) if variant == 'a' else (80, 96, 48)
    twig = (70, 60, 52)
    thorn = (214, 206, 186)
    dens = 1.0 if variant == 'a' else 1.5

    def biparted_leaf(x, y, ang, L):
        rach = polyline(x, y, ang, L, rng.uniform(-0.2, 0.2), 3)
        stem(acc, rach, 0.0012, 0.0008, (96, 104, 60), 0.02)
        npin = rng.integers(3, 6)
        for k in range(npin):
            s = 0.25 + 0.75 * (k + 1) / npin
            q = min(len(rach) - 2, int(s * (len(rach) - 1)))
            px, py = rach[q]
            for sd in (-1, 1):
                pa = ang + sd * math.radians(rng.uniform(55, 75))
                pl = L * rng.uniform(0.32, 0.45)
                pin = polyline(px, py, pa, pl, 0.0, 2)
                nlf = rng.integers(8, 12)
                for j in range(nlf):
                    s2 = (j + 0.5) / nlf
                    lx = px + math.sin(pa) * pl * s2
                    ly = py + math.cos(pa) * pl * s2
                    for sd2 in (-1, 1):
                        z = 0.02 + rng.uniform(0, 0.03)
                        leaf(acc, lx, ly, pa + sd2 * math.radians(70), pl * 0.24, pl * 0.11, 'elliptic', col, z, rng, ns=2,
                             midrib=False, shade=0.72 + 0.28 * (z - 0.02) / 0.03)

    main = polyline(0.0, 0.0, 0.0, H * 0.9, 0.0, 1)
    # zigzag main twig
    pts = [(0.0, 0.0)]
    nn = 7
    for k in range(1, nn + 1):
        pts.append(((0.025 if k % 2 else -0.025) + rng.uniform(-0.01, 0.01), H * 0.9 * k / nn))
    stem(acc, pts, 0.008, 0.003, twig, 0.0)
    for k in range(1, nn + 1):
        x0, y0 = pts[k]
        for sd in (-1, 1):   # paired thorns
            tp = polyline(x0, y0, sd * math.radians(rng.uniform(60, 80)), rng.uniform(0.02, 0.035), 0.0, 1)
            stem(acc, tp, 0.0018, 0.0002, thorn, 0.01)
        # fascicle of compound leaves
        nf = int(rng.integers(4, 7) * dens)
        for j in range(nf):
            a = rng.uniform(-1.3, 1.3)
            biparted_leaf(x0, y0, a, rng.uniform(0.07, 0.12) * (W / 0.8) * 1.6)
        # short side shoot with a second cluster
        if rng.random() < 0.6 * dens:
            sd = 1 if rng.random() < 0.5 else -1
            sh = polyline(x0, y0, sd * rng.uniform(0.6, 1.1), rng.uniform(0.08, 0.16), 0.0, 3)
            stem(acc, sh, 0.003, 0.0015, twig, 0.001)
            for j in range(int(3 * dens)):
                biparted_leaf(sh[-1][0], sh[-1][1], rng.uniform(-1.5, 1.5), rng.uniform(0.1, 0.16))
    acc.fit(W, H)
    return acc


def model_palm(variant, rng):
    W, H = tile_size('palm_' + variant)
    acc = Acc()
    dead = variant == 'dead'
    if dead:
        col, tipc, rach = (112, 90, 58), (92, 72, 48), (128, 108, 74)
    elif variant == 'a':
        col, tipc, rach = (92, 110, 46), (138, 132, 62), (156, 148, 86)
    else:
        col, tipc, rach = (82, 108, 44), (118, 124, 56), (146, 150, 84)
    rpts = polyline(0.0, 0.0, 0.0, H * 0.985, 0.0, 12)
    stem(acc, rpts, 0.07, 0.014, rach, 0.03)
    n = 92 if not dead else 70
    for side in (-1, 1):
        for i in range(n):
            s = 0.04 + 0.95 * (i + rng.uniform(-0.3, 0.3)) / n
            if rng.random() < (0.04 if not dead else 0.3):
                continue                           # gap / torn out
            y0 = s * H * 0.985
            Lmax = (W * 0.5 - 0.03)
            L = Lmax * (math.sin(math.pi * min(1.0, s ** 0.85)) ** 0.7) * (0.55 + 0.45 * min(1.0, s / 0.15))
            if rng.random() < (0.07 if not dead else 0.25):
                L *= rng.uniform(0.35, 0.7)        # broken leaflet
            if dead:
                ang = side * math.radians(rng.uniform(18, 34))
            else:
                ang = side * math.radians(64 - 26 * s + rng.uniform(-5, 5))
            Wd = rng.uniform(0.040, 0.055) * (0.8 if dead else 1.0)
            z = 0.0 + rng.uniform(0, 0.02)
            # leaflet as a lance blade curving towards the tip of the frond
            c = jitter_col(rng, col, 0.1, 0.04)
            ns = 8
            pts = polyline(side * 0.02, y0, ang, L, -side * rng.uniform(0.05, 0.25), ns)
            left, right = [], []
            for k, (x, y) in enumerate(pts):
                u = k / ns
                w = Wd * 0.5 * leaf_outline('lance', min(0.999, max(0.001, u)))
                if k < ns:
                    dx, dy = pts[k + 1][0] - x, pts[k + 1][1] - y
                else:
                    dx, dy = x - pts[k - 1][0], y - pts[k - 1][1]
                ln = math.hypot(dx, dy) or 1
                px, py = -dy / ln, dx / ln
                cc = c * (1 - u) + np.array(tipc, np.float32) * u if u > 0.82 else c
                kk = (0.78 + 0.22 * z / 0.02) * (0.9 + 0.1 * u)
                left.append(acc.vert(x + px * w, y + py * w, z, lin(cc, kk)))
                right.append(acc.vert(x - px * w, y - py * w, z, lin(cc * 0.93, kk)))
            for k in range(ns):
                acc.tri(left[k], right[k], right[k + 1])
                acc.tri(left[k], right[k + 1], left[k + 1])
    acc.fit(W, H, margin=0.005)
    return acc


# ------------------------------------------------------------------ bake + pack
def bake(out_dir, log=print, seed=11):
    import bpy
    os.makedirs(out_dir, exist_ok=True)
    tmp = os.path.join(os.path.dirname(out_dir), 'build', 'tiles')
    os.makedirs(tmp, exist_ok=True)
    mat = rd.mat_vertex_emission()
    atlas = np.zeros((ATLAS, ATLAS, 4), np.float32)
    builders = {
        'broadleaf_a': lambda r: model_broadleaf('a', r),
        'broadleaf_b': lambda r: model_broadleaf('b', r),
        'broadleaf_c': lambda r: model_broadleaf('c', r),
        'conifer_a': lambda r: model_conifer('a', r),
        'conifer_b': lambda r: model_conifer('b', r),
        'acacia_a': lambda r: model_acacia('a', r),
        'acacia_b': lambda r: model_acacia('b', r),
        'shrub_a': lambda r: model_shrub(r),
        'palm_a': lambda r: model_palm('a', r),
        'palm_b': lambda r: model_palm('b', r),
        'palm_dead': lambda r: model_palm('dead', r),
    }
    cover = {}
    for k, (name, fn) in enumerate(builders.items()):
        rd.clear_scene()
        rng = np.random.default_rng(seed + 31 * k)
        acc = fn(rng)
        ob = acc.to_object('tile_' + name)
        ob.data.materials.append(mat)
        bpy.context.scene.collection.objects.link(ob)
        c, r, nc, nr, W, H = LAYOUT[name]
        rx, ry = CELL * nc * 2, CELL * nr * 2
        rd.setup(rx, ry, samples=32)
        rd.ortho_camera((0.0, H * 0.5, 5.0), (0.0, 0.0, 0.0), max(W, H))
        img = rd.render(os.path.join(tmp, name + '.png'))
        img = im.downsample2(img)
        y1 = ATLAS - r * CELL
        y0 = y1 - nr * CELL
        x0 = c * CELL
        atlas[y0:y1, x0:x0 + nc * CELL] = img
        cover[name] = float((img[..., 3] > 0.4).mean())
        log('tile %-12s tris %6d coverage %.2f' % (name, len(acc.T), cover[name]))
    atlas = im.dilate(atlas)
    prev = atlas.copy()
    clip = (prev[..., 3:4] > 0.4).astype(np.float32)
    prev[..., :3] = prev[..., :3] * clip + 0.82 * (1 - clip)
    prev[..., 3] = 1.0
    im.save_png(os.path.join(tmp, 'preview_atlas_clip.png'), prev)
    path = os.path.join(out_dir, 'T_TreeLeafAtlas.png')
    im.save_png(path, atlas)
    return path, cover
