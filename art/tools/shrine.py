# DEMIURGE art pipeline - the village shrine (b.shrine), built procedurally from the T-014 reference sheet
# (docs/concept/assets/shrine/index.md) and the T-015 states (docs/concept/assets/shrine/states/index.md).
#
# build(config, lod) -> {part name: MB}. config = 'intact' | 'construction' | 'damaged' | 'burnt' | 'ruin' |
# 'drowned' | 'culture_a' | 'culture_b' | 'culture_c'. lod = 0 (hero, ~6-10k tris), 1 (~2k), 2 (map, ~300).
# masks(config) -> per-vertex mask function for the stain (G) and growth (B) channels.
# Blender frame: metres, Z up, pivot at ground level in the centre of the plinth (not the steps), front = -Y,
# the window is in the right-hand side wall (+X) as seen from the front.
import math
import random

from mathutils import Matrix, Vector, noise

import dmkit as K
from dmkit import MB, V

# ----------------------------------------------------------------------------------------------- dimensions
PL_W, PL_D, PL_H = 6.0, 4.4, 0.8
SKIRT = 1.0                       # plinth continues below ground so the asset sits on slopes
COP_H, COP_IN = 0.10, 0.05        # top course: 0.10 high, set back 0.05
STEP_W, RISER, TREAD, NSTEP, CHEEK = 2.4, 0.20, 0.22, 4, 0.22
CEL_W, CEL_D, CEL_Y0 = 4.8, 3.2, -1.7       # cella: front face at y = -1.7 (0.5 behind the plinth front)
WALL_H, WALL_T = 2.0, 0.45
Z_FLOOR = PL_H
Z_EAVE = PL_H + WALL_H            # 2.8: top of the walls / eave plate
POST, PROUD, CAP_H, CAP_W = 0.32, 0.08, 0.12, 0.40
DOOR_W, DOOR_H, DPOST = 1.1, 1.6, 0.22
LIN_L, LIN_H, LIN_D = 2.2, 0.30, 0.25
OV_SIDE, OV_FRONT = 0.3, 0.4
RIDGE_L, RIDGE_H, THATCH_T = 2.6, 1.45, 0.30
FIN_H = 0.35
STONE = (0.65, 0.45, 1.25)
STONE_X, STONE_Y = 1.7, -2.72
ALTAR = (0.9, 0.6, 0.45)
WIN, WIN_SILL = 0.35, 1.1

X0, X1 = -CEL_W / 2, CEL_W / 2               # cella outer faces
Y0, Y1 = CEL_Y0, CEL_Y0 + CEL_D              # -1.7 .. 1.5
EX0, EX1 = X0 - OV_SIDE, X1 + OV_SIDE        # eave outline
EY0, EY1 = Y0 - OV_FRONT, Y1 + OV_SIDE       # -2.1 .. 1.8
YR = (EY0 + EY1) / 2                         # ridge line y
Z_ET = 3.08                                  # thatch top surface at the eave edge (front view: 3.06)
Z_EB = Z_ET - THATCH_T                       # ragged lip bottom (2.78; the lintel shows below it)
Z_RIDGE = Z_EAVE + RIDGE_H                   # 4.2: ridge top (roll included)
ROLL_R = 0.10
Z_RP = Z_RIDGE - ROLL_R                      # plane ridge line (roll centre)
FIRE = Vector((0.0, -0.65, PL_H + ALTAR[2] + 0.12))   # fire point (altar bowl), for the fire-light mask

CONFIGS = ['intact', 'construction', 'damaged', 'burnt', 'ruin', 'drowned', 'culture_a', 'culture_b', 'culture_c']
WATER_Z = PL_H + 1.0      # drowned: water plane 1.0 m above the plinth top (1.8 m above ground)


def rng(seed):
    return random.Random(seed)


def sstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# ----------------------------------------------------------------------------------------------- plinth / steps


def plinth(cfg, lod):
    mb = MB('plinth')
    mat = {'construction': 'limestone_fresh', 'culture_a': 'plaster'}.get(cfg, 'limestone')
    style = {'drowned': '@wet', 'damaged': '@dust', 'ruin': '@ruin'}.get(cfg, '')
    m = mat + style
    hx, hy = PL_W / 2, PL_D / 2
    if lod == 2:
        K.cbox(mb, (0, 0, (PL_H - COP_H - SKIRT) / 2), (hx, hy, (PL_H - COP_H + SKIRT) / 2), m, 0.0, skip=('-z',))
        K.cbox(mb, (0, 0, PL_H - COP_H / 2), (hx - COP_IN, hy - COP_IN, COP_H / 2), m, 0.0, skip=('-z',))
        return {'plinth': mb}
    wear = {'ruin': 0.045, 'drowned': 0.02}.get(cfg, 0.012)
    jit = 0.004 if cfg == 'culture_a' else 0.012

    def disp(p, n):
        d = n * (K.fbm(p, 3, 1.7, 3.0) * jit)
        # worn arrises: vertices on the upper edges sink and round off
        ex = min(hx - abs(p.x), hy - abs(p.y))
        if p.z > PL_H - COP_H - 0.01 and ex < 0.02 + COP_IN:
            d += Vector((0, 0, -wear * (0.5 + 0.5 * K.fbm(p, 2, 4.0, 7.0))))
        return d
    sp = 0.5 if lod == 0 else 1.2
    # body (to the top of the second course from the top), skirt below ground, no bottom
    K.gbox(mb, (-hx, -hy, -SKIRT), (hx, hy, PL_H - COP_H), m, sp, skip=('-z', '+z'), disp=disp, key='pb', uv_z0=-0.1)
    # top ledge of the body around the set-back coping
    zt = PL_H - COP_H
    ring_o = [(-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy)]
    ring_i = [(-hx + COP_IN, -hy + COP_IN), (hx - COP_IN, -hy + COP_IN), (hx - COP_IN, hy - COP_IN), (-hx + COP_IN, hy - COP_IN)]
    tile = K.tile_of(m)
    for i in range(4):
        a, b = ring_o[i], ring_o[(i + 1) % 4]
        c, d = ring_i[(i + 1) % 4], ring_i[i]
        pts = [V(a[0], a[1], zt), V(b[0], b[1], zt), V(c[0], c[1], zt), V(d[0], d[1], zt)]
        # subdivide the ledge along its length so it welds to the grid below
        L = (V(*b, 0) - V(*a, 0)).length
        nseg = max(1, int(round(L / sp)))
        for k in range(nseg):
            f0, f1 = k / nseg, (k + 1) / nseg
            q = [pts[0].lerp(pts[1], f0), pts[0].lerp(pts[1], f1), pts[3].lerp(pts[2], f1), pts[3].lerp(pts[2], f0)]
            qd = [x + disp(x, Vector((0, 0, 1))) if j < 2 else x for j, x in enumerate(q)]
            ids = [mb.v(qd[0], ('pb', round(q[0].x, 4), round(q[0].y, 4), round(q[0].z, 4))),
                   mb.v(qd[1], ('pb', round(q[1].x, 4), round(q[1].y, 4), round(q[1].z, 4))),
                   mb.v(qd[2], ('pc', round(q[2].x, 4), round(q[2].y, 4), round(q[2].z, 4))),
                   mb.v(qd[3], ('pc', round(q[3].x, 4), round(q[3].y, 4), round(q[3].z, 4)))]
            mb.face(ids, m, [(x.x / tile, x.y / tile) for x in q], (0, 0, 1))
    # coping course: grid box from zt to PL_H, top subdivided (vertex AO under the walls, masks)
    K.gbox(mb, (-hx + COP_IN, -hy + COP_IN, zt), (hx - COP_IN, hy - COP_IN, PL_H), m, sp if lod == 0 else 1.5, skip=('-z',),
           disp=lambda p, n: n * (K.fbm(p, 3, 1.7, 5.0) * jit * 0.6), key='pc', uv_z0=-0.1)
    parts = {'plinth': mb}
    if cfg == 'damaged' and lod == 0:
        # one displaced block on the front course, right of the steps, pushed out 5 cm
        b = MB('plinth_block')
        b.push(Matrix.Translation((1.55, -hy - 0.05 + 0.13, 0.45)) @ Matrix.Rotation(0.06, 4, 'Z'))
        K.cbox(b, (0, 0, 0), (0.26, 0.14, 0.1), m, 0.02)
        parts['plinth_block'] = b
    return parts


def steps(cfg, lod):
    mb = MB('steps')
    mat = {'construction': 'limestone_fresh', 'culture_a': 'plaster'}.get(cfg, 'limestone')
    m = mat + {'drowned': '@wet', 'damaged': '@dust', 'ruin': '@ruin'}.get(cfg, '')
    y_front = -PL_D / 2 - NSTEP * TREAD
    if lod == 2:
        # a single sloped block
        tile = K.tile_of(m)
        yb = -PL_D / 2 + 0.05
        pts = [V(-STEP_W / 2, y_front, 0), V(STEP_W / 2, y_front, 0), V(STEP_W / 2, yb, PL_H), V(-STEP_W / 2, yb, PL_H)]
        mb.face([mb.v(p) for p in pts], m, [(p.x / tile, p.z / tile) for p in pts], (0, -1, 1))
        for sx in (-1, 1):
            tri = [V(sx * STEP_W / 2, y_front, 0), V(sx * STEP_W / 2, yb, 0), V(sx * STEP_W / 2, yb, PL_H)]
            mb.face([mb.v(p) for p in tri], m, [(p.y / tile, p.z / tile) for p in tri], (sx, 0, 0))
        return {'steps': mb}
    c = 0.025 if lod == 0 else 0.0
    dish = {'ruin': 0.035, 'construction': 0.0}.get(cfg, 0.008)
    inner = STEP_W / 2 - CHEEK
    for k in range(NSTEP):
        zt = RISER * (k + 1)
        yk = y_front + TREAD * k
        zb = -0.3 if k == 0 else zt - RISER - 0.04
        if lod == 0:
            # tread made of 3 blocks so the joints read and the middle can be dished (worn)
            xs = [-inner, -inner / 3 - 0.07 * ((k % 2) * 2 - 1), inner / 3 + 0.05 * ((k % 2) * 2 - 1), inner]
            for j in range(3):
                x0, x1 = xs[j], xs[j + 1]
                dz = -dish if j == 1 else -dish * 0.3
                K.cbox(mb, ((x0 + x1) / 2, (yk - PL_D / 2 + 0.1) / 2, (zb + zt + dz) / 2),
                       ((x1 - x0) / 2 - 0.004, (-PL_D / 2 + 0.1 - yk) / 2, (zt + dz - zb) / 2), m, c, skip=('-z', '+y'))
        else:
            K.cbox(mb, (0, (yk - PL_D / 2 + 0.1) / 2, (zb + zt) / 2), (inner, (-PL_D / 2 + 0.1 - yk) / 2, (zt - zb) / 2), m, c, skip=('-z', '+y'))
        for sx in (-1, 1):
            K.cbox(mb, (sx * (inner + CHEEK / 2), (yk - PL_D / 2 + 0.1) / 2, (zb + zt + 0.05) / 2),
                   (CHEEK / 2, (-PL_D / 2 + 0.1 - yk) / 2, (zt + 0.05 - zb) / 2), m, c, skip=('-z', '+y'))
    return {'steps': mb}


