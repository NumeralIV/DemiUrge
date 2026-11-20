# DEMIURGE art - Age6 geometry kit: parts, primitives, UV skins, damage states, merge to mesh.
#
# An asset is a list of Parts. Each Part is a small closed bmesh with a kind:
#   body   - massing; cut by state planes and the cut is capped (filled)
#   shrub  - planting; dropped when above a cut, thinned in damaged states
#   detail - thin things (beams, panels, antennas); dropped when their centre is cut away
#   ground - aprons, roads; never cut
#   rubble - added by the states
# UVs are written per loop at creation by a Skin (which material slot + how to map).
import math
import random
import bmesh
from mathutils import Vector, Matrix

UP = Vector((0, 0, 1))


# --------------------------------------------------------------------------- skins (material slot + UV rule)


def _soft(t, k=100.0):
    return t / (abs(t) + k)


class Skin:
    """mode: 'tile'   side (s/T, z/T), top (x/T, y/T)
             'facade' side (s/T, z/T), top/bottom pinned to a solid panel texel
             'point'  every vertex pinned near (u0, v0) with a small spread (flat colour regions)
             'region' like point, centred in an atlas region with a spread of 35 % of it"""

    def __init__(self, mat, mode, tile=1.0, pin=(0.5, 0.5), spread=(0.01, 0.01)):
        self.mat, self.mode, self.tile = mat, mode, tile
        self.pin, self.spread = pin, spread

    def side(self, s, z, x, y):
        if self.mode in ('tile', 'facade'):
            return (s / self.tile, z / self.tile)
        return (self.pin[0] + self.spread[0] * _soft(s + x), self.pin[1] + self.spread[1] * _soft(z + y))

    def top(self, x, y):
        if self.mode == 'tile':
            return (x / self.tile, y / self.tile)
        return (self.pin[0] + self.spread[0] * _soft(x), self.pin[1] + self.spread[1] * _soft(y))


def region_skin(mat, reg):
    u0, v0, u1, v1 = reg
    return Skin(mat, 'region', 1.0, ((u0 + u1) * 0.5, (v0 + v1) * 0.5), ((u1 - u0) * 0.35, (v1 - v0) * 0.35))


# --------------------------------------------------------------------------- parts


class Part:
    def __init__(self, kind='body', tag=''):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new('UVMap')
        self.kind = kind
        self.tag = tag
        self.cap = None          # Skin for caps made by cuts
        self.char = 0.0          # global darkening 0..1
        self.burn = []           # (co, no) planes: darken near them
        self.scorch = []         # (x, y, radius, strength) soft discs
        self.smooth = True

    def face(self, verts, mat, uvs):
        try:
            f = self.bm.faces.new(verts)
        except ValueError:
            return None
        f.material_index = mat
        for l, uv in zip(f.loops, uvs):
            l[self.uv].uv = uv
        return f

    def transform(self, M):
        bmesh.ops.transform(self.bm, matrix=M, verts=self.bm.verts)
        return self

    def move(self, x, y, z):
        return self.transform(Matrix.Translation((x, y, z)))

    def rotz(self, a):
        return self.transform(Matrix.Rotation(a, 4, 'Z'))

    def bounds(self):
        if not self.bm.verts:
            return Vector((0, 0, 0)), Vector((0, 0, 0))
        xs = [v.co for v in self.bm.verts]
        lo = Vector((min(c.x for c in xs), min(c.y for c in xs), min(c.z for c in xs)))
        hi = Vector((max(c.x for c in xs), max(c.y for c in xs), max(c.z for c in xs)))
        return lo, hi

    def centre(self):
        lo, hi = self.bounds()
        return (lo + hi) * 0.5

    def ntris(self):
        return sum(len(f.verts) - 2 for f in self.bm.faces)

    def copy(self):
        p = Part(self.kind, self.tag)
        p.bm.free()
        p.bm = self.bm.copy()
        p.uv = p.bm.loops.layers.uv['UVMap']
        p.cap, p.char, p.burn, p.smooth, p.scorch = self.cap, self.char, list(self.burn), self.smooth, list(self.scorch)
        return p


# --------------------------------------------------------------------------- 2D profiles (CCW)


