# DEMIURGE art - Age6 (Space Age) asset builders.
#
# Each builder(B) appends Parts to B.parts for one LOD and returns an info dict:
#   w, d     footprint (m, local X and Y)       h      total height (m)
#   hbody    height of the main mass (for state cuts)   style  'tall' | 'flat' | 'none'
#   sockets  named points the integration code may use (crown, rocket mount, track ends)
# Storey height is 3.6 m and facade tiers start on multiples of it so the window bands line up.
import math
import random
from mathutils import Vector, Matrix

import age6_kit as K
from age6_kit import Skin, region_skin, prism, box, lathe, sphere, beam, blob, sweep, rrect, circle, sector
from age6_tex import TECH_REG, FACADE_TILE, ROOF_TILE, CONCRETE_TILE, HULL_TILE, CLAD_TILE

ST = 3.6
S, R_, D_ = 0, 1, 2      # material slots: Structure, Roof, Detail

# slot sets (Structure, Roof, Detail)
SET_CITY = ('M_Age6_Facade', 'M_Age6_GreenRoof', 'M_Age6_Tech')
SET_CONC = ('M_Age6_Concrete', 'M_Age6_GreenRoof', 'M_Age6_Tech')
SET_HULL = ('M_Age6_Hull', None, 'M_Age6_Tech')
SET_HALL = ('M_Age6_Cladding', 'M_Age6_RoofMetal', 'M_Age6_Tech')


def tech(name):
    return region_skin(D_, TECH_REG[name])


FAC = Skin(S, 'facade', FACADE_TILE, pin=(0.4 / FACADE_TILE, 0.75 / FACADE_TILE), spread=(0.006, 0.008))
PANEL = Skin(S, 'point', FACADE_TILE, pin=(0.4 / FACADE_TILE, 0.75 / FACADE_TILE), spread=(0.006, 0.008))
ROOF = Skin(R_, 'tile', ROOF_TILE)
CONC = Skin(S, 'tile', CONCRETE_TILE)
CONC_ROOFSLOT = Skin(R_, 'tile', ROOF_TILE)
HULL = Skin(S, 'tile', HULL_TILE)
CLAD = Skin(S, 'tile', CLAD_TILE)
RMETAL = Skin(R_, 'tile', CLAD_TILE)
WHITE, STEEL, GOLD, BLACK, ORANGE, LAMP, GLASS = (tech(n) for n in ('white', 'steel', 'gold', 'black', 'orange', 'redlamp', 'glass'))


def state_skins(struct_skin):
    return {'cap': struct_skin, 'rubble': struct_skin, 'steel': STEEL, 'orange': ORANGE}