def standing_stones(cfg, lod):
    out = {}
    m = 'monolith' + {'drowned': '@wet', 'ruin': '@ruin', 'damaged': '@dust'}.get(cfg, '')
    for i, sx in enumerate((-1, 1)):
        mb = MB('standing_stone_%s' % ('l' if sx < 0 else 'r'))
        w, d, h = STONE
        bury = 0.2
        if lod == 2:
            prof = [(0.30, -bury), (0.33, 0.4), (0.27, 0.9), (0.12, h - 0.05), (0.0, h)]
            mb.push(Matrix.Translation((sx * STONE_X, STONE_Y, 0)) @ Matrix.Diagonal((1.0, d / w, 1.0, 1.0)))
            K.lathe(mb, prof, 5, m, cap_top=False)
        else:
            def shape(p, hh=h + bury):
                # icosphere point -> monolith: undo the sphere's taper, then a slab section (wider than deep, a little
                # squared) at full width up to 62 % of the height and a rounded dome above
                hz = hh / 2
                t = max(-1.0, min(1.0, p.z / hz))
                ring = math.sqrt(max(1e-4, 1.0 - t * t))
                zf = (t + 1.0) / 2.0
                if zf < 0.45:
                    k = 1.0 - 0.08 * zf
                else:
                    u = (zf - 0.45) / 0.55
                    k = 0.964 * math.sqrt(max(0.0, 1.0 - u ** 1.7))
                k = max(k, 0.04)
                x, y = p.x / ring, p.y / ring
                ax, ay = abs(x) / (w / 2), abs(y) / (d / 2)
                sq = 1.0 + 0.12 * min(ax, ay) / max(ax, ay, 1e-3)
                return Vector((x * k * sq, y * k * sq, t * hz))
            lean = Matrix.Translation((sx * STONE_X, STONE_Y, 0)) @ Matrix.Rotation(math.radians(3.5 * sx + 1.5), 4, 'Y') @ Matrix.Rotation(math.radians(-2.5), 4, 'X')
            old = mb.push(lean)
            K.blob(mb, (0, 0, (h - bury) / 2), (w, d, h + bury), m, seed=11 + i, subdiv=2 if lod == 0 else 1,
                   rough=0.085, scale_fn=shape, rot=0.15 * sx + 0.2 * i)
            mb.pop(old)
        out[mb.name] = mb
    return out


# ----------------------------------------------------------------------------------------------- walls


def wall_tops(cfg):
    """top(s) per wall (front, back, left, right) as (fn, kinks, rough_top)."""
    zt = Z_EAVE
    seed_off = {'front': 0.0, 'back': 3.3, 'left': 6.1, 'right': 9.7}

    def flat(_s):
        return zt
    tops = {}
    for wname in ('front', 'back', 'left', 'right'):
        so = seed_off[wname]
        if cfg == 'construction':
            # built to 1.1-1.4 m with stepped, uneven top courses
            def f(s, so=so):
                base = Z_FLOOR + 1.25 + 0.12 * noise.noise(Vector((s * 0.7, so, 0.0)))
                step = math.floor((s * 3.1 + so) % 7) * 0.035
                return base + step - 0.1
            tops[wname] = (f, [], 0.02)
        elif cfg == 'ruin':
            # stumps 0.6-1.5 m, highest at the back corners, lowest at the front
            def f(s, so=so, w=wname):
                L = CEL_W if w in ('front', 'back') else CEL_D - 2 * WALL_T
                if w == 'back':
                    hgt = 1.1 + 0.35 * (abs(s - L / 2) / (L / 2)) ** 1.5
                elif w == 'front':
                    hgt = 0.6 + 0.25 * (abs(s - L / 2) / (L / 2)) ** 2
                else:
                    hgt = 0.7 + 0.75 * (s / L) ** 1.3       # s runs front -> back on the side walls
                hgt += 0.18 * noise.noise(Vector((s * 1.7, so, 1.0))) + 0.07 * (math.floor(s * 4.3 + so) % 3)
                return Z_FLOOR + max(0.45, min(1.55, hgt))
            tops[wname] = (f, [], 0.04)
        elif cfg == 'damaged' and wname in ('right', 'front'):
            def f(s, w=wname):
                # right side wall: s runs front -> back (0 .. 2.3); front wall: s runs left -> right (0 .. 4.8)
                if w == 'right':
                    gone = 1.0 - sstep(0.55, 1.05, s)       # front 35 % collapsed
                else:
                    gone = sstep(3.7, 4.35, s)               # right end of the front wall
                low = Z_FLOOR + 0.6 + 0.25 * noise.noise(Vector((s * 2.3, 4.0, 2.0)))
                jag = 0.12 * noise.noise(Vector((s * 5.0, 1.0, 3.0)))
                return zt + (low - zt + jag) * gone
            tops[wname] = (f, [], 0.03)
        else:
            tops[wname] = (flat, [], 0.0)
    return tops


def walls(cfg, lod):
    if cfg == 'culture_b':
        return walls_frame_reed(lod)
    if cfg == 'culture_c':
        return walls_drum(lod)
    mb = MB('walls')
    mat = {'culture_a': 'plaster'}.get(cfg, 'rubble')
    m = mat + {'drowned': '@wet', 'damaged': '@dust', 'ruin': '@ruin'}.get(cfg, '')
    lod2 = lod == 2
    ds = 0.25 if lod == 0 else (0.8 if lod == 1 else 99.0)
    dz = 0.3 if lod == 0 else (1.0 if lod == 1 else 99.0)
    jit = (0.004 if mat == 'plaster' else 0.014) if lod == 0 else 0.0
    corner = 0.0 if mat == 'plaster' else (0.035 if cfg in ('ruin', 'damaged', 'burnt', 'construction') else 0.02)
    tops = wall_tops(cfg)
    t = WALL_T
    door_half = DOOR_W / 2 + DPOST       # the opening in the masonry receives the door posts
    door = (CEL_W / 2 - door_half, CEL_W / 2 + door_half, Z_FLOOR, Z_FLOOR + DOOR_H)
    if cfg == 'ruin':
        door = (CEL_W / 2 - DOOR_W / 2 - 0.05, CEL_W / 2 + DOOR_W / 2 + 0.12, Z_FLOOR, Z_FLOOR + DOOR_H)
    inner_d = CEL_D - 2 * t
    win_c = inner_d / 2 + (-0.1 - (Y0 + t + inner_d / 2))
    win = (win_c - WIN / 2, win_c + WIN / 2, Z_FLOOR + WIN_SILL, Z_FLOOR + WIN_SILL + WIN)
    specs = {
        # name: (origin, ex (along), ey (outward), length, holes, caps)
        'front': (V(X0, Y0 + t, 0), V(1, 0, 0), V(0, -1, 0), CEL_W, [door], (True, True)),
        'back': (V(X1, Y1 - t, 0), V(-1, 0, 0), V(0, 1, 0), CEL_W, [], (True, True)),
        'left': (V(X0 + t, Y0 + t, 0), V(0, 1, 0), V(-1, 0, 0), inner_d, [], (False, False)),
        'right': (V(X1 - t, Y0 + t, 0), V(0, 1, 0), V(1, 0, 0), inner_d, [win], (False, False)),
    }
    for i, (wname, (o, ex, ey, L, holes, caps)) in enumerate(specs.items()):
        fn, kinks, rough = tops[wname]
        if lod2:
            fn = (lambda s_, h=(fn(0.0) + fn(L / 2) + fn(L)) / 3: h)
            rough = 0.0
        if cfg == 'construction' and wname == 'right':
            holes = [(win[0], win[1], win[2], win[3] + 0.4)]
        if cfg == 'ruin' and wname != 'front':
            holes = []
        if cfg == 'damaged' and wname == 'right':
            holes = []
        if lod2 and wname == 'right':
            holes = []
        old = mb.push(K.frame(o, ex, ey))
        K.wall(mb, L, t, Z_FLOOR, fn, holes, m, ds, dz, caps[0], caps[1], jit, corner, rough if lod == 0 else 0.0,
               seed=i * 7 + 1, uoff=i * 0.83, kinks=kinks, key='w' + wname)
        mb.pop(old)
    parts = {'walls': mb}
    if cfg not in ('ruin', 'burnt') and lod == 0 and cfg != 'culture_a':
        # window frame (oak) and a dark shutter-less opening
        wf = MB('window_frame')
        om = oak_mat(cfg)
        xw = X1 + 0.005
        yc = -0.1
        zc = Z_FLOOR + WIN_SILL + WIN / 2
        for (cy, cz, hy, hz) in ((yc, zc - WIN / 2 - 0.03, WIN / 2 + 0.06, 0.03), (yc, zc + WIN / 2 + 0.03, WIN / 2 + 0.06, 0.03),
                                 (yc - WIN / 2 - 0.03, zc, 0.03, WIN / 2), (yc + WIN / 2 + 0.03, zc, 0.03, WIN / 2)):
            K.cbox(wf, (xw - 0.04, cy, cz), (0.06, hy, hz), om, 0.008, grain='y' if hz < hy else 'z')
        parts['window_frame'] = wf
    return parts


def walls_frame_reed(lod):
    """Culture B: woven reed panels between oak frames (same cella footprint)."""
    mb = MB('walls')
    fr = MB('wall_frame')
    t = 0.12
    zt = Z_EAVE
    if lod == 2:
        K.cbox(mb, (0, (Y0 + Y1) / 2, (Z_FLOOR + zt) / 2), (CEL_W / 2, CEL_D / 2, WALL_H / 2), 'reed_panel', 0.0, skip=('-z',))
        return {'walls': mb}
    door_half = DOOR_W / 2 + DPOST
    sides = [
        ('front', V(X0, Y0 + t, 0), V(1, 0, 0), V(0, -1, 0), CEL_W, [(CEL_W / 2 - door_half, CEL_W / 2 + door_half, Z_FLOOR, Z_FLOOR + DOOR_H)]),
        ('back', V(X1, Y1 - t, 0), V(-1, 0, 0), V(0, 1, 0), CEL_W, []),
        ('left', V(X0 + t, Y0, 0), V(0, 1, 0), V(-1, 0, 0), CEL_D, []),
        ('right', V(X1 - t, Y0, 0), V(0, 1, 0), V(1, 0, 0), CEL_D, []),
    ]
    for i, (nm, o, ex, ey, L, holes) in enumerate(sides):
        old = mb.push(K.frame(o, ex, ey))
        K.wall(mb, L, t, Z_FLOOR, lambda s: zt, holes, 'reed_panel', 0.3 if lod == 0 else 1.2, 0.3 if lod == 0 else 1.0,
               True, True, 0.006 if lod == 0 else 0.0, 0.0, seed=i, uoff=i * 0.7, key='rw' + nm)
        mb.pop(old)
        # oak frame: sill, top plate, studs every ~1.2 m, a brace in each bay (outer face)
        old = fr.push(K.frame(o, ex, ey))
        w_out = t + 0.06
        K.cbox(fr, (L / 2, w_out - 0.06, Z_FLOOR + 0.08), (L / 2, 0.07, 0.08), 'oak', 0.012, grain='x')
        K.cbox(fr, (L / 2, w_out - 0.06, zt - 0.08), (L / 2, 0.07, 0.08), 'oak', 0.012, grain='x')
        nb = max(1, int(round(L / 1.3)))
        for k in range(1, nb):
            s = L * k / nb
            if holes and holes[0][0] - 0.15 < s < holes[0][1] + 0.15:
                continue
            K.cbox(fr, (s, w_out - 0.06, (Z_FLOOR + zt) / 2), (0.07, 0.07, WALL_H / 2), 'oak', 0.012)
        if lod == 0:
            for k in range(nb):
                s0, s1 = L * k / nb + 0.1, L * (k + 1) / nb - 0.1
                if holes and not (s1 < holes[0][0] or s0 > holes[0][1]):
                    continue
                a = V(s0, w_out - 0.05, Z_FLOOR + 0.18)
                b = V(s1, w_out - 0.05, zt - 0.18)
                if k % 2:
                    a, b = V(s1, w_out - 0.05, Z_FLOOR + 0.18), V(s0, w_out - 0.05, zt - 0.18)
                K.tube(fr, [a, b], 0.045, 4, 'oak')
        fr.pop(old)
    return {'walls': mb, 'wall_frame': fr}