def dedupe(pts, eps=1e-4):
    out = []
    for p in pts:
        if not out or (abs(p[0] - out[-1][0]) > eps or abs(p[1] - out[-1][1]) > eps):
            out.append(p)
    if len(out) > 1 and abs(out[0][0] - out[-1][0]) < eps and abs(out[0][1] - out[-1][1]) < eps:
        out.pop()
    return out


def rrect(w, d, r, seg):
    hw, hd = w * 0.5, d * 0.5
    r = max(0.0, min(r, hw - 1e-3, hd - 1e-3))
    if seg <= 0 or r <= 1e-3:
        return [(hw, -hd), (hw, hd), (-hw, hd), (-hw, -hd)]
    pts = []
    for cx, cy, a0 in ((hw - r, -hd + r, -90), (hw - r, hd - r, 0), (-hw + r, hd - r, 90), (-hw + r, -hd + r, 180)):
        for k in range(seg + 1):
            a = math.radians(a0 + 90.0 * k / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return dedupe(pts)


def circle(r, n, a0=0.0):
    return [(r * math.cos(a0 + 2 * math.pi * k / n), r * math.sin(a0 + 2 * math.pi * k / n)) for k in range(n)]


def sector(r0, r1, a0, a1, n):
    outer = [(r1 * math.cos(a0 + (a1 - a0) * k / n), r1 * math.sin(a0 + (a1 - a0) * k / n)) for k in range(n + 1)]
    inner = [(r0 * math.cos(a1 - (a1 - a0) * k / n), r0 * math.sin(a1 - (a1 - a0) * k / n)) for k in range(n + 1)]
    return outer + inner


def scale_profile(pts, sx, sy=None):
    sy = sx if sy is None else sy
    return [(x * sx, y * sy) for x, y in pts]


def offset_profile(pts, d):
    """Offset a convex-ish CCW profile outward by d (inward when d < 0) using vertex normals."""
    n = len(pts)
    out = []
    for i in range(n):
        p0, p1, p2 = Vector(pts[i - 1]), Vector(pts[i]), Vector(pts[(i + 1) % n])
        e0 = (p1 - p0).normalized()
        e1 = (p2 - p1).normalized()
        n0 = Vector((e0.y, -e0.x))
        n1 = Vector((e1.y, -e1.x))
        nn = (n0 + n1)
        if nn.length < 1e-6:
            nn = n1
        nn.normalize()
        c = max(0.35, nn.dot(n1))
        q = p1 + nn * (d / c)
        out.append((q.x, q.y))
    return out


def perimeter_points(pts, spacing, jitter=0.0, rng=None):
    """Points along a closed profile, every `spacing` metres, with outward normals."""
    n = len(pts)
    segs = []
    tot = 0.0
    for i in range(n):
        a, b = Vector(pts[i]), Vector(pts[(i + 1) % n])
        L = (b - a).length
        segs.append((a, b, L, tot))
        tot += L
    cnt = max(1, int(tot / spacing))
    out = []
    for k in range(cnt):
        s = (k + 0.5) * tot / cnt
        if rng and jitter:
            s += rng.uniform(-jitter, jitter) * spacing
        s %= tot
        for a, b, L, s0 in segs:
            if s0 <= s <= s0 + L + 1e-9:
                t = (s - s0) / max(L, 1e-9)
                p = a.lerp(b, t)
                e = (b - a).normalized()
                out.append((p, Vector((e.y, -e.x))))
                break
    return out


# --------------------------------------------------------------------------- 3D primitives


def prism(profile, z0, z1, side, top=None, bot=None, kind='body', tag='', profile_top=None, cap_top=True,
          cap_bot=True, smooth=True):
    """Extrude a CCW profile from z0 to z1. profile_top (same count) lofts to a different top outline."""
    profile = dedupe(profile)
    pt = dedupe(profile_top) if profile_top else profile
    assert len(pt) == len(profile)
    top = top or side
    bot = bot or side
    p = Part(kind, tag)
    p.smooth = smooth
    bm = p.bm
    n = len(profile)
    vb = [bm.verts.new((x, y, z0)) for x, y in profile]
    vt = [bm.verts.new((x, y, z1)) for x, y in pt]
    s = 0.0
    for i in range(n):
        j = (i + 1) % n
        L = (Vector(profile[j]) - Vector(profile[i])).length
        x0, y0 = profile[i]
        x1, y1 = profile[j]
        uvs = [side.side(s, z0, x0, y0), side.side(s + L, z0, x1, y1), side.side(s + L, z1, x1, y1), side.side(s, z1, x0, y0)]
        p.face([vb[i], vb[j], vt[j], vt[i]], side.mat, uvs)
        s += L
    if cap_top:
        p.face(vt, top.mat, [top.top(v.co.x, v.co.y) for v in vt])
    if cap_bot:
        rb = list(reversed(vb))
        p.face(rb, bot.mat, [bot.top(v.co.x, v.co.y) for v in rb])
    return p


def box(cx, cy, z0, w, d, h, side, top=None, rot=0.0, kind='body', tag='', bot=None):
    p = prism(rrect(w, d, 0, 0), z0, z0 + h, side, top, bot, kind, tag, smooth=False)
    if rot:
        p.rotz(rot)
    return p.move(cx, cy, 0)


def lathe(rz, n, skin, kind='body', tag='', r_ref=None, skins=None):
    """rz: [(r, z), ...] bottom to top. skins: optional per-band skin list (len(rz) - 1)."""
    p = Part(kind, tag)
    bm = p.bm
    r_ref = r_ref or max(r for r, z in rz)
    rings = []
    for r, z in rz:
        if r < 1e-4:
            rings.append([bm.verts.new((0, 0, z))])
        else:
            rings.append([bm.verts.new((r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n), z)) for i in range(n)])
    vacc = [0.0]
    for k in range(1, len(rz)):
        vacc.append(vacc[-1] + math.hypot(rz[k][0] - rz[k - 1][0], rz[k][1] - rz[k - 1][1]))
    for k in range(len(rz) - 1):
        sk = skins[k] if skins else skin
        A, B = rings[k], rings[k + 1]
        v0, v1 = vacc[k], vacc[k + 1]
        for i in range(n):
            j = (i + 1) % n
            u0 = (2 * math.pi * i / n) * r_ref
            u1 = (2 * math.pi * (i + 1) / n) * r_ref
            c0 = sk.side(u0, v0, 0, 0)
            c1 = sk.side(u1, v0, 0, 0)
            c2 = sk.side(u1, v1, 0, 0)
            c3 = sk.side(u0, v1, 0, 0)
            if len(A) == 1:
                p.face([A[0], B[j], B[i]], sk.mat, [((c0[0] + c1[0]) * 0.5, c0[1]), c2, c3][::1] if False else [((c0[0] + c1[0]) * 0.5, c0[1]), c2, c3])
            elif len(B) == 1:
                p.face([A[i], A[j], B[0]], sk.mat, [c0, c1, ((c2[0] + c3[0]) * 0.5, c2[1])])
            else:
                p.face([A[i], A[j], B[j], B[i]], sk.mat, [c0, c1, c2, c3])
    if len(rings[0]) > 1:
        rb = list(reversed(rings[0]))
        p.face(rb, skin.mat, [skin.top(v.co.x, v.co.y) for v in rb])
    if len(rings[-1]) > 1:
        sk = skins[-1] if skins else skin
        p.face(rings[-1], sk.mat, [sk.top(v.co.x, v.co.y) for v in rings[-1]])
    return p


