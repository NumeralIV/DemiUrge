# DEMIURGE art - wonders-a: wonder 3 (Sail age), the Sea Gate.
# Two tapering pale-limestone towers (70 m, 14 m square at the base) on rubble islets either side of a harbour
# mouth, joined by one high segmental arch spanning 160 m (soffit 45 m above the water at the crown, springing
# at 18 m), with a bronze-domed lantern pavilion (12 m across) at the crown. Z = 0 is mean water level.
import math
import random

from mathutils import Vector, Matrix

from wa_lib import (MB, STRUCT, ROOF, DETAIL, TAU, cell_uv, lathe, cyl_between, rock, block, flame, disc,
                    mb_cut_planes, box_face, uv_box, face_normal, ico, cell_face)

ASSET = dict(
    key='gate', name='SeaGate', concept='wonder-3-sail', age=3, title='The Sea Gate',
    mats=[('GateLimestone', 'Structure'), ('GateBronze', 'Roof'), ('DetailA', 'Detail')],
    emissive_tint='#ffb050', light_kind='lantern (lighthouse) and tower windows',
    states=['Intact', 'Construction', 'Damaged', 'Ruin'],
    day_emit=2.0,
)
WATER = True

SPAN_HALF = 80.0
SPRING_Z = 18.0
SOFFIT_Z = 45.0
R_I = (SPAN_HALF ** 2 + (SOFFIT_Z - SPRING_Z) ** 2) / (2 * (SOFFIT_Z - SPRING_Z))
CZ = SOFFIT_Z - R_I               # circle centre height
RING = 7.0
ARCH_W = 10.0
DECK_C, DECK_E = 57.0, 42.0       # deck (extrados) height at the crown and at the towers
R_E = (80.0 ** 2 + (DECK_C - DECK_E) ** 2) / (2 * (DECK_C - DECK_E))
CZ_E = DECK_C - R_E
TW_HALF0, TW_HALF1 = 7.0, 5.25    # tower half width at z = 0 and at the cornice
TW_X = SPAN_HALF + TW_HALF0       # tower centre x
SHAFT_TOP = 63.5
TILE = 8.0


def intrados(x):
    return CZ + math.sqrt(max(0.0, R_I ** 2 - x * x))


def deck(x):
    return CZ_E + math.sqrt(max(0.0, R_E ** 2 - x * x))


def hw(z):
    return TW_HALF0 + (TW_HALF1 - TW_HALF0) * max(0.0, min(1.0, z / SHAFT_TOP))


def oriented(mb, idx, mat, uvs, want, smooth=False):
    pts = [mb.v[i] for i in idx]
    if face_normal(pts).dot(Vector(want)) < 0:
        idx = list(reversed(idx))
        uvs = list(reversed(uvs))
    mb.face(idx, mat, uvs, smooth)


# --------------------------------------------------------------------------- tower