def walls_drum(lod):
    """Culture C: the cella becomes a round drum of pale dressed stone (door frame stays straight)."""
    mb = MB('walls')
    r = DRUM_R
    cy = DRUM_Y
    n = 40 if lod == 0 else (16 if lod == 1 else 10)
    m = 'dressed_stone'
    tile = K.tile_of(m)
    zs = [Z_FLOOR + WALL_H * k / (6 if lod == 0 else 1) for k in range((6 if lod == 0 else 1) + 1)]
    door_a = math.asin((DOOR_W / 2 + DPOST) / r)
    ring = []
    for k in range(n + 1):
        a = -math.pi / 2 + 2 * math.pi * k / n
        ring.append(a)
    # outer and inner surfaces, skipping the door gap (front = angle -90 deg)
    for (rad, want_out) in ((r, 1), (r - WALL_T * 0.8, -1)):
        if lod == 2 and want_out < 0:
            continue
        for k in range(n):
            a0, a1 = ring[k], ring[k + 1]
            am = (a0 + a1) / 2
            for j in range(len(zs) - 1):
                in_door = abs(((am + math.pi / 2 + math.pi) % (2 * math.pi)) - math.pi) < door_a and zs[j] < Z_FLOOR + DOOR_H - 1e-3
                if in_door and lod < 2:
                    continue
                p = [V(rad * math.cos(a0), cy + rad * math.sin(a0), zs[j]), V(rad * math.cos(a1), cy + rad * math.sin(a1), zs[j]),
                     V(rad * math.cos(a1), cy + rad * math.sin(a1), zs[j + 1]), V(rad * math.cos(a0), cy + rad * math.sin(a0), zs[j + 1])]
                uvs = [(r * a0 / tile, (q.z - Z_FLOOR) / tile) for q in p[:1]] + [(r * a1 / tile, (p[1].z - Z_FLOOR) / tile), (r * a1 / tile, (p[2].z - Z_FLOOR) / tile), (r * a0 / tile, (p[3].z - Z_FLOOR) / tile)]
                mb.face([mb.v(q, ('dr', round(q.x, 4), round(q.y, 4), round(q.z, 4))) for q in p], m, uvs,
                        V(math.cos(am), math.sin(am), 0) * want_out)
    if lod < 2:
        ri = r - WALL_T * 0.8
        for k in range(n):
            a0, a1 = ring[k], ring[k + 1]
            p = [V(ri * math.cos(a0), cy + ri * math.sin(a0), Z_EAVE), V(r * math.cos(a0), cy + r * math.sin(a0), Z_EAVE),
                 V(r * math.cos(a1), cy + r * math.sin(a1), Z_EAVE), V(ri * math.cos(a1), cy + ri * math.sin(a1), Z_EAVE)]
            mb.face([mb.v(q, ('dr', round(q.x, 4), round(q.y, 4), round(q.z, 4))) for q in p], m, [(q.x / tile, q.y / tile) for q in p], (0, 0, 1))
        # reveals at the door jambs
        for sgn in (-1, 1):
            a = -math.pi / 2 + sgn * door_a
            p = [V(ri * math.cos(a), cy + ri * math.sin(a), Z_FLOOR), V(r * math.cos(a), cy + r * math.sin(a), Z_FLOOR),
                 V(r * math.cos(a), cy + r * math.sin(a), Z_FLOOR + DOOR_H), V(ri * math.cos(a), cy + ri * math.sin(a), Z_FLOOR + DOOR_H)]
            mb.face([mb.v(q) for q in p], m, [(q.y / tile, q.z / tile) for q in p], (-sgn, 0, 0))
    return {'walls': mb}


DRUM_R = 2.15
DRUM_Y = 0.0


# ----------------------------------------------------------------------------------------------- timber


def oak_mat(cfg):
    return {'construction': 'oak_fresh', 'burnt': 'oak_charred', 'drowned': 'oak@wet', 'damaged': 'oak@dust',
            'culture_a': 'paint_cobalt'}.get(cfg, 'oak')


def posts(cfg, lod):
    if cfg == 'ruin':
        return {}
    mb = MB('posts')
    om = oak_mat(cfg)
    # culture A: corner posts are plastered over (not built); the door surround is cobalt paint (oak_mat)
    thin = 0.85 if cfg == 'burnt' else 1.0
    c = 0.025 if lod == 0 else 0.0
    corners = [(-1, -1), (1, -1), (-1, 1), (1, 1)]
    if cfg not in ('culture_a', 'culture_c') and lod < 2:
        for (sx, sy) in corners:
            cx = sx * (CEL_W / 2 + PROUD - POST / 2)
            cy = (Y0 - PROUD + POST / 2) if sy < 0 else (Y1 + PROUD - POST / 2)
            if cfg == 'damaged' and sx > 0 and sy < 0:
                # fallen: leaning about 30 deg against the rubble, foot slid out
                old = mb.push(Matrix.Translation((cx + 0.55, cy - 0.15, Z_FLOOR - 0.05)) @ Matrix.Rotation(math.radians(-32), 4, 'Y') @ Matrix.Rotation(0.25, 4, 'Z'))
                K.cbox(mb, (0, 0, WALL_H / 2), (POST / 2 * thin, POST / 2 * thin, WALL_H / 2), om, c)
                K.cbox(mb, (0, 0, WALL_H + CAP_H / 2), (CAP_W / 2, CAP_W / 2, CAP_H / 2), om, c * 0.6)
                mb.pop(old)
                continue
            hgt = WALL_H if cfg != 'burnt' else WALL_H * 0.9
            K.cbox(mb, (cx, cy, Z_FLOOR + hgt / 2), (POST / 2 * thin, POST / 2 * thin, hgt / 2), om, c)
            if cfg != 'burnt':
                K.cbox(mb, (cx, cy, Z_EAVE + CAP_H / 2), (CAP_W / 2, CAP_W / 2, CAP_H / 2), om, c * 0.6)
    # door posts with base and cap blocks
    for sx in (-1, 1):
        cx = sx * (DOOR_W / 2 + DPOST / 2)
        cy = Y0 - PROUD + DPOST / 2
        if lod == 2:
            continue
        if cfg == 'culture_c':
            cy = DRUM_Y - DRUM_R - 0.02 + DPOST / 2
        K.cbox(mb, (cx, cy, Z_FLOOR + 0.03), (0.14, 0.14, 0.03), om, c * 0.5)
        K.cbox(mb, (cx, cy, Z_FLOOR + (DOOR_H - 0.08) / 2 + 0.03), (DPOST / 2 * thin, DPOST / 2 * thin, (DOOR_H - 0.08) / 2 - 0.03), om, c)
        K.cbox(mb, (cx, cy, Z_FLOOR + DOOR_H - 0.04), (0.14, 0.14, 0.04), om, c * 0.5)
    return {'posts': mb} if mb.F else {}


def lintel(cfg, lod):
    if cfg == 'ruin' or lod == 2:
        return {}
    mb = MB('lintel')
    om = oak_mat(cfg)
    cy = Y0 - PROUD - 0.02 + LIN_D / 2
    if cfg == 'culture_c':
        cy = DRUM_Y - DRUM_R - 0.04 + LIN_D / 2
    zc = Z_FLOOR + DOOR_H + LIN_H / 2
    tilt = math.radians(2.0) if cfg == 'damaged' else 0.0
    old = mb.push(Matrix.Translation((0, cy, zc)) @ Matrix.Rotation(tilt, 4, 'Y'))
    K.cbox(mb, (0, 0, 0), (LIN_L / 2, LIN_D / 2, LIN_H / 2), om, 0.02 if lod == 0 else 0.0, grain='x')
    if lod == 0:
        yf = -LIN_D / 2
        carve = 'oak' if om == 'paint_cobalt' else om
        # two fillets and a raised zigzag band between them (the culture C lintel carries concentric rings)
        for zf in (-0.085, 0.085):
            K.cbox(mb, (0, yf - 0.006, zf), (LIN_L / 2 - 0.12, 0.008, 0.012), carve, 0.004, grain='x')
        if cfg == 'culture_c':
            for cx in (-0.7, 0.0, 0.7):
                for r in (0.06, 0.035):
                    pts = [V(cx + r * math.cos(2 * math.pi * k / 16), yf - 0.006, r * math.sin(2 * math.pi * k / 16)) for k in range(17)]
                    K.tube(mb, pts, 0.007, 4, carve)
        else:
            teeth = 14
            x0, x1 = -LIN_L / 2 + 0.16, LIN_L / 2 - 0.16
            pts = []
            for k in range(teeth * 2 + 1):
                x = x0 + (x1 - x0) * k / (teeth * 2)
                pts.append(V(x, yf - 0.004, 0.055 if k % 2 else -0.055))
            # rectangular raised ribbon along the zigzag
            tile = K.tile_of(carve)
            w, h = 0.014, 0.012
            for k in range(len(pts) - 1):
                a, b = pts[k], pts[k + 1]
                d = (b - a).normalized()
                nrm = V(-d.z, 0, d.x)
                q = [a - nrm * w, b - nrm * w, b + nrm * w, a + nrm * w]
                top = [x + V(0, -h, 0) for x in q]
                mb.face([mb.v(x) for x in top], carve, [(x.x / tile, x.z / tile) for x in top], (0, -1, 0))
                for (p0, p1, wv) in ((q[0], q[1], -nrm), (q[3], q[2], nrm)):
                    f = [p0, p1, p1 + V(0, -h, 0), p0 + V(0, -h, 0)]
                    mb.face([mb.v(x) for x in f], carve, [(x.x / tile, x.y / tile) for x in f], wv)
    mb.pop(old)
    return {'lintel': mb}


def threshold(cfg, lod):
    if lod == 2 or cfg == 'ruin':
        return {}
    mb = MB('threshold')
    cy = Y0 + WALL_T / 2 - 0.05
    if cfg == 'culture_c':
        cy = DRUM_Y - DRUM_R + 0.25
    K.cbox(mb, (0, cy, Z_FLOOR + 0.03), (DOOR_W / 2 + 0.05, WALL_T / 2 + 0.03, 0.03), oak_mat(cfg) if cfg != 'culture_a' else 'oak', 0.01, grain='x')
    return {'threshold': mb}


# ----------------------------------------------------------------------------------------------- roof (thatch)


def roof_top_z(x, y):
    """Undisplaced thatch top surface height at (x, y) inside the eave outline (hip roof)."""
    hx = (EX1 - EX0) / 2
    # distance inward from the eave outline on each slope, scaled by the slope's rise/run
    run_long = (EY1 - EY0) / 2
    run_hip = hx - RIDGE_L / 2
    rise = Z_RP - Z_ET
    zf = Z_ET + rise * min(1.0, (y - EY0) / run_long if y < YR else (EY1 - y) / run_long)
    zh = Z_ET + rise * min(1.0, (hx - abs(x)) / run_hip)
    return min(zf, zh)


def thatch_disp(p, n, cfg):
    d = 0.028 * noise.fractal(Vector((p.x * 1.1, p.y * 1.1, p.z * 1.1)) + Vector((3.3, 1.1, 0.0)), 0.6, 2.0, 3)
    d += 0.012 * noise.noise(Vector((p.x * 3.7, p.y * 3.7, p.z * 3.7)))
    out = n * d
    if cfg == 'damaged':
        # right third slumped 0.4-0.6 m, sliding outward
        f = sstep(0.8, EX1, p.x)
        out += Vector((0.12 * f, 0.0, -0.5 * f * (0.8 + 0.2 * noise.noise(Vector((p.y * 2, 0, 1))))))
    if cfg == 'drowned':
        out += Vector((0, 0, -0.08 * (1 - abs(p.x) / EX1)))     # sagging
    return out


