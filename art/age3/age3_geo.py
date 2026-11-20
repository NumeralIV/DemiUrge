# DEMIURGE art - Age3 geometry kit: a small mesh builder that writes faces with trim-sheet UVs.
#
# Every face is mapped into one band of the family trim sheets (age3_tex.BANDS) or one cell of the Detail
# atlas. Planar faces are cut into band-height strips ("metric" mapping) so the texture keeps its real
# scale on any size of wall; thin parts use "fit" mapping (the face's height is squeezed into the band).
# Faces carry a tag ('wall', 'roof', 'prop', 'base', ...) that the state passes (construction, damaged, ruin)
# use to decide what to cut, remove or thicken.
import math
import random

from age3_tex import BANDS, cell_uv

SLOTS = ['Structure', 'Roof', 'Detail']
SLOT = {n: i for i, n in enumerate(SLOTS)}
INSET = 2.0 / 1024


# ----------------------------------------------------------------------------------------------- vector helpers
def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def norm(a):
    l = length(a)
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-12 else (0.0, 0.0, 1.0)


def lerp3(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def newell(pts):
    nx = ny = nz = 0.0
    for i in range(len(pts)):
        a = pts[i]
        b = pts[(i + 1) % len(pts)]
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    return norm((nx, ny, nz))


def frame_for(n):
    if abs(n[2]) > 0.92:
        t = (1.0, 0.0, 0.0) if n[2] > 0 else (1.0, 0.0, 0.0)
        up = norm(cross(n, t))
        return t, up
    t = norm(cross((0.0, 0.0, 1.0), n))
    up = norm(cross(n, t))
    return t, up


def rotz(p, a, c=(0.0, 0.0, 0.0)):
    ca, sa = math.cos(a), math.sin(a)
    x, y = p[0] - c[0], p[1] - c[1]
    return (c[0] + x * ca - y * sa, c[1] + x * sa + y * ca, p[2])


def _clip(poly, lo, hi):
    """poly: list of (p3, s, w). Keep lo <= w <= hi (Sutherland-Hodgman, two half-planes)."""
    def clip_half(vs, keep, cut):
        out = []
        n = len(vs)
        for i in range(n):
            a = vs[i]
            b = vs[(i + 1) % n]
            ina, inb = keep(a[2]), keep(b[2])
            if ina:
                out.append(a)
            if ina != inb:
                t = (cut - a[2]) / (b[2] - a[2])
                out.append((lerp3(a[0], b[0], t), a[1] + (b[1] - a[1]) * t, cut))
        return out
    vs = clip_half(poly, lambda w: w >= lo - 1e-7, lo)
    if len(vs) < 3:
        return []
    vs = clip_half(vs, lambda w: w <= hi + 1e-7, hi)
    return vs if len(vs) >= 3 else []


def poly_area(pts):
    n = (0.0, 0.0, 0.0)
    for i in range(len(pts)):
        n = add(n, cross(pts[i], pts[(i + 1) % len(pts)]))
    return 0.5 * length(n)


# ----------------------------------------------------------------------------------------------- builder
class MB:
    def __init__(self, seed=1):
        self.V = []
        self.F = []      # (vertex ids, slot, uvs, cols, tag)
        self.col = (1.0, 1.0, 1.0)
        self.tag = 'wall'
        self.rng = random.Random(seed)

    # -- raw
    def face(self, pts, slot, uvs, tag=None, cols=None):
        b = len(self.V)
        self.V.extend(pts)
        self.F.append((list(range(b, b + len(pts))), slot, list(uvs), cols or [self.col] * len(pts), tag or self.tag))

    def tris(self):
        return sum(len(f[0]) - 2 for f in self.F)

    def extend(self, other, xf=None):
        b = len(self.V)
        for p in other.V:
            self.V.append(xf(p) if xf else p)
        for ids, s, uv, c, t in other.F:
            self.F.append(([i + b for i in ids], s, uv, c, t))

    # -- band-mapped planar polygon. pts CCW seen from outside.
    def poly(self, pts, band, mode='metric', anchor=None, uoff=0.0, frame=None, tag=None, flip_u=False):
        if band in BANDS:
            slot, v0, v1, hm, wm = BANDS[band]
        else:
            return self.apoly(pts, band, tag=tag, frame=frame)
        n = newell(pts)
        t, up = frame if frame else frame_for(n)
        va, vb = v0 + INSET, v1 - INSET
        rec = [(p, dot(p, t), dot(p, up)) for p in pts]
        ws = [r[2] for r in rec]
        wmin, wmax = min(ws), max(ws)
        su = -1.0 if flip_u else 1.0
        if mode == 'fit' or wmax - wmin < 1e-6:
            span = max(wmax - wmin, 1e-6)
            self.face([r[0] for r in rec], SLOT[slot],
                      [((su * r[1] + uoff) / wm, va + (r[2] - wmin) / span * (vb - va)) for r in rec], tag)
            return
        w0 = anchor if anchor is not None else wmin
        k0 = math.floor((wmin - w0) / hm + 1e-6)
        k1 = math.ceil((wmax - w0) / hm - 1e-6)
        segs = []
        for k in range(k0, k1):
            lo = max(wmin, w0 + k * hm)
            hi = min(wmax, w0 + (k + 1) * hm)
            if hi - lo > 1e-6:
                segs.append([lo, hi, w0 + k * hm, hm])
        # fold slivers (< 22 % of a band) into their neighbour, stretching that strip a little
        merged = []
        for s in segs:
            if merged and (s[1] - s[0]) < 0.22 * hm:
                m = merged[-1]
                m[1] = s[1]
                m[2] = m[0] if m[0] > m[2] else m[2]
                m[3] = m[1] - m[2]
            else:
                merged.append(s)
        if len(merged) > 1 and (merged[0][1] - merged[0][0]) < 0.22 * hm:
            a, b = merged[0], merged[1]
            b[0] = a[0]
            b[2] = a[0]
            b[3] = b[1] - a[0]
            merged = merged[1:]
        for lo, hi, base, scale in merged:
            part = _clip(rec, lo, hi)
            if len(part) < 3:
                continue
            pts3 = [r[0] for r in part]
            if poly_area(pts3) < 1e-6:
                continue
            sc = max(scale, hi - base)
            self.face(pts3, SLOT[slot], [((su * r[1] + uoff) / wm, va + (r[2] - base) / sc * (vb - va)) for r in part], tag)

    # -- atlas cell on a quad: p0 bottom-left, p1 bottom-right, p2 top-right, p3 top-left (CCW from outside)
    def aquad(self, p0, p1, p2, p3, cell, tag=None, flipx=False):
        u0, v0, u1, v1 = cell_uv(cell)
        if flipx:
            u0, u1 = u1, u0
        self.face([p0, p1, p2, p3], SLOT['Detail'], [(u0, v0), (u1, v0), (u1, v1), (u0, v1)], tag)

    def apoly(self, pts, cell, tag=None, frame=None):
        """Atlas cell fitted onto any planar polygon (fills: iron, rope, canvas, charred ...)."""
        n = newell(pts)
        t, up = frame if frame else frame_for(n)
        u0, v0, u1, v1 = cell_uv(cell)
        ss = [dot(p, t) for p in pts]
        ws = [dot(p, up) for p in pts]
        s0, s1 = min(ss), max(ss)
        w0, w1 = min(ws), max(ws)
        ds, dw = max(s1 - s0, 1e-6), max(w1 - w0, 1e-6)
        self.face(list(pts), SLOT['Detail'], [(u0 + (s - s0) / ds * (u1 - u0), v0 + (w - w0) / dw * (v1 - v0)) for s, w in zip(ss, ws)], tag)

    # -- boxes
    def box(self, c, hx, hy, hz, band='ashlar', top=None, bottom=False, rot=0.0, mode='metric', anchor=None,
            sides=(True, True, True, True), tag=None, side_bands=None):
        """Axis box centred at c (rotated about Z by rot). sides = (-Y, +X, +Y, -X)."""
        x0, x1, y0, y1, z0, z1 = -hx, hx, -hy, hy, c[2] - hz, c[2] + hz

        def P(x, y, z):
            return rotz((c[0] + x, c[1] + y, z), rot, (c[0], c[1], 0.0))
        quads = [
            [P(x0, y0, z0), P(x1, y0, z0), P(x1, y0, z1), P(x0, y0, z1)],   # -Y
            [P(x1, y0, z0), P(x1, y1, z0), P(x1, y1, z1), P(x1, y0, z1)],   # +X
            [P(x1, y1, z0), P(x0, y1, z0), P(x0, y1, z1), P(x1, y1, z1)],   # +Y
            [P(x0, y1, z0), P(x0, y0, z0), P(x0, y0, z1), P(x0, y1, z1)],   # -X
        ]
        for i, q in enumerate(quads):
            if sides[i]:
                b = side_bands[i] if side_bands else band
                self.poly(q, b, mode, anchor, tag=tag)
        if top is not False:
            self.poly([P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)], top or band, 'fit' if mode == 'fit' else 'metric', tag=tag)
        if bottom:
            self.poly([P(x0, y1, z0), P(x1, y1, z0), P(x1, y0, z0), P(x0, y0, z0)], band, 'fit', tag=tag)

    def beam(self, a, b, w, h, band='beam', up=(0.0, 0.0, 1.0), caps=True, tag=None, cell=None):
        """Rectangular timber from a to b, w wide, h tall; texture runs along the beam."""
        ax = norm(sub(b, a))
        if abs(dot(ax, up)) > 0.95:
            up = (1.0, 0.0, 0.0)
        s = norm(cross(ax, up))
        u = norm(cross(s, ax))
        hw, hh = w * 0.5, h * 0.5
        corners = [add(mul(s, -hw), mul(u, -hh)), add(mul(s, hw), mul(u, -hh)), add(mul(s, hw), mul(u, hh)), add(mul(s, -hw), mul(u, hh))]
        A = [add(a, c) for c in corners]
        B = [add(b, c) for c in corners]
        # verifier fix: (s, u, ax) is a left-handed frame, so the old winding [A_i, A_j, B_j, B_i] and the caps
        # put every face of every beam inside out (scaffolds, crane bracing, rafters, raking cornices, timbers)
        for i in range(4):
            j = (i + 1) % 4
            q = [B[i], B[j], A[j], A[i]]
            if cell:
                self.apoly(q, cell, tag=tag, frame=(ax, norm(sub(A[j], A[i]))))
            else:
                self.poly(q, band, 'fit', frame=(ax, norm(sub(A[j], A[i]))), tag=tag)
        if caps:
            if cell:
                self.apoly(list(A), cell, tag=tag)
                self.apoly(list(reversed(B)), cell, tag=tag)
            else:
                self.poly(list(A), band, 'fit', tag=tag)
                self.poly(list(reversed(B)), band, 'fit', tag=tag)

    # -- cylinders, cones, domes
    def cyl(self, c, r0, r1, z0, z1, sides, band, mode='around', cap_top=True, cap_bot=False, top_band=None,
            a0=0.0, tag=None, cell=None, rings=None):
        """Frustum about the Z axis through c. mode 'around': texture U wraps the circumference in metres and V
        is cut into band strips; 'axis': U runs up the height, V wraps once around (columns, posts, ropes)."""
        if cell:
            slot = SLOT['Detail']
            u0, v0, u1, v1 = cell_uv(cell)
        else:
            sl, v0, v1, hm, wm = BANDS[band]
            slot = SLOT[sl]
            v0, v1 = v0 + INSET, v1 - INSET
        H = z1 - z0
        if rings is None:
            if cell or mode == 'axis':
                zs = [z0, z1]
            else:
                k = max(1, int(round(H / hm)))
                zs = [z0 + H * i / k for i in range(k + 1)]
        else:
            zs = rings
        rm = 0.5 * (r0 + r1)
        for i in range(len(zs) - 1):
            za, zb = zs[i], zs[i + 1]
            ta, tb = (za - z0) / H, (zb - z0) / H
            ra, rb = r0 + (r1 - r0) * ta, r0 + (r1 - r0) * tb
            for j in range(sides):
                aa = a0 + 2 * math.pi * j / sides
                ab = a0 + 2 * math.pi * (j + 1) / sides
                p0 = (c[0] + ra * math.cos(aa), c[1] + ra * math.sin(aa), za)
                p1 = (c[0] + ra * math.cos(ab), c[1] + ra * math.sin(ab), za)
                p2 = (c[0] + rb * math.cos(ab), c[1] + rb * math.sin(ab), zb)
                p3 = (c[0] + rb * math.cos(aa), c[1] + rb * math.sin(aa), zb)
                if cell:
                    uvs = [(u0 + (u1 - u0) * j / sides, v0), (u0 + (u1 - u0) * (j + 1) / sides, v0),
                           (u0 + (u1 - u0) * (j + 1) / sides, v1), (u0 + (u1 - u0) * j / sides, v1)]
                elif mode == 'axis':
                    fa, fb = j / sides, (j + 1) / sides
                    uvs = [(za / wm, v0 + (v1 - v0) * fa), (za / wm, v0 + (v1 - v0) * fb), (zb / wm, v0 + (v1 - v0) * fb), (zb / wm, v0 + (v1 - v0) * fa)]
                else:
                    ua, ub = aa * rm / wm, ab * rm / wm
                    uvs = [(ua, v0), (ub, v0), (ub, v1), (ua, v1)]
                if rb < 1e-5:
                    self.face([p0, p1, p2], slot, uvs[:3], tag)
                else:
                    self.face([p0, p1, p2, p3], slot, uvs, tag)
        if cap_top and r1 > 1e-5:
            pts = [(c[0] + r1 * math.cos(a0 + 2 * math.pi * j / sides), c[1] + r1 * math.sin(a0 + 2 * math.pi * j / sides), z1) for j in range(sides)]
            if cell:
                self.apoly(pts, cell, tag=tag)
            else:
                self.poly(pts, top_band or band, 'fit', tag=tag)
        if cap_bot and r0 > 1e-5:
            pts = [(c[0] + r0 * math.cos(a0 + 2 * math.pi * j / sides), c[1] + r0 * math.sin(a0 + 2 * math.pi * j / sides), z0) for j in reversed(range(sides))]
            if cell:
                self.apoly(pts, cell, tag=tag)
            else:
                self.poly(pts, band, 'fit', tag=tag)

    def dome(self, c, R, z0, H, sides, rings, band, open_r=0.0, a0=0.0, tag=None, profile=None):
        """Dome of radius R and height H on z0. profile(t) -> (r_frac, z_frac) for t 0..1 (default ellipse)."""
        sl, v0, v1, hm, wm = BANDS[band]
        v0, v1 = v0 + INSET, v1 - INSET
        prof = []
        for i in range(rings + 1):
            t = i / rings
            if profile:
                rf, zf = profile(t)
            else:
                rf, zf = math.cos(t * math.pi / 2), math.sin(t * math.pi / 2)
            prof.append((R * rf, z0 + H * zf))
        if open_r > 0:
            cut = [p for p in prof if p[0] >= open_r - 1e-6]
            if len(cut) < len(prof):
                # end exactly at the opening radius
                i = len(cut)
                a, b = prof[i - 1], prof[i]
                t = (a[0] - open_r) / max(a[0] - b[0], 1e-6)
                cut.append((open_r, a[1] + (b[1] - a[1]) * t))
            prof = cut
        arc = [0.0]
        for i in range(1, len(prof)):
            arc.append(arc[-1] + math.hypot(prof[i][0] - prof[i - 1][0], prof[i][1] - prof[i - 1][1]))
        # group rings into strips no longer than one band
        groups = []
        start = 0
        for i in range(1, len(prof)):
            if arc[i] - arc[start] > hm * 1.05 and i - 1 > start:
                groups.append((start, i - 1))
                start = i - 1
        groups.append((start, len(prof) - 1))
        for g0, g1 in groups:
            span = max(arc[g1] - arc[g0], 1e-6)
            for i in range(g0, g1):
                (ra, za), (rb, zb) = prof[i], prof[i + 1]
                va = v0 + (arc[i] - arc[g0]) / span * (v1 - v0)
                vb_ = v0 + (arc[i + 1] - arc[g0]) / span * (v1 - v0)
                for j in range(sides):
                    aa = a0 + 2 * math.pi * j / sides
                    ab = a0 + 2 * math.pi * (j + 1) / sides
                    ua, ub = aa * R / wm, ab * R / wm
                    p0 = (c[0] + ra * math.cos(aa), c[1] + ra * math.sin(aa), za)
                    p1 = (c[0] + ra * math.cos(ab), c[1] + ra * math.sin(ab), za)
                    p2 = (c[0] + rb * math.cos(ab), c[1] + rb * math.sin(ab), zb)
                    p3 = (c[0] + rb * math.cos(aa), c[1] + rb * math.sin(aa), zb)
                    if rb < 1e-4:
                        self.face([p0, p1, p2], SLOT[sl], [(ua, va), (ub, va), ((ua + ub) * 0.5, vb_)], tag)
                    else:
                        self.face([p0, p1, p2, p3], SLOT[sl], [(ua, va), (ub, va), (ub, vb_), (ua, vb_)], tag)
        return prof[-1]

    # -- profile swept along a footprint path (cornices, string courses, plinths, copings)
    def sweep(self, path, profile, band, z=0.0, closed=True, tag=None, cap_ends=False):
        """path: [(x, y)] CCW seen from above (outside on the right of travel). profile: [(out, up)] from the wall
        face outward/upward, ending back at the wall on top. Texture U runs along the path, V across the profile."""
        sl, v0, v1, hm, wm = BANDS[band]
        v0, v1 = v0 + INSET, v1 - INSET
        n = len(path)
        segs = n if closed else n - 1
        normals = []
        for i in range(segs):
            a, b = path[i], path[(i + 1) % n]
            dx, dy = b[0] - a[0], b[1] - a[1]
            L = math.hypot(dx, dy)
            normals.append((dy / L, -dx / L))
        offs = []
        for i in range(n):
            if closed:
                n0 = normals[i - 1]
                n1 = normals[i % segs]
            else:
                n0 = normals[max(0, i - 1)]
                n1 = normals[min(i, segs - 1)]
            mx, my = n0[0] + n1[0], n0[1] + n1[1]
            ml = math.hypot(mx, my)
            if ml < 1e-6:
                mx, my = n1
            else:
                mx, my = mx / ml, my / ml
            k = 1.0 / max(0.3, mx * n1[0] + my * n1[1])
            offs.append((mx * k, my * k))
        plen = [0.0]
        for j in range(1, len(profile)):
            plen.append(plen[-1] + math.hypot(profile[j][0] - profile[j - 1][0], profile[j][1] - profile[j - 1][1]))
        tot = max(plen[-1], 1e-6)

        def P(i, j):
            o, h = profile[j]
            return (path[i][0] + offs[i][0] * o, path[i][1] + offs[i][1] * o, z + h)
        acc = 0.0
        for i in range(segs):
            i1 = (i + 1) % n
            L = math.hypot(path[i1][0] - path[i][0], path[i1][1] - path[i][1])
            for j in range(len(profile) - 1):
                q = [P(i, j), P(i1, j), P(i1, j + 1), P(i, j + 1)]
                if poly_area(q) < 1e-7:
                    continue
                va = v0 + plen[j] / tot * (v1 - v0)
                vb_ = v0 + plen[j + 1] / tot * (v1 - v0)
                self.face(q, SLOT[sl], [(acc / wm, va), ((acc + L) / wm, va), ((acc + L) / wm, vb_), (acc / wm, vb_)], tag)
            acc += L
        if cap_ends and not closed:
            for i, rev in ((0, True), (n - 1, False)):
                pts = [P(i, j) for j in range(len(profile))]
                if rev:
                    pts = list(reversed(pts))
                if len(pts) >= 3 and poly_area(pts) > 1e-6:
                    self.poly(pts, band, 'fit', tag=tag)

    # -- roofs
    def gable_roof(self, x0, x1, y0, y1, ze, pitch, ov=0.4, ovg=0.3, axis='X', band='tile', thick=0.22, gable_band='plaster',
                   lod=0, tag='roof', gable_anchor=0.0, ridge_cap=True, gable_walls=True, info=None):
        """Gable roof over the rectangle; ridge along `axis`. ze = wall top (eave line at the wall face)."""
        tp = math.tan(math.radians(pitch))
        if axis == 'Y':
            # build as X then rotate 90 degrees about the rectangle centre
            cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
            hw, hd = (x1 - x0) * 0.5, (y1 - y0) * 0.5
            sub_ = MB()
            sub_.gable_roof(cx - hd, cx + hd, cy - hw, cy + hw, ze, pitch, ov, ovg, 'X', band, thick, gable_band, lod, tag,
                            gable_anchor, ridge_cap, gable_walls, info=None)
            self.extend(sub_, lambda p: rotz(p, math.pi / 2, (cx, cy, 0.0)))
            if info is not None:
                planes = []
                for pl in _gable_planes(cx - hd, cx + hd, cy - hw, cy + hw, ze, tp, ov, ovg):
                    planes.append([rotz(p, math.pi / 2, (cx, cy, 0.0)) for p in pl])
                info.setdefault('roof_planes', []).extend(planes)
                info['ridge_z'] = max(info.get('ridge_z', 0), ze + hw * tp)
            return ze + hw * tp
        hd = (y1 - y0) * 0.5
        ym = (y0 + y1) * 0.5
        zr = ze + hd * tp
        ye0, ye1 = y0 - ov, y1 + ov
        zl = ze - ov * tp
        X0, X1 = x0 - ovg, x1 + ovg
        prev_tag = self.tag
        self.tag = tag
        # slopes (top)
        self.poly([(X0, ye0, zl), (X1, ye0, zl), (X1, ym, zr), (X0, ym, zr)], band, 'metric')
        self.poly([(X1, ye1, zl), (X0, ye1, zl), (X0, ym, zr), (X1, ym, zr)], band, 'metric')
        if lod <= 2:
            th = thick
            # fascia boards along the eaves
            self.poly([(X0, ye0, zl - th), (X1, ye0, zl - th), (X1, ye0, zl), (X0, ye0, zl)], 'planks', 'fit')
            self.poly([(X1, ye1, zl - th), (X0, ye1, zl - th), (X0, ye1, zl), (X1, ye1, zl)], 'planks', 'fit')
            # eave soffits (overhang underside, down to the wall line)
            self.poly([(X0, y0, ze - th), (X1, y0, ze - th), (X1, ye0, zl - th), (X0, ye0, zl - th)], 'planks', 'fit')
            self.poly([(X1, y1, ze - th), (X0, y1, ze - th), (X0, ye1, zl - th), (X1, ye1, zl - th)], 'planks', 'fit')
            # verge edges (barge boards) and verge soffits
            for X, s in ((X0, -1), (X1, 1)):
                for (ya, za), (yb, zb) in (((ye0, zl), (ym, zr)), ((ym, zr), (ye1, zl))):
                    q = [(X, ya, za - th), (X, yb, zb - th), (X, yb, zb), (X, ya, za)]
                    if s < 0:
                        q = [(X, yb, zb - th), (X, ya, za - th), (X, ya, za), (X, yb, zb)]
                    self.poly(q, 'planks', 'fit')
                if lod <= 1:
                    xw = x0 if s < 0 else x1
                    for (ya, za), (yb, zb) in (((ye0, zl), (ym, zr)), ((ym, zr), (ye1, zl))):
                        q = [(X, ya, za - th), (xw, ya, za - th), (xw, yb, zb - th), (X, yb, zb - th)]
                        if s > 0:
                            q = [(xw, ya, za - th), (X, ya, za - th), (X, yb, zb - th), (xw, yb, zb - th)]
                        self.poly(q, 'planks', 'fit')
        else:
            # hull: close the ends with the underside so the roof is a solid wedge
            self.poly([(X0, ye1, zl), (X1, ye1, zl), (X1, ye0, zl), (X0, ye0, zl)], 'planks', 'fit')
            self.poly([(X0, ye0, zl), (X0, ym, zr), (X0, ye1, zl)], 'planks', 'fit')
            self.poly([(X1, ye1, zl), (X1, ym, zr), (X1, ye0, zl)], 'planks', 'fit')
        if ridge_cap and lod <= 1:
            r = 0.13
            self.cyl_along((X0, ym, zr + 0.02), (X1, ym, zr + 0.02), r, 4 if lod == 0 else 3, band, half=True)
        self.tag = 'wall'
        if gable_walls:
            # gable end walls (triangles under the roof)
            self.poly([(x1, y0, ze), (x1, y1, ze), (x1, ym, zr - thick)], gable_band, 'metric', anchor=gable_anchor)
            self.poly([(x0, y1, ze), (x0, y0, ze), (x0, ym, zr - thick)], gable_band, 'metric', anchor=gable_anchor)
        self.tag = prev_tag
        if info is not None:
            info.setdefault('roof_planes', []).extend(_gable_planes(x0, x1, y0, y1, ze, tp, ov, ovg))
            info['ridge_z'] = max(info.get('ridge_z', 0), zr)
        return zr

    def hip_roof(self, x0, x1, y0, y1, ze, pitch, ov=0.4, band='tile', thick=0.22, lod=0, tag='roof', info=None, caps=True):
        tp = math.tan(math.radians(pitch))
        X0, X1, Y0, Y1 = x0 - ov, x1 + ov, y0 - ov, y1 + ov
        zl = ze - ov * tp
        W, D = X1 - X0, Y1 - Y0
        h = min(W, D) * 0.5
        zr = zl + h * tp
        cx, cy = (X0 + X1) * 0.5, (Y0 + Y1) * 0.5
        if W >= D:
            r0, r1 = (cx - (W * 0.5 - h), cy), (cx + (W * 0.5 - h), cy)
        else:
            r0, r1 = (cx, cy - (D * 0.5 - h)), (cx, cy + (D * 0.5 - h))
        R0 = (r0[0], r0[1], zr)
        R1 = (r1[0], r1[1], zr)
        A, B, C, D_ = (X0, Y0, zl), (X1, Y0, zl), (X1, Y1, zl), (X0, Y1, zl)
        prev = self.tag
        self.tag = tag
        if W >= D:
            planes = [[A, B, R1, R0], [B, C, R1], [C, D_, R0, R1], [D_, A, R0]]
        else:
            planes = [[A, B, R0], [B, C, R1, R0], [C, D_, R1], [D_, A, R0, R1]]
        for pl in planes:
            pts = [p for i, p in enumerate(pl) if i == 0 or length(sub(p, pl[i - 1])) > 1e-6]
            self.poly(pts, band, 'metric')
        if lod <= 2:
            th = thick
            ring = [A, B, C, D_]
            for i in range(4):
                a, b = ring[i], ring[(i + 1) % 4]
                self.poly([(a[0], a[1], zl - th), (b[0], b[1], zl - th), b, a], 'planks', 'fit')
            # soffit ring
            wall = [(x0, y0, ze - th), (x1, y0, ze - th), (x1, y1, ze - th), (x0, y1, ze - th)]
            for i in range(4):
                a, b = ring[i], ring[(i + 1) % 4]
                wa, wb = wall[i], wall[(i + 1) % 4]
                self.poly([wa, wb, (b[0], b[1], zl - th), (a[0], a[1], zl - th)], 'planks', 'fit')
        else:
            self.poly([D_, C, B, A], 'planks', 'fit')
        if caps and lod <= 1:
            r = 0.11
            sides = 4 if lod == 0 else 3
            for a, b in ((A, R0 if W >= D else R0), (B, R1 if W >= D else R0), (C, R1), (D_, R0 if W >= D else R1)):
                self.cyl_along((a[0], a[1], a[2] + 0.02), (b[0], b[1], b[2] + 0.02), r, sides, band, half=True)
            if length(sub(R0, R1)) > 0.05:
                self.cyl_along((R0[0], R0[1], zr + 0.02), (R1[0], R1[1], zr + 0.02), r * 1.2, sides, band, half=True)
        self.tag = prev
        if info is not None:
            info.setdefault('roof_planes', []).extend([[A, B, R1 if W >= D else R0, R0], [C, D_, R0 if W >= D else R1, R1]])
            info['ridge_z'] = max(info.get('ridge_z', 0), zr)
        return zr

    def cyl_along(self, a, b, r, sides, band, half=False, tag=None, cell=None, caps=False):
        """Round/half-round section from a to b (ridge caps, ropes, poles, rails)."""
        ax = norm(sub(b, a))
        ref = (0.0, 0.0, 1.0) if abs(ax[2]) < 0.9 else (1.0, 0.0, 0.0)
        s = norm(cross(ax, ref))
        u = norm(cross(s, ax))
        L = length(sub(b, a))
        if cell:
            cu0, cv0, cu1, cv1 = cell_uv(cell)
            slot = SLOT['Detail']
            wm = 1.0
        else:
            sl, cv0, cv1, hm, wm = BANDS[band]
            slot = SLOT[sl]
            cv0, cv1 = cv0 + INSET, cv1 - INSET
            cu0, cu1 = 0.0, L / wm
        ang0, ang1 = (0.0, math.pi) if half else (0.0, 2 * math.pi)
        n = sides
        for j in range(n):
            t0 = ang0 + (ang1 - ang0) * j / n
            t1 = ang0 + (ang1 - ang0) * (j + 1) / n
            d0 = add(mul(s, math.cos(t0) * r), mul(u, math.sin(t0) * r))
            d1 = add(mul(s, math.cos(t1) * r), mul(u, math.sin(t1) * r))
            q = [add(a, d0), add(b, d0), add(b, d1), add(a, d1)]
            # orientation: outward = d direction
            fa, fb = j / n, (j + 1) / n
            if cell:
                uvs = [(cu0, cv0 + (cv1 - cv0) * fa), (cu1, cv0 + (cv1 - cv0) * fa), (cu1, cv0 + (cv1 - cv0) * fb), (cu0, cv0 + (cv1 - cv0) * fb)]
            else:
                uvs = [(0.0, cv0 + (cv1 - cv0) * fa), (cu1, cv0 + (cv1 - cv0) * fa), (cu1, cv0 + (cv1 - cv0) * fb), (0.0, cv0 + (cv1 - cv0) * fb)]
            nrm = newell(q)
            mid = add(mul(add(d0, d1), 0.5), (0, 0, 0))
            if dot(nrm, mid) < 0:
                q = [q[1], q[0], q[3], q[2]]
                uvs = [uvs[1], uvs[0], uvs[3], uvs[2]]
            self.face(q, slot, uvs, tag)
        if caps and not half:
            ring_a = [add(a, add(mul(s, math.cos(2 * math.pi * j / n) * r), mul(u, math.sin(2 * math.pi * j / n) * r))) for j in range(n)]
            ring_b = [add(b, add(mul(s, math.cos(2 * math.pi * j / n) * r), mul(u, math.sin(2 * math.pi * j / n) * r))) for j in range(n)]
            for ring, out in ((ring_a, mul(ax, -1)), (ring_b, ax)):
                pts = ring if dot(newell(ring), out) > 0 else list(reversed(ring))
                if cell:
                    self.apoly(pts, cell, tag=tag)
                else:
                    self.poly(pts, band, 'fit', tag=tag)

    def prism(self, pts2, z0, z1, band, top_band=None, mode='metric', anchor=None, tag=None, top=True, bottom=False):
        """Vertical extrusion of a CCW footprint polygon."""
        n = len(pts2)
        acc = 0.0
        for i in range(n):
            a, b = pts2[i], pts2[(i + 1) % n]
            L = math.hypot(b[0] - a[0], b[1] - a[1])
            self.poly([(a[0], a[1], z0), (b[0], b[1], z0), (b[0], b[1], z1), (a[0], a[1], z1)], band, mode,
                      anchor if anchor is not None else z0, tag=tag)
            acc += L
        if top:
            self.poly([(p[0], p[1], z1) for p in pts2], top_band or band, 'metric' if mode != 'fit' else 'fit', tag=tag)
        if bottom:
            self.poly([(p[0], p[1], z0) for p in reversed(pts2)], band, 'fit', tag=tag)


def _gable_planes(x0, x1, y0, y1, ze, tp, ov, ovg):
    hd = (y1 - y0) * 0.5
    ym = (y0 + y1) * 0.5
    zr = ze + hd * tp
    zl = ze - ov * tp
    X0, X1 = x0 - ovg, x1 + ovg
    return [[(X0, y0 - ov, zl), (X1, y0 - ov, zl), (X1, ym, zr), (X0, ym, zr)],
            [(X1, y1 + ov, zl), (X0, y1 + ov, zl), (X0, ym, zr), (X1, ym, zr)]]


def rect_path(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
