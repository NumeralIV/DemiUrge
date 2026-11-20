# DEMIURGE art - Age4 industrial kit: chimney stack, factories, warehouses, rail station, mine headframe,
# spoil heaps, bridges, culture ornaments. All generators take an LOD (0..3) and return an a4geo.Asm.
import math
import random

from mathutils import Vector

import a4geo as G
from a4geo import S, R, D, Opening, v3

LODSIDES = (28, 14, 8, 6)


# --------------------------------------------------------------------------- helpers

def shift(asm, dx, dy, dz=0.0):
    off = Vector((dx, dy, dz))
    for p in asm.parts:
        p.v = [(q[0] + dx, q[1] + dy, q[2] + dz) for q in p.v]
        p.vd = {}
    asm.sockets = [(n, Vector(pos) + off, k) for (n, pos, k) in asm.sockets]
    for k in ('blobs',):
        if asm.info.get(k):
            asm.info[k] = [((c[0] + dx, c[1] + dy, c[2] + dz), r, s, q) for (c, r, s, q) in asm.info[k]]
    return asm


def member(asm, p0, p1, w, h=None, band='iron', tags=(), up=None):
    p = G.Part('detail', ('beam', 'lattice') + tuple(tags), band)
    G.beam(p, p0, p1, w, w if h is None else h, D, band, up=up)
    asm.add(p)


def bays(L, n, w, z0, z1, tile, rng, accent=0.0, skip=(), sill=True, lintel='stone', start=None):
    """n evenly spaced openings of width w along a facade of length L."""
    out = []
    pitch = L / n
    for i in range(n):
        if i in skip:
            continue
        c = (start if start is not None else pitch * 0.5) + pitch * i
        out.append(Opening(c - w / 2, c + w / 2, z0, z1, tile, rnd=rng.random(), accent=accent, sill=sill, lintel=lintel))
    return out


def pilasters(asm, x0, x1, y0, y1, z0, z1, pitch_s, pitch_e, lod, w=0.6, out=0.18, sides='SNEW'):
    """Brick pilasters at bay divisions (they give mills their vertical rhythm)."""
    if lod > 0:
        return
    p = G.Part('solid', ('pilaster', 'body'))
    if 'S' in sides or 'N' in sides:
        n = int(round((x1 - x0) / pitch_s))
        for i in range(n + 1):
            x = x0 + (x1 - x0) * i / n
            if 'S' in sides:
                G.block(p, x - w / 2, x + w / 2, y0 - out, y0 + 0.01, z0, z1)
            if 'N' in sides:
                G.block(p, x - w / 2, x + w / 2, y1 - 0.01, y1 + out, z0, z1)
    if 'E' in sides or 'W' in sides:
        n = int(round((y1 - y0) / pitch_e))
        for i in range(1, n):
            y = y0 + (y1 - y0) * i / n
            if 'W' in sides:
                G.block(p, x0 - out, x0 + 0.01, y - w / 2, y + w / 2, z0, z1)
            if 'E' in sides:
                G.block(p, x1 - 0.01, x1 + out, y - w / 2, y + w / 2, z0, z1)
    asm.add(p)


def stack_parts(asm, cx, cy, H, lod, r0=2.0, r1=1.25, base=5.5, tags=(), name='Smoke'):
    sides = LODSIDES[lod]
    b = r0 + 0.7
    pl = G.Part('solid', ('stack', 'body') + tuple(tags))
    G.block(pl, cx - b, cx + b, cy - b, cy + b, -0.3, base)
    asm.add(pl)
    if lod <= 1:
        cap = G.Part('detail', ('stackband',), 'stone')
        G.block(cap, cx - b - 0.15, cx + b + 0.15, cy - b - 0.15, cy + b + 0.15, base, base + 0.45, D, band='stone')
        asm.add(cap)
    sh = G.Part('solid', ('stack', 'shaft') + tuple(tags))
    zc = H - 2.6
    G.cylinder(sh, (cx, cy, base), r0, r1, zc - base, sides, S, caps=True)
    asm.add(sh)
    cr = G.Part('solid', ('stack', 'crown') + tuple(tags))
    char = lambda q: G.planar_tile_uv(q, 'char', cx - r1 - 0.5, cy - r1 - 0.5, cx + r1 + 0.5, cy + r1 + 0.5)   # noqa: E731
    if lod <= 2:
        prof = [(r1 + 0.02, 0.0), (r1 + 0.42, 0.8), (r1 + 0.42, 1.2), (r1 + 0.3, 2.6)]
    else:
        prof = [(r1, 0.0), (r1 + 0.3, 2.6)]
    G.lathe(cr, (cx, cy, zc), prof, sides, S, top_mat=D, top_uv=char, top_col=(0, 1.0, 0, 1))
    asm.add(cr)
    for k in range(len(cr.f)):
        pts = [cr.v[i] for i in cr.f[k]]
        if min(p[2] for p in pts) > H - 4.5:
            c = cr.fcol[k]
            cr.fcol[k] = (c[0], max(c[1], 0.6), c[2], 1.0)
    if lod == 0:
        bands = G.Part('detail', ('stackband',), 'stone')
        for f in (0.33, 0.62, 0.86):
            z = base + (zc - base) * f
            r = r0 + (r1 - r0) * (z - base) / (zc - base)
            G.cylinder(bands, (cx, cy, z), r + 0.09, r + 0.08, 0.45, sides, D, band='stone')
        asm.add(bands)
    asm.sockets.append(('%s_%d' % (name, len([s for s in asm.sockets if s[2] == 'smoke'])), v3(cx, cy, H + 0.5), 'smoke'))


def stack(lod):
    asm = G.Asm()
    stack_parts(asm, 0, 0, 52.0, lod, 2.3, 1.35, 6.0)
    asm.info.update(eave=52.0, ridge=52.0, con_frac=0.45, ruin_hf=(lambda x, y: 9.0 + 6.0 * G.fbm2(x * 0.5, y * 0.5, 5)),
                    blobs=[((0.7, -0.4, 51.5), 2.8, 77, 1.6)], n_rubble=4, rubble_size=(3.0, 6.0), hole_scale=1.2)
    return asm


# --------------------------------------------------------------------------- sawtooth roof

def sawtooth(asm, x0, x1, y0, y1, ze, n, lod, rise=3.6, glaze_frac=0.16, tags=('roof',)):
    """n teeth across X, ridges along Y, glazed steep faces toward +X. Gable infills are the caps (brick)."""
    tw = (x1 - x0) / n
    for i in range(n):
        p = G.Part('solid', tags, 'lead')
        xs = x0 + tw * i
        xe = xs + tw
        xp = xe - tw * glaze_frac
        prof = [(xs, ze - 0.35), (xe, ze - 0.35), (xe, ze), (xp, ze + rise), (xs, ze)]
        mats = [S, D, D, R, D]
        uvs = [None, ('band', 'lead'), ('band', 'glazing'), None, ('band', 'lead')]
        G.extrude_profile(p, prof, lambda a, z, s: v3(a, s, z), y0, y1, mats, S, uvs)
        asm.add(p)
    if lod == 0:
        cp = G.Part('detail', ('roof', 'coping'), 'stone')
        for i in range(n):
            xs = x0 + tw * i
            xe = xs + tw
            xp = xe - tw * glaze_frac
            for y in (y0, y1):
                G.beam(cp, (xs, y, ze + 0.1), (xp, y, ze + rise + 0.12), 0.25, 0.42, D, 'stone', up=(0, 0, 1))
                G.beam(cp, (xp, y, ze + rise + 0.12), (xe, y, ze + 0.1), 0.25, 0.42, D, 'stone', up=(0, 0, 1))
        asm.add(cp)
    return ze + rise


# --------------------------------------------------------------------------- factory