def roof(cfg, lod):
    if cfg in ('burnt', 'ruin'):
        return {}
    if cfg == 'construction':
        return roof_frame(lod)
    if cfg == 'culture_a':
        return roof_dome(lod)
    if cfg == 'culture_b':
        return roof_saddle(lod)
    if cfg == 'culture_c':
        return roof_cone(lod)
    m = 'thatch_sodden' if cfg == 'drowned' else 'thatch'
    mb = MB('roof')
    tile = K.tile_of(m)
    # outline corners (front-left, front-right, back-right, back-left) and ridge ends
    c = [V(EX0, EY0, Z_ET), V(EX1, EY0, Z_ET), V(EX1, EY1, Z_ET), V(EX0, EY1, Z_ET)]
    rl, rr = V(-RIDGE_L / 2, YR, Z_RP), V(RIDGE_L / 2, YR, Z_RP)
    if lod == 2:
        for (a, b, e, f, want) in ((c[0], c[1], rr, rl, (0, -1, 1)), (c[2], c[3], rl, rr, (0, 1, 1))):
            pts = [a, b, e, f]
            mb.face([mb.v(p) for p in pts], m, [(p.x / tile, p.z / tile) for p in pts], want)
        for (a, b, e, want) in ((c[1], c[2], rr, (1, 0, 1)), (c[3], c[0], rl, (-1, 0, 1))):
            mb.face([mb.v(p) for p in (a, b, e)], m, [(p.y / tile, p.z / tile) for p in (a, b, e)], want)
        # thick eave band and soffit
        for i in range(4):
            a, b = c[i], c[(i + 1) % 4]
            pts = [a, b, b - V(0, 0, THATCH_T), a - V(0, 0, THATCH_T)]
            o = (a + b) / 2
            mb.face([mb.v(p) for p in pts], m, [((p.x + p.y) / tile, p.z / tile) for p in pts], (o.x, o.y - YR, 0))
        q = [c[i] - V(0, 0, THATCH_T) for i in range(4)]
        mb.face([mb.v(p) for p in q], m, [(p.x / tile, p.y / tile) for p in q], (0, 0, -1))
        return {'roof': mb}
    sp = 0.15 if lod == 0 else 0.5
    nv = 6 if lod == 0 else 3
    disp_on = lod == 0

    def D(p, n):
        return thatch_disp(p, n, cfg) if disp_on else Vector()

    def slope_grid(e0, e1, r0, r1, want, uaxis):
        nu = max(2, int(round((e1 - e0).length / sp)))
        slope_len = ((e0 + e1) / 2 - (r0 + r1) / 2).length
        ids = {}
        pos = {}
        for j in range(nv + 1):
            b = j / nv
            a0 = e0.lerp(r0, b)
            a1 = e1.lerp(r1, b)
            # subdivide the narrowing row with the same count (collapses at a hip apex)
            for i in range(nu + 1):
                p = a0.lerp(a1, i / nu)
                nrm = V(want).normalized()
                q = p + D(p, nrm)
                ids[(i, j)] = mb.v(q, ('rt', round(p.x, 4), round(p.y, 4), round(p.z, 4)))
                pos[(i, j)] = (p, b * slope_len)
        holes = HOLES.get(cfg, [])
        for j in range(nv):
            for i in range(nu):
                cc = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
                pm = sum((pos[k][0] for k in cc), Vector()) / 4
                if any((pm - Vector(h[0])).length < h[1] * (1 + 0.35 * noise.noise(pm * 3.0)) for h in holes):
                    continue
                uvs = [((pos[k][0][uaxis] * (1 if want[1] <= 0 and want[0] >= 0 else -1)) / tile, pos[k][1] / tile) for k in cc]
                mb.face([ids[k] for k in cc], m, uvs, want)
        return nu

    slope_grid(c[0], c[1], rl, rr, (0, -1, 1.2), 0)
    slope_grid(c[2], c[3], rr, rl, (0, 1, 1.2), 0)
    slope_grid(c[1], c[2], rr, rr, (1, 0, 1.0), 1)
    slope_grid(c[3], c[0], rl, rl, (-1, 0, 1.0), 1)
    # eave lip: rounded roll down to a ragged edge, then the soffit back to the walls
    loop = []
    for (a, b) in ((c[0], c[1]), (c[1], c[2]), (c[2], c[3]), (c[3], c[0])):
        n = max(2, int(round((b - a).length / sp)))
        for i in range(n):
            loop.append(a.lerp(b, i / n))
    ctr = V((EX0 + EX1) / 2, YR, 0)
    prof = [(0.0, 0.0), (0.035, -0.07), (0.05, -0.16), (0.035, -0.24), (0.01, -THATCH_T)] if lod == 0 else [(0.0, 0.0), (0.04, -0.15), (0.0, -THATCH_T)]
    rings = []
    rnd = random.Random(5)
    for k, p in enumerate(loop):
        outv = V(p.x - ctr.x, p.y - ctr.y, 0)
        # outward direction perpendicular to the edge (corners: diagonal)
        ox = 1 if abs(p.x - EX1) < 1e-4 else (-1 if abs(p.x - EX0) < 1e-4 else 0)
        oy = 1 if abs(p.y - EY1) < 1e-4 else (-1 if abs(p.y - EY0) < 1e-4 else 0)
        o = V(ox, oy, 0).normalized() if (ox or oy) else outv.normalized()
        ring = []
        for j, (dout, dz) in enumerate(prof):
            q = p + o * dout + V(0, 0, dz)
            if j == 0:
                q = q + D(p, V(o.x, o.y, 1.0).normalized())
            elif lod == 0:
                q = q + D(p, V(o.x, o.y, 1.0).normalized()) * 0.8
                if j == len(prof) - 1:
                    q += V(0, 0, rnd.uniform(-0.05, 0.035)) + o * rnd.uniform(-0.02, 0.03)
                    if cfg == 'damaged' and p.x > 0.8:
                        q += V(0, 0, rnd.uniform(-0.12, 0.0))
            ring.append(q)
        # soffit point under the eave, at the wall face
        sx = min(max(p.x, X0), X1)
        sy = min(max(p.y, Y0), Y1)
        sp_ = V(sx, sy, Z_EAVE + 0.14) + (D(V(sx, sy, Z_EAVE), V(0, 0, -1)) * 0.3 if lod == 0 else V(0, 0, 0))
        ring.append(sp_)
        rings.append((ring, p))
    for k in range(len(rings)):
        ra, pa = rings[k]
        rb, pb = rings[(k + 1) % len(rings)]
        ids_a = [mb.v(ra[0], ('rt', round(pa.x, 4), round(pa.y, 4), round(pa.z, 4)))] + [mb.v(q, ('lip', k, j)) for j, q in enumerate(ra[1:], 1)]
        kb = (k + 1) % len(rings)
        ids_b = [mb.v(rb[0], ('rt', round(pb.x, 4), round(pb.y, 4), round(pb.z, 4)))] + [mb.v(q, ('lip', kb, j)) for j, q in enumerate(rb[1:], 1)]
        along_a = (pa.x if abs(pa.y - EY0) < 1e-4 or abs(pa.y - EY1) < 1e-4 else pa.y)
        along_b = (pb.x if abs(pb.y - EY0) < 1e-4 or abs(pb.y - EY1) < 1e-4 else pb.y)
        if abs(along_b - along_a) > 1.0:
            along_b = along_a + (pb - pa).length
        for j in range(len(ra) - 1):
            want = (pa + pb) / 2 - ctr
            want = V(want.x, want.y, 0).normalized()
            if j == len(ra) - 2:
                want = V(0, 0, -1)
            q = [ids_a[j], ids_b[j], ids_b[j + 1], ids_a[j + 1]]
            vv = [-0.0, 0.07, 0.16, 0.24, THATCH_T, THATCH_T + 0.4] if lod == 0 else [0.0, 0.15, THATCH_T, THATCH_T + 0.4]
            # the strands run over the lip: v continues "down" the slope
            uvs = [(along_a / tile, -vv[j] / tile), (along_b / tile, -vv[j] / tile), (along_b / tile, -vv[j + 1] / tile), (along_a / tile, -vv[j + 1] / tile)]
            mb.face(q, m, uvs, want)
    parts = {'roof': mb}
    if lod == 0:
        fr = MB('roof_fringe')
        ftile = K.tile_of('fringe')
        nb = len(prof) - 1
        rf = random.Random(17)
        acc = 0.0
        for k in range(len(rings)):
            ra, pa = rings[k]
            rb, pb = rings[(k + 1) % len(rings)]
            a, b = ra[nb], rb[nb]
            seg = (b - a).length
            if seg < 1e-4:
                continue
            oa = V(pa.x - ctr.x, pa.y - ctr.y, 0).normalized()
            ob = V(pb.x - ctr.x, pb.y - ctr.y, 0).normalized()
            ha, hb = 0.15 + rf.uniform(-0.03, 0.04), 0.15 + rf.uniform(-0.03, 0.04)
            q = [a + V(0, 0, 0.04), b + V(0, 0, 0.04), b + ob * 0.045 - V(0, 0, hb), a + oa * 0.045 - V(0, 0, ha)]
            want = (oa + ob) * 0.5
            fr.face([fr.v(x) for x in q], 'fringe', [(acc / ftile, 1.0), ((acc + seg) / ftile, 1.0), ((acc + seg) / ftile, 0.0), (acc / ftile, 0.0)], want)
            acc += seg
        parts['roof_fringe'] = fr
    # ceiling over the cella (closes the interior for GI; seen through holes)
    ce = MB('roof_ceiling')
    pts = [V(X0 + 0.1, Y0 + 0.1, Z_EAVE + 0.06), V(X1 - 0.1, Y0 + 0.1, Z_EAVE + 0.06), V(X1 - 0.1, Y1 - 0.1, Z_EAVE + 0.06), V(X0 + 0.1, Y1 - 0.1, Z_EAVE + 0.06)]
    ce.face([ce.v(p) for p in pts], m, [(p.x / tile, p.y / tile) for p in pts], (0, 0, -1))
    if HOLES.get(cfg) and lod == 0:
        up = [p + V(0, 0, 0.01) for p in pts]
        ce.face([ce.v(p) for p in up], m, [(p.x / tile, p.y / tile) for p in up], (0, 0, 1))
    parts['roof_ceiling'] = ce
    # ridge roll and hip rolls
    rolls = MB('roof_rolls')
    nr = 10 if lod == 0 else 6
    seg = 8 if lod == 0 else 3

    def roll(a, b, r0, r1):
        pts = [a.lerp(b, i / seg) for i in range(seg + 1)]
        pts = [p + D(p, V(0, 0, 1)) for p in pts]
        K.tube(rolls, pts, [r0 + (r1 - r0) * i / seg for i in range(seg + 1)], nr, m, cap0=True, cap1=True)
    roll(rl + V(-0.05, 0, 0), rr + V(0.05, 0, 0), ROLL_R, ROLL_R)
    if lod == 0:
        for (e, r) in ((c[0], rl), (c[3], rl), (c[1], rr), (c[2], rr)):
            roll(r, e + (r - e).normalized() * 0.05, 0.075, 0.05)
    parts['roof_rolls'] = rolls
    # rope bands: closed loops at constant height, sitting on the (displaced) thatch
    if lod == 0:
        ropes = MB('roof_ropes')
        for zb in (Z_ET + 0.32, Z_ET + 0.66, Z_ET + 0.98):
            f = (zb - Z_ET) / (Z_RP - Z_ET)
            ix0 = EX0 + (EX1 - EX0 - RIDGE_L) / 2 * f
            ix1 = EX1 - (EX1 - EX0 - RIDGE_L) / 2 * f
            iy0 = EY0 + (EY1 - EY0) / 2 * f
            iy1 = EY1 - (EY1 - EY0) / 2 * f
            cs = [V(ix0, iy0, zb), V(ix1, iy0, zb), V(ix1, iy1, zb), V(ix0, iy1, zb)]
            pts = []
            for i in range(4):
                a, b = cs[i], cs[(i + 1) % 4]
                n = max(2, int(round((b - a).length / 0.45)))
                for k in range(n):
                    pts.append(a.lerp(b, k / n))
            pts.append(pts[0])
            out = []
            for p in pts:
                nrm = V(0 if abs(p.x) < ix1 - 1e-3 else (1 if p.x > 0 else -1), 0 if (iy0 + 1e-3 < p.y < iy1 - 1e-3) else (1 if p.y > YR else -1), 1.1).normalized()
                q = p + thatch_disp(p, nrm, cfg) + nrm * 0.012
                out.append(q)
            K.tube(ropes, out, 0.024, 4, 'rope')
        # ridge binding
        for i in range(5):
            x = -RIDGE_L / 2 + 0.25 + i * (RIDGE_L - 0.5) / 4
            pts = [V(x, YR + 0.115 * math.cos(a), Z_RP + 0.115 * math.sin(a)) for a in [math.pi * k / 6 for k in range(7)]]
            K.tube(ropes, pts, 0.02, 4, 'rope')
        parts['roof_ropes'] = ropes
    if cfg == 'damaged' and lod == 0:
        # exposed rafters in the tear (follow the slumped slope)
        rf = MB('roof_rafters')
        for x in (1.25, 1.6, 1.95, 2.3):
            a = V(x, EY0 + 0.2, Z_ET - 0.12)
            b = V(x * 0.85, YR, Z_RP - 0.15)
            pts = [a.lerp(b, i / 4) for i in range(5)]
            pts = [p + thatch_disp(p, V(0, 0, 0), cfg) for p in pts]
            K.tube(rf, pts, 0.05, 6, 'oak@dust')
        parts['roof_rafters'] = rf
    return parts


