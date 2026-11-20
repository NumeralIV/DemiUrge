# DEMIURGE art - Age2 (Iron Age) asset definitions. Each function builds one asset on a Builder.
# Local space: metres, Z up, origin at the footprint centre on the ground, front (door / street /
# outer face) toward -Y. Slots: S Structure, R Roof, D Detail (trim sheet).
import math
from mathutils import Vector, Matrix
from age2_kit import S, R, D, hash01

# ---------------------------------------------------------------------- helpers


def win(c, w=0.75, zb=1.15, h=0.95, cell=0, kind='window', **kw):
    d = dict(c=c, w=w, zb=zb, h=h, cell=cell, kind=kind)
    d.update(kw)
    return d


def door(c, w=1.05, zb=0.25, h=2.05, **kw):
    d = dict(c=c, w=w, zb=zb, h=h, kind='door')
    d.update(kw)
    return d


def _ops(L, ops):
    out = []
    for o in ops:
        q = dict(o)
        q['x0'] = L / 2 + o['c'] - o['w'] / 2
        q['x1'] = L / 2 + o['c'] + o['w'] / 2
        q['zt'] = o['zb'] + o['h']
        if o.get('kind') == 'arch':
            q['zs'] = q['zt'] - o['w'] / 2
        out.append(q)
    return out


def quoins(b, x0, x1, y0, y1, z0, z1, ls=0.6, ss=0.34, ch=0.34, out=0.045):
    """Dressed corner stones standing slightly proud at the four corners (LOD0-1)."""
    if b.lod > 1 or b.state != 'Intact':
        return
    for (cx, cy, sx, sy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x1, y1, -1, -1), (x0, y1, 1, -1)):
        z = z0
        k = 0
        while z + ch <= z1 + 0.01:
            lx, ly = (ls, ss) if k % 2 == 0 else (ss, ls)
            b.box(cx + sx * (lx / 2 - out), cy + sy * (ly / 2 - out), z, lx, ly, ch - 0.03, S, ml=1)
            z += ch
            k += 1


def rect_walls(b, x0, x1, y0, y1, z0, z1, t, ops=None, key=0, slot=S, inward=False, band=None, ml=3, top=True):
    """Four walls whose OUTER faces lie on the rectangle (or inner faces when inward=True, for
    courtyards: then the 'outside' of each wall faces the courtyard). ops: {'front','right','back','left'}."""
    ops = ops or {}
    h = t / 2
    if not inward:
        sides = {
            'front': ((x0, y0 + h), (x1, y0 + h)),
            'right': ((x1 - h, y0 + t), (x1 - h, y1 - t)),
            'back': ((x1, y1 - h), (x0, y1 - h)),
            'left': ((x0 + h, y1 - t), (x0 + h, y0 + t)),
        }
    else:   # walls outside the rectangle, faces toward its centre
        sides = {
            'front': ((x1 + t, y0 - h), (x0 - t, y0 - h)),
            'right': ((x1 + h, y1), (x1 + h, y0)),
            'back': ((x0 - t, y1 + h), (x1 + t, y1 + h)),
            'left': ((x0 - h, y0), (x0 - h, y1)),
        }
    for k, nm in enumerate(('front', 'right', 'back', 'left')):
        p0, p1 = sides[nm]
        L = (Vector(p1) - Vector(p0)).length
        b.wall(p0, p1, z0, z1, t, slot, _ops(L, ops.get(nm, [])), ml=ml, key=key * 10 + k, band=band, top=top)
    return sides


def plinth(b, w, d, top=0.25, below=0.35, slot=S, ml=3):
    b.box(0, 0, -below, w, d, top + below, slot, ml=ml)


def ruin_rubble(b, pts, key):
    """Rubble heaps for Damaged / Ruin states at the given (x, y, radius, height) spots."""
    if b.state == 'Ruin':
        for k, (x, y, r, h) in enumerate(pts):
            b.rubble(x, y, 0.0, r, h, 8, seed=key * 31 + k, ml=2)
    elif b.state == 'Damaged':
        for k, (x, y, r, h) in enumerate(pts[:1]):
            b.rubble(x, y, 0.0, r * 0.7, h * 0.6, 5, seed=key * 31 + k, ml=1)


def woodpile(b, x, y, w, d, h, yaw=0.0, face=-1):
    """Stacked split logs: a dark core with log ends on the long faces (LOD0), a box at LOD1."""
    if b.state == 'Ruin' or b.lod > 1:
        return
    b.box(x, y, 0.0, w, d, h, D, yaw=yaw, band='timber', ml=1, shade=0.55)
    if b.lod > 0:
        return
    long_x = w >= d
    L = w if long_x else d
    n = max(2, int(L / 0.3))
    rows = max(1, int(h / 0.28))
    for side in (face,):
        for r in range(rows):
            for i in range(n):
                off = (i + 0.5 + 0.5 * (r % 2)) * L / (n + 0.5) - L / 2
                zc = 0.12 + r * h / rows
                rad = 0.09 + 0.025 * hash01(i, r, side)
                if long_x:
                    p0 = (x + off, y + side * d / 2 - side * 0.05, zc)
                    p1 = (x + off, y + side * d / 2 + side * 0.03, zc)
                else:
                    p0 = (x + side * w / 2 - side * 0.05, y + off, zc)
                    p1 = (x + side * w / 2 + side * 0.03, y + off, zc)
                b.beam(p0, p1, rad * 2, rad * 2, D, 'timber', ml=0, ends=True)
                b.faces.pop(-1)   # the hidden inner end


def barrel(b, x, y):
    if b.lod > 0 or b.state == 'Ruin':
        return
    b.cyl(x, y, 0.0, 0.9, 0.3, 0.3, 8, D, band='plank', ml=0)


def burn_spot(b, x, y, z, r):
    if b.state in ('Damaged', 'Ruin'):
        b.burn.append((b.M @ Vector((x, y, z)), r))


# ---------------------------------------------------------------------- houses


def house_a(b):
    """Small rubble-stone cottage, steep thatch, chimney on one gable."""
    W, Dp, t, z1, pitch = 7.0, 5.2, 0.55, 2.9, 50
    key = 11
    b.eave_z = z1
    plinth(b, W + 0.3, Dp + 0.3)
    quoins(b, -W / 2, W / 2, -Dp / 2, Dp / 2, 0.25, z1)
    rect_walls(b, -W / 2, W / 2, -Dp / 2, Dp / 2, -0.3, z1, t, {
        'front': [door(-0.9), win(1.6, cell=1)],
        'right': [win(0.0, cell=2)],
        'back': [win(-1.2, cell=3)],
        'left': [],
    }, key)
    hz = Dp / 2 * math.tan(math.radians(pitch))
    for x in (-W / 2 + t / 2, W / 2 - t / 2):
        b.gable_tri((x, -Dp / 2 if x > 0 else Dp / 2), (x, Dp / 2 if x > 0 else -Dp / 2), z1, z1 + hz, t)
    b.gable_roof(-W / 2, W / 2, Dp / 2, z1, pitch, 'thatch', oh=0.5, ohg=0.4, key=key)
    b.chimney(W / 2 - 0.55, 0.9, z1 - 0.5, z1 + hz + 0.9, 0.75, 0.7)
    woodpile(b, -W / 2 - 0.55, 0.4, 0.6, 2.2, 1.1)
    barrel(b, W / 2 + 0.5, -Dp / 2 + 0.6)
    b.box(-2.6, -Dp / 2 - 0.45, 0.0, 1.4, 0.4, 0.45, S, ml=0)       # bench stone
    barrel(b, W / 2 + 0.55, -Dp / 2 + 1.35)
    if b.lod == 0 and b.state != 'Ruin':                              # chopping block
        b.cyl(-W / 2 - 1.1, -1.4, 0.0, 0.55, 0.3, 0.28, 8, D, band='timber', ml=0)
    ruin_rubble(b, [(1.0, -Dp / 2 - 0.6, 1.3, 0.6), (-W / 2 - 0.5, 1.0, 1.0, 0.5), (0.5, 0.5, 1.6, 0.7)], key)
    if b.building:
        b.scaffold((-W / 2, -Dp / 2), (W / 2, -Dp / 2), z1 * 0.7, 1.1, key)
        b.stone_pile(W / 2 + 1.4, -1.0, 6, key)