def factory(variant, lod):
    asm = G.Asm()
    rng = random.Random(41 if variant == 'A' else 43)
    big = variant == 'A'
    # main mill
    mx0, mx1 = (-27.0, 27.0) if big else (-18.0, 18.0)
    my0, my1 = (-13.0, 13.0) if big else (-10.0, 10.0)
    ze = 11.0 if big else 7.2
    nb = 12 if big else 8
    Lx, Ly = mx1 - mx0, my1 - my0
    if big:
        win = lambda L, n, skip=(): bays(L, n, 1.7, 1.0, 4.4, 'factorywin', rng, skip=skip) + bays(L, n, 1.7, 5.9, 9.6, 'factorywin', rng)   # noqa: E731
    else:
        win = lambda L, n, skip=(): bays(L, n, 1.9, 1.0, 5.4, 'factorywin', rng, skip=skip)   # noqa: E731
    front = win(Lx, nb, skip=(2, nb - 3)) + [Opening(Lx / nb * 2.5 - 1.6, Lx / nb * 2.5 + 1.6, 0.15, 3.9, 'loading', rnd=rng.random(), sill=False, lintel='iron'),
                                            Opening(Lx / nb * (nb - 2.5) - 1.6, Lx / nb * (nb - 2.5) + 1.6, 0.15, 3.9, 'loading', rnd=rng.random(), sill=False, lintel='iron')]
    ops = {'S': front, 'N': win(Lx, nb), 'E': win(Ly, 5 if big else 4), 'W': win(Ly, 5 if big else 4, skip=(2,))}
    wall, dress, dec = G.body(mx0, mx1, my0, my1, -0.3, ze, ops if lod <= 2 else {}, lod)
    asm.add([wall, dress, dec])
    pilasters(asm, mx0, mx1, my0, my1, -0.3, ze + 0.2, Lx / nb, Ly / (5 if big else 4), lod)
    if lod <= 1:
        asm.add(G.band_ring(mx0, mx1, my0, my1, -0.3, 0.6, 0.04, 'stone', ('plinth',)))
    if lod == 0:
        asm.add(G.band_ring(mx0, mx1, my0, my1, ze - 0.5, ze - 0.1, 0.22, 'stone', ('cornice',)))
        if big:
            asm.add(G.band_ring(mx0, mx1, my0, my1, 5.0, 5.3, 0.06, 'stone'))
    nteeth = 6 if big else 4
    zr = sawtooth(asm, mx0, mx1, my0, my1, ze, nteeth, lod, rise=3.8 if big else 3.2)
    asm.info.update(eave=ze, ridge=zr)
    # engine / boiler house (west), gable to the front
    ex0, ex1, ey0, ey1, eze = (mx0 - 14.0, mx0, -9.0, 5.0, 9.0) if big else (mx0 - 10.0, mx0, -7.0, 3.0, 7.5)
    e_ops = {'S': bays(ex1 - ex0, 3, 1.5, 1.2, 6.8 if big else 5.6, 'factorywin', rng),
             'W': bays(ey1 - ey0, 3, 1.5, 1.2, 6.8 if big else 5.6, 'factorywin', rng),
             'N': [Opening(2.0, 5.0, 0.15, 3.6, 'loading', rnd=rng.random(), sill=False, lintel='iron')]}
    ew, ed, edc = G.body(ex0, ex1 + 0.3, ey0, ey1, -0.3, eze, e_ops if lod <= 2 else {}, lod)
    asm.add([ew, ed, edc])
    er, _ = G.gable_roof(ex0, ex1, ey0, ey1, eze, 30, ox=0.25, oy=0.3, ridge_x=False)
    asm.add(er)
    if lod <= 1:
        # roof ventilator (louvred lantern on the ridge)
        lv = G.Part('solid', ('roof', 'lantern'))
        ezr = eze + ((ex1 - ex0) / 2 + 0.3) * math.tan(math.radians(30))
        cx = (ex0 + ex1) / 2
        G.block(lv, cx - 1.0, cx + 1.0, ey0 + 3, ey1 - 3, ezr - 0.6, ezr + 1.0)
        asm.add(lv)
        lr, _ = G.gable_roof(cx - 1.0, cx + 1.0, ey0 + 3, ey1 - 3, ezr + 1.0, 30, ox=0.25, oy=0.15, ridge_x=False)
        asm.add(lr)
    # office block (front east), hipped
    if big:
        ox0, ox1, oy0, oy1, oze = 13.0, 27.0, -19.5, -13.0, 7.6
        o_ops = {'S': bays(ox1 - ox0, 5, 1.1, 1.0, 2.8, 'sash2', rng, skip=(2,)) + bays(ox1 - ox0, 5, 1.1, 4.6, 6.4, 'sash2', rng)
                 + [Opening((ox1 - ox0) / 2 - 0.65, (ox1 - ox0) / 2 + 0.65, 0.15, 2.9, 'door_red', rnd=rng.random(), accent=1.0, sill=False)],
                 'W': bays(oy1 - oy0, 2, 1.1, 1.0, 2.8, 'sash2', rng) + bays(oy1 - oy0, 2, 1.1, 4.6, 6.4, 'sash2', rng),
                 'E': bays(oy1 - oy0, 2, 1.1, 4.6, 6.4, 'sash2', rng)}
        ow, od, odc = G.body(ox0, ox1, oy0, oy1 + 0.3, -0.3, oze, o_ops if lod <= 2 else {}, lod)
        asm.add([ow, od, odc])
        orf, _ = G.gable_roof(ox0, ox1, oy0, oy1, oze, 32, ox=0.35, oy=0.35, hip=True)
        asm.add(orf)
        if lod <= 1:
            asm.add(G.band_ring(ox0, ox1, oy0, oy1, -0.3, 0.5, 0.04, 'stone', ('plinth',)))
        # loading canopy on the east side
        cp = G.Part('solid', ('roof', 'canopy'))
        if lod <= 2:
            asm.add(G.lean_roof(mx1, mx1 + 4.5, -9.0, 9.0, 4.6, 5.4, 'E', ox=0.0, oy=0.0, fascia=0.15))
            if lod <= 1:
                for y in (-8.5, -3, 3, 8.5):
                    member(asm, (mx1 + 4.3, y, 0), (mx1 + 4.3, y, 4.5), 0.25, band='iron')
    # stacks behind the mill: the tallest elements, three on the big mill
    if big:
        for (sx, sy, H) in ((-19.0, my1 + 6.0, 50.0), (1.5, my1 + 7.0, 58.0), (20.0, my1 + 6.0, 46.0)):
            stack_parts(asm, sx, sy, H, lod, 2.0, 1.2, 5.0)
    else:
        stack_parts(asm, mx1 - 5.0, my1 + 5.5, 44.0, lod, 1.9, 1.15, 5.0)
    # water tower (big)
    if big:
        tx, ty = mx1 + 7.0, my1 + 2.0
        leg_top = 14.0
        r = 3.1
        for (a, b) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            member(asm, (tx + a * 2.8, ty + b * 2.8, -0.3), (tx + a * 2.2, ty + b * 2.2, leg_top), 0.35, band='iron')
        if lod <= 1:
            for z in (4.7, 9.4):
                k = 2.8 - 0.6 * z / leg_top
                pts = [(tx - k, ty - k), (tx + k, ty - k), (tx + k, ty + k), (tx - k, ty + k)]
                for i in range(4):
                    a, b = pts[i], pts[(i + 1) % 4]
                    member(asm, (a[0], a[1], z), (b[0], b[1], z), 0.18, band='iron')
            if lod == 0:
                for (z0, z1) in ((0.0, 4.7), (4.7, 9.4), (9.4, 14.0)):
                    k0, k1 = 2.8 - 0.6 * z0 / leg_top, 2.8 - 0.6 * z1 / leg_top
                    for (ax, ay, bx, by) in ((-1, -1, 1, -1), (1, -1, 1, 1), (1, 1, -1, 1), (-1, 1, -1, -1)):
                        member(asm, (tx + ax * k0, ty + ay * k0, z0), (tx + bx * k1, ty + by * k1, z1), 0.1, band='iron')
        tk = G.Part('solid', ('tank', 'body'))
        sides = LODSIDES[min(lod + 1, 3)]
        G.cylinder(tk, (tx, ty, leg_top), r, r, 4.4, sides, D, band='rust', caps=True)
        asm.add(tk)
        cone = G.Part('solid', ('roof', 'tankroof'))
        G.cylinder(cone, (tx, ty, leg_top + 4.4), r + 0.15, 0.25, 1.6, sides, D, band='lead', caps=True)
        asm.add(cone)
    asm.info['n_holes'] = 5 if big else 3
    asm.info['hole_scale'] = 1.6
    asm.info['con_frac'] = 0.6
    asm.info['n_rubble'] = 9 if big else 6
    asm.info['rubble_size'] = (4.0, 8.0)
    # break the top of one stack when damaged
    if big:
        b = [((1.5 + 0.6, my1 + 7.0 - 0.4, 57.5), 2.6, 91, 1.6)]
    else:
        b = [((mx1 - 5.0 + 0.6, my1 + 5.5 - 0.4, 43.5), 2.6, 91, 1.6)]
    rr = random.Random(5)
    for k in range(asm.info['n_holes']):
        b.append(((rr.uniform(mx0 + 4, mx1 - 4), rr.uniform(my0 + 3, my1 - 3), ze + rr.uniform(0.5, 3.0)), rr.uniform(2.4, 4.0), 200 + k, 1.8))
    b.append(((mx0 + 3, my0, ze - 2.0), 2.8, 301, 1.0))
    asm.info['blobs'] = b
    # centre the footprint
    bb = asm.bbox()
    return shift(asm, -(bb[0] + bb[1]) / 2, -(bb[2] + bb[3]) / 2)


# --------------------------------------------------------------------------- warehouses