HOLES = {
    # (centre, radius) of torn / rotted patches in the thatch top surface
    'damaged': [((1.8, -1.2, 3.55), 0.42), ((1.55, -0.75, 3.85), 0.28)],
    'drowned': [((-1.4, -1.3, 3.45), 0.22), ((0.9, 1.1, 3.6), 0.2), ((2.1, -0.6, 3.5), 0.18)],
}


def finials(cfg, lod):
    if cfg in ('burnt', 'ruin', 'culture_a', 'culture_b', 'culture_c'):
        return {}
    mb = MB('finials')
    om = oak_mat(cfg) if cfg != 'drowned' else 'oak@wet'
    sides = 10 if lod == 0 else (8 if lod == 1 else 5)
    z0 = Z_RIDGE - 0.04 if cfg != 'construction' else Z_RIDGE - 0.06
    if lod == 2:
        prof = [(0.07, 0.0), (0.06, 0.12), (0.11, 0.22), (0.0, FIN_H + 0.04)]
    else:
        prof = [(0.085, 0.0), (0.085, 0.045), (0.065, 0.06), (0.06, 0.12), (0.075, 0.135), (0.10, 0.17), (0.11, 0.225), (0.10, 0.28),
                (0.07, 0.315), (0.03, 0.335), (0.025, 0.35), (0.0, 0.36)]
        prof = [(r, z + 0.0) for r, z in prof]
    for sx in (-1, 1):
        x = sx * RIDGE_L / 2
        z = z0
        if cfg == 'damaged' and sx > 0:
            z += thatch_disp(V(x, YR, Z_RP), V(0, 0, 0), cfg).z
            x += 0.08
        K.lathe(mb, prof, sides, om, ctr=(x, YR, z), cap_top=False)
    return {'finials': mb}


# ----------------------------------------------------------------------------------------------- cult objects


def antlers(cfg, lod):
    if cfg in ('ruin', 'construction') or lod == 2:
        return {}
    mb = MB('antlers')
    m = 'bone' + {'drowned': '@wet', 'damaged': '@dust'}.get(cfg, '')
    if cfg == 'burnt':
        base = V(0, Y0 - 0.12, Z_EAVE + 0.16)
    elif cfg == 'culture_c':
        base = V(0, DRUM_Y - DRUM_R - 0.25, Z_EAVE + 0.06)
    else:
        base = V(0, EY0 - 0.08, Z_EAVE + 0.06)
    # deer skull: a cranium with a long narrow snout pointing down, eye sockets, nasal ridge
    def cranium(p):
        return Vector((p.x * (1.0 - 0.15 * max(0.0, p.z) / 0.1), p.y, p.z))
    K.blob(mb, base + V(0, 0.01, 0.08), (0.2, 0.16, 0.19), m, seed=3, subdiv=3 if lod == 0 else 1, rough=0.05, scale_fn=cranium)

    def snout(p):
        f = max(0.0, min(1.0, (0.16 - p.z) / 0.32))        # 0 top .. 1 tip
        return Vector((p.x * (1.0 - 0.55 * f), p.y * (1.0 - 0.35 * f), p.z))
    K.blob(mb, base + V(0, -0.015, -0.1), (0.13, 0.11, 0.34), m, seed=4, subdiv=2 if lod == 0 else 1, rough=0.04, scale_fn=snout)
    if lod == 0:
        for sx in (-1, 1):
            K.blob(mb, base + V(sx * 0.072, -0.045, 0.065), (0.055, 0.04, 0.05), 'ember@ash' if cfg != 'burnt' else m, seed=5 + sx, subdiv=1, rough=0.02)
    # mounting peg into the wall top
    if cfg not in ('burnt',):
        K.tube(mb, [base + V(0, 0.05, 0.02), V(0, Y0 + 0.15, Z_EAVE - 0.02)], 0.03, 5, oak_mat(cfg) if cfg != 'culture_a' else 'oak')
    sides = 6 if lod == 0 else 4
    am = 'bone@antler' if cfg in ('intact', 'culture_a', 'culture_b', 'culture_c') else m
    for sx in (-1, 1):
        def P(x, y, z):
            return base + V(sx * x, y, z)
        beam = [P(0.05, 0.01, 0.13), P(0.13, -0.03, 0.22), P(0.24, -0.06, 0.34), P(0.33, -0.07, 0.48), P(0.40, -0.06, 0.62), P(0.44, -0.04, 0.74)]
        K.tube(mb, beam, [0.055, 0.047, 0.04, 0.032, 0.022, 0.01], sides, am, cap0=True)
        if lod == 0:
            tines = [
                [P(0.10, -0.02, 0.18), P(0.12, -0.12, 0.25), P(0.11, -0.19, 0.36)],     # brow tine forward
                [P(0.27, -0.065, 0.39), P(0.24, -0.10, 0.52), P(0.20, -0.11, 0.63)],    # bez / trez tine
                [P(0.37, -0.065, 0.56), P(0.32, -0.07, 0.68), P(0.30, -0.06, 0.80)],    # top tine inward
            ]
            for t in tines:
                K.tube(mb, t, [0.022, 0.016, 0.006], 5, am)
    return {'antlers': mb}


def cloth(cfg, lod):
    if cfg in ('construction', 'burnt', 'ruin', 'drowned') or lod == 2:
        return {}
    mb = MB('cloth')
    m = 'wool'
    rnd = random.Random(21)
    cy = Y0 - PROUD + DPOST / 2
    if cfg == 'culture_c':
        cy = DRUM_Y - DRUM_R - 0.02 + DPOST / 2
    for sx in (-1, 1):
        cx = sx * (DOOR_W / 2 + DPOST / 2)
        zt = Z_FLOOR + DOOR_H - 0.16
        # wrap round the post
        K.cbox(mb, (cx, cy, zt), (DPOST / 2 + 0.02, DPOST / 2 + 0.02, 0.045), m, 0.01)
        # two hanging tails with a frayed end, curving a little forward and outward
        for k, (dx, ln, w) in enumerate(((-0.06, 0.68, 0.085), (0.0, 0.58, 0.075), (0.06, 0.63, 0.08))):
            segs = 6 if lod == 0 else 2
            pts = []
            for j in range(segs + 1):
                f = j / segs
                z = zt - 0.03 - ln * f
                y = cy - DPOST / 2 - 0.035 - 0.06 * f * f - 0.01 * k
                x = cx + dx * (1.0 + 0.6 * f) + sx * 0.035 * f + 0.015 * math.sin(f * 4 + k * 2)
                pts.append(V(x, y, z))
            tile = K.tile_of(m)
            rows = []
            for j, p in enumerate(pts):
                f = j / segs
                wj = w * (1.0 + 0.25 * f)
                tw = 0.5 * math.sin(f * 3.0 + k)                 # twist about the strip axis
                ax = V(math.cos(tw), math.sin(tw) * 0.6, 0) * (wj / 2)
                a = mb.v(p - ax)
                b = mb.v(p + ax)
                a2 = mb.v(p - ax + V(0, 0.006, 0))
                b2 = mb.v(p + ax + V(0, 0.006, 0))
                if j == segs:
                    for vi in (a, b):
                        mb.P[vi].z += rnd.uniform(-0.06, 0.02)
                    mb.P[a2].z = mb.P[a].z
                    mb.P[b2].z = mb.P[b].z
                rows.append((a, b, a2, b2, (p.z - zt) / tile))
            for j in range(segs):
                a, b, a2, b2, va = rows[j]
                c, d, c2, d2, vb = rows[j + 1]
                mb.face([a, b, d, c], m, [(0, va), (w / tile, va), (w / tile, vb), (0, vb)], (0, -1, 0))
                mb.face([a2, b2, d2, c2], m, [(0, va), (w / tile, va), (w / tile, vb), (0, vb)], (0, 1, 0))
    return {'cloth': mb}


def altar(cfg, lod):
    parts = {}
    mb = MB('altar')
    m = {'construction': 'limestone_fresh', 'culture_a': 'plaster'}.get(cfg, 'limestone') + {'drowned': '@wet', 'ruin': '@ruin', 'burnt': ''}.get(cfg, '')
    w, d, h = ALTAR
    cy = -0.65
    if cfg == 'culture_c':
        cy = DRUM_Y - 0.4
    if lod == 2:
        return {}
    K.cbox(mb, (0, cy, Z_FLOOR + h / 2 - 0.05), (w / 2, d / 2, h / 2 + 0.05), m, 0.02, skip=('-z', '+z'))
    # top with a recessed fire bowl
    tile = K.tile_of(m)
    z = Z_FLOOR + h
    ix, iy, dep = w / 2 - 0.13, d / 2 - 0.11, 0.09
    o = [V(-w / 2, cy - d / 2, z), V(w / 2, cy - d / 2, z), V(w / 2, cy + d / 2, z), V(-w / 2, cy + d / 2, z)]
    i_ = [V(-ix, cy - iy, z), V(ix, cy - iy, z), V(ix, cy + iy, z), V(-ix, cy + iy, z)]
    for k in range(4):
        q = [o[k], o[(k + 1) % 4], i_[(k + 1) % 4], i_[k]]
        mb.face([mb.v(p) for p in q], m, [(p.x / tile, p.y / tile) for p in q], (0, 0, 1))
        dn = [i_[k], i_[(k + 1) % 4], i_[(k + 1) % 4] - V(0, 0, dep), i_[k] - V(0, 0, dep)]
        cc = (i_[k] + i_[(k + 1) % 4]) / 2
        mb.face([mb.v(p) for p in dn], m, [((p.x + p.y) / tile, p.z / tile) for p in dn], V(0, cy, 0) - V(cc.x, cc.y, 0))
    fl = [p - V(0, 0, dep) for p in i_]
    mb.face([mb.v(p) for p in fl], m, [(p.x / tile, p.y / tile) for p in fl], (0, 0, 1))
    parts['altar'] = mb
    zb = z - dep
    if cfg in ('intact', 'damaged', 'culture_a', 'culture_b', 'culture_c'):
        em = MB('embers')
        for k in range(6 if lod == 0 else 2):
            r = random.Random(40 + k)
            p = V(r.uniform(-ix + 0.08, ix - 0.08), cy + r.uniform(-iy + 0.07, iy - 0.07), zb + 0.03)
            K.blob(em, p, (r.uniform(0.09, 0.16), r.uniform(0.06, 0.1), r.uniform(0.04, 0.07)), 'ember', seed=50 + k, subdiv=1, rough=0.2, rot=r.uniform(0, 3))
        parts['embers'] = em
        if lod == 0:
            fm = MB('flame')
            for k in range(3):
                a = math.pi * k / 3
                rv = V(math.cos(a), math.sin(a), 0)
                K.card(fm, V(0, cy, zb + 0.04), rv, (0, 0, 1), 0.3, 0.42, 'flame', (0, 0, 1, 1), segs=1, normal=V(-rv.y, rv.x, 0))
                K.card(fm, V(0, cy, zb + 0.04), -rv, (0, 0, 1), 0.3, 0.42, 'flame', (0, 0, 1, 1), segs=1, normal=V(rv.y, -rv.x, 0))
            parts['flame'] = fm
    if cfg == 'burnt':
        ash = MB('ash')
        K.blob(ash, (0, cy, Z_FLOOR + 0.02), (2.6, 1.7, 0.42), 'ember@ash', seed=77, subdiv=2 if lod == 0 else 1, rough=0.25, flat_bottom=Z_FLOOR - 0.02)
        for k in range(7 if lod == 0 else 0):
            r = random.Random(90 + k)
            K.blob(ash, (r.uniform(-1.5, 1.5), r.uniform(-0.9, 0.9) + 0.0, Z_FLOOR + 0.12), (r.uniform(0.15, 0.3), r.uniform(0.1, 0.18), 0.1), 'oak_charred', seed=90 + k, subdiv=1, rough=0.25, rot=r.uniform(0, 3))
        parts['ash'] = ash
    return parts