def house_b(b):
    """Stone longhouse with a dark shingle roof, porch over the door, central chimney."""
    W, Dp, t, z1, pitch = 10.5, 6.0, 0.55, 2.8, 45
    key = 23
    b.eave_z = z1
    plinth(b, W + 0.3, Dp + 0.3)
    quoins(b, -W / 2, W / 2, -Dp / 2, Dp / 2, 0.25, z1)
    rect_walls(b, -W / 2, W / 2, -Dp / 2, Dp / 2, -0.3, z1, t, {
        'front': [win(-3.9, cell=4), door(-1.6), win(1.0, cell=5), win(3.7, cell=6)],
        'right': [win(0.0, cell=7)],
        'back': [win(-2.5, cell=8), win(2.5, cell=9)],
        'left': [win(0.6, cell=10, w=0.6, h=0.7, zb=1.3)],
    }, key)
    hz = Dp / 2 * math.tan(math.radians(pitch))
    for x in (-W / 2 + t / 2, W / 2 - t / 2):
        b.gable_tri((x, -Dp / 2 if x > 0 else Dp / 2), (x, Dp / 2 if x > 0 else -Dp / 2), z1, z1 + hz, t)
    b.gable_roof(-W / 2, W / 2, Dp / 2, z1, pitch, 'shingle', oh=0.45, ohg=0.35, key=key)
    b.chimney(1.0, 0.2, z1, z1 + hz + 1.0, 0.85, 0.85)
    # porch
    if b.state != 'Ruin':
        b.lean_to(-2.9, -0.3, -Dp / 2 - 1.7, -Dp / 2, 2.25, 22, 'shingle', oh=0.15, key=key + 5, ml=1)
        for x in (-2.8, -0.4):
            b.beam((x, -Dp / 2 - 1.6, 0.0), (x, -Dp / 2 - 1.6, 2.15), 0.18, 0.18, D, 'timber', ml=1)
        b.beam((-2.9, -Dp / 2 - 1.6, 2.15), (-0.3, -Dp / 2 - 1.6, 2.15), 0.18, 0.18, D, 'timber', ml=1)
    woodpile(b, W / 2 + 0.5, 0.0, 0.7, 3.4, 1.3, face=1)
    ox0, ox1, oy1, oz = -W / 2, -W / 2 + 4.2, Dp / 2 + 2.6, 2.0
    b.wall((ox0 + 0.25, Dp / 2 - 0.1), (ox0 + 0.25, oy1), -0.3, oz, 0.5, S, [], ml=2, key=key + 21)
    b.wall((ox0, oy1 - 0.25), (ox1, oy1 - 0.25), -0.3, oz, 0.5, S,
           _ops(ox1 - ox0, [door(0.6, w=1.0, h=1.75, zb=0.0)]), ml=2, key=key + 22)
    b.wall((ox1 - 0.25, oy1), (ox1 - 0.25, Dp / 2 - 0.1), -0.3, oz, 0.5, S,
           _ops(2.7, [win(0.0, w=0.5, h=0.5, zb=1.1, cell=11)]), ml=2, key=key + 23)
    b.lean_to(ox0, ox1, oy1, Dp / 2, oz, 16, 'shingle', oh=0.25, key=key + 24, ml=2)
    if b.keep(2):
        yw = Dp / 2 + 4.0
        b.wall((-W / 2 + 0.25, Dp / 2), (-W / 2 + 0.25, yw), -0.3, 1.25, 0.5, S, [], ml=2, key=key + 11)
        b.wall((-W / 2, yw - 0.25), (W / 2, yw - 0.25), -0.3, 1.25, 0.5, S,
               _ops(W, [dict(c=1.5, w=2.2, zb=-0.3, h=2.0, kind='open')]), ml=2, key=key + 12)
    if b.lod == 0 and b.state != 'Ruin':
        b.box(1.5, Dp / 2 + 1.6, 0.0, 1.6, 0.7, 0.6, S, ml=0)
        b.quad((0.8, Dp / 2 + 1.35, 0.61), (2.2, Dp / 2 + 1.35, 0.61), (2.2, Dp / 2 + 1.85, 0.61), (0.8, Dp / 2 + 1.85, 0.61), D, b.band_uv('water', 0, 1.4))
    barrel(b, -W / 2 + 0.6, -Dp / 2 - 0.5)
    ruin_rubble(b, [(2.5, -Dp / 2 - 0.7, 1.5, 0.6), (-3.0, 0.5, 2.0, 0.8), (3.0, 0.8, 1.6, 0.7)], key)
    if b.building:
        b.scaffold((-W / 2, -Dp / 2), (W / 2, -Dp / 2), z1 * 0.7, 1.1, key)
        b.stone_pile(-W / 2 - 1.5, 1.0, 6, key)


def house_c(b):
    """Two storeys: rubble ground floor, jettied timber-framed plaster upper floor, gable to the street."""
    W, Dp, t, z1 = 7.6, 6.6, 0.6, 3.0
    jt, z2, t2, pitch = 0.35, 5.7, 0.3, 52
    key = 37
    b.eave_z = z1 + 0.1
    plinth(b, W + 0.3, Dp + 0.3)
    quoins(b, -W / 2, W / 2, -Dp / 2, Dp / 2, 0.25, z1 - 0.1)
    rect_walls(b, -W / 2, W / 2, -Dp / 2, Dp / 2, -0.3, z1, t, {
        'front': [door(-1.4), win(1.5, cell=11, w=1.2, h=1.0)],
        'right': [win(-1.0, cell=12)],
        'back': [door(1.2, w=0.95)],
        'left': [win(0.8, cell=13)],
    }, key)
    W2, D2 = W + 2 * jt * 0.4, Dp + 2 * jt
    upper_ok = b.state in ('Intact', 'Damaged')
    if upper_ok:
        # jetty joists and floor
        b.box(0, 0, z1, W2, D2, 0.22, D, band='timber', ml=3)
        if b.lod == 0:
            for k in range(9):
                x = -W2 / 2 + 0.4 + k * (W2 - 0.8) / 8
                for sy in (-1, 1):
                    b.box(x, sy * (Dp / 2 + jt * 0.5), z1 - 0.18, 0.16, jt + 0.1, 0.18, D, band='timber', ml=0)
        rect_walls(b, -W2 / 2, W2 / 2, -D2 / 2, D2 / 2, z1 + 0.22, z2, t2, {
            'front': [win(-1.6, cell=14, w=0.9, h=1.1, zb=z1 + 1.0), win(1.6, cell=15, w=0.9, h=1.1, zb=z1 + 1.0)],
            'right': [win(0.0, cell=0, w=0.8, h=1.0, zb=z1 + 1.0)],
            'back': [win(0.0, cell=1, w=0.8, h=1.0, zb=z1 + 1.0)],
            'left': [win(-0.8, cell=2, w=0.8, h=1.0, zb=z1 + 1.0)],
        }, key + 5, slot=D, band='plaster')
        # timber frame on the outer faces
        if b.lod <= 1:
            for (p0, p1) in (((-W2 / 2, -D2 / 2), (W2 / 2, -D2 / 2)), ((W2 / 2, -D2 / 2), (W2 / 2, D2 / 2)),
                             ((W2 / 2, D2 / 2), (-W2 / 2, D2 / 2)), ((-W2 / 2, D2 / 2), (-W2 / 2, -D2 / 2))):
                a = Vector((p1[0] - p0[0], p1[1] - p0[1], 0))
                L = a.length
                a.normalize()
                n = Vector((a.y, -a.x, 0)) * 0.04
                o = Vector((p0[0], p0[1], 0)) + n
                for zz in (z1 + 0.32, z2 - 0.1):
                    b.beam(o + Vector((0, 0, zz)), o + a * L + Vector((0, 0, zz)), 0.2, 0.2, D, 'timber', ml=1)
                np_ = max(2, int(L / 1.25))
                for k in range(np_ + 1):
                    p = o + a * (L * k / np_)
                    b.beam(p + Vector((0, 0, z1 + 0.22)), p + Vector((0, 0, z2)), 0.18, 0.16, D, 'timber', ml=1)
                if b.lod == 0:
                    for k in (0, np_ - 1):
                        pa = o + a * (L * k / np_)
                        pb = o + a * (L * (k + 1) / np_)
                        if k == 0:
                            b.beam(pa + Vector((0, 0, z1 + 0.35)), pb + Vector((0, 0, z2 - 0.15)), 0.14, 0.12, D, 'timber', ml=0)
                        else:
                            b.beam(pb + Vector((0, 0, z1 + 0.35)), pa + Vector((0, 0, z2 - 0.15)), 0.14, 0.12, D, 'timber', ml=0)
        # roof: ridge along Y (gable to the street)
        hz = W2 / 2 * math.tan(math.radians(pitch))
        b.gable_tri((-W2 / 2, -D2 / 2 + t2 / 2), (W2 / 2, -D2 / 2 + t2 / 2), z2, z2 + hz, t2, D, band='plaster')
        b.gable_tri((W2 / 2, D2 / 2 - t2 / 2), (-W2 / 2, D2 / 2 - t2 / 2), z2, z2 + hz, t2, D, band='plaster')
        if b.lod <= 1:
            for sy in (-1, 1):
                y = sy * (D2 / 2 + 0.04)
                b.beam((0, y, z2), (0, y, z2 + hz - 0.2), 0.18, 0.16, D, 'timber', ml=1)
                b.beam((-W2 / 2, y, z2 + 0.05), (W2 / 2, y, z2 + 0.05), 0.2, 0.2, D, 'timber', ml=1)
        b.push(b.at(0, 0, 0, math.pi / 2))
        b.gable_roof(-D2 / 2, D2 / 2, W2 / 2, z2, pitch, 'shingle', oh=0.4, ohg=0.45, key=key)
        b.pop()
        b.chimney(-1.5, D2 / 2 - 0.5, z2 - 0.6, z2 + hz + 0.6, 0.8, 0.7)
    elif b.building:
        # half-built upper floor frame
        b.box(0, 0, z1, W2, D2, 0.22, D, band='timber', ml=2)
        for x in (-W2 / 2, 0, W2 / 2):
            for y in (-D2 / 2, D2 / 2):
                b.beam((x, y, z1 + 0.22), (x, y, z2), 0.2, 0.2, D, 'timber', ml=1)
        for y in (-D2 / 2, D2 / 2):
            b.beam((-W2 / 2, y, z2), (W2 / 2, y, z2), 0.2, 0.2, D, 'timber', ml=1)
        b.scaffold((-W / 2, -Dp / 2), (W / 2, -Dp / 2), z2 * 0.8, 1.1, key)
        b.stone_pile(W / 2 + 1.4, 1.0, 6, key)
    else:   # ruin: charred timbers of the fallen upper floor
        if b.lod <= 1:
            b.beam((-2.5, -1.0, 0.4), (2.0, 1.5, 1.3), 0.22, 0.22, D, 'char', ml=1)
            b.beam((-1.0, 2.0, 0.3), (2.6, -0.5, 0.6), 0.22, 0.22, D, 'char', ml=1)
    if b.state == 'Damaged':
        burn_spot(b, 1.5, -Dp / 2, z1 + 1.5, 3.0)
    ruin_rubble(b, [(1.2, -Dp / 2 - 0.7, 1.4, 0.6), (-1.5, 0.5, 2.0, 0.9), (2.0, 1.5, 1.5, 0.7)], key)
    barrel(b, W / 2 + 0.5, -Dp / 2 + 0.7)


