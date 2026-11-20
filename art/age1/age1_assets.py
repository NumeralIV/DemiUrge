# DEMIURGE art - Age1 (Bronze Age) asset generators.
# Every generator is gen(variant, state, lod) -> (MB, info). Coordinates: metres, Z up, front (door / street /
# outside) toward -Y, origin at the centre of the footprint at ground level. The footprint is the same for
# every state of an asset; only what stands on it changes.
import math
import random
from mathutils import Vector, Matrix

from age1_geo import (MB, S, R, D, FOOT, TILE, prism, box, rect, ellipse, loft, lathe, tube, beam, slab, wall,
                      wall_cols, rubble, face, fbm3, vnoise)

STATES = ('Intact', 'Construction', 'Damaged', 'Ruin')
TAN = math.tan


def _seed(*parts):
    h = 2166136261
    for p in parts:
        for ch in str(p):
            h = ((h ^ ord(ch)) * 16777619) & 0xFFFFFFFF
    return h


def ragged(rng, base, amp, step=0.45, seed=0):
    """Broken wall top: base(s) minus a noisy, course-quantised loss."""
    def f(s):
        n = 0.5 + 0.5 * vnoise(s * 0.9, 0.3, 0.7, seed) * 0.8 + 0.2 * vnoise(s * 3.1, 1.3, 0.2, seed + 5)
        z = base(s) - amp * n
        return round(z / 0.125) * 0.125 + 0.03 * vnoise(s * 7.0, 2.0, 0.0, seed + 9)
    return f


# --------------------------------------------------------------------------- walls of a rectangular building

def rect_runs(W, Dp, t, cx=0.0, cy=0.0):
    """The four wall runs (p0, p1) of a W x Dp rectangle (centre lines), CCW, outer face outward."""
    x0, x1, y0, y1 = cx - W / 2, cx + W / 2, cy - Dp / 2, cy + Dp / 2
    return {
        'front': ((x0 - t / 2, y0), (x1 + t / 2, y0)),
        'right': ((x1, y0 - t / 2), (x1, y1 + t / 2)),
        'back': ((x1 + t / 2, y1), (x0 - t / 2, y1)),
        'left': ((x0, y1 + t / 2), (x0, y0 - t / 2)),
    }


def build_walls(mb, W, Dp, t, tops, openings, step, cx=0.0, cy=0.0, base=FOOT, mat=S, breaks=None):
    runs = rect_runs(W, Dp, t, cx, cy)
    for k, (p0, p1) in runs.items():
        L = (Vector(p1) - Vector(p0)).length
        cols = wall_cols(L, tops[k], openings.get(k, ()), base=base, step=step, breaks=(breaks or {}).get(k, ()))
        wall(mb, p0, p1, t, cols, mat=mat)
    return runs


def run_point(runs, side, s, t, inset):
    """World point on a run at distance s, pushed `inset` metres inward from the outer face."""
    p0, p1 = Vector((*runs[side][0], 0)), Vector((*runs[side][1], 0))
    d = (p1 - p0).normalized()
    n = Vector((d.y, -d.x, 0))
    return p0 + d * s + n * (t / 2 - inset), d, n


def opening_fill(mb, runs, side, s, w, z0, z1, t, curtain=False):
    """Dark interior card (trim 'opening', night-emissive) inside a door / window, a lintel above it."""
    c, d, n = run_point(runs, side, s, t, 0.16)
    rot = math.atan2(d.y, d.x)
    box(mb, c.x, c.y, z0, w + 0.02, 0.05, z1 - z0, D, ('t', 'opening'), rot=rot)
    if curtain:
        c2, _, _ = run_point(runs, side, s, t, 0.1)
        box(mb, c2.x, c2.y, z0 + (z1 - z0) * 0.35, w * 0.98, 0.03, (z1 - z0) * 0.62, D, ('t', 'hide'), rot=rot)
    lc, _, _ = run_point(runs, side, s, t, -0.04)
    a = lc - d * (w / 2 + 0.25) + Vector((0, 0, z1 + 0.08))
    b = lc + d * (w / 2 + 0.25) + Vector((0, 0, z1 + 0.08))
    beam(mb, a, b, 0.16, 0.16, 'planks')


def footing(mb, W, Dp, t, cy=0.0, h=0.22, ft=None):
    ft = ft or t + 0.14
    for (x, y, w, d) in ((0, cy - Dp / 2, W + ft, ft), (0, cy + Dp / 2, W + ft, ft), (-W / 2, cy, ft, Dp - ft + 0.02), (W / 2, cy, ft, Dp - ft + 0.02)):
        box(mb, x, y, FOOT - 0.02, w, d, h - FOOT + 0.02, D, ('t', 'stone', 0.8))


# --------------------------------------------------------------------------- thatch roofs

def thatch_gable(mb, W, Dp, t, wall_h, pitch, o, ov, th, lod, rng, hole=None, ridge_roll=True, sag=0.07, seed=0, cy=0.0):
    """Gabled thatch over a W x Dp body centred on (0, cy); the ridge runs along X."""
    m0 = len(mb.V)
    tp = TAN(pitch)
    ye = Dp / 2 + t / 2 + o
    ze = wall_h - o * tp
    zr = wall_h + (Dp / 2 + t / 2) * tp
    thv = th / math.cos(pitch)
    xe = W / 2 + t / 2 + ov
    cell = (0.33, 0.8, 2.2)[lod]
    nu = max(2, int(round(2 * xe / cell)))
    nv = (5, 3, 1)[lod]
    slope_len = ye / math.cos(pitch)
    for side in (-1, 1):
        P = []
        for i in range(nu + 1):
            s = i / nu
            row = []
            for j in range(nv + 1):
                tt = j / nv
                x = -xe + 2 * xe * s
                y = side * ye * (1 - tt)
                z = ze + (zr - ze) * tt + thv
                if 0 < i < nu and 0 < j < nv:
                    z -= sag * math.sin(math.pi * s) * math.sin(math.pi * tt)
                if lod == 0:
                    z += 0.03 * vnoise(x * 1.3, y * 1.3, side, seed)
                    if j == 0:
                        y += side * (0.04 + 0.05 * vnoise(x * 2.7, 0.5, side, seed + 1))
                        z += 0.03 * vnoise(x * 3.1, 1.5, side, seed + 2)
                row.append(Vector((x, y, z)))
            P.append(row)
        def uvf(i, j, P=P):
            return (P[i][j].x / TILE['Thatch'], (j / nv) * slope_len / TILE['Thatch'])
        mask = None
        if hole and hole[0] == side:
            hs0, hs1, ht0, ht1 = hole[1:]
            def mask(i, j, hs0=hs0, hs1=hs1, ht0=ht0, ht1=ht1):
                s, tt = (i + 0.5) / nu, (j + 0.5) / nv
                wob = 0.08 * vnoise(i * 0.7, j * 0.9, 3.3, seed + 4)
                return not (hs0 + wob < s < hs1 - wob and ht0 < tt < ht1)
        slab(mb, P, nu, nv, thv, R, uvf, mask)
    if ridge_roll and lod < 2:
        tube(mb, (-xe - 0.06, 0, zr + thv - 0.06), (xe + 0.06, 0, zr + thv - 0.06), 0.26, 0.26, (10, 6)[lod],
             mat=R, tile=TILE['Thatch'])
        if lod == 0:   # wicker ties over the ridge
            n = max(2, int(2 * xe / 1.2))
            for k in range(n + 1):
                x = -xe + 0.3 + (2 * xe - 0.6) * k / n
                tube(mb, (x, -0.2, zr + thv + 0.12), (x, 0.2, zr + thv + 0.12), 0.06, 0.06, 5, strip='wicker')
    if cy:   # verifier fix: the roof used to stay at y = 0 while house D's body sits at y = cy
        mb.transform(Matrix.Translation((0.0, cy, 0.0)), m0)
    return zr + thv + (0.2 if ridge_roll else 0.0), (xe, ye, ze, zr, thv)


def thatch_hip(mb, W, Dp, t, wall_h, pitch, o, th, lod, rng, hole=None, seed=0, cx=0.0, cy=0.0):
    tp = TAN(pitch)
    Xe, Ye = W / 2 + t / 2 + o, Dp / 2 + t / 2 + o
    thv = th / math.cos(pitch)
    ze = wall_h - o * tp + thv
    if Xe >= Ye:
        Xr, Yr = Xe - Ye, 0.0
        zr = ze + Ye * tp
    else:
        Xr, Yr = 0.0, Ye - Xe
        zr = ze + Xe * tp
    slopes = [
        ('front', (-Xe, -Ye), (Xe, -Ye), (-Xr, -Yr), (Xr, -Yr)),
        ('right', (Xe, -Ye), (Xe, Ye), (Xr, -Yr), (Xr, Yr)),
        ('back', (Xe, Ye), (-Xe, Ye), (Xr, Yr), (-Xr, Yr)),
        ('left', (-Xe, Ye), (-Xe, -Ye), (-Xr, Yr), (-Xr, -Yr)),
    ]
    cell = (0.5, 1.0, 3.0)[lod]
    nv = (5, 3, 1)[lod]
    for (name, E0, E1, R0, R1) in slopes:
        E0, E1, R0, R1 = [Vector((p[0] + cx, p[1] + cy, 0)) for p in (E0, E1, R0, R1)]
        L = (E1 - E0).length
        nu = max(1, int(round(L / cell)))
        ed = (E1 - E0).normalized()
        mid_e = (E0 + E1) / 2
        mid_r = (R0 + R1) / 2
        slope_len = math.hypot((mid_r - mid_e).length, zr - ze)
        P = []
        for i in range(nu + 1):
            s = i / nu
            row = []
            for j in range(nv + 1):
                tt = j / nv
                q = (E0 + (E1 - E0) * s) * (1 - tt) + (R0 + (R1 - R0) * s) * tt
                z = ze + (zr - ze) * tt
                if 0 < i < nu and 0 < j < nv:
                    z -= 0.06 * math.sin(math.pi * s) * math.sin(math.pi * tt)
                if lod == 0:
                    z += 0.03 * vnoise(q.x * 1.3, q.y * 1.3, 0.0, seed)
                    if j == 0:
                        out = Vector((ed.y, -ed.x, 0))
                        q = q + out * (0.04 + 0.05 * vnoise(q.x * 2.7 + q.y, 0.5, 0, seed + 1))
                        z += 0.03 * vnoise(q.x * 3.1, q.y * 3.1, 1.5, seed + 2)
                row.append(Vector((q.x, q.y, z)))
            P.append(row)
        def uvf(i, j, P=P, ed=ed, slope_len=slope_len, nv=nv):
            return (P[i][j].dot(ed) / TILE['Thatch'], (j / nv) * slope_len / TILE['Thatch'])
        mask = None
        if hole and hole[0] == name:
            hs0, hs1, ht0, ht1 = hole[1:]
            def mask(i, j, nu=nu, hs0=hs0, hs1=hs1, ht0=ht0, ht1=ht1):
                s, tt = (i + 0.5) / nu, (j + 0.5) / nv
                wob = 0.08 * vnoise(i * 0.7, j * 0.9, 3.3, seed + 4)
                return not (hs0 + wob < s < hs1 - wob and ht0 < tt < ht1)
        slab(mb, P, nu, nv, thv, R, uvf, mask)
    if lod < 2:
        sides = (8, 5)[lod]
        r = 0.2
        zt = zr - 0.04
        if Xr > 0:
            tube(mb, (-Xr - 0.1 + cx, cy, zt), (Xr + 0.1 + cx, cy, zt), r, r, sides, mat=R, tile=TILE['Thatch'])
        for sx in (-1, 1):
            for sy in (-1, 1):
                a = Vector((sx * Xe + cx, sy * Ye + cy, ze - 0.02))
                b = Vector((sx * Xr + cx, sy * Yr + cy, zt))
                tube(mb, a + (b - a) * 0.04, b, r * 0.8, r, sides, mat=R, tile=TILE['Thatch'])
    return zr + 0.2