def tower(mb, cx, q, z_top=None, lights=None, lit_fn=None, side=1):
    """Chamfered square tower centred at (cx, 0). side = +1 for the east tower (arch on its -x face)."""
    ch = 0.6
    bands = [(18.0, 0.35), (36.0, 0.3), (52.0, 0.3)]
    zs = [-3.0, 0.0]
    for (zb, out) in bands:
        zs += [zb - 0.4, zb - 0.4, zb + 0.4, zb + 0.4]
    zs += [SHAFT_TOP, SHAFT_TOP, SHAFT_TOP + 1.2, SHAFT_TOP + 1.2]
    offs = [0.0, 0.0]
    for (zb, out) in bands:
        offs += [0.0, out, out, 0.0]
    offs += [0.0, 0.75, 0.75, 0.0]
    if q >= 2:
        zs = [-3.0, 0.0, SHAFT_TOP, SHAFT_TOP, SHAFT_TOP + 1.2, SHAFT_TOP + 1.2]
        offs = [0, 0, 0, 0.75, 0.75, 0]
    zt = z_top if z_top is not None else 999.0
    rows = []
    zlist = []
    for z, o in zip(zs, offs):
        if z > zt:
            break
        rows.append(z)
        zlist.append(o)
    if z_top is not None and rows[-1] < zt:
        rows.append(zt)
        zlist.append(0.0)

    def ring(z, o):
        h = (hw(min(z, SHAFT_TOP)) if z <= SHAFT_TOP else hw(SHAFT_TOP)) + o
        pts = []
        for (sx, sy) in ((1, -1), (1, 1), (-1, 1), (-1, -1)):
            # chamfered corner: two points per corner, CCW
            if sx * sy < 0:
                pts += [(cx + sx * (h - ch), sy * h), (cx + sx * h, sy * (h - ch))]
            else:
                pts += [(cx + sx * h, sy * (h - ch)), (cx + sx * (h - ch), sy * h)]
        return pts
    grid = []
    for z, o in zip(rows, zlist):
        grid.append([mb.vert((x, y, z)) for (x, y) in ring(z, o)])
    n = len(grid[0])
    for j in range(len(grid) - 1):
        for i in range(n):
            i2 = (i + 1) % n
            idx = [grid[j][i], grid[j][i2], grid[j + 1][i2], grid[j + 1][i]]
            if len(set(mb.v[k] for k in idx)) < 3:
                continue
            pts = [mb.v[k] for k in idx]
            nn = face_normal(pts)
            mb.face(idx, STRUCT, [uv_box(p, nn, TILE) for p in pts], False)
    top = grid[-1]
    mb.face(list(top), STRUCT, [uv_box(mb.v[k], (0, 0, 1), TILE) for k in top], False)
    bot = list(reversed(grid[0]))
    mb.face(bot, STRUCT, [uv_box(mb.v[k], (0, 0, -1), TILE) for k in bot], False)
    full = z_top is None
    if full:
        # parapet ring and merlons on the cornice
        h = hw(SHAFT_TOP) + 0.3
        zb = SHAFT_TOP + 1.2
        t = 0.7
        sides = [((cx - h, -h), (cx + h, -h)), ((cx + h, -h), (cx + h, h)), ((cx + h, h), (cx - h, h)), ((cx - h, h), (cx - h, -h))]
        for (p0, p1) in sides:
            L = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
            ux, uy = (p1[0] - p0[0]) / L, (p1[1] - p0[1]) / L
            nx, ny = uy, -ux       # outward normal
            mid = ((p0[0] + p1[0]) / 2 - nx * t / 2, (p0[1] + p1[1]) / 2 - ny * t / 2)
            block(mb, (mid[0], mid[1], zb + 1.4), (abs(ux) * (L / 2) + abs(nx) * t / 2, abs(uy) * (L / 2) + abs(ny) * t / 2, 1.4), 0.0, STRUCT, tile=TILE)
            if q < 2:
                k = 5
                for m in range(k):
                    s = (m + 0.5) / k
                    px, py = p0[0] + (p1[0] - p0[0]) * s - nx * t / 2, p0[1] + (p1[1] - p0[1]) * s - ny * t / 2
                    block(mb, (px, py, zb + 2.8 + 0.85), (abs(ux) * 0.75 + abs(nx) * t / 2, abs(uy) * 0.75 + abs(ny) * t / 2, 0.85), 0.0, STRUCT, tile=TILE)
    # windows (arched slits) on the faces
    if q < 3:
        wl = [8.0, 26.0, 42.0, 56.5]
        n_w = 0
        for face in ('-y', '+y', 'out', 'in'):
            for z in wl:
                if z_top is not None and z + 3 > z_top:
                    continue
                if face == 'in' and z < DECK_E + 1:
                    continue
                n_w += 1
                lit = lit_fn(n_w) if lit_fn else (n_w % 3 != 1)
                window(mb, cx, z, face, side, lit, q, lights)
        if full or (z_top and z_top > DECK_E + 4):
            # door onto the deck
            door_z = DECK_E + 0.05
            window(mb, cx, door_z, 'in', side, False, q, None, w=2.2, h=3.4, cell='recess')