def warehouse(variant, lod):
    asm = G.Asm()
    rng = random.Random(51 if variant == 'A' else 53)
    if variant == 'A':
        x0, x1, y0, y1 = -15.0, 15.0, -8.0, 8.0
        ns, sh = 5, 3.4
        ze = 0.15 + ns * sh + 0.4
        L = x1 - x0
        nb = 9
        mid = nb // 2
        front = []
        for s in range(ns):
            zf = 0.15 + s * sh
            if s == 0:
                front += bays(L, nb, 1.2, zf + 0.9, zf + 2.4, 'sash2', rng, skip=(mid, 1, nb - 2))
                for i in (1, nb - 2):
                    c = L / nb * (i + 0.5)
                    front.append(Opening(c - 1.3, c + 1.3, 0.15, 3.0, 'loading', rnd=rng.random(), sill=False, lintel='iron'))
                c = L / nb * (mid + 0.5)
                front.append(Opening(c - 1.4, c + 1.4, 0.15, 3.1, 'loading', rnd=rng.random(), sill=False, lintel='iron'))
            else:
                tiles = ['sash2' if rng.random() < 0.75 else 'boarded' for _ in range(nb)]
                for i in range(nb):
                    c = L / nb * (i + 0.5)
                    if i == mid:
                        front.append(Opening(c - 1.15, c + 1.15, zf + 0.15, zf + 2.75, 'loading', rnd=rng.random(), sill=False, lintel='iron'))
                    else:
                        front.append(Opening(c - 0.55, c + 0.55, zf + 0.9, zf + 2.3, tiles[i], rnd=rng.random()))
        back = []
        for s in range(ns):
            zf = 0.15 + s * sh
            back += bays(L, nb, 1.1, zf + 0.9, zf + 2.3, 'sash2' if s else 'boarded', rng)
        side = []
        for s in range(ns):
            zf = 0.15 + s * sh
            side += bays(y1 - y0, 4, 1.0, zf + 0.9, zf + 2.3, 'sash2', rng)
        ops = {'S': front, 'N': back, 'E': side, 'W': side}
        wall, dress, dec = G.body(x0, x1, y0, y1, -0.3, ze, ops if lod <= 2 else {}, lod)
        asm.add([wall, dress, dec])
        pilasters(asm, x0, x1, y0, y1, -0.3, ze, L / nb, (y1 - y0) / 4, lod, w=0.5, out=0.14, sides='SN')
        if lod <= 1:
            asm.add(G.band_ring(x0, x1, y0, y1, -0.3, 0.7, 0.04, 'stone', ('plinth',)))
        if lod == 0:
            asm.add(G.band_ring(x0, x1, y0, y1, ze - 0.5, ze - 0.15, 0.2, 'stone', ('cornice',)))
            for s in range(1, ns):
                asm.add(G.band_ring(x0, x1, y0, y1, 0.15 + s * sh - 0.05, 0.15 + s * sh + 0.12, 0.03, 'stone'))
        # double-pile roof: two parallel ridges along X (an M roof) reads as a warehouse from above
        r1_, zr = G.gable_roof(x0, x1, y0, 0.0, ze, 34, ox=0.2, oy=0.3)
        r2_, _ = G.gable_roof(x0, x1, 0.0, y1, ze, 34, ox=0.2, oy=0.3)
        asm.add([r1_, r2_])
        # cathead (projecting hoist housing) over the loading doors
        c = x0 + L / nb * (mid + 0.5)
        if lod <= 2:
            ch = G.Part('solid', ('body', 'cathead'))
            G.block(ch, c - 1.6, c + 1.6, y0 - 1.4, y0 + 0.5, ze - 2.4, ze + 1.2)
            asm.add(ch)
            cr, _ = G.gable_roof(c - 1.6, c + 1.6, y0 - 1.4, y0 + 0.5, ze + 1.2, 40, ox=0.15, oy=0.15, ridge_x=False)
            asm.add(cr)
            member(asm, (c, y0 - 1.3, ze - 2.3), (c, y0 - 2.6, ze - 2.3), 0.3, band='iron')
            if lod <= 1:
                # hoist chain
                member(asm, (c, y0 - 2.5, ze - 2.4), (c, y0 - 2.5, 3.6), 0.06, band='iron')
        asm.info.update(eave=ze, ridge=zr, n_holes=3, hole_scale=1.4, con_frac=0.5, n_rubble=6, rubble_size=(4.0, 7.0))
        for i, x in enumerate((x0 + 3.0, x1 - 3.0)):
            asm.sockets.append(('Ornament_Gable_%d' % i, v3(x0 if i == 0 else x1, -4.0, zr + 0.2), 'ornament'))
        return asm
    # B: long goods shed with platform canopy and siding
    x0, x1, y0, y1 = -20.0, 20.0, -7.0, 7.0
    ze = 6.6
    L = x1 - x0
    rng2 = rng
    front = []
    for i in range(4):
        c = L / 4 * (i + 0.5)
        front.append(Opening(c - 1.8, c + 1.8, 0.15, 4.2, 'loading', rnd=rng2.random(), sill=False, lintel='iron'))
        for dc in (-3.6, 3.6):
            front.append(Opening(c + dc - 0.6, c + dc + 0.6, 2.2, 4.6, 'factorywin', rnd=rng2.random()))
    back = [Opening(L / 8 * (2 * i + 1) - 1.6, L / 8 * (2 * i + 1) + 1.6, 1.15, 4.4, 'loading', rnd=rng2.random(), sill=False, lintel='iron') for i in range(4)]
    side = bays(y1 - y0, 2, 1.2, 2.0, 4.6, 'factorywin', rng2)
    ops = {'S': front, 'N': back, 'E': side, 'W': side}
    wall, dress, dec = G.body(x0, x1, y0, y1, -0.3, ze, ops if lod <= 2 else {}, lod)
    asm.add([wall, dress, dec])
    pilasters(asm, x0, x1, y0, y1, -0.3, ze, L / 8, (y1 - y0) / 2, lod, w=0.55, out=0.15, sides='SNEW')
    if lod <= 1:
        asm.add(G.band_ring(x0, x1, y0, y1, -0.3, 0.6, 0.04, 'stone', ('plinth',)))
    rf, zr = G.gable_roof(x0, x1, y0, y1, ze, 24, ox=0.4, oy=0.6)
    asm.add(rf)
    # rooflight strips on both slopes
    if lod <= 1:
        gl = G.Part('detail', ('roof', 'rooflight'), 'glazing')
        t = math.tan(math.radians(24))
        for sgn in (-1, 1):
            ya, yb = sgn * 1.2, sgn * 3.4
            za, zb_ = zr - abs(ya) * t + 0.06, zr - abs(yb) * t + 0.06
            q = [v3(x0 + 2, ya, za), v3(x1 - 2, ya, za), v3(x1 - 2, yb, zb_), v3(x0 + 2, yb, zb_)]
            if sgn < 0:
                q = [q[0], q[3], q[2], q[1]]
            gl.add(q, D, G.band_uv(q, 'glazing', (1, 0, 0)))
        asm.add(gl)
    # raised loading platform + canopy on the north (rail) side
    pf = G.Part('solid', ('platform',))
    G.block(pf, x0 - 1.0, x1 + 1.0, y1, y1 + 4.0, -0.3, 1.1, S, mat_top=S)
    asm.add(pf)
    if lod <= 1:
        cpp = G.Part('detail', ('coping',), 'stone')
        G.block(cpp, x0 - 1.05, x1 + 1.05, y1 + 3.6, y1 + 4.08, 1.1, 1.22, D, band='stone')
        asm.add(cpp)
    if lod <= 2:
        asm.add(G.lean_roof(x0 - 0.5, x1 + 0.5, y1, y1 + 4.6, 4.9, 5.9, 'N', ox=0.0, oy=0.0, fascia=0.15))
        if lod <= 1:
            for i in range(9):
                x = x0 + L * i / 8
                member(asm, (x, y1 + 0.05, 5.0), (x, y1 + 4.4, 4.95), 0.18, 0.3, band='iron')
                member(asm, (x, y1 + 0.05, 3.2), (x, y1 + 2.0, 4.95), 0.12, band='iron')
    # siding track along the platform
    track(asm, x0 - 6, x1 + 6, y1 + 6.0, lod)
    asm.info.update(eave=ze, ridge=zr, n_holes=3, hole_scale=1.3, con_frac=0.6, n_rubble=6)
    bb = asm.bbox()
    return shift(asm, -(bb[0] + bb[1]) / 2, -(bb[2] + bb[3]) / 2)


def track(asm, xa, xb, y, lod, z=0.0, gauge=1.435):
    """Standard-gauge track along X: ballast bed with sleepers (trackbed band) and two steel rails. Rail top = z."""
    if lod >= 3:
        return
    tb = G.Part('detail', ('track',), 'trackbed')
    G.block(tb, xa, xb, y - 1.6, y + 1.6, z - 0.45, z - 0.17, D, band='trackbed')
    asm.add(tb)
    if lod <= 1:
        rl = G.Part('detail', ('track', 'rail'), 'iron')
        for dy in (-gauge / 2, gauge / 2):
            G.beam(rl, (xa, y + dy, z - 0.09), (xb, y + dy, z - 0.09), 0.08, 0.17, D, 'iron')
        asm.add(rl)


# --------------------------------------------------------------------------- rail station