# ----------------------------------------------------------------------------------------------- state props


def rubble_pile(mb, ctr, radius, height, count, mat, seed, lod, ground=None):
    rnd = random.Random(seed)
    for k in range(count):
        a = rnd.uniform(0, 2 * math.pi)
        r = radius * math.sqrt(rnd.random())
        hh = height * (1 - r / radius) ** 1.2
        s = rnd.uniform(0.14, 0.34)
        p = V(ctr[0] + r * math.cos(a), ctr[1] + r * math.sin(a), ctr[2] + rnd.uniform(0, hh) + s * 0.25)
        if ground:
            p.z = max(p.z, ground(p) + s * 0.25)
        K.blob(mb, p, (s, s * rnd.uniform(0.6, 0.9), s * rnd.uniform(0.45, 0.7)), mat, seed=seed * 100 + k, subdiv=1 if lod == 0 else 0,
               rough=0.22, rot=rnd.uniform(0, 3.1))


def ground_z(p):
    """Support height under p: plinth top on the plinth, ground elsewhere."""
    if abs(p.x) <= PL_W / 2 and abs(p.y) <= PL_D / 2:
        return PL_H
    return 0.0


def props(cfg, lod):
    parts = {}
    if lod == 2:
        if cfg == 'ruin':
            # the birch is the map-scale "ruin" signal
            mb = MB('birch')
            K.lathe(mb, [(0.12, Z_FLOOR), (0.05, Z_FLOOR + 2.5), (0.0, Z_FLOOR + 3.0)], 4, 'birch_bark', ctr=(0.2, -0.1, 0), cap_top=False)
            K.blob(mb, (0.2, -0.1, Z_FLOOR + 3.2), (2.2, 2.0, 2.6), 'turf', seed=3, subdiv=0, rough=0.2)
            parts['birch'] = mb
        return parts
    if cfg == 'construction':
        parts.update(scaffold(lod))
    if cfg == 'damaged':
        mb = MB('rubble_pile')
        rubble_pile(mb, (2.3, -1.45, PL_H - 0.05), 0.95, 1.1, 40 if lod == 0 else 14, 'rubble@dust', 5, lod, ground_z)
        rubble_pile(mb, (3.3, -1.7, 0.0), 0.7, 0.5, 14 if lod == 0 else 5, 'rubble@dust', 6, lod, ground_z)
        parts['rubble_pile'] = mb
    if cfg == 'burnt':
        mb = MB('charred_beams')
        for k, (a, b) in enumerate(((V(-2.3, -1.2, 2.86), V(1.6, 1.3, 2.95)), (V(-1.9, 1.35, 2.88), V(2.2, -0.9, 2.5)),
                                    (V(0.4, -1.55, 2.84), V(1.4, 1.3, 1.6)), (V(-2.2, 0.2, 2.86), V(-0.6, -1.0, 1.2)))):
            K.tube(mb, [a, a.lerp(b, 0.5) + V(0, 0, 0.04), b], [0.06, 0.055, 0.05], 6 if lod == 0 else 4, 'oak_charred', cap0=True, cap1=True, jitter=0.15, seed=k)
        parts['charred_beams'] = mb
    if cfg == 'ruin':
        mb = MB('loose_stones')
        rubble_pile(mb, (0.3, -0.2, PL_H), 1.6, 0.25, 12 if lod == 0 else 5, 'rubble@ruin', 9, lod, ground_z)
        rnd = random.Random(12)
        for k in range(9 if lod == 0 else 4):
            a = rnd.uniform(0, 2 * math.pi)
            p = V(3.4 * math.cos(a) * rnd.uniform(0.95, 1.2), 2.7 * math.sin(a) * rnd.uniform(0.95, 1.2), 0.05)
            s = rnd.uniform(0.18, 0.35)
            K.blob(mb, p, (s, s * 0.8, s * 0.55), 'rubble@ruin', seed=200 + k, subdiv=1, rough=0.2, rot=rnd.uniform(0, 3))
        parts['loose_stones'] = mb
        parts.update(turf_cap(lod))
        parts.update(birch(lod))
        parts.update(plants(lod, 'ruin'))
    if cfg == 'drowned':
        parts.update(plants(lod, 'drowned'))
    return parts


def scaffold(lod):
    sc = MB('scaffold')
    m = 'oak_fresh'
    s6 = 6 if lod == 0 else 4
    xs = (PL_W / 2 - 0.25, PL_W / 2 + 0.45)
    ys = (Y0 - 0.2, (Y0 + Y1) / 2, Y1 + 0.2)
    for xi, x in enumerate(xs):
        z0 = PL_H if xi == 0 else -0.1
        for y in ys:
            K.tube(sc, [V(x, y, z0), V(x + 0.03 * (xi * 2 - 1), y, 3.6)], [0.045, 0.038], s6, m)
    for z in (1.7, 2.65):
        for x in xs:
            K.tube(sc, [V(x, ys[0] - 0.15, z), V(x, ys[-1] + 0.15, z)], 0.035, s6, m)
        for y in ys:
            K.tube(sc, [V(xs[0] - 0.1, y, z + 0.06), V(xs[1] + 0.1, y, z + 0.06)], 0.03, s6, m)
        if lod == 0:
            for k in range(4):
                K.cbox(sc, (xs[0] + 0.15 + k * 0.17, (ys[0] + ys[-1]) / 2, z + 0.12), (0.075, (ys[-1] - ys[0]) / 2, 0.02), m, 0.005, grain='y')
    # lashings at the lift joints
    if lod == 0:
        for x in xs:
            for y in ys:
                for z in (1.7, 2.65):
                    K.tube(sc, [V(x, y, z - 0.05), V(x, y, z + 0.08)], 0.052, 6, 'rope')
    # ladder leaning on the right wall near the front
    la = MB('ladder')
    foot = V(PL_W / 2 + 0.95, Y0 - 0.55, 0.0)
    top = V(X1 + 0.35, Y0 - 0.55, 3.0)
    d = (top - foot).normalized()
    side = V(0, 1, 0)
    for s in (-0.22, 0.22):
        K.tube(la, [foot + side * s, top + side * s + d * 0.2], 0.035, s6, m)
    for k in range(9 if lod == 0 else 4):
        f = (k + 0.7) / (9 if lod == 0 else 4)
        p = foot.lerp(top, f)
        K.tube(la, [p - side * 0.24, p + side * 0.24], 0.022, 5 if lod == 0 else 3, m)
    # reed bundles and a basket of stones on the plinth
    rb = MB('reed_bundles')
    for k, (x, y, a) in enumerate(((-2.2, -2.0, 0.1), (-1.85, -1.95, -0.05), (-2.05, -1.98, 0.02))):
        z = PL_H + 0.15 + (0.24 if k == 2 else 0.0)
        if k == 2:
            x = -2.02
        d = V(math.cos(a), math.sin(a), 0) * 0.6
        K.tube(rb, [V(x, y, z) - d + V(0.0, 0.0, 0.0), V(x, y, z) + d], [0.13, 0.15], 10 if lod == 0 else 5, 'thatch', cap0=True, cap1=True)
        if lod == 0:
            for t in (-0.3, 0.3):
                p = V(x, y, z) + d * (t / 0.6)
                K.tube(rb, [p - d.normalized() * 0.03, p + d.normalized() * 0.03], 0.155, 8, 'rope')
    bk = MB('basket')
    K.lathe(bk, [(0.16, PL_H), (0.24, PL_H + 0.06), (0.27, PL_H + 0.3), (0.255, PL_H + 0.32), (0.22, PL_H + 0.3)], 12 if lod == 0 else 6, 'reed_panel',
            ctr=(0.9, -1.95, 0), cap_top=False)
    rubble_pile(bk, (0.9, -1.95, PL_H + 0.25), 0.18, 0.08, 6 if lod == 0 else 2, 'rubble', 31, lod)
    # loose wall stones waiting by the walls
    rubble_pile(bk, (-1.6, Y1 + 0.42, PL_H), 0.35, 0.18, 8 if lod == 0 else 3, 'rubble', 32, lod)
    return {'scaffold': sc, 'ladder': la, 'reed_bundles': rb, 'basket': bk}


def roof_frame(lod):
    fr = MB('roof_frame')
    m = 'oak_fresh'
    s6 = 6 if lod == 0 else 4
    r = 0.05
    zt = Z_EAVE + 0.08
    # eave plates on the post caps
    cs = [V(X0 + 0.08, Y0 - 0.0, zt), V(X1 - 0.08, Y0, zt), V(X1 - 0.08, Y1, zt), V(X0 + 0.08, Y1, zt)]
    for i in range(4):
        a, b = cs[i], cs[(i + 1) % 4]
        d = (b - a).normalized()
        K.tube(fr, [a - d * 0.35, b + d * 0.35], 0.07, s6, m)
    rl, rr = V(-RIDGE_L / 2, YR, Z_RIDGE - 0.1), V(RIDGE_L / 2, YR, Z_RIDGE - 0.1)
    K.tube(fr, [rl - V(0.15, 0, 0), rr + V(0.15, 0, 0)], 0.06, s6, m)
    ec = [V(EX0 + 0.05, EY0 + 0.05, zt - 0.05), V(EX1 - 0.05, EY0 + 0.05, zt - 0.05), V(EX1 - 0.05, EY1 - 0.05, zt - 0.05), V(EX0 + 0.05, EY1 - 0.05, zt - 0.05)]
    for e, rp in ((ec[0], rl), (ec[3], rl), (ec[1], rr), (ec[2], rr)):
        K.tube(fr, [e, rp], r, s6, m)
    # common rafters: 9 per long slope, 5 per hip end; jacks stop on the hip rafters
    def onto_hip(x_eave, side):
        # rafter from the eave at x to the ridge line (or to the hip rafter when beyond the ridge)
        ye = EY0 + 0.05 if side < 0 else EY1 - 0.05
        if abs(x_eave) <= RIDGE_L / 2:
            return V(x_eave, ye, zt - 0.05), V(x_eave, YR, Z_RIDGE - 0.1)
        hx = (EX1 - EX0) / 2 - RIDGE_L / 2
        f = (abs(x_eave) - RIDGE_L / 2) / hx
        yh = YR + (ye - YR) * f
        zh = Z_RIDGE - 0.1 + (zt - 0.05 - (Z_RIDGE - 0.1)) * f
        return V(x_eave, ye, zt - 0.05), V(x_eave, yh, zh)
    n_long = 9 if lod == 0 else 5
    for side in (-1, 1):
        for k in range(n_long):
            x = EX0 + 0.35 + (EX1 - EX0 - 0.7) * k / (n_long - 1)
            a, b = onto_hip(x, side)
            K.tube(fr, [a, b], 0.045, s6, m)
    n_short = 5 if lod == 0 else 3
    for sx in (-1, 1):
        for k in range(n_short):
            y = EY0 + 0.45 + (EY1 - EY0 - 0.9) * k / (n_short - 1)
            xe = EX0 + 0.05 if sx < 0 else EX1 - 0.05
            hx = (EX1 - EX0) / 2 - RIDGE_L / 2
            f = abs(y - YR) / ((EY1 - EY0) / 2)
            xh = sx * (RIDGE_L / 2 + hx * f)
            zh = Z_RIDGE - 0.1 + (zt - 0.05 - (Z_RIDGE - 0.1)) * f
            K.tube(fr, [V(xe, y, zt - 0.05), V(xh, y, zh)], 0.045, s6, m)
    # three rows of purlins (rings at constant height)
    for k in (0.28, 0.55, 0.8):
        z = zt + (Z_RIDGE - 0.1 - zt) * k + 0.06
        ix0 = EX0 + ((EX1 - EX0) / 2 - RIDGE_L / 2) * k
        ix1 = EX1 - ((EX1 - EX0) / 2 - RIDGE_L / 2) * k
        iy0 = EY0 + (EY1 - EY0) / 2 * k
        iy1 = EY1 - (EY1 - EY0) / 2 * k
        cs2 = [V(ix0, iy0, z), V(ix1, iy0, z), V(ix1, iy1, z), V(ix0, iy1, z)]
        for i in range(4):
            a, b = cs2[i], cs2[(i + 1) % 4]
            K.tube(fr, [a, b], 0.035, 5 if lod == 0 else 3, m)
        if lod == 0:
            for p in cs2:
                K.tube(fr, [p - V(0, 0, 0.05), p + V(0, 0, 0.06)], 0.055, 6, 'rope')
    return {'roof_frame': fr}