def sphere(cx, cy, cz, r, n, rings, skin, kind='body', tag=''):
    rz = [(0.0, -r)]
    for k in range(1, rings):
        a = -math.pi / 2 + math.pi * k / rings
        rz.append((r * math.cos(a), r * math.sin(a)))
    rz.append((0.0, r))
    return lathe(rz, n, skin, kind, tag).move(cx, cy, cz)


def beam(p0, p1, w, h, skin, kind='detail', tag='', caps=True):
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    L = d.length
    if L < 1e-4:
        return None
    part = prism(rrect(w, h, 0, 0), 0.0, L, skin, kind=kind, tag=tag, cap_top=caps, cap_bot=caps, smooth=False)
    dirv = d / L
    ref = UP if abs(dirv.z) < 0.95 else Vector((1, 0, 0))
    side = dirv.cross(ref).normalized()
    up2 = side.cross(dirv).normalized()
    M = Matrix((side, up2, dirv)).transposed().to_4x4()
    M.translation = p0
    part.transform(M)
    if M.determinant() < 0:
        # (side, up2, dir) is a left-handed frame: the transform mirrors the box, so flip the winding back
        # (verification 2026-11-19: every beam was inside-out before this).
        bmesh.ops.reverse_faces(part.bm, faces=part.bm.faces[:])
    return part