def station(lod):
    asm = G.Asm()
    rng = random.Random(61)
    X0, X1 = -32.0, 32.0
    ys, yn = -6.0, 26.0           # shed walls
    zs = 9.0                      # springing
    rise = 12.0
    yc = (ys + yn) / 2
    half = (yn - ys) / 2
    nseg = (18, 12, 8, 6)[lod]
    # ---------------- north shed wall with arched (tall) windows
    nw_ops = {'N': bays(X1 - X0, 8, 2.0, 1.6, 7.2, 'factorywin', rng), 'S': bays(X1 - X0, 8, 2.0, 1.6, 7.2, 'factorywin', rng)}
    wn, dn, dcn = G.body(X0, X1, yn, yn + 0.9, -0.3, zs, nw_ops if lod <= 2 else {}, lod)
    asm.add([wn, dn, dcn])
    pilasters(asm, X0, X1, yn, yn + 0.9, -0.3, zs + 0.3, (X1 - X0) / 8, 10, lod, w=0.8, out=0.25, sides='N')
    # ---------------- barrel vault (glazed crown, slated haunches)
    def arc(t, r_add=0.0):
        a = math.pi * t
        return (yc - (half + r_add) * math.cos(a), zs + (rise + r_add * rise / half) * math.sin(a))
    outer = [arc(i / nseg, 0.0) for i in range(nseg + 1)]
    inner = [arc(i / nseg, -0.35) for i in range(nseg + 1)]
    prof = outer + inner[::-1]
    mats, uvs = [], []
    nglass0 = int(nseg * 0.22)
    for i in range(nseg):
        if nglass0 <= i < nseg - nglass0:
            mats.append(D)
            uvs.append(('band', 'glazing'))
        else:
            mats.append(R)
            uvs.append(None)
    mats.append(D)
    uvs.append(('band', 'iron'))
    for i in range(nseg):
        mats.append(S)
        uvs.append(None)
    mats.append(D)
    uvs.append(('band', 'iron'))
    vault = G.Part('solid', ('roof', 'vault'), 'iron')
    G.extrude_profile(vault, prof, lambda a, z, s: v3(s, a, z), X0 - 1.0, X1 + 1.0, mats, D, uvs,
                      cap_uv=lambda q: G.band_uv(q, 'iron'))
    asm.add(vault)
    # inner face of the vault: dark iron (the underside is seen through the glazing)
    for fi in range(len(vault.f)):
        if vault.fm[fi] == S:
            vault.fm[fi] = D
            vault.fuv[fi] = G.band_uv([vault.v[i] for i in vault.f[fi]], 'iron', (1, 0, 0))
    # iron ribs (arched girders) protruding above the roof skin
    nr = (9, 5, 3, 0)[lod]
    rseg = (12, 8, 6, 4)[lod]
    for k in range(nr):
        x = X0 - 0.5 + (X1 - X0 + 1.0) * k / max(1, nr - 1)
        pts = [arc(i / rseg, 0.12) for i in range(rseg + 1)]
        for i in range(rseg):
            a, b = pts[i], pts[i + 1]
            member(asm, (x, a[0], a[1]), (x, b[0], b[1]), 0.35, 0.45, band='iron', up=(1, 0, 0))
    # glazed end screens (lunettes) above the train openings
    if lod <= 2:
        for xe, sgn in ((X0 - 0.9, -1), (X1 + 0.9, 1)):
            sc = G.Part('solid', ('screen', 'window'), 'iron')
            pts = [arc(i / nseg, -0.36) for i in range(nseg + 1)]

            def to3(a, z, s):
                return v3(s, a, z)
            lu = lambda q: G.tile_uv(q, 'lunette', v3(0, ys, zs), (0, 1, 0), (0, 0, 1), yn - ys, rise)   # noqa: E731
            G.extrude_profile(sc, pts, to3, xe - 0.12, xe + 0.12, [D] * len(pts), D,
                              [('band', 'iron')] * len(pts), cap_uv=lu)
            for fi in range(len(sc.f)):
                sc.fcol[fi] = (rng.random(), 0, 0, 1)
            asm.add(sc)
            # lower iron valance girder across the opening
            member(asm, (xe, ys, zs - 0.2), (xe, yn, zs - 0.2), 0.4, 0.9, band='iron')
    # columns along the platforms supporting nothing visible - skip; arcade piers at the open ends
    for xe in (X0 - 0.6, X1 + 0.6):
        pr = G.Part('solid', ('body', 'pier'))
        G.block(pr, xe - 0.8, xe + 0.8, yn - 0.2, yn + 1.3, -0.3, zs + 0.4)
        asm.add(pr)
    # ---------------- station building (front, -Y)
    bx0, bx1, by0, by1 = -30.0, 30.0, -18.0, ys
    bze = 10.4
    rngb = rng
    s_ops = []
    L = bx1 - bx0
    nbays = 15
    for i in range(nbays):
        c = L / nbays * (i + 0.5)
        if abs(c - L / 2) < 6.5:
            continue
        s_ops.append(Opening(c - 0.7, c + 0.7, 1.0, 4.0, 'sash2', rnd=rngb.random()))
        s_ops.append(Opening(c - 0.65, c + 0.65, 6.0, 8.8, 'sash2', rnd=rngb.random()))
    wb, db, dcb = G.body(bx0, bx1, by0, by1, -0.3, bze, {'S': s_ops, 'E': bays(by1 - by0, 3, 1.3, 1.0, 4.0, 'sash2', rngb) + bays(by1 - by0, 3, 1.2, 6.0, 8.8, 'sash2', rngb),
                                                      'W': bays(by1 - by0, 3, 1.3, 1.0, 4.0, 'sash2', rngb) + bays(by1 - by0, 3, 1.2, 6.0, 8.8, 'sash2', rngb)} if lod <= 2 else {}, lod)
    asm.add([wb, db, dcb])
    if lod <= 1:
        asm.add(G.band_ring(bx0, bx1, by0, by1, -0.3, 0.7, 0.05, 'stone', ('plinth',)))
    if lod == 0:
        asm.add(G.band_ring(bx0, bx1, by0, by1, 5.0, 5.35, 0.06, 'stone'))
        asm.add(G.band_ring(bx0, bx1, by0, by1, bze - 0.55, bze - 0.1, 0.25, 'stone', ('cornice',)))
    roof, zr = G.gable_roof(bx0, bx1, by0, by1, bze, 30, ox=0.3, oy=0.3, hip=True)
    asm.add(roof)
    # end pavilions, a storey higher, hipped
    for (px0, px1) in ((bx0 - 0.6, bx0 + 7.0), (bx1 - 7.0, bx1 + 0.6)):
        pv_ops = {'S': bays(px1 - px0, 2, 1.2, 1.0, 4.0, 'sash2', rngb) + bays(px1 - px0, 2, 1.1, 6.0, 8.8, 'sash2', rngb) + bays(px1 - px0, 2, 1.0, 10.6, 12.4, 'sash2', rngb)}
        pw, pd, pdc = G.body(px0, px1, by0 - 0.8, by1 - 1.0, -0.3, 13.6, pv_ops if lod <= 2 else {}, lod)
        asm.add([pw, pd, pdc])
        pr_, _ = G.gable_roof(px0, px1, by0 - 0.8, by1 - 1.0, 13.6, 38, ox=0.35, oy=0.35, hip=True)
        asm.add(pr_)
        if lod == 0:
            asm.add(G.band_ring(px0, px1, by0 - 0.8, by1 - 1.0, 13.0, 13.45, 0.25, 'stone', ('cornice',)))
    # central entrance block projecting forward with three tall glazed doors
    ex0, ex1 = -6.5, 6.5
    e_ops = {'S': [Opening(1.6 + 3.6 * i, 3.6 + 3.6 * i, 0.15, 4.6, 'shop', rnd=rngb.random(), sill=False) for i in range(3)] +
             [Opening(1.7 + 3.6 * i, 3.5 + 3.6 * i, 6.0, 9.6, 'sash2', rnd=rngb.random()) for i in range(3)]}
    ew, ed, edc = G.body(ex0, ex1, by0 - 2.0, by0 + 0.5, -0.3, 11.6, e_ops if lod <= 2 else {}, lod)
    asm.add([ew, ed, edc])
    er_, _ = G.gable_roof(ex0, ex1, by0 - 2.0, by0 + 3.0, 11.6, 34, ox=0.3, oy=0.3, ridge_x=False)
    asm.add(er_)
    # clock tower
    tx, ty, tw = 0.0, (by0 + by1) / 2 + 1.0, 2.8
    tz0, tz1 = bze - 1.0, 27.0
    t_ops = {}
    if lod <= 1:
        for sd in ('S', 'N', 'E', 'W'):
            t_ops[sd] = [Opening(tw - 0.8, tw + 0.8, tz1 - 8.8, tz1 - 5.8, 'louvre', rnd=0.0, sill=True)]
    tw_, td, tdc = G.body(tx - tw, tx + tw, ty - tw, ty + tw, tz0, tz1, t_ops, lod)
    asm.add([tw_, td, tdc])
    if lod <= 2:
        ck = G.Part('detail', ('clock',), 'stone')
        for sd in ('S', 'N', 'E', 'W'):
            org, dirv, nrm, Ls = G.side_frame(sd, tx - tw, tx + tw, ty - tw, ty + tw)
            c = org + dirv * tw + Vector((0, 0, tz1 - 2.6)) + nrm * 0.12
            uax = dirv
            q = [c - uax * 1.25 - Vector((0, 0, 1.25)), c + uax * 1.25 - Vector((0, 0, 1.25)), c + uax * 1.25 + Vector((0, 0, 1.25)), c - uax * 1.25 + Vector((0, 0, 1.25))]
            ck.add(q, D, G.tile_uv(q, 'clock', q[0], uax, (0, 0, 1), 2.5, 2.5), (0.0, 0, 0, 1))
        asm.add(ck)
        if lod <= 1:
            asm.add(G.band_ring(tx - tw, tx + tw, ty - tw, ty + tw, tz1 - 0.6, tz1, 0.25, 'stone', ('cornice',)))
    trf, tzr = G.gable_roof(tx - tw, tx + tw, ty - tw, ty + tw, tz1, 62, ox=0.35, oy=0.35, hip=True)
    asm.add(trf)
    asm.sockets.append(('Ornament_Spire_0', v3(tx, ty, tzr + 0.1), 'ornament'))
    if lod <= 1:
        member(asm, (tx, ty, tzr - 0.3), (tx, ty, tzr + 2.6), 0.12, band='iron', tags=('ornament',))
    # porte-cochere canopy (iron and glass) in front of the entrance
    if lod <= 2:
        cy0, cy1 = by0 - 6.5, by0 - 2.0
        can = G.Part('solid', ('roof', 'canopy'), 'iron')
        G.block(can, -8.0, 8.0, cy0, cy1, 4.3, 4.6, D, band='iron')
        asm.add(can)
        if lod <= 1:
            gl = G.Part('detail', ('roof', 'rooflight'), 'glazing')
            q = [v3(-7.6, cy0 + 0.4, 4.62), v3(7.6, cy0 + 0.4, 4.62), v3(7.6, cy1 - 0.3, 4.62), v3(-7.6, cy1 - 0.3, 4.62)]
            gl.add(q, D, G.band_uv(q, 'glazing', (1, 0, 0)))
            asm.add(gl)
            for x in (-7.5, -2.5, 2.5, 7.5):
                member(asm, (x, cy0 + 0.3, 0.0), (x, cy0 + 0.3, 4.3), 0.28, band='iron')
    # ---------------- platforms and tracks
    plats = [(ys, ys + 4.5), (2.6, 7.4), (14.6, 19.4)]
    for (pa, pb) in plats:
        pf = G.Part('solid', ('platform',))
        G.block(pf, X0 - 2.0, X1 + 6.0, pa, pb, -0.3, 0.95)
        asm.add(pf)
        if lod <= 1:
            ed_ = G.Part('detail', ('coping',), 'stone')
            for yy in ((pa, pa + 0.5) if pa > ys else ()), (pb - 0.5, pb):
                if yy:
                    G.block(ed_, X0 - 2.0, X1 + 6.0, yy[0], yy[1], 0.95, 1.02, D, band='stone')
            asm.add(ed_)
    for ty_ in (0.75, 9.25, 12.75, 21.25):
        track(asm, X0 - 14.0, X1 + 14.0, ty_, lod)
    asm.info.update(eave=bze, ridge=zr + 8.0, n_holes=4, hole_scale=1.8, con_frac=0.55, n_rubble=10, rubble_size=(4.0, 8.0))
    rr = random.Random(9)
    asm.info['blobs'] = [((rr.uniform(-25, 25), rr.uniform(4, 18), zs + rise * 0.9), rr.uniform(3.5, 5.5), 400 + k, 0.8) for k in range(4)] + \
                        [((18.0, by0 + 3.0, bze + 1.0), 3.2, 410, 1.0), ((tx + 2.0, ty, tz1 + 1.0), 2.2, 411, 1.2)]
    bb = asm.bbox()
    return shift(asm, -(bb[0] + bb[1]) / 2, -(bb[2] + bb[3]) / 2)