def roof_dome(lod):
    """Culture A: flat roof slab, a cobalt band, a square drum and a low plaster dome with a cobalt finial."""
    mb = MB('roof')
    m = 'plaster'
    sp = 0.5
    K.cbox(mb, (0, (Y0 + Y1) / 2, Z_EAVE + 0.1), (CEL_W / 2 + 0.12, CEL_D / 2 + 0.12, 0.1), m, 0.03 if lod == 0 else 0.0)
    if lod < 2:
        K.cbox(mb, (0, (Y0 + Y1) / 2, Z_EAVE - 0.08), (CEL_W / 2 + 0.03, CEL_D / 2 + 0.03, 0.08), 'paint_cobalt', 0.01, grain='x')
    dc = V(0, (Y0 + Y1) / 2, Z_EAVE + 0.2)
    K.cbox(mb, (dc.x, dc.y, dc.z + 0.2), (1.55, 1.45, 0.2), m, 0.03 if lod == 0 else 0.0)
    R = 1.42
    rings = 9 if lod == 0 else (4 if lod == 1 else 3)
    sides = 32 if lod == 0 else (14 if lod == 1 else 8)
    prof = [(R * math.cos(0.5 * math.pi * k / rings), dc.z + 0.4 + 1.05 * math.sin(0.5 * math.pi * k / rings)) for k in range(rings + 1)]
    old = mb.push(Matrix.Translation((dc.x, dc.y, 0)))
    K.lathe(mb, prof, sides, m, cap_top=False)
    fin = [(0.09, dc.z + 1.43), (0.1, dc.z + 1.5), (0.07, dc.z + 1.56), (0.08, dc.z + 1.64), (0.0, dc.z + 1.86)]
    K.lathe(mb, fin, 10 if lod == 0 else 5, 'paint_cobalt', cap_top=False)
    mb.pop(old)
    return {'roof': mb}


def roof_saddle(lod):
    """Culture B: upswept saddle roof of silver shingle along X, horns at both ends, saffron ribbons."""
    mb = MB('roof')
    m = 'shingle'
    tile = K.tile_of(m)
    S = 3.6                          # half length incl. the swept ends
    D = (CEL_D / 2 + 0.45)          # half depth to the eave
    yc = (Y0 + Y1) / 2
    ns = 28 if lod == 0 else (10 if lod == 1 else 6)
    nt = 6 if lod == 0 else (3 if lod == 1 else 1)

    def zr(s):
        return 4.15 + 1.2 * (abs(s) / S) ** 2.6

    def ze(s):
        return Z_EAVE + 0.05 + 0.55 * (abs(s) / S) ** 3.0

    def surf(s, t):
        f = abs(t) ** 0.85
        z = zr(s) + (ze(s) - zr(s)) * f
        return V(s * (1.0 - 0.06 * abs(t)), yc + t * D, z)
    for sgn in (-1, 1):
        ids = {}
        for i in range(ns + 1):
            s = -S + 2 * S * i / ns
            for j in range(nt + 1):
                t = sgn * j / nt
                ids[(i, j)] = mb.v(surf(s, t), ('sd', i, j * sgn))
        for i in range(ns):
            for j in range(nt):
                cc = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
                uvs = []
                for (a, b) in cc:
                    s = -S + 2 * S * a / ns
                    uvs.append((s * sgn / tile, (1 - b / nt) * D * 1.15 / tile))
                mb.face([ids[k] for k in cc], m, uvs, (0, sgn, 1))
        # underside (oak boarding) so the roof has thickness from below
        idu = {}
        for i in range(ns + 1):
            s = -S + 2 * S * i / ns
            for j in range(nt + 1):
                t = sgn * j / nt
                idu[(i, j)] = mb.v(surf(s, t) - V(0, 0, 0.14), ('su', i, j * sgn))
        for i in range(ns):
            for j in range(nt):
                cc = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
                mb.face([idu[k] for k in cc], 'oak', [((-S + 2 * S * a / ns) / 0.7, b / nt) for (a, b) in cc], (0, 0, -1))
        # eave edge band
        for i in range(ns):
            a, b = ids[(i, nt)], ids[(i + 1, nt)]
            c, d = idu[(i + 1, nt)], idu[(i, nt)]
            mb.face([a, b, c, d], m, [(i / ns * 2 * S / tile, 0), ((i + 1) / ns * 2 * S / tile, 0), ((i + 1) / ns * 2 * S / tile, 0.14 / tile), (i / ns * 2 * S / tile, 0.14 / tile)], (0, sgn, 0))
        for i in (0, ns):
            for j in range(nt):
                q = [ids[(i, j)], ids[(i, j + 1)], idu[(i, j + 1)], idu[(i, j)]]
                mb.face(q, m, [(0, 0), (D / nt / tile, 0), (D / nt / tile, 0.14 / tile), (0, 0.14 / tile)], (1 if i else -1, 0, 0))
    parts = {'roof': mb}
    if lod < 2:
        # gable infill (reed) under the saddle at both ends
        gb = MB('gables')
        for sx in (-1, 1):
            x = sx * (CEL_W / 2 - 0.06)
            s = abs(x)
            top = zr(s) - 0.15
            pts = [V(x, Y0 + 0.06, Z_EAVE), V(x, Y1 - 0.06, Z_EAVE), V(x, yc, top)]
            gb.face([gb.v(p) for p in pts], 'reed_panel', [(p.y, p.z) for p in pts], (sx, 0, 0))
        parts['gables'] = gb
    # horns: ridge ends continue into curved spikes; ribbons hang from them
    hn = MB('horns')
    for sx in (-1, 1):
        pts = [V(sx * (S - 0.4), yc, zr(S - 0.4) - 0.05), V(sx * S, yc, zr(S)), V(sx * (S + 0.25), yc, zr(S) + 0.18), V(sx * (S + 0.4), yc, zr(S) + 0.3)]
        K.tube(hn, pts, [0.11, 0.09, 0.05, 0.01], 8 if lod == 0 else 4, 'oak')
    parts['horns'] = hn
    if lod == 0:
        rb = MB('ribbons')
        for sx in (-1, 1):
            for k, dy in enumerate((-0.08, 0.08)):
                top = V(sx * (S - 0.05), yc + dy, zr(S) - 0.08)
                K.card(rb, top - V(0, 0, 1.1), V(0, 1, 0), V(0, 0, 1), 0.09, 1.1, 'wool', (0, 0, 0.09 / 0.5, 1.1 / 0.5), segs=4, normal=V(sx, 0, 0))
                K.card(rb, top - V(0, 0, 1.1) + V(sx * 0.005, 0, 0), V(0, -1, 0), V(0, 0, 1), 0.09, 1.1, 'wool', (0, 0, 0.09 / 0.5, 1.1 / 0.5), segs=4, normal=V(-sx, 0, 0))
        parts['ribbons'] = rb
    return parts


def roof_cone(lod):
    """Culture C: slate cone (40 deg; see the doc: 55 deg would break the +-0.5 m height rule) and a stone ring finial."""
    mb = MB('roof')
    m = 'slate'
    R = DRUM_R + 0.3
    H = 1.95
    sides = 40 if lod == 0 else (16 if lod == 1 else 8)
    rings = 6 if lod == 0 else (2 if lod == 1 else 1)
    zb = Z_EAVE + 0.05
    prof = [(R * (1 - k / rings), zb + H * k / rings) for k in range(rings + 1)]
    prof[-1] = (0.06, zb + H - 0.06)
    old = mb.push(Matrix.Translation((0, DRUM_Y, 0)))
    K.lathe(mb, prof, sides, m, cap_top=True)
    K.lathe(mb, [(R, zb), (R, zb - 0.12), (DRUM_R - 0.05, zb - 0.1)], sides, m, cap_top=False)
    if lod < 2:
        ring = [V(0.11 * math.cos(2 * math.pi * k / 14), 0, zb + H + 0.12 + 0.11 * math.sin(2 * math.pi * k / 14)) for k in range(15)]
        K.tube(mb, ring, 0.035, 6 if lod == 0 else 4, 'dressed_stone')
        K.lathe(mb, [(0.07, zb + H - 0.1), (0.06, zb + H + 0.02)], 8, 'dressed_stone', cap_top=True)
    else:
        K.lathe(mb, [(0.06, zb + H - 0.1), (0.09, zb + H + 0.12), (0.0, zb + H + 0.25)], 5, 'dressed_stone', cap_top=False)
    mb.pop(old)
    return {'roof': mb}


def turf_cap(lod):
    """Ruin: soil and grass over the plinth top, a low irregular mound; grass also on the steps."""
    mb = MB('turf')
    m = 'turf'
    tile = K.tile_of(m)
    nx, ny = (24, 18) if lod == 0 else (8, 6)
    hx, hy = PL_W / 2 - 0.02, PL_D / 2 - 0.02
    ids = {}
    for j in range(ny + 1):
        for i in range(nx + 1):
            x = -hx + 2 * hx * i / nx
            y = -hy + 2 * hy * j / ny
            edge = min(hx - abs(x), hy - abs(y))
            h = 0.06 + 0.06 * noise.fractal(Vector((x * 0.9, y * 0.9, 2.0)), 0.6, 2.0, 3)
            h *= sstep(0.0, 0.35, edge)
            z = PL_H + h - 0.035 * (1 - sstep(0.0, 0.25, edge))
            ids[(i, j)] = mb.v(V(x, y, z))
    for j in range(ny):
        for i in range(nx):
            cc = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
            pts = [mb.P[ids[k]] for k in cc]
            mb.face([ids[k] for k in cc], m, [(p.x / tile, p.y / tile) for p in pts], (0, 0, 1))
    return {'turf': mb}