def blob(c, r, rng, skin, subdiv=1, squash=0.8, kind='shrub', tag=''):
    p = Part(kind, tag)
    # subdiv 1 (LOD0 planting) -> 80-face sphere, 0 (LOD1, rubble) -> 20-face icosahedron. The 20-face
    # version at LOD0 read as faceted low-poly trees at 40 m (verification 2026-11-19).
    bmesh.ops.create_icosphere(p.bm, subdivisions=2 if subdiv >= 1 else 1, radius=r)
    for v in p.bm.verts:
        k = rng.uniform(0.82, 1.15)
        v.co = Vector((v.co.x * k, v.co.y * k, v.co.z * k * squash))
        if v.co.z < -r * 0.25 * squash:
            v.co.z = -r * 0.25 * squash
    for f in p.bm.faces:
        f.material_index = skin.mat
        for l in f.loops:
            co = l.vert.co
            l[p.uv].uv = skin.top(co.x + c[0], co.y + c[1] + co.z)
    return p.move(c[0], c[1], c[2] + r * 0.2 * squash)


def hedge(samples, closed, z, width, height, lump, rng, skin, kind='shrub', tag=''):
    """A lumpy planted band (or a planter box when lump = 0) along samples [(point, outward normal)].
    The outer edge sits on the samples; the band runs `width` inward."""
    p = Part(kind, tag)
    bm = p.bm
    m = len(samples)
    if m < 2:
        return None
    ob, ot, it, ib = [], [], [], []
    for q, n in samples:
        o = q + n * (rng.uniform(0.0, lump * 0.35) if lump else 0.0)
        i = q - n * width
        h = z + height * (1.0 + (rng.uniform(-lump, lump) if lump else 0.0))
        hi = z + height * (1.0 + (rng.uniform(-lump, lump) * 0.6 if lump else 0.0))
        ob.append(bm.verts.new((o.x, o.y, z)))
        ot.append(bm.verts.new((o.x, o.y, h)))
        it.append(bm.verts.new((i.x, i.y, hi)))
        ib.append(bm.verts.new((i.x, i.y, z)))

    acc = [0.0]
    for k in range(1, m + 1):
        acc.append(acc[-1] + (samples[k % m][0] - samples[k - 1][0]).length)

    def uv(v):
        return skin.top(v.co.x + v.co.z * 0.5, v.co.y + v.co.z * 0.5)
    rng_k = range(m) if closed else range(m - 1)
    for k in rng_k:
        j = (k + 1) % m
        s0, s1 = acc[k], acc[k + 1]
        for ring in ((ob, ot), (it, ib)):
            a, c = ring
            quad = (a[k], a[j], c[j], c[k])
            p.face(list(quad), skin.mat, [skin.side(s0, a[k].co.z, 0, 0), skin.side(s1, a[j].co.z, 0, 0),
                                          skin.side(s1, c[j].co.z, 0, 0), skin.side(s0, c[k].co.z, 0, 0)])
        for quad in ((ot[k], ot[j], it[j], it[k]), (ib[k], ib[j], ob[j], ob[k])):
            p.face(list(quad), skin.mat, [uv(v) for v in quad])
    if not closed:
        for quad in ((ob[0], ot[0], it[0], ib[0]), (ob[-1], ib[-1], it[-1], ot[-1])):
            p.face(list(quad), skin.mat, [uv(v) for v in quad])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return p


def line_samples(a, b, spacing, normal):
    a, b = Vector(a), Vector(b)
    n = max(1, int((b - a).length / spacing))
    return [(a.lerp(b, k / n), normal.copy()) for k in range(n + 1)]