def window(mb, cx, z, face, side, lit, q, lights, w=1.0, h=2.6, cell=None):
    hz = hw(z) + 0.06
    segs = 3 if q == 0 else 1
    shape = [(-w / 2, 0.0), (w / 2, 0.0), (w / 2, h - w / 2)]
    for k in range(1, segs):
        a = math.pi * k / segs
        shape.append((w / 2 * math.cos(a), h - w / 2 + w / 2 * math.sin(a)))
    shape.append((-w / 2, h - w / 2))
    if face == '-y':
        o, t, nrm = Vector((cx, -hz, 0)), Vector((1, 0, 0)), Vector((0, -1, 0))
    elif face == '+y':
        o, t, nrm = Vector((cx, hz, 0)), Vector((-1, 0, 0)), Vector((0, 1, 0))
    elif face == 'out':
        o, t, nrm = Vector((cx + side * hz, 0, 0)), Vector((0, side, 0)), Vector((side, 0, 0))
    else:
        o, t, nrm = Vector((cx - side * hz, 0, 0)), Vector((0, -side, 0)), Vector((-side, 0, 0))
    cell = cell or ('doorglow' if lit else 'recess')
    idx, uvs = [], []
    for (x, y) in shape:
        p = o + t * x + Vector((0, 0, z + y))
        idx.append(mb.vert(p, (0.0, 1.0 if lit else 0.0, 0.0, 1.0)))
        uvs.append(cell_uv(cell, (x + w / 2) / w, y / h))
    oriented(mb, idx, DETAIL, uvs, nrm)
    if lit and lights is not None:
        p = o + nrm * 0.6 + Vector((0, 0, z + 1.3))
        lights.append(dict(kind='window', pos=[round(p.x, 2), round(p.y, 2), round(p.z, 2)], radius_m=4.0, color='#ffad52'))


# --------------------------------------------------------------------------- arch