# --------------------------------------------------------------------------- mine headframe

def wheel(asm, c, r, lod):
    n = (16, 10, 8, 0)[lod]
    if n == 0:
        return
    pts = [v3(c[0] + r * math.cos(2 * math.pi * i / n), c[1], c[2] + r * math.sin(2 * math.pi * i / n)) for i in range(n)]
    for i in range(n):
        member(asm, pts[i], pts[(i + 1) % n], 0.22, 0.28, band='iron', up=(0, 1, 0), tags=('wheel',))
    if lod <= 1:
        for i in range(0, n, 2):
            member(asm, c, pts[i], 0.1, 0.1, band='iron', tags=('wheel',))
    member(asm, (c[0], c[1] - 0.4, c[2]), (c[0], c[1] + 0.4, c[2]), 0.45, 0.45, band='iron', tags=('wheel',))


def minehead(lod):
    asm = G.Asm()
    rng = random.Random(71)
    sx = -12.0                 # shaft
    top = 22.0
    bw, tw = 2.8, 1.5
    legs = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    for (a, b) in legs:
        member(asm, (sx + a * bw, b * bw, -0.3), (sx + a * tw, b * tw, top), 0.5, band='iron')
    levels = [0.0, 5.5, 11.0, 16.5, top] if lod <= 1 else [0.0, 11.0, top]
    if lod <= 2:
        for z in levels[1:]:
            k = bw + (tw - bw) * z / top
            pts = [(sx - k, -k), (sx + k, -k), (sx + k, k), (sx - k, k)]
            for i in range(4):
                a, b = pts[i], pts[(i + 1) % 4]
                member(asm, (a[0], a[1], z), (b[0], b[1], z), 0.3 if z == top else 0.22, band='iron')
        if lod <= 1:
            for li in range(len(levels) - 1):
                z0, z1 = levels[li], levels[li + 1]
                k0, k1 = bw + (tw - bw) * z0 / top, bw + (tw - bw) * z1 / top
                faces = [((-1, -1), (1, -1)), ((1, -1), (1, 1)), ((1, 1), (-1, 1)), ((-1, 1), (-1, -1))]
                for fi, (a, b) in enumerate(faces):
                    member(asm, (sx + a[0] * k0, a[1] * k0, z0), (sx + b[0] * k1, b[1] * k1, z1), 0.13, band='iron')
                    if lod == 0:
                        member(asm, (sx + b[0] * k0, b[1] * k0, z0), (sx + a[0] * k1, a[1] * k1, z1), 0.13, band='iron')
    # head platform and sheave wheels
    pl = G.Part('solid', ('lattice', 'platform'), 'iron')
    G.block(pl, sx - tw - 0.6, sx + tw + 0.6, -tw - 0.6, tw + 0.6, top, top + 0.35, D, band='iron')
    asm.add(pl)
    if lod <= 1:
        rail = G.Part('detail', ('lattice', 'beam'), 'iron')
        for (a, b) in ((( -1, -1), (1, -1)), ((1, 1), (-1, 1))):
            G.beam(rail, (sx + a[0] * (tw + 0.55), a[1] * (tw + 0.55), top + 1.2), (sx + b[0] * (tw + 0.55), b[1] * (tw + 0.55), top + 1.2), 0.06, 0.06, D, 'iron')
        asm.add(rail)
    wr = 2.4
    for wy in (-0.95, 0.95):
        wheel(asm, (sx + 0.2, wy, top + 0.35 + wr + 0.1), wr, lod)
    # back-stays raking toward the winding house
    bs_foot = 6.0
    for b in (-1, 1):
        member(asm, (sx + tw, b * tw, top), (sx + bs_foot + 10.0, b * 3.2, -0.3), 0.55, band='iron')
        if lod <= 1:
            for t in (0.33, 0.66):
                p0 = Vector((sx + tw, b * tw, top)).lerp(Vector((sx + bs_foot + 10.0, b * 3.2, -0.3)), t)
                member(asm, p0, (sx + tw, b * tw, top * (1 - t) * 0.85), 0.16, band='iron')
    if lod <= 2:
        for t in (0.3, 0.65):
            pa = Vector((sx + tw, -tw, top)).lerp(Vector((sx + bs_foot + 10.0, -3.2, -0.3)), t)
            member(asm, pa, (pa.x, -pa.y, pa.z), 0.2, band='iron')
    # heapstead (shed around the shaft collar)
    h_ops = {'S': bays(9.0, 2, 1.4, 1.2, 3.4, 'factorywin', rng), 'W': [Opening(2.5, 5.5, 0.15, 3.6, 'loading', rnd=rng.random(), sill=False, lintel='iron')]}
    hw, hd, hdc = G.body(sx - 4.5, sx + 4.5, -4.2, 4.2, -0.3, 6.2, h_ops if lod <= 2 else {}, lod, tags=('body', 'heapstead'))
    asm.add([hw, hd, hdc])
    hr, _ = G.gable_roof(sx - 4.5, sx + 4.5, -4.2, 4.2, 6.2, 22, ox=0.3, oy=0.3)
    asm.add(hr)
    # winding engine house: tall, gable toward the headframe, big arched windows
    wx0, wx1, wy0, wy1, wze = 6.0, 20.0, -6.0, 6.0, 10.5
    w_ops = {'W': [Opening(3.0, 5.0, 2.0, 8.2, 'factorywin', rnd=rng.random()), Opening(7.0, 9.0, 2.0, 8.2, 'factorywin', rnd=rng.random())],
             'S': bays(wx1 - wx0, 4, 1.6, 1.6, 7.6, 'factorywin', rng), 'N': bays(wx1 - wx0, 4, 1.6, 1.6, 7.6, 'factorywin', rng),
             'E': [Opening(4.5, 7.5, 0.15, 3.8, 'loading', rnd=rng.random(), sill=False, lintel='iron')]}
    ww, wd, wdc = G.body(wx0, wx1, wy0, wy1, -0.3, wze, w_ops if lod <= 2 else {}, lod)
    asm.add([ww, wd, wdc])
    wr_, wzr = G.gable_roof(wx0, wx1, wy0, wy1, wze, 32, ox=0.35, oy=0.35)
    asm.add(wr_)
    pilasters(asm, wx0, wx1, wy0, wy1, -0.3, wze, (wx1 - wx0) / 4, 12.0, lod, w=0.6, out=0.18, sides='SN')
    if lod <= 1:
        asm.add(G.band_ring(wx0, wx1, wy0, wy1, -0.3, 0.7, 0.05, 'stone', ('plinth',)))
    if lod == 0:
        asm.add(G.band_ring(wx0, wx1, wy0, wy1, wze - 0.45, wze - 0.1, 0.22, 'stone', ('cornice',)))
    # winding ropes from the drum to the sheaves
    if lod <= 2:
        for wy in (-0.95, 0.95):
            member(asm, (wx0 + 0.5, wy, 7.0), (sx + 0.2 + wr * 0.7, wy, top + 0.35 + wr + 0.1 + wr * 0.7), 0.07, band='iron', tags=('rope',))
    # boiler house and stack
    bh_ops = {'S': bays(10.0, 3, 1.4, 1.2, 4.0, 'factorywin', rng)}
    bw_, bd, bdc = G.body(wx1, wx1 + 10.0, -4.5, 4.5, -0.3, 6.0, bh_ops if lod <= 2 else {}, lod)
    asm.add([bw_, bd, bdc])
    asm.add(G.lean_roof(wx1, wx1 + 10.0, -4.5, 4.5, 6.0, 8.0, 'E', ox=0.2, oy=0.3))
    stack_parts(asm, wx1 + 13.5, 3.0, 34.0, lod, 1.5, 0.95, 4.0)
    # ore bin on a timber trestle beside the heapstead
    if lod <= 2:
        bx, by = sx, -9.5
        for (a, b) in legs:
            member(asm, (bx + a * 2.0, by + b * 1.6, -0.3), (bx + a * 2.0, by + b * 1.6, 4.5), 0.35, band='timber', tags=('bin',))
        bn = G.Part('solid', ('body', 'bin'), 'timber')
        G.prism(bn, [(bx - 2.4, by - 2.0), (bx + 2.4, by - 2.0), (bx + 2.4, by + 2.0), (bx - 2.4, by + 2.0)], 4.5, 8.0, D, band='timber')
        asm.add(bn)
        if lod <= 1:
            bnr = G.Part('solid', ('roof',), 'lead')
            G.prism(bnr, [(bx - 2.7, by - 2.3), (bx + 2.7, by - 2.3), (bx + 2.7, by + 2.3), (bx - 2.7, by + 2.3)], 8.0, 8.25, D, band='lead')
            asm.add(bnr)
            # gantry from the heapstead to the bin
            member(asm, (bx - 0.8, -4.2, 5.2), (bx - 0.8, by + 2.0, 6.0), 0.15, band='timber')
            member(asm, (bx + 0.8, -4.2, 5.2), (bx + 0.8, by + 2.0, 6.0), 0.15, band='timber')
    # narrow-gauge tub track out of the heapstead toward the tip
    if lod <= 1:
        tr = G.Part('detail', ('track',), 'trackbed')
        G.block(tr, sx - 22.0, sx - 4.6, -1.0, 1.0, -0.3, -0.12, D, band='trackbed')
        asm.add(tr)
    asm.sockets.append(('Smoke_Winding', v3(wx0 + 7, 0, wzr + 0.5), 'steam'))
    asm.info.update(eave=wze, ridge=wzr, n_holes=3, hole_scale=1.5, con_frac=0.55, n_rubble=7, rubble_size=(3.5, 7.0),
                    ruin_hf=lambda x, y: 2.5 + 4.0 * G.fbm2(x * 0.15, y * 0.15, 71, 3))
    asm.info['blobs'] = [((sx, 0.0, top + 2.0), 3.2, 500, 1.0), ((sx + 2.0, -2.0, 12.0), 2.0, 501, 1.0),
                         ((13.0, -3.0, wze + 1.5), 3.0, 502, 1.0), ((wx1 + 13.5 + 0.5, 3.0 - 0.4, 33.5), 2.2, 503, 1.6)]
    bb = asm.bbox()
    dx, dy = -(bb[0] + bb[1]) / 2, -(bb[2] + bb[3]) / 2
    asm.info['shaft'] = (sx + dx, dy, top, bw, tw)
    return shift(asm, dx, dy)