def house_d(b):
    """Farmstead: thatched hip-roofed hall, shingled back wing, walled yard and woodshed."""
    key = 41
    W, Dm, t, z1 = 10.2, 6.4, 0.6, 2.8
    y0, y1 = -5.5, -5.5 + Dm
    b.eave_z = z1
    b.push(b.at(0, (y0 + y1) / 2))
    plinth(b, W + 0.3, Dm + 0.3)
    quoins(b, -W / 2, W / 2, -Dm / 2, Dm / 2, 0.25, z1)
    rect_walls(b, -W / 2, W / 2, -Dm / 2, Dm / 2, -0.3, z1, t, {
        'front': [win(-3.6, cell=3), door(-1.0, w=1.2, h=2.1), win(1.6, cell=4), win(3.8, cell=5)],
        'right': [win(0.0, cell=6)],
        'back': [win(2.6, cell=7)],
        'left': [win(-0.8, cell=8, w=0.6, h=0.7, zb=1.3)],
    }, key)
    b.hip_roof(-W / 2, W / 2, -Dm / 2, Dm / 2, z1, 47, 'thatch', oh=0.5, key=key)
    b.chimney(-2.2, 0.3, z1, z1 + 3.4 + 0.9, 0.8, 0.8)
    b.pop()
    # back wing (byre / store), ridge along Y
    wx0, wx1, wy0, wy1, wz = -W / 2, -W / 2 + 4.6, y1 - 0.3, 5.5, 2.5
    b.push(b.at((wx0 + wx1) / 2, (wy0 + wy1) / 2))
    ww, wd = wx1 - wx0, wy1 - wy0
    plinth(b, ww + 0.2, wd + 0.2)
    rect_walls(b, -ww / 2, ww / 2, -wd / 2, wd / 2, -0.3, wz, 0.5, {
        'right': [door(0.6, w=1.6, h=2.0)],
        'left': [win(0.5, cell=9, w=0.5, h=0.6, zb=1.4)],
        'back': [],
    }, key + 3)
    hz = ww / 2 * math.tan(math.radians(44))
    b.gable_tri((ww / 2, wd / 2 - 0.25), (-ww / 2, wd / 2 - 0.25), wz, wz + hz, 0.5)
    b.push(b.at(0, 0, 0, math.pi / 2))
    b.gable_roof(-wd / 2, wd / 2 + 0.2, ww / 2, wz, 44, 'thatch', oh=0.4, ohg=0.3, key=key + 4)
    b.pop()
    b.pop()
    # walled yard (low rubble wall) on the right behind the hall
    if b.keep(2):
        yz = 1.3
        b.wall((W / 2 - 0.25, y1 + 0.2), (W / 2 - 0.25, 5.3), -0.3, yz, 0.5, S, [], ml=2, key=key + 7, flat=False)
        b.wall((W / 2 - 0.25, 5.3), (wx1 + 0.3, 5.3), -0.3, yz, 0.5, S, _ops(W / 2 - 0.25 - wx1 - 0.3, [dict(c=0.0, w=2.0, zb=-0.3, h=2.0, kind='open')]), ml=2, key=key + 8)
    # woodshed lean-to against the right gable
    if b.state != 'Ruin':
        b.push(b.at(W / 2 + 0.9, (y0 + y1) / 2, 0, -math.pi / 2))
        b.lean_to(-2.2, 2.2, -0.9, 0.9, 1.9, 24, 'thatch', oh=0.2, key=key + 9, ml=1)
        b.pop()
        woodpile(b, W / 2 + 0.95, (y0 + y1) / 2, 1.2, 3.8, 1.5, face=1)
        for yy in (-2.1, 2.1):
            b.beam((W / 2 + 1.75, (y0 + y1) / 2 + yy, 0.0), (W / 2 + 1.75, (y0 + y1) / 2 + yy, 1.9), 0.16, 0.16, D, 'timber', ml=1)
    barrel(b, 2.0, y1 + 1.0)
    ruin_rubble(b, [(0.5, y0 - 0.8, 1.6, 0.6), (-2.0, -2.0, 2.2, 0.9), (2.5, -2.4, 1.8, 0.8), (-3.0, 3.0, 1.5, 0.6)], key)
    if b.building:
        b.scaffold((-W / 2, y0), (W / 2, y0), z1 * 0.7, 1.1, key)
        b.stone_pile(3.0, 3.0, 6, key)


# ---------------------------------------------------------------------- town walls


def _talus(b, x0, x1, y_face, out, z0, z1, ml=1):
    """Battered base (sloped skirt) along a wall face at y_face, sloping out by `out` toward -Y."""
    if not b.keep(ml):
        return
    A = Vector((x0, y_face - out, z0))
    B = Vector((x1, y_face - out, z0))
    C = Vector((x1, y_face, z1))
    Dd = Vector((x0, y_face, z1))
    b.quad(A, B, C, Dd, S)
    # end triangles close the wedge between the skirt and the wall (verification fix)
    b.face([Vector((x0, y_face, z0)), A, Dd], S)
    b.face([B, Vector((x1, y_face, z0)), C], S)


def wall_seg(b):
    """16 m straight curtain wall, 3.2 m thick, walkway at 7.6 m, crenellated outer parapet (-Y).
    Tiles end to end along X; merlons on a 2 m module so neighbours line up."""
    key = 51
    b.shutters = False
    b.grime_h = 2.6
    L, t, zw = 16.0, 3.2, 7.6
    intact = b.state == 'Intact'
    # ends closed in every state: the planner joins segments at polygon corners without a tower
    # (towers only every sixth corner), where open ends showed a see-through slit (verification fix)
    b.wall((-L / 2, 0), (L / 2, 0), -1.2, zw, t, S, [], ml=3, key=key, ends=True)
    # battered base on the outer face
    if b.state != 'Construction':
        _talus(b, -L / 2, L / 2, -t / 2, 0.7, -0.6, 1.4, ml=1)
    if b.lod >= 3:
        return
    # parapets follow the core where it still stands at full height
    def core_full(x):
        return b.top_profile(x + L / 2, L, -1.2, zw, key) >= zw - 0.01
    if b.state in ('Intact', 'Damaged'):
        for k in range(8):
            xc = -7.0 + 2.0 * k
            if not (core_full(xc - 0.9) and core_full(xc + 0.9) and core_full(xc)):
                continue
            b.box(xc, -t / 2 + 0.35, zw, 2.0, 0.7, 1.05, S, ml=2)   # top kept: it is the crenel floor (was a hole)
            b.box(xc, -t / 2 + 0.35, zw + 1.05, 1.15, 0.7, 0.95, S, ml=1)
            b.box(xc, t / 2 - 0.25, zw, 2.0, 0.5, 0.95, S, ml=1)
            if b.lod == 0:   # slit in every other merlon, coping on the inner parapet
                if k % 2 == 0:
                    b.box(xc, -t / 2 - 0.005, zw + 1.25, 0.14, 0.02, 0.6, D, band='char', ml=0)
    if b.damaged:
        for k in range(2):
            c = hash01(key, k, 3) * L - L / 2
            if hash01(key, k, 5) < 0.55:
                b.rubble(c, -t / 2 - 2.0, 0.0, 2.2, 1.4, 8, seed=key + k, ml=1)
                burn_spot(b, c, -t / 2, zw - 1, 3.0)
    if b.state == 'Ruin':
        for k, x in enumerate((-5.5, 0.5, 5.0)):
            b.rubble(x, -t / 2 - 2.2 - 0.6 * (k % 2), 0.0, 2.6, 1.6, 9, seed=key + k * 3, ml=2)
            b.rubble(x + 1.5, t / 2 + 2.0, 0.0, 2.2, 1.2, 7, seed=key + k * 3 + 1, ml=1)
    if b.building:
        b.scaffold((L / 2, t / 2), (-L / 2, t / 2), 5.5, 1.2, key)
        b.stone_pile(3.0, t / 2 + 3.0, 9, key)
        b.stone_pile(-4.0, t / 2 + 3.2, 6, key + 1)


def _square_tower(b, cx, cy, w, d, h, key, roofed, slits=True, door_x=None, zw=7.6, roof='tile',
                  hoard_h=2.4, base=-1.2, front_slits=2):
    """Hollow square tower: walls t=1.3, crenellated top or timber hoarding + pyramid roof."""
    t = 1.3
    b.shutters = False
    b.grime_h = 2.6
    b.push(b.at(cx, cy))
    ops = {'front': [], 'right': [], 'back': [], 'left': []}
    if slits:
        for side, L in (('front', w), ('back', w), ('right', d), ('left', d)):
            n = front_slits if side in ('front', 'back') else 1
            for k in range(n):
                c = (k - (n - 1) / 2) * (L / (n + 1)) * 1.4
                for zb in (3.2, zw + 0.6 if not door_x else 9.0):
                    if zb + 1.5 < h - 0.5:
                        ops[side].append(win(c, w=0.24, zb=zb, h=1.5, kind='slit', cell=k + 3, lintel=False, sill=False))
    if door_x:
        for side in ('left', 'right'):
            ops[side].append(dict(c=0.0, w=1.3, zb=zw, h=2.3, kind='open'))
    if not roofed:
        # crenels are openings in the top band of the walls
        zc = h - 0.95
        for side, L in (('front', w), ('back', w), ('right', d), ('left', d)):
            n = int(L / 2.0)
            for k in range(n):
                c = -L / 2 + (k + 0.5) * L / n
                if k % 2 == 1:
                    ops[side].append(dict(c=c, w=L / n, zb=zc, h=1.2, kind='open'))
    rect_walls(b, -w / 2, w / 2, -d / 2, d / 2, base, h, t, ops, key)
    if b.state in ('Intact', 'Damaged'):
        # floors visible through the top
        b.box(0, 0, h - 1.3 if not roofed else h - 0.4, w - 2 * t, d - 2 * t, 0.3, D, band='plank', ml=2)
        if not roofed and b.lod == 0:   # string course below the parapet
            for (x, y, sx, sy) in ((0, -d / 2 - 0.12, w + 0.5, 0.25), (0, d / 2 + 0.12, w + 0.5, 0.25), (-w / 2 - 0.12, 0, 0.25, d), (w / 2 + 0.12, 0, 0.25, d)):
                b.box(x, y, h - 1.55, sx, sy, 0.3, S, ml=0)
    # base batter
    if b.state != 'Construction':
        for (yaw, L, off) in ((0, w, d / 2), (math.pi, w, d / 2), (math.pi / 2, d, w / 2), (-math.pi / 2, d, w / 2)):
            b.push(b.at(0, 0, 0, yaw))
            _talus(b, -L / 2 - 0.6, L / 2 + 0.6, -off, 0.6, -0.6, 1.4, ml=1)
            b.pop()
    if roofed and b.state in ('Intact', 'Damaged'):
        ho = 0.7
        hw, hd = w / 2 + ho, d / 2 + ho
        # corbels carrying the hoarding
        if b.lod <= 1:
            for (x0, y0, x1, y1) in ((-hw, -d / 2, hw, -d / 2), (-hw, d / 2, hw, d / 2), (-w / 2, -hd, -w / 2, hd), (w / 2, -hd, w / 2, hd)):
                n = 6
                for k in range(n + 1):
                    x = x0 + (x1 - x0) * k / n
                    y = y0 + (y1 - y0) * k / n
                    nx = 0 if x0 != x1 else (1 if x0 > 0 else -1)
                    ny = 0 if y0 != y1 else (1 if y0 > 0 else -1)
                    b.beam((x - nx * 0.2, y - ny * 0.2, h - 0.1), (x + nx * ho, y + ny * ho, h - 0.1), 0.22, 0.24, D, 'timber', ml=1)
        hops = {}
        for side, L in (('front', 2 * hw), ('back', 2 * hw), ('right', 2 * hd), ('left', 2 * hd)):
            n = 3
            hops[side] = [dict(c=(k - 1) * L / 3.2, w=0.6, zb=h + 0.9, h=0.7, kind='window', cell=10 + k, lintel=False, sill=False) for k in range(n)]
        rect_walls(b, -hw, hw, -hd, hd, h, h + hoard_h, 0.16, hops, key + 5, slot=D, band='plank', ml=3)
        b.box(0, 0, h, 2 * hw - 0.3, 2 * hd - 0.3, 0.2, D, band='plank', ml=2)
        b.hip_roof(-hw, hw, -hd, hd, h + hoard_h, 52 if roof != 'thatch' else 50, roof, oh=0.45, key=key + 6)
        if b.lod <= 1:   # finial pole
            zr = h + hoard_h + (min(hw, hd) + 0.45) * math.tan(math.radians(52)) + 0.2
            b.beam((0, 0, zr - 0.6), (0, 0, zr + 1.2), 0.12, 0.12, D, 'iron', ml=1)
    if b.building:
        b.scaffold((-w / 2 - 0.2, -d / 2), (w / 2 + 0.2, -d / 2), h * 0.55, 1.2, key)
        b.stone_pile(w / 2 + 2.2, -d / 2 - 1.5, 9, key)
    if b.damaged:
        burn_spot(b, w * 0.3, -d / 2, h, 4.0)
        b.rubble(w * 0.2, -d / 2 - 2.0, 0.0, 2.0, 1.2, 7, seed=key + 3, ml=1)
    if b.ruined:
        b.rubble(-w * 0.2, -d / 2 - 2.4, 0.0, 3.0, 1.8, 9, seed=key + 5, ml=2)
        b.rubble(w * 0.3, d / 2 + 2.2, 0.0, 2.6, 1.4, 8, seed=key + 6, ml=1)
    b.pop()