def arch(q, x0=-SPAN_HALF, x1=SPAN_HALF):
    """The arch body between the tower faces: a slab bounded by the intrados below and the deck above."""
    mb = MB()
    n = [72, 36, 16, 8][q]
    xs = [x0 + (x1 - x0) * i / n for i in range(n + 1)]
    W = ARCH_W / 2
    bot = {}
    topv = {}
    for side_y in (-W, W):
        bot[side_y] = [mb.vert((x, side_y, intrados(x))) for x in xs]
        topv[side_y] = [mb.vert((x, side_y, deck(x))) for x in xs]
    # side faces (front -y, back +y) as quad strips: intrados -> deck
    for side_y, want in ((-W, (0, -1, 0)), (W, (0, 1, 0))):
        for i in range(n):
            idx = [bot[side_y][i], bot[side_y][i + 1], topv[side_y][i + 1], topv[side_y][i]]
            pts = [mb.v[k] for k in idx]
            oriented(mb, idx, STRUCT, [uv_box(p, want, TILE) for p in pts], want)
    # soffit (underside) and deck: u along the curve, v across
    arc_s = [0.0]
    for i in range(1, n + 1):
        arc_s.append(arc_s[-1] + math.hypot(xs[i] - xs[i - 1], intrados(xs[i]) - intrados(xs[i - 1])))
    for i in range(n):
        idx = [bot[-W][i], bot[W][i], bot[W][i + 1], bot[-W][i + 1]]
        uv = [(arc_s[i] / TILE, 0.0), (arc_s[i] / TILE, ARCH_W / TILE), (arc_s[i + 1] / TILE, ARCH_W / TILE), (arc_s[i + 1] / TILE, 0.0)]
        oriented(mb, idx, STRUCT, uv, (0, 0, -1))
        idx = [topv[-W][i], topv[-W][i + 1], topv[W][i + 1], topv[W][i]]
        pts = [mb.v[k] for k in idx]
        oriented(mb, idx, STRUCT, [uv_box(p, (0, 0, 1), TILE) for p in pts], (0, 0, 1))
    # ends (hidden in the towers, or cut faces)
    for i, want in ((0, (-1, 0, 0)), (n, (1, 0, 0))):
        idx = [bot[-W][i], bot[W][i], topv[W][i], topv[-W][i]]
        pts = [mb.v[k] for k in idx]
        oriented(mb, idx, STRUCT, [uv_box(p, want, TILE) for p in pts], want)
    if q < 3:
        # voussoir ring standing proud of both faces
        for side_y in (-W, W):
            sgn = -1 if side_y < 0 else 1
            y_out = side_y + sgn * 0.3
            a0 = math.asin(max(-1, min(1, x0 / R_I)))
            a1 = math.asin(max(-1, min(1, x1 / R_I)))
            m = n
            ri, re = R_I - 0.05, R_I + RING
            prev = None
            for i in range(m + 1):
                a = a0 + (a1 - a0) * i / m
                pi_ = (ri * math.sin(a), CZ + ri * math.cos(a))
                pe = (re * math.sin(a), CZ + re * math.cos(a))
                vi_o = mb.vert((pi_[0], y_out, pi_[1]))
                ve_o = mb.vert((pe[0], y_out, pe[1]))
                vi_b = mb.vert((pi_[0], side_y, pi_[1]))
                ve_b = mb.vert((pe[0], side_y, pe[1]))
                cur = (vi_o, ve_o, vi_b, ve_b, a)
                if prev:
                    s0, s1 = prev[4] * R_I, a * R_I
                    # face
                    oriented(mb, [prev[0], cur[0], cur[1], prev[1]], STRUCT,
                             [(0.0, s0 / TILE), (0.0, s1 / TILE), (RING / TILE, s1 / TILE), (RING / TILE, s0 / TILE)], (0, sgn, 0))
                    # outer (extrados) lip and inner lip
                    oriented(mb, [prev[1], cur[1], cur[3], prev[3]], STRUCT,
                             [(s0 / TILE, 0), (s1 / TILE, 0), (s1 / TILE, 0.04), (s0 / TILE, 0.04)],
                             (math.sin((prev[4] + a) / 2), 0, math.cos((prev[4] + a) / 2)))
                    oriented(mb, [prev[0], cur[0], cur[2], prev[2]], STRUCT,
                             [(s0 / TILE, 0), (s1 / TILE, 0), (s1 / TILE, 0.04), (s0 / TILE, 0.04)],
                             (-math.sin((prev[4] + a) / 2), 0, -math.cos((prev[4] + a) / 2)))
                prev = cur
        # parapets along both deck edges, and a projecting cornice under them
        for side_y in (-W, W):
            sgn = -1 if side_y < 0 else 1
            for i in range(n):
                xa, xb = xs[i], xs[i + 1]
                for (yi, yo, z0off, z1off) in ((side_y - sgn * 0.6, side_y, 0.0, 1.35),):
                    va = [mb.vert((xa, yo, deck(xa) + z0off)), mb.vert((xb, yo, deck(xb) + z0off)),
                          mb.vert((xb, yo, deck(xb) + z1off)), mb.vert((xa, yo, deck(xa) + z1off))]
                    vb = [mb.vert((xa, yi, deck(xa) + z0off)), mb.vert((xb, yi, deck(xb) + z0off)),
                          mb.vert((xb, yi, deck(xb) + z1off)), mb.vert((xa, yi, deck(xa) + z1off))]
                    for quad, want in (([va[0], va[1], va[2], va[3]], (0, sgn, 0)), ([vb[0], vb[1], vb[2], vb[3]], (0, -sgn, 0)),
                                       ([va[3], va[2], vb[2], vb[3]], (0, 0, 1))):
                        pts = [mb.v[k] for k in quad]
                        oriented(mb, quad, STRUCT, [uv_box(p, want, TILE) for p in pts], want)
                # cornice band
                yo = side_y + sgn * 0.45
                za, zb = deck(xa) - 1.6, deck(xb) - 1.6
                cv = [mb.vert((xa, yo, za)), mb.vert((xb, yo, zb)), mb.vert((xb, yo, za + 0.9 + (zb - za))), mb.vert((xa, yo, za + 0.9))]
                cb = [mb.vert((xa, side_y, za - 0.3)), mb.vert((xb, side_y, zb - 0.3)), mb.vert((xb, side_y, zb + 0.9)), mb.vert((xa, side_y, za + 0.9))]
                for quad, want in (([cv[0], cv[1], cv[2], cv[3]], (0, sgn, 0)), ([cb[0], cb[1], cv[1], cv[0]], (0, sgn * 0.5, -1)),
                                   ([cv[3], cv[2], cb[2], cb[3]], (0, 0, 1))):
                    pts = [mb.v[k] for k in quad]
                    oriented(mb, quad, STRUCT, [uv_box(p, want, TILE) for p in pts], want)
    return mb


