# DEMIURGE ships - the eight Ship-family assets, each built for a LOD (0..3) and a state (Intact, Damaged, Ruin).
# Bow toward -Y, waterline at Z = 0, pivot at the centre of the waterplane.
import math
import random

from mathutils import Matrix, Vector

import ship_geo as g
from ship_geo import Part, HullSpec

STATES = ('Intact', 'Damaged', 'Ruin')
CULTURES = ['#e69f00', '#56b4e9', '#009e73', '#f0e442', '#d55e00', '#cc79a7']

# faces re-banded when burnt (same sheet, so the material slot never changes)
CHAR = {'plank_tar': 'char', 'plank_oiled': 'char', 'deck': 'char', 'reed': 'char', 'steel_black': 'char',
        'steel_deck': 'char', 'composite': 'char', 'stripe': 'char', 'steel_red': 'steel_red',
        'sail': 'sail_torn', 'sail_stripe': 'sail_torn', 'super_win': 'char_roof', 'paint_white': 'char_roof',
        'glass': 'char_roof', 'containers': 'metal', 'spar': 'metal', 'oar': 'metal', 'flag': 'rope', 'cargo': 'rope',
        'gold': 'metal', 'glow': 'metal'}


class Ctx:
    def __init__(self, lod, state, seed):
        self.lod, self.state = lod, state
        g.LOD = lod                                       # deck strip width per LOD (ship_geo.deck_strip_lines)
        self.P = Part()
        self.r = random.Random(seed * 31 + 7)            # same per LOD so states match across LODs
        self.q = (1.0, 0.55, 0.30, 0.12)[lod]
        self.dmg = state in ('Damaged', 'Ruin')
        self.ruin = state == 'Ruin'

    def n(self, base, lo):
        return max(lo, int(round(base * self.q)))

    def hashn(self, *k):
        h = 2166136261
        for x in k:
            h = ((h ^ (int(x * 1000) & 0xffffffff)) * 16777619) & 0xffffffff
        return (h & 0xffff) / 65535.0


# --------------------------------------------------------------------------- shared parts


def billow_square(c, yard, w_top, w_bot, h, depth, band, ns, nt, keep=None, two=True):
    """Square sail hanging from a yard at `yard` (Vector), set across the ship, bellied forward (-Y)."""
    def f(s, t):
        a = 1.0 - t                       # 0 at the head, 1 at the foot
        w = w_bot + (w_top - w_bot) * t
        x = (s - 0.5) * w
        z = yard.z - 0.25 - a * h + (1.0 - math.sin(math.pi * s)) * 0.06 * h * a
        y = yard.y - 0.2 - depth * (math.sin(math.pi * s) ** 0.8) * math.sin(math.pi * a * 0.82)
        return (yard.x + x, y, z)
    g.grid(c.P, f, ns, nt, band, smooth=True, two_sided=two, thick=0.03, keep=keep,
           jitter=0.38 if keep is not None else 0.0, seed=int(abs(yard.y) * 10 + yard.z * 3))


def fore_aft_sail(c, luff0, luff1, leech1, leech0, depth, band, ns, nt, keep=None):
    """Sail in the Y-Z plane: luff0 (tack) -> luff1 (head) along the mast/stay, leech side aft. Bellies toward +X."""
    luff0, luff1, leech1, leech0 = (Vector(p) for p in (luff0, luff1, leech1, leech0))

    def f(s, t):
        a = luff0.lerp(leech0, s)
        b = luff1.lerp(leech1, s)
        p = a.lerp(b, t)
        p.x += depth * math.sin(math.pi * s) * math.sin(math.pi * min(t * 1.1, 1.0))
        return p
    g.grid(c.P, f, ns, nt, band, smooth=True, two_sided=True, thick=0.03, keep=keep)


def flag(c, attach, length, height, pennant=False, yaw=0.0, droop=0.0):
    """Culture flag streaming toward -Y from attach (top of the hoist)."""
    if c.ruin and c.hashn(attach.x, attach.y, attach.z) > 0.25:
        return
    ns = c.n(6, 2)
    attach = Vector(attach)
    R = Matrix.Rotation(yaw, 3, 'Z')
    ph = c.hashn(attach.y, 3.3) * 6.28

    def f(s, t):
        hh = height * (1.0 - 0.85 * s) if pennant else height
        z = -0.5 * height + (t - 0.5) * hh - droop * s * s * length
        x = 0.10 * height * math.sin(ph + s * 7.0) * s + (0.25 * height * s if c.dmg else 0.0)
        return attach + R @ Vector((x, -s * length, z))
    keep = None
    if c.dmg:
        # torn fly: the flag ends at the first missing cell, so no piece floats free of the hoist (verifier fix)
        first = next((i for i in range(1, ns) if c.hashn(i, 0, attach.y) <= 0.3), ns)
        keep = lambda i, j: i < first
    g.grid(c.P, f, ns, 1, 'flag', smooth=True, two_sided=True, thick=0.01, keep=keep)