def wall_tower_a(b):
    """Square wall tower, crenellated (the Iron-age town ring). 9 x 9 m, 15 m."""
    _square_tower(b, 0, 0, 9.0, 9.0, 15.0, 61, roofed=False, door_x=True)


def wall_tower_b(b):
    """Square wall tower with timber hoarding and a pyramidal tiled roof. 9 x 9 m."""
    _square_tower(b, 0, 0, 9.0, 9.0, 13.5, 67, roofed=True, door_x=True, roof='tile')


def gatehouse(b):
    """Twin-tower gatehouse, 26 x 11 m: round-arched passage (road along Y), portcullis,
    open gate leaves, timber gallery with a gable roof between hoarded towers."""
    key = 71
    gw, gd, zg = 8.0, 9.6, 10.6        # gate block width (x), depth (y), walkway
    tw, td, th = 9.0, 11.0, 14.5
    aw, az = 4.6, 3.8                  # arch width, spring height
    # gate block: two passage walls + front/back arched walls
    t = 1.6
    ops_arch = [dict(c=0.0, w=aw, zb=-0.3, h=az + aw / 2 + 0.3, kind='arch')]
    for sy in (-1, 1):
        y = sy * (gd / 2 - t / 2)
        p0, p1 = ((-gw / 2, y), (gw / 2, y)) if sy < 0 else ((gw / 2, y), (-gw / 2, y))
        b.wall(p0, p1, -1.2, zg, t, S, _ops(gw, ops_arch), ml=3, key=key + sy, ends=b.state != 'Intact')
    for sx in (-1, 1):   # passage side walls (inner faces toward the road)
        x = sx * (aw / 2 + 0.6)
        p0, p1 = ((x, -gd / 2 + t), (x, gd / 2 - t)) if sx < 0 else ((x, gd / 2 - t), (x, -gd / 2 + t))
        b.wall(p0, p1, -1.2, zg - 0.3, 1.2, S, [], ml=2, key=key + 5 + sx)
    if b.state in ('Intact', 'Damaged'):
        b.box(0, 0, zg - 0.35, gw, gd - 2 * t + 0.1, 0.35, S, ml=2, bottom=True)    # passage vault/ceiling
        # portcullis (half raised) in the outer arch
        if b.lod <= 1:
            yz = -gd / 2 + t - 0.35
            for k in range(7):
                x = -aw / 2 + 0.3 + k * (aw - 0.6) / 6
                b.beam((x, yz, 3.0), (x, yz, az + aw / 2 - 0.2), 0.12, 0.12, D, 'iron', ml=1)
            for zz in (3.1, 4.0, 4.9):
                b.beam((-aw / 2 + 0.1, yz, zz), (aw / 2 - 0.1, yz, zz), 0.1, 0.1, D, 'iron', ml=0)
            # gate leaves swung open against the passage walls
            for sx in (-1, 1):
                x = sx * (aw / 2 + 0.6 - 0.75)
                b.box(x, -gd / 2 + t + 1.35, 0.0, 0.18, 2.4, az + 0.6, D, band='plank', ml=1)
    # flanking towers with hoarding and roofs
    for sx in (-1, 1):
        _square_tower(b, sx * (gw / 2 + tw / 2), -0.4, tw, td, th, key + 10 + sx, roofed=True, door_x=False,
                      roof='tile', front_slits=2)
    # gallery over the gate between the towers
    if b.state in ('Intact', 'Damaged'):
        b.box(0, 0, zg, gw, gd, 0.25, D, band='plank', ml=2)
        gops = {'front': [win(c, w=0.7, zb=zg + 0.9, h=0.8, cell=12 + i, lintel=False, sill=False) for i, c in enumerate((-2.2, 0.0, 2.2))],
                'back': [win(c, w=0.7, zb=zg + 0.9, h=0.8, cell=i, lintel=False, sill=False) for i, c in enumerate((-2.2, 2.2))]}
        b.wall((-gw / 2, -gd / 2 - 0.4), (gw / 2, -gd / 2 - 0.4), zg, zg + 2.4, 0.16, D, _ops(gw, gops['front']), ml=3, key=key + 20, band='plank')
        b.wall((gw / 2, gd / 2 + 0.2), (-gw / 2, gd / 2 + 0.2), zg, zg + 2.4, 0.16, D, _ops(gw, gops['back']), ml=3, key=key + 21, band='plank')
        b.push(b.at(0, -0.1, 0, math.pi / 2))
        b.gable_roof(-gd / 2 - 0.4, gd / 2 + 0.2, gw / 2, zg + 2.4, 40, 'tile', oh=0.2, ohg=0.4, key=key + 22)
        b.pop()
    elif b.building:
        b.scaffold((-gw / 2, gd / 2 + 0.3), (gw / 2, gd / 2 + 0.3), zg * 0.6, 1.0, key)
    if b.ruined:
        b.rubble(0, -gd / 2 - 3.0, 0.0, 3.0, 1.0, 9, seed=key + 7, ml=2)
    # road through the gate
    if b.lod <= 2:
        b.quad((-aw / 2 - 0.4, -gd / 2 - 3.0, 0.03), (aw / 2 + 0.4, -gd / 2 - 3.0, 0.03), (aw / 2 + 0.4, gd / 2 + 2.0, 0.03), (-aw / 2 - 0.4, gd / 2 + 2.0, 0.03), D,
               uvs=[(0, 0.93), (aw / 2, 0.93), (aw / 2, 0.935), (0, 0.935)])


# ---------------------------------------------------------------------- market


