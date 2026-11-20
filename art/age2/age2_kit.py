# DEMIURGE art - Age2 geometry kit: a face-soup builder with LOD and damage-state awareness.
#
# Every asset is built by a function f(b) that calls the primitives below on a Builder `b`. The same
# function is called for LOD0..LOD3 and for each state (Intact, Construction, Damaged, Ruin); the
# primitives read b.lod / b.state and simplify, cut or burn themselves. Space: metres, Z up, front -Y.
# Material slots: 0 Structure, 1 Roof, 2 Detail (trim sheet).
import math
import random
import bmesh
import bpy
from mathutils import Vector, Matrix

import age2_tex as tx

S, R, D = 0, 1, 2
STATES = ('Intact', 'Construction', 'Damaged', 'Ruin')


def hash01(*a):
    h = 2166136261
    for x in a:
        h = ((h ^ (int(x) & 0xffffffff)) * 16777619) & 0xffffffff
    h ^= h >> 13
    h = (h * 1274126177) & 0xffffffff
    return (h & 0xffffff) / float(0x1000000)


def vnoise1(x, seed=0):
    i = math.floor(x)
    f = x - i
    f = f * f * (3 - 2 * f)
    return hash01(i, seed) * (1 - f) + hash01(i + 1, seed) * f


class Builder:
    def __init__(self, lod=0, state='Intact', seed=1, tiles=None):
        self.lod = lod
        self.state = state
        self.seed = seed
        self.rng = random.Random(seed * 7919 + lod * 13 + STATES.index(state) * 101)
        self.srng = random.Random(seed * 7919 + STATES.index(state) * 101)   # state rng, LOD independent
        self.tiles = tiles or {S: 4.0, R: 3.0}
        self.faces = []        # (pts, slot, uvs or None, shade)
        self.M = Matrix.Identity(4)
        self.stack = []
        self.burn = []         # (centre Vector, radius) scorch spots (asset space)
        self.grime_h = 1.6     # height of the darker base band
        self.eave_z = None     # walls darken just under this height (eave shadow / soot)
        self.shutters = True
        self.ridges = []       # (end0, end1) ridge lines in asset space: culture ornament attach points

    # ------------------------------------------------------------------ transform stack
    def push(self, m):
        self.stack.append(self.M)
        self.M = self.M @ m

    def pop(self):
        self.M = self.stack.pop()

    def at(self, x=0.0, y=0.0, z=0.0, yaw=0.0):
        return Matrix.Translation((x, y, z)) @ Matrix.Rotation(yaw, 4, 'Z')

    # ------------------------------------------------------------------ helpers
    def keep(self, ml):
        """Part with max LOD `ml` is emitted at this LOD (ml=3: also in the LOD3 hull)."""
        return self.lod <= ml

    def segs(self, n, lo=3):
        return max(lo, int(round(n / (1, 2, 3, 4)[self.lod])))

    @property
    def ruined(self):
        return self.state == 'Ruin'

    @property
    def building(self):
        return self.state == 'Construction'

    @property
    def damaged(self):
        return self.state == 'Damaged'

    # ------------------------------------------------------------------ faces
    def face(self, pts, slot, uvs=None, shade=1.0):
        P = [self.M @ Vector(p) for p in pts]
        self.faces.append((P, slot, uvs, shade))

    def quad(self, a, b, c, d, slot, uvs=None, shade=1.0):
        self.face([a, b, c, d], slot, uvs, shade)

    # trim-sheet mapping: u along `length` metres from u0, v spans the band across the face
    @staticmethod
    def band_uv(band, u0, u1, f0=0.0, f1=1.0, cell=None):
        v0, v1 = tx.BANDS[band]
        pad = 1.5 / 1024
        v0, v1 = v0 + pad, v1 - pad
        if cell is not None:   # window cell index: map 0..1 inside the cell
            cw = 1.0 / tx.WINDOW_CELLS
            ua, ub = cell * cw + 0.004, (cell + 1) * cw - 0.004
            return [(ua, v0), (ub, v0), (ub, v1), (ua, v1)]
        va = v0 + (v1 - v0) * f0
        vb = v0 + (v1 - v0) * f1
        return [(u0 / tx.TRIM_U, va), (u1 / tx.TRIM_U, va), (u1 / tx.TRIM_U, vb), (u0 / tx.TRIM_U, vb)]

    # ------------------------------------------------------------------ primitives
    def box(self, cx, cy, z0, sx, sy, sz, slot, yaw=0.0, band=None, bottom=False, ml=2, shade=1.0, top=True):
        if not self.keep(ml) or sx <= 0 or sy <= 0 or sz <= 0:
            return
        self.push(self.at(cx, cy, z0, yaw))
        x, y = sx / 2, sy / 2
        c = [Vector(p) for p in ((-x, -y, 0), (x, -y, 0), (x, y, 0), (-x, y, 0), (-x, -y, sz), (x, -y, sz), (x, y, sz), (-x, y, sz))]
        fs = [((0, 1, 5, 4), sx, sz), ((1, 2, 6, 5), sy, sz), ((2, 3, 7, 6), sx, sz), ((3, 0, 4, 7), sy, sz)]
        if top:
            fs.append(((4, 5, 6, 7), sx, sy))
        if bottom:
            fs.append(((3, 2, 1, 0), sx, sy))
        for idx, w, hgt in fs:
            uvs = None
            if band:
                if w >= hgt:
                    uvs = self.band_uv(band, 0, w)
                else:   # rotate so u runs along the long edge
                    q = self.band_uv(band, 0, hgt)
                    uvs = [q[3], q[0], q[1], q[2]]
            self.face([c[i] for i in idx], slot, uvs, shade)
        self.pop()

    def beam(self, p0, p1, w, h, slot=D, band='timber', ml=1, up=(0, 0, 1), shade=1.0, ends=True):
        """Square-section member from p0 to p1 (any direction)."""
        if not self.keep(ml):
            return
        p0, p1 = Vector(p0), Vector(p1)
        a = p1 - p0
        Lr = a.length
        if Lr < 1e-4:
            return
        a.normalize()
        upv = Vector(up)
        if abs(a.dot(upv)) > 0.95:
            upv = Vector((1, 0, 0))
        sd = a.cross(upv).normalized()
        u2 = sd.cross(a).normalized()
        hw, hh = w / 2, h / 2
        ring = [(-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh)]
        q0 = [p0 + sd * x + u2 * y for x, y in ring]
        q1 = [p1 + sd * x + u2 * y for x, y in ring]
        for i in range(4):
            j = (i + 1) % 4
            uvs = self.band_uv(band, 0, Lr) if band else None
            self.face([q0[i], q1[i], q1[j], q0[j]], slot, uvs, shade)
        if ends:
            self.face([q1[3], q1[2], q1[1], q1[0]], slot, None if not band else self.band_uv(band, 0, w), shade)
            self.face([q0[0], q0[1], q0[2], q0[3]], slot, None if not band else self.band_uv(band, 0, w), shade)

    def cyl(self, cx, cy, z0, z1, r0, r1, sides, slot, band=None, top=True, ml=2, shade=1.0, tile=None, a0=0.0):
        if not self.keep(ml):
            return
        n = sides
        tile = tile or self.tiles.get(slot, 2.0)
        ring0 = [Vector((cx + r0 * math.cos(a0 + 2 * math.pi * i / n), cy + r0 * math.sin(a0 + 2 * math.pi * i / n), z0)) for i in range(n)]
        ring1 = [Vector((cx + r1 * math.cos(a0 + 2 * math.pi * i / n), cy + r1 * math.sin(a0 + 2 * math.pi * i / n), z1)) for i in range(n)]
        circ = 2 * math.pi * r0
        for i in range(n):
            j = (i + 1) % n
            if band:
                uvs = self.band_uv(band, circ * i / n, circ * (i + 1) / n)
            else:
                ua, ub = circ * i / n / tile, circ * (i + 1) / n / tile
                uvs = [(ua, z0 / tile), (ub, z0 / tile), (ub, z1 / tile), (ua, z1 / tile)]
            self.face([ring0[i], ring0[j], ring1[j], ring1[i]], slot, uvs, shade)
        if top and r1 > 0.01:
            if band:   # keep the cap inside its trim band (world-planar UVs wandered over other bands,
                       # including the night-window cells; verification fix)
                v0, v1 = tx.BANDS[band]
                pad = 1.5 / 1024
                v0, v1 = v0 + pad, v1 - pad
                cuv = [((r1 * math.cos(a0 + 2 * math.pi * i / n) + r1) / tx.TRIM_U,
                        v0 + (v1 - v0) * (0.5 + 0.5 * math.sin(a0 + 2 * math.pi * i / n))) for i in range(n)]
                self.face(ring1, slot, cuv, shade)
            else:
                self.face(ring1, slot, None, shade)

    def cone(self, cx, cy, z0, z1, r, sides, slot, ml=2, shade=1.0, band=None):
        if not self.keep(ml):
            return
        n = sides
        apex = Vector((cx, cy, z1))
        sl = math.hypot(r, z1 - z0)
        circ = 2 * math.pi * r
        tile = self.tiles.get(slot, 2.0)
        for i in range(n):
            j = (i + 1) % n
            a = Vector((cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n), z0))
            bb = Vector((cx + r * math.cos(2 * math.pi * j / n), cy + r * math.sin(2 * math.pi * j / n), z0))
            if band:
                q = self.band_uv(band, circ * i / n, circ * (i + 1) / n)
                uvs = [q[0], q[1], ((q[1][0] + q[0][0]) / 2, q[2][1])]
            else:
                uvs = [(circ * i / n / tile, 0), (circ * (i + 1) / n / tile, 0), (circ * (i + 0.5) / n / tile, sl / tile)]
            self.face([a, bb, apex], slot, uvs, shade)

    def dome(self, cx, cy, z0, r, rings, sides, slot, band=None, ml=2, squash=1.0):
        if not self.keep(ml):
            return
        circ = 2 * math.pi * r
        pts = []
        for k in range(rings + 1):
            t = (math.pi / 2) * k / rings
            pts.append((r * math.cos(t), z0 + r * squash * math.sin(t)))
        for k in range(rings):
            ra, za = pts[k]
            rb, zb = pts[k + 1]
            for i in range(sides):
                a0 = 2 * math.pi * i / sides
                a1 = 2 * math.pi * (i + 1) / sides
                p = [Vector((cx + ra * math.cos(a0), cy + ra * math.sin(a0), za)), Vector((cx + ra * math.cos(a1), cy + ra * math.sin(a1), za)),
                     Vector((cx + rb * math.cos(a1), cy + rb * math.sin(a1), zb)), Vector((cx + rb * math.cos(a0), cy + rb * math.sin(a0), zb))]
                if band:
                    q = self.band_uv(band, circ * i / sides, circ * (i + 1) / sides, k / rings, (k + 1) / rings)
                else:
                    q = None
                if rb < 1e-4:
                    self.face(p[:3], slot, q[:3] if q else None)
                else:
                    self.face(p, slot, q)

    def tube(self, pts, r, sides, slot, band=None, ml=0, half=False, shade=1.0, caps=False):
        """Round (or half-round, upper half) tube along a polyline (thatch rolls, ridges)."""
        if not self.keep(ml):
            return
        pts = [Vector(p) for p in pts]
        rings = []
        acc = [0.0]
        for i in range(1, len(pts)):
            acc.append(acc[-1] + (pts[i] - pts[i - 1]).length)
        sd_prev = None
        for i, p in enumerate(pts):
            a = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
            # parallel-transported frame: the old per-ring frame flipped where the tangent changed
            # side or turned near vertical, twisting the ornament horn and ring (verification fix)
            sd = None
            if sd_prev is not None:
                sd = sd_prev - a * sd_prev.dot(a)
                sd = sd.normalized() if sd.length > 1e-6 else None
            if sd is None:
                up = Vector((0, 0, 1))
                if abs(a.dot(up)) > 0.95:
                    up = Vector((1, 0, 0))
                sd = a.cross(up).normalized()
            sd_prev = sd
            u2 = sd.cross(a).normalized()
            ring = []
            for k in range(sides + (1 if half else 0)):
                t = (math.pi * k / sides) if half else (2 * math.pi * k / sides)
                ring.append(p + sd * (r * math.cos(t)) + u2 * (r * math.sin(t)))
            rings.append(ring)
        m = len(rings[0])
        tile = self.tiles.get(slot, 2.0)
        for i in range(len(rings) - 1):
            for k in range(m - (1 if half else 0)):
                j = (k + 1) % m
                if band:
                    uvs = self.band_uv(band, acc[i], acc[i + 1])
                else:
                    ca, cb = k * r * 2 * math.pi / sides / tile, (k + 1) * r * 2 * math.pi / sides / tile
                    uvs = [(acc[i] / tile, ca), (acc[i + 1] / tile, ca), (acc[i + 1] / tile, cb), (acc[i] / tile, cb)]
                self.face([rings[i][k], rings[i + 1][k], rings[i + 1][j], rings[i][j]], slot, uvs, shade)
        if caps and not half:
            cuv = self.band_uv(band, 0, 2 * r) if band else None
            # ring order winds about -tangent: the start cap uses it as is, the end cap reversed
            self.face(list(rings[0]), slot, cuv and [cuv[0], cuv[1], cuv[2]] + [cuv[3]] * (m - 3), shade)
            self.face(list(reversed(rings[-1])), slot, cuv and [cuv[0], cuv[1], cuv[2]] + [cuv[3]] * (m - 3), shade)

    def lump(self, cx, cy, z0, rx, ry, h, slot=S, sides=7, ml=1, seed=0):
        """Irregular low mound (rubble heaps, spoil)."""
        if not self.keep(ml):
            return
        rings = 2 if self.lod <= 1 else 1
        sides = sides if self.lod == 0 else max(5, sides - 2)
        prof = [(1.0, 0.0), (0.62, 0.62), (0.0, 1.0)] if rings == 2 else [(1.0, 0.0), (0.0, 1.0)]
        rr = [[0.75 + 0.5 * hash01(seed, k, i) for i in range(sides)] for k in range(len(prof))]
        pts = []
        for k, (fr, fz) in enumerate(prof):
            ring = []
            for i in range(sides):
                a = 2 * math.pi * (i + 0.3 * hash01(seed, 9, i)) / sides
                ring.append(Vector((cx + rx * fr * rr[k][i] * math.cos(a), cy + ry * fr * rr[k][i] * math.sin(a), z0 + h * fz * (0.85 + 0.3 * hash01(seed, k, i, 5)) if k < len(prof) - 1 else z0 + h)))
            pts.append(ring)
        for k in range(len(prof) - 1):
            for i in range(sides):
                j = (i + 1) % sides
                if k == len(prof) - 2:
                    self.face([pts[k][i], pts[k][j], pts[k + 1][0]], slot)
                else:
                    self.face([pts[k][i], pts[k][j], pts[k + 1][j], pts[k + 1][i]], slot)

    def rubble(self, cx, cy, z0, rad, h, n=8, seed=0, ml=1, blocks=True):
        """Heap of fallen stone: a mound plus loose blocks."""
        self.lump(cx, cy, z0 - 0.1, rad, rad * 0.8, h, S, 8, ml, seed)
        if blocks and self.keep(min(ml, 1)):
            for k in range(n if self.lod == 0 else n // 2):
                a = 2 * math.pi * hash01(seed, k, 1)
                d = rad * (0.6 + 0.6 * hash01(seed, k, 2))
                s = 0.35 + 0.4 * hash01(seed, k, 3)
                self.box(cx + d * math.cos(a), cy + d * math.sin(a), z0 - 0.1, s * 1.4, s, s * 0.8, S, yaw=a * 1.7, ml=1)

    # ------------------------------------------------------------------ walls with openings / states
    def top_profile(self, x, L, z0, z1, key):
        """Top height of a wall column at distance x along a wall, by state."""
        st = self.state
        if st == 'Intact':
            return z1
        H = z1 - z0
        if st == 'Construction':
            base = 0.5 + 0.25 * vnoise1(x / 5.0 + key * 3.1, 7)
            return z0 + round(H * base / 0.35) * 0.35
        if st == 'Damaged':
            dip = 0.0
            for k in range(2):
                c = hash01(key, k, 3) * L
                w = 1.2 + 2.5 * hash01(key, k, 4)
                if hash01(key, k, 5) < 0.55:
                    t = max(0.0, 1.0 - abs(x - c) / w)
                    dip = max(dip, t * (0.15 + 0.3 * hash01(key, k, 6)))
            return z1 - round(H * dip / 0.3) * 0.3
        # Ruin
        v = 0.18 + 0.5 * vnoise1(x / 2.2 + key * 5.3, 11) ** 1.5
        return z0 + max(0.4, round(H * v / 0.3) * 0.3)

    def wall(self, p0, p1, z0, z1, t, slot=S, openings=(), ml=3, ends=True, top=True, key=0, flat=False,
             panes=True, outward_only=False, band=None):
        n0 = len(self.faces)
        self._wall(p0, p1, z0, z1, t, slot, openings, ml, ends, top, key, flat, panes, outward_only)
        if band:
            self._band_remap(n0, slot, band, p0, p1, z0, z1)

    def _band_remap(self, n0, slot, band, p0, p1, z0, z1):
        Mi = self.M.inverted()
        p0 = Vector((p0[0], p0[1], 0.0))
        a = Vector((p1[0], p1[1], 0.0)) - p0
        a.normalize()
        v0, v1 = tx.BANDS[band]
        pad = 1.5 / 1024
        v0, v1 = v0 + pad, v1 - pad
        side = Vector((a.y, -a.x, 0.0))
        for k in range(n0, len(self.faces)):
            pts, sl, uvs, shade = self.faces[k]
            if sl != slot:
                continue
            qs = [Mi @ p for p in pts]
            fn = (qs[1] - qs[0]).cross(qs[2] - qs[0])
            if fn.length < 1e-12 and len(qs) > 3:
                fn = (qs[2] - qs[0]).cross(qs[3] - qs[0])
            fn = fn.normalized() if fn.length > 1e-12 else Vector((0, 0, 1))
            uv = []
            for q in qs:
                x = (q - p0).dot(a)
                d = (q - p0).dot(side)
                f = min(1.0, max(0.0, (q.z - z0) / max(1e-3, z1 - z0)))
                if abs(fn.z) > 0.7:
                    # tops, sills and soffits: v across the wall thickness, not the (constant) height,
                    # so the face does not collapse to one texel row (verification fix)
                    f = min(1.0, max(0.0, 0.5 + d / 0.6))
                elif abs(fn.dot(a)) > 0.7:
                    # ends and reveals: u across the thickness
                    x = d
                uv.append((x / tx.TRIM_U, v0 + (v1 - v0) * f))
            self.faces[k] = (pts, sl, uv, shade)

    def _wall(self, p0, p1, z0, z1, t, slot, openings, ml, ends, top, key, flat, panes, outward_only):
        """Straight wall on the centreline p0->p1 (2D), outside to the right of the direction.
        openings: dicts {x0, x1, zb, zt, kind: window|door|slit|arch|open, cell}. x measured from p0."""
        if not self.keep(ml):
            return
        p0 = Vector((p0[0], p0[1], 0.0))
        p1 = Vector((p1[0], p1[1], 0.0))
        a = p1 - p0
        L = a.length
        if L < 1e-3:
            return
        a.normalize()
        nrm = Vector((a.y, -a.x, 0.0))
        lod = self.lod

        def P(x, z, side):
            return p0 + a * x + nrm * (side * t / 2) + Vector((0, 0, z))

        # LOD3: a single solid slab
        if lod >= 3:
            zt = z1 if flat else max(self.top_profile(x, L, z0, z1, key) for x in (0.25 * L, 0.5 * L, 0.75 * L))
            self.quad(P(0, z0, 1), P(L, z0, 1), P(L, zt, 1), P(0, zt, 1), slot)
            self.quad(P(L, z0, -1), P(0, z0, -1), P(0, zt, -1), P(L, zt, -1), slot)
            self.quad(P(0, zt, 1), P(L, zt, 1), P(L, zt, -1), P(0, zt, -1), slot)
            return

        ops = []
        for o in openings:
            k = o.get('kind', 'window')
            if lod >= 2 and k in ('window', 'door', 'slit'):
                continue
            if lod == 1 and k == 'slit':   # 0.24 m arrow slits are sub-pixel at LOD1 (verification fix)
                continue
            ops.append(o)
        # column breakpoints
        xs = {0.0, L}
        for o in ops:
            xs.add(max(0.0, min(L, o['x0'])))
            xs.add(max(0.0, min(L, o['x1'])))
        varying = (self.state != 'Intact') and not flat
        if varying:
            step = (0.8, 1.6, 3.2)[min(lod, 2)]
            n = max(1, int(round(L / step)))
            for i in range(1, n):
                xs.add(L * i / n)
        xs = sorted(xs)
        xs2 = [xs[0]]
        for x in xs[1:]:
            if x - xs2[-1] > 0.02:
                xs2.append(x)
        xs = xs2
        nc = len(xs) - 1
        tops = []
        for i in range(nc):
            xm = 0.5 * (xs[i] + xs[i + 1])
            tops.append(z1 if not varying else self.top_profile(xm, L, z0, z1, key))
        zs = {z0, z1}
        zs.update(tops)
        for o in ops:
            zs.add(o['zb'])
            zs.add(o['zt'])
        if z1 - z0 > 3.5 and lod <= 1:
            zs.add(z0 + self.grime_h)
        zs = sorted(z for z in zs if z0 - 1e-6 <= z <= z1 + 1e-6)
        nz = len(zs) - 1

        def solid(i, j):
            if i < 0 or i >= nc or j < 0 or j >= nz:
                return False
            zm = 0.5 * (zs[j] + zs[j + 1])
            if zm > tops[i]:
                return False
            xm = 0.5 * (xs[i] + xs[i + 1])
            for o in ops:
                if o['x0'] < xm < o['x1'] and o['zb'] < zm < o['zt']:
                    return False
            return True

        sol = [[solid(i, j) for j in range(nz)] for i in range(nc)]

        def S_(i, j):
            return 0 <= i < nc and 0 <= j < nz and sol[i][j]

        # faces of the slab: outer (+1) and inner (-1), merged along rows
        for j in range(nz):
            i = 0
            while i < nc:
                if not sol[i][j]:
                    i += 1
                    continue
                k = i
                while k + 1 < nc and sol[k + 1][j]:
                    k += 1
                xa, xb = xs[i], xs[k + 1]
                za, zb = zs[j], zs[j + 1]
                self.quad(P(xa, za, 1), P(xb, za, 1), P(xb, zb, 1), P(xa, zb, 1), slot)
                if not outward_only:
                    self.quad(P(xb, za, -1), P(xa, za, -1), P(xa, zb, -1), P(xb, zb, -1), slot)
                i = k + 1
        # vertical side faces (reveals, ends, ragged steps)
        for i in range(nc + 1):
            for j in range(nz):
                left = S_(i - 1, j)
                right = S_(i, j)
                if left == right:
                    continue
                if (i == 0 or i == nc) and not ends:
                    continue
                x = xs[i]
                za, zb = zs[j], zs[j + 1]
                if left:   # face points +a
                    self.quad(P(x, za, 1), P(x, za, -1), P(x, zb, -1), P(x, zb, 1), slot)
                else:
                    self.quad(P(x, za, -1), P(x, za, 1), P(x, zb, 1), P(x, zb, -1), slot)
        # horizontal faces (tops, lintel soffits), merged along x
        for j in range(1, nz + 1):
            i = 0
            while i < nc:
                below = S_(i, j - 1)
                above = S_(i, j)
                if below == above or (above and j == 0):
                    i += 1
                    continue
                kind = below
                k = i
                while k + 1 < nc and S_(k + 1, j - 1) == below and S_(k + 1, j) == above:
                    k += 1
                xa, xb = xs[i], xs[k + 1]
                z = zs[j]
                if kind and (top or j < nz):     # solid below: top face
                    self.quad(P(xa, z, 1), P(xb, z, 1), P(xb, z, -1), P(xa, z, -1), slot)
                elif not kind:                   # soffit
                    self.quad(P(xa, z, -1), P(xb, z, -1), P(xb, z, 1), P(xa, z, 1), slot)
                i = k + 1

        # opening furniture
        for o in openings:
            k = o.get('kind', 'window')
            xa, xb, zb_, zt_ = o['x0'], o['x1'], o['zb'], o['zt']
            # skip furniture where the wall has collapsed below the opening
            ci = min(range(nc), key=lambda q: abs(0.5 * (xs[q] + xs[q + 1]) - 0.5 * (xa + xb)))
            if tops[ci] < zt_ - 0.05 and k in ('window', 'door'):
                continue
            if k == 'arch':
                # clamp the spandrels to the wall that still stands over the opening; they used to be
                # drawn to full height on ruins and floated over the broken wall (verification fix)
                cols = [q for q in range(nc) if xa - 1e-3 <= 0.5 * (xs[q] + xs[q + 1]) <= xb + 1e-3]
                ztop = min([tops[q] for q in cols] + [zt_])
                if ztop > o['zs'] + 0.05:
                    self._arch(P, xa, xb, o['zs'], ztop, slot, top_cap=ztop < zt_ - 1e-3)
                continue
            if k == 'open':
                continue
            if lod >= 2:
                if self.state in ('Ruin', 'Construction'):
                    continue
                if k in ('window', 'door', 'slit'):
                    off = 1.0 + 0.04 / (t / 2)
                    def Q(x, z):
                        return p0 + a * x + nrm * (off * t / 2) + Vector((0, 0, z))
                    if k == 'door':
                        uv = self.band_uv('plank', 0, xb - xa, 0, 1)
                    else:
                        uv = self.band_uv('window', 0, 0, cell=o.get('cell', 0))
                    if panes or k != 'door':
                        self.quad(Q(xa, zb_), Q(xb, zb_), Q(xb, zt_), Q(xa, zt_), D, uv)
                continue
            if not panes or self.state in ('Ruin',):
                continue
            if self.state == 'Construction' and k != 'slit':
                continue
            depth = o.get('depth', 0.14 if k != 'door' else 0.18)
            def Q2(x, z):
                return p0 + a * x + nrm * (t / 2 - depth) + Vector((0, 0, z))
            if k == 'door':
                self.quad(Q2(xa, zb_), Q2(xb, zb_), Q2(xb, zt_), Q2(xa, zt_), D, self.band_uv('plank', 0, xb - xa, 0, 1))
            else:
                self.quad(Q2(xa, zb_), Q2(xb, zb_), Q2(xb, zt_), Q2(xa, zt_), D, self.band_uv('window', 0, 0, cell=o.get('cell', 0)))
            if k in ('window', 'door') and o.get('lintel', True) and lod <= 1:
                ov = 0.18
                c0 = p0 + a * (xa - ov) + nrm * (t / 2 + 0.03)
                c1 = p0 + a * (xb + ov) + nrm * (t / 2 + 0.03)
                lz = zt_ + 0.12
                self.beam(c0 + Vector((0, 0, lz)), c1 + Vector((0, 0, lz)), 0.24, 0.22, D, 'timber', ml=1)
            if k == 'window' and o.get('shutters', self.shutters) and lod <= 1 and self.state in ('Intact', 'Damaged'):
                sw = (xb - xa) / 2 + 0.04
                for sgn, xc in ((-1, xa - sw / 2 - 0.06), (1, xb + sw / 2 + 0.06)):
                    if xc - sw / 2 < 0.1 or xc + sw / 2 > L - 0.1:
                        continue
                    if self.damaged and hash01(key, int(xa * 10), sgn) > 0.6:
                        continue
                    c = p0 + a * xc + nrm * (t / 2 + 0.035)
                    yaw = math.atan2(a.y, a.x)
                    self.push(Matrix.Translation(c) @ Matrix.Rotation(yaw, 4, 'Z'))
                    self.box(0, 0, zb_ - 0.02, sw, 0.05, zt_ - zb_ + 0.04, D, band='plank', ml=1)
                    self.pop()
            if k == 'window' and o.get('sill', True) and lod == 0:
                c0 = p0 + a * (xa - 0.08) + nrm * (t / 2 + 0.04)
                c1 = p0 + a * (xb + 0.08) + nrm * (t / 2 + 0.04)
                self.beam(c0 + Vector((0, 0, zb_ - 0.05)), c1 + Vector((0, 0, zb_ - 0.05)), 0.16, 0.1, S, None, ml=0)

    def _arch(self, P, xa, xb, zs_, zt_, slot, top_cap=False):
        """Fill the spandrels of a round arch inside the rectangular opening (xa..xb, up to zt_)."""
        r = (xb - xa) / 2
        xc = (xa + xb) / 2
        n = self.segs(10, 4)
        pts = []
        for k in range(n + 1):
            th = math.pi - math.pi * k / n
            pts.append((xc + r * math.cos(th), min(zt_, zs_ + r * math.sin(th))))
        for k in range(n):
            (x0, z0), (x1, z1) = pts[k], pts[k + 1]
            if zt_ - max(z0, z1) > 1e-3 or True:
                self.quad(P(x0, z0, 1), P(x1, z1, 1), P(x1, zt_, 1), P(x0, zt_, 1), slot)
                self.quad(P(x1, z1, -1), P(x0, z0, -1), P(x0, zt_, -1), P(x1, zt_, -1), slot)
            if zt_ - min(z0, z1) > 1e-3:
                self.quad(P(x0, z0, -1), P(x1, z1, -1), P(x1, z1, 1), P(x0, z0, 1), slot)
            if top_cap and zt_ - min(z0, z1) > 1e-3:
                # the spandrel stops at a broken wall top: close it (verification fix)
                self.quad(P(x0, zt_, 1), P(x1, zt_, 1), P(x1, zt_, -1), P(x0, zt_, -1), slot)

    def gable_tri(self, p0, p1, z_eave, z_ridge, t, slot=S, ml=3, key=0, band=None):
        """Triangular gable infill on top of a wall from p0 to p1 (outside to the right)."""
        if not self.keep(ml) or self.state in ('Ruin', 'Construction'):
            return
        n0 = len(self.faces)
        self._gable_tri(p0, p1, z_eave, z_ridge, t, slot)
        if band:
            self._band_remap(n0, slot, band, p0, p1, z_eave, z_ridge)

    def _gable_tri(self, p0, p1, z_eave, z_ridge, t, slot):
        p0 = Vector((p0[0], p0[1], 0))
        p1 = Vector((p1[0], p1[1], 0))
        a = (p1 - p0)
        L = a.length
        a.normalize()
        nrm = Vector((a.y, -a.x, 0))
        m = (p0 + p1) / 2
        for side in (1, -1):
            o = nrm * (side * t / 2)
            A = p0 + o + Vector((0, 0, z_eave))
            B = p1 + o + Vector((0, 0, z_eave))
            C = m + o + Vector((0, 0, z_ridge))
            self.face([A, B, C] if side == 1 else [B, A, C], slot)

    # ------------------------------------------------------------------ roofs
    def slab(self, Pf, ns, nt, thick, slot, uvf, holes=None, edges=(True, True, True, False), noise=0.0,
             shade_under=0.55, key=0):
        """Roof slab: top surface Pf(s,t) (t=0 eave, t=1 ridge), offset down by `thick` along -normal.
        edges = (eave, rake s=0, rake s=1, ridge). holes(i,j)->True removes a cell."""
        ns = max(1, ns)
        nt = max(1, nt)
        grid = [[Pf(i / ns, j / nt) for j in range(nt + 1)] for i in range(ns + 1)]
        # normals per vertex from the grid
        def nrm(i, j):
            i0, i1 = max(i - 1, 0), min(i + 1, ns)
            j0, j1 = max(j - 1, 0), min(j + 1, nt)
            ds = grid[i1][j] - grid[i0][j]
            dt_ = grid[i][j1] - grid[i][j0]
            n = ds.cross(dt_)
            if n.length < 1e-8:
                return Vector((0, 0, 1))
            n.normalize()
            if n.z < 0:
                n = -n
            return n
        N_ = [[nrm(i, j) for j in range(nt + 1)] for i in range(ns + 1)]
        if noise > 0:
            for i in range(1, ns):
                for j in range(1, nt):
                    d = (hash01(key, i, j, 3) - 0.5) * 2 * noise
                    grid[i][j] = grid[i][j] + N_[i][j] * d
        bot = [[grid[i][j] - N_[i][j] * thick for j in range(nt + 1)] for i in range(ns + 1)]
        cell = [[not (holes and holes(i, j)) for j in range(nt)] for i in range(ns)]
        # winding: top normal must point up
        n0 = (grid[1][0] - grid[0][0]).cross(grid[0][1] - grid[0][0])
        flip = n0.z < 0

        def F(pts, uvs, shade=1.0):
            if flip:
                pts = pts[::-1]
                uvs = uvs[::-1] if uvs else None
            self.face(pts, slot, uvs, shade)

        def uvq(i, j):
            return [uvf(i / ns, j / nt), uvf((i + 1) / ns, j / nt), uvf((i + 1) / ns, (j + 1) / nt), uvf(i / ns, (j + 1) / nt)]

        for i in range(ns):
            for j in range(nt):
                if not cell[i][j]:
                    continue
                F([grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]], uvq(i, j))
                if thick > 0:
                    F([bot[i][j + 1], bot[i + 1][j + 1], bot[i + 1][j], bot[i][j]], uvq(i, j)[::-1], shade_under)
        if thick <= 0:
            return
        # boundary edges: between a present cell and a missing one / the grid border
        def present(i, j):
            return 0 <= i < ns and 0 <= j < nt and cell[i][j]
        for i in range(ns):
            for j in range(nt + 1):      # edges along s at row j
                a_, b_ = present(i, j - 1), present(i, j)
                if a_ == b_:
                    continue
                if j == 0 and not edges[0]:
                    continue
                if j == nt and not edges[3]:
                    continue
                A, B = grid[i][j], grid[i + 1][j]
                A2, B2 = bot[i][j], bot[i + 1][j]
                uv = [uvf(i / ns, j / nt), uvf((i + 1) / ns, j / nt), (uvf((i + 1) / ns, j / nt)[0], uvf((i + 1) / ns, j / nt)[1] - thick / 3), (uvf(i / ns, j / nt)[0], uvf(i / ns, j / nt)[1] - thick / 3)]
                if b_:   # cell above (j) present, edge faces toward -t
                    F([A2, B2, B, A], uv, 0.8)
                else:
                    F([A, B, B2, A2], uv, 0.8)
        for i in range(ns + 1):
            for j in range(nt):
                a_, b_ = present(i - 1, j), present(i, j)
                if a_ == b_:
                    continue
                if i == 0 and not edges[1]:
                    continue
                if i == ns and not edges[2]:
                    continue
                A, B = grid[i][j], grid[i][j + 1]
                A2, B2 = bot[i][j], bot[i][j + 1]
                uv = [uvf(i / ns, j / nt), uvf(i / ns, (j + 1) / nt), (uvf(i / ns, (j + 1) / nt)[0] + thick / 3, uvf(i / ns, (j + 1) / nt)[1]), (uvf(i / ns, j / nt)[0] + thick / 3, uvf(i / ns, j / nt)[1])]
                if b_:
                    F([A, B, B2, A2], uv, 0.8)
                else:
                    F([A2, B2, B, A], uv, 0.8)

    def roof_holes(self, key, ns, nt, count=1, size=0.3):
        """Hole predicate for a damaged roof slope, plus scorch spots."""
        if self.state != 'Damaged':
            return None
        hs = []
        for k in range(count):
            hs.append((0.2 + 0.6 * hash01(key, k, 21), 0.25 + 0.5 * hash01(key, k, 22), size * (0.7 + 0.6 * hash01(key, k, 23))))

        def f(i, j):
            s = (i + 0.5) / ns
            t = (j + 0.5) / nt
            for (hs_, ht, r) in hs:
                if ((s - hs_) / (r * 0.8)) ** 2 + ((t - ht) / r) ** 2 < 1:
                    return True
            return False
        return f, hs

    def gable_roof(self, x0, x1, half, z_eave, pitch, kind='thatch', oh=0.4, ohg=0.3, key=0, ml=3,
                   ridge_cap=True, rafters=True, strips=0.0):
        """Gable roof with the ridge along X from x0 to x1, eaves at y = +-half (wall line), wall top z_eave."""
        if not self.keep(ml):
            return
        tn = math.tan(math.radians(pitch))
        th = {'thatch': 0.42, 'shingle': 0.14, 'tile': 0.16, 'slate': 0.12, 'stone': 0.2}[kind]
        lift = 0.08 if kind != 'thatch' else 0.05
        zr = z_eave + half * tn + lift + th
        if self.state in ('Construction', 'Ruin'):
            self._rafters_gable(x0, x1, half, z_eave, pitch, key)
            return
        tile = self.tiles[R]
        Ltot = (x1 - x0) + 2 * ohg
        slope_len = (half + oh) / math.cos(math.radians(pitch))
        lod = self.lod
        if lod >= 3:
            ns, nt = 1, 1
        elif lod == 2:
            ns, nt = (3, 2) if self.damaged else (1, 1)
        else:
            cell = (0.55, 1.4)[lod] if kind == 'thatch' else (1.6, 3.0)[lod]
            ns = max(2, int(Ltot / cell))
            nt = max(2, int(slope_len / cell))
        noise = 0.05 if (kind == 'thatch' and lod == 0) else 0.0
        thick = th if lod < 3 else 0.0
        for side in (-1, 1):
            hz = self.roof_holes(key * 10 + side + 5, ns, nt, 1, 0.28)
            holes = hz[0] if hz else None

            def Pf(s, t, side=side):
                y = side * (half + oh) * (1 - t)
                z = z_eave + lift + th + (half - abs(y)) * tn
                return Vector((x0 - ohg + Ltot * s, y, z))

            def uvf(s, t):
                return (Ltot * s / tile, slope_len * t / tile)
            self.slab(Pf, ns, nt, thick, R, uvf, holes, edges=(True, True, True, False), noise=noise, key=key + side)
            if strips > 0 and lod == 0:
                # cover-tile ridges running down the slope (skipped over holes)
                n = int(Ltot / strips)
                for k in range(n):
                    s_ = (k + 0.5) / n
                    if holes and any(holes(min(ns - 1, int(s_ * ns)), j) for j in range(nt)):
                        continue
                    self.tube([Pf(s_, 0.0) + Vector((0, 0, 0.01)), Pf(s_, 0.97) + Vector((0, 0, 0.01))], 0.075, 3, R, ml=0, half=True)
            if hz:
                for (hs_, ht, r) in hz[1]:
                    p = Pf(hs_, ht)
                    self.burn.append((self.M @ p, 2.2 + r * 6))
                    self._hole_rafters(Pf, hs_, ht, r, side, z_eave, tn)
        self.ridges.append((self.M @ Vector((x0 - ohg + 0.15, 0, zr + 0.05)), self.M @ Vector((x1 + ohg - 0.15, 0, zr + 0.05))))
        if kind == 'shingle' and lod == 0 and oh > 0.2:   # rafter tails under the eaves
            n = max(2, int((x1 - x0) / 0.6))
            for i in range(n + 1):
                x = x0 + 0.25 + (x1 - x0 - 0.5) * i / n
                for side in (-1, 1):
                    za = z_eave + lift - 0.02
                    zb_ = za - (oh - 0.08) * tn
                    self.beam((x, side * (half - 0.05), za), (x, side * (half + oh - 0.08), zb_), 0.09, 0.13, D, 'timber', ml=0)
        # ridge
        if ridge_cap and lod <= 2:
            pa = Vector((x0 - ohg, 0, zr - 0.02))
            pb = Vector((x1 + ohg, 0, zr - 0.02))
            if kind == 'thatch':
                self.tube([pa, pb], 0.32, self.segs(6, 3), R, ml=2, half=True)
            else:
                band = None
                self.tube([pa + Vector((0, 0, 0.02)), pb + Vector((0, 0, 0.02))], 0.16 if kind != 'slate' else 0.1, self.segs(4, 2), R, ml=1, half=True)
        # thatch eave and rake rolls
        if kind == 'thatch' and lod <= 1:
            for side in (-1, 1):
                y = side * (half + oh)
                z = z_eave + lift + th * 0.5 - 0.05 - oh * tn
                self.tube([(x0 - ohg, y, z), (x1 + ohg, y, z)], th * 0.55, 5 if lod == 0 else 3, R, ml=1)
                for xe in (x0 - ohg, x1 + ohg):
                    self.tube([(xe, y, z), (xe, 0, zr - 0.15)], th * 0.5, 5 if lod == 0 else 3, R, ml=1)
        # bargeboards on non-thatch roofs
        if kind in ('shingle', 'tile', 'slate') and lod <= 1:
            for xe in (x0 - ohg, x1 + ohg):
                for side in (-1, 1):
                    a = Vector((xe, side * (half + oh), z_eave + lift + th - oh * tn - 0.1))
                    b_ = Vector((xe, 0, zr - 0.05))
                    self.beam(a, b_, 0.08, 0.3, D, 'timber', ml=1)

    def _hole_rafters(self, Pf, hs_, ht, r, side, z_eave, tn):
        if self.lod > 1:
            return
        for k in (-1, 0, 1):
            s = hs_ + k * r * 0.35
            a = Pf(s, max(0.0, ht - r * 1.2)) - Vector((0, 0, 0.3))
            b_ = Pf(s, min(1.0, ht + r * 1.2)) - Vector((0, 0, 0.3))
            self.beam(a, b_, 0.14, 0.18, D, 'char', ml=1)

    def _rafters_gable(self, x0, x1, half, z_eave, pitch, key):
        """Ruin: a few fallen charred roof timbers. Construction: a stack of timber waiting outside."""
        if self.lod > 1:
            return
        Ltot = x1 - x0
        if self.ruined:
            n = 2 + int(Ltot / 4)
            for i in range(n):
                xa = x0 + Ltot * (0.15 + 0.7 * hash01(key, i, 1))
                xb = xa + (hash01(key, i, 2) - 0.5) * 3.0
                ya = -half * 0.8 * (1 if i % 2 else -1)
                yb = ya * -0.4 + (hash01(key, i, 3) - 0.5)
                self.beam((xa, ya, 0.2 + 0.9 * hash01(key, i, 4)), (xb, yb, 0.25), 0.2, 0.22, D, 'char', ml=1)
            return
        if self.building:
            yb = half + 1.6
            for i in range(5):
                zz = 0.12 + 0.22 * (i // 3)
                yy = yb + (i % 3) * 0.25 + (0.12 if i >= 3 else 0)
                self.beam((x0 + 0.4, yy, zz), (x0 + 0.4 + min(Ltot, 7.0), yy, zz), 0.2, 0.2, D, 'timber', ml=1)
            return
        tn = math.tan(math.radians(pitch))
        zr = z_eave + half * tn
        Ltot = x1 - x0
        n = max(2, int(Ltot / (0.9 if self.lod == 0 else 1.8)))
        band = 'timber' if self.building else 'char'
        for i in range(n + 1):
            x = x0 + Ltot * i / n
            if self.ruined and hash01(key, i, 77) > 0.25:
                continue
            for side in (-1, 1):
                if self.ruined and hash01(key, i, side, 78) > 0.6:
                    continue
                a = Vector((x, side * (half + 0.3), z_eave - 0.3 * tn + 0.1))
                b_ = Vector((x, 0, zr + 0.1))
                if self.ruined:
                    b_ = a.lerp(b_, 0.4 + 0.5 * hash01(key, i, side))
                self.beam(a, b_, 0.14, 0.2, D, band, ml=2)
        if self.building:
            self.beam((x0, 0, zr + 0.2), (x1, 0, zr + 0.2), 0.2, 0.25, D, 'timber', ml=2)
            for side in (-1, 1):   # wall plates
                self.beam((x0, side * half, z_eave), (x1, side * half, z_eave), 0.25, 0.2, D, 'timber', ml=2)

    def hip_roof(self, x0, x1, y0, y1, z_eave, pitch, kind='thatch', oh=0.4, key=0, ml=3, ridge_cap=True):
        """Hipped roof over the rectangle x0..x1, y0..y1 (wall lines). Ridge along the long axis
        (pyramid when square)."""
        if not self.keep(ml):
            return
        tn = math.tan(math.radians(pitch))
        th = {'thatch': 0.42, 'shingle': 0.14, 'tile': 0.16, 'slate': 0.12, 'stone': 0.2}[kind]
        lift = 0.06
        if self.state in ('Construction', 'Ruin'):
            if (x1 - x0) >= (y1 - y0):
                self.push(self.at((x0 + x1) / 2, (y0 + y1) / 2))
                self._rafters_gable(-(x1 - x0) / 2, (x1 - x0) / 2, (y1 - y0) / 2, z_eave, pitch, key)
            else:
                self.push(self.at((x0 + x1) / 2, (y0 + y1) / 2, 0, math.pi / 2))
                self._rafters_gable(-(y1 - y0) / 2, (y1 - y0) / 2, (x1 - x0) / 2, z_eave, pitch, key)
            self.pop()
            return
        swap = (y1 - y0) > (x1 - x0)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if swap:
            self.push(self.at(cx, cy, 0, math.pi / 2))
            hx, hy = (y1 - y0) / 2, (x1 - x0) / 2
        else:
            self.push(self.at(cx, cy))
            hx, hy = (x1 - x0) / 2, (y1 - y0) / 2
        ex, ey = hx + oh, hy + oh
        rl = max(0.0, ex - ey)           # half ridge length
        zb = z_eave + lift + th - oh * tn
        zr = zb + ey * tn
        tile = self.tiles[R]
        lod = self.lod
        cellsz = (0.55, 1.4, 99, 99)[lod] if kind == 'thatch' else (1.6, 3.0, 99, 99)[lod]
        noise = 0.05 if (kind == 'thatch' and lod == 0) else 0.0
        thick = th if lod < 3 else 0.0
        slope_len = ey / math.cos(math.radians(pitch))
        # long sides (trapezoids)
        for side in (-1, 1):
            ns = max(1, int(2 * ex / cellsz)) if lod <= 1 else (3 if self.damaged and lod == 2 else 1)
            nt = max(1, int(slope_len / cellsz)) if lod <= 1 else (2 if self.damaged and lod == 2 else 1)
            hz = self.roof_holes(key * 10 + side + 3, ns, nt, 1, 0.3)

            def Pf(s, t, side=side):
                xe = -ex + 2 * ex * s
                xr = -rl + 2 * rl * s
                x = xe + (xr - xe) * t
                return Vector((x, side * ey * (1 - t), zb + ey * tn * t))

            def uvf(s, t):
                return (2 * ex * s / tile, slope_len * t / tile)
            self.slab(Pf, ns, nt, thick, R, uvf, hz[0] if hz else None, edges=(True, True, True, False), noise=noise, key=key + side)
            if hz:
                for (hs_, ht, r) in hz[1]:
                    self.burn.append((self.M @ Pf(hs_, ht), 2.2 + r * 6))
                    self._hole_rafters(Pf, hs_, ht, r, side, z_eave, tn)
        # hip ends (triangles; bilinear patch with collapsed top)
        for side in (-1, 1):
            ns = max(1, int(2 * ey / cellsz)) if lod <= 1 else 1
            nt = max(1, int(slope_len / cellsz)) if lod <= 1 else 1

            def Pf(s, t, side=side):
                ye = -ey + 2 * ey * s
                y = ye * (1 - t)
                x = side * (ex + (rl - ex) * t)
                return Vector((x, y, zb + ey * tn * t))

            def uvf(s, t):
                return (2 * ey * s / tile, slope_len * t / tile)
            self.slab(Pf, ns, nt, thick, R, uvf, None, edges=(True, False, False, False), noise=noise, key=key + 7 + side)
        self.ridges.append((self.M @ Vector((-rl, 0, zr + 0.05)), self.M @ Vector((rl, 0, zr + 0.05))))
        if ridge_cap and lod <= 2:
            if kind == 'thatch':
                if rl > 0.05:
                    self.tube([(-rl, 0, zr - 0.02), (rl, 0, zr - 0.02)], 0.32, self.segs(6, 3), R, ml=2, half=True)
                for sx in (-1, 1):
                    for sy in (-1, 1):
                        self.tube([(sx * rl, 0, zr - 0.05), (sx * ex, sy * ey, zb - 0.02)], 0.22, self.segs(5, 3), R, ml=0, half=True)
            else:
                if rl > 0.05:
                    self.tube([(-rl, 0, zr + 0.01), (rl, 0, zr + 0.01)], 0.13, self.segs(4, 2), R, ml=1, half=True)
                for sx in (-1, 1):
                    for sy in (-1, 1):
                        self.tube([(sx * rl, 0, zr), (sx * ex, sy * ey, zb + 0.02)], 0.1, self.segs(4, 2), R, ml=0, half=True)
        if kind == 'thatch' and lod <= 1:
            z = zb - th * 0.5 + 0.02
            corners = [(-ex, -ey), (ex, -ey), (ex, ey), (-ex, ey), (-ex, -ey)]
            self.tube([(x, y, z) for x, y in corners], th * 0.55, 5 if lod == 0 else 3, R, ml=1)
        self.pop()

    def lean_to(self, x0, x1, y_lo, y_hi, z_lo, pitch, kind='shingle', oh=0.3, key=0, ml=2):
        """Single-pitch roof falling toward y_lo (wall line at y_hi is the high side)."""
        if not self.keep(ml):
            return
        if self.state in ('Construction', 'Ruin'):   # lean-tos are simply absent
            return
        tn = math.tan(math.radians(pitch))
        th = {'thatch': 0.35, 'shingle': 0.12, 'tile': 0.14, 'slate': 0.1, 'stone': 0.2}[kind]
        sgn = 1 if y_hi > y_lo else -1
        ya = y_lo - sgn * oh
        span = abs(y_hi - ya)
        tile = self.tiles[R]
        L_ = x1 - x0 + 2 * oh * 0.5
        sl = span / math.cos(math.radians(pitch))
        lod = self.lod
        ns = max(1, int(L_ / 1.6)) if lod <= 1 else 1
        nt = max(1, int(sl / 1.6)) if lod <= 1 else 1

        def Pf(s, t):
            y = ya + (y_hi - ya) * t
            return Vector((x0 - oh * 0.5 + L_ * s, y, z_lo - oh * tn + th + span * tn * t))

        def uvf(s, t):
            return (L_ * s / tile, sl * t / tile)
        self.slab(Pf, ns, nt, th if lod < 3 else 0.0, R, uvf, None, edges=(True, True, True, True), noise=0.0, key=key)

    # ------------------------------------------------------------------ props used by many assets
    def chimney(self, x, y, z0, z1, w=0.8, d=0.7, ml=2):
        if self.state == 'Ruin':     # the chimney stack often outlives the house
            z0, z1 = 0.0, 2.2 + 1.5 * hash01(int(x * 10), int(y * 10), 5)
            self.box(x, y, z0, w, d, z1, S, ml=ml)
            return
        if self.building:
            return
        self.box(x, y, z0, w, d, z1 - z0, S, ml=ml)
        self.box(x, y, z1, w + 0.12, d + 0.12, 0.15, S, ml=0)

    def scaffold(self, p0, p1, z1, out=1.2, key=0):
        """Timber scaffold poles and planks along a wall line (construction state)."""
        if not self.building or self.lod > 1:
            return
        p0 = Vector((p0[0], p0[1], 0))
        p1 = Vector((p1[0], p1[1], 0))
        a = p1 - p0
        L = a.length
        a.normalize()
        nrm = Vector((a.y, -a.x, 0))
        n = max(1, int(L / 2.6))
        for i in range(n + 1):
            p = p0 + a * (L * i / n) + nrm * out
            self.beam(p + Vector((0, 0, -0.3)), p + Vector((0, 0, z1 + 1.0)), 0.12, 0.12, D, 'timber', ml=1)
            q = p - nrm * (out - 0.2)
            for zz in (z1 * 0.45, z1 * 0.9):
                if zz > 0.8:
                    self.beam(p + Vector((0, 0, zz)), q + Vector((0, 0, zz)), 0.08, 0.08, D, 'timber', ml=0)
        for zz in (z1 * 0.45, z1 * 0.9):
            if zz < 0.8:
                continue
            b0 = p0 + nrm * (out - 0.35) + Vector((0, 0, zz + 0.06))
            b1 = p1 + nrm * (out - 0.35) + Vector((0, 0, zz + 0.06))
            self.beam(b0, b1, 0.5, 0.06, D, 'timber', ml=1, up=(0, 0, 1))
            self.beam(p0 + nrm * out + Vector((0, 0, zz + 0.9)), p1 + nrm * out + Vector((0, 0, zz + 0.9)), 0.07, 0.07, D, 'timber', ml=0)

    def stone_pile(self, x, y, n=6, key=0):
        """Stack of dressed blocks waiting to be laid (construction)."""
        if self.lod > 1:
            return
        for k in range(n):
            self.box(x + (k % 3) * 0.75 - 0.75, y + (k // 3) * 0.55, (k // 6) * 0.45, 0.7, 0.5, 0.42, S, yaw=0.05 * hash01(key, k), ml=1)

    def figure(self, x, y, yaw=0.0):
        """1.8 m scale figure (preview only)."""
        self.cyl(x, y, 0.0, 1.45, 0.22, 0.18, 8, D, band='cloth_r', ml=0)
        self.cyl(x, y, 1.45, 1.8, 0.11, 0.09, 8, D, band='plaster', ml=0)


# ---------------------------------------------------------------------- mesh output


def grime(z, base_h):
    if z <= 0:
        return 0.62
    return 0.62 + 0.38 * min(1.0, z / base_h) ** 0.7


def to_mesh(b, name, tiles):
    """Builder -> bpy Mesh (triangulated, UVs, colour attribute 'Col', sharp edges by angle)."""
    me = bpy.data.meshes.get(name)
    if me:
        bpy.data.meshes.remove(me)
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new('UVMap')
    col = bm.loops.layers.color.new('Col')
    cache = {}

    def V(p):
        k = (round(p.x * 500), round(p.y * 500), round(p.z * 500))
        v = cache.get(k)
        if v is None:
            v = bm.verts.new(p)
            cache[k] = v
        return v

    burn = b.burn
    ruin_dark = 0.82 if b.state == 'Ruin' else 1.0
    for pts, slot, uvs, shade in b.faces:
        vs = []
        us = []
        for i, p in enumerate(pts):
            v = V(p)
            if vs and vs[-1] is v:
                continue
            vs.append(v)
            us.append(i)
        if len(vs) > 2 and vs[0] is vs[-1]:
            vs.pop()
            us.pop()
        if len(vs) < 3 or len(set(vs)) != len(vs):
            continue
        try:
            f = bm.faces.new(vs)
        except ValueError:
            continue
        f.material_index = slot
        # UVs
        if uvs is None:
            nrm = (pts[1] - pts[0]).cross(pts[2] - pts[0])
            if nrm.length < 1e-9 and len(pts) > 3:
                nrm = (pts[2] - pts[0]).cross(pts[3] - pts[0])
            nrm = nrm.normalized() if nrm.length > 1e-9 else Vector((0, 0, 1))
            tile = tiles.get(slot, 2.0)
            if abs(nrm.z) > 0.7:
                uvs_ = [(p.x / tile, p.y / tile) for p in pts]
            else:
                t = Vector((-nrm.y, nrm.x, 0)).normalized()
                uvs_ = [(p.dot(t) / tile, p.z / tile) for p in pts]
        else:
            uvs_ = list(uvs)
            while len(uvs_) < len(pts):
                uvs_.append(uvs_[-1])
        for li, loop in enumerate(f.loops):
            i = us[li]
            loop[uvl].uv = uvs_[i]
            p = pts[i]
            g = grime(p.z, b.grime_h) if slot != R else 1.0
            if b.eave_z is not None and slot == S and p.z < b.eave_z + 0.3:
                g *= 1.0 - 0.28 * max(0.0, min(1.0, (p.z - (b.eave_z - 1.0)) / 1.0))
            g *= shade * ruin_dark
            for (c, r) in burn:
                d = (p - c).length
                if d < r:
                    g *= 0.3 + 0.7 * (d / r) ** 1.5
            g = max(0.05, min(1.0, g))
            loop[col] = (g, g, g, 1.0)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    for _ in range(3):
        me.materials.append(None)     # slots must exist or to_mesh clamps the material indices
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(34))
    # (no validate(): with no materials attached yet it would reset material indices)
    return me


def tri_count(me):
    return len(me.polygons)