def _minehead_ruin(asm, lod):
    """Ruin: the lattice headframe lies toppled along the tub track, away from the engine house, instead of
    being cut away to 3 m stumps. The ruin had lost the one silhouette that says 'colliery' (verifier
    addition, 2026-11-19)."""
    if lod >= 3 or 'shaft' not in asm.info:
        return
    sx, sy, top, bw, tw = asm.info['shaft']
    ang, yaw = math.radians(-93.4), math.radians(18.0)   # -93.4: both the foot and the head rest on the ground
    piv = Vector((sx - bw, sy, 0.0))
    ca, sa, cy_, sy_ = math.cos(ang), math.sin(ang), math.cos(yaw), math.sin(yaw)

    def T(q):
        x, y, z = q[0] - piv.x, q[1] - piv.y, q[2]
        x, z = x * ca + z * sa, -x * sa + z * ca
        x, y = x * cy_ - y * sy_, x * sy_ + y * cy_
        return Vector((x + piv.x, y + piv.y, z + 0.3))
    legs = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    for (a, b) in legs:
        member(asm, T((sx + a * bw, sy + b * bw, 0.0)), T((sx + a * tw, sy + b * tw, top)), 0.5, band='rust', tags=('fallen',))
    levels = [5.5, 11.0, 16.5, top] if lod <= 1 else [11.0, top]
    for z in levels:
        k = bw + (tw - bw) * z / top
        pts = [(sx - k, sy - k), (sx + k, sy - k), (sx + k, sy + k), (sx - k, sy + k)]
        for i in range(4):
            a, b = pts[i], pts[(i + 1) % 4]
            member(asm, T((a[0], a[1], z)), T((b[0], b[1], z)), 0.22, band='rust', tags=('fallen',))
    if lod <= 1:
        for li in range(len(levels) - 1):
            z0, z1 = levels[li], levels[li + 1]
            k0, k1 = bw + (tw - bw) * z0 / top, bw + (tw - bw) * z1 / top
            for (a, b) in (((-1, -1), (1, -1)), ((1, 1), (-1, 1))):
                member(asm, T((sx + a[0] * k0, sy + a[1] * k0, z0)), T((sx + b[0] * k1, sy + b[1] * k1, z1)), 0.13, band='rust', tags=('fallen',))
    # one sheave wheel thrown clear, lying flat
    wheel_c = T((sx, sy, top + 2.0))
    wheel_c = Vector((wheel_c.x - 3.0, wheel_c.y - 3.5, 0.25))
    ring = G.Part('detail', ('fallen', 'wheel'), 'iron')
    n = 12 if lod <= 1 else 8
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        G.beam(ring, (wheel_c.x + 2.4 * math.cos(a0), wheel_c.y + 2.4 * math.sin(a0), 0.25),
               (wheel_c.x + 2.4 * math.cos(a1), wheel_c.y + 2.4 * math.sin(a1), 0.25), 0.2, 0.3, D, 'rust')
    asm.add(ring)


# --------------------------------------------------------------------------- spoil heaps (tiling gravel; MIs make coal / ore)