def stall(b, x, y, yaw, variant, cloth, key=0, ml=2):
    """Market stall prop at (x, y). variant: 0 gable awning, 1 lean-to canopy, 2 cart stall."""
    if not b.keep(min(ml, 2)):
        return
    b.push(b.at(x, y, 0, yaw))
    ruined = b.state == 'Ruin'
    if variant == 0:
        w, d, hp = 2.8, 1.8, 2.25
        b.box(0, 0, 0.0, w - 0.3, 1.0, 0.85, D, band='plank', ml=2)                  # counter
        b.box(0, 0, 0.85, w - 0.2, 1.1, 0.08, D, band='timber', ml=1)
        for sx in (-1, 1):
            for sy in (-1, 1):
                b.beam((sx * w / 2, sy * d / 2, 0), (sx * w / 2, sy * d / 2, hp), 0.1, 0.1, D, 'timber', ml=1)
        if not ruined:
            pitch = 24
            hz = (d / 2 + 0.25) * math.tan(math.radians(pitch))
            for sy in (-1, 1):
                for flip in (0, 1):
                    A = Vector((-w / 2 - 0.15, sy * (d / 2 + 0.25), hp - 0.05))
                    B = Vector((w / 2 + 0.15, sy * (d / 2 + 0.25), hp - 0.05))
                    C = Vector((w / 2 + 0.15, 0, hp + hz))
                    Dd = Vector((-w / 2 - 0.15, 0, hp + hz))
                    pts = [A, B, C, Dd] if (sy < 0) != flip else [B, A, Dd, C]
                    uvs = b.band_uv(cloth, 0, w + 0.3)
                    if (sy < 0) == flip:
                        uvs = [uvs[1], uvs[0], uvs[3], uvs[2]]
                    if flip:
                        pts = [p - Vector((0, 0, 0.01)) for p in pts]
                    b.face(pts, D, uvs, 1.0 if not flip else 0.6)
            # valance
            if b.lod == 0:
                b.box(0, -d / 2 - 0.25, hp - 0.35, w + 0.3, 0.02, 0.3, D, band=cloth, ml=0)
        if b.lod == 0:
            for k in range(4):
                b.box(-0.9 + k * 0.6, -0.1, 0.93, 0.45, 0.6, 0.18, D, band='goods', ml=0)
            b.cyl(w / 2 + 0.5, 0.4, 0.0, 0.55, 0.32, 0.36, 8, D, band='goods', ml=0)
    elif variant == 1:
        w, hb, hf = 3.0, 2.6, 1.95
        b.box(0, 0.8, 0.0, w, 0.12, hb, D, band='plank', ml=2)                         # back board
        b.box(0, 0.25, 0.0, w - 0.2, 0.9, 0.8, D, band='plank', ml=2)
        for sx in (-1, 1):
            b.beam((sx * w / 2, -0.8, 0), (sx * w / 2, -0.8, hf), 0.1, 0.1, D, 'timber', ml=1)
        if not ruined:
            A = Vector((-w / 2 - 0.1, -1.0, hf - 0.05))
            B = Vector((w / 2 + 0.1, -1.0, hf - 0.05))
            C = Vector((w / 2 + 0.1, 0.85, hb))
            Dd = Vector((-w / 2 - 0.1, 0.85, hb))
            b.face([A, B, C, Dd], D, b.band_uv(cloth, 0, w + 0.2))
            dz = Vector((0, 0, -0.01))   # underside offset: the coincident reversed face was dropped (verification fix)
            b.face([B + dz, A + dz, Dd + dz, C + dz], D, [b.band_uv(cloth, 0, w + 0.2)[i] for i in (1, 0, 3, 2)], 0.6)
        if b.lod == 0:
            for k in range(3):
                b.cyl(-1.0 + k * 1.0, -1.4, 0.0, 0.45, 0.32, 0.38, 8, D, band='goods', ml=0)
            b.box(0, 0.25, 0.8, w - 0.4, 0.7, 0.2, D, band='goods', ml=0)
    else:
        # two-wheeled cart with a flat canopy
        w, d = 2.6, 1.4
        b.box(0, 0, 0.7, w, d, 0.18, D, band='plank', ml=2)
        b.box(0, -d / 2 + 0.04, 0.88, w, 0.08, 0.4, D, band='plank', ml=1)
        b.box(0, d / 2 - 0.04, 0.88, w, 0.08, 0.4, D, band='plank', ml=1)
        if b.keep(1):
            for sy in (-1, 1):
                # wheel cap (cyl top) faces outward on both sides; the +Y wheel showed its open side (verification fix)
                b.push(b.at(0.2, sy * (d / 2 + 0.12), 0.62) @ Matrix.Rotation(-sy * math.pi / 2, 4, 'X'))
                b.cyl(0, 0, -0.06, 0.06, 0.6, 0.6, b.segs(10, 6), D, band='plank', ml=1)
                b.pop()
            for sy in (-1, 1):
                b.beam((-w / 2, sy * 0.35, 0.75), (-w / 2 - 1.8, sy * 0.3, 0.45), 0.08, 0.08, D, 'timber', ml=1)
            b.beam((0.2, -d / 2 - 0.2, 0.62), (0.2, d / 2 + 0.2, 0.62), 0.08, 0.08, D, 'iron', ml=1)
        for sx in (-1, 1):
            for sy in (-1, 1):
                b.beam((sx * (w / 2 - 0.1), sy * (d / 2 - 0.05), 0.85), (sx * (w / 2 - 0.1), sy * (d / 2 - 0.05), 2.3), 0.07, 0.07, D, 'timber', ml=1)
        if not ruined:
            A = Vector((-w / 2 - 0.2, -d / 2 - 0.25, 2.25))
            B = Vector((w / 2 + 0.2, -d / 2 - 0.25, 2.25))
            C = Vector((w / 2 + 0.2, d / 2 + 0.25, 2.4))
            Dd = Vector((-w / 2 - 0.2, d / 2 + 0.25, 2.4))
            b.face([A, B, C, Dd], D, b.band_uv(cloth, 0, w + 0.4))
            dz = Vector((0, 0, -0.01))
            b.face([B + dz, A + dz, Dd + dz, C + dz], D, [b.band_uv(cloth, 0, w + 0.4)[i] for i in (1, 0, 3, 2)], 0.6)
        if b.lod == 0:
            b.box(0.1, 0, 0.88, 1.8, 1.0, 0.3, D, band='goods', ml=0)
    b.pop()


def market_stall_a(b):
    stall(b, 0, 0, 0.0, 0, 'cloth_r', 81)


def market_stall_b(b):
    stall(b, 0, 0, 0.0, 1, 'cloth_o', 82)


def market_stall_c(b):
    stall(b, 0, 0, 0.0, 2, 'cloth_i', 83)


def market(b):
    """Colonnaded market square, 34 x 26 m: tiled galleries on timber posts round a paved court,
    rows of stalls under striped awnings, round fountain in the middle, entrances front and back."""
    key = 91
    b.shutters = False
    W, Dp, g = 34.0, 26.0, 4.6           # outer size, gallery depth
    zt, pitch = 3.5, 24
    plinth(b, W + 0.4, Dp + 0.4, top=0.15, below=0.4)
    gapF, gapB = 7.0, 5.0
    t = 0.55
    # outer back walls of the galleries (gaps for the entrances)
    segs = [((-W / 2, -Dp / 2 + t / 2), (-gapF / 2, -Dp / 2 + t / 2)), ((gapF / 2, -Dp / 2 + t / 2), (W / 2, -Dp / 2 + t / 2)),
            ((W / 2 - t / 2, -Dp / 2 + t), (W / 2 - t / 2, Dp / 2 - t)),
            ((W / 2, Dp / 2 - t / 2), (gapB / 2, Dp / 2 - t / 2)), ((-gapB / 2, Dp / 2 - t / 2), (-W / 2, Dp / 2 - t / 2)),
            ((-W / 2 + t / 2, Dp / 2 - t), (-W / 2 + t / 2, -Dp / 2 + t))]
    for k, (p0, p1) in enumerate(segs):
        L = (Vector(p1) - Vector(p0)).length
        ops = []
        if L > 10:
            n = int(L / 4.0)
            for i in range(n):
                c = -L / 2 + (i + 0.5) * L / n
                ops.append(win(c, w=0.9, zb=1.6, h=1.0, cell=(k * 3 + i) % 16, lintel=True))
        b.wall(p0, p1, -0.3, zt, t, S, _ops(L, ops), ml=3, key=key + k, ends=True)
    # galleries: posts on stone bases + plates + gable roofs
    ix0, ix1, iy0, iy1 = -W / 2 + g, W / 2 - g, -Dp / 2 + g, Dp / 2 - g
    lines = [((-W / 2 + 0.3, iy0), (-gapF / 2 - 0.2, iy0)), ((gapF / 2 + 0.2, iy0), (W / 2 - 0.3, iy0)),
             ((W / 2 - 0.3, iy1), (gapB / 2 + 0.2, iy1)), ((-gapB / 2 - 0.2, iy1), (-W / 2 + 0.3, iy1)),
             ((ix0, iy0), (ix0, iy1)), ((ix1, iy0), (ix1, iy1))]
    standing = b.state in ('Intact', 'Damaged')
    for k, (p0, p1) in enumerate(lines):
        p0, p1 = Vector((p0[0], p0[1], 0)), Vector((p1[0], p1[1], 0))
        L = (p1 - p0).length
        n = max(1, int(round(L / 3.0)))
        for i in range(n + 1):
            p = p0 + (p1 - p0) * (i / n)
            hp = zt - 0.15
            if b.state == 'Ruin':
                if hash01(key, k, i) > 0.35:
                    continue
                hp = 0.6 + 1.8 * hash01(key, k, i, 2)
            elif b.building and hash01(key, k, i, 4) > 0.6:
                continue
            b.box(p.x, p.y, 0.0, 0.55, 0.55, 0.45, S, ml=1)
            b.beam(p + Vector((0, 0, 0.45)), p + Vector((0, 0, hp)), 0.3, 0.3, D, 'timber', ml=2)
            if b.lod == 0 and standing:
                b.beam(p + Vector((0, 0, hp - 0.25)), p + Vector((0, 0, hp)), 0.5, 0.5, D, 'timber', ml=0)
                dirv = (p1 - p0).normalized()
                for sg in (-1, 1):
                    if (i == 0 and sg < 0) or (i == n and sg > 0):
                        continue
                    b.beam(p + Vector((0, 0, hp - 0.9)), p + dirv * (sg * 0.75) + Vector((0, 0, hp - 0.05)), 0.12, 0.12, D, 'timber', ml=0)
        if standing:
            b.beam(p0 + Vector((0, 0, zt - 0.15)), p1 + Vector((0, 0, zt - 0.15)), 0.3, 0.3, D, 'timber', ml=1)
    hw = g / 2
    # front wings (split by the entrance), back wings, side ranges (penetrating into the corners)
    for (x0, x1) in ((-W / 2, -gapF / 2 - 0.2), (gapF / 2 + 0.2, W / 2)):
        b.push(b.at(0, -Dp / 2 + hw))
        b.gable_roof(x0, x1, hw, zt, pitch, 'tile', oh=0.45, ohg=0.25, key=key + int(x0), strips=0.25)
        b.pop()
    for (x0, x1) in ((-W / 2, -gapB / 2 - 0.2), (gapB / 2 + 0.2, W / 2)):
        b.push(b.at(0, Dp / 2 - hw))
        b.gable_roof(x0, x1, hw, zt, pitch, 'tile', oh=0.45, ohg=0.25, key=key + 50 + int(x0), strips=0.25)
        b.pop()
    for sx in (-1, 1):
        b.push(b.at(sx * (W / 2 - hw), 0, 0, math.pi / 2))
        b.gable_roof(-Dp / 2 + hw, Dp / 2 - hw, hw, zt, pitch, 'tile', oh=0.45, ohg=0.0, key=key + 70 + sx, strips=0.25)
        b.pop()
    # gable infill at the entrance ends of the front/back wings
    hz = hw * math.tan(math.radians(pitch))
    for (x, y, sgn) in ((-gapF / 2 - 0.2, -Dp / 2 + hw, 1), (gapF / 2 + 0.2, -Dp / 2 + hw, -1),
                        (-gapB / 2 - 0.2, Dp / 2 - hw, 1), (gapB / 2 + 0.2, Dp / 2 - hw, -1)):
        b.gable_tri((x, y - sgn * hw), (x, y + sgn * hw), zt - 0.15, zt + hz, 0.3, D, band='plank')
    # paved court (slightly raised) and fountain
    if b.keep(2):
        b.box(0, 0, 0.0, ix1 - ix0 - 0.4, iy1 - iy0 - 0.4, 0.2, S, ml=2, top=True)
    fr = 2.4
    sides = b.segs(20, 8)
    if b.state != 'Ruin':
        b.cyl(0, 0, 0.0, 0.32, fr + 0.9, fr + 0.9, sides, S, ml=1)                  # step
        b.cyl(0, 0, 0.3, 0.95, fr, fr, sides, S, ml=2, top=False)                   # basin wall
        b.cyl(0, 0, 0.3, 0.95, fr - 0.35, fr - 0.35, sides, S, ml=1, top=False, a0=0.0)
        # rim top ring
        if b.keep(1):
            for i in range(sides):
                a0, a1 = 2 * math.pi * i / sides, 2 * math.pi * (i + 1) / sides
                ro, ri = fr, fr - 0.35
                b.face([(ro * math.cos(a0), ro * math.sin(a0), 0.95), (ro * math.cos(a1), ro * math.sin(a1), 0.95),
                        (ri * math.cos(a1), ri * math.sin(a1), 0.95), (ri * math.cos(a0), ri * math.sin(a0), 0.95)], S)
        b.cone(0, 0, 0.8, 0.8, fr - 0.35, sides, D, ml=2, band='water')            # water surface
        b.cyl(0, 0, 0.8, 2.2, 0.35, 0.28, b.segs(10, 6), S, ml=1)                    # column
        b.cyl(0, 0, 2.2, 2.45, 0.9, 0.75, b.segs(12, 6), S, ml=1)                    # bowl
        b.cone(0, 0, 2.45, 2.45, 0.7, b.segs(12, 6), D, ml=1, band='water')
    else:
        b.rubble(0, 0, 0.0, 2.6, 0.9, 8, seed=key + 3, ml=2)
    # stalls in rows aligned with the colonnade
    cl = ('cloth_r', 'cloth_o', 'cloth_i')
    k = 0
    for x in (-10.0, -5.0, 5.0, 10.0):
        ys = (-6.6, -2.2, 2.2, 6.6) if abs(x) > 6 else (-5.2, 5.2)
        for y in ys:
            if b.state == 'Ruin' and hash01(key, k, 9) > 0.3:
                k += 1
                continue
            if b.building:
                k += 1
                continue
            v = int(hash01(key, k, 1) * 3) % 3
            stall(b, x, y, math.pi / 2 if x < 0 else -math.pi / 2, v, cl[int(hash01(key, k, 2) * 3) % 3], key + k, ml=2)
            k += 1
    # goods along the gallery walls
    if b.lod == 0 and standing:
        for i in range(10):
            x = -W / 2 + 2.0 + i * (W - 4.0) / 9
            if abs(x) < gapB / 2 + 0.5:
                continue
            b.box(x, Dp / 2 - 1.2, 0.15, 1.0, 0.8, 0.6, D, band='goods', ml=0)
            if abs(x) > gapF / 2 + 0.5:
                b.box(x + 0.3, -Dp / 2 + 1.2, 0.15, 0.9, 0.8, 0.5, D, band='goods', ml=0)
    if b.state in ('Intact', 'Damaged') and b.lod <= 1:
        stall(b, -1.5, -Dp / 2 - 3.2, 0.3, 2, 'cloth_o', key + 40, ml=1)
        stall(b, 2.0, Dp / 2 + 3.0, math.pi - 0.2, 2, 'cloth_i', key + 41, ml=1)
    if b.damaged:
        burn_spot(b, -W / 2 + hw, 6.0, zt, 6.0)
    if b.ruined:
        for k2, (x, y) in enumerate(((-12, -Dp / 2 + 2), (8, Dp / 2 - 2), (W / 2 - 2, 3))):
            b.rubble(x, y, 0.0, 2.6, 1.0, 8, seed=key + 20 + k2, ml=2)
    if b.building:
        for x in (-8.0, 8.0):
            b.stone_pile(x, 0.0, 9, key + int(x))