def roof_frame(mb, W, Dp, t, wall_h, pitch, o, lod, rng, posts=True, partial=1.0, cy=0.0):
    """Bare timber roof frame: ridge pole on forked gable posts, pairs of rafters (body centred on (0, cy))."""
    m0 = len(mb.V)
    tp = TAN(pitch)
    zr = wall_h + (Dp / 2 + t / 2) * tp
    xe = W / 2 + t / 2
    if posts:
        for sx in (-1, 1):
            tube(mb, (sx * (W / 2), 0, FOOT), (sx * (W / 2), 0, zr + 0.25), 0.11, 0.09, (6, 5, 4)[lod], strip='logs')
    tube(mb, (-xe - 0.3, 0, zr + 0.05), (xe * (2 * partial - 1) + 0.3, 0, zr + 0.05), 0.1, 0.09, (6, 5, 4)[lod], strip='logs')
    if lod < 2:
        n = max(2, int(2 * xe / 0.75))
        for k in range(n + 1):
            x = -xe + 2 * xe * k / n
            if k / n > partial:
                break
            for side in (-1, 1):
                a = Vector((x, side * (Dp / 2 + t / 2 + o * 0.7), wall_h - o * 0.7 * tp))
                b = Vector((x, -side * 0.12, zr + 0.18))
                tube(mb, a, b, 0.055, 0.05, (5, 4)[lod], strip='logs', cap0=lod == 0, cap1=lod == 0)
    if cy:
        mb.transform(Matrix.Translation((0.0, cy, 0.0)), m0)
    return zr


# --------------------------------------------------------------------------- houses

HOUSE = {
    'A': dict(W=5.6, Dp=4.4, h=2.3, roof='gable', pitch=47, o=0.55, ov=0.45, door=('front', 0.0), win=[('right', 0.3)]),
    'B': dict(W=8.4, Dp=5.2, h=2.5, roof='hip', pitch=44, o=0.6, door=('front', -1.2), win=[('front', 1.9), ('left', 0.4)], timber=True, porch=True),
    'C': dict(W=6.4, Dp=5.6, h=2.7, roof='flat', door=('front', -1.4), win=[('front', 1.2), ('right', -0.6)], upper=True),
    'D': dict(W=6.8, Dp=4.8, h=2.4, roof='gable', pitch=45, o=0.55, ov=0.45, door=('front', 1.2), win=[('left', 0.0)], yard=True),
}
HOUSE_TITLES = {
    'A': 'small mudbrick house, gabled straw thatch',
    'B': 'long timber-framed mudbrick house, hipped thatch, front porch',
    'C': 'flat-roofed mudbrick house with a roof room, clay roofs, ladders',
    'D': 'mudbrick house with a walled front yard and bread oven, gabled thatch',
}