# --------------------------------------------------------------------------- lantern

def lantern(mb, q, lights=None, z0=None):
    z0 = DECK_C if z0 is None else z0
    segs = [16, 8, 8, 6][q]
    # drum
    lathe(mb, [(6.4, z0 - 0.4), (6.4, z0 + 1.6), (6.0, z0 + 2.0), (0.5, z0 + 2.0)], segs, STRUCT,
          uvfn=lambda i, th, j, r, z: (th * 6.4 / TILE, (z if j < 2 else r) / TILE), cap_top=False)
    # glass core with the fire behind it
    rows = lathe(mb, [(4.4, z0 + 2.0), (4.4, z0 + 8.0)], 8, DETAIL,
                 uvfn=lambda i, th, j, r, z: cell_uv('lantern', (i % 8) / 1.0 if False else (i % 2), j), cap_top=False)
    # fix glass uvs: one cell per facet
    k = len(mb.fuv) - 8
    for i in range(8):
        mb.fuv[k + i] = [cell_uv('lantern', 0, 0), cell_uv('lantern', 1, 0), cell_uv('lantern', 1, 1), cell_uv('lantern', 0, 1)]
    for vi in rows[0] + rows[1]:
        mb.col[vi] = (0.2, 1.0, 0.0, 1.0)
    # columns
    if q < 3:
        nc = 8
        for i in range(nc):
            a = TAU * (i + 0.5) / nc
            r = 5.3
            cyl_between(mb, (r * math.cos(a), r * math.sin(a), z0 + 2.0), (r * math.cos(a), r * math.sin(a), z0 + 8.0),
                        0.45, 0.4, [8, 6, 4][q], STRUCT, uzone=(0.0, 0.35), vlen=TILE)
    # entablature
    lathe(mb, [(4.2, z0 + 8.0), (6.3, z0 + 8.0), (6.5, z0 + 8.6), (6.5, z0 + 9.2), (6.1, z0 + 9.4)], segs * 2, STRUCT,
          uvfn=lambda i, th, j, r, z: (th * 6.5 / TILE, z / TILE))
    # ribbed bronze dome with a finial
    dprof = []
    nr = [9, 5, 4, 3][q]
    for j in range(nr + 1):
        t = j / nr
        a = t * math.pi / 2
        dprof.append((6.1 * math.cos(a) + 0.001, z0 + 9.4 + 6.2 * math.sin(a) * (1 + 0.15 * t)))
    lathe(mb, dprof, [32, 16, 10, 6][q], ROOF, uvfn=lambda i, th, j, r, z: (th * 6.1 / 4.0, (z - z0) / 4.0))
    if q < 2:
        nrib = 12
        for i in range(nrib):
            a = TAU * i / nrib
            for j in range(len(dprof) - 1):
                (r0, za), (r1, zb) = dprof[j], dprof[j + 1]
                cyl_between(mb, ((r0 + 0.12) * math.cos(a), (r0 + 0.12) * math.sin(a), za),
                            ((r1 + 0.12) * math.cos(a), (r1 + 0.12) * math.sin(a), zb), 0.2, 0.18, 4, ROOF, uzone=(0.0, 0.2), vlen=4.0, caps=False)
    top = dprof[-1][1]
    if q < 3:
        lathe(mb, [(0.001, top - 0.3), (0.9, top + 0.4), (0.7, top + 1.2), (0.15, top + 1.5), (0.06, top + 3.6), (0.001, top + 3.7)],
              [10, 6, 4][q], ROOF, uvfn=lambda i, th, j, r, z: (i / 10.0, z / 4.0))
    if lights is not None:
        lights.append(dict(kind='lantern', pos=[0.0, 0.0, round(z0 + 5.0, 2)], radius_m=400.0, color='#ffb050',
                           note='the harbour lighthouse: strongest light of the wonder; lays a reflection track on the water'))