# ---------------------------------------------------------------------- temple


def temple(b):
    """Pillared stone temple on a three-step stylobate, 6 x 11 columns, pale stone roof with
    pediments; front (pediment + door) toward -Y. ~22 x 37 m, 14 m high."""
    key = 101
    cw, cl = 18.6, 33.4                  # column-centre rectangle
    nx, ny = 6, 11
    st = [(23.2, 38.0, 0.45), (22.0, 36.8, 0.9), (20.8, 35.6, 1.35)]
    for (w, d, z) in st:
        b.box(0, 0, -0.35, w, d, z + 0.35, S, ml=3)
    zs = 1.35
    zc = zs + 7.6                       # column top
    zt = zc + 1.9                       # top of the entablature
    # columns
    pos = []
    for i in range(nx):
        x = -cw / 2 + cw * i / (nx - 1)
        for y in (-cl / 2, cl / 2):
            pos.append((x, y))
    for j in range(1, ny - 1):
        y = -cl / 2 + cl * j / (ny - 1)
        for x in (-cw / 2, cw / 2):
            pos.append((x, y))
    sides = (24, 10, 6, 4)[b.lod]
    for k, (x, y) in enumerate(pos):
        hfrac = 1.0
        if b.building:
            hfrac = 1.0 if y < -cl / 2 + 1 else 0.25 + 0.75 * hash01(key, k, 1)
        elif b.ruined:
            r = hash01(key, k, 2)
            hfrac = 0.08 + 0.2 * r if r < 0.45 else (0.4 + 0.4 * r if r < 0.75 else 1.0)
        elif b.damaged and hash01(key, k, 3) < 0.07:
            hfrac = 0.15
        h = (zc - 0.35 - zs - 0.3) * hfrac
        b.box(x, y, zs, 1.4, 1.4, 0.3, S, ml=1)
        if b.lod < 3:
            b.cyl(x, y, zs + 0.3, zs + 0.3 + h, 0.56, 0.56 - 0.1 * hfrac, sides, D, band='dressed', ml=2, top=hfrac < 1)
        if hfrac >= 1.0:
            b.cyl(x, y, zc - 0.35 - 0.25, zc - 0.35, 0.47, 0.68, max(4, sides), S, ml=1, top=False)
            b.box(x, y, zc - 0.35, 1.45, 1.45, 0.35, S, ml=1)
        elif b.ruined and b.lod <= 1 and hash01(key, k, 4) < 0.5:   # fallen drums
            a = hash01(key, k, 5) * 6.28
            for dk in range(3):
                dx, dy = x + math.cos(a) * (2.5 + 1.6 * dk), y + math.sin(a) * (2.5 + 1.6 * dk)
                if abs(dx) < 11 and abs(dy) < 19:
                    continue
                b.push(b.at(dx, dy, 0.55, a) @ Matrix.Rotation(math.pi / 2, 4, 'Y'))
                b.cyl(0, 0, -0.75, 0.75, 0.55, 0.55, max(6, sides - 4), S, ml=1)
                b.pop()
    # cella
    cx0, cx1, cy0, cy1 = -6.4, 6.4, -10.8, 14.4
    rect_walls(b, cx0, cx1, cy0, cy1, zs - 0.3, zc if b.state != 'Ruin' else zc, 1.0, {
        'front': [dict(c=0.0, w=3.4, zb=zs, h=6.2, kind='door')],
        'right': [], 'back': [], 'left': [],
    }, key, ml=3)
    if b.state in ('Intact', 'Damaged'):
        # pronaos antae
        for sx in (-1, 1):
            b.box(sx * 5.9, cy0 - 3.0, zs, 1.0, 6.0, zc - zs, S, ml=2)
        # entablature block (architrave + frieze) with a projecting cornice
        b.box(0, 0, zc, cw + 1.6, cl + 1.6, 1.5, S, ml=3, bottom=True)
        b.box(0, 0, zc + 1.5, cw + 2.2, cl + 2.2, 0.4, S, ml=2, bottom=True)
        if b.lod == 0:   # dentils under the cornice
            ex_, ey_ = cw / 2 + 0.8, cl / 2 + 0.8
            for sy in (-1, 1):
                n = int(2 * ex_ / 0.62)
                for i in range(n):
                    b.box(-ex_ + (i + 0.5) * 2 * ex_ / n, sy * (ey_ + 0.12), zc + 1.28, 0.3, 0.24, 0.22, S, ml=0)
            for sx in (-1, 1):
                n = int(2 * ey_ / 0.62)
                for i in range(n):
                    b.box(sx * (ex_ + 0.12), -ey_ + (i + 0.5) * 2 * ey_ / n, zc + 1.28, 0.24, 0.3, 0.22, S, ml=0)
        if b.lod == 0:   # triglyph blocks round the frieze, with dark grooves
            fx, fy = cw / 2 + 0.8, cl / 2 + 0.8
            for sy in (-1, 1):
                n = int(2 * fx / 1.55)
                for i in range(n + 1):
                    x = -fx + 0.4 + i * (2 * fx - 0.8) / n
                    b.box(x, sy * (fy + 0.05), zc + 0.75, 0.75, 0.12, 0.72, S, ml=0)
                    b.box(x, sy * (fy + 0.115), zc + 0.8, 0.12, 0.02, 0.6, S, ml=0, shade=0.5)
            for sx in (-1, 1):
                n = int(2 * fy / 1.55)
                for i in range(n + 1):
                    y = -fy + 0.4 + i * (2 * fy - 0.8) / n
                    b.box(sx * (fx + 0.05), y, zc + 0.75, 0.12, 0.75, 0.72, S, ml=0)
                    b.box(sx * (fx + 0.115), y, zc + 0.8, 0.02, 0.12, 0.6, S, ml=0, shade=0.5)
        # roof (ridge along Y) with pediments
        pitch = 15
        half = (cw + 2.2) / 2
        hz = half * math.tan(math.radians(pitch))
        for sy in (-1, 1):
            y = sy * (cl / 2 + 0.8)
            p0, p1 = ((-half + 0.3, y), (half - 0.3, y)) if sy < 0 else ((half - 0.3, y), (-half + 0.3, y))
            b.gable_tri(p0, p1, zt, zt + hz - 0.25, 0.8, S)
            # raking cornices
            if b.lod <= 1:
                for sx in (-1, 1):
                    b.beam((sx * half, y + sy * 0.15, zt + 0.05), (0, y + sy * 0.15, zt + hz + 0.1), 0.6, 0.35, S, None, ml=1)
            if b.lod == 0:   # acroteria
                b.box(0, y + sy * 0.1, zt + hz + 0.1, 0.8, 0.4, 0.9, S, ml=0)
                for sx in (-1, 1):
                    b.box(sx * (half - 0.2), y + sy * 0.1, zt + 0.1, 0.6, 0.4, 0.6, S, ml=0)
        b.push(b.at(0, 0, 0, math.pi / 2))
        b.gable_roof(-cl / 2 - 0.8, cl / 2 + 0.8, half - 0.1, zt, pitch, 'stone', oh=0.15, ohg=0.25, key=key + 3, strips=0.5)
        b.pop()
        # altar and fire bowl before the steps
    if b.keep(1):
        b.box(0, -cl / 2 - 6.5, -0.3, 2.6, 1.6, 1.4, S, ml=1)
        if b.state != 'Ruin':
            b.cyl(0, -cl / 2 - 6.5, 1.1, 1.45, 0.55, 0.7, b.segs(10, 6), D, band='bronze', ml=0)
    if b.building:
        b.scaffold((cx0, cy0 - 0.2), (cx1, cy0 - 0.2), zc * 0.7, 1.3, key)
        b.scaffold((cx1 + 0.2, cy0), (cx1 + 0.2, cy1), zc * 0.6, 1.3, key + 1)
        b.stone_pile(-13.5, -8.0, 9, key)
        b.stone_pile(13.5, 6.0, 9, key + 1)
    if b.damaged:
        burn_spot(b, 3.0, 6.0, zt, 7.0)
        b.rubble(cw / 2 + 2.5, 4.0, 0.0, 2.2, 1.0, 7, seed=key + 9, ml=1)
    if b.ruined:
        b.rubble(0, 2.0, zs, 4.5, 1.6, 9, seed=key + 11, ml=2)
        b.rubble(-cw / 2 - 3.5, -6.0, 0.0, 2.6, 1.0, 8, seed=key + 12, ml=1)
        b.rubble(cw / 2 + 3.0, 9.0, 0.0, 3.0, 1.2, 8, seed=key + 13, ml=1)