def mast(c, base, h, r0, r1, sides, band='spar', cut=None):
    """Returns the effective top z (cut masts stop early and leave a splintered stub)."""
    top = base + Vector((0, 0, h))
    if cut is not None and cut < h:
        top = base + Vector((0, 0, cut))
        g.cyl(c.P, base, top, r0, r0 + (r1 - r0) * cut / h, sides, band, cap0=False, cap1=True)
        # splinter
        s = Part()
        rr = r0 + (r1 - r0) * cut / h
        g.cyl(s, (0, 0, 0), (0.05, 0.03, rr * 3.0), rr * 0.8, 0.02, max(3, sides // 2), band, cap0=False, cap1=False)
        c.P.extend(s, Matrix.Translation(top))
        return top.z
    g.cyl(c.P, base, top, r0, r1, sides, band, cap0=False, cap1=True)
    return top.z


def fallen_spar(c, a, b, r, band='spar'):
    g.cyl(c.P, a, b, r, r * 0.7, max(4, c.n(6, 4)), band, cap0=True, cap1=True)


def house(c, base, w, l, storeys, sh, band='super_win', roof='paint_white'):
    """Deckhouse of `storeys` floors, each mapped to one storey of the window band; flat roof."""
    base = Vector(base)
    for k in range(storeys):
        skip = ['-z']
        if k < storeys - 1:
            skip.append('+z')
        g.box(c.P, base + Vector((0, 0, (k + 0.5) * sh)), (w, l, sh), band, skip=tuple(skip), top_band=roof,
              u0=0.13 * k)


def yard(c, centre, w, r, band='spar'):
    centre = Vector(centre)
    g.cyl(c.P, centre - Vector((w * 0.5, 0, 0)), centre + Vector((w * 0.5, 0, 0)), r * 0.6, r * 0.6, max(4, c.n(6, 4)), band,
          cap0=False, cap1=False)


def sail_keep(c, ns, nt, salt):
    """Torn sails: the foot and the leeches go ragged, the head stays bent to the yard."""
    if not c.dmg:
        return None
    k = 1.0 if not c.ruin else 1.8

    def raw(i, j):
        a = 1.0 - j / max(nt, 1)                     # 1 at the foot
        v = c.hashn(i, j, salt) * 0.6 + c.hashn(i // 2, j // 2, salt + 1) * 0.4
        p = (0.6 * a * a + (0.22 if i in (0, ns - 1) else 0.0)) * k
        return v > p or j == nt - 1
    # keep only cloth still hanging from the yard: flood-fill from the head row through edge neighbours
    # (verifier fix 2026-11-19: isolated cells used to float in mid-air as loose squares of sail)
    mask = {(i, j) for i in range(ns) for j in range(nt) if raw(i, j)}
    hung = {(i, nt - 1) for i in range(ns) if (i, nt - 1) in mask}
    todo = list(hung)
    while todo:
        i, j = todo.pop()
        for q in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
            if q in mask and q not in hung:
                hung.add(q)
                todo.append(q)

    def keep(i, j):
        return (i, j) in hung
    return keep


def oar(c, pivot, side, length, angle_down, sweep, blade=True, r=0.06):
    """Oar from inboard handle through the pivot down into the water. side = +1 starboard (+X), -1 port."""
    d = Vector((side * math.cos(angle_down), sweep, -math.sin(angle_down))).normalized()
    a = pivot - d * (length * 0.28)
    b = pivot + d * (length * 0.72)
    g.cyl(c.P, a, b, r, r * 0.8, 4 if c.lod == 0 else 3, 'oar', cap0=False, cap1=False, along='u')
    if blade:
        bl = Part()
        g.box(bl, (0, 0, 0), (0.05, 0.22, 0.9), 'oar')
        R = Matrix.Rotation(-side * (math.pi * 0.5 - angle_down), 4, 'Y')
        c.P.extend(bl, Matrix.Translation(b - d * 0.45) @ R)


def lifeboat(c, centre, L, B, band='paint_white'):
    hs = HullSpec(L=L, B=B, sheer=0.45, keel=-0.35, n=2.2, bow=0.4, stern=0.4, rocker=0.1)
    p = Part()
    g.hull(p, hs, c.n(6, 3), 2, 1, lambda z: band, deck_z=0.3, deck_band='plank_oiled', thick=0.05,
           rail_band=band)
    c.P.extend(p, Matrix.Translation(Vector(centre)))


def scale_rings(c, hs, ts, band='rope', w=0.07, grow=1.04, y_rake=None):
    """Lashing rings around a reed hull at stations ts. y_rake: the hull's own end curl, so the rings near the
    upswept ends stay on the bundle (verifier fix 2026-11-19: they used to float beside the curled ends)."""
    for t in ts:
        sec = g.section(hs, t, 6, 2)
        y = hs.y(t)
        dy = (lambda z: y_rake(t, z)) if y_rake else (lambda z: 0.0)
        ring = [Vector((-x * grow, y + dy(z), z)) for x, z in reversed(sec)] + [Vector((x * grow, y + dy(z), z)) for x, z in sec[1:]]
        r0 = [p + Vector((0, -w * 0.5, 0)) for p in ring]
        r1 = [p + Vector((0, w * 0.5, 0)) for p in ring]
        g.loft(c.P, [r0, r1], lambda j: band, smooth=True)
        c.P.orient(len(c.P.F) - (len(ring) - 1), lambda q, yy=y: Vector((0, q.y, max(q.z, hs.keel_z(t) + 0.15))))


def stripe_strip(c, hs, t0, t1, z0, z1, band, n_st, grow=1.012):
    """A band painted along both hull sides between heights z0..z1 (relative to sheer when negative)."""
    rings = []
    for i in range(n_st + 1):
        t = t0 + (t1 - t0) * i / n_st
        sec = g.section(hs, t, 4, 6)
        zs = hs.sheer_z(t)
        a, b = (zs + z0, zs + z1) if z0 < 0 else (z0, z1)

        def x_at(z):
            for k in range(len(sec) - 1):
                (x0, za), (x1, zb) = sec[k], sec[k + 1]
                if (za - z) * (zb - z) <= 0 and zb != za:
                    return x0 + (x1 - x0) * (z - za) / (zb - za)
            return sec[-1][0]
        rings.append((t, x_at(a) * grow, a, x_at(b) * grow, b))
    for side in (-1, 1):
        rr = [[Vector((side * xa, hs.y(t), za)), Vector((side * xb, hs.y(t), zb))] for (t, xa, za, xb, zb) in rings]
        s0 = len(c.P.F)
        g.loft(c.P, rr, lambda j: band, smooth=True)
        c.P.orient(s0, lambda q: Vector((0, q.y, q.z)))


# --------------------------------------------------------------------------- 1. reed boat (Stone age)


def reed_boat(c):
    L, B = 8.0, 1.5
    hs = HullSpec(L=L, B=B, sheer=0.42, keel=-0.22, n=2.0, bow=0.46, stern=0.46, sheer_bow=2.3, sheer_stern=1.8,
                  rocker=2.5, rocker_stern=1.95, flare=0.0, bow_pow=0.7, stern_pow=0.8, transom=0.0)
    P = c.P

    def curl(t, z):
        # the bundle ends curl back over the boat
        e = hs.end_factor(t)
        sgn = 1.0 if t < 0.5 else -1.0
        return sgn * 0.45 * e ** 3 * max(z - 0.3, 0.0)
    g.hull(P, hs, c.n(30, 6), c.n(7, 2), c.n(3, 1), lambda z: 'reed', deck_z=lambda t: hs.sheer_z(t) - 0.16,
           deck_band='reed', thick=0.16, rail_band='reed', y_rake=curl)
    if c.lod <= 1:
        scale_rings(c, hs, [0.08, 0.16, 0.26, 0.38, 0.5, 0.62, 0.74, 0.84, 0.92][::(1 if c.lod == 0 else 2)], y_rake=curl)
    # culture-coloured woven binding along the gunwale
    stripe_strip(c, hs, 0.18, 0.82, -0.13, -0.02, 'flag', c.n(10, 2), grow=1.03)
    deck = hs.sheer - 0.16
    if c.lod <= 2 and not c.ruin:
        # cargo bundles amidships
        for k, (y, s) in enumerate(((-0.3, 0.5), (0.35, 0.45), (0.0, 0.42))):
            if c.lod <= 1 or k < 2:
                g.cyl(P, (-0.32, y, deck + s * 0.5), (0.32, y, deck + s * 0.5), s * 0.5, s * 0.5, c.n(7, 4), 'cargo', along='v')
        if c.lod <= 1:
            # paddlers and the poleman
            for k, y in enumerate((-2.0, -1.2, 1.2)):
                side = 1 if k % 2 == 0 else -1
                if c.dmg and k == 1:
                    continue
                g.person(P, (side * 0.15, y, deck - 0.05), yaw=math.pi, h=1.7, pose='sit')
                oar(c, Vector((side * 0.55, y + 0.1, deck + 0.55)), side, 2.2, math.radians(58), 0.25, r=0.03)
            g.person(P, (0.0, 2.5, deck + 0.1 + 0.35), yaw=math.pi, h=1.75, pose='stand')
            g.cyl(P, (0.25, 2.3, deck + 2.6), (0.6, 3.6, -0.6), 0.03, 0.03, 3, 'oar', cap0=False, cap1=False, along='u')
    # stern pennant pole
    t_s = 0.86
    pole_base = Vector((0, hs.y(t_s), hs.sheer_z(t_s) - 0.1))
    top = pole_base + Vector((0, 0.15, 2.3))
    if not c.ruin:
        g.cyl(P, pole_base, top, 0.035, 0.025, 4 if c.lod < 2 else 3, 'spar', cap0=False)
        flag(c, top, 1.1, 0.55, pennant=True)
    return dict(L=L, B=B, deck=deck, scale_pos=(0.0, -0.6, deck), hs=hs,
                damage=dict(Damaged=(4.0, 1.5, 0.12), Ruin=(14.0, 4.0, 0.42)))


# --------------------------------------------------------------------------- 2. canoe (Bronze age)


def canoe(c):
    L, B = 12.0, 1.35
    hs = HullSpec(L=L, B=B, sheer=0.52, keel=-0.32, n=2.6, bow=0.30, stern=0.26, sheer_bow=0.95, sheer_stern=0.55,
                  rocker=0.35, rocker_stern=0.25, flare=0.12, bow_pow=0.9, stern_pow=1.2)
    P = c.P

    def bands(z):
        if z < 0.02:
            return 'plank_tar'
        if z > 0.38:
            return 'stripe'
        return 'plank_oiled'
    g.hull(P, hs, c.n(26, 6), c.n(5, 2), c.n(4, 1), bands, deck_z=lambda t: hs.sheer_z(t) - 0.42, deck_band='deck',
           thick=0.07, marks=(0.02, 0.38) if c.lod < 3 else (), rail_band='oar', inner_band='plank_oiled')
    deck = hs.sheer - 0.42
    # prow post with a carved head, sternpost
    bow_top = Vector((0, hs.y(0.0) + 0.15, hs.sheer_z(0.0)))
    g.cyl(P, bow_top - Vector((0, -0.05, 0.6)), bow_top + Vector((0, -0.25, 0.9)), 0.08, 0.05, 4, 'oar', cap0=False)
    head = bow_top + Vector((0, -0.3, 0.95))
    if c.lod <= 2:
        g.box(P, head, (0.12, 0.35, 0.18), 'gold', R=Matrix.Rotation(0.4, 3, 'X'))
    if c.lod <= 1 and not c.ruin:
        n_rowers = 6 if c.lod == 0 else 4
        for k in range(n_rowers):
            y = -2.6 + k * (5.4 / (n_rowers - 1))
            side = 1 if k % 2 == 0 else -1
            if c.dmg and k in (2, 5):
                continue
            g.person(P, (side * 0.12, y, deck), yaw=math.pi, h=1.7, pose='sit')
            oar(c, Vector((side * 0.62, y + 0.15, deck + 0.55)), side, 2.4, math.radians(55), 0.3, r=0.03)
        # steersman
        g.person(P, (0, 4.4, deck), yaw=math.pi, h=1.75, pose='stand')
    if c.lod <= 1:
        g.cyl(P, (0.45, 4.6, deck + 1.2), (0.85, 6.8, -0.4), 0.04, 0.04, 3, 'oar', cap0=False, cap1=False, along='u')
    # mast and a small culture-striped square sail
    mbase = Vector((0, -1.0, deck - 0.1))
    mh = 5.6
    cut = None if not c.dmg else (mh * 0.55 if c.ruin else None)
    topz = mast(c, mbase, mh, 0.08, 0.05, c.n(6, 3), cut=cut)
    if not c.ruin:
        yz = mbase.z + mh - 0.3
        yard(c, (0, -1.05, yz), 3.4, 0.06)
        ns, nt = c.n(6, 1), c.n(4, 1)
        billow_square(c, Vector((0, -1.05, yz)), 3.3, 3.0, 2.6, 0.35, 'sail_stripe', ns, nt, keep=sail_keep(c, ns, nt, 11))
        if c.lod <= 1:
            for side in (-1, 1):
                g.line(P, (0, -1.0, yz + 0.2), (side * 0.6, -1.0, hs.sheer), 0.012)
            g.line(P, (0, -1.0, yz + 0.2), (0, hs.y(0.02), hs.sheer_z(0.02)), 0.012)
        flag(c, Vector((0, -1.0, mbase.z + mh + 0.05)), 1.4, 0.5, pennant=True)
    return dict(L=L, B=B, deck=deck, scale_pos=(0.0, 1.6, deck), hs=hs,
                damage=dict(Damaged=(5.0, 1.0, 0.12), Ruin=(16.0, 3.0, 0.4)))


# --------------------------------------------------------------------------- 3. war galley (Iron age)


def galley(c):
    L, B = 32.0, 4.6
    hs = HullSpec(L=L, B=B, sheer=1.55, keel=-1.0, n=3.0, bow=0.22, stern=0.25, sheer_bow=0.7, sheer_stern=2.4,
                  rocker=0.35, rocker_stern=1.1, flare=0.14, stern_pow=1.2, bow_pow=0.85)
    P = c.P

    def bands(z):
        if z < 0.0:
            return 'plank_tar'
        if 0.75 < z < 1.05:
            return 'stripe'
        return 'plank_oiled'
    g.hull(P, hs, c.n(30, 6), c.n(5, 2), c.n(5, 1), bands, deck_z=lambda t: hs.sheer_z(t) - 0.6, deck_band='deck',
           thick=0.12, marks=(0.0, 0.75, 1.05) if c.lod < 3 else (), rail_band='oar', inner_band='plank_oiled')
    deck = hs.sheer - 0.6
    # bronze ram at the waterline
    yb = hs.y(0.0)
    g.cyl(P, (0, yb + 0.6, 0.15), (0, yb - 2.2, 0.0), 0.5, 0.06, 4, 'gold', cap0=True, cap1=False, phase=math.pi / 4)
    # stem post curling up at the bow
    if c.lod <= 2:
        pts = [Vector((0, yb + 0.2, hs.sheer_z(0.0) - 0.3))]
        for k in range(1, c.n(4, 2) + 1):
            a = k / c.n(4, 2)
            pts.append(Vector((0, yb - 0.4 * a + 0.5 * a * a, hs.sheer_z(0.0) + 1.3 * a)))
        for a, b in zip(pts, pts[1:]):
            g.cyl(P, a, b, 0.14, 0.11, 4, 'plank_oiled', cap0=False)
    # stern ornament (aphlaston): a fan of curved timbers rising and curling forward
    ys = hs.y(1.0)
    zs = hs.sheer_z(1.0)
    n_seg = c.n(6, 2)
    for f_i, xoff in enumerate((-0.25, 0.0, 0.25) if c.lod == 0 else (0.0,)):
        prev = Vector((xoff, ys - 0.4, zs - 0.4))
        for k in range(1, n_seg + 1):
            a = k / n_seg
            ang = a * math.pi * 0.9
            p = Vector((xoff * (1 + a), ys - 0.4 + 1.6 * math.sin(ang * 0.6) - 1.4 * a * a, zs - 0.4 + 2.6 * math.sin(ang * 0.55)))
            if c.ruin and a > 0.5:
                break
            g.cyl(P, prev, p, 0.16 * (1 - 0.5 * a) + 0.03, 0.16 * (1 - 0.5 * (a + 1 / n_seg)) + 0.03, 4, 'plank_oiled',
                  cap0=False)
            prev = p
    # outrigger (oar box) along both sides
    y0, y1 = -11.0, 10.5
    for side in (-1, 1):
        x = side * (B * 0.5 + 0.30)
        g.box(P, (x, (y0 + y1) * 0.5, 1.45), (0.75, y1 - y0, 0.22), 'plank_oiled', top_band='deck')
        if c.lod <= 1:
            # rail posts
            for k in range(c.n(8, 4) + 1):
                y = y0 + (y1 - y0) * k / c.n(8, 4)
                g.box(P, (x + side * 0.32, y, 1.85), (0.1, 0.1, 0.6), 'oar')
            g.box(P, (x + side * 0.32, (y0 + y1) * 0.5, 2.17), (0.12, y1 - y0, 0.08), 'oar')
    # oars
    n_oar = (18, 9, 0, 0)[c.lod]
    lost = set()
    if c.dmg:
        lost = {k for k in range(n_oar) if c.hashn(k, 5) < (0.35 if not c.ruin else 0.9)}
    for side in (-1, 1):
        for k in range(n_oar):
            if (k + (side > 0) * 3) in lost:
                continue
            y = y0 + 0.8 + (y1 - y0 - 1.6) * k / max(n_oar - 1, 1)
            piv = Vector((side * (B * 0.5 + 0.62), y, 1.6))
            oar(c, piv, side, 7.0, math.radians(24), 0.12, r=0.055)
    if c.lod == 2 and not c.ruin:
        # oar banks as two slanted cards
        for side in (-1, 1):
            x0 = side * (B * 0.5 + 0.6)
            x1 = side * (B * 0.5 + 5.2)
            a, b = Vector((x0, y0 + 0.5, 1.55)), Vector((x0, y1 - 0.5, 1.55))
            cc, d = Vector((x1, y1 - 0.2, -0.3)), Vector((x1, y0 + 0.8, -0.3))
            g.grid(P, lambda s, t: tuple(a.lerp(b, s).lerp(d.lerp(cc, s), t)), 1, 1, 'oar', two_sided=True, thick=0.05)
    # shields along the rail (culture painted)
    if c.lod <= 1 and not c.ruin:
        ns = 10 if c.lod == 0 else 6
        for side in (-1, 1):
            for k in range(ns):
                y = -8.5 + 16.5 * k / (ns - 1)
                if c.dmg and c.hashn(k, side) < 0.3:
                    continue
                ctr = Vector((side * (B * 0.5 + 0.72), y, 1.95))
                pts = []
                nsd = 8 if c.lod == 0 else 6
                for j in range(nsd):
                    a = 2 * math.pi * j / nsd
                    pts.append(ctr + Vector((side * 0.02, math.cos(a) * 0.42, math.sin(a) * 0.42)))
                g.cap(P, pts, 'flag', ref=lambda q, cc=ctr: cc - Vector((side * 0.5, 0, 0)))
                g.cyl(P, ctr + Vector((side * 0.02, 0, 0)), ctr + Vector((side * 0.09, 0, 0)), 0.1, 0.04, 4, 'gold', cap0=False)
    # stern canopy
    if c.lod <= 2:
        # stern awning: four posts and a striped gable cloth
        cz = hs.sheer_z(0.84) - 0.6
        y0_, y1_, hw_, hp, rp = 8.4, 11.2, 1.3, 1.7, 2.3
        if c.lod <= 1:
            for (x, y) in ((-hw_, y0_), (hw_, y0_), (-hw_, y1_), (hw_, y1_)):
                g.cyl(P, (x, y, cz), (x, y, cz + hp), 0.05, 0.05, 4, 'oar', cap0=False, cap1=False)
        for side in (-1, 1):
            a, b = Vector((side * (hw_ + 0.15), y1_ + 0.2, cz + hp - 0.15)), Vector((side * (hw_ + 0.15), y0_ - 0.2, cz + hp - 0.15))
            cc, d = Vector((0, y0_ - 0.2, cz + rp)), Vector((0, y1_ + 0.2, cz + rp))
            g.grid(P, lambda s_, t_, a=a, b=b, cc=cc, d=d: tuple(a.lerp(b, s_).lerp(d.lerp(cc, s_), t_)), 1, 1, 'sail_stripe',
                   two_sided=True, thick=0.03)
    # helmsmen and marines
    if c.lod <= 1 and not c.ruin:
        for (x, y) in ((0.6, 11.2), (-0.6, 11.2), (0.4, -10.5), (-0.5, -11.2), (0.0, -9.8), (0.9, -2.0), (-0.8, 3.0)):
            g.person(P, (x, y, hs.sheer_z(g.t_of_y(hs, y)) - 0.6), yaw=math.pi, h=1.75)
        # steering oars
        for side in (-1, 1):
            g.cyl(P, (side * 1.5, 11.4, hs.sheer_z(0.86) + 0.3), (side * 2.3, 14.5, -0.9), 0.08, 0.08, 4, 'oar', cap0=False, cap1=False, along='u')
            g.box(P, (side * 2.32, 14.55, -0.95), (0.08, 0.7, 1.4), 'oar', R=Matrix.Rotation(-0.3, 3, 'X'))
    # mast, yard, striped square sail
    mbase = Vector((0, -1.0, deck))
    mh = 12.5
    cut = None
    if c.dmg:
        cut = mh * (0.62 if not c.ruin else 0.35)
    topz = mast(c, mbase, mh, 0.22, 0.13, c.n(8, 4), cut=cut)
    yz = mbase.z + mh - 0.8
    if c.state == 'Damaged':
        # yard and sail came down: yard hangs diagonally, torn sail draped
        fallen_spar(c, (-6.0, -0.4, deck + 1.0), (5.5, -1.6, deck + 6.5), 0.13)
        ns, nt = c.n(8, 1), c.n(6, 1)
        billow_square(c, Vector((0, -1.6, deck + 6.8)), 9.5, 8.0, 5.0, 0.8, 'sail_torn', ns, nt, keep=sail_keep(c, ns, nt, 21))
    elif not c.ruin:
        yard(c, (0, -1.2, yz), 14.0, 0.18)
        ns, nt = c.n(10, 1), c.n(7, 1)
        billow_square(c, Vector((0, -1.2, yz)), 13.5, 12.5, 7.2, 1.5, 'sail_stripe', ns, nt)
        if c.lod <= 1:
            # stays and halyards
            g.line(P, (0, -1.0, yz + 0.4), (0, yb + 1.0, hs.sheer_z(0.02)), 0.03)
            g.line(P, (0, -1.0, yz + 0.4), (0, 9.0, hs.sheer_z(0.78)), 0.03)
            for side in (-1, 1):
                g.line(P, (0, -1.0, yz), (side * (B * 0.5 - 0.1), 0.5, hs.sheer), 0.025)
                g.line(P, (side * 6.9, -1.2, yz), (side * 2.0, 2.5, hs.sheer), 0.02)
        flag(c, Vector((0, -1.0, mbase.z + mh + 0.05)), 3.5, 0.9, pennant=True)
    if not c.ruin and c.lod <= 2:
        flag(c, Vector((0, ys - 0.2, zs + 2.0)), 1.8, 1.1)
    return dict(L=L + 2.2, B=B + 10.4, deck=deck, scale_pos=(0.0, -6.0, deck), hs=hs,
                damage=dict(Damaged=(5.0, 1.2, 0.25), Ruin=(16.0, 3.0, 1.05)))


# --------------------------------------------------------------------------- 4. cog (late Iron / early Sail)


def cog(c):
    L, B = 24.0, 7.6
    hs = HullSpec(L=L, B=B, sheer=3.4, keel=-2.2, n=2.3, bow=0.30, stern=0.20, transom=0.6, sheer_bow=1.0, sheer_stern=0.8,
                  rocker=0.3, rocker_stern=0.1, flare=0.06, stern_pow=1.7, bow_pow=0.8)
    P = c.P

    def bands(z):
        if z < 0.0:
            return 'plank_tar'
        if 2.35 < z < 2.85:
            return 'stripe'
        return 'plank_tar' if z < 2.35 else 'plank_oiled'
    g.hull(P, hs, c.n(26, 6), c.n(6, 2), c.n(6, 1), bands, deck_z=lambda t: hs.sheer_z(t) - 0.9, deck_band='deck',
           thick=0.14, marks=(0.0, 2.35, 2.85) if c.lod < 3 else (), rail_band='oar', inner_band='plank_oiled',
           transom_band='plank_oiled')
    deck = hs.sheer - 0.9
    # castles
    for (ya, yb_, zf, w) in ((-11.3, -7.4, hs.sheer_z(0.04) - 0.2, 5.0), (6.6, 11.6, hs.sheer_z(0.9) + 1.9, 6.4)):
        yc = (ya + yb_) * 0.5
        hgt = zf - deck
        g.box(P, (0, yc, deck + hgt * 0.5), (w, yb_ - ya, hgt), 'plank_oiled', skip=('-z',), top_band='deck')
        # crenellated, culture-painted parapet
        nm = c.n(6, 2)
        for side in (-1, 1):
            g.box(P, (side * (w * 0.5 - 0.06), yc, zf + 0.45), (0.12, yb_ - ya, 0.9), 'stripe')
        for end in (ya, yb_):
            g.box(P, (0, end, zf + 0.45), (w, 0.12, 0.9), 'stripe')
        if c.lod <= 1:
            for side in (-1, 1):
                for k in range(nm):
                    y = ya + (yb_ - ya) * (k + 0.5) / nm
                    g.box(P, (side * (w * 0.5 - 0.06), y, zf + 1.05), (0.16, (yb_ - ya) / nm * 0.5, 0.3), 'plank_oiled')
        if c.lod <= 1 and not c.ruin:
            g.person(P, (0.6, yc, zf), yaw=math.pi, h=1.75)
        if not c.ruin and c.lod <= 2:
            g.cyl(P, (0, yc + (1.8 if ya > 0 else 0), zf), (0, yc + (1.8 if ya > 0 else 0), zf + 4.0), 0.06, 0.05, 4, 'spar', cap0=False)
            flag(c, Vector((0, yc + (1.8 if ya > 0 else 0), zf + 4.0)), 2.2, 1.3)
    # stern rudder and bowsprit
    g.box(P, (0, 12.25, 0.5), (0.25, 1.1, 5.2), 'plank_oiled')
    g.cyl(P, (0, -10.5, hs.sheer_z(0.05) + 0.4), (0, -16.5, hs.sheer_z(0.0) + 2.4), 0.2, 0.12, c.n(6, 4), 'spar', cap0=False)
    # mast with a top (crow's nest), yard and big square sail
    mbase = Vector((0, -0.6, deck))
    mh = 21.0
    cut = None if not c.dmg else mh * (0.66 if not c.ruin else 0.38)
    mast(c, mbase, mh, 0.38, 0.2, c.n(8, 4), cut=cut)
    top_z = mbase.z + mh * 0.82
    if cut is None or cut > mh * 0.85:
        g.cyl(P, (0, -0.6, top_z), (0, -0.6, top_z + 1.2), 0.85, 0.95, c.n(8, 5), 'plank_oiled', cap0=True)
    yz = mbase.z + mh * 0.74
    if c.state == 'Intact':
        yard(c, (0, -0.95, yz), 13.0, 0.25)
        ns, nt = c.n(10, 1), c.n(8, 1)
        billow_square(c, Vector((0, -0.95, yz)), 12.6, 11.6, 11.0, 2.0, 'sail_stripe', ns, nt)
    elif c.state == 'Damaged':
        fallen_spar(c, (-6.3, 0.5, deck + 1.0), (6.0, -2.2, deck + 9.5), 0.2)
        ns, nt = c.n(10, 1), c.n(8, 1)
        billow_square(c, Vector((0, -2.0, deck + 9.8)), 11.0, 9.0, 8.0, 1.2, 'sail_torn', ns, nt, keep=sail_keep(c, ns, nt, 31))
    if c.lod <= 1 and not c.ruin:
        mtop = mbase.z + (cut if cut else mh)
        for side in (-1, 1):
            for k in range(3 if c.lod == 0 else 1):
                g.line(P, (0, -0.6, min(top_z, mtop)), (side * (B * 0.5 - 0.1), 0.5 + k * 1.1, hs.sheer), 0.035)
        g.line(P, (0, -0.6, min(top_z, mtop)), (0, -16.3, hs.sheer_z(0.0) + 2.3), 0.04)
        g.line(P, (0, -0.6, min(top_z, mtop)), (0, 11.0, hs.sheer_z(0.9) + 1.9), 0.04)
        for k in range(2 if c.lod == 0 else 0):
            g.person(P, (1.2 - 2.4 * k, 3.0 + k, deck), yaw=math.pi * 0.7, h=1.75)
    if c.state != 'Ruin':
        mt = mbase.z + (cut if cut else mh)
        flag(c, Vector((0, -0.6, mt + 0.05)), 6.0, 1.0, pennant=True)
    return dict(L=L + 4.5, B=B, deck=deck, scale_pos=(1.0, 2.0, deck), hs=hs,
                damage=dict(Damaged=(6.0, 1.5, 0.45), Ruin=(18.0, 4.0, 3.0)))


# --------------------------------------------------------------------------- 5. full-rigged sailing ship (Sail age)


def sail_ship(c):
    L, B = 44.0, 11.0
    hs = HullSpec(L=L, B=B, sheer=4.6, keel=-4.6, n=2.6, bow=0.27, stern=0.17, transom=0.66, sheer_bow=1.5, sheer_stern=3.0,
                  rocker=0.5, rocker_stern=0.2, flare=0.0, tumble=0.10, stern_pow=1.8, bow_pow=0.75)
    P = c.P

    def bands(z):
        if 1.6 < z < 2.6:
            return 'stripe'
        return 'plank_tar'
    g.hull(P, hs, c.n(32, 7), c.n(6, 2), c.n(7, 1), bands, deck_z=lambda t: hs.sheer_z(t) - 1.1, deck_band='deck',
           thick=0.2, marks=(1.6, 2.6) if c.lod < 3 else (), rail_band='oar', inner_band='plank_oiled',
           transom_band='plank_tar')
    deck = hs.sheer - 1.1
    ys, zs = hs.y(1.0), hs.sheer_z(1.0)
    # stern gallery: white-framed windows on the transom (night-lit)
    hb = hs.half_beam(1.0) * 0.86
    if c.lod <= 2:
        g.quad(P, (hb, ys + 0.06, zs - 3.0), (-hb, ys + 0.06, zs - 3.0), (-hb * 0.95, ys + 0.06, zs - 0.4),
               (hb * 0.95, ys + 0.06, zs - 0.4), 'super_win', u_range=(0.0, 0.42))
    # gun ports along the culture stripe
    if c.lod <= 1:
        np_ = 11 if c.lod == 0 else 6
        for side in (-1, 1):
            for k in range(np_):
                y = -13.0 + 26.0 * k / (np_ - 1)
                t = g.t_of_y(hs, y)
                sec = g.section(hs, t, 4, 6)
                x = [xx for xx, zz in sec if zz >= 2.1][0] if any(zz >= 2.1 for _, zz in sec) else sec[-1][0]
                g.box(P, (side * (x + 0.02), y, 2.1), (0.12, 0.75, 0.62), 'metal', skip=(('-x',) if side > 0 else ('+x',)))
                if c.lod == 0 and not c.ruin:
                    # run-out gun barrel
                    g.cyl(P, (side * (x + 0.05), y, 2.08), (side * (x + 1.1), y, 2.12), 0.16, 0.13, 6, 'metal', cap0=False, cap1=True)
    # bowsprit and jib boom
    yb = hs.y(0.0)
    bs0 = Vector((0, yb + 3.0, hs.sheer_z(0.05)))
    bs1 = Vector((0, yb - 11.0, hs.sheer_z(0.0) + 4.5))
    g.cyl(P, bs0, bs1, 0.35, 0.12, c.n(8, 4), 'spar', cap0=False)
    # figurehead
    if c.lod <= 2:
        # gilded beakhead scroll under the bowsprit
        sz = hs.sheer_z(0.0)
        pts = [Vector((0, yb + 0.6, sz - 2.4)), Vector((0, yb - 0.9, sz - 1.4)), Vector((0, yb - 1.5, sz - 0.4)),
               Vector((0, yb - 1.2, sz + 0.2))]
        for k, (a, b) in enumerate(zip(pts, pts[1:])):
            g.cyl(P, a, b, 0.32 - 0.07 * k, 0.25 - 0.07 * k, c.n(6, 4), 'gold', cap0=k == 0, cap1=k == 2)
    # masts: (y, height above deck, yards [(frac height, width, sail h)])
    masts = [(-12.5, 31.0, [(0.33, 19.0, 8.0), (0.62, 15.0, 7.5), (0.86, 10.5, 5.0)]),
             (0.0, 35.0, [(0.33, 21.0, 9.0), (0.62, 16.5, 8.0), (0.86, 11.5, 5.5)]),
             (12.0, 25.0, [(0.55, 12.0, 6.0), (0.84, 8.5, 4.2)])]
    for mi, (my, mh, yards) in enumerate(masts):
        mb = Vector((0, my, deck))
        cut = None
        if c.state == 'Damaged' and mi == 0:
            cut = mh * 0.48
        if c.ruin:
            cut = mh * (0.30, 0.42, 0.0)[mi]
            if cut <= 0.1:
                continue
        mast(c, mb, mh, 0.48 if mi < 2 else 0.36, 0.14, c.n(8, 4), cut=cut)
        top = mb.z + (cut if cut else mh)
        # fighting tops
        for fh in (0.30, 0.60):
            if mb.z + mh * fh < top:
                g.box(P, (0, my, mb.z + mh * fh), (3.6 if fh < 0.5 else 2.2, 2.6 if fh < 0.5 else 1.6, 0.3), 'plank_oiled')
        for yi, (fh, w, sh) in enumerate(yards):
            yz = mb.z + mh * fh + sh * 0.95
            if yz > top - 0.2:
                continue
            if mi == 2 and yi == 0:
                continue     # mizzen course replaced by the spanker
            yard(c, (0, my - 0.4, yz), w, 0.3)
            ns, nt = c.n(8, 1), c.n(6, 1)
            billow_square(c, Vector((0, my - 0.5, yz)), w * 0.96, w * (1.04 if yi == 0 else 1.0), sh, sh * 0.22, 'sail', ns, nt,
                          keep=sail_keep(c, ns, nt, mi * 10 + yi))
        if mi == 0 and c.state == 'Damaged':
            # fallen fore-topmast hanging over the bow
            fallen_spar(c, (0.4, my - 0.2, top), (3.5, my - 9.0, deck + 4.0), 0.22)
            ns, nt = c.n(6, 1), c.n(4, 1)
            billow_square(c, Vector((1.6, my - 4.0, top - 0.5)), 10.0, 9.0, 6.0, 0.6, 'sail_torn', ns, nt,
                          keep=sail_keep(c, ns, nt, 77))
        # rigging
        if c.lod <= 1 and not c.ruin:
            for side in (-1, 1):
                nsh = 4 if c.lod == 0 else 2
                for k in range(nsh):
                    yy = my - 1.5 + 3.0 * k / max(nsh - 1, 1)
                    t = g.t_of_y(hs, yy)
                    xr = hs.half_beam(t) * (1 - hs.tumble) + 0.4
                    g.line(P, (side * 0.4, my, min(top, mb.z + mh * 0.6)), (side * xr, yy + 1.0, hs.sheer_z(t)), 0.04)
                # topmast shrouds to the fighting top
                if top > mb.z + mh * 0.62:
                    g.line(P, (0, my, min(top, mb.z + mh * 0.9)), (side * 1.6, my, mb.z + mh * 0.6), 0.03)
            # backstay
            g.line(P, (0, my, top - 0.3), (0, my + 9.0, hs.sheer_z(g.t_of_y(hs, my + 9.0))), 0.04)
        if not c.ruin:
            flag(c, Vector((0, my, top + 0.05)), 7.5 if mi == 1 else 5.0, 0.9, pennant=True)
    # stays and jibs from the foremast to the bowsprit
    if not c.ruin:
        fm = masts[0]
        fm_top = deck + fm[1] * (0.48 if c.state == 'Damaged' else 0.82)
        if c.lod <= 1:
            g.line(P, (0, fm[0], fm_top), tuple(bs1), 0.05)
            g.line(P, (0, fm[0], deck + fm[1] * 0.45), tuple(bs0.lerp(bs1, 0.55)), 0.05)
        if c.state == 'Intact':
            ns, nt = c.n(5, 1), c.n(4, 1)
            for j, (a, h_) in enumerate(((0.9, 0.78), (0.55, 0.55))):
                tack = bs0.lerp(bs1, a)
                head = Vector((0, fm[0] - 0.5, deck + fm[1] * h_))
                clew = Vector((0, tack.y + 6.0 + 2 * j, tack.z + 1.2))
                fore_aft_sail(c, tack, head, head.lerp(clew, 0.97), clew, 0.6, 'sail', ns, nt)
        # spanker on the mizzen
        mz = masts[2]
        if c.state == 'Intact':
            ns, nt = c.n(5, 1), c.n(4, 1)
            fore_aft_sail(c, (0, mz[0] + 0.4, deck + 2.5), (0, mz[0] + 0.4, deck + mz[1] * 0.5),
                          (0, mz[0] + 10.0, deck + mz[1] * 0.42), (0, mz[0] + 11.5, deck + 2.8), 0.7, 'sail', ns, nt)
            g.cyl(P, (0, mz[0] + 0.4, deck + 2.6), (0, mz[0] + 12.0, deck + 2.8), 0.15, 0.1, 4, 'spar', cap0=False)
        # ensign at the stern
        g.cyl(P, (0, ys - 0.3, zs), (0, ys + 1.3, zs + 3.5), 0.06, 0.05, 4, 'spar', cap0=False)
        flag(c, Vector((0, ys + 1.3, zs + 3.4)), 3.2, 2.0)
    # crew and deck furniture
    if c.lod <= 1 and not c.ruin:
        for k in range(10 if c.lod == 0 else 5):
            y = -16 + 32 * c.hashn(k, 1)
            t = g.t_of_y(hs, y)
            hwid = max(hs.half_beam(t) - 1.2, 0.3)
            x = (c.hashn(k, 2) - 0.5) * 2 * hwid
            g.person(P, (x, y, hs.sheer_z(t) - 1.1), yaw=c.hashn(k, 3) * 6.28, h=1.75)
        g.box(P, (0, 6.0, deck + 0.4), (1.2, 2.2, 0.8), 'plank_oiled')      # capstan / binnacle block
    if c.lod <= 2:
        # quarterdeck house (the stern castle); the forecastle is carried by the sheer rising to the bow
        qz = hs.sheer_z(g.t_of_y(hs, 15.0)) - 1.1
        g.box(P, (0, 17.2, qz + 1.2), (hs.half_beam(0.89) * 1.6, 7.0, 2.4), 'plank_oiled', skip=('-z',), top_band='deck')
        if c.lod <= 1:
            g.quad(P, (-2.4, 13.68, qz + 0.5), (2.4, 13.68, qz + 0.5), (2.4, 13.68, qz + 1.8), (-2.4, 13.68, qz + 1.8),
                   'super_win', u_range=(0.1, 0.42))

        g.box(P, (0, -5.0, deck + 0.35), (3.0, 4.0, 0.7), 'plank_oiled', top_band='deck')   # hatch
        lifeboat(c, (2.0, 4.0, deck + 0.8), 6.0, 1.8, band='plank_oiled')
    return dict(L=L + 12.0, B=B + 10.0, deck=deck, scale_pos=(0.5, -3.0, deck), hs=hs,
                damage=dict(Damaged=(6.0, 1.5, 0.6), Ruin=(20.0, 4.5, 4.6)))


# --------------------------------------------------------------------------- 6. steamship (Industry age)


def steamship(c):
    L, B = 76.0, 11.0
    hs = HullSpec(L=L, B=B, sheer=5.2, keel=-4.6, n=5.0, bow=0.2, stern=0.18, transom=0.3, sheer_bow=1.6, sheer_stern=0.7,
                  rocker=0.0, rocker_stern=0.6, flare=0.04, bow_pow=0.95, stern_pow=1.6)
    P = c.P

    def bands(z):
        if z < 0.7:
            return 'steel_red'
        if z > 4.45:
            return 'stripe'
        return 'steel_black'
    g.hull(P, hs, c.n(30, 6), c.n(6, 2), c.n(6, 1), bands, deck_z=lambda t: hs.sheer_z(t) - 1.0, deck_band='deck',
           thick=0.12, marks=(0.7, 4.45) if c.lod < 3 else (), rail_band='paint_white', inner_band='paint_white')
    deck = hs.sheer - 1.0
    # midship house: two decks + bridge
    hz = deck
    house(c, (0, 0.0, hz), 8.0, 22.0, 1, 2.8)
    g.box(P, (0, -2.0, hz + 4.1), (7.0, 14.0, 2.6), 'super_win', skip=('-z',), top_band='paint_white')
    g.box(P, (0, -7.6, hz + 6.5), (10.5, 3.6, 2.2), 'super_win', skip=('-z',), top_band='paint_white')
    # funnel: culture-coloured with a black top, raked aft
    fy = 2.8
    f0 = Vector((0, fy, hz + 5.4))
    f1 = Vector((0, fy + 1.4, hz + 13.6))
    fun_cut = c.ruin
    if not fun_cut:
        g.cyl(P, f0, f0.lerp(f1, 0.82), 1.7, 1.7, c.n(12, 6), 'stripe', cap0=False, cap1=False)
        g.cyl(P, f0.lerp(f1, 0.82), f1, 1.72, 1.72, c.n(12, 6), 'metal', cap0=False, cap1=True)
    else:
        g.cyl(P, f0, f0.lerp(f1, 0.3), 1.7, 1.7, c.n(12, 6), 'char_roof', cap0=False, cap1=True)
    # ventilator cowls
    if c.lod <= 1:
        for (x, y) in ((2.6, 7.5), (-2.6, 7.5), (2.6, -11.5), (-2.6, -11.5)):
            g.cyl(P, (x, y, hz + 2.8), (x, y, hz + 4.6), 0.35, 0.35, 6, 'paint_white', cap0=False, cap1=False)
            g.cyl(P, (x, y - 0.2, hz + 4.6), (x, y - 0.75, hz + 5.0), 0.5, 0.55, 6, 'paint_white', cap0=True, cap1=False)
    # hatches fore and aft
    for y in (-24.0, -16.0, 16.0, 25.0):
        g.box(P, (0, y, deck + 0.5), (4.6, 4.2, 1.0), 'plank_oiled', top_band='plank_oiled')
    # masts with derricks
    for my, mh in ((-21.0, 19.0), (21.0, 16.0)):
        cut = None
        if c.dmg and my < 0:
            cut = mh * (0.55 if not c.ruin else 0.3)
        if c.ruin and my > 0:
            cut = mh * 0.4
        mast(c, Vector((0, my, deck)), mh, 0.32, 0.15, c.n(8, 4), cut=cut)
        top = deck + (cut if cut else mh)
        if c.lod <= 2 and (cut is None or cut > 3.5):
            for side in (-1, 1):
                g.cyl(P, (0, my + 0.5 * (1 if my < 0 else -1), deck + 2.0),
                      (side * 1.2, my + 9.0 * (1 if my < 0 else -1), deck + 6.0), 0.12, 0.08, 4, 'spar', cap0=False)
        if c.lod <= 1 and not c.ruin:
            for side in (-1, 1):
                g.line(P, (0, my, top - 0.5), (side * (B * 0.5 - 0.2), my + 1.5, hs.sheer), 0.04)
        if c.state == 'Damaged' and my < 0:
            fallen_spar(c, (0.3, my, top), (5.2, my - 7.0, deck + 0.5), 0.16)
    if c.lod <= 1 and not c.ruin:
        g.line(P, (0, -21.0, deck + 18.5), (0, hs.y(0.0) + 0.4, hs.sheer_z(0.0)), 0.04)
        g.line(P, (0, 21.0, deck + 15.5), (0, -21.0, deck + 18.0), 0.03)
    # lifeboats in davits on the house top
    if c.lod <= 1:
        for side in (-1, 1):
            for y in (-1.0, 6.0):
                if c.ruin and c.hashn(side, y) < 0.7:
                    continue
                lifeboat(c, (side * 4.4, y, hz + 3.4), 6.5, 1.9)
    if not c.ruin:
        g.cyl(P, (0, hs.y(1.0) - 0.6, hs.sheer_z(1.0)), (0, hs.y(1.0) + 0.6, hs.sheer_z(1.0) + 4.5), 0.07, 0.05, 4, 'paint_white', cap0=False)
        flag(c, Vector((0, hs.y(1.0) + 0.6, hs.sheer_z(1.0) + 4.4)), 3.0, 1.8)
        mt = deck + (19.0 * 0.55 if c.dmg else 19.0)
        flag(c, Vector((0, -21.0, mt + 0.05)), 3.5, 0.7, pennant=True)
    if c.lod <= 1 and not c.ruin:
        for k in range(6 if c.lod == 0 else 3):
            y = (-30, -18, 12, 20, -6.5, 30)[k]
            g.person(P, ((k % 3 - 1) * 1.6, y, deck if abs(y) > 11 else hz + 5.4 + (2.2 if k == 4 else 0)), yaw=k * 1.3, h=1.75)
    return dict(L=L, B=B, deck=deck, scale_pos=(2.0, -12.0, deck), hs=hs, close_target=(0, -2.0, 6.0),
                damage=dict(Damaged=(5.0, 1.5, 0.9), Ruin=(14.0, -6.0, 5.0)))


# --------------------------------------------------------------------------- 7. container ship (Atomic age)


def container_ship(c):
    L, B = 150.0, 24.0
    hs = HullSpec(L=L, B=B, sheer=10.5, keel=-8.5, n=9.0, bow=0.20, stern=0.16, transom=0.72, sheer_bow=2.8, sheer_stern=0.0,
                  rocker=0.0, rocker_stern=1.5, flare=0.07, bow_pow=0.9, stern_pow=1.4, n_end=3.0)
    P = c.P

    def bands(z):
        if z < 1.6:
            return 'steel_red'
        if z > 9.4:
            return 'stripe'
        return 'steel_black'
    g.hull(P, hs, c.n(34, 6), c.n(7, 2), c.n(6, 1), bands, deck_z=lambda t: hs.sheer_z(t) - 1.1, deck_band='steel_deck',
           thick=0.3, marks=(1.6, 9.4) if c.lod < 3 else (), rail_band='steel_deck', inner_band='steel_black',
           transom_band='steel_black')
    deck = hs.sheer - 1.1
    yb = hs.y(0.0)
    # bulbous bow
    if c.lod <= 1:
        g.cyl(P, (0, yb + 6.0, -5.0), (0, yb - 2.5, -4.6), 2.2, 0.6, c.n(10, 6), 'steel_red', cap0=False, cap1=True)
    # accommodation block aft, bridge wings, funnel
    ay = 52.0
    nd = 7
    hh = nd * 2.8
    house(c, (0, ay, deck), 20.0, 11.0, nd, 2.8)
    g.box(P, (0, ay - 1.0, deck + hh + 1.4), (B + 3.0, 6.0, 2.8), 'super_win', skip=(), top_band='paint_white')
    if c.lod <= 2:
        g.box(P, (0, ay - 4.05, deck + hh + 1.6), (B + 2.6, 0.2, 1.6), 'glass')
        g.cyl(P, (0, ay + 1.0, deck + hh + 2.8), (0, ay + 1.0, deck + hh + 9.0), 0.25, 0.15, 6, 'metal', cap0=False)
        g.box(P, (0, ay + 1.0, deck + hh + 7.5), (3.5, 0.3, 0.3), 'metal')
    fy = ay + 11.0
    fh = 13.0 if not c.ruin else 5.0
    g.box(P, (0, fy, deck + fh * 0.5), (5.4, 6.0, fh), 'stripe', skip=('-z',), top_band='metal')
    g.box(P, (0, fy, deck + fh + 0.6), (5.6, 6.2, 1.2), 'metal', skip=('-z',))
    # containers: bays of 40 ft boxes (12.2 x 2.44 x 2.6), with gaps so the colours read bay by bay
    cl, cw, ch = 12.2, 2.44, 2.6
    cols = 9
    bay_ys = []
    y = ay - 8.0 - cl * 0.5
    while y > yb + 22.0:
        bay_ys.append(y)
        y -= cl + 0.9
    rng = random.Random(4041)
    for bi, by in enumerate(bay_ys):
        t = g.t_of_y(hs, by)
        hb = hs.half_beam(t) - 0.6
        ncols = min(cols, int(2 * hb / cw))
        if ncols < 3:
            continue
        x0 = -ncols * cw * 0.5
        heights = []
        for ci in range(ncols):
            base = 4 if 0.3 < t < 0.75 else 3
            hgt = max(1, base + rng.choice((0, 0, 1, -1)) - (1 if ci in (0, ncols - 1) else 0))
            if c.dmg:
                if c.hashn(bi, ci, 9) < (0.3 if not c.ruin else 0.8):
                    hgt = max(0, hgt - rng.choice((1, 2, 3)))
            heights.append(hgt)
        if c.lod >= 2:
            # one block per bay with a flat colour band
            hmax = max(heights) if heights else 0
            if hmax == 0:
                continue
            k = rng.randrange(8)
            g.box(P, (0, by, deck + hmax * ch * 0.5), (ncols * cw, cl, hmax * ch), 'containers', skip=('-z',),
                  u_range=g.seg_u('containers', k))
            continue
        if c.lod == 1:
            # one box per stack (column), single colour, hidden sides skipped
            for ci in range(ncols):
                if heights[ci] == 0:
                    continue
                x = x0 + (ci + 0.5) * cw
                k = rng.randrange(8)
                skip = ['-z']
                if ci > 0 and heights[ci - 1] >= heights[ci]:
                    skip.append('-x')
                if ci < ncols - 1 and heights[ci + 1] >= heights[ci]:
                    skip.append('+x')
                hz_ = heights[ci] * ch
                g.box(P, (x, by, deck + hz_ * 0.5), (cw - 0.04, cl, hz_), 'containers', skip=tuple(skip),
                      u_range=g.seg_u('containers', k))
            continue
        for ci in range(ncols):
            x = x0 + (ci + 0.5) * cw
            for tier in range(heights[ci]):
                z = deck + (tier + 0.5) * ch
                k = rng.randrange(8) if rng.random() > 0.12 else 7
                if k == 7 and rng.random() < 0.6:
                    k = rng.randrange(7)
                skip = ['-z']
                if tier < heights[ci] - 1:
                    skip.append('+z')
                if ci > 0 and heights[ci - 1] > tier:
                    skip.append('-x')
                if ci < ncols - 1 and heights[ci + 1] > tier:
                    skip.append('+x')
                g.box(P, (x, by, z), (cw - 0.04, cl, ch - 0.04), 'containers', skip=tuple(skip), u_range=g.seg_u('containers', k))
    if c.state == 'Damaged' and c.lod <= 1:
        # a few boxes knocked askew
        for k in range(5):
            x = (c.hashn(k, 1) - 0.5) * 16
            y = bay_ys[k % len(bay_ys)] + 3
            g.box(P, (x, y, deck + 8.0 + k), (cw, cl, ch), 'containers', R=Matrix.Rotation(0.4 + k * 0.2, 3, 'Y'),
                  u_range=g.seg_u('containers', k % 7))
    # forecastle details: mooring winches block, mast
    g.box(P, (0, yb + 9.0, hs.sheer_z(0.05) - 0.4), (8.0, 4.0, 1.0), 'metal')
    if c.lod <= 1:
        g.cyl(P, (0, yb + 8.0, hs.sheer_z(0.05)), (0, yb + 8.0, hs.sheer_z(0.05) + 9.0), 0.25, 0.18, 6, 'paint_white', cap0=False)
    if not c.ruin:
        g.cyl(P, (0, hs.y(1.0) - 0.5, hs.sheer), (0, hs.y(1.0) - 0.5, hs.sheer + 5.0), 0.08, 0.06, 4, 'paint_white', cap0=False)
        flag(c, Vector((0, hs.y(1.0) - 0.5, hs.sheer + 4.9)), 3.6, 2.2)
    if c.lod <= 1 and not c.ruin:
        for k in range(4):
            g.person(P, (B * 0.5 + 0.6 if k < 2 else -B * 0.5 - 0.6, ay - 3.0 + k, deck + hh + 0.0), yaw=k, h=1.75)
    return dict(L=L, B=B, deck=deck, scale_pos=(B * 0.5 + 1.0, ay - 2.0, deck + hh), hs=hs, close_target=(0, 30.0, 17.0),
                damage=dict(Damaged=(6.0, 1.0, 1.2), Ruin=(18.0, -4.0, 7.0)))


# --------------------------------------------------------------------------- 8. fast sea freighter (Space age)


def fast_freighter(c):
    L, B = 112.0, 16.0
    hs = HullSpec(L=L, B=B, sheer=6.6, keel=-3.8, n=4.0, bow=0.42, stern=0.08, transom=0.86, sheer_bow=0.8, sheer_stern=0.0,
                  rocker=1.6, rocker_stern=0.6, flare=0.20, bow_pow=1.25, stern_pow=1.0, n_end=2.4)
    P = c.P

    def bands(z):
        if z < 0.9:
            return 'steel_black'
        if z < 2.4:
            return 'stripe'
        return 'composite'

    def rake(t, z):
        # wave-piercing bow: the keel line reaches forward, the deck edge falls back
        e = max(0.0, 1.0 - t / 0.42)
        return -(hs.sheer - z) * 0.55 * e ** 2.2
    g.hull(P, hs, c.n(32, 6), c.n(6, 2), c.n(5, 1), bands, deck_z=lambda t: hs.sheer_z(t) - 0.25, deck_band='composite',
           thick=0.25, marks=(0.9, 2.4) if c.lod < 3 else (), y_rake=rake, rail_band='composite', inner_band='composite',
           transom_band='composite')
    deck = hs.sheer - 0.25
    # gold trim line along the sheer
    if c.lod <= 2:
        stripe_strip(c, hs, 0.12, 0.98, -0.75, -0.45, 'gold', c.n(16, 4), grow=1.01)
    # forward superstructure: a low glazed wedge
    wy0, wy1 = -34.0, -14.0
    hw = 6.2
    h0 = 6.0
    pts_bot = [Vector((-hw, wy1, deck)), Vector((hw, wy1, deck)), Vector((hw * 0.7, wy0, deck)), Vector((-hw * 0.7, wy0, deck))]
    pts_top = [Vector((-hw * 0.85, wy1 - 1.0, deck + h0)), Vector((hw * 0.85, wy1 - 1.0, deck + h0)),
               Vector((hw * 0.45, wy0 + 6.0, deck + h0 * 0.8)), Vector((-hw * 0.45, wy0 + 6.0, deck + h0 * 0.8))]
    s0 = len(P.F)
    for k in range(4):
        a, b = pts_bot[k], pts_bot[(k + 1) % 4]
        cc, d = pts_top[(k + 1) % 4], pts_top[k]
        # lower 55 % composite, upper glazing band
        m1 = a.lerp(d, 0.55)
        m2 = b.lerp(cc, 0.55)
        g.quad(P, a, b, m2, m1, 'composite')
        g.quad(P, m1, m2, cc, d, 'glass')
    g.cap(P, pts_top, 'composite', ref=lambda q: q - Vector((0, 0, 1)))
    ctr = Vector((0, (wy0 + wy1) * 0.5, deck + h0 * 0.4))
    P.orient(s0, lambda q: ctr)
    # culture fin on the wedge
    fin = [Vector((0, wy1 - 3.0, deck + h0)), Vector((0, wy1 - 9.0, deck + h0 * 0.9)), Vector((0, wy1 - 2.0, deck + h0 + 5.5)),
           Vector((0, wy1 - 0.6, deck + h0 + 5.5))]
    if not c.ruin:
        g.grid(P, lambda s, t: tuple(fin[0].lerp(fin[1], s).lerp(fin[3].lerp(fin[2], s), t)), 1, 1, 'flag', two_sided=True,
               thick=0.25)
    # cargo: long white cylindrical segments with gold end rings, in two rows
    seg_len = 15.0
    ys = [-9.0 + k * (seg_len + 1.4) for k in range(4)]
    for row in (-1, 1):
        if c.lod == 3:
            g.box(P, (row * 3.35, ys[0] + (ys[-1] + seg_len - ys[0]) * 0.5, deck + 2.9), (5.2, ys[-1] + seg_len - ys[0], 5.2),
                  'paint_white', skip=('-z',))
            continue
        for k, y in enumerate(ys):
            if c.dmg and c.hashn(row, k, 4) < (0.25 if not c.ruin else 0.7):
                continue
            r = 2.7
            x = row * 3.35
            z = deck + r + 0.3
            g.cyl(P, (x, y, z), (x, y + seg_len, z), r, r, c.n(14, 6) if c.lod < 2 else 5, 'paint_white', cap0=True, cap1=True)
            if c.lod <= 1:
                for e in (y + 0.35, y + seg_len - 0.35):
                    g.cyl(P, (x, e - 0.3, z), (x, e + 0.3, z), r + 0.08, r + 0.08, c.n(14, 6), 'gold', cap0=False, cap1=False)
            if c.lod <= 1:
                g.box(P, (x, y + seg_len * 0.5, deck + 0.25), (2.4, seg_len - 1.0, 0.5), 'metal')
    # engine pods at the stern with blue glow
    for side in (-1, 1):
        x = side * 4.6
        y_end = hs.y(1.0)
        nsd = c.n(12, 6) if c.lod < 3 else 4
        g.cyl(P, (x, y_end - 10.0, 0.6), (x, y_end + 0.6, 0.6), 1.5, 1.4, nsd, 'composite', cap0=c.lod < 3, cap1=False)
        g.cyl(P, (x, y_end + 0.6, 0.6), (x, y_end + 0.9, 0.6), 1.4, 1.0, nsd, 'glow' if not c.ruin else 'metal',
              cap0=False, cap1=True)
    # mast with sensor bar
    if c.lod <= 2:
        g.cyl(P, (0, wy1 - 2.0, deck + h0), (0, wy1 - 2.0, deck + h0 + 8.0), 0.2, 0.12, 6 if c.lod < 2 else 3, 'composite', cap0=False)
        g.box(P, (0, wy1 - 2.0, deck + h0 + 6.5), (4.0, 0.3, 0.3), 'gold')
    if not c.ruin:
        g.cyl(P, (0, hs.y(1.0) - 1.0, deck), (0, hs.y(1.0) - 1.0, deck + 4.5), 0.07, 0.05, 4, 'composite', cap0=False)
        flag(c, Vector((0, hs.y(1.0) - 1.0, deck + 4.4)), 3.0, 1.8)
    if c.lod <= 1 and not c.ruin:
        for k in range(3):
            g.person(P, (-2.0 + 2.0 * k, wy1 + 1.5, deck), yaw=k, h=1.75)
    return dict(L=L + 4.0, B=B, deck=deck, scale_pos=(4.0, wy1 + 3.0, deck), hs=hs, close_target=(0, -22.0, 8.0),
                damage=dict(Damaged=(5.0, 1.0, 0.8), Ruin=(16.0, -3.0, 5.0)))


# --------------------------------------------------------------------------- registry

ASSETS = [
    # key, builder, display, sim ages it serves, default culture index for previews, kit mapping in DmTownsKit
    ('ReedBoat', reed_boat, 'Reed boat', ['Age0 Stone', 'Age1 Bronze (river)'], 0, 'Boat V0'),
    ('Canoe', canoe, 'Plank canoe with sail', ['Age1 Bronze', 'Age2 Iron (fishing)'], 0, 'Boat V1'),
    ('Galley', galley, 'War galley with oars', ['Age1 Bronze (late)', 'Age2 Iron'], 1, 'Galley'),
    ('Cog', cog, 'Cog', ['Age2 Iron (late)', 'Age3 Sail (early)'], 1, 'SailShip V0'),
    ('SailShip', sail_ship, 'Full-rigged sailing ship', ['Age3 Sail', 'Age4 Industry (early)'], 1, 'SailShip V1'),
    ('Steamship', steamship, 'Single-funnel steamship', ['Age4 Industry'], 2, 'Steamer'),
    ('ContainerShip', container_ship, 'Container ship', ['Age5 Atomic'], 2, 'CargoShip V0'),
    ('FastFreighter', fast_freighter, 'Fast sea freighter', ['Age6 Space'], 5, 'CargoShip V1'),
]


def noise3(p, seed, scale):
    x, y, z = p.x * scale, p.y * scale, p.z * scale
    s = 0.0
    r = random.Random(seed)
    for k in range(4):
        a = [r.uniform(-1, 1) for _ in range(3)]
        ph = r.uniform(0, 6.28)
        f = 1.0 + k * 0.9
        s += math.sin((a[0] * x + a[1] * y + a[2] * z) * f + ph) / (1 + k * 0.6)
    return s


def apply_state(P, info, state, seed, lod=0):
    """Burn patches (re-band faces), then heel, trim and sink the whole ship."""
    if state == 'Intact':
        return P
    frac = 0.28 if state == 'Damaged' else 0.75
    L = info['L']
    sc = 6.0 / max(L, 6.0)
    vals, areas = [], []
    for f in P.F:
        cpt = P.centroid(f)
        vals.append(noise3(cpt, seed, sc) + 0.35 * noise3(cpt, seed + 1, sc * 4.0))
        areas.append(P.normal(f).length * 0.5)
    # threshold so that `frac` of the burnable surface AREA burns (not of the face count)
    # in the Damaged state a single big face (a deckhouse wall, a whole deck panel) never burns whole
    big = (L * 0.12) ** 2
    if state == 'Damaged':
        for i, f in enumerate(P.F):
            if areas[i] > big and f[1] in CHAR:
                vals[i] = -1e9
    order = sorted(range(len(vals)), key=lambda i: -vals[i])
    tot = sum(a for a, f in zip(areas, P.F) if f[1] in CHAR) or 1.0
    acc, th = 0.0, float('inf')
    for i in order:
        if P.F[i][1] not in CHAR:
            continue
        acc += areas[i]
        th = vals[i]
        if acc >= frac * tot:
            break
    for f, v in zip(P.F, vals):
        if v >= th and f[1] in CHAR:
            f[1] = CHAR[f[1]]
    if state == 'Ruin':
        for f in P.F:
            if f[1] in ('sail', 'sail_stripe'):
                f[1] = 'sail_torn'
    roll, pitch, sink = info['damage'][state]
    M = Matrix.Translation((0, 0, -sink)) @ Matrix.Rotation(math.radians(pitch), 4, 'X') @ Matrix.Rotation(math.radians(roll), 4, 'Y')
    P.transform(M)
    return P


def build(key, lod, state):
    for k, fn, *_ in ASSETS:
        if k == key:
            seed = sum(ord(ch) for ch in key)
            c = Ctx(lod, state, seed)
            info = fn(c)
            apply_state(c.P, info, state, seed, lod)
            return c.P, info
    raise KeyError(key)