# --------------------------------------------------------------------------- islets

def islet(mb, cx, q, rng, scale=1.0, extra_rubble=0):
    vs, fs = ico([3, 2, 1, 1][q])
    sx, sy, sz = 17.0 * scale, 15.5 * scale, 7.0
    base = len(mb.v)
    for v in vs:
        k = 1 + 0.12 * math.sin(v.x * 5 + cx) * math.sin(v.y * 4.3) + 0.08 * math.sin(v.z * 7 + v.x * 3)
        z = v.z * sz * k
        if z > 0:
            z *= 0.55
        mb.vert((cx + v.x * sx * k, v.y * sy * k, z - 1.2))
    lo = (cx - sx, -sy, -12)
    size = (2 * sx, 2 * sy, 20)
    for f in fs:
        idx = [base + i for i in f]
        pts = [mb.v[i] for i in idx]
        # stone cell, uv tiled per ~6 m so the rock grain is not stretched
        n = face_normal(pts)
        uv = []
        for p in pts:
            u, v = uv_box(p, n, 6.0)
            uv.append(cell_uv('stone', u % 1.0, v % 1.0))
        # avoid wrapping inside one face: if a face straddles a tile edge, project it unwrapped into the cell
        us = [p[0] for p in uv]
        vv = [p[1] for p in uv]
        if max(us) - min(us) > 0.12 or max(vv) - min(vv) > 0.12:
            uv = [cell_uv('stone', 0.2 + 0.6 * (i == 1) + 0.3 * (i == 2), 0.2 + 0.6 * (i == 2)) for i in range(len(pts))]
        mb.face(idx, DETAIL, uv, True)
    # boulders: densest at the waterline, a scatter on top
    if q < 3:
        nb = [150, 60, 18][q] + extra_rubble
        for k in range(nb):
            a = rng.random() * TAU
            rr = rng.uniform(0.55, 1.05)
            x = cx + math.cos(a) * sx * rr
            y = math.sin(a) * sy * rr
            z = 2.6 * (1 - rr) * 1.8 - 0.4 + rng.uniform(-0.6, 0.4)
            s = rng.uniform(1.0, 2.6) * (1.25 if rr > 0.9 else 1.0)
            rock(mb, (x, y, z), (s * 1.25, s, s * 0.8), rng, level=(1 if (q == 0 and s > 2.0) else (0 if q < 2 else -1)), cell='stone')


def plinth(mb, cx, q):
    h = TW_HALF0 + 2.2
    block(mb, (cx, 0, 0.6), (h, h, 3.2), 0.0, STRUCT, tile=TILE)
    block(mb, (cx, 0, 3.6), (h - 0.9, h - 0.9, 0.4), 0.0, STRUCT, tile=TILE)