# ---------------------------------------------------------------------- academy


def academy(b):
    """Iron-age academy: a cloistered limestone quadrangle (two storeys, slate roofs) round a
    garden court with an armillary sphere, a pedimented entrance porch on the front (-Y) and a
    round observatory tower with a verdigris bronze dome rising from the back range.
    46 x 38 m footprint, dome top ~22 m."""
    key = 121
    b.shutters = False
    b.grime_h = 2.2
    W, Dp, dr = 44.0, 36.0, 8.0          # outer size and range depth
    z1, t = 8.2, 0.75
    plinth(b, W + 0.6, Dp + 0.6, top=0.3)
    # outer walls: two rows of windows, entrance arch in front, doors elsewhere
    def row(L, zb, h, w, step, skip=(), cell0=0):
        n = int(L / step)
        out = []
        for i in range(n):
            c = -L / 2 + (i + 0.5) * L / n
            if any(abs(c - s) < 2.4 for s in skip):
                continue
            out.append(win(c, w=w, zb=zb, h=h, cell=(cell0 + i) % 16))
        return out
    ops = {
        'front': row(W, 1.5, 2.2, 1.1, 3.2, (0.0,)) + row(W, 5.2, 2.0, 1.0, 3.2, (), 5) + [dict(c=0.0, w=3.0, zb=0.3, h=4.4, kind='arch')],
        'back': row(W, 1.5, 2.2, 1.1, 3.2, (0.0,), 2) + row(W, 5.2, 2.0, 1.0, 3.2, (0.0,), 7),
        'right': row(Dp, 1.5, 2.2, 1.1, 3.2, (), 4) + row(Dp, 5.2, 2.0, 1.0, 3.2, (), 9),
        'left': row(Dp, 1.5, 2.2, 1.1, 3.2, (), 6) + row(Dp, 5.2, 2.0, 1.0, 3.2, (), 11),
    }
    rect_walls(b, -W / 2, W / 2, -Dp / 2, Dp / 2, -0.3, z1, t, ops, key)
    # courtyard walls (faces toward the court)
    cx0, cx1, cy0, cy1 = -W / 2 + dr, W / 2 - dr, -Dp / 2 + dr, Dp / 2 - dr
    cw, cd = cx1 - cx0, cy1 - cy0
    iops = {
        'front': row(cw, 5.2, 1.8, 0.9, 3.4, (), 3) + [door(c, w=1.4, h=2.6, zb=0.3) for c in (-6.0, 6.0)],
        'back': row(cw, 5.2, 1.8, 0.9, 3.4, (), 8) + [door(0.0, w=1.6, h=2.8, zb=0.3)],
        'right': row(cd, 5.2, 1.8, 0.9, 3.4, (), 1) + [door(0.0, w=1.4, h=2.6, zb=0.3)],
        'left': row(cd, 5.2, 1.8, 0.9, 3.4, (), 12) + [door(0.0, w=1.4, h=2.6, zb=0.3)],
    }
    rect_walls(b, cx0, cx1, cy0, cy1, -0.3, z1, t, iops, key + 1, inward=True)
    # cloister arcade around the court with a lean-to roof against the ranges
    ca = 2.6
    ax0, ax1, ay0, ay1 = cx0 + ca, cx1 - ca, cy0 + ca, cy1 - ca
    za = 4.2
    aops = {}
    for side, L in (('front', ax1 - ax0), ('back', ax1 - ax0), ('right', ay1 - ay0), ('left', ay1 - ay0)):
        n = int(L / 3.1)
        aops[side] = [dict(c=-L / 2 + (i + 0.5) * L / n, w=2.1, zb=0.0, h=3.25, kind='arch') for i in range(n)]
    rect_walls(b, ax0, ax1, ay0, ay1, -0.3, za, 0.6, aops, key + 2, inward=True, ml=3)
    if b.state in ('Intact', 'Damaged'):
        for (yaw, a, bb, off) in ((0, ax0 - 0.6, ax1 + 0.6, ay0 - 0.3), (math.pi, -(ax1 + 0.6), -(ax0 - 0.6), -(ay1 + 0.3)),
                                  (math.pi / 2, ay0 - 0.6, ay1 + 0.6, -(ax1 + 0.3)), (-math.pi / 2, -(ay1 + 0.6), -(ay0 - 0.6), ax0 - 0.3)):
            b.push(b.at(0, 0, 0, yaw))
            b.lean_to(a, bb, off + 0.0, off - (ca + 0.0), za, 17, 'slate', oh=0.35, key=key + 30 + int(yaw * 10), ml=2)
            b.pop()
    # roofs: hipped front and back ranges, gabled side ranges running into them
    pitch = 32
    b.hip_roof(-W / 2, W / 2, -Dp / 2, -Dp / 2 + dr, z1, pitch, 'slate', oh=0.45, key=key + 4)
    b.hip_roof(-W / 2, W / 2, Dp / 2 - dr, Dp / 2, z1, pitch, 'slate', oh=0.45, key=key + 5)
    for sx in (-1, 1):
        b.push(b.at(sx * (W / 2 - dr / 2), 0, 0, math.pi / 2))
        b.gable_roof(-Dp / 2 + dr / 2, Dp / 2 - dr / 2, dr / 2, z1, pitch, 'slate', oh=0.45, ohg=0.0, key=key + 6 + sx)
        b.pop()
    # cornice band
    if b.lod <= 1 and b.state in ('Intact', 'Damaged'):
        for (x, y, sx, sy) in ((0, -Dp / 2 - 0.1, W + 0.4, 0.35), (0, Dp / 2 + 0.1, W + 0.4, 0.35), (-W / 2 - 0.1, 0, 0.35, Dp), (W / 2 + 0.1, 0, 0.35, Dp)):
            b.box(x, y, z1 - 0.35, sx, sy, 0.35, S, ml=1)
            b.box(x, y, 4.1, sx, sy, 0.25, S, ml=0)
    # chimneys
    for (x, y) in ((-14.0, -Dp / 2 + dr / 2), (14.0, Dp / 2 - dr / 2), (-W / 2 + dr / 2, 8.0), (W / 2 - dr / 2, -8.0)):
        b.chimney(x, y, z1, z1 + 4.6, 1.0, 0.9, ml=1)
    # entrance porch: four columns and a pediment
    pz = 6.6
    pw, pd = 11.0, 4.6
    py0 = -Dp / 2 - pd
    b.box(0, py0 + pd / 2 - 0.3, -0.3, pw + 1.2, pd + 0.6, 0.6, S, ml=2)
    b.box(0, py0 - 0.5, -0.3, pw - 1.0, 1.0, 0.3, S, ml=1)
    for i in range(4):
        x = -pw / 2 + 0.8 + i * (pw - 1.6) / 3
        hfrac = 1.0 if b.state != 'Ruin' else 0.2 + 0.5 * hash01(key, i)
        if b.building and i % 3 != 0:
            hfrac = 0.5
        b.cyl(x, py0 + 0.6, 0.3, 0.3 + (pz - 0.6) * hfrac, 0.42, 0.36, (12, 8, 6, 4)[b.lod], D, band='dressed', ml=2, top=hfrac < 1)
    if b.state in ('Intact', 'Damaged'):
        b.box(0, py0 + pd / 2 + 0.1, pz - 0.3, pw, pd + 0.4, 1.0, S, ml=2, bottom=True)
        phz = (pw / 2) * math.tan(math.radians(22))
        b.gable_tri((-pw / 2, py0 + 0.1), (pw / 2, py0 + 0.1), pz + 0.7, pz + 0.7 + phz, 0.5, S)
        b.push(b.at(0, py0 + pd / 2, 0, math.pi / 2))
        b.gable_roof(-pd / 2 - 0.2, pd / 2 + 1.5, pw / 2, pz + 0.7, 22, 'slate', oh=0.3, ohg=0.3, key=key + 9)
        b.pop()
    # observatory tower on the back range with a verdigris dome
    tr = 4.6
    ty = Dp / 2 - dr / 2
    th = 15.5 if b.state in ('Intact', 'Damaged') else (9.0 if b.building else 6.5)
    tsides = (20, 12, 8, 6)[b.lod]
    b.cyl(0, ty, -0.3, th, tr, tr, tsides, S, ml=3, top=b.state not in ('Intact', 'Damaged'))
    if b.state in ('Intact', 'Damaged'):
        if b.lod <= 1:
            b.cyl(0, ty, th - 0.6, th, tr + 0.35, tr + 0.35, tsides, S, ml=1)
            b.cyl(0, ty, z1 + 1.4, z1 + 1.7, tr + 0.25, tr + 0.25, tsides, S, ml=0)
            for i in range(6):
                a = 2 * math.pi * (i + 0.5) / 6
                p = Vector((math.cos(a) * (tr + 0.02), ty + math.sin(a) * (tr + 0.02), 0))
                tng = Vector((-math.sin(a), math.cos(a), 0)) * 0.45
                b.quad(p - tng + Vector((0, 0, 11.0)), p + tng + Vector((0, 0, 11.0)), p + tng + Vector((0, 0, 13.4)), p - tng + Vector((0, 0, 13.4)), D,
                       b.band_uv('window', 0, 0, cell=i + 4))
        b.dome(0, ty, th, tr + 0.2, (7, 4, 3, 2)[b.lod], tsides, D, band='bronze', ml=3, squash=0.92)
        if b.lod <= 1:
            # ribs and the observing slit
            for i in range(8 if b.lod == 0 else 4):
                a = 2 * math.pi * i / (8 if b.lod == 0 else 4)
                pts = []
                for k in range(7):
                    ph = (math.pi / 2) * k / 6
                    r = (tr + 0.28) * math.cos(ph)
                    pts.append((math.cos(a) * r, ty + math.sin(a) * r, th + (tr + 0.28) * 0.92 * math.sin(ph)))
                b.tube(pts, 0.12, 4, D, band='bronze', ml=1)
            pts = []
            for k in range(6):
                ph = (math.pi / 2) * k / 6 * 0.95
                r = (tr + 0.24) * math.cos(ph)
                pts.append((-0.0 + 0.0, ty - r, th + (tr + 0.24) * 0.92 * math.sin(ph)))
            b.tube(pts, 0.32, 3, D, band='char', ml=1, half=True)
            # lantern
            b.cyl(0, ty, th + (tr + 0.2) * 0.92 - 0.1, th + (tr + 0.2) * 0.92 + 1.2, 0.8, 0.8, 8, D, band='bronze', ml=1)
            b.cone(0, ty, th + (tr + 0.2) * 0.92 + 1.2, th + (tr + 0.2) * 0.92 + 2.4, 1.0, 8, D, band='bronze', ml=1)
    # garden court: hedged parterre beds, gravel, armillary sphere
    gx0, gx1, gy0, gy1 = ax0 + 0.6, ax1 - 0.6, ay0 + 0.6, ay1 - 0.6
    if b.keep(2):
        v0 = 0.93
        b.quad((gx0, gy0, 0.31), (gx1, gy0, 0.31), (gx1, gy1, 0.31), (gx0, gy1, 0.31), D,
               [(0, 0.9075), (gx1 - gx0, 0.9075), (gx1 - gx0, 0.9355), (0, 0.9355)])
    if b.state != 'Ruin' and not b.building:
        bw, bd = (gx1 - gx0) / 2 - 1.6, (gy1 - gy0) / 2 - 1.6
        for sx in (-1, 1):
            for sy in (-1, 1):
                x = sx * (bw / 2 + 0.8)
                y = (gy0 + gy1) / 2 + sy * (bd / 2 + 0.8)
                b.box(x, y, 0.3, bw, bd, 0.55, D, band='hedge', ml=2)
                if b.lod == 0:
                    b.cyl(x - sx * bw * 0.25, y, 0.85, 2.6, 0.5, 0.15, 6, D, band='hedge', ml=0)
        if b.keep(1):
            cy = (gy0 + gy1) / 2
            b.cyl(0, cy, 0.3, 1.4, 0.45, 0.35, 8, S, ml=1)
            for k, (axis, ang) in enumerate((('Z', 0.0), ('X', math.pi / 2), ('X', math.pi / 4))):
                pts = []
                for i in range(17):
                    a = 2 * math.pi * i / 16
                    v = Vector((math.cos(a) * 1.3, math.sin(a) * 1.3, 0))
                    v = Matrix.Rotation(ang, 3, axis) @ v if axis == 'X' else v
                    pts.append(v + Vector((0, cy, 2.75)))
                b.tube(pts, 0.06, 4, D, band='bronze', ml=1)
    if b.building:
        b.scaffold((-W / 2, -Dp / 2), (W / 2, -Dp / 2), z1 * 0.6, 1.3, key)
        b.scaffold((W / 2, -Dp / 2), (W / 2, Dp / 2), z1 * 0.5, 1.3, key + 1)
        b.stone_pile(-W / 2 - 3.0, 4.0, 9, key)
        b.stone_pile(0, 2.0, 9, key + 1)
    if b.damaged:
        burn_spot(b, W / 2 - dr / 2, -6.0, z1 + 2, 7.0)
        b.rubble(W / 2 + 2.5, -6.0, 0.0, 2.6, 1.2, 8, seed=key + 13, ml=1)
    if b.ruined:
        for k, (x, y) in enumerate(((-W / 2 - 2.5, -8.0), (W / 2 + 2.5, 6.0), (-10.0, -Dp / 2 - 2.5), (12.0, Dp / 2 + 2.5), (0, 0))):
            b.rubble(x, y, 0.0, 3.2, 1.4, 9, seed=key + 20 + k, ml=2)