def sweep(path, profile, skin, kind='body', tag='', caps=True):
    """path: list of Vector points; profile: CCW list of (lateral, up) offsets. Frames keep Z up."""
    p = Part(kind, tag)
    bm = p.bm
    rings = []
    along = [0.0]
    for k in range(1, len(path)):
        along.append(along[-1] + (path[k] - path[k - 1]).length)
    for k, P in enumerate(path):
        if k == 0:
            t = path[1] - path[0]
        elif k == len(path) - 1:
            t = path[-1] - path[-2]
        else:
            t = path[k + 1] - path[k - 1]
        t.z = 0
        t.normalize()
        lat = UP.cross(t).normalized()      # left of travel
        rings.append([bm.verts.new(P + lat * a + UP * b) for a, b in profile])
    n = len(profile)
    perim = [0.0]
    for i in range(n):
        perim.append(perim[-1] + (Vector(profile[(i + 1) % n]) - Vector(profile[i])).length)
    for k in range(len(path) - 1):
        A, B = rings[k], rings[k + 1]
        for i in range(n):
            j = (i + 1) % n
            uvs = [skin.side(along[k], perim[i], 0, 0), skin.side(along[k], perim[i + 1], 0, 0),
                   skin.side(along[k + 1], perim[i + 1], 0, 0), skin.side(along[k + 1], perim[i], 0, 0)]
            p.face([A[i], A[j], B[j], B[i]], skin.mat, uvs)
    if caps:
        p.face(list(reversed(rings[0])), skin.mat, [skin.top(v.co.x, v.co.y + v.co.z) for v in reversed(rings[0])])
        p.face(rings[-1], skin.mat, [skin.top(v.co.x, v.co.y + v.co.z) for v in rings[-1]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return p


# --------------------------------------------------------------------------- cutting


def side_of(part, co, no):
    ds = [(v.co - co).dot(no) for v in part.bm.verts]
    if not ds:
        return 'empty'
    if max(ds) <= 1e-4:
        return 'in'
    if min(ds) >= -1e-4:
        return 'out'
    return 'both'


def cut(part, co, no, cap_skin):
    """Remove the side of `part` the normal points to; cap the hole. Returns the part or None."""
    s = side_of(part, co, no)
    if s == 'in':
        return part
    if s in ('out', 'empty'):
        return None
    bm = part.bm
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-4, plane_co=co, plane_no=no, clear_outer=True)
    if not bm.faces:
        return None
    bnd = [e for e in bm.edges if e.is_boundary]
    if bnd:
        res = bmesh.ops.holes_fill(bm, edges=bnd, sides=0)
        sk = part.cap or cap_skin
        for f in res.get('faces', []):
            f.material_index = sk.mat
            for l in f.loops:
                co2 = l.vert.co
                l[part.uv].uv = sk.top(co2.x + co2.z * 0.3, co2.y + co2.z * 0.7)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return part


# --------------------------------------------------------------------------- states


class StateCtx:
    def __init__(self, info, skins, lod, seed):
        self.info = info
        self.skins = skins
        self.lod = lod
        self.rng = random.Random(seed)


def _hdir(rng):
    a = rng.uniform(0, 2 * math.pi)
    return Vector((math.cos(a), math.sin(a), 0.0))


def rubble(parts, ctx, centre, spread_dir, count, rmin, rmax, radius, ring=False, burnt=0.5):
    rng = ctx.rng
    skin = ctx.skins['rubble']
    for k in range(count):
        if ring:
            a = rng.uniform(0, 2 * math.pi)
            rr = radius * rng.uniform(0.75, 1.25)
            pos = centre + Vector((math.cos(a) * rr * ctx.info['w'] / max(ctx.info['w'], ctx.info['d']),
                                   math.sin(a) * rr * ctx.info['d'] / max(ctx.info['w'], ctx.info['d']), 0))
        else:
            perp = Vector((-spread_dir.y, spread_dir.x, 0))
            pos = centre + spread_dir * radius * rng.uniform(0.85, 1.6) + perp * radius * rng.uniform(-0.9, 0.9)
        r = rng.uniform(rmin, rmax)
        sq = rng.uniform(0.35, 0.7)
        b = blob((pos.x, pos.y, -0.3 + 0.05 * r * sq + 0.01), r, rng, skin, subdiv=0, squash=sq, kind='rubble')
        b.rotz(0)
        b.char = burnt * rng.uniform(0.7, 1.1)
        b.smooth = False
        parts.append(b)


def scaffold(parts, ctx, lo, hi, z0, z1):
    sk = ctx.skins['steel']
    lod = ctx.lod
    if lod >= 3:
        return
    m = 1.2
    x0, y0, x1, y1 = lo.x - m, lo.y - m, hi.x + m, hi.y + m
    step = 6.0 if lod == 0 else 12.0
    pts = []
    for x0_, y0_, x1_, y1_ in ((x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)):
        L = math.hypot(x1_ - x0_, y1_ - y0_)
        n = max(1, int(L / step))
        for k in range(n):
            t = k / n
            pts.append((x0_ + (x1_ - x0_) * t, y0_ + (y1_ - y0_) * t))
    for x, y in pts:
        parts.append(beam((x, y, z0), (x, y, z1), 0.25, 0.25, sk))
    dz = 1.8 if lod == 0 else 3.6
    z = z0 + dz
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    while z < z1:
        for i in range(4):
            a, b = corners[i], corners[(i + 1) % 4]
            parts.append(beam((a[0], a[1], z), (b[0], b[1], z), 0.2, 0.2, sk))
        z += dz


def crane(parts, ctx, x, y, top, jib_dir, jib_len):
    sk_o = ctx.skins['orange']
    sk_s = ctx.skins['steel']
    lod = ctx.lod
    mast = box(x, y, -0.3, 2.0, 2.0, top + 0.3, sk_o, kind='detail', tag='crane')
    parts.append(mast)
    d = Vector((math.cos(jib_dir), math.sin(jib_dir), 0))
    base = Vector((x, y, top))
    parts.append(beam(base, base + d * jib_len, 1.4, 1.6, sk_o, kind='detail', caps=True))
    parts.append(beam(base, base - d * jib_len * 0.3, 1.4, 1.4, sk_o, kind='detail', caps=True))
    cw = base - d * jib_len * 0.27
    parts.append(box(cw.x, cw.y, top - 2.5, 3.0, 3.0, 2.5, ctx.skins['rubble'], kind='detail'))
    parts.append(box(x, y, top + 1.6, 1.2, 1.2, 5.0, sk_o, kind='detail'))
    if lod <= 1:
        hook = base + d * jib_len * 0.6
        parts.append(beam(hook, hook - UP * min(top * 0.4, 25.0), 0.12, 0.12, sk_s))


def _drop_or_cut(parts, planes, cap):
    out = []
    for p in parts:
        if p.kind == 'ground':
            out.append(p)
            continue
        if p.kind == 'body':
            q = p
            for co, no in planes:
                if q is None:
                    break
                q = cut(q, co, no, cap)
            if q is not None:
                q.burn.extend([(co, no) for co, no in planes])
                out.append(q)
        else:
            c = p.centre()
            if all((c - co).dot(no) <= 0 for co, no in planes):
                out.append(p)
    return out


def debris_on_cuts(parts, ctx, planes, count):
    """Broken chunks sitting on the cut rims, so a cut reads as a jagged break, not a clean roof."""
    rng = ctx.rng
    rim = []
    for p in parts:
        if p.kind != 'body':
            continue
        lo, hi = p.bounds()
        c = (lo + hi) * 0.5
        for v in p.bm.verts:
            for co, no in planes:
                if abs((v.co - co).dot(no)) < 0.05:
                    rim.append((v.co.copy(), c))
                    break
    if not rim:
        return
    sk = ctx.skins['rubble']
    for _ in range(count):
        q, c = rim[rng.randrange(len(rim))]
        d = Vector((c.x - q.x, c.y - q.y, 0))
        if d.length > 1e-3:
            d.normalize()
        r = rng.uniform(0.9, 2.4)
        pos = q + d * r * 0.6
        sq = rng.uniform(0.5, 1.0)
        b = blob((pos.x, pos.y, max(pos.z - r * 0.25, -0.3 + 0.05 * r * sq + 0.01)), r, rng, sk, subdiv=0, squash=sq, kind='rubble')
        b.char = rng.uniform(0.3, 0.6)
        b.smooth = False
        parts.append(b)


def apply_state(parts, state, ctx):
    info = ctx.info
    style = info.get('style', 'tall')
    if state == 'Intact':
        return parts
    cap = ctx.skins['cap']
    rng = ctx.rng
    W, D, H = info['w'], info['d'], info['hbody']
    R = 0.5 * max(W, D)
    C = Vector((info.get('cx', 0.0), info.get('cy', 0.0), 0))
    nrub = {0: 1.0, 1: 0.45, 2: 0.18, 3: 0.08}[ctx.lod]
    if style == 'flat':
        nrub *= min(1.0, 0.5 * max(W, D) / 40.0)
    if style == 'tall':
        if state == 'Construction':
            hc = max(3.6, round(H * info.get('build_frac', 0.55) / 3.6) * 3.6) + 0.02
            planes = [(Vector((0, 0, hc)), UP.copy())]
            parts = [p for p in parts if p.kind != 'shrub']
            parts = _drop_or_cut(parts, planes, cap)
            for p in parts:
                p.burn = []
                if p.kind == 'body':
                    _unglaze(p, hc - 7.2, cap)
            pts = [v.co for p in parts if p.kind == 'body' for v in p.bm.verts if v.co.z > hc - 7.4]
            if pts:
                lo = Vector((min(c.x for c in pts), min(c.y for c in pts), 0))
                hi = Vector((max(c.x for c in pts), max(c.y for c in pts), 0))
                scaffold(parts, ctx, lo, hi, max(0.0, hc - 7.2), hc + 1.8)
                ext = max(hi.x - lo.x, hi.y - lo.y)
                crane(parts, ctx, hi.x + 5.0, hi.y + 5.0, hc + 18.0, math.radians(200), max(28.0, ext * 0.9))
            rubble(parts, ctx, C, _hdir(rng), max(1, int(8 * nrub)), 0.8, 2.0, R + 6, ring=True, burnt=0.0)
            return parts
        d = _hdir(rng)
        perp = Vector((-d.y, d.x, 0))
        if state == 'Damaged':
            planes = [(C + d * R * 0.25 + UP * H * 0.72, (d * 0.6 + UP).normalized()),
                      (C - d * R * 0.2 + perp * R * 0.45 + UP * H * 0.86, (-d * 0.35 + perp * 0.6 + UP).normalized())]
            parts = [p for p in parts if not (p.kind == 'shrub' and rng.random() < 0.35)]
            parts = _drop_or_cut(parts, planes, cap)
            for p in parts:
                p.char = max(p.char, 0.08)
            debris_on_cuts(parts, ctx, planes, max(2, int(26 * nrub)))
            rubble(parts, ctx, C, d, max(2, int(36 * nrub)), 0.8, 3.2, R)
            return parts
        if state == 'Ruin':
            planes = [(C + UP * H * 0.34 + d * R * 0.2, (d * 0.45 + UP).normalized()),
                      (C + UP * H * 0.26 - d * R * 0.3 + perp * R * 0.2, (-d * 0.4 + perp * 0.3 + UP).normalized())]
            parts = [p for p in parts if not (p.kind == 'shrub' and rng.random() < 0.6)]
            parts = [p for p in parts if not (p.kind == 'detail' and p.tag != 'keep' and rng.random() < 0.5)]
            parts = _drop_or_cut(parts, planes, cap)
            for p in parts:
                if p.kind != 'shrub':
                    p.char = max(p.char, 0.3)
            debris_on_cuts(parts, ctx, planes, max(2, int(22 * nrub)))
            rubble(parts, ctx, C, d, max(3, int(70 * nrub)), 1.0, 4.5, R * 0.95, ring=True, burnt=0.6)
            return parts
    else:  # flat
        xmid = info.get('cx', 0.0)
        if state == 'Construction':
            planes = [(Vector((xmid + W * 0.08, 0, 0)), Vector((1, 0, 0)))]
            out = []
            for p in parts:
                if p.tag == 'base' or p.kind == 'ground':
                    out.append(p)
                elif p.kind == 'body':
                    q = cut(p, planes[0][0], planes[0][1], cap)
                    if q:
                        out.append(q)
                elif p.centre().x < planes[0][0].x:
                    out.append(p)
            out = [p for p in out if p.kind != 'shrub']
            rubble(out, ctx, C + Vector((W * 0.3, 0, 0)), Vector((1, 0, 0)), max(1, int(10 * nrub)), 0.6, 1.6, R * 0.3, burnt=0.0)
            return out
        if state == 'Damaged':
            g = C + Vector((W * rng.choice((-0.25, 0.25)), 0, 0))
            out = []
            for p in parts:
                if p.kind == 'ground' or p.tag == 'base':
                    out.append(p)
                    continue
                if p.kind == 'body':
                    a = cut(p.copy(), g + Vector((3.5, 0, 0)) * 0 - Vector((3.5, 0, 0)), Vector((1, 0, 0)), cap)
                    b = cut(p, g + Vector((3.5, 0, 0)), Vector((-1, 0, 0)), cap)
                    for q in (a, b):
                        if q:
                            q.burn.append((g, Vector((1, 0, 0))))
                            out.append(q)
                elif abs(p.centre().x - g.x) > 6.0 and rng.random() > 0.3:
                    out.append(p)
            for p in out:
                p.burn.append((g, Vector((1, 0, 0))))
                p.char = max(p.char, 0.05)
            rubble(out, ctx, g, Vector((0, 1, 0)), max(2, int(26 * nrub)), 0.6, 2.4, R * 0.25, ring=False)
            return out
        if state == 'Ruin':
            out = []
            hk = info.get('ruin_h', 1.0)
            for p in parts:
                if p.kind == 'ground':
                    out.append(p)
                elif p.tag == 'base':
                    q = cut(p, Vector((0, 0, hk)) + _hdir(rng) * 0.5, (UP + _hdir(rng) * 0.3).normalized(), cap)
                    if q:
                        out.append(q)
                elif p.kind == 'detail' and rng.random() < 0.12:
                    out.append(p)
            for p in out:
                p.char = max(p.char, 0.35)
            rubble(out, ctx, C, Vector((1, 0, 0)), max(3, int(50 * nrub)), 0.6, 2.6, R * 0.8, ring=True, burnt=0.6)
            return out
    return parts


def _unglaze(part, zmin, cap):
    """Construction: storeys above zmin show the bare concrete frame (no glass yet)."""
    part.bm.normal_update()
    for f in part.bm.faces:
        if f.material_index != cap.mat:
            continue
        c = f.calc_center_median()
        if c.z > zmin and abs(f.normal.z) < 0.5:
            for l in f.loops:
                co = l.vert.co
                l[part.uv].uv = cap.top(co.x + co.y, co.z)


# --------------------------------------------------------------------------- merge


def vertex_shade(part, co):
    c = 1.0 - part.char
    if part.burn:
        h = math.sin(co.x * 12.9898 + co.y * 78.233 + co.z * 37.719) * 43758.5453
        nz = 0.45 + 0.55 * (h - math.floor(h))
        for pco, pno in part.burn:
            d = abs((co - pco).dot(pno))
            c *= 1.0 - 0.62 * nz * math.exp(-d / 4.0)
    for x, y, r, k in part.scorch:
        d2 = ((co.x - x) ** 2 + (co.y - y) ** 2) / (r * r)
        c *= 1.0 - k * math.exp(-d2)
    return max(0.12, min(1.0, c))


def merge(parts, me, mat_count=3):
    """Write parts into mesh `me` (triangulated). Returns the list of used material slots."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new('UVMap')
    col = bm.loops.layers.color.new('Col')
    smooth_faces = []
    for p in parts:
        if p is None or not p.bm.faces:
            continue
        suv = p.bm.loops.layers.uv['UVMap']
        vmap = {}
        shade = {}
        for v in p.bm.verts:
            nv = bm.verts.new(v.co)
            vmap[v] = nv
            shade[v] = vertex_shade(p, v.co)
        for f in p.bm.faces:
            try:
                nf = bm.faces.new([vmap[v] for v in f.verts])
            except ValueError:
                continue
            nf.material_index = f.material_index
            nf.smooth = p.smooth
            for ls, ld in zip(f.loops, nf.loops):
                ld[uv].uv = ls[suv].uv
                s = shade[ls.vert]
                ld[col] = (s, s, s, 1.0)
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    used = sorted({f.material_index for f in bm.faces})
    remap = {m: i for i, m in enumerate(used)}
    for f in bm.faces:
        f.material_index = remap[f.material_index]
    bm.to_mesh(me)
    bm.free()
    return used