# --------------------------------------------------------------------------- states

def build(state, q):
    rng = random.Random(400)
    mb = MB()
    lights = []
    if state == 'Intact':
        for s in (-1, 1):
            islet(mb, s * TW_X, q, rng)
            plinth(mb, s * TW_X, q)
            tower(mb, s * TW_X, q, lights=lights, side=s)
        mb.add(arch(q))
        lantern(mb, q, lights)
    elif state == 'Construction':
        for s in (-1, 1):
            islet(mb, s * TW_X, q, rng, extra_rubble=30)
            plinth(mb, s * TW_X, q)
            tower(mb, s * TW_X, q, z_top=44.0 if s < 0 else 40.0, lights=lights, side=s, lit_fn=lambda n: n % 4 == 0)
        # the first courses of the arch, springing from both towers, on timber centering
        left = mb_cut_planes(arch(q, -SPAN_HALF, -52.0), [((-60.0, 0, 0), (1.0, 0, 0.3)), ((0, 0, 30.0), (0.0, 0.05, 1.0))], STRUCT, TILE)
        right = mb_cut_planes(arch(q, 52.0, SPAN_HALF), [((64.0, 0, 0), (-1.0, 0, 0.3)), ((0, 0, 28.0), (0.0, -0.05, 1.0))], STRUCT, TILE)
        mb.add(left)
        mb.add(right)
        if q < 3:
            centering(mb, q)
            for s in (-1, 1):
                scaff(mb, s * TW_X, (44.0 if s < 0 else 40.0), q)
    elif state == 'Damaged':
        for s in (-1, 1):
            islet(mb, s * TW_X, q, rng, extra_rubble=20)
            plinth(mb, s * TW_X, q)
        tower(mb, -TW_X, q, lights=lights, side=-1, lit_fn=lambda n: n % 4 == 1)
        t = MB()
        tower(t, TW_X, q, lights=None, side=1, lit_fn=lambda n: False)
        t = mb_cut_planes(t, [((TW_X, 0, 58.0), (0.35, -0.25, 1.0)), ((TW_X, 0, 61.0), (-0.3, 0.2, 1.0))], STRUCT, TILE)
        mb.add(t)
        a = arch(q)
        left = mb_cut_planes(a, [((-24.0, 0, 50.0), (1.0, 0.1, 0.25)), ((-30.0, 0, 50.0), (0.6, -0.2, -0.8))], STRUCT, TILE)
        right = mb_cut_planes(a, [((18.0, 0, 50.0), (-1.0, 0.15, 0.3)), ((25.0, 0, 46.0), (-0.5, 0.3, -0.9))], STRUCT, TILE)
        mb.add(left)
        mb.add(right)
        # fallen voussoirs and the lantern dome in the channel, half sunk
        if q < 3:
            for k in range([46, 16, 5][q]):
                x = rng.uniform(-26, 22)
                y = rng.uniform(-14, 14)
                s = rng.uniform(1.2, 2.6)
                block(mb, (x, y, rng.uniform(-0.8, 0.6)), (s * 1.4, s, s * 0.8), rng.random() * TAU, STRUCT, tile=TILE,
                      rot=Matrix.Rotation(rng.uniform(-0.6, 0.6), 3, 'X') @ Matrix.Rotation(rng.random() * TAU, 3, 'Z'))
            d = MB()
            lantern(d, q, None, z0=0.0)
            mb.add(d.transformed(Matrix.Translation(Vector((6.0, 9.0, -4.0))) @ Matrix.Rotation(1.2, 4, 'Y') @ Matrix.Rotation(0.4, 4, 'X')))
    elif state == 'Ruin':
        for s, ztop in ((-1, 31.0), (1, 22.0)):
            islet(mb, s * TW_X, q, rng, scale=1.1, extra_rubble=60)
            plinth(mb, s * TW_X, q)
            t = MB()
            tower(t, s * TW_X, q, z_top=ztop + 6.0, lit_fn=lambda n: False, side=s)
            t = mb_cut_planes(t, [((s * TW_X, 0, ztop), (0.45 * s, 0.15, 1.0)), ((s * TW_X, 0, ztop + 1.2), (0.15 * s, -0.4, 1.0))], STRUCT, TILE)
            mb.add(t)
        if q < 3:
            for k in range([70, 22, 6][q]):
                x = rng.choice((-1, 1)) * TW_X + rng.uniform(-24, 24)
                y = rng.uniform(-18, 18)
                s = rng.uniform(1.0, 2.4)
                block(mb, (x, y, rng.uniform(-0.6, 2.0)), (s * 1.4, s, s * 0.8), 0.0, STRUCT, tile=TILE,
                      rot=Matrix.Rotation(rng.uniform(-0.7, 0.7), 3, 'Y') @ Matrix.Rotation(rng.random() * TAU, 3, 'Z'))
    return mb, lights