# ---------------------------------------------------------------------- culture ornaments
# Small ridge-end ornaments: the integration agent attaches one at each ridge end listed under
# "ridge_ends" in the manifest (houses) to give a culture its own roofline. Pivot = ridge point.


def ornament_a(b):
    """Cliff-Terrace: a small lime-plaster dome finial with a cobalt knob."""
    b.push(Matrix.Scale(1.4, 4))
    b.cyl(0, 0, -0.15, 0.25, 0.26, 0.26, b.segs(10, 6), D, band='plaster', ml=2, top=False)
    b.dome(0, 0, 0.25, 0.27, (4, 2, 2, 1)[b.lod], b.segs(10, 6), D, band='plaster', ml=2)
    # the cobalt knob is the culture's colour accent: kept down to LOD2 (verification fix)
    b.cyl(0, 0, 0.5, 0.62, 0.07, 0.07, 6 if b.lod < 2 else 4, D, band='cloth_i', ml=2, top=False)
    b.dome(0, 0, 0.62, 0.11, 2 if b.lod < 2 else 1, 6 if b.lod < 2 else 4, D, band='cloth_i', ml=2)
    b.pop()


def ornament_b(b):
    """Lagoon-Weaver: an upswept carved timber horn with a saffron ribbon."""
    b.push(Matrix.Scale(1.4, 4))
    pts = []
    for k in range(7):
        t = k / 6
        pts.append((0.0, -0.05 - 0.35 * t, 0.05 + 0.75 * t ** 1.6))
    b.tube(pts, 0.09, b.segs(5, 3), D, band='timber', ml=2, caps=True)
    b.beam((0, -0.02, 0.0), (0, 0.4, 0.0), 0.16, 0.12, D, 'timber', ml=1)
    if b.lod <= 1:
        b.quad((-0.06, -0.3, 0.4), (0.06, -0.3, 0.4), (0.06, -0.55, -0.25), (-0.06, -0.55, -0.25), D, b.band_uv('cloth_o', 0, 0.12))
        # back of the ribbon, offset 4 mm so it is not dropped as a duplicate face (verification fix)
        b.quad((0.06, -0.3037, 0.4015), (-0.06, -0.3037, 0.4015), (-0.06, -0.5537, -0.2485), (0.06, -0.5537, -0.2485), D, b.band_uv('cloth_o', 0, 0.12))
    b.pop()


def ornament_c(b):
    """Ring-Builder: a pale stone ring finial on a short post."""
    b.push(Matrix.Scale(1.4, 4))
    b.cyl(0, 0, -0.1, 0.35, 0.12, 0.1, b.segs(8, 5), D, band='dressed', ml=2, top=False)
    pts = []
    n = b.segs(12, 6)
    for k in range(n + 1):
        a = 2 * math.pi * k / n
        pts.append((0.0, 0.3 * math.cos(a), 0.66 + 0.3 * math.sin(a)))
    b.tube(pts, 0.07, b.segs(5, 3), D, band='dressed', ml=2)
    b.pop()


# ---------------------------------------------------------------------- registry
# name -> (function, structure texture, roof texture, kind, states)

ASSETS = {
    'House_A': (house_a, 'Rubble', 'Thatch', 'house'),
    'House_B': (house_b, 'Rubble', 'Shingle', 'house'),
    'House_C': (house_c, 'Rubble', 'Shingle', 'house'),
    'House_D': (house_d, 'Rubble', 'Thatch', 'house'),
    'WallSeg': (wall_seg, 'Ashlar', 'TileClay', 'wall'),
    'WallTower_A': (wall_tower_a, 'Ashlar', 'TileClay', 'wall'),
    'WallTower_B': (wall_tower_b, 'Ashlar', 'TileClay', 'wall'),
    'Gatehouse': (gatehouse, 'Ashlar', 'TileClay', 'wall'),
    'Market': (market, 'Limestone', 'TileClay', 'civic'),
    'Temple': (temple, 'Limestone', 'TileStone', 'civic'),
    'Academy': (academy, 'Limestone', 'Slate', 'civic'),
    'MarketStall_A': (market_stall_a, 'Limestone', 'TileClay', 'prop'),
    'MarketStall_B': (market_stall_b, 'Limestone', 'TileClay', 'prop'),
    'MarketStall_C': (market_stall_c, 'Limestone', 'TileClay', 'prop'),
    'Ornament_A': (ornament_a, 'Limestone', 'TileClay', 'prop'),
    'Ornament_B': (ornament_b, 'Limestone', 'TileClay', 'prop'),
    'Ornament_C': (ornament_c, 'Limestone', 'TileClay', 'prop'),
}