def spoil(variant, lod):
    asm = G.Asm()
    p = G.Part('solid', ('heap',))
    if variant == 'A':
        rx, ry, h = 22.0, 17.0, 15.0
        rings, sides = (16, 9, 4, 2)[lod], (44, 26, 12, 8)[lod]
    else:
        rx, ry, h = 7.0, 5.5, 4.2
        rings, sides = (6, 4, 2, 1)[lod], (18, 12, 8, 6)[lod]
    rng = random.Random(81 if variant == 'A' else 83)
    p.begin()
    rows = []
    for ri in range(rings + 1):
        rho = 1.0 - ri / rings
        row = []
        for j in range(sides):
            a = 2 * math.pi * j / sides
            gul = 0.5 + 0.5 * math.sin(a * 11 + 1.3 + 2.0 * G.vnoise(a * 2, rho * 3, 5))   # erosion gullies
            jit = 1.0 + 0.2 * (G.vnoise(math.cos(a) * 2.2 + 7, math.sin(a) * 2.2, 11) - 0.5) * 2
            x = rx * rho * math.cos(a) * jit
            y = ry * rho * math.sin(a) * jit
            # B: a tipped coal / ore heap is a cone at the angle of repose (about 32-37 deg here) with a rounded
            # crest and a slightly concave toe. It was (1 - rho^2)^0.8, a dome that read as a boulder
            # (verifier fix, 2026-11-19).
            prof = (1 - rho ** 1.4) ** 0.85 if variant == 'A' else (1 - rho ** 1.25) ** 1.15
            z = h * prof * (1.0 - 0.07 * gul * (1 - rho) * rho * 4) if ri > 0 else 0.0
            if ri > 0:
                z *= 1.0 + 0.12 * (G.vnoise(x * 0.25 + 3, y * 0.25, 13) - 0.5) * 2 * min(1.0, rho * 3)
            if variant == 'A':
                z = min(z, h * 0.86 + 0.6 * G.vnoise(x * 0.3, y * 0.3, 17))   # tipped plateau
            row.append(v3(x, y, z))
        rows.append(row)
    topc = v3(0, 0, sum(r.z for r in rows[-1]) / len(rows[-1]) + (0.15 if variant == 'A' else 0.3))

    def uv(q):
        return [(pt[0] / 4.0, pt[1] / 4.0) for pt in q]
    for ri in range(rings):
        for j in range(sides):
            k = (j + 1) % sides
            q = [rows[ri][j], rows[ri][k], rows[ri + 1][k], rows[ri + 1][j]]
            p.add(q, S, uv(q))
    for j in range(sides):
        k = (j + 1) % sides
        q = [rows[rings][j], rows[rings][k], topc]
        p.add(q, S, uv(q))
    for j in range(sides):
        k = (j + 1) % sides
        a, b = rows[0][j], rows[0][k]
        q = [v3(a.x, a.y, -0.5), v3(b.x, b.y, -0.5), b, a]
        p.add(q, S, uv(q))
    bot = [v3(r.x, r.y, -0.5) for r in rows[0]][::-1]
    p.add(bot, S, uv(bot))
    p.end_solid()
    asm.add(p)
    if variant == 'A' and lod <= 1:
        # tipping track up the flank and a tub tippler on top
        tr = G.Part('detail', ('track',), 'timber')
        G.beam(tr, (rx + 8.0, 0.0, -0.2), (rx * 0.25, 0.0, h * 0.93 + 0.4), 1.6, 0.35, D, 'timber')
        asm.add(tr)
        if lod == 0:
            for t in (0.25, 0.5, 0.75):
                pt = Vector((rx + 8.0, 0.0, -0.2)).lerp(Vector((rx * 0.25, 0.0, h * 0.93 + 0.4)), t)
                member(asm, (pt.x, 0.0, -0.3), (pt.x, 0.0, pt.z), 0.3, band='timber')
    return asm


# --------------------------------------------------------------------------- bridges (pivot: deck centre at rail level)

def truss_bridge(lod):
    asm = G.Asm()
    span = 20.0            # half span between pier faces
    pier_h = 1.5           # half-pier thickness at each end
    yt = 4.7               # truss planes
    zb, zt = -0.65, 6.6    # chord centre lines
    npan = 8
    xs = [-span + 2 * span * i / npan for i in range(npan + 1)]
    top_x0, top_x1 = xs[1], xs[-2]
    ch = 0.7 if lod <= 1 else 0.9
    for y in (-yt, yt):
        member(asm, (xs[0] - 0.4, y, zb), (xs[-1] + 0.4, y, zb), ch, 1.0, band='iron', tags=('chord',))
        member(asm, (top_x0, y, zt), (top_x1, y, zt), ch, 0.9, band='iron', tags=('chord',))
        member(asm, (xs[0], y, zb), (top_x0, y, zt), ch, 0.9, band='iron', tags=('endpost',))
        member(asm, (xs[-1], y, zb), (top_x1, y, zt), ch, 0.9, band='iron', tags=('endpost',))
        if lod <= 2:
            for i in range(1, npan):
                member(asm, (xs[i], y, zb), (xs[i], y, zt), 0.4 if lod <= 1 else 0.55, 0.5, band='iron')
            for i in range(1, npan - 1):
                if xs[i] < 0:
                    a, b = (xs[i], zt), (xs[i + 1], zb)
                else:
                    a, b = (xs[i + 1], zt), (xs[i], zb)
                member(asm, (a[0], y, a[1]), (b[0], y, b[1]), 0.35 if lod <= 1 else 0.5, 0.45, band='iron')
            if lod == 0:
                # counters in the two centre panels
                c = npan // 2
                member(asm, (xs[c - 1], y, zb), (xs[c], y, zt), 0.22, 0.3, band='iron')
                member(asm, (xs[c + 1], y, zb), (xs[c], y, zt), 0.22, 0.3, band='iron')
    # top lateral struts and portal bracing
    if lod <= 2:
        for i in range(1, npan):
            member(asm, (xs[i], -yt, zt + 0.2), (xs[i], yt, zt + 0.2), 0.3, 0.45, band='iron')
        if lod == 0:
            for i in range(1, npan - 1):
                member(asm, (xs[i], -yt, zt + 0.3), (xs[i + 1], yt, zt + 0.3), 0.14, band='iron')
                if lod == 0:
                    member(asm, (xs[i], yt, zt + 0.3), (xs[i + 1], -yt, zt + 0.3), 0.14, band='iron')
            for x in (top_x0, top_x1):
                member(asm, (x, -yt, zt - 1.6), (x, yt, zt - 1.6), 0.25, 0.5, band='iron')
    # floor beams, deck plate, tracks
    if lod == 0:
        for x in xs:
            member(asm, (x, -yt - 0.3, zb - 0.1), (x, yt + 0.3, zb - 0.1), 0.35, 0.8, band='iron')
    dk = G.Part('solid', ('deck',), 'iron')
    G.block(dk, -span - pier_h, span + pier_h, -yt + 0.3, yt - 0.3, -0.75, -0.42, D, band='iron', mat_top=R)
    asm.add(dk)
    for ty in (-2.0, 2.0):
        track(asm, -span - pier_h, span + pier_h, ty, lod)
    # half piers at both ends (adjacent spans complete each other); masonry with a stone cap
    for sgn in (-1, 1):
        x0p, x1p = (span, span + pier_h) if sgn > 0 else (-span - pier_h, -span)
        pr = G.Part('solid', ('pier', 'body'))
        G.prism(pr, [(x0p, -6.2), (x1p, -6.2), (x1p, 6.2), (x0p, 6.2)], -16.0, -1.25, S)
        asm.add(pr)
        if lod <= 2:
            cw = G.Part('solid', ('pier', 'cutwater'))
            for yy, s2 in ((-6.2, -1), (6.2, 1)):
                G.prism(cw, [(x0p, yy), (x1p, yy), ((x0p + x1p) / 2 + (0.0), yy + s2 * 1.6)] if True else [], -16.0, -2.5, S)
            asm.add(cw)
        if lod <= 1:
            cap = G.Part('detail', ('pier',), 'stone')
            G.block(cap, x0p - (0.2 if sgn < 0 else 0), x1p + (0.2 if sgn > 0 else 0), -6.5, 6.5, -1.25, -0.8, D, band='stone')
            asm.add(cap)
    asm.info.update(eave=6.6, ridge=7.0, n_holes=2, hole_scale=1.2)
    asm.info['blobs'] = [((6.0, -yt, 3.0), 2.6, 600, 1.0), ((-11.0, yt, 5.5), 2.2, 601, 1.0), ((2.0, 0.0, -0.6), 1.8, 602, 1.0)]
    return asm


def _bridge_a_construction(asm, lod):
    G._apply_cut(asm, lambda x, y: 60.0 if x < 1.0 else -1.3, drop_tags=('track',), lod=lod)
    # timber falsework trestles under the unbuilt half
    if lod <= 2:
        for x in (5.0, 10.0, 15.0):
            for y in (-4.7, 4.7):
                member(asm, (x, y, -16.0), (x, y, -1.0), 0.45, band='timber')
            member(asm, (x, -5.2, -1.0), (x, 5.2, -1.0), 0.5, band='timber')
            if lod <= 1:
                member(asm, (x, -4.7, -16.0), (x, 4.7, -6.0), 0.25, band='timber')
                member(asm, (x, 4.7, -16.0), (x, -4.7, -6.0), 0.25, band='timber')
        member(asm, (1.0, -4.7, -1.0), (20.0, -4.7, -1.0), 0.4, band='timber')
        member(asm, (1.0, 4.7, -1.0), (20.0, 4.7, -1.0), 0.4, band='timber')