def birch(lod):
    mb = MB('birch')
    rnd = random.Random(8)
    base = V(0.25, -0.05, PL_H + 0.05)
    trunk = [base + V(0.06 * math.sin(k * 0.9), 0.04 * math.cos(k * 1.3), 4.3 * k / 8) for k in range(9)]
    K.tube(mb, trunk, [0.11, 0.095, 0.085, 0.075, 0.065, 0.05, 0.04, 0.028, 0.012], 8 if lod == 0 else 5, 'birch_bark', cap1=True)
    leaves = MB('birch_leaves')
    tips = []
    nb = 9 if lod == 0 else 5
    for k in range(nb):
        z0 = 1.6 + 2.4 * k / nb
        a = rnd.uniform(0, 2 * math.pi) + k * 2.4
        L = 1.1 * (1 - 0.5 * k / nb) + rnd.uniform(0, 0.3)
        p0 = base + V(0, 0, z0)
        d = V(math.cos(a), math.sin(a), 0.9).normalized()
        pts = [p0, p0 + d * L * 0.5 + V(0, 0, 0.1), p0 + d * L + V(0, 0, 0.05)]
        K.tube(mb, pts, [0.03, 0.02, 0.006], 5 if lod == 0 else 3, 'birch_bark')
        tips.extend(pts[1:])
    tips.append(base + V(0, 0, 4.3))
    ncard = 7 if lod == 0 else 3
    for p in tips:
        for k in range(ncard):
            r = rnd.uniform(0.15, 0.6)
            a = rnd.uniform(0, 2 * math.pi)
            q = p + V(r * math.cos(a), r * math.sin(a), rnd.uniform(-0.45, 0.35))
            right = V(math.cos(a + 1.3), math.sin(a + 1.3), rnd.uniform(-0.3, 0.3))
            up = V(rnd.uniform(-0.3, 0.3), rnd.uniform(-0.3, 0.3), 1.0)
            K.card(leaves, q - up.normalized() * 0.35, right, up, 0.75, 0.75, 'foliage', (0.0, 0.5, 0.5, 1.0), segs=1)
    return {'birch': mb, 'birch_leaves': leaves}


def plants(lod, cfg):
    mb = MB('plants')
    rnd = random.Random(44 if cfg == 'ruin' else 45)
    if cfg == 'ruin':
        spots = []
        for k in range(26 if lod == 0 else 8):
            spots.append(V(rnd.uniform(-2.0, 2.0), rnd.uniform(-1.4, 1.2), PL_H + 0.08))
        for k in range(8 if lod == 0 else 3):     # along the plinth foot and in the step risers
            spots.append(V(rnd.uniform(-3.0, 3.0), -2.25 - rnd.uniform(0, 0.8), 0.0))
        for s in spots:
            fern = rnd.random() < 0.3
            uvr = (0.5, 0.0, 1.0, 0.5) if fern else (0.0, 0.0, 0.5, 0.5)
            h = rnd.uniform(0.35, 0.6) if not fern else rnd.uniform(0.5, 0.8)
            for k in range(2):
                a = rnd.uniform(0, math.pi) + k * math.pi / 2
                K.card(mb, s, V(math.cos(a), math.sin(a), 0), V(0, 0, 1), h * 1.1, h, 'foliage', uvr, segs=2, bend=0.08)
    else:
        # drowned: weed strands hanging from the lintel and the antlers, floating weed and reeds on the water
        for k in range(7 if lod == 0 else 3):
            x = -1.0 + 2.0 * k / 6
            top = V(x, Y0 - PROUD - 0.04 - 0.005, Z_FLOOR + DOOR_H + 0.02)
            K.card(mb, top - V(0, 0, 0.5), V(1, 0, 0), V(0, 0, 1), 0.22, 0.5, 'foliage', (0.5, 0.5, 1.0, 1.0), segs=2)
        for k in range(12 if lod == 0 else 4):
            p = V(rnd.uniform(-4.5, 4.5), rnd.uniform(-4.2, 3.5), WATER_Z + 0.01)
            if abs(p.x) < PL_W / 2 - 0.3 and abs(p.y) < PL_D / 2 - 0.3:
                continue
            a = rnd.uniform(0, math.pi)
            s = rnd.uniform(0.25, 0.6)
            K.card(mb, p - V(0, s / 2, 0), V(math.cos(a), math.sin(a), 0), V(-math.sin(a), math.cos(a), 0), s, s, 'foliage', (0.5, 0.5, 1.0, 1.0), segs=1, normal=V(0, 0, 1))
        for k in range(5 if lod == 0 else 2):
            p = V(rnd.uniform(-4.0, 4.0), rnd.uniform(-4.0, -2.6), WATER_Z + 0.02)
            a = rnd.uniform(0, math.pi)
            d = V(math.cos(a), math.sin(a), 0) * rnd.uniform(0.3, 0.6)
            K.tube(mb, [p - d, p + d], 0.03, 5, 'thatch_sodden')
    return {'plants': mb}


# ----------------------------------------------------------------------------------------------- assembly


def build(cfg, lod):
    parts = {}
    for fn in (plinth, steps, standing_stones, walls, posts, lintel, threshold, roof, finials, antlers, cloth, altar, props):
        parts.update(fn(cfg, lod))
    return parts


def collision():
    """UCX hulls (convex): plinth + steps, walls, roof (reference sheet: 3 boxes)."""
    hx, hy = PL_W / 2, PL_D / 2
    yf = -hy - NSTEP * TREAD
    plinth = [V(-hx, -hy, 0), V(hx, -hy, 0), V(hx, hy, 0), V(-hx, hy, 0), V(-hx, -hy, PL_H), V(hx, -hy, PL_H), V(hx, hy, PL_H), V(-hx, hy, PL_H),
              V(-STEP_W / 2, yf, 0), V(STEP_W / 2, yf, 0)]
    walls_ = [V(x, y, z) for x in (X0, X1) for y in (Y0, Y1) for z in (PL_H, Z_EAVE)]
    roof_ = [V(EX0, EY0, Z_EB), V(EX1, EY0, Z_EB), V(EX1, EY1, Z_EB), V(EX0, EY1, Z_EB), V(EX0, EY0, Z_ET), V(EX1, EY0, Z_ET), V(EX1, EY1, Z_ET),
             V(EX0, EY1, Z_ET), V(-RIDGE_L / 2, YR, Z_RIDGE), V(RIDGE_L / 2, YR, Z_RIDGE)]
    return [plinth, walls_, roof_]


# ----------------------------------------------------------------------------------------------- masks (G stain, B growth)


def mask(cfg):
    """Returns f(p, n, part, mat) -> (stain, growth). p, n in the asset frame (front -Y)."""
    def door_soot(p):
        # soot fans above the door and the window, and the top 0.4 m all round (burnt)
        f = 0.0
        if p.z > Z_FLOOR + DOOR_H - 0.2 and abs(p.x) < 1.2 + (p.z - Z_FLOOR - DOOR_H) * 0.8 and p.y < Y0 + 0.2:
            f = max(f, 1.0 - abs(p.x) / (1.2 + (p.z - Z_FLOOR - DOOR_H) * 0.8))
        if p.x > X1 - 0.2 and abs(p.y + 0.1) < 0.4 + (p.z - Z_FLOOR - WIN_SILL) * 0.6 and p.z > Z_FLOOR + WIN_SILL + 0.2:
            f = max(f, 0.8)
        f = max(f, sstep(Z_EAVE - 0.45, Z_EAVE - 0.05, p.z) * (1.0 if Z_FLOOR < p.z else 0.0))
        return f

    def nz(p, s=1.0, o=0.0):
        return noise.fractal(Vector((p.x * s, p.y * s, p.z * s)) + Vector((o, o * 0.3, o * 0.7)), 0.6, 2.0, 3)

    def f(p, n, part, mat):
        st, gr = 0.0, 0.0
        base = mat.split('@')[0]
        if cfg == 'intact' or cfg.startswith('culture'):
            # light weathering: rain-splash darkening at the plinth foot, moss in the shade at the base
            gr = 0.35 * sstep(0.35, 0.0, p.z) * max(0.0, 0.5 + nz(p, 1.5, 3))
            st = 0.32 * sstep(0.3, 0.0, p.z) * (0.7 + 0.3 * nz(p, 2.0, 5))
            if part == 'walls':
                st = max(st, 0.22 * sstep(Z_FLOOR + 0.35, Z_FLOOR, p.z))
                gr = max(gr, 0.25 * sstep(Z_FLOOR + 0.25, Z_FLOOR, p.z))
            if base == 'thatch':
                gr = 0.35 * max(0.0, nz(p, 0.8, 7) + 0.2) * (1.0 if p.y > YR else 0.4)     # mossier north (back) slope
        elif cfg == 'construction':
            st = 0.0
        elif cfg == 'damaged':
            # dust (style "dust": growth channel = pale dust) and cracks (stain)
            gr = 0.3 + 0.5 * sstep(1.5, 2.6, p.x) * max(0.0, n.z)
            crack = 0.0
            if part in ('plinth', 'steps'):
                xc = 0.15 + 0.08 * math.sin(p.z * 9.0) + 0.06 * nz(p, 4.0, 2)
                crack = sstep(0.05, 0.0, abs(p.x - xc)) * (1.0 if p.y < -PL_D / 2 + 0.05 else (sstep(-1.6, -2.15, p.y)))
            if part == 'walls':
                for (a, b) in (((-2.3, 1.2), (-1.2, 2.7)), ((0.9, 1.0), (1.9, 2.5)), ((-1.0, 2.6), (-0.3, 1.5))):
                    t_ = (p.x - a[0]) / (b[0] - a[0])
                    if 0 <= t_ <= 1:
                        zc = a[1] + (b[1] - a[1]) * t_ + 0.06 * nz(p, 6.0, 5)
                        crack = max(crack, sstep(0.04, 0.0, abs(p.z - zc)))
            st = crack
        elif cfg == 'burnt':
            soot = 0.0
            if base in ('rubble', 'limestone', 'bone'):
                soot = door_soot(p) if part == 'walls' or base == 'bone' else 0.0
                if part in ('plinth', 'steps') and p.z > PL_H - 0.12 and abs(p.x) < 2.2 and p.y < -1.0:
                    soot = 0.6 * sstep(2.2, 0.5, abs(p.x))
                if part in ('steps',):
                    soot = max(soot, 0.35 * sstep(1.2, 0.0, abs(p.x)))
                if part == 'walls' and (abs(p.x) < X1 - WALL_T - 0.02 and Y0 + WALL_T - 0.02 < p.y < Y1 - WALL_T + 0.02):
                    soot = max(soot, 0.7)      # interior faces blackened
                if base == 'bone':
                    soot = 0.9
                soot *= 0.75 + 0.25 * nz(p, 3.0, 1)
            st = max(0.0, min(1.0, soot))
        elif cfg == 'ruin':
            # moss and grass in every joint and on upward faces, lichen on the stones (growth); weathering darkening
            up = max(0.0, n.z)
            gr = 0.25 + 0.6 * up + 0.3 * sstep(0.4, 0.0, p.z) + 0.25 * nz(p, 2.0, 4)
            if part.startswith('standing_stone'):
                gr = 0.45 + 0.35 * nz(p, 2.5, 8) + 0.2 * (1.0 if n.y > 0.2 else 0.0)
            if part == 'walls' and n.y > 0.3:
                gr += 0.25                                    # north faces
            gr = max(0.0, min(1.0, gr))
            st = 0.2 * max(0.0, nz(p, 1.0, 6))
        elif cfg == 'drowned':
            # stain = wetness (below the waterline everything, above it a fading wet band), growth = weed / algae band
            st = 1.0 if p.z < WATER_Z + 0.02 else sstep(WATER_Z + 0.6, WATER_Z, p.z) * 0.8
            if base in ('oak', 'bone'):
                st = max(st, 0.75)
            if base.startswith('thatch'):
                st = 0.6 + 0.4 * max(0.0, nz(p, 1.5, 2))
            band = sstep(WATER_Z - 0.4, WATER_Z - 0.05, p.z) * sstep(WATER_Z + 0.35, WATER_Z + 0.2, p.z)
            streak = sstep(WATER_Z + 0.8, WATER_Z + 0.3, p.z) * max(0.0, nz(Vector((p.x * 6, p.y * 6, 0.0)), 1.0, 1) + 0.1)
            under = 0.55 if p.z < WATER_Z - 0.05 else 0.0
            gr = max(band, streak * 0.8, under * (0.6 + 0.4 * max(0.0, nz(p, 2.0, 9))))
        return (max(0.0, min(1.0, st)), max(0.0, min(1.0, gr)))
    return f


def has_fire(cfg):
    return cfg in ('intact', 'damaged', 'culture_a', 'culture_b', 'culture_c')