def house(var, state, lod):
    P = HOUSE[var]
    rng = random.Random(_seed('house', var, state, lod))
    sd = _seed('house', var) % 1000
    mb = MB(sd)
    W, Dp, h, t = P['W'], P['Dp'], P['h'], 0.42
    cy = 0.0
    yard = P.get('yard')
    if yard:          # house at the back, yard toward -Y; footprint centred on the whole plot
        cy = 2.2
    pitch = math.radians(P.get('pitch', 45))
    tp = TAN(pitch)
    step = (0.5, 1.1, None)[lod]
    intact = state == 'Intact'
    # --- wall tops per state
    def flat(s):
        return h
    tops = {k: flat for k in ('front', 'right', 'back', 'left')}
    breaks = {}
    if P['roof'] == 'gable' and state in ('Intact', 'Damaged'):
        def gable(s, Lr=Dp + t):
            y = s - Lr / 2
            return h + max(0.0, (Dp / 2 + t / 2 - abs(y))) * tp - 0.04
        tops['right'] = tops['left'] = gable
        breaks = {'right': ((Dp + t) / 2,), 'left': ((Dp + t) / 2,)}
    if state == 'Construction':
        for k in tops:
            fr = rng.uniform(0.45, 0.8)
            tops[k] = ragged(rng, lambda s, fr=fr: h * fr + 0.3, 0.35, seed=sd + len(k))
        step = (0.35, 0.8, None)[lod]
    elif state == 'Damaged':
        base_r = tops['right']
        tops['right'] = ragged(rng, lambda s: min(base_r(s), h + 0.4) if s < 1.0 else base_r(s), 0.0, seed=sd)
        f0 = tops['front']
        Lf = W + t
        def fr_top(s, Lf=Lf):
            d = max(0.0, s - Lf * 0.62) / (Lf * 0.38)
            return f0(s) - (h * 0.55) * min(1.0, d * 1.4) + 0.15 * vnoise(s * 2.3, 0, 0, sd)
        tops['front'] = fr_top
        def rt_top(s, Lr=Dp + t, b=base_r):
            d = max(0.0, 1.0 - s / (Lr * 0.45))
            return b(s) - (h * 0.55) * min(1.0, d * 1.4) + 0.15 * vnoise(s * 2.3, 1, 0, sd)
        tops['right'] = rt_top
        step = (0.35, 0.9, 2.0)[lod]
    elif state == 'Ruin':
        for k in tops:
            tops[k] = ragged(rng, lambda s: 0.9 + 0.7 * vnoise(s * 0.4, 3, 0, sd), 0.5, seed=sd + len(k) * 3)
        step = (0.3, 0.7, 1.6)[lod]
    # --- openings
    openings = {}
    dside, dpos = P['door']
    Lside = {'front': W + t, 'back': W + t, 'left': Dp + t, 'right': Dp + t}
    def sat(side, pos):
        return Lside[side] / 2 + (pos if side in ('front', 'right') else -pos)
    dw, dh = 0.95, 1.8
    openings.setdefault(dside, []).append((sat(dside, dpos) - dw / 2, sat(dside, dpos) + dw / 2, -10.0, dh))
    wins = []
    for (ws, wp) in P['win']:
        a = sat(ws, wp)
        openings.setdefault(ws, []).append((a - 0.25, a + 0.25, 1.25, 1.7))
        wins.append((ws, a))
    runs = build_walls(mb, W, Dp, t, tops, openings, step, cy=cy, breaks=breaks)
    # stone footing course (a ring under the walls) and, when the inside shows, an earth floor
    footing(mb, W, Dp, t, cy)
    if state != 'Intact':
        prism(mb, rect(0, cy, W - t + 0.05, Dp - t + 0.05), FOOT, 0.1, S, ('w', 3.0))
    # door and windows
    if state != 'Ruin':
        opening_fill(mb, runs, dside, sat(dside, dpos), dw, 0.0, dh, t, curtain=(lod == 0))
        for (ws, a) in wins:
            if state == 'Construction' and tops[ws](a) < 1.9:
                continue
            opening_fill(mb, runs, ws, a, 0.5, 1.25, 1.7, t)
        # threshold stone
        c, d, n = run_point(runs, dside, sat(dside, dpos), t, -0.3)
        box(mb, c.x, c.y, FOOT, dw + 0.4, 0.55, 0.42, D, ('t', 'stone', 1.0), rot=math.atan2(d.y, d.x))
    # timber framing (B)
    if P.get('timber') and lod < 2 and state != 'Ruin':
        for side, (p0, p1) in runs.items():
            L = Lside[side]
            n = max(2, int(round(L / 2.1)))
            for k in range(n + 1):
                s = t / 2 + (L - t) * k / n
                if any(a - 0.15 < s < b + 0.15 for (a, b, _, _) in openings.get(side, ())):
                    continue
                c, d, nn = run_point(runs, side, s, t, -0.05)
                ztop = min(tops[side](s), h) - 0.05
                if ztop > 0.6:
                    beam(mb, c + Vector((0, 0, 0.2)), c + Vector((0, 0, ztop)), 0.2, 0.2, 'planks')
            if state == 'Intact':
                a, d, nn = run_point(runs, side, 0.0, t, -0.06)
                b, _, _ = run_point(runs, side, L, t, -0.06)
                beam(mb, a + Vector((0, 0, h - 0.12)), b + Vector((0, 0, h - 0.12)), 0.18, 0.2, 'planks')
    # --- roofs
    top_z = h
    roof = P['roof']
    if roof == 'gable':
        if state == 'Intact':
            top_z, _ = thatch_gable(mb, W, Dp, t, h, pitch, P['o'], P['ov'], 0.36, lod, rng, seed=sd, cy=cy)
        elif state == 'Damaged':
            top_z, g = thatch_gable(mb, W, Dp, t, h, pitch, P['o'], P['ov'], 0.36, lod, rng, hole=(-1, 0.55, 1.01, -0.1, 0.8), seed=sd, cy=cy)
            # exposed rafters in the hole, a fallen bundle
            xe, ye, ze, zr, thv = g
            for x in (W * 0.18, W * 0.32, W * 0.45):
                tube(mb, (x, cy - ye * 0.8, ze + (zr - ze) * 0.2 - 0.05), (x, cy - 0.1, zr - 0.05), 0.06, 0.05, 5, strip='logs')
            rubble(mb, W / 2 + 0.6, -Dp / 2 - 0.6, 1.2, 0.7, rng)
        elif state == 'Construction':
            top_z = roof_frame(mb, W, Dp, t, h, pitch, P['o'], lod, rng, cy=cy)
    elif roof == 'hip':
        if state == 'Intact':
            top_z = thatch_hip(mb, W, Dp, t, h, pitch, P['o'], 0.36, lod, rng, seed=sd)
        elif state == 'Damaged':
            top_z = thatch_hip(mb, W, Dp, t, h, pitch, P['o'], 0.36, lod, rng, hole=('front', 0.55, 1.01, -0.1, 0.85), seed=sd)
            rubble(mb, W / 2 + 0.6, -Dp / 2 - 0.6, 1.3, 0.8, rng)
        elif state == 'Construction':
            top_z = roof_frame(mb, W, Dp, t, h, pitch, P['o'], lod, rng)
        if P.get('porch') and state in ('Intact', 'Damaged') and lod < 2:
            # lean-to porch on two posts in front of the door
            px0, px1 = -3.2, 0.6
            yb, yf = -Dp / 2 - t / 2 - 0.05, -Dp / 2 - 2.0
            for x in (px0 + 0.2, px1 - 0.2):
                tube(mb, (x, yf + 0.2, FOOT), (x, yf + 0.2, 1.95), 0.09, 0.08, 6, strip='logs')
            beam(mb, (px0, yf + 0.2, 1.98), (px1, yf + 0.2, 1.98), 0.14, 0.16, 'planks')
            Pp = []
            nu = 6 if lod == 0 else 2
            for i in range(nu + 1):
                x = px0 - 0.3 + (px1 - px0 + 0.6) * i / nu
                Pp.append([Vector((x, yf - 0.25, 2.12)), Vector((x, yb, h - 0.15))])
            slab(mb, Pp, nu, 1, 0.2, R, lambda i, j, Pp=Pp: (Pp[i][j].x / 2.0, j * 0.9))
    elif roof == 'flat':
        top_z = flat_roofs(mb, P, W, Dp, t, h, state, lod, rng, sd, runs)
    # --- beam ends under the gable eaves, clutter by the door
    if P['roof'] == 'gable' and state in ('Intact', 'Damaged') and lod < 2:
        zr0 = h + (Dp / 2 + t / 2) * tp
        xe0 = W / 2 + t / 2 + P['ov']
        for sx in (-1, 1):
            tube(mb, (sx * (W / 2 - 0.3), cy, zr0 - 0.35), (sx * (xe0 + 0.12), cy, zr0 - 0.38), 0.12, 0.11, (7, 5)[lod], strip='logs')
            for sy in (-1, 1):
                tube(mb, (sx * (W / 2 - 0.3), cy + sy * (Dp / 2), h + 0.05), (sx * (xe0 - 0.05), cy + sy * (Dp / 2), h + 0.04), 0.1, 0.09, (6, 4)[lod], strip='logs')
    if state == 'Intact' and lod == 0:
        c, d, n = run_point(runs, dside, sat(dside, dpos), t, -0.75)
        for k, off in enumerate((0.95, 1.4)):
            q = c + d * off
            lathe(mb, q.x, q.y, [(0.0, 0.0), (0.17, 0.02), (0.24, 0.28), (0.17, 0.52), (0.11, 0.58), (0.13, 0.62)], 8, S, tile=1.0)
        q = c - d * 1.3
        for k in range(5):
            zz = 0.1 + (k // 3) * 0.17
            oo = (k % 3) * 0.19 - 0.19 + (k // 3) * 0.09
            a = q + n * -0.1 + d * oo
            tube(mb, a + Vector((0, 0, zz)) - n * 0.0, a + Vector((0, 0, zz)) + n * 0.75, 0.085, 0.08, 6, strip='logs')
    # --- yard (D)
    if yard:
        yt = 0.32
        yW, yD = W + 0.3, 4.1
        ycy = cy - Dp / 2 - t / 2 - yD / 2 + yt / 2
        ytops = {}
        yh = 1.45 if state != 'Ruin' else 0.6
        for k in ('front', 'right', 'back', 'left'):
            if state in ('Intact', 'Damaged', 'Construction'):
                fr = 1.0 if state != 'Construction' else 0.55
                ytops[k] = lambda s, fr=fr, k=k: yh * fr + 0.06 * math.sin(s * 3.0) + (0.0 if state != 'Damaged' or k != 'left' else -0.7 * max(0.0, 1 - abs(s - 2.0) / 1.2))
            else:
                ytops[k] = ragged(rng, lambda s: 0.5, 0.3, seed=sd + 77)
        gate_s = (yW + yt) / 2 + 1.2
        yruns = rect_runs(yW, yD, yt, 0.0, ycy)
        for k in ('front', 'right', 'left'):
            p0, p1 = yruns[k]
            L = (Vector(p1) - Vector(p0)).length
            ops = [(gate_s - 0.6, gate_s + 0.6, -10, 10)] if k == 'front' else []
            cols = wall_cols(L, ytops[k], ops, step=(0.6, 1.5, None)[lod])
            wall(mb, p0, p1, yt, cols)
        if state != 'Ruin':
            # bread oven (beehive of clay) and a woodpile
            lathe(mb, -yW / 2 + 1.1, ycy + 0.6, [(0.75, FOOT), (0.78, 0.2), (0.7, 0.6), (0.45, 1.0), (0.0, 1.2)],
                  (12, 8, 6)[lod], S, tile=2.0)
            if lod < 2:
                for k in range(5):
                    y = ycy + 1.2 - k * 0.0
                    z = 0.12 + (k % 3) * 0.2
                    xo = 0.5 + (k // 3) * 0.12
                    tube(mb, (yW / 2 - xo - 0.1, ycy + 1.4 - k * 0.22 * 0, z), (yW / 2 - xo - 0.1, ycy - 0.2, z), 0.1, 0.1, 6, strip='logs')
                for k in range(3):
                    lathe(mb, yW / 2 - 0.9 - k * 0.55, ycy - 1.3, [(0.0, 0.0), (0.2, 0.02), (0.27, 0.3), (0.2, 0.55), (0.12, 0.62)],
                          (8, 6)[lod], R if False else S, tile=1.0)
        else:
            rubble(mb, -yW / 2 + 1.1, ycy + 0.6, 0.9, 0.5, rng)
    # --- construction clutter
    if state == 'Construction' and lod < 2:
        for k in range(3):                         # stacks of drying mudbricks
            box(mb, -W / 2 + 0.6 + k * 0.9, -Dp / 2 - 1.5 + (cy if yard else 0) - (2.0 if yard else 0), 0.0, 0.8, 0.45, 0.25 + 0.25 * (k % 2), S, ('w', 2.0))
        a = Vector((W / 2 - 0.6, -Dp / 2 - 1.3 + cy, 0.0))
        ladder(mb, a, a + Vector((0, 1.1, h * 0.7)), 0.45, lod)
        for k in range(2):                         # thatch bundles waiting
            tube(mb, (W / 2 + 0.8, -0.8 + k * 0.5 + cy, 0.2), (W / 2 + 0.8, 0.6 + k * 0.5 + cy, 0.22), 0.2, 0.23, 7, mat=R, tile=2.0)
    if state == 'Ruin' and lod < 2:
        rubble(mb, W * 0.15, cy + 0.2, 1.4, 0.55, rng)
        rubble(mb, -W / 2 - 0.5, cy - Dp * 0.2, 0.9, 0.45, rng)
        tube(mb, (-W * 0.3, cy - Dp * 0.3, 0.1), (W * 0.25, cy + Dp * 0.25, 0.9), 0.1, 0.09, 6, strip='logs')
    if lod == 0:
        mb.jitter(0.025, 0.6, seed=sd)
    foot_d = Dp + t + 0.14 + (4.1 if yard else 0)
    info = dict(W=W + t + 0.14, D=foot_d, H=top_z, title=HOUSE_TITLES[var])
    if P['roof'] == 'gable':
        zr = h + (Dp / 2 + t / 2) * tp + 0.36 / math.cos(pitch)
        xe = W / 2 + t / 2 + P['ov']
        info['sockets'] = {'ridge_end_left': [-xe, cy, round(zr, 3)], 'ridge_end_right': [xe, cy, round(zr, 3)]}
    return mb, info


def ladder(mb, a, b, w, lod):
    a, b = Vector(a), Vector(b)
    d = (b - a)
    L = d.length
    side = Vector((d.y, -d.x, 0)).normalized() * (w / 2) if abs(d.z) < L * 0.999 else Vector((w / 2, 0, 0))
    if (Vector((d.x, d.y, 0))).length < 1e-4:
        side = Vector((w / 2, 0, 0))
    for sg in (-1, 1):
        tube(mb, a + side * sg, b + side * sg, 0.04, 0.035, (5, 4, 3)[lod], strip='logs', cap0=False, cap1=False)
    if lod < 2:
        n = int(L / 0.35)
        for k in range(1, n):
            p = a + d * (k / n)
            tube(mb, p - side * 1.15, p + side * 1.15, 0.025, 0.025, 4, strip='planks', cap0=False, cap1=False)


def flat_roofs(mb, P, W, Dp, t, h, state, lod, rng, sd, runs):
    """Clay roof slab with parapet, beam ends (vigas), a roof room, ladders and jars."""
    def roof_slab(x0, x1, y0, y1, z, hole=None):
        nu = max(1, int((x1 - x0) / (0.8 if lod == 0 else 2.5)))
        nv = max(1, int((y1 - y0) / (0.8 if lod == 0 else 2.5)))
        G = [[Vector((x0 + (x1 - x0) * i / nu, y0 + (y1 - y0) * j / nv, z + (0.02 * vnoise(i, j, 0, sd) if lod == 0 else 0)))
              for j in range(nv + 1)] for i in range(nu + 1)]
        mask = None
        if hole:
            def mask(i, j):
                s, q = (i + 0.5) / nu, (j + 0.5) / nv
                return not (hole[0] < s < hole[1] and hole[2] < q < hole[3])
        slab(mb, G, nu, nv, 0.32, R, lambda i, j: (G[i][j].x / 3.0, G[i][j].y / 3.0), mask)
    zt = h + 0.3
    if state in ('Intact', 'Damaged'):
        hole = (0.45, 0.95, 0.1, 0.7) if state == 'Damaged' else None
        roof_slab(-W / 2 - t / 2 - 0.08, W / 2 + t / 2 + 0.08, -Dp / 2 - t / 2 - 0.08, Dp / 2 + t / 2 + 0.08, zt, hole)
        ptops = {k: (lambda s: zt + 0.42) for k in ('front', 'right', 'back', 'left')}
        if state == 'Damaged':
            ptops['front'] = ragged(rng, lambda s: zt + 0.42, 0.3, seed=sd + 3)
        build_walls(mb, W + t - 0.25, Dp + t - 0.25, 0.25, ptops, {}, (0.8, 2.0, None)[lod], base=zt - 0.1)
        # vigas: beam ends through the front and back walls under the roof
        if lod < 2:
            n = int(W / 0.75)
            for k in range(n + 1):
                x = -W / 2 + 0.2 + (W - 0.4) * k / n
                for sy in (-1, 1):
                    tube(mb, (x, sy * (Dp / 2 - 0.3), h + 0.12), (x, sy * (Dp / 2 + t / 2 + 0.38), h + 0.1 + 0.02 * sy), 0.075, 0.07, (6, 4)[lod], strip='logs')
        # roof room (upper storey) at the back left
        W2, D2, h2 = 3.4, 3.0, 2.3
        cx2, cy2 = -W / 2 + W2 / 2 + 0.3, Dp / 2 - D2 / 2 - 0.3
        up_ok = state == 'Intact'
        tops2 = {k: (lambda s: zt + h2) for k in ('front', 'right', 'back', 'left')}
        if not up_ok:
            for k in tops2:
                tops2[k] = ragged(rng, lambda s: zt + h2 * 0.6, 0.7, seed=sd + 11 + len(k))
        L2 = W2 + 0.36
        ops2 = {'front': [(L2 / 2 - 0.4, L2 / 2 + 0.4, -10, zt + 1.65)]}
        r2 = build_walls(mb, W2, D2, 0.36, tops2, ops2, (0.5, 1.2, None)[lod], cx=cx2, cy=cy2, base=zt - 0.1)
        if up_ok:
            opening_fill(mb, r2, 'front', L2 / 2, 0.8, zt, zt + 1.65, 0.36, curtain=lod == 0)
            roof_slab(cx2 - W2 / 2 - 0.26, cx2 + W2 / 2 + 0.26, cy2 - D2 / 2 - 0.26, cy2 + D2 / 2 + 0.26, zt + h2 + 0.28)
            if lod < 2:
                for k in range(5):
                    x = cx2 - W2 / 2 + 0.3 + (W2 - 0.6) * k / 4
                    tube(mb, (x, cy2 - D2 / 2 + 0.2, zt + h2 + 0.1), (x, cy2 - D2 / 2 - 0.5, zt + h2 + 0.08), 0.07, 0.065, 5, strip='logs')
        if lod < 2:
            # ladders: ground -> roof at the front right, roof -> roof room
            a = Vector((W / 2 - 0.9, -Dp / 2 - t / 2 - 1.1, 0.0))
            ladder(mb, a, Vector((W / 2 - 0.9, -Dp / 2 - t / 2 - 0.05, zt + 0.9)), 0.5, lod)
            if up_ok:
                a = Vector((cx2 + W2 / 2 + 1.0, cy2 - 0.2, zt))
                ladder(mb, a, Vector((cx2 + W2 / 2 + 0.2, cy2 - 0.2, zt + h2 + 0.6)), 0.45, lod)
            # storage jars and a firewood bundle on the lower roof
            for k in range(3):
                lathe(mb, W / 2 - 0.9 - k * 0.6, 0.6 + (k % 2) * 0.4, [(0.0, zt), (0.2, zt + 0.02), (0.3, zt + 0.35), (0.22, zt + 0.66), (0.14, zt + 0.72)],
                      (8, 6)[lod], R, tile=1.0)
            for k in range(4):
                tube(mb, (0.4, -0.6 + k * 0.18, zt + 0.1 + (k % 2) * 0.12), (2.0, -0.4 + k * 0.18, zt + 0.1 + (k % 2) * 0.12), 0.07, 0.07, 5, strip='logs')
        # water spouts
        for sx in (-1, 1):
            beam(mb, (sx * W * 0.3, -Dp / 2, zt + 0.1), (sx * W * 0.3, -Dp / 2 - t / 2 - 0.55, zt + 0.05), 0.14, 0.1, 'planks')
        return zt + h2 + 0.6 if up_ok else zt + 0.5
    if state == 'Construction':
        # a few roof beams laid across the half-built walls
        if lod < 2:
            for k in range(4):
                x = -W / 2 + 0.6 + k * 1.2
                tube(mb, (x, -Dp / 2 - 0.3, h * 0.72), (x, Dp / 2 + 0.3, h * 0.72), 0.08, 0.075, 5, strip='logs')
        return h
    return 1.6


# --------------------------------------------------------------------------- granary

GRANARY_DRUMS = [(-7.6, 6.8), (-2.6, 9.4), (2.8, 9.4), (7.8, 6.8), (-9.2, 1.6), (9.2, 1.6), (-4.4, 3.6), (4.4, 3.6)]
PLAT_C = (0.0, 4.6)
PLAT_R = (13.2, 9.6)
PLAT_H = 1.5


def drum(mb, x, y, z0, lod, rng, sd, height_frac=1.0, cap=True, ragged_top=0.0, ladder_side=None, cap_tilt=None):
    segs = (24, 14, 8)[lod]
    rb = 2.05
    H = 4.4
    prof = [(rb * 1.02, z0 - 0.3), (rb * 1.04, z0 + 0.3)]
    nz = (6, 3, 2)[lod]
    for k in range(1, nz + 1):
        f = k / nz
        z = z0 + 0.3 + (H - 0.3) * f
        if f > height_frac + 1e-6:
            break
        r = rb * (1.04 + 0.06 * math.sin(math.pi * f * 0.9) - 0.12 * f * f)
        prof.append((r, z))
    if height_frac < 1.0:
        zf = z0 + 0.3 + (H - 0.3) * height_frac
        r = rb * (1.04 + 0.06 * math.sin(math.pi * height_frac * 0.9) - 0.12 * height_frac ** 2)
        if prof[-1][1] < zf - 0.05:
            prof.append((r, zf))
    lathe(mb, x, y, prof, segs, S, tile=3.0, cap_top=True, noise=0.07 if lod == 0 else 0.0, nscale=0.9, seed=sd,
          top_ragged=ragged_top, rng=rng)
    # stone collar
    if lod < 2:
        lathe(mb, x, y, [(rb + 0.3, z0 - 0.05), (rb + 0.32, z0 + 0.35), (rb + 0.05, z0 + 0.5)], segs, D, strip='stone',
              noise=0.05 if lod == 0 else 0.0, seed=sd + 1)
    top = prof[-1][1]
    if height_frac >= 0.5 and ragged_top == 0.0:
        # low door facing away from the platform centre (dark trim card, night-emissive)
        d = Vector((x - PLAT_C[0], y - PLAT_C[1], 0))
        d = d.normalized() if d.length > 1e-3 else Vector((0, -1, 0))
        rr = rb * 1.06 + 0.02
        box(mb, x + d.x * rr, y + d.y * rr, z0 + 0.35, 0.75, 0.12, 1.15, D, ('t', 'opening'), rot=math.atan2(d.y, d.x) + math.pi / 2)
        if lod < 2:
            beam(mb, (x + d.x * (rr + 0.05) - d.y * 0.6, y + d.y * (rr + 0.05) + d.x * 0.6, z0 + 1.6),
                 (x + d.x * (rr + 0.05) + d.y * 0.6, y + d.y * (rr + 0.05) - d.x * 0.6, z0 + 1.6), 0.14, 0.14, 'planks')
    if cap and height_frac >= 1.0:
        zc = top - 0.25
        cp = [(rb * 0.86, zc), (rb + 0.42, zc - 0.32), (rb + 0.5, zc - 0.05), (rb * 0.55, zc + 1.4), (0.22, zc + 2.6), (0.0, zc + 2.85)]
        if lod == 2:
            cp = [cp[1], cp[2], cp[5]]
        mb_start = len(mb.V)
        lathe(mb, x, y, cp, segs, R, tile=2.0, noise=0.06 if lod == 0 else 0.0, nscale=1.1, seed=sd + 2)
        if lod < 2:   # top knot
            lathe(mb, x, y, [(0.0, zc + 2.6), (0.28, zc + 2.62), (0.24, zc + 3.0), (0.0, zc + 3.15)], 6, R, tile=1.0)
        if cap_tilt:
            # slipped cap: rotated about its own base and shifted (dx, dy, dz) relative to where it sat.
            # Verifier fix: the old translation (z0 + 0.3 - zc) put this cap ~5.8 m below ground, invisible.
            m = (Matrix.Translation((x + cap_tilt[0], y + cap_tilt[1], zc + cap_tilt[2])) @
                 Matrix.Rotation(cap_tilt[3], 4, 'X') @ Matrix.Translation((-x, -y, -zc)))
            mb.transform(m, mb_start)
            return max(top, max(mb.V[i].z for i in range(mb_start, len(mb.V))))
        top = zc + 3.1
    return top


def granary(var, state, lod):
    rng = random.Random(_seed('granary', state, lod))
    sd = _seed('granary') % 1000
    mb = MB(sd)
    pc = PLAT_C
    n = (36, 20, 12)[lod]
    poly = ellipse(pc[0], pc[1], PLAT_R[0], PLAT_R[1], n, wob=0.0)
    top_poly = ellipse(pc[0], pc[1], PLAT_R[0] - 0.25, PLAT_R[1] - 0.25, n)
    ph = PLAT_H if state != 'Ruin' else 1.0
    if lod == 0:   # irregular rim
        poly = [(x + 0.12 * vnoise(x * 0.5, y * 0.5, 0, sd), y + 0.12 * vnoise(x * 0.5, y * 0.5, 1, sd)) for x, y in poly]
        top_poly = [(x + 0.1 * vnoise(x * 0.5, y * 0.5, 0, sd), y + 0.1 * vnoise(x * 0.5, y * 0.5, 1, sd)) for x, y in top_poly]
    prism(mb, poly, FOOT, ph, D, ('t', 'stone', 2.0), top_uv=('w', 3.0), top_mat=S, top_poly=top_poly)
    # threshing floor in front
    tf = (0.0, -9.0)
    prism(mb, ellipse(tf[0], tf[1], 7.0, 5.6, (28, 16, 10)[lod]), FOOT, 0.07, S, ('w', 3.0))
    top = ph
    drums = GRANARY_DRUMS
    if state == 'Intact':
        for k, (x, y) in enumerate(drums):
            top = max(top, drum(mb, x, y, ph, lod, rng, sd + k))
    elif state == 'Construction':
        for k, (x, y) in enumerate(drums):
            if k < 3:
                drum(mb, x, y, ph, lod, rng, sd + k, cap=False)
            elif k < 6:
                fr = (0.35, 0.6, 0.2)[k - 3]
                drum(mb, x, y, ph, lod, rng, sd + k, height_frac=fr, cap=False, ragged_top=0.08)
                if lod < 2:   # scaffold poles
                    for a in (0.3, 2.4, 4.4):
                        px, py = x + math.cos(a) * 2.7, y + math.sin(a) * 2.7
                        tube(mb, (px, py, ph - 0.3), (px, py, ph + 4.0), 0.06, 0.05, 5, strip='logs')
        if lod < 2:
            for k in range(4):
                tube(mb, (-4 + k * 0.6, -3.5, 0.22), (-4 + k * 0.6, -1.5, 0.25), 0.2, 0.23, 7, mat=R, tile=2.0)
        top = ph + 4.6
    elif state == 'Damaged':
        for k, (x, y) in enumerate(drums):
            if k in (1, 6):
                drum(mb, x, y, ph, lod, rng, sd + k, height_frac=0.75 if k == 6 else 1.0, cap=False, ragged_top=0.35 if k == 6 else 0.0)
            elif k == 4:
                top = max(top, drum(mb, x, y, ph, lod, rng, sd + k, cap_tilt=(0.0, -0.7, -0.55, 0.42)))
            else:
                top = max(top, drum(mb, x, y, ph, lod, rng, sd + k))
        rubble(mb, 4.4, 1.0, 1.4, 0.7, rng, mat=S, tile=3.0)
        rubble(mb, -11.5, -2.5, 2.0, 1.0, rng, mat=D, tile=None) if False else rubble(mb, -11.5, -2.5, 2.0, 1.0, rng, mat=S, tile=3.0)
    else:   # ruin
        for k, (x, y) in enumerate(drums):
            if k in (0, 3, 5, 6):
                drum(mb, x, y, ph, lod, rng, sd + k, height_frac=rng.uniform(0.12, 0.35), cap=False, ragged_top=0.3)
            else:
                rubble(mb, x, y, 2.3, 1.0 + ph, rng, mat=S, tile=3.0)
        top = ph + 2.0
    # grain heaps, baskets, ladders, a cart
    if state == 'Intact' or (state == 'Damaged'):
        for (gx, gy, gr, gh) in ((-2.3, -9.6, 2.9, 1.5), (3.2, -10.6, 2.1, 1.05), (0.8, -6.8, 1.6, 0.6)):
            prof = [(gr * 1.02, -0.05), (gr, 0.1), (gr * 0.7, gh * 0.55), (gr * 0.32, gh * 0.9), (0.0, gh + 0.1)]
            lathe(mb, gx, gy, prof, (16, 10, 6)[lod], D, strip='grain', noise=0.08 if lod == 0 else 0, nscale=0.7, seed=sd + 9, ring_uv=True)
        if lod < 2:
            for (bx, by) in ((-6.0, -11.5), (-5.2, -12.4), (5.6, -12.0)):
                lathe(mb, bx, by, [(0.0, 0.0), (0.3, 0.0), (0.38, 0.45), (0.36, 0.5)], (10, 6)[lod], D, strip='wicker', cap_top=True)
            for (lx, ly, ang) in ((-5.0, -4.6, 0.25), (4.6, -4.5, -0.2), (11.6, 0.4, 1.2)):
                c, s_ = math.cos(ang), math.sin(ang)
                a = Vector((lx + s_ * 1.0, ly - c * 1.0, 0.0))
                b = Vector((lx, ly, ph + 0.6))
                ladder(mb, a, b, 0.5, lod)
            # drum-top ladders to the hatches
            for k in (0, 4, 7):
                x, y = drums[k]
                d = Vector((x - pc[0], y - pc[1], 0)).normalized()
                a = Vector((x, y, ph)) + d * -3.0
                b = Vector((x, y, ph + 4.0)) + d * -2.0
                if state == 'Intact':
                    ladder(mb, a, b, 0.45, lod)
            cart(mb, 8.4, -8.6, 0.6, lod)
    if state == 'Ruin' and lod < 2:
        rubble(mb, 10.5, -1.0, 2.2, 1.1, rng, tile=3.0)
        rubble(mb, -3.0, -6.2, 1.6, 0.5, rng, tile=3.0)
    if lod == 0:
        mb.jitter(0.02, 0.5, seed=sd)
    info = dict(W=2 * PLAT_R[0] + 0.3, D=(pc[1] + PLAT_R[1]) - (tf[1] - 5.6), H=top,
                title='granary: eight clay grain drums with thatch caps on a dry-stone platform, threshing floor')
    return mb, info


def cart(mb, x, y, rot, lod):
    m0 = len(mb.V)
    box(mb, 0, 0, 0.75, 1.3, 2.4, 0.12, D, ('t', 'planks'))
    for sx in (-1, 1):
        box(mb, sx * 0.62, 0, 0.87, 0.08, 2.4, 0.4, D, ('t', 'planks'))
        w0 = len(mb.V)
        lathe(mb, 0, 0, [(0.0, -0.08), (0.48, -0.08), (0.48, 0.08), (0.0, 0.08)], (12, 8)[lod], D, strip='planks')
        mb.transform(Matrix.Translation((sx * 0.78, 0.2, 0.5)) @ Matrix.Rotation(math.pi / 2, 4, 'Y'), w0)
    beam(mb, (0, -1.2, 0.8), (0, -3.0, 0.55), 0.1, 0.1, 'planks')
    mb.transform(Matrix.Translation((x, y, 0)) @ Matrix.Rotation(rot, 4, 'Z'), m0)


# --------------------------------------------------------------------------- temple

TEMPLE_C = (0.0, 5.0)          # centre of the stepped platform (stair runs toward -Y)
TIERS = [(30.0, FOOT, 3.4), (22.0, 3.4, 6.4), (15.0, 6.4, 9.0)]


def temple(var, state, lod):
    rng = random.Random(_seed('temple', state, lod))
    sd = _seed('temple') % 1000
    mb = MB(sd)
    cx, cy = TEMPLE_C
    tiers = TIERS
    nt = 3
    if state == 'Construction':
        nt = 2
    erode = state == 'Ruin'
    top = 0.0
    for k in range(nt):
        size, z0, z1 = tiers[k]
        if state == 'Construction' and k == 1:
            z1 = z0 + 1.6
        if erode:
            z1 = z0 + (z1 - z0) * 0.72
            z0 = z0 if k == 0 else tiers[k][1] * 0.72
        inset = 0.45 if not erode else 1.6 + k * 0.4
        p = rect(cx, cy, size, size)
        tp = rect(cx, cy, size - 2 * inset, size - 2 * inset)
        if erode:   # verifier fix: LOD2 used to fall back to crisp intact-looking tiers with a Roof slot LOD0 lacks
            # eroded mound: subdivided frustum with noise
            n = (10, 6, 3)[lod]
            def ring(sz, z, amp):
                out = []
                half = sz / 2
                for side in range(4):
                    for i in range(n):
                        f = -1 + 2 * i / n
                        x, y = [(f * half, -half), (half, f * half), (-f * half, half), (-half, -f * half)][side]
                        q = Vector((cx + x, cy + y, z))
                        d = fbm3(q, 0.25, sd + k) * amp
                        rr = Vector((x, y, 0)).normalized()
                        out.append(q + rr * d + Vector((0, 0, abs(d) * 0.3)))
                return out
            rings = [ring(size + 0.6, z0, 0.6), ring(size - inset, z0 + (z1 - z0) * 0.55, 0.9), ring(size - 2 * inset, z1, 0.7)]
            loft(mb, rings, S, 'lathe', tile=2.0, cap_top=True, cap_bottom=True)
        else:
            prism(mb, p, z0, z1, S, ('w', 2.0), top_uv=('w', 3.0), top_mat=R, top_poly=tp)
        # buttress pilasters on the faces
        if lod == 0 and not erode:
            half = size / 2
            nb = int(size / 3.2)
            for side in range(4):
                for i in range(1, nb):
                    f = -half + size * i / nb
                    if side == 0 and abs(f) < 3.2:
                        continue        # stair
                    rot = side * math.pi / 2
                    ox, oy = [(f, -half), (half, f), (-f, half), (-half, -f)][side]
                    box(mb, cx + ox, cy + oy, z0, 0.9, 0.5, (z1 - z0) - 0.35, S, ('w', 2.0), rot=rot)
        if lod < 2 and not erode and not (state == 'Construction' and k == 1):
            ps = size - 2 * inset - 0.3
            ptop = {kk: (lambda s_, z1=z1: z1 + 0.55) for kk in ('front', 'right', 'back', 'left')}
            Lp = ps + 0.35
            build_walls(mb, ps, ps, 0.35, ptop, {'front': [(Lp / 2 - 2.6, Lp / 2 + 2.6, -100, 100)]}, (3.0, 8.0)[lod],
                        cx=cx, cy=cy, base=z1 - 0.1)
        top = z1
    # the stair: a ramp block with steps toward -Y from the ground to the top tier
    zt = tiers[nt - 1][2] if state != 'Construction' else tiers[0][2]
    if erode:
        zt = tiers[1][2] * 0.6
    y_top = cy - tiers[nt - 1][0] / 2 + (0.45 if not erode else 1.0)
    run = zt * 1.95
    y_bot = y_top - run
    sw = 4.2
    if not erode:
        # side walls of the stair (balustrades) and the solid ramp under it
        for sx in (-1, 1):
            pts = [(sx * (sw / 2 + 0.25), y_bot - 0.3), (sx * (sw / 2 + 0.25), y_top)]
            a0 = Vector((cx + sx * (sw / 2 + 0.25) - 0.25, y_bot - 0.3, FOOT))
            # wall as a sloped prism built from quads
        ramp_poly_l = [(y_bot - 0.4, FOOT), (y_top + 2.0, FOOT), (y_top + 2.0, zt + 0.55), (y_top, zt + 0.55), (y_bot - 0.4, 0.55)]
        _xprism(mb, ramp_poly_l, cx - sw / 2 - 0.5, cx - sw / 2, S, 2.0)
        _xprism(mb, ramp_poly_l, cx + sw / 2, cx + sw / 2 + 0.5, S, 2.0)
        core = [(y_bot - 0.4, FOOT), (y_top + 2.0, FOOT), (y_top + 2.0, zt), (y_top, zt), (y_bot, 0.0)]
        if lod == 2 or state == 'Construction':
            _xprism(mb, core, cx - sw / 2, cx + sw / 2, S, 2.0)
        else:
            nst = int(zt / (0.24 if lod == 0 else 0.6))
            # base wedge below the steps
            _xprism(mb, [(y_bot - 0.4, FOOT), (y_top + 2.0, FOOT), (y_top + 2.0, zt - zt / nst), (y_top, zt - zt / nst), (y_bot, -zt / nst)],
                    cx - sw / 2, cx + sw / 2, S, 2.0)
            for i in range(nst):
                z = (i + 1) * zt / nst
                y0 = y_bot + run * i / nst
                y1 = y_bot + run * (i + 1) / nst
                box(mb, cx, (y0 + y1) / 2 + (y_top - y1) / 2 * 0 + 0.0, z - zt / nst, sw, (y1 - y0) + 0.02, zt / nst, S, ('w', 2.0),
                    top_mat=R, top_uv=('w', 3.0), bottom=False)
    else:
        _xprism(mb, [(y_bot, FOOT), (y_top + 2.0, FOOT), (y_top + 2.0, zt), (y_top, zt - 0.4), (y_bot, 0.2)], cx - sw / 2 - 0.4, cx + sw / 2 + 0.4, S, 2.0)
    # the sanctuary (cella) on top
    if state in ('Intact', 'Damaged') :
        zc = tiers[2][2]
        cW, cD, ch = 8.6, 6.6, 4.2
        ccy = cy + 1.2
        tops = {k: (lambda s: zc + ch) for k in ('front', 'right', 'back', 'left')}
        if state == 'Damaged':
            for k in tops:
                tops[k] = ragged(rng, lambda s: zc + ch * 0.72, 1.4, seed=sd + len(k))
        Lf = cW + 0.6
        ops = {'front': [(Lf / 2 - 0.8, Lf / 2 + 0.8, -10, zc + 2.6)]}
        cruns = build_walls(mb, cW, cD, 0.6, tops, ops, (0.6, 1.5, None)[lod], cy=ccy, base=zc - 0.2)
        if state == 'Intact':
            opening_fill(mb, cruns, 'front', Lf / 2, 1.6, zc, zc + 2.6, 0.6, curtain=False)
            # flat roof with a painted frieze band, parapet, vigas
            G = [[Vector((-cW / 2 - 0.55 + (cW + 1.1) * i / 2, ccy - cD / 2 - 2.6 + (cD + 3.15) * j / 2, zc + ch + 0.35))
                  for j in range(3)] for i in range(3)]
            slab(mb, G, 2, 2, 0.35, R, lambda i, j: (G[i][j].x / 3.0, G[i][j].y / 3.0))
            fz = zc + ch - 0.2
            for side in ('front', 'right', 'back', 'left'):
                c, d, n = run_point(cruns, side, (cW + 0.6 if side in ('front', 'back') else cD + 0.6) / 2, 0.6, -0.02)
                L = (cW + 0.62) if side in ('front', 'back') else (cD + 0.62)
                pts = [c - d * L / 2 + Vector((0, 0, fz - 0.5)), c + d * L / 2 + Vector((0, 0, fz - 0.5)),
                       c + d * L / 2 + Vector((0, 0, fz + 0.15)), c - d * L / 2 + Vector((0, 0, fz + 0.15))]
                if side != 'front':
                    mb.polyt(pts, 'frieze', d)
            # front portico: four painted timber columns under the roof overhang
            yc = ccy - cD / 2 - 2.2
            for xi in (-2.7, -0.9, 0.9, 2.7):
                tube(mb, (xi, yc, zc), (xi, yc, zc + ch + 0.05), 0.24, 0.2, (12, 8, 4)[lod], strip='planks')
                if lod < 2:
                    box(mb, xi, yc, zc - 0.05, 0.7, 0.7, 0.25, D, ('t', 'stone', 1.0))
                    box(mb, xi, yc, zc + ch - 0.2, 0.6, 0.6, 0.25, D, ('t', 'frieze'))
            beam(mb, (-3.5, yc, zc + ch + 0.15), (3.5, yc, zc + ch + 0.15), 0.4, 0.45, 'frieze')
            pp = {k: (lambda s: zc + ch + 0.35 + 0.45) for k in ('front', 'right', 'back', 'left')}
            build_walls(mb, cW + 0.8, cD + 2.85, 0.22, pp, {}, (1.0, 2.5, None)[lod], cy=ccy - 1.3, base=zc + ch + 0.25)
            if lod < 2:
                n = 7
                for k in range(n + 1):
                    y = ccy - cD / 2 + 0.3 + (cD - 0.6) * k / n
                    for sx in (-1, 1):
                        tube(mb, (sx * (cW / 2 - 0.2), y, zc + ch - 0.05), (sx * (cW / 2 + 0.75), y, zc + ch - 0.07), 0.09, 0.085, 5, strip='logs')
            # altar / fire bowl in front of the portico
            box(mb, 0.0, yc - 2.4, zc - 0.1, 1.4, 1.0, 1.1, S, ('w', 2.0), top_mat=R, top_uv=('w', 3.0))
            lathe(mb, 0.0, yc - 2.4, [(0.25, zc + 1.0), (0.55, zc + 1.15), (0.62, zc + 1.4), (0.45, zc + 1.35), (0.0, zc + 1.3)], (12, 8, 6)[lod], D, strip='stone')
            top = zc + ch + 0.9
        else:
            rubble(mb, 1.5, ccy - 0.6, 2.2, 1.2 + zc, rng, tile=2.0)
            rubble(mb, -cW / 2 - 1.0, ccy - cD / 2 - 1.2, 1.1, zc + 0.6, rng, tile=2.0)
            for k in range(3):
                x = -2.7 + k * 1.8
                tube(mb, (x, ccy - cD / 2 - 2.2, zc), (x, ccy - cD / 2 - 2.2, zc + (2.5, 1.2, 3.8)[k]), 0.24, 0.21, (10, 6, 4)[lod], strip='planks')
            tube(mb, (3.0, ccy - cD / 2 - 3.2, zc + 0.25), (5.2, ccy - cD / 2 + 1.0, zc + 0.6), 0.22, 0.2, 7, strip='planks')
            top = zc + ch
        # corner erosion rubble
        if state == 'Damaged':
            rubble(mb, cx + 14.6, cy - 14.4, 3.0, 2.2, rng, tile=2.0)
            rubble(mb, cx - 11.0, cy + 11.4, 2.4, 1.6 + 3.4, rng, tile=2.0)
    elif state == 'Construction':
        if lod < 2:
            # scaffolds, brick stacks, ramp of earth to the half-built tier
            for k in range(6):
                a = -9 + k * 3.6
                for (ox, oy) in ((a, -11.6), (a, -12.6)):
                    tube(mb, (ox, cy + oy + 0.6, FOOT), (ox, cy + oy + 0.6, 5.6), 0.07, 0.06, 5, strip='logs')
                beam(mb, (a, cy - 11.0, 4.9), (a, cy - 12.1, 4.9), 0.1, 0.1, 'planks')
            beam(mb, (-9.2, cy - 11.6, 4.95), (9.2, cy - 11.6, 4.95), 1.0, 0.08, 'planks')
            for k in range(5):
                box(mb, 13.5 - (k % 2) * 1.2, -11 + k * 1.1, 0.0, 1.0, 0.9, 0.4 + 0.3 * (k % 3), S, ('w', 2.0))
            rubble(mb, 16.2, cy + 4.0, 3.2, 3.0, rng, tile=2.0)
        top = tiers[1][1] + 1.6
    else:   # ruin: low cella stumps on the mound
        if lod < 2:
            rubble(mb, cx + 1.0, cy + 1.0, 4.0, tiers[2][2] * 0.72 + 1.2, rng, tile=2.0)
            rubble(mb, cx - 13.0, cy - 10.0, 3.5, 1.5, rng, tile=2.0)
            rubble(mb, cx + 12.0, cy + 13.0, 4.0, 2.2, rng, tile=2.0)
        top = tiers[2][2] * 0.72 + 1.2
    if lod == 0:
        mb.jitter(0.03, 0.4, seed=sd)
    info = dict(W=30.0, D=(cy + 15.0) - (y_bot - 0.4), H=top,
                title='temple: three-tier stepped mudbrick platform, front stair, columned sanctuary with painted frieze')
    return mb, info


def _xprism(mb, poly_yz, x0, x1, mat, tile):
    """Extrude a polygon given in the (y, z) plane from x0 to x1."""
    n = len(poly_yz)
    A = [Vector((x0, y, z)) for (y, z) in poly_yz]
    B = [Vector((x1, y, z)) for (y, z) in poly_yz]
    face(mb, list(reversed(A)), mat, ('w', tile))
    face(mb, B, mat, ('w', tile))
    for i in range(n):
        j = (i + 1) % n
        face(mb, [A[i], A[j], B[j], B[i]], mat, ('w', tile))


def temple_court(var, state, lod):
    """Walled court around the temple (same pivot as SM_Age1_Temple): gate pylons, pool, two tree spots."""
    rng = random.Random(_seed('court', state, lod))
    sd = _seed('court') % 1000
    mb = MB(sd)
    cx, cy = TEMPLE_C
    W, Dp, t, h = 58.0, 52.0, 0.8, 2.3
    ccy = cy - 3.0
    if state == 'Ruin':
        hf = lambda s: 0.7
    elif state == 'Construction':
        hf = lambda s: h * 0.5
    else:
        hf = lambda s: h
    tops = {}
    for k in ('front', 'right', 'back', 'left'):
        base = hf
        if state in ('Ruin', 'Construction'):
            tops[k] = ragged(rng, base, 0.5 if state == 'Ruin' else 0.4, seed=sd + len(k))
        elif state == 'Damaged' and k in ('right', 'back'):
            def dm(s, b=base, k=k):
                gap = 1.0 - min(1.0, abs(s - (18.0 if k == 'right' else 30.0)) / 4.0)
                return b(s) - 2.4 * gap + 0.1 * vnoise(s * 1.7, 0, 0, sd)
            tops[k] = dm
        else:
            tops[k] = lambda s, b=base: b(s) + 0.05 * vnoise(s * 0.8, 0, 0, sd)
    gw = 4.4
    Lf = W + t
    ops = {'front': [(Lf / 2 - gw / 2, Lf / 2 + gw / 2, -10, 10)]}
    step = (1.0, 3.0, None)[lod] if state != 'Intact' else (2.0, 6.0, None)[lod]
    runs = build_walls(mb, W, Dp, t, tops, ops, step, cy=ccy)
    # coping on intact walls, buttresses
    if False:   # buttresses read as dark niches from the game camera; the plain wall matches the concept
        for side, (p0, p1) in runs.items():
            L = (Vector(p1) - Vector(p0)).length
            nb = int(L / 4.0)
            for i in range(1, nb):
                s = L * i / nb
                if side == 'front' and abs(s - Lf / 2) < gw:
                    continue
                c, d, n = run_point(runs, side, s, t, -0.2)
                if tops[side](s) > h * 0.8:
                    box(mb, c.x, c.y, FOOT, 0.7, 0.5, h - 0.1 - FOOT, S, ('w', 2.0), rot=math.atan2(d.y, d.x))
    # gate pylons
    yg = ccy - Dp / 2
    if state != 'Ruin':
        ph = 3.6 if state != 'Construction' else 1.6
        for sx in (-1, 1):
            prism(mb, rect(sx * (gw / 2 + 1.3), yg, 2.6, 2.2), FOOT, ph, S, ('w', 2.0), top_uv=('w', 3.0), top_mat=R,
                  top_poly=rect(sx * (gw / 2 + 1.3), yg, 2.3, 1.9))
        if state in ('Intact', 'Damaged'):
            beam(mb, (-gw / 2 - 1.0, yg, ph - 0.2), (gw / 2 + 1.0, yg, ph - 0.2), 0.5, 0.4, 'frieze')
    # pool: stone kerb, water card, dark sides
    px, py, pw, pd = 15.5, ccy - 19.0, 11.0, 6.5   # verifier fix: was ccy - 13, which cut 5.5 x 2.3 m into the temple's base tier
    if state != 'Ruin':
        kerb = 0.5
        for (bx, by, bw, bd) in ((px, py - pd / 2 - kerb / 2, pw + 2 * kerb, kerb), (px, py + pd / 2 + kerb / 2, pw + 2 * kerb, kerb),
                                 (px - pw / 2 - kerb / 2, py, kerb, pd), (px + pw / 2 + kerb / 2, py, kerb, pd)):
            box(mb, bx, by, FOOT, bw, bd, 0.55 - FOOT, D, ('t', 'stone', 1.0))
        if state != 'Construction':
            wz = 0.05 if state == 'Intact' else -0.25
            box(mb, px, py, FOOT - 0.4, pw + 0.02, pd + 0.02, wz - FOOT + 0.4, D, ('t', 'water'))
    else:
        rubble(mb, px, py, 3.0, 0.4, rng, tile=2.0)
    if lod == 0:
        mb.jitter(0.03, 0.3, seed=sd)
    info = dict(W=W + t, D=Dp + t, H=3.6, title='temple court: mudbrick enclosure with gate pylons and a stone-kerbed pool',
                sockets={'tree_1': [-17.0, ccy - 18.0, 0.0], 'tree_2': [-19.0, ccy + 14.0, 0.0], 'tree_3': [19.0, ccy + 15.0, 0.0],
                         'temple_pivot': [0.0, 0.0, 0.0]})
    return mb, info


# --------------------------------------------------------------------------- palisade, rampart, gate

def palisade_logs(mb, x0, x1, y, zb, lod, rng, state, sd, hmin=3.4, hmax=4.1, gap=None):
    sides = (7, 5, 0)[lod]
    sp = 0.33
    n = int((x1 - x0) / sp)
    if lod == 2:
        # one jagged card-like slab standing in for the logs
        pts = []
        xs = [x0 + (x1 - x0) * k / 16 for k in range(17)]
        tops = []
        for k, x in enumerate(xs):
            hh = (hmax + hmin) / 2
            if state == 'Ruin':
                hh = 0.8
            elif gap and gap[0] < x < gap[1]:
                hh = 0.6
            tops.append(zb + hh + (0.25 if k % 2 else 0.0))
        poly = [(xs[0], zb - 0.6)] + [(xs[-1], zb - 0.6)]
        P = [[Vector((x, y - 0.14, zb - 0.6)), Vector((x, y - 0.14, tz))] for x, tz in zip(xs, tops)]
        slab(mb, P, 16, 1, 0.0, D, lambda i, j: (P[i][j].x / 3.0, 0.16 + 0.12 * j), down=Vector((0, -0.28, 0)))
        return
    for k in range(n):
        x = x0 + sp * (k + 0.5) + rng.uniform(-0.03, 0.03)
        r = rng.uniform(0.13, 0.17)
        hgt = rng.uniform(hmin, hmax)
        tip = True
        lean = Vector((rng.uniform(-0.04, 0.04), rng.uniform(-0.04, 0.04), 0))
        if state == 'Construction' and rng.random() < 0.45 and x > (x0 + x1) / 2 - 1:
            continue
        if state == 'Damaged' and gap and gap[0] < x < gap[1]:
            if rng.random() < 0.6:
                continue
            hgt *= rng.uniform(0.3, 0.6)
            tip = False
            lean = Vector((rng.uniform(-0.6, 0.6), rng.uniform(-0.9, -0.3), 0))
        if state == 'Ruin':
            if rng.random() < 0.35:
                continue
            hgt = rng.uniform(0.3, 1.1)
            tip = False
        b = Vector((x, y, zb - 0.6))
        t0 = Vector((x, y, zb + hgt)) + lean
        tube(mb, b, t0, r, r * 0.95, sides, strip='logs', cap0=False, cap1=not tip)
        if tip:
            tube(mb, t0, t0 + Vector((0, 0, 0.45)) + lean * 0.1, r * 0.95, 0.0, sides, strip='logs', cap0=False, cap1=False)
    if state in ('Intact', 'Damaged') and lod < 2:
        for zr in (1.3, 2.7):
            xa, xb = x0 + 0.1, x1 - 0.1
            if state == 'Damaged' and gap:
                tube(mb, (xa, y + 0.26, zb + zr), (gap[0], y + 0.26, zb + zr), 0.08, 0.08, 5, strip='logs')
                tube(mb, (gap[1], y + 0.26, zb + zr), (xb, y + 0.26, zb + zr), 0.08, 0.08, 5, strip='logs')
            else:
                tube(mb, (xa, y + 0.26, zb + zr), (xb, y + 0.26, zb + zr), 0.08, 0.08, 5, strip='logs')
    if state in ('Construction', 'Damaged', 'Ruin') and lod < 2:
        for k in range(2 if state != 'Ruin' else 3):
            xa = (x0 + x1) / 2 + rng.uniform(-2.0, 2.0)
            a = Vector((xa, y - 1.4 - k * 0.4, 0.15))
            tube(mb, a, a + Vector((rng.uniform(2.4, 3.6), rng.uniform(-0.6, 0.6), 0.05)), 0.14, 0.13, sides, strip='logs')


def bank(mb, x0, x1, base_w, top_w, hgt, lod, sd, ymid=0.0, taper=None, state='Intact'):
    """Earth rampart along X: trapezoid cross-section with noise; taper=(x_start, side) rounds an end down."""
    nx = max(2, int((x1 - x0) / (1.0, 2.5, 5.0)[lod]))
    prof = [(-base_w / 2, FOOT), (-base_w / 2 + 0.3, 0.05), (-top_w / 2, hgt), (top_w / 2, hgt), (base_w / 2 - 0.3, 0.05), (base_w / 2, FOOT)]
    if lod == 2:
        prof = [prof[0], prof[2], prof[3], prof[5]]
    rings = []
    for i in range(nx + 1):
        x = x0 + (x1 - x0) * i / nx
        k = 1.0
        if taper:
            xs, sg = taper
            d = (x - xs) * sg
            if d > 0:
                k = max(0.05, 1.0 - d / (abs(x1 - x0) * 0.6))
        ring = []
        for (yy, zz) in prof:
            z = FOOT + (zz - FOOT) * k if zz > 0 else zz
            q = Vector((x, ymid + yy * (0.6 + 0.4 * k), z))
            if lod == 0 and zz > 0:
                fade = math.sin(math.pi * (i / nx)) if not taper else 1.0
                q.z += 0.12 * vnoise(x * 0.6, yy * 0.6, 0, sd) * fade
                q.y += 0.15 * vnoise(x * 0.5, yy, 1, sd) * fade
            ring.append(q)
        rings.append(ring)
    # ring order along +X; faces between consecutive rings, profile is open (closed below ground by caps)
    m = len(prof)
    for i in range(nx):
        for j in range(m - 1):
            a, b, c, d = rings[i][j], rings[i + 1][j], rings[i + 1][j + 1], rings[i][j + 1]
            face(mb, [a, b, c, d], S, ('w', 4.0))
        # bottom
    face(mb, [rings[0][0], rings[0][-1], rings[-1][-1], rings[-1][0]], S, ('w', 4.0))
    face(mb, list(reversed(rings[0])), S, ('w', 4.0))
    face(mb, rings[-1], S, ('w', 4.0))


def palisade(var, state, lod):
    rng = random.Random(_seed('pal', state, lod))
    sd = _seed('pal') % 1000
    mb = MB(sd)
    L = 8.0
    bank(mb, -L / 2 - 0.05, L / 2 + 0.05, 1.8, 0.7, 0.35 if state != 'Ruin' else 0.2, lod, sd)
    palisade_logs(mb, -L / 2, L / 2, 0.0, 0.3, lod, rng, state, sd, gap=(-1.0, 1.6))
    if lod == 0:
        mb.jitter(0.015, 0.8, seed=sd)
    return mb, dict(W=L, D=1.8, H=4.6, title='timber palisade, 8 m straight segment on a low earth footing (tile along X)')


def rampart(var, state, lod):
    rng = random.Random(_seed('ram', var, state, lod))
    sd = _seed('ram', var) % 1000
    mb = MB(sd)
    L = 10.0
    hb = 2.0 if state not in ('Ruin', 'Construction') else (1.1 if state == 'Ruin' else 1.4)
    bw = 7.0 if state != 'Ruin' else 8.0
    if var == 'Straight':
        bank(mb, -L / 2 - 0.1, L / 2 + 0.1, bw, 2.2, hb, lod, sd)
        palisade_logs(mb, -L / 2, L / 2, -0.35, hb - 0.1, lod, rng, state, sd, gap=(-1.2, 1.4))
    else:   # End: the bank rounds down toward +X, the palisade stops at a big post
        bank(mb, -L / 2 - 0.1, L / 2, bw, 2.2, hb, lod, sd, taper=(0.0, 1))
        palisade_logs(mb, -L / 2, 0.6, -0.35, hb - 0.1, lod, rng, state, sd, gap=(-2.5, -0.5))
        if state != 'Ruin':
            tube(mb, (0.9, -0.35, FOOT), (0.9, -0.35, hb + 4.4), 0.26, 0.24, (8, 6, 4)[lod], strip='logs')
    if lod == 0:
        mb.jitter(0.015, 0.8, seed=sd)
    t = 'earth rampart with a palisade on its crest, 10 m straight segment (tile along X)' if var == 'Straight' else \
        'earth rampart terminus: the bank rounds down toward +X, the palisade ends at a heavy post'
    return mb, dict(W=L, D=bw, H=hb + 4.5, title=t)


def gate(var, state, lod):
    rng = random.Random(_seed('gate', state, lod))
    sd = _seed('gate') % 1000
    mb = MB(sd)
    gw = 4.4
    tw = 3.2
    hb = 2.0 if state != 'Ruin' else 1.1
    # flanking bank ends
    for sx in (-1, 1):
        x0, x1 = sx * (gw / 2 + tw + 0.2), sx * (gw / 2 + tw + 5.0)
        if sx < 0:
            bank(mb, x1, x0 + 0.6, 7.0, 2.2, hb, lod, sd + 1, taper=(x0 - 1.0, 1))
            palisade_logs(mb, x1, x0, -0.35, hb - 0.1, lod, rng, state, sd + 2)
        else:
            bank(mb, x0 - 0.6, x1, 7.0, 2.2, hb, lod, sd + 3, taper=(x0 + 1.0, -1))
            palisade_logs(mb, x0, x1, -0.35, hb - 0.1, lod, rng, state, sd + 4)
    th = 7.6
    top = th
    for sx in (-1, 1):
        cxg = sx * (gw / 2 + tw / 2)
        if state == 'Ruin':
            for (ox, oy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                tube(mb, (cxg + ox * tw / 2 * 0.8, oy * 1.3, FOOT), (cxg + ox * tw / 2 * 0.8, oy * 1.3, rng.uniform(0.5, 1.8)), 0.24, 0.22, (7, 5, 4)[lod], strip='logs')
            continue
        hh = th if not (state == 'Damaged' and sx > 0) else th * 0.55
        if state == 'Construction':
            hh = th if sx < 0 else th * 0.75
        for (ox, oy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            tube(mb, (cxg + ox * tw / 2 * 0.8, oy * 1.3, FOOT), (cxg + ox * tw / 2 * 0.8, oy * 1.3, hh), 0.24, 0.21, (8, 6, 4)[lod], strip='logs')
        if state in ('Intact', 'Construction') or sx < 0:
            zp = 4.6
            box(mb, cxg, 0, zp, tw + 0.3, 3.0, 0.22, D, ('t', 'planks'))
            if state != 'Construction' or sx < 0:
                # wicker breastwork around the platform
                for (bx, by, bw, bd) in ((cxg, -1.5, tw + 0.3, 0.12), (cxg, 1.5, tw + 0.3, 0.12), (cxg - sx * (tw / 2 + 0.1), 0, 0.12, 3.0), (cxg + sx * (tw / 2 + 0.1), 0, 0.12, 3.0)):
                    box(mb, bx, by, zp + 0.2, bw, bd, 1.0, D, ('t', 'wicker'))
            if state == 'Intact' or (state == 'Damaged' and sx < 0):
                top = max(top, thatch_hip(mb, tw - 0.5, 2.6 - 0.5, 0.25, th, math.radians(42), 0.5, 0.25, lod, rng, seed=sd + sx, cx=cxg))
    if state in ('Intact', 'Construction', 'Damaged'):
        # bridge walkway over the gate and the gate leaves
        if state != 'Construction':
            box(mb, 0, 0, 4.6, gw + 0.6, 2.4, 0.22, D, ('t', 'planks'))
            box(mb, 0, -1.2, 4.8, gw + 0.6, 0.12, 1.0, D, ('t', 'wicker'))
        if state == 'Intact':
            for sx in (-1, 1):
                leaf = gw / 2 - 0.05
                m0 = len(mb.V)
                box(mb, -sx * leaf / 2, 0, 0.0, leaf, 0.14, 3.6, D, ('t', 'planks'))   # closed leaf, hinge at the origin
                mb.transform(Matrix.Translation((sx * gw / 2, -0.9, 0.0)) @ Matrix.Rotation(-sx * math.radians(70), 4, 'Z'), m0)
        elif state == 'Damaged':
            m0 = len(mb.V)
            box(mb, 0, 0, 0.0, gw / 2, 0.14, 3.6, D, ('t', 'planks'))
            mb.transform(Matrix.Translation((-1.0, -2.6, 0.08)) @ Matrix.Rotation(math.radians(-88), 4, 'X') @ Matrix.Rotation(0.3, 4, 'Z'), m0)
    if state in ('Damaged', 'Ruin') and lod < 2:
        rubble(mb, 3.5, -3.6, 1.6, 0.6, rng, tile=4.0)
    if lod == 0:
        mb.jitter(0.015, 0.8, seed=sd)
    return mb, dict(W=gw + 2 * tw + 9.6, D=7.0, H=top, title='gate: two timber towers with thatched roofs, walkway and plank leaves, flanked by rampart ends')


# --------------------------------------------------------------------------- fields

def field(var, state, lod):
    """24 x 16 m field plot. Variants are growth stages: Ripe, Growing, Ploughed, Stubble."""
    rng = random.Random(_seed('field', var, lod))
    sd = _seed('field', var) % 1000
    mb = MB(sd)
    W, Dp = 24.0, 16.0
    nu, nv = {0: (12, 8), 1: (6, 4), 2: (2, 2)}[lod]
    if var == 'Ploughed':
        nv = {0: 40, 1: 14, 2: 2}[lod]
    G = []
    for i in range(nu + 1):
        row = []
        for j in range(nv + 1):
            x, y = -W / 2 + W * i / nu, -Dp / 2 + Dp * j / nv
            z = 0.06 + (0.05 * vnoise(x * 0.15, y * 0.15, 0, sd) if lod < 2 else 0.0)
            if var == 'Ploughed' and lod < 2 and 0 < i < nu and 0 < j < nv:
                z += 0.13 if j % 2 else -0.07
            row.append(Vector((x, y, z)))
        G.append(row)
    slab(mb, G, nu, nv, 0.36, S, lambda i, j: (G[i][j].x / 3.0, G[i][j].y / 3.0))
    m = 0.8
    hc = {'Ripe': 1.0, 'Growing': 0.5, 'Stubble': 0.14}.get(var)
    if hc:
        cu, cv = {0: (16, 11), 1: (6, 4), 2: (1, 1)}[lod]
        C = []
        for i in range(cu + 1):
            row = []
            for j in range(cv + 1):
                x = -W / 2 + m + (W - 2 * m) * i / cu
                y = -Dp / 2 + m + (Dp - 2 * m) * j / cv
                z = 0.06 + hc
                if lod < 2:
                    z += hc * (0.08 * vnoise(x * 0.3, y * 0.3, 2, sd) + 0.05 * math.sin(x * 0.7 + y * 0.35))
                    if i in (0, cu) or j in (0, cv):
                        z -= hc * 0.4
                        x += 0.12 * vnoise(x, y, 4, sd) + (0.3 if i == cu else -0.3 if i == 0 else 0)
                        y += 0.12 * vnoise(y, x, 5, sd) + (0.3 if j == cv else -0.3 if j == 0 else 0)
                row.append(Vector((x, y, z)))
            C.append(row)
        slab(mb, C, cu, cv, hc, R, lambda i, j: (C[i][j].x / 2.0, C[i][j].y / 2.0))
    if var == 'Stubble' and lod < 2:
        for i in range(5):
            for j in range(3):
                x = -W / 2 + 3.0 + i * 4.5 + rng.uniform(-0.6, 0.6)
                y = -Dp / 2 + 3.5 + j * 4.5 + rng.uniform(-0.6, 0.6)
                lathe(mb, x, y, [(0.6, 0.0), (0.5, 0.5), (0.28, 1.2), (0.0, 1.45)], (8, 5)[lod], D, strip='grain', noise=0.05, seed=sd + i * 7 + j, ring_uv=True)
    if lod == 0:
        mb.jitter(0.02, 0.4, seed=sd)
    title = {'Ripe': 'field plot, ripe grain (gold)', 'Growing': 'field plot, young green crop',
             'Ploughed': 'field plot, ploughed furrows', 'Stubble': 'field plot, harvested stubble with sheaf stooks'}[var]
    return mb, dict(W=W, D=Dp, H=(hc or 0.15) + 0.2 + (1.45 if var == 'Stubble' else 0), title=title + ', 24 x 16 m (scale freely in XY)')


# --------------------------------------------------------------------------- reed boat landing

def landing(var, state, lod):
    rng = random.Random(_seed('landing', state, lod))
    sd = _seed('landing') % 1000
    mb = MB(sd)
    y_shore, y_end = 6.0, -11.0
    dz = 0.95
    hw = 1.25
    # earth ramp on the shore
    bank(mb, -2.6, 2.6, 0.0, 0.0, 0.0, lod, sd) if False else None
    m0 = len(mb.V)
    bank(mb, -1.6, 3.2, 6.0, 3.0, dz - 0.04, lod, sd, taper=(1.2, 1))
    mb.transform(Matrix.Translation((0, y_shore, 0)) @ Matrix.Rotation(math.pi / 2, 4, 'Z'), m0)
    # posts
    npost = int((y_shore - y_end) / 2.0)
    for k in range(npost + 1):
        y = y_shore - 1.0 - k * 2.0
        if y < y_end:
            break
        for sx in (-1, 1):
            hgt = dz + (0.55 if k % 2 == 0 else 0.0)
            lean = Vector((0, 0, 0))
            if state == 'Ruin':
                hgt = rng.uniform(-0.2, 0.4)
            if state == 'Damaged' and k in (4, 5):
                lean = Vector((sx * 0.4, 0.3, 0))
                hgt = dz - 0.3
            tube(mb, (sx * hw, y, -2.4), Vector((sx * hw, y, hgt)) + lean, 0.13, 0.12, (7, 5, 4)[lod], strip='logs')
    if state != 'Ruin':
        # stringers and deck planks
        y_deck_end = y_end if state != 'Construction' else (y_shore + y_end) / 2
        for sx in (-1, 1):
            if state == 'Damaged':
                tube(mb, (sx * (hw - 0.2), y_shore, dz - 0.16), (sx * (hw - 0.2), -3.0, dz - 0.16), 0.11, 0.11, 6, strip='logs')
                if sx < 0:
                    tube(mb, (sx * (hw - 0.2), -3.0, dz - 0.16), (sx * (hw - 0.6), y_end, 0.1), 0.11, 0.11, 6, strip='logs')
            else:
                tube(mb, (sx * (hw - 0.2), y_shore, dz - 0.16), (sx * (hw - 0.2), y_end, dz - 0.16), 0.11, 0.11, (6, 5, 4)[lod], strip='logs')
        if lod == 0:
            y = y_shore
            k = 0
            while y > y_deck_end:
                w = rng.uniform(0.22, 0.3)
                skip = state == 'Damaged' and (-6.0 < y < -1.0 and rng.random() < 0.55 or y < -6.0 and rng.random() < 0.8)
                if not skip:
                    box(mb, rng.uniform(-0.05, 0.05), y - w / 2, dz - 0.05, 2 * hw + 0.3 + rng.uniform(-0.15, 0.15), w - 0.03, 0.09, D, ('t', 'planks'),
                        rot=rng.uniform(-0.03, 0.03))
                y -= w
                k += 1
        else:
            if state == 'Damaged':
                box(mb, 0, (y_shore - 1.0) / 2, dz - 0.05, 2 * hw + 0.3, y_shore + 1.0, 0.09, D, ('t', 'planks'))
            else:
                box(mb, 0, (y_shore + y_deck_end) / 2, dz - 0.05, 2 * hw + 0.3, y_shore - y_deck_end, 0.09, D, ('t', 'planks'))
    # reed shelter and a drying rack on the shore
    if state in ('Intact', 'Damaged') and lod < 2:
        sx0, sy0 = 4.6, 7.6
        for oy in (-1.4, 1.4):
            for ox in (-1.2, 1.2):
                tube(mb, (sx0 + ox, sy0 + oy, FOOT), (sx0, sy0 + oy, 2.6), 0.06, 0.05, 5, strip='logs')
        if state == 'Intact':
            for sgn in (-1, 1):
                P = [[Vector((sx0 + sgn * 1.45, sy0 - 1.7, 0.05)), Vector((sx0 + sgn * 0.02, sy0 - 1.7, 2.7))],
                     [Vector((sx0 + sgn * 1.45, sy0 + 1.7, 0.05)), Vector((sx0 + sgn * 0.02, sy0 + 1.7, 2.7))]]
                slab(mb, P, 1, 1, 0.08, D, lambda i, j, P=P: (P[i][j].y / 1.0, 0.41 + 0.07 * j))
        rx, ry = -4.6, 7.4
        for oy in (-1.5, 1.5):
            tube(mb, (rx, ry + oy, FOOT), (rx, ry + oy, 2.0), 0.06, 0.05, 5, strip='logs')
        tube(mb, (rx, ry - 1.7, 1.95), (rx, ry + 1.7, 1.95), 0.05, 0.05, 5, strip='logs')
        if state == 'Intact':
            for k in range(3):
                box(mb, rx + 0.02 * k, ry - 1.2 + k * 1.0, 0.9 - 0.15 * k, 0.03, 0.8, 1.0 + 0.1 * k, D, ('t', 'hide'))
    if lod == 0:
        mb.jitter(0.01, 0.8, seed=sd)
    return mb, dict(W=9.0, D=(y_shore + 3.2) - y_end, H=2.7,
                    title='reed boat landing: timber jetty on posts, earth ramp, reed shelter and net rack (Z=0 = waterline)',
                    sockets={'mooring_1': [-2.4, -6.0, 0.0], 'mooring_2': [2.4, -9.0, 0.0], 'shore_end': [0.0, y_shore + 3.2, 0.0]})


def reed_boat(var, state, lod):
    """Lashed reed-bundle boat; Z = 0 is the waterline. Built bow toward +X, then turned to face -Y."""
    sd = _seed('boat', var) % 1000
    mb = MB(sd)
    L, Rr, curl = (6.6, 0.62, 1.25) if var == 'A' else (4.4, 0.46, 0.8)
    nseg = (28, 12, 6)[lod]
    sides = (12, 8, 5)[lod]
    def section(s_):
        x = s_ * L / 2
        z = 0.08 + curl * abs(s_) ** 3.0
        r = Rr * max(0.04, 1 - abs(s_) ** 2.4) ** 0.6
        return x, z, r
    def ring_at(s_, scale=1.0):
        x, z, r = section(s_)
        out = []
        for j in range(sides):
            a = 2 * math.pi * j / sides
            yy = math.cos(a) * r * 1.25 * scale
            zz = math.sin(a) * r * 0.6 * scale
            if zz < 0:
                zz *= 0.7          # flatter bottom
            out.append(Vector((x, yy, z + zz)))
        return out
    rings = [ring_at(-1 + 2 * i / nseg) for i in range(nseg + 1)]
    loft(mb, rings, D, 'tube', strip='reed')
    for ring, tip, rev in ((rings[0], Vector((-L / 2 - 0.12, 0, section(-1)[1] + 0.12)), True),
                           (rings[-1], Vector((L / 2 + 0.12, 0, section(1)[1] + 0.12)), False)):
        for j in range(sides):
            j1 = (j + 1) % sides
            pts = [ring[j], ring[j1], tip] if not rev else [ring[j1], ring[j], tip]
            mb.polyt(pts, 'reed', (1, 0, 0))
    if lod < 2:
        nl = 9 if var == 'A' else 6
        for k in range(nl):
            s_ = -0.86 + 1.72 * k / (nl - 1)
            loft(mb, [ring_at(s_ - 0.012, 1.06), ring_at(s_ + 0.012, 1.06)], D, 'tube', strip='wicker')
        # deck mat, paddle and a basket
        box(mb, 0, 0, Rr * 0.55, L * 0.42, Rr * 1.4, 0.05, D, ('t', 'wicker'))
        beam(mb, (-0.9, -0.35, Rr * 0.62 + 0.06), (1.1, 0.4, Rr * 0.62 + 0.06), 0.06, 0.04, 'planks')
        box(mb, 1.3, 0.45, Rr * 0.6 + 0.04, 0.5, 0.18, 0.03, D, ('t', 'planks'), rot=0.36)
        if var == 'A':
            lathe(mb, -1.2, 0.1, [(0.0, Rr * 0.6), (0.25, Rr * 0.6), (0.3, Rr * 0.6 + 0.35), (0.27, Rr * 0.6 + 0.4)], 8, D, strip='wicker', cap_top=True)
    # verifier fix: built along +X, turned so the bow faces -Y like every other asset and the Ship family
    mb.transform(Matrix.Rotation(-math.pi / 2, 4, 'Z'))
    return mb, dict(W=Rr * 2.5, D=L + 0.25, H=curl + 0.6, title='reed boat %s: lashed reed bundles, upswept ends, bow toward -Y (Z=0 = waterline)' % ('large' if var == 'A' else 'small'))


# --------------------------------------------------------------------------- culture ornaments

def ornament(var, state, lod):
    sd = _seed('orn', var) % 1000
    mb = MB(sd)
    if var == 'GableHorns':
        # crossed carved boards at a gable end (Lagoon-Weaver style upswept points); pivot = ridge end point
        for sx in (-1, 1):
            a = Vector((0, sx * -0.9, -0.85))
            b = Vector((0, sx * 0.55, 0.55))
            beam(mb, a, b, 0.07, 0.2, 'planks')
            c = b + Vector((0, sx * 0.25, 0.4))
            tube(mb, b, c, 0.09, 0.02, (6, 4, 3)[lod], strip='planks')
        info = dict(W=0.3, D=2.2, H=1.0, title='ornament: crossed gable horns (socket ridge_end_*)')
    else:
        # clay pot finial with a disc (Ring-Builder style); pivot = ridge end point
        lathe(mb, 0, 0, [(0.0, -0.1), (0.18, -0.05), (0.26, 0.2), (0.16, 0.42), (0.1, 0.5), (0.2, 0.56), (0.0, 0.62)], (10, 7, 5)[lod], R, tile=1.0)
        info = dict(W=0.5, D=0.5, H=0.62, title='ornament: clay pot ridge finial (socket ridge_end_*)')
    return mb, info