def _bridge_a_ruin(asm, lod):
    """The span has broken off the far pier and lies tilted into the river."""
    piv = Vector((-20.0, 0.0, -1.0))
    ang = math.radians(-21.0)
    ca, sa = math.cos(ang), math.sin(ang)
    out = []
    for p in asm.parts:
        if 'pier' in p.tags:
            out.append(p)
            continue
        if 'track' in p.tags and lod >= 1:
            continue

        def rot(q):
            d = q - piv
            return Vector((piv.x + d.x * ca - d.z * sa, d.y, piv.z + d.x * sa + d.z * ca))
        p2 = p.transformed(rot)
        out.append(p2)
    asm.parts = out
    G._apply_holes(asm, [((12.0, -4.7, -2.0), 3.0, 700, 1.0), ((-4.0, 4.7, 2.0), 2.4, 701, 1.0)])
    G.soot(asm, [], 1.0, 0.35)
    G._apply_cut(asm, lambda x, y: -2.6 + 1.2 * G.fbm2(x * 0.4, y * 0.4, 7) if abs(x) > 19.9 else 60.0, lod=lod)


def viaduct(lod):
    asm = G.Asm()
    pitch = 12.0
    narch = 3
    L = pitch * narch
    x0, x1 = -L / 2, L / 2
    w = 4.6                   # half width
    pt = 2.4                  # pier thickness
    zspr, zdeck, zfoot = -9.0, 0.0, -16.0
    zpar = 1.1
    r = (pitch - pt) / 2
    nseg = (14, 9, 6, 4)[lod]
    for i in range(narch + 1):
        xc = x0 + pitch * i
        a, b = max(x0, xc - pt / 2), min(x1, xc + pt / 2)
        pr = G.Part('solid', ('pier', 'body'))
        G.block(pr, a, b, -w, w, zfoot, zdeck - 0.6)
        asm.add(pr)
        if lod == 0:
            # stone impost band
            ib = G.Part('detail', ('impost',), 'stone')
            G.block(ib, a - 0.08 * (a > x0), b + 0.08 * (b < x1), -w - 0.08, w + 0.08, zspr - 0.35, zspr, D, band='stone')
            asm.add(ib)
    for i in range(narch):
        xa = x0 + pitch * i + pt / 2
        xb = xa + 2 * r
        xc = (xa + xb) / 2
        prof = [(xa, zdeck - 0.6), (xa, zspr)] + [(xc - r * math.cos(math.pi * k / nseg), zspr + r * math.sin(math.pi * k / nseg)) for k in range(1, nseg)] + [(xb, zspr), (xb, zdeck - 0.6)]
        sp = G.Part('solid', ('body', 'spandrel'))
        G.extrude_profile(sp, prof, lambda a, z, s: v3(a, s, z), -w, w, S, S)
        asm.add(sp)
        if lod == 0:
            # projecting voussoir ring on both faces
            for (ya, yb) in ((-w - 0.12, -w + 0.01), (w - 0.01, w + 0.12)):
                vr = G.Part('detail', ('voussoir',), 'stone')
                outer = [(xc - (r + 0.55) * math.cos(math.pi * k / nseg), zspr + (r + 0.55) * math.sin(math.pi * k / nseg)) for k in range(nseg + 1)]
                inner = [(xc - r * math.cos(math.pi * k / nseg), zspr + r * math.sin(math.pi * k / nseg)) for k in range(nseg + 1)]
                G.extrude_profile(vr, outer + inner[::-1], lambda a, z, s: v3(a, s, z), ya, yb, D, D,
                                  [('band', 'stone')] * (2 * nseg + 2), cap_uv=lambda q: G.band_uv(q, 'stone'))
                asm.add(vr)
    # deck slab with string course, parapets and coping
    dk = G.Part('solid', ('deck', 'body'))
    G.block(dk, x0, x1, -w, w, zdeck - 0.6, zdeck - 0.42, S, mat_top=R)
    asm.add(dk)
    if lod <= 1:
        asm.add(G.band_ring(x0 + 0.001, x1 - 0.001, -w, w, zdeck - 1.1, zdeck - 0.6, 0.22, 'stone', ('cornice',)))
    if lod <= 2:
        pp = G.Part('solid', ('parapet', 'body'))
        G.block(pp, x0, x1, -w, -w + 0.45, zdeck - 0.42, zpar)
        G.block(pp, x0, x1, w - 0.45, w, zdeck - 0.42, zpar)
        asm.add(pp)
    if lod <= 1:
        cp = G.Part('detail', ('coping',), 'stone')
        G.block(cp, x0, x1, -w - 0.06, -w + 0.51, zpar, zpar + 0.14, D, band='stone')
        G.block(cp, x0, x1, w - 0.51, w + 0.06, zpar, zpar + 0.14, D, band='stone')
        asm.add(cp)
    for ty in (-1.9, 1.9):
        track(asm, x0, x1, ty, lod)
    asm.info.update(eave=16.0, ridge=17.0, n_holes=2, hole_scale=1.0)
    asm.info['blobs'] = [((4.0, -w, 0.4), 1.8, 800, 1.0), ((-11.0, w, -2.5), 2.2, 801, 1.0), ((-6.0, -w, -5.0), 1.6, 802, 1.0)]
    return asm


def _viaduct_construction(asm, lod):
    G._apply_cut(asm, lambda x, y: (-5.5 + 1.0 * math.floor(G.fbm2(x * 0.1, y * 0.1, 3) * 3)) if x > -6 else 60.0,
                 drop_tags=('track', 'parapet', 'coping'), lod=lod)
    # timber centring under the middle and east arches
    if lod <= 2:
        for xc in (0.0, 12.0):
            for y in (-3.5, 0.0, 3.5):
                n = 7 if lod <= 1 else 4
                pts = [(xc - 4.8 * math.cos(math.pi * k / n), -9.0 + 4.8 * math.sin(math.pi * k / n)) for k in range(n + 1)]
                for k in range(n):
                    member(asm, (pts[k][0], y, pts[k][1] - 0.2), (pts[k + 1][0], y, pts[k + 1][1] - 0.2), 0.25, band='timber')
                member(asm, (xc - 4.6, y, -9.2), (xc + 4.6, y, -9.2), 0.3, band='timber')
                if lod <= 1:
                    member(asm, (xc, y, -16.0), (xc, y, -4.4), 0.3, band='timber')
                    for dx in (-3.0, 3.0):
                        member(asm, (xc + dx, y, -16.0), (xc + dx, y, -9.2), 0.25, band='timber')


def _viaduct_ruin(asm, lod):
    def hf(x, y):
        if -5.0 < x < 5.0:
            return -15.0 + 2.0 * G.fbm2(x * 0.5, y * 0.5, 4)
        return -4.0 + 3.0 * G.fbm2(x * 0.3 + 2, y * 0.3, 5)
    G._apply_cut(asm, hf, drop_tags=('track', 'coping'), lod=lod)
    G.soot(asm, [], 1.0, 0.3)
    G.rubble_ring(asm, (-5.0, 5.0, -4.0, 4.0, -16, -10), 811, 5, lod, (4.0, 7.0), -16.0, inside=True)


def _truss_damaged(asm, lod):
    pass


# --------------------------------------------------------------------------- culture ornaments

def ornament(culture, lod):
    asm = G.Asm()
    p = G.Part('detail', ('ornament',), 'terracotta')
    if culture == 'A':      # domed chimney cap (warm clay)
        n = (10, 8, 6, 4)[lod]
        rings = (5, 3, 2, 1)[lod]
        prof = [(0.38, 0.0), (0.38, 0.3)] + [(0.42 * math.cos(math.pi / 2 * k / rings) + 1e-3, 0.3 + 0.42 * math.sin(math.pi / 2 * k / rings)) for k in range(rings + 1)]
        prof[-1] = (0.0, 0.72)
        G.lathe(p, (0, 0, 0), prof, n, D, band='terracotta')
        G.cylinder(p, (0, 0, 0.7), 0.05, 0.0, 0.25, 4, D, band='terracotta')
    elif culture == 'B':    # upswept twin horns (timber)
        seg = (6, 4, 3, 2)[lod]
        for s in (-1, 1):
            pts = []
            for k in range(seg + 1):
                t = k / seg
                pts.append(v3(s * (0.1 + 0.55 * t ** 1.6), 0.0, 1.3 * t))
            for k in range(seg):
                G.beam(p, pts[k], pts[k + 1], 0.12 * (1 - 0.6 * k / seg), 0.14 * (1 - 0.6 * k / seg), D, 'timber')
        p.capband = 'timber'
    else:                   # conical stone finial
        n = (10, 8, 6, 4)[lod]
        G.cylinder(p, (0, 0, 0), 0.3, 0.3, 0.18, n, D, band='stone')
        G.cylinder(p, (0, 0, 0.18), 0.22, 0.0, 0.95, n, D, band='stone', caps=True)
        p.capband = 'stone'
    asm.add(p)
    return asm