def centering(mb, q):
    """Timber falsework under the unbuilt arch: two ribs, struts, and props down to the water."""
    sides = [4, 3, 3][q]
    n = [28, 14, 8][q]
    for y in (-3.5, 3.5):
        pts = []
        for i in range(n + 1):
            x = -SPAN_HALF + 2 * SPAN_HALF * i / n
            pts.append(Vector((x, y, intrados(x) - 0.5)))
        for i in range(n):
            cyl_between(mb, pts[i], pts[i + 1], 0.35, 0.35, sides, DETAIL, cell='timber', caps=False)
        for i in range(2, n - 1, 2):
            p = pts[i]
            cyl_between(mb, p, Vector((p.x, y, -1.5)), 0.3, 0.3, sides, DETAIL, cell='timber', caps=False)
            if i + 2 < n:
                cyl_between(mb, Vector((p.x, y, p.z - 8.0)), pts[i + 2], 0.2, 0.2, sides, DETAIL, cell='timber', caps=False)
    for i in range(1, n, 3):
        x = -SPAN_HALF + 2 * SPAN_HALF * i / n
        z = intrados(x) - 0.5
        cyl_between(mb, (x, -3.6, z), (x, 3.6, z), 0.2, 0.2, sides, DETAIL, cell='timber', caps=False)


def scaff(mb, cx, ztop, q):
    sides = [4, 3, 3][q]
    h = hw(ztop) + 1.6
    for (dx, dy) in ((-h, -h), (h, -h), (h, h), (-h, h), (0, -h), (0, h), (h, 0), (-h, 0)):
        cyl_between(mb, (cx + dx, dy, ztop - 14.0), (cx + dx, dy, ztop + 6.0), 0.16, 0.14, sides, DETAIL, cell='timber')
    if q < 2:
        for z in (ztop - 10.0, ztop - 5.0, ztop, ztop + 4.0):
            for (a, b) in (((-h, -h), (h, -h)), ((h, -h), (h, h)), ((h, h), (-h, h)), ((-h, h), (-h, -h))):
                cyl_between(mb, (cx + a[0], a[1], z), (cx + b[0], b[1], z), 0.1, 0.1, 3, DETAIL, cell='timber', caps=False)


def lod3(state):
    return build(state, 3)


CAMERAS = {
    'close': ((-66.0, -6.0, 24.0), 66.0, 30.0, 8.0, 30.0),   # tower foot and figure in frame (verifier fix)
    'mid': ((0.0, 0.0, 30.0), 300.0, 30.0, 26.0, 35.0),
    'far': ((0.0, 0.0, 25.0), 1000.0, 30.0, 40.0, 35.0),
    'figure': (-80.0, -7.6, 0.2, 4.0),   # on the plinth at the tower foot, was floating 3 m above the islet (verifier fix)
}