class B:
    """Per-LOD build context."""

    def __init__(self, lod, seed):
        self.lod = lod
        self.rng = random.Random(seed)
        self.parts = []

    def add(self, p):
        if p is not None:
            self.parts.append(p)
        return p

    def seg(self, n):
        return {0: n, 1: max(1, n // 2), 2: 1, 3: 0}[self.lod]

    def circ(self, n):
        return {0: n, 1: max(10, n // 2), 2: max(8, n // 3), 3: 6}[self.lod]

    # ---- planting
    def shrubs_along(self, profile, z, inset, spacing, rmin=0.9, rmax=1.7, exclude=None, prob=1.0):
        if self.lod >= 2:
            return
        sp = spacing * (1.0 if self.lod == 0 else 2.4)
        sub = 1 if self.lod == 0 else 0
        for p, n in K.perimeter_points(profile, sp, 0.3, self.rng):
            q = p - n * inset
            if exclude is not None and _inside(exclude, q.x, q.y, 0.8):
                continue
            if self.rng.random() > prob:
                continue
            self.add(blob((q.x, q.y, z), self.rng.uniform(rmin, rmax), self.rng, ROOF, subdiv=sub, squash=0.75))

    def shrubs_scatter(self, x0, y0, x1, y1, z, count, rmin=1.0, rmax=2.0):
        if self.lod >= 2:
            return
        if self.lod == 1:
            count = max(1, int(count * 0.4))
        sub = 1 if self.lod == 0 else 0
        for _ in range(count):
            self.add(blob((self.rng.uniform(x0, x1), self.rng.uniform(y0, y1), z), self.rng.uniform(rmin, rmax), self.rng, ROOF, subdiv=sub, squash=0.8))

    def parapet(self, profile, z, h=1.0, t=0.22, skin=PANEL, inset=0.15):
        if self.lod >= 2:
            return
        prof = K.offset_profile(profile, -inset) if inset else profile
        n = len(prof)
        step = 1 if self.lod == 0 else 2
        for i in range(0, n, step):
            a, b = prof[i], prof[(i + step) % n]
            self.add(beam((a[0], a[1], z + h * 0.5), (b[0], b[1], z + h * 0.5), t, h, skin))

    def solar_panel(self, cx, cy, z, w, d, tilt, rot=0.0, kind='detail', tag='', legs=True):
        """A tilted module table; the top face carries the full solar-cell region."""
        p = prism(rrect(w, d, 0, 0), -0.06, 0.06, WHITE, kind=kind, tag=tag, smooth=False)
        u0, v0, u1, v1 = TECH_REG['solar']
        p.bm.normal_update()
        for f in p.bm.faces:
            if f.normal.z > 0.9:
                for l in f.loops:
                    co = l.vert.co
                    l[p.uv].uv = (u0 + (u1 - u0) * (0.01 + 0.98 * (co.x / w + 0.5)), v0 + (v1 - v0) * (0.01 + 0.98 * (co.y / d + 0.5)))
        p.transform(Matrix.Rotation(tilt, 4, 'X'))
        p.rotz(rot)
        p.move(cx, cy, z)
        self.add(p)
        if legs and self.lod == 0:
            for sx in (-0.4, 0.4):
                lo = Vector((sx * w, -0.35 * d * math.cos(tilt), 0)).to_3d()
                hi = Vector((sx * w, 0.35 * d * math.cos(tilt), 0))
                for pp, hh in ((lo, z - 0.35 * d * math.sin(tilt)), (hi, z + 0.35 * d * math.sin(tilt))):
                    q = Matrix.Rotation(rot, 3, 'Z') @ pp
                    self.add(beam((cx + q.x, cy + q.y, z - 1.5 if z > 1.5 else -0.3), (cx + q.x, cy + q.y, hh), 0.12, 0.12, STEEL, kind=kind, tag=tag))

    def roof_solar(self, cx, cy, z, w, d, rot=0.0):
        """Rows of tilted modules on a flat roof (facing -Y)."""
        if self.lod >= 3:
            return
        rows = max(1, int(d / 3.2))
        if self.lod == 2:
            self.add(box(cx, cy, z, w, d, 0.5, WHITE, top=None, rot=rot, kind='detail'))
            return
        for r in range(rows):
            y = -d * 0.5 + (r + 0.5) * d / rows
            if self.lod == 0:
                n = max(1, int(w / 2.2))
                for k in range(n):
                    x = -w * 0.5 + (k + 0.5) * w / n
                    q = Matrix.Rotation(rot, 3, 'Z') @ Vector((x, y, 0))
                    self.solar_panel(cx + q.x, cy + q.y, z + 0.8, w / n - 0.15, 2.0, math.radians(18), rot, legs=False)
            else:
                q = Matrix.Rotation(rot, 3, 'Z') @ Vector((0, y, 0))
                self.solar_panel(cx + q.x, cy + q.y, z + 0.8, w, 2.0, math.radians(18), rot, legs=False)

    def spire(self, x, y, z0, h, r0, r1, skin=WHITE, lamp=False):
        n = {0: 10, 1: 6, 2: 4, 3: 4}[self.lod]
        self.add(lathe([(r0, z0), (r1, z0 + h)], n, skin, kind='detail').move(x, y, 0))
        if lamp and self.lod <= 1:
            self.add(box(x, y, z0 + h, 0.6, 0.6, 0.6, LAMP, kind='detail'))

    def planter_ring(self, prof, z, exclude=None, width=1.7, tree_spacing=9.0, trees=True):
        """White planter box with a lumpy hedge spilling over it along a terrace edge, plus small trees."""
        lod = self.lod
        if lod >= 3:
            return
        sp = {0: 1.8, 1: 3.6, 2: 6.0}[lod]
        smp = K.perimeter_points(prof, sp)
        ok = [exclude is None or not _inside(exclude, (q - n * 1.0).x, (q - n * 1.0).y, 0.3) for q, n in smp]
        runs = []
        if all(ok):
            runs.append((smp, True))
        else:
            k0 = ok.index(False)
            cur = []
            for t in range(1, len(smp) + 1):
                k = (k0 + t) % len(smp)
                if ok[k]:
                    cur.append(smp[k])
                elif cur:
                    runs.append((cur, False))
                    cur = []
            if cur:
                runs.append((cur, False))
        for run, closed in runs:
            if len(run) < 2:
                continue
            if lod <= 1:
                self.add(K.hedge(run, closed, z, 0.45, 0.75, 0.0, self.rng, PANEL, kind='detail'))
            self.add(K.hedge(run, closed, z + (0.45 if lod <= 1 else 0.0), width, 0.85, 0.4 if lod == 0 else 0.15, self.rng, ROOF))
            if trees and lod <= 1:
                step = max(1, int(tree_spacing * (1.0 if lod == 0 else 1.6) / sp))
                for i in range(self.rng.randrange(step), len(run), step):
                    q, n = run[i]
                    c = q - n * (width * 0.55)
                    self.add(blob((c.x, c.y, z + 1.0), self.rng.uniform(1.3, 2.3), self.rng, ROOF, subdiv=1 if lod == 0 else 0, squash=0.85))

    def terrace_tier(self, prof, z0, z1, next_prof=None, shrub_spacing=3.2, parapet=True, side=FAC, roof=ROOF):
        self.add(prism(prof, z0, z1, side, roof, PANEL))
        if next_prof is None or _gap(prof, next_prof) > 1.5:
            self.planter_ring(prof, z1, next_prof)


def _inside(prof, x, y, margin=0.0):
    n = len(prof)
    for i in range(n):
        ax, ay = prof[i]
        bx, by = prof[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        L = math.hypot(ex, ey) or 1.0
        if (ex * (y - ay) - ey * (x - ax)) / L < -margin:
            return False
    return True


def _gap(a, b):
    """Widest exposed band between profile a and the (smaller) profile b above it."""
    d = [max(p[0] for p in a) - max(p[0] for p in b), min(p[0] for p in b) - min(p[0] for p in a),
         max(p[1] for p in a) - max(p[1] for p in b), min(p[1] for p in b) - min(p[1] for p in a)]
    return max(0.0, max(d))


def _shift(prof, dx, dy):
    return [(x + dx, y + dy) for x, y in prof]


# =========================================================================== arcology towers


def _balcony(b, c, d, z, bw, depth=3.0):
    """Garden balcony slab sticking out along direction d, with a planted front edge."""
    a = math.atan2(d.y, d.x)
    b.add(box(c.x, c.y, z, bw, depth, 0.6, PANEL, ROOF, rot=a + math.pi / 2))
    if b.lod <= 1:
        t = Vector((-d.y, d.x, 0))
        e = c + d * (depth * 0.5)
        smp = K.line_samples(e - t * (bw * 0.5 - 0.3), e + t * (bw * 0.5 - 0.3), 1.8 if b.lod == 0 else 3.5, d)
        b.add(K.hedge(smp, False, z + 0.6, 1.3, 0.8, 0.4 if b.lod == 0 else 0.15, b.rng, ROOF))
        if b.lod == 0:
            for s_ in (-0.3, 0.25):
                q = c + t * (bw * s_) + d * 0.2
                b.add(blob((q.x, q.y, z + 1.3), b.rng.uniform(1.1, 1.7), b.rng, ROOF, subdiv=1))


def tower_a(b):
    """Garden spire: slender rounded-square shaft with alternating garden balconies."""
    sg = b.seg(6)
    pod = rrect(32, 28, 7, sg)
    shaft = rrect(20, 20, 7, sg)
    b.terrace_tier(pod, -0.3, 7.2, shaft)
    b.add(prism(shaft, 7.2, 111.6, FAC, PANEL, PANEL))
    k = 0
    z = 14.4
    while z < 106 and b.lod <= 2:
        faces = (0, 2) if k % 2 == 0 else (1, 3)
        for f in faces:
            a = f * math.pi / 2
            d = Vector((math.cos(a), math.sin(a), 0))
            _balcony(b, d * (10 + 1.4), d, z, 13.0, 2.8)
        z += 7.2
        k += 1
    b.add(prism(rrect(21.2, 21.2, 7.6, sg), 111.6, 112.4, GOLD, GOLD, GOLD))
    crown = rrect(15, 15, 5, sg)
    b.add(prism(crown, 112.4, 119.6, FAC, ROOF, PANEL))
    b.planter_ring(crown, 119.6, trees=False)
    b.spire(0, 0, 119.6, 16.0, 0.7, 0.1, lamp=True)
    return dict(w=32, d=28, h=135.6, hbody=119.6, style='tall', sockets={'crown': (0, 0, 119.6)})


def tower_b(b):
    """Stepped arcology: four rounded tiers with planted setbacks."""
    sg = b.seg(6)
    tiers = [(38, 32, 9, -0.3, 28.8), (30, 25, 8, 28.8, 57.6), (23, 19, 7, 57.6, 86.4), (16, 14, 5.5, 86.4, 108.0)]
    profs = [rrect(w, d, r, sg) for w, d, r, _, _ in tiers]
    for i, (w, d, r, z0, z1) in enumerate(tiers):
        nxt = profs[i + 1] if i + 1 < len(profs) else None
        b.terrace_tier(profs[i], z0, z1, nxt)
    b.roof_solar(0, 0, 108.0, 10, 8)
    b.add(prism(rrect(4, 4, 1, b.seg(2)), 108.0, 111.6, PANEL, PANEL, PANEL))
    b.spire(0, 0, 111.6, 12.0, 0.6, 0.1, lamp=True)
    return dict(w=38, d=32, h=123.6, hbody=108.0, style='tall', sockets={'crown': (0, 0, 108.0)})


def tower_c(b):
    """Round tower with a helix of garden balconies."""
    n = b.circ(40)
    b.add(prism(circle(11, n), -0.3, 147.6, FAC, PANEL, PANEL))
    k = 0
    z = 7.2
    while z < 144:
        if b.lod <= 2:
            a0 = math.radians(k * 37.0)
            ns = {0: 8, 1: 4, 2: 2}[b.lod]
            a1 = a0 + math.radians(80)
            b.add(prism(sector(10.6, 14.4, a0, a1, ns), z, z + 0.6, PANEL, ROOF, PANEL))
            if b.lod <= 1:
                m = {0: 9, 1: 4}[b.lod]
                smp = []
                for i in range(m + 1):
                    a = a0 + (a1 - a0) * (0.04 + 0.92 * i / m)
                    nrm = Vector((math.cos(a), math.sin(a), 0))
                    smp.append((nrm * 14.2, nrm))
                b.add(K.hedge(smp, False, z + 0.6, 1.4, 0.8, 0.4 if b.lod == 0 else 0.15, b.rng, ROOF))
                if b.lod == 0:
                    a = (a0 + a1) * 0.5
                    b.add(blob((13.0 * math.cos(a), 13.0 * math.sin(a), z + 1.3), b.rng.uniform(1.3, 1.9), b.rng, ROOF, subdiv=1))
        z += 7.2
        k += 1
    b.add(prism(circle(12.2, n), 147.6, 148.4, GOLD, GOLD, GOLD))
    crown = circle(8.5, n)
    b.add(prism(crown, 148.4, 155.6, FAC, ROOF, PANEL))
    b.planter_ring(crown, 155.6, trees=False)
    b.spire(0, 0, 155.6, 20.0, 0.9, 0.12, lamp=True)
    return dict(w=28.8, d=28.8, h=176.2, hbody=155.6, style='tall', sockets={'crown': (0, 0, 155.6)})


def tower_d(b):
    """Twin rounded slabs on a planted podium, joined by garden sky-bridges."""
    sg = b.seg(6)
    pod = rrect(48, 40, 7, sg)
    s1 = _shift(rrect(30, 11, 5.5, sg), 0, 10)
    s2 = _shift(rrect(30, 11, 5.5, sg), 0, -10)
    b.add(prism(pod, -0.3, 7.2, FAC, ROOF, PANEL))
    b.planter_ring(pod, 7.2)
    b.add(prism(s1, 7.2, 93.6, FAC, ROOF, PANEL))
    b.add(prism(s2, 7.2, 79.2, FAC, ROOF, PANEL))
    for z in (28.8, 50.4, 72.0):
        br = rrect(14, 10, 1, b.seg(2))
        b.add(prism(br, z, z + 7.2, FAC, ROOF, PANEL))
        if b.lod <= 1:
            for sx in (-1, 1):
                d = Vector((sx, 0, 0))
                smp = K.line_samples((sx * 7, -4.2, 0), (sx * 7, 4.2, 0), 1.8, d)
                b.add(K.hedge(smp, False, z + 7.2, 1.5, 0.8, 0.35, b.rng, ROOF))
    b.planter_ring(s1, 93.6, tree_spacing=8)
    b.planter_ring(s2, 79.2, tree_spacing=8)
    b.roof_solar(0, 10, 93.6, 14, 4)
    b.spire(9, 10, 93.6, 9.0, 0.4, 0.08, lamp=True)
    return dict(w=48, d=40, h=102.6, hbody=93.6, style='tall', sockets={'crown': (0, 10, 93.6)})


def tower_e(b):
    """Hero arcology: a terraced garden mountain with a slender spire tower rising from it."""
    sg = b.seg(6)
    tiers = []
    for k in range(6):
        tiers.append((rrect(48 - 4.4 * k, 44 - 4.4 * k, 10 - k, sg), -0.3 if k == 0 else 7.2 * k, 7.2 * (k + 1)))
    shaft = rrect(19, 19, 8, sg)
    for i, (pf, z0, z1) in enumerate(tiers):
        nxt = tiers[i + 1][0] if i + 1 < len(tiers) else shaft
        b.terrace_tier(pf, z0, z1, nxt)
    b.add(prism(shaft, 43.2, 165.6, FAC, PANEL, PANEL))
    for z in (79.2, 115.2, 151.2):
        ring = rrect(25, 25, 10, sg)
        b.add(prism(ring, z, z + 0.7, PANEL, ROOF, PANEL))
        b.planter_ring(ring, z + 0.7, shaft, width=1.5, tree_spacing=12)
    b.add(prism(rrect(20.4, 20.4, 8.6, sg), 165.6, 166.4, GOLD, GOLD, GOLD))
    crown = rrect(15, 15, 6, sg)
    b.add(prism(crown, 166.4, 173.6, FAC, PANEL, PANEL))
    nd = b.circ(24)
    rz = [(6.0 * math.cos(math.radians(a)), 173.6 + 5.2 * math.sin(math.radians(a))) for a in range(0, 90, 15 if b.lod == 0 else 30)] + [(0.0, 178.8)]
    b.add(lathe(rz, nd, GLASS, kind='body'))
    b.spire(0, 0, 178.8, 12.0, 0.6, 0.08, lamp=True)
    return dict(w=48, d=44, h=190.8, hbody=173.6, style='tall', build_frac=0.45, sockets={'crown': (0, 0, 173.6)})


# =========================================================================== green-roof district blocks


def block_a(b):
    """Stadium ziggurat, five planted steps."""
    sg = b.seg(5)
    profs = [rrect(44 - 6 * k, 28 - 4 * k, (28 - 4 * k) / 2, sg) for k in range(5)]
    for k in range(5):
        b.terrace_tier(profs[k], -0.3 if k == 0 else ST * k, ST * (k + 1), profs[k + 1] if k < 4 else None, 2.6)
    b.roof_solar(0, 0, 18.0, 12, 6)
    return dict(w=44, d=28, h=19.5, hbody=18.0, style='tall', build_frac=0.6)


def block_b(b):
    """Street block that steps down to its front (-Y) in planted terraces."""
    sg = b.seg(4)
    profs = [_shift(rrect(40, 30 - 4 * k, 5, sg), 0, 2 * k) for k in range(6)]
    for k in range(6):
        b.terrace_tier(profs[k], -0.3 if k == 0 else ST * k, ST * (k + 1), profs[k + 1] if k < 5 else None, 2.6)
    b.roof_solar(0, 10, 21.6, 26, 6)
    b.shrubs_scatter(-18, 7.5, 18, 8.5, 21.6, 6) if b.lod == 0 else None
    return dict(w=40, d=30, h=23.0, hbody=21.6, style='tall', build_frac=0.6)


def block_c(b):
    """Round drum tiers with a glass dome and a gold ring."""
    n = b.circ(36)
    rs = [16, 14, 12, 10]
    for k, r in enumerate(rs):
        nxt = circle(rs[k + 1], n) if k + 1 < len(rs) else None
        b.terrace_tier(circle(r, n), -0.3 if k == 0 else 7.2 * k, 7.2 * (k + 1), nxt, 2.8)
    b.add(prism(circle(8.6, n), 28.8, 29.6, GOLD, GOLD, GOLD))
    rz = [(8.0 * math.cos(math.radians(a)), 29.6 + 6.5 * math.sin(math.radians(a))) for a in range(0, 90, 10 if b.lod == 0 else 30)] + [(0.0, 36.1)]
    b.add(lathe(rz, n, GLASS))
    if b.lod <= 1:
        for k in range(8 if b.lod == 0 else 4):
            a = 2 * math.pi * k / (8 if b.lod == 0 else 4)
            b.add(beam((8.1 * math.cos(a), 8.1 * math.sin(a), 29.6), (0.2 * math.cos(a), 0.2 * math.sin(a), 36.1), 0.25, 0.25, WHITE))
    return dict(w=32, d=32, h=36.1, hbody=28.8, style='tall', build_frac=0.5)


def block_d(b):
    """Courtyard block: four planted bars around a garden court."""
    sg = b.seg(3)
    bars = [(_shift(rrect(48, 10, 3, sg), 0, 15), 18.0), (_shift(rrect(48, 10, 3, sg), 0, -15), 18.0),
            (_shift(rrect(10, 20, 2, sg), 19, 0), 14.4), (_shift(rrect(10, 20, 2, sg), -19, 0), 14.4)]
    pent = _shift(rrect(34, 6, 2, sg), 0, 16)
    for i, (pf, h) in enumerate(bars):
        b.add(prism(pf, -0.3, h, FAC, ROOF, PANEL))
        b.planter_ring(pf, h, pent if i == 0 else None, width=1.5, tree_spacing=10)
    b.add(prism(pent, 18.0, 21.6, FAC, ROOF, PANEL))
    b.planter_ring(pent, 21.6, trees=False)
    b.roof_solar(0, -15, 18.0, 30, 6)
    b.add(box(0, 0, -0.3, 28.4, 20.4, 0.5, ROOF, ROOF, kind='ground'))
    if b.lod <= 1:
        b.shrubs_scatter(-12, -8, 12, 8, 0.2, 12, 1.6, 3.0)
    return dict(w=48, d=40, h=22.6, hbody=21.6, style='tall', build_frac=0.6)


# =========================================================================== maglev

TRACK_Z = 10.0          # guideway top
TRACK_LAT = 3.2         # each guideway's offset from the axis


def _guideway_profile(c):
    return [(c - 1.2, 8.6), (c + 1.2, 8.6), (c + 1.3, 9.6), (c + 1.3, 10.0), (c - 1.3, 10.0), (c - 1.3, 9.6)]


def _guideways(b, path, skin, kind='body', tag=''):
    for c in (-TRACK_LAT, TRACK_LAT):
        b.add(sweep(path, _guideway_profile(c), skin, kind=kind, tag=tag))
        if b.lod <= 1:
            for e in (-0.9, 0.9):
                prof = [(c + e - 0.12, 10.0), (c + e + 0.12, 10.0), (c + e + 0.12, 10.18), (c + e - 0.12, 10.18)]
                b.add(sweep(path, prof, STEEL, kind='detail'))


def _pylon(b, x, y, a, skin):
    sg = b.seg(2)
    col = prism(rrect(2.0, 1.5, 0.5, sg), -0.3, 7.6, skin, profile_top=rrect(1.6, 1.3, 0.4, sg), tag='base')
    head = prism(rrect(2.0, 9.4, 0.6, sg), 7.6, 8.6, skin, profile_top=rrect(2.0, 10.0, 0.6, sg), tag='base')
    for p in (col, head):
        p.rotz(a).move(x, y, 0)
        b.add(p)


def track_straight(b):
    nseg = 2
    path = [Vector((-20 + 40 * k / (nseg - 1), 0, 0)) for k in range(nseg)]
    _guideways(b, path, CONC)
    _pylon(b, 0, 0, 0.0, CONC)
    return dict(w=40, d=9.2, h=10.2, hbody=10.0, style='flat', ruin_h=4.0,
                sockets={'start': (-20, 0, 10.0, 180.0), 'end': (20, 0, 10.0, 0.0)})


def track_curve(b):
    R = 160.0
    th = 40.0 / R
    nseg = {0: 9, 1: 5, 2: 3, 3: 3}[b.lod]
    path = []
    for k in range(nseg):
        t = -th / 2 + th * k / (nseg - 1)
        path.append(Vector((R * math.sin(t), R - R * math.cos(t), 0)))
    _guideways(b, path, CONC)
    _pylon(b, 0, 0, 0.0, CONC)
    e = (R * math.sin(th / 2), R - R * math.cos(th / 2))
    return dict(w=40, d=10.5, h=10.2, hbody=10.0, style='flat', ruin_h=4.0,
                sockets={'start': (-e[0], e[1], 10.0, 180.0 - math.degrees(th / 2)), 'end': (e[0], e[1], 10.0, math.degrees(th / 2))})


def station(b):
    """Elevated maglev station: two guideways between platforms under a white-ribbed glass vault,
    with planted entrance halls on both sides."""
    sg = b.seg(4)
    path = [Vector((-45, 0, 0)), Vector((45, 0, 0))]
    _guideways(b, path, PANEL)
    for y in (-9.0, 9.0):
        b.add(box(0, y, 8.8, 90, 6.4, 1.2, PANEL, PANEL))
    for x in (-36, -12, 12, 36):
        _pylon(b, x, 0, 0.0, PANEL)
        b.add(box(x, 0, 7.6, 2.4, 24.6, 1.2, PANEL, kind='body'))
    # vault: glass shell + white ribs
    na = {0: 14, 1: 8, 2: 6, 3: 4}[b.lod]
    a_, b_, t_ = 12.6, 8.0, 0.3

    def band(a, bb, t):
        out = [(a * math.cos(math.pi * k / na), bb * math.sin(math.pi * k / na)) for k in range(na + 1)]
        inn = [((a - t) * math.cos(math.pi * k / na), (bb - t) * math.sin(math.pi * k / na)) for k in range(na, -1, -1)]
        return out + inn
    M = Matrix(((0, 0, 1, 0), (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    shell = prism(band(a_, b_, t_), -42, 42, GLASS, GLASS, GLASS, kind='body', smooth=True)
    shell.transform(M).move(0, 0, 10.0)
    b.add(shell)
    nrib = {0: 13, 1: 7, 2: 3, 3: 0}[b.lod]
    for k in range(nrib):
        x = -42 + 84 * k / max(1, nrib - 1)
        rib = prism(band(a_ + 0.25, b_ + 0.25, 0.7), x - 0.35, x + 0.35, WHITE, WHITE, WHITE, kind='detail')
        rib.transform(M).move(0, 0, 10.0)
        b.add(rib)
    for x in (-42.3, 42.3):
        b.add(box(x, 0, 10.0, 0.6, 25.6, 1.0, WHITE, kind='detail'))
    # entrance halls
    for sy in (-1, 1):
        hall = _shift(rrect(40, 14, 4, sg), 0, sy * 20)
        b.add(prism(hall, -0.3, 7.2, FAC, ROOF, PANEL))
        b.planter_ring(hall, 7.2, tree_spacing=8)
        for x in (-10, 10):
            b.add(box(x, sy * 12.6, -0.3, 5, 4, 10.3, PANEL, PANEL))
    return dict(w=90, d=54, h=18.3, hbody=18.0, style='tall', build_frac=0.6,
                sockets={'start': (-45, 0, 10.0, 180.0), 'end': (45, 0, 10.0, 0.0)})


# =========================================================================== solar


def solar_a(b):
    """Ground-mounted array: six rows of tilted tables facing -Y."""
    rows = 6
    if b.lod == 3:
        b.solar_panel(0, 0, 1.3, 44, 28, 0.0, kind='detail', legs=False)
        return dict(w=46, d=30, h=2.6, hbody=2.6, style='flat', ruin_h=0.6)
    for r in range(rows):
        y = -12.5 + 5 * r
        if b.lod == 2:
            b.solar_panel(0, y, 1.5, 44, 3.6, math.radians(25), legs=False)
            continue
        for k in range(11):
            x = -20 + 4 * k
            b.solar_panel(x, y, 1.5, 3.9, 3.6, math.radians(25), legs=(b.lod == 0))
        if b.lod <= 1:
            for k in range(12 if b.lod == 0 else 4):
                x = -22 + 44 * k / (11 if b.lod == 0 else 3)
                b.add(box(x, y, -0.3, 0.6, 0.6, 0.5, CONC, tag='base'))
    return dict(w=46, d=30, h=2.6, hbody=2.6, style='flat', ruin_h=0.6)


def solar_b(b):
    """Agrivoltaic canopy: high, flatter rows over planted crop strips."""
    if b.lod == 3:
        b.solar_panel(0, 0, 4.5, 44, 28, 0.0, kind='detail', legs=False)
        return dict(w=46, d=30, h=5.0, hbody=5.0, style='flat', ruin_h=1.0)
    for r in range(4):
        y = -11.25 + 7.5 * r
        b.add(box(0, y + 3.2, -0.3, 44, 2.6, 0.55, CONC_ROOFSLOT, CONC_ROOFSLOT, kind='ground'))
        if b.lod == 2:
            b.solar_panel(0, y, 4.5, 44, 4.4, math.radians(10), legs=False)
            continue
        for k in range(8):
            x = -19.25 + 5.5 * k
            b.solar_panel(x, y, 4.5, 5.4, 4.4, math.radians(10), legs=False)
        for k in range(9 if b.lod == 0 else 3):
            x = -22 + 44 * k / (8 if b.lod == 0 else 2)
            b.add(beam((x, y, -0.3), (x, y, 4.4), 0.25, 0.25, STEEL, kind='body', tag='base'))
    return dict(w=46, d=30, h=5.0, hbody=5.0, style='flat', ruin_h=1.0)


# =========================================================================== launch site

MOUNT_Z = 9.0


def _wedge(x0, x1, y0, y1, z0, z1, skin, kind='body', tag='', slope_to=-1):
    """Ramp: full height z1 at y1 (slope_to=-1 means it falls toward y0)."""
    p = K.Part(kind, tag)
    p.smooth = False
    bm = p.bm
    v = [bm.verts.new(c) for c in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1))]
    T = skin.tile if skin.mode == 'tile' else 1.0
    p.face([v[3], v[2], v[1], v[0]], skin.mat, [skin.top(c.co.x, c.co.y) for c in (v[3], v[2], v[1], v[0])])
    p.face([v[0], v[1], v[5], v[4]], skin.mat, [skin.top(c.co.x, c.co.y + c.co.z) for c in (v[0], v[1], v[5], v[4])])
    p.face([v[2], v[3], v[4], v[5]], skin.mat, [skin.side(c.co.x, c.co.z, 0, 0) for c in (v[2], v[3], v[4], v[5])])
    p.face([v[1], v[2], v[5]], skin.mat, [skin.side(c.co.y, c.co.z, 0, 0) for c in (v[1], v[2], v[5])])
    p.face([v[3], v[0], v[4]], skin.mat, [skin.side(c.co.y, c.co.z, 0, 0) for c in (v[3], v[0], v[4])])
    import bmesh as _bm
    _bm.ops.recalc_face_normals(bm, faces=bm.faces)
    return p


def gslab(x0, y0, x1, y1, z0, z1, nx, ny, skin, kind='ground'):
    """Slab with a subdivided top (so vertex scorch has vertices to land on)."""
    p = K.Part(kind)
    p.smooth = False
    bm = p.bm
    g = [[bm.verts.new((x0 + (x1 - x0) * i / nx, y0 + (y1 - y0) * j / ny, z1)) for i in range(nx + 1)] for j in range(ny + 1)]
    for j in range(ny):
        for i in range(nx):
            vs = [g[j][i], g[j][i + 1], g[j + 1][i + 1], g[j + 1][i]]
            p.face(vs, skin.mat, [skin.top(v.co.x, v.co.y) for v in vs])
    ring = [g[0][i] for i in range(nx + 1)] + [g[j][nx] for j in range(1, ny + 1)] + [g[ny][i] for i in range(nx - 1, -1, -1)] + [g[j][0] for j in range(ny - 1, 0, -1)]
    low = [bm.verts.new((v.co.x, v.co.y, z0)) for v in ring]
    n = len(ring)
    s = 0.0
    for i in range(n):
        j = (i + 1) % n
        L = (ring[j].co - ring[i].co).length
        p.face([low[i], low[j], ring[j], ring[i]], skin.mat, [skin.side(s, z0, 0, 0), skin.side(s + L, z0, 0, 0), skin.side(s + L, z1, 0, 0), skin.side(s, z1, 0, 0)])
        s += L
    p.face(list(reversed(low)), skin.mat, [skin.top(v.co.x, v.co.y) for v in reversed(low)])
    import bmesh as _bm
    _bm.ops.recalc_face_normals(bm, faces=bm.faces)
    return p


def _gantry(b, cx, cy, z0, H, w=10.0):
    lod = b.lod
    hw = w * 0.5
    legs = [(cx - hw, cy - hw), (cx + hw, cy - hw), (cx + hw, cy + hw), (cx - hw, cy + hw)]
    if lod == 3:
        b.add(box(cx, cy, z0, w, w, H, STEEL, STEEL, kind='body'))
        return
    for x, y in legs:
        b.add(box(x, y, z0, 0.9, 0.9, H, STEEL, STEEL, kind='body'))
    lev = {0: 4.6, 1: 9.2, 2: 18.4}[lod]
    nl = int(H / lev)
    for k in range(1, nl + 1):
        z = z0 + k * lev
        zp = z - lev
        for i in range(4):
            a, c = legs[i], legs[(i + 1) % 4]
            b.add(beam((a[0], a[1], z), (c[0], c[1], z), 0.45, 0.45, STEEL))
            if lod == 0:
                b.add(beam((a[0], a[1], zp), (c[0], c[1], z), 0.3, 0.3, STEEL))
                b.add(beam((c[0], c[1], zp), (a[0], a[1], z), 0.3, 0.3, STEEL))
            elif (k + i) % 2 == 0 or lod == 2:
                b.add(beam((a[0], a[1], zp), (c[0], c[1], z), 0.35, 0.35, STEEL))
    if lod <= 1:
        for k in range(0, nl, 2 if lod == 0 else 4):     # stair/elevator core inside the lattice
            pass
        # body, so damage cuts it with the legs (as 'detail' it survived whole and stood as a black monolith
        # above the cut lattice in the Damaged state; verification 2026-11-19)
        b.add(box(cx, cy, z0, 3.0, 3.0, H - 2.0, STEEL, STEEL, kind='body'))


def launch_pad(b):
    """Launch pad: concrete apron, raised hardstand with flame trench, launch mount, lattice gantry with
    service arms and a hammerhead crane, fuel spheres, tanks, pipe runs, light masts, service buildings.
    The rocket is a separate mesh standing on the mount socket."""
    lod = b.lod
    nseg = {0: 14, 1: 7, 2: 3, 3: 1}[lod]
    apron = gslab(-70, -62, 70, 62, -0.3, 0.4, nseg, nseg, CONC)
    apron.scorch = [(0, 44, 20, 0.55), (0, 28, 10, 0.35), (0, 0, 12, 0.3), (-40, 24, 16, 0.08), (30, -30, 22, 0.08), (40, 40, 18, 0.1)]
    b.add(apron)
    top = 6.5
    # hardstand (two halves and a front block around the flame trench, which runs toward +Y)
    for x0, x1 in ((-26, -5), (5, 26)):
        bot = [(x0, -26), (x1, -26), (x1, 26), (x0, 26)]
        sx0 = x0 + (4 if x0 < 0 else 0)
        sx1 = x1 - (4 if x1 > 0 else 0)
        tp = [(sx0, -22), (sx1, -22), (sx1, 22), (sx0, 22)]
        b.add(prism(bot, 0.0, top, CONC, CONC, CONC, profile_top=tp, smooth=False))
    b.add(prism([(-5, -26), (5, -26), (5, -6), (-5, -6)], 0.0, top, CONC, CONC, CONC,
                profile_top=[(-5, -22), (5, -22), (5, -6), (-5, -6)], smooth=False))
    b.add(_wedge(-11, 11, -58, -26, 0.0, top, CONC))
    # trench walls beyond the hardstand, deflector under the mount
    for x in (-6, 6):
        b.add(box(x, 40, 0.0, 2.0, 30, 3.0, CONC, CONC))
    b.add(_wedge(-5, 5, -6, 8, 0.4, 5.6, BLACK))
    # launch mount table around the exhaust hole
    for (cx, cy, w, d) in ((0, -6.5, 16, 3), (0, 6.5, 16, 3), (-6.5, 0, 3, 10), (6.5, 0, 3, 10)):
        b.add(box(cx, cy, top, w, d, MOUNT_Z - top, STEEL, STEEL))
    if lod <= 1:
        for x, y in ((-6.5, -6.5), (6.5, -6.5), (-6.5, 6.5), (6.5, 6.5)):
            b.add(box(x, y, 0.4, 2.0, 2.0, top, STEEL, STEEL, kind='detail'))
    # gantry
    gx, H = 16.0, 92.0
    _gantry(b, gx, 0.0, top, H)
    if lod <= 2:
        for z in (38.0, 60.0, 82.0) if lod <= 1 else (60.0,):
            b.add(box(7.5, 0, z, 7.0, 2.4, 2.6, WHITE, WHITE, kind='detail'))
        jz = top + H
        b.add(beam((gx - 6, 0, jz + 2.0), (gx + 14, 0, jz + 2.0), 1.4, 1.6, WHITE, caps=True))
        b.add(box(gx, 0, jz, 4, 4, 3.5, WHITE, WHITE, kind='detail'))
        b.add(beam((gx, 0, jz + 3.5), (gx, 0, jz + 16.0), 0.35, 0.35, STEEL))
        if lod <= 1:
            b.add(box(gx, 0, jz + 16.0, 0.5, 0.5, 0.5, LAMP, kind='detail'))
    # fuel spheres, tanks, pipes
    ns, nr = b.circ(28), {0: 14, 1: 8, 2: 6, 3: 4}[lod]
    for (x, y) in ((-44, 14), (-44, 34)):
        b.add(sphere(x, y, 9.2, 7.5, ns, nr, WHITE))
        b.add(prism(circle(4.4, b.circ(16)), 0.0, 3.0, STEEL, STEEL, STEEL).move(x, y, 0))
    if lod <= 2:
        for k in range(3):
            t = lathe([(2.2, 0.0), (2.2, 16.0)], b.circ(16), WHITE)
            t.transform(Matrix.Rotation(math.pi / 2, 4, 'Y')).move(-52, -14 - 6 * k, 3.2)
            b.add(t)
            if lod <= 1:
                for xx in (-46, -58):
                    b.add(box(xx, -14 - 6 * k, 0.0, 1.0, 3.0, 2.0, STEEL, kind='detail'))
    if lod <= 1:
        for y in (14, 34):
            b.add(beam((-36.5, y, 1.5), (-26, 4, 1.5), 0.8, 0.8, STEEL))
        b.add(beam((-36, -26, 1.2), (-26, -14, 1.2), 0.7, 0.7, STEEL))
        b.add(beam((-26, 4, 1.5), (-26, -14, 1.2), 0.8, 0.8, STEEL))
    # service buildings and light masts
    b.add(box(44, -36, -0.3, 18, 11, 6.0, CONC, CONC))
    b.add(box(48, 30, -0.3, 12, 16, 8.0, CONC, CONC))
    if lod <= 1:
        b.add(box(44, -36.6, 2.4, 18.1, 11.0, 1.0, BLACK, BLACK, kind='detail'))
        for (x, y) in ((44, -36), (48, 30)):
            b.add(box(x - 3, y, 6.0 if y < 0 else 8.0, 3, 2, 1.4, WHITE, WHITE, kind='detail'))
        for (x, y) in ((-62, -54), (62, -54), (-62, 54), (62, 54)):
            b.add(beam((x, y, -0.3), (x, y, 32.0), 0.6, 0.6, STEEL))
            b.add(box(x, y, 32.0, 2.4, 1.0, 1.6, WHITE, WHITE, kind='detail'))
    if lod <= 2:
        for xx in (-4.5, 4.5):
            b.add(box(xx, -60, 0.4, 2.0, 4.0, 0.15, BLACK, BLACK, kind='ground'))
        # rail sidings and service roads across the apron
        for y in (-44.0, -46.6):
            for x0, x1 in ((-70, -12), (12, 70)):
                b.add(box((x0 + x1) / 2, y, 0.4, x1 - x0, 0.35, 0.12, STEEL, STEEL, kind='ground'))
        for x in (34.0, 36.6):
            b.add(box(x, 8, 0.4, 0.35, 108, 0.12, STEEL, STEEL, kind='ground'))
    # water tower (deluge water) - a tall silhouette beside the pad
    wx, wy = 56.0, 46.0
    if lod <= 2:
        for dx, dy in ((-3, -3), (3, -3), (3, 3), (-3, 3)):
            b.add(beam((wx + dx * 1.4, wy + dy * 1.4, -0.3), (wx + dx * 0.8, wy + dy * 0.8, 38.0), 0.6, 0.6, STEEL, kind='body'))
    b.add(prism(circle(5.5, b.circ(18)), 38.0, 47.0, WHITE, WHITE, WHITE).move(wx, wy, 0))
    if lod <= 1:
        b.add(lathe([(5.5, 47.0), (3.0, 49.0), (0.0, 49.6)], b.circ(18), WHITE, kind='detail').move(wx, wy, 0))
    # service buildings along the east side, small tanks by the spheres
    for (x, y, w, d, h) in ((60, -10, 14, 22, 7.0), (60, 14, 10, 12, 5.0), (-60, -46, 16, 10, 6.0), (20, -52, 12, 8, 4.5)):
        b.add(box(x, y, -0.3, w, d, h + 0.3, CONC, CONC))
        if lod <= 1:
            b.add(box(x, y, h, w * 0.5, d * 0.4, 1.2, WHITE, WHITE, kind='detail'))
    if lod <= 1:
        for (x, y) in ((-30, 30), (-30, 38), (-56, 2), (-56, 8)):
            b.add(prism(circle(1.8, 12), 0.4, 9.0, WHITE, WHITE, WHITE).move(x, y, 0))
    return dict(w=140, d=124, h=114.5, hbody=top + H, style='tall', build_frac=0.5,
                rubble_c=(gx, 0.0), rubble_r=14.0,
                sockets={'rocket_mount': (0, 0, MOUNT_Z), 'crawlerway': (0, -62, 0.4)})


def rocket(b):
    """The launcher (82 m), base at Z = 0, standing on the pad's rocket_mount socket."""
    n = {0: 32, 1: 16, 2: 10, 3: 6}[b.lod]
    core = [(3.3, 1.8), (3.6, 3.0), (3.6, 46.0), (3.6, 47.6), (3.6, 52.0), (3.6, 64.0), (3.6, 65.0),
            (3.5, 67.5), (3.2, 70.5), (2.7, 73.5), (2.0, 76.5), (1.2, 79.2), (0.45, 81.2), (0.0, 82.0)]
    sk = [BLACK, HULL, HULL, BLACK, HULL, GOLD, HULL, HULL, HULL, HULL, HULL, HULL, HULL]
    if b.lod >= 2:
        core = [(3.3, 1.8), (3.6, 3.0), (3.6, 47.6), (3.6, 52.0), (3.6, 65.0), (3.2, 70.5), (2.0, 76.5), (0.6, 81.0), (0.0, 82.0)]
        sk = [BLACK, HULL, BLACK, HULL, HULL, HULL, HULL, HULL]
    b.add(lathe(core, n, HULL, skins=sk, r_ref=3.6))
    nb = max(6, n // 2)
    for y in (-5.6, 5.6):
        bo = [(1.4, 1.0), (1.7, 2.2), (1.7, 30.0), (1.5, 32.5), (1.0, 35.0), (0.4, 36.6), (0.0, 37.0)]
        bs = [BLACK, HULL, HULL, HULL, HULL, HULL]
        if b.lod >= 2:
            bo = [(1.4, 1.0), (1.7, 2.2), (1.7, 30.0), (0.9, 35.0), (0.0, 37.0)]
            bs = [BLACK, HULL, HULL, HULL]
        b.add(lathe(bo, nb, HULL, skins=bs, r_ref=1.7).move(0, y, 0))
        if b.lod <= 1:
            for z in (6.0, 27.0):
                b.add(box(0, y * 0.68, z, 0.6, 2.4, 0.6, BLACK, kind='detail'))
    if b.lod <= 1:
        for k in range(4):
            a = math.pi / 4 + k * math.pi / 2
            f = box(4.4, 0, 1.8, 2.0, 0.25, 4.0, WHITE, kind='detail')
            f.rotz(a)
            b.add(f)
        for x, y in ((0, 0), (1.6, 0), (-1.6, 0), (0, 1.6), (0, -1.6)):
            b.add(lathe([(1.0, 0.0), (0.5, 1.9)], 10 if b.lod == 0 else 6, BLACK, kind='detail').move(x, y, 0))
    return dict(w=7.2, d=14.6, h=82.0, hbody=82.0, style='none')


def assembly_hall(b):
    """Vehicle assembly hall: the biggest box in the game, long and low as in the concepts ("a huge low
    assembly hall"; the rocket stands taller). Launchers are integrated lying down and rolled out to the
    pad, so three tall door bays sit on the long front (-Y). Hipped metal roof with a skylight ridge,
    pilaster ribs, an office/plant annex on each end and a low workshop lean-to along the back.
    (Re-proportioned in verification 2026-11-19: it was a 124 x 84 x 104 m near-cube that read as a tower block.)"""
    lod = b.lod
    W, D, H = 176.0, 92.0, 58.0
    b.add(box(0, 0, -0.3, W, D, H + 0.3, CLAD, RMETAL))
    b.add(box(0, 0, -0.3, W + 1.2, D + 1.2, 1.5, STEEL, STEEL))
    b.add(box(0, 0, H, W + 0.8, D + 0.8, 1.4, CLAD, RMETAL))
    roof = prism(rrect(W, D, 0, 0), H + 1.4, H + 6.4, RMETAL, RMETAL, RMETAL, profile_top=rrect(W - 30, 6, 0, 0), smooth=False)
    for f in roof.bm.faces:          # plan-project the shallow hip slopes (side mapping stretched the panels 8x)
        for l in f.loops:
            l[roof.uv].uv = RMETAL.top(l.vert.co.x, l.vert.co.y)
    b.add(roof)
    doors = (-54.0, 0.0, 54.0)
    if lod <= 1:
        step = 11.0 if lod == 0 else 22.0
        x = -W / 2 + step
        while x < W / 2 - 1:
            for y in (-D / 2 - 0.5, D / 2 + 0.5):
                if not (y < 0 and any(abs(x - xd) < 17.5 for xd in doors)):
                    b.add(box(x, y, -0.3, 1.4, 1.0, H, CLAD, CLAD, kind='body'))   # cut with the wall
            x += step
        for y in (-D / 2 + 15, -D / 2 + 46, D / 2 - 15):
            for xx in (-W / 2 - 0.5, W / 2 + 0.5):
                b.add(box(xx, y, -0.3, 1.0, 1.4, H, CLAD, CLAD, kind='body'))
    for xd in doors:
        b.add(box(xd, -D / 2 - 0.5, -0.3, 30, 1.2, 50.3, STEEL, STEEL))
        if lod <= 1:
            for z in range(8, 50, 7 if lod == 0 else 14):
                b.add(box(xd, -D / 2 - 1.15, z, 30, 0.3, 0.5, BLACK, kind='detail'))
            b.add(box(xd, -D / 2 - 0.9, 50.0, 33, 1.8, 2.4, WHITE, WHITE, kind='detail'))
            for sx in (-1, 1):
                b.add(box(xd + sx * 15.9, -D / 2 - 0.9, -0.3, 1.8, 1.8, 52.7, WHITE, WHITE, kind='body'))
    # end annexes and the workshop lean-to along the back
    b.add(box(W / 2 + 13, 6, -0.3, 26, 64, 24.3, CLAD, RMETAL))
    b.add(box(-W / 2 - 10, 12, -0.3, 20, 50, 14.3, CLAD, RMETAL))
    b.add(box(0, D / 2 + 6, -0.3, 120, 12, 10.3, CLAD, RMETAL))
    if lod <= 2:
        b.add(box(0, 0, H + 6.4, W - 34, 3.0, 0.6, GLASS, GLASS, kind='detail'))
        for y in (-D / 2 + 14, D / 2 - 14):
            b.add(box(0, y, H + 1.4 + 1.6, W - 40, 2.2, 0.5, GLASS, GLASS, kind='detail'))
    if lod <= 1:
        for k in range(9 if lod == 0 else 3):
            b.add(box(W / 2 + 6 + 7.5 * (k % 3), -16 + 10 * (k // 3), 24.0, 5, 4, 2.4, WHITE, WHITE, kind='detail'))
        for k in range(5):
            b.add(box(-48 + 24 * k, D / 2 + 6, 10.0, 6, 5, 2.2, WHITE, WHITE, kind='detail'))
        for (x, y) in ((-W / 2 - 10, -6), (-W / 2 - 10, 30)):
            b.add(prism(circle(2.4, 12), 14.0, 20.0, WHITE, WHITE, WHITE).move(x, y, 0))
    return dict(w=W + 46, d=D + 14, h=H + 7.0, hbody=H, style='tall', build_frac=0.55, autocenter=True,
                sockets={'doors': (0, -D / 2 - 1, 0)})


def control_bunker(b):
    """Earth-bermed control bunker with a concrete block, slit windows, antennas and a dish."""
    sg = b.seg(3)
    b.add(prism(rrect(48, 40, 5, sg), -0.3, 1.2, CONC, CONC, CONC))
    b.add(prism(rrect(46, 38, 4, sg), 1.2, 8.0, CONC_ROOFSLOT, CONC_ROOFSLOT, CONC, profile_top=rrect(30, 22, 3, sg)))
    b.add(box(0, 0, 8.0, 22, 13, 6.0, CONC, CONC))
    b.add(box(0, -0.05, 11.0, 22.3, 13.3, 1.0, BLACK, BLACK, kind='detail'))
    b.add(_wedge(-4, 4, -24, -8, 1.2, 8.0, CONC))
    if b.lod <= 2:
        b.add(beam((6, 3, 14.0), (6, 3, 30.0), 0.35, 0.35, STEEL))
        b.add(beam((-7, 4, 14.0), (-7, 4, 24.0), 0.3, 0.3, STEEL))
    if b.lod <= 1:
        b.add(box(6, 3, 30.0, 0.4, 0.4, 0.4, LAMP, kind='detail'))
        dish = lathe([(0.0, 0.0), (1.5, 0.25), (3.0, 1.0), (3.2, 1.25)], 16 if b.lod == 0 else 8, WHITE, kind='detail')
        dish.transform(Matrix.Rotation(math.radians(-55), 4, 'X')).move(-3, 2, 16.0)
        b.add(dish)
        b.add(box(-3, 2, 14.0, 1.0, 1.0, 2.2, WHITE, kind='detail'))
        for k in range(3):
            b.add(box(-6 + 6 * k, 4.5, 14.0, 2.6, 2.0, 1.2, WHITE, WHITE, kind='detail'))
    return dict(w=48, d=40, h=30.4, hbody=14.0, style='tall', build_frac=0.6)


def crawlerway(b):
    """Straight 60 m crawlerway segment: two concrete lanes and a dark gravel median."""
    nx = {0: 6, 1: 3, 2: 1, 3: 1}[b.lod]
    for y in (-10.5, 10.5):
        b.add(gslab(-30, y - 6, 30, y + 6, -0.3, 0.3, nx, 1, CONC))
    med = gslab(-30, -4.5, 30, 4.5, -0.3, 0.15, nx, 1, CONC)
    med.char = 0.55
    b.add(med)
    return dict(w=60, d=33, h=0.6, hbody=0.6, style='none', sockets={'start': (-30, 0, 0.3, 180.0), 'end': (30, 0, 0.3, 0.0)})


# =========================================================================== registry

STATES4 = ['Intact', 'Construction', 'Damaged', 'Ruin']

# name, builder, slot set, states, struct skin for cuts/rubble, budget class, sim mapping
ASSETS = [
    ('ArcologyTower_A', tower_a, SET_CITY, STATES4, PANEL, 'civic', 'Tower v0 (Age 6, tier 3+)'),
    ('ArcologyTower_B', tower_b, SET_CITY, STATES4, PANEL, 'civic', 'Tower v1'),
    ('ArcologyTower_C', tower_c, SET_CITY, STATES4, PANEL, 'civic', 'Tower v2 (round)'),
    ('ArcologyTower_D', tower_d, SET_CITY, STATES4, PANEL, 'civic', 'Tower v3'),
    ('ArcologyTower_E', tower_e, SET_CITY, STATES4, PANEL, 'civic', 'Tower v4 (hero, capital core)'),
    ('GreenBlock_A', block_a, SET_CITY, STATES4, PANEL, 'civic', 'Apartment v0 (Age 6)'),
    ('GreenBlock_B', block_b, SET_CITY, STATES4, PANEL, 'civic', 'Apartment v1 (Age 6, street front)'),
    ('GreenBlock_C', block_c, SET_CITY, STATES4, PANEL, 'civic', 'Apartment v2 (Age 6, round)'),
    ('GreenBlock_D', block_d, SET_CITY, STATES4, PANEL, 'civic', 'Apartment v3 (Age 6, courtyard)'),
    ('MaglevStation_A', station, SET_CITY, STATES4, PANEL, 'civic', 'transit station (Age 6)'),
    ('MaglevTrack_Straight', track_straight, ('M_Age6_Concrete', None, 'M_Age6_Tech'), STATES4, CONC, 'prop', 'elevated maglev guideway, 40 m'),
    ('MaglevTrack_Curve', track_curve, ('M_Age6_Concrete', None, 'M_Age6_Tech'), STATES4, CONC, 'prop', 'elevated maglev guideway, 40 m arc R160'),
    ('SolarArray_A', solar_a, ('M_Age6_Concrete', None, 'M_Age6_Tech'), STATES4, CONC, 'house', 'solar field module'),
    ('SolarArray_B', solar_b, SET_CONC, STATES4, CONC, 'house', 'agrivoltaic canopy module'),
    ('LaunchPad', launch_pad, ('M_Age6_Concrete', None, 'M_Age6_Tech'), STATES4, CONC, 'civic', 'b.launch: Gantry'),
    ('Rocket', rocket, SET_HULL, ['Intact'], HULL, 'vehicle', 'b.launch: Rocket (Z scaled by ark progress)'),
    ('AssemblyHall', assembly_hall, SET_HALL, STATES4, CLAD, 'civic', 'b.launch: assembly hall'),
    ('ControlBunker', control_bunker, SET_CONC, STATES4, CONC, 'house', 'b.launch: control bunker'),
    ('Crawlerway', crawlerway, ('M_Age6_Concrete', None, None), ['Intact'], CONC, 'prop', 'b.launch: crawler road, 60 m'),
]
