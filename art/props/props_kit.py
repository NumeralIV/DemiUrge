# DEMIURGE props - generators for the built props (everything but rocks and cliffs).
#
# Each generator is gen(lod, rng) -> Part. Face tags are 'Role:Tex': the role picks the material slot
# (Structure, Roof, Detail), the texture key sets the UV scale (metres per tile). Wood is modelled with its
# grain along the local +Z of each piece (beam / pole helpers orient it). Byte vertex colours can only darken,
# so per-piece variation tints sit in 0.75..1.0.
import math

from mathutils import Matrix, Quaternion, Vector, noise

import props_lib as L
from props_lib import Part, merge, box, bbox, cyl, lathe, tube, card, hcard, lump, W

WOOD, WOODR, WOODD = 'Structure:Wood', 'Roof:Wood', 'Detail:Wood'
METAL, METALD, METALR = 'Structure:Metal', 'Detail:Metal', 'Roof:Metal'
STONE, STONED, STONER = 'Structure:Stone', 'Detail:Stone', 'Roof:Stone'
ROCKT = 'Structure:Rock'
STRAW, STRAWR = 'Structure:Straw', 'Roof:Straw'
CLOTH, CLOTHR = 'Structure:Cloth', 'Roof:Cloth'
LEAF, LEAFR = 'Structure:Leaf', 'Roof:Leaf'
STALK = 'Structure:Stalk'
GLOW = 'Detail:Glow'
CHAR = (0.16, 0.14, 0.13, 0.0)
CLUSTER_UV = (0.0, 0.25, 1.0, 1.0)


# --------------------------------------------------------------------------- helpers
def wt(rng, lo=0.78, hi=1.0, a=0.0):
    t = rng.uniform(lo, hi)
    return (t, t * rng.uniform(0.96, 1.0), t * rng.uniform(0.92, 1.0), a)


def _orient(part, p0, p1):
    d = Vector(p1) - Vector(p0)
    if d.length < 1e-6:
        return part.xf(Matrix.Translation(Vector(p0)))
    dn = d.normalized()
    q = dn.to_track_quat('Z', 'Y' if abs(dn.y) < 0.95 else 'X')
    return part.xf(Matrix.Translation(Vector(p0)) @ q.to_matrix().to_4x4())


def beam(p0, p1, w, h, mat, rng, col=W, ch=0.0, roll=0.0):
    Ln = (Vector(p1) - Vector(p0)).length
    b = bbox(w, h, Ln, ch, mat, col, rng=rng) if ch > 0 else box(w, h, Ln, mat, col, rng=rng)
    if roll:
        b.xf(Matrix.Rotation(roll, 4, 'Z'))
    return _orient(b, p0, p1)


def pole(p0, p1, r, seg, mat, rng=None, col=W, r1=None, caps=True):
    Ln = (Vector(p1) - Vector(p0)).length
    c = cyl(r, Ln, seg, mat, col, r_top=r1, caps=caps)
    if rng:
        c.xf(Matrix.Rotation(rng.uniform(0, 6.28), 4, 'Z'))
    return _orient(c, p0, p1)


def tri_wave(t):
    t = t % 2.0
    return 1.0 - abs(t - 1.0)


def strip_uv(part, s=1.0):
    """Leaf-mat UVs for solid foliage cores: u along x (+y), v ping-pongs inside the opaque strip v 0..0.25."""
    for i, fc in enumerate(part.f):
        part.uv[i] = [((part.v[j].x + 0.5 * part.v[j].y) / (0.6 * s),
                       0.01 + 0.23 * tri_wave((part.v[j].z + 0.5 * abs(part.v[j].y)) / (0.15 * s))) for j in fc]
    return part


def leafcard(c, n, size, rng, mat=LEAF, uv=CLUSTER_UV, col=W):
    """Foliage card centred at c, facing along n, random roll; 4:3 like the atlas cluster."""
    n = Vector(n).normalized()
    h = size * 0.75
    cd = card(size, h, uv, mat, col, z0=-h / 2)
    q = (-n).to_track_quat('Y', 'Z' if abs(n.z) < 0.95 else 'X')
    qr = Quaternion(n, rng.uniform(-0.6, 0.6))
    cd.xf(Matrix.Translation(Vector(c)) @ (qr @ q).to_matrix().to_4x4())
    return cd


def colour_faces(part, col):
    part.col = [tuple(col)] * len(part.f)
    return part


# =========================================================================== fences (4 m tiles along X)
def fence_wattle(lod, rng, damaged=False):
    p = Part()
    n_st = 11 if lod < 2 else 6
    xs = [-2.0 + i * 4.0 / (n_st - 1) for i in range(n_st)]
    lean_mid = []
    for i, x in enumerate(xs):
        end = i in (0, n_st - 1)
        h = 1.25 + (0 if end else rng.uniform(-0.06, 0.1))
        lx, ly = (0, 0) if end else (rng.gauss(0, 0.025), rng.gauss(0, 0.025))
        if damaged and 0.25 < (i / (n_st - 1)) < 0.65:
            ly += rng.uniform(0.25, 0.45)
            h *= rng.uniform(0.6, 0.95)
        lean_mid.append((lx, ly))
        p.add(pole((x, 0, -0.3), (x + lx * h, ly * h, h), 0.035, 5 if lod == 0 else 4, WOOD, rng, wt(rng), caps=False))
    rods = (7, 4, 2)[lod]
    seg = (4, 3, 3)[lod]
    for k in range(rods):
        z = 0.12 + k * 0.95 / max(rods - 1, 1)
        pts = []
        for i, x in enumerate(xs):
            lx, ly = lean_mid[i]
            sgn = 1 if (i + k) % 2 == 0 else -1
            pts.append((x + lx * z, ly * z + sgn * 0.045, z + rng.uniform(-0.015, 0.015)))
        if damaged and k >= rods // 2:
            # the upper rods are broken out in the middle third
            a = [q for q in pts if q[0] < -0.6]
            b = [q for q in pts if q[0] > 0.8]
            for run in (a, b):
                if len(run) >= 2:
                    p.add(tube(run, 0.022, seg, WOOD, wt(rng, 0.7, 0.92), caps=False))
            continue
        p.add(tube(pts, 0.022, seg, WOOD, wt(rng, 0.7, 0.92), caps=False))
    if damaged and lod == 0:
        for j in range(3):   # loose rods on the ground
            x0 = rng.uniform(-0.6, 0.4)
            p.add(tube([(x0, -0.35 - j * 0.12, 0.03), (x0 + 1.4, -0.2 - j * 0.1, 0.05)], 0.022, 4, WOOD, wt(rng, 0.7, 0.9)))
    return p


def fence_rail(lod, rng, damaged=False):
    p = Part()
    posts = [-2.0, 0.0, 2.0]
    tops = []
    for i, x in enumerate(posts):
        tilt = (0.0, 0.0)
        if damaged and i == 1:
            tilt = (0.25, 0.35)
        top = Vector((x + tilt[0], tilt[1], 1.35))
        p.add(beam((x, 0, -0.35), top, 0.15, 0.13, WOOD, rng, wt(rng), ch=0.025 if lod == 0 else 0.0,
                   roll=rng.uniform(-0.2, 0.2) if i == 1 else 0.0))
        tops.append(top)
    for k, z in enumerate((0.45, 0.9, 1.25)):
        if damaged and k == 2:
            # top rail fallen: one end on the ground
            p.add(beam((-1.9, -0.25, 0.05), (1.6, -0.6, 0.45), 0.09, 0.12, WOOD, rng, wt(rng)))
            continue
        if damaged and k == 1:
            p.add(beam((-2.0, 0.1, z), (0.0 + 0.25 * z / 1.35, 0.1 + 0.35 * z / 1.35, z - 0.05), 0.08, 0.12, WOOD, rng, wt(rng)))
            continue
        sag = rng.uniform(-0.03, 0.02)
        p.add(beam((-2.0, 0.1, z), (0.0, 0.1, z + sag), 0.08, 0.12, WOOD, rng, wt(rng), ch=0.015 if lod == 0 else 0))
        p.add(beam((0.0, 0.1, z + sag), (2.0, 0.1, z), 0.08, 0.12, WOOD, rng, wt(rng), ch=0.015 if lod == 0 else 0))
    return p


def _wall_hull(x0, x1, h0, h1, rng, moss):
    """Battered dry-stone wall section from x0 to x1 (LOD2): base 0.78 m wide at -0.25, top ~0.46 wide, the top
    sloping from h0 (at x0) to h1 (at x1); a convex hull, box-projected like the stones."""
    pts = []
    for x, h in ((x0, h0), (x1, h1)):
        for y in (-1, 1):
            pts += [(x, y * 0.39, -0.25), (x, y * 0.3, max(0.1, h - 0.3)), (x, y * 0.2, h - 0.05), (x, y * 0.06, h)]
    return L.box_uv_part(L.hull(pts, ROCKT, wt(rng, 0.78, 0.95, moss)), 2.0, rng)


def fence_drystone(lod, rng, damaged=False):
    p = Part()
    if lod >= 2:   # verifier fix: a wall hull (collapse decimation of the stones left sheets and holes at 1 km)
        if not damaged:
            p.add(_wall_hull(-2.0, 2.0, 1.28, 1.28, rng, 0.5))
        else:
            p.add(_wall_hull(-2.0, -0.6, 1.28, 0.55, rng, 0.5), _wall_hull(0.8, 2.0, 0.55, 1.28, rng, 0.5),
                  _wall_hull(-0.6, 0.8, 0.4, 0.4, rng, 0.6))
            for side in (-1, 1):
                st = lump(1.0, 1, ROCKT, rng, amp=0.25, squash=(0.45, 0.3, 0.16), col=wt(rng, 0.7, 0.9, 0.5))
                st.at((0.1 + side * 0.3, side * 0.62, 0.02), (0, 0, rng.uniform(0, 3)))
                p.add(L.box_uv_part(st, 2.0, rng))
        return p
    z = -0.25
    course = 0
    gap = lambda x, zz: damaged and zz > 0.35 + 0.5 * abs(x - 0.1) and abs(x - 0.1) < 1.0
    while z < 0.95:
        h = rng.uniform(0.15, 0.22)
        t = 0.78 - 0.32 * max(0.0, z) / 1.0
        x = -2.0 + (rng.uniform(0, 0.25) if course % 2 else 0.0)
        first = True
        while x < 2.0:
            ln = rng.uniform(0.3, 0.6)
            if first and course % 2:
                ln = x + 2.0
                x = -2.0
            x1 = min(2.0, x + ln)
            xc = (x + x1) / 2
            first = False
            if (x1 - x) > 0.05 and not gap(xc, z + h):
                st = box(x1 - x - 0.012, t * rng.uniform(0.94, 1.04), h * rng.uniform(0.88, 1.0), ROCKT,
                         wt(rng, 0.72, 1.0, rng.uniform(0.2, 0.9)), rng=rng)
                st.at((xc, rng.gauss(0, 0.012), z), (rng.gauss(0, 0.02), rng.gauss(0, 0.02), rng.gauss(0, 0.025)))
                p.add(st)
            x = x1
        z += h
        course += 1
    # coping: stones on edge along the top
    x = -2.0
    while x < 1.98 and lod < 2:
        w = rng.uniform(0.12, 0.2)
        if not gap(x + w / 2, 1.2):
            cp = box(w - 0.015, 0.46, rng.uniform(0.24, 0.32), ROCKT, wt(rng, 0.7, 0.95, rng.uniform(0.5, 1.0)), rng=rng)
            cp.at((x + w / 2, 0, z - 0.04), (0, rng.gauss(0, 0.12), 0))
            p.add(cp)
        x += w
    if damaged:
        for k in range(10 if lod == 0 else 4):   # tumbled stones at the foot of the breach
            st = box(rng.uniform(0.25, 0.45), rng.uniform(0.25, 0.4), rng.uniform(0.14, 0.2), ROCKT,
                     wt(rng, 0.7, 0.95, rng.uniform(0.3, 0.9)), rng=rng)
            side = -1 if k % 2 else 1
            st.at((rng.uniform(-0.8, 1.0), side * rng.uniform(0.45, 1.0), -0.03),
                  (rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), rng.uniform(0, 3)))
            p.add(st)
    return p


def fence_iron(lod, rng, damaged=False):
    p = Part()
    for i, x in enumerate((-2.0, 2.0)):
        lean = (0.0, 0.3) if (damaged and i == 1) else (0.0, 0.0)
        top = (x, lean[1], 1.45)
        p.add(beam((x, 0, -0.3), top, 0.1, 0.1, METAL, rng, ch=0.012 if lod == 0 else 0))
        fin = lathe([(0.07, 0), (0.07, 0.04), (0.04, 0.06), (0.05, 0.12), (0.0, 0.2)], 8 if lod == 0 else 4, METAL)
        p.add(_orient(fin, top, Vector(top) + Vector((0, lean[1] * 0.1, 0.2))))
    for z in (0.15, 1.05):
        p.add(beam((-2.0, 0, z), (2.0, 0, z), 0.04, 0.02, METAL, rng))
    step = (0.15, 0.3, 0.6)[lod]
    n = int(round(3.7 / step))
    for k in range(n + 1):
        x = -1.85 + k * 3.7 / n
        if damaged and 0.1 < x < 0.7 and k % 2 == 0:
            continue   # missing bars
        top = Vector((x, 0, 1.22))
        if damaged and -0.5 < x < 0.1:
            top += Vector((rng.uniform(-0.15, 0.15), rng.uniform(0.15, 0.4), -0.05))   # bent out
        p.add(beam((x, 0, 0.02), top, 0.022, 0.022, METAL, rng))
        if lod < 2:
            tip = lathe([(0.03, 0), (0.0, 0.11)], 4, METAL, cap_bot=True, cap_top=False)
            p.add(_orient(tip, top, top + (top - Vector((x, 0, 0.02))).normalized() * 0.11))
    return p


# =========================================================================== hedgerows (4 m tiles along X)
def hedge(lod, rng, tall=False):
    p = Part()
    sub = (3, 2, 2)[lod]
    core = L.ico(1.0, sub, LEAF, W, 70.0)
    seed = Vector((rng.uniform(-40, 40), rng.uniform(-40, 40), 3.0))
    sq = (2.3, 0.62, 0.82)

    def f(q):
        n = noise.fractal(q * 2.2 + seed, 0.5, 2.0, 3)
        v = Vector((q.x * sq[0], q.y * sq[1], q.z * sq[2])) * (1.0 + 0.16 * n)
        v.z += 0.78
        if v.z < 0.05:   # verifier fix: the root zone goes 0.1-0.15 m below ground (it floated 5 mm)
            v.z = 0.05 + (v.z - 0.05) * 0.8
        return v
    L.displace(core, f)
    colour_faces(core, (0.62, 0.62, 0.6, 0.0))   # the core is the shaded inside of the hedge
    p.add(strip_uv(core))
    n_cards = ((160 if tall else 230), 80, 26)[lod]
    size = (0.62, 0.9, 1.4)[lod]
    for k in range(n_cards):
        th = rng.uniform(-0.15 * math.pi, 1.15 * math.pi)
        x = rng.uniform(-2.15, 2.15)
        ex = max(0.25, 1.0 - (abs(x) / 2.35) ** 6)
        bump = 1.0 + 0.18 * noise.noise(Vector((x * 0.9, th, 0.0)) + seed)
        y = math.cos(th) * 0.62 * 1.08 * ex * bump
        zz = 0.78 + math.sin(th) * 0.82 * 1.05 * bump
        n = Vector((rng.gauss(0, 0.45), math.cos(th) / 0.62, math.sin(th) / 0.82 + 0.35))
        c = Vector((x, y, max(0.12, zz))) + n.normalized() * 0.06
        shade = 1.0 if math.sin(th) > 0.2 else 0.82
        p.add(leafcard(c, n, size * rng.uniform(0.8, 1.25), rng, col=wt(rng, 0.8 * shade, 1.0 * shade)))
    if tall:
        # a hedgerow tree (hawthorn / field maple) rising out of the hedge
        tx = 0.9
        trunk = tube([(tx, 0.05, -0.2), (tx + 0.1, 0.0, 1.4), (tx + 0.05, -0.05, 2.4)], [0.14, 0.11, 0.08],
                     6 if lod == 0 else 4, WOODR, wt(rng, 0.6, 0.75), caps=False)
        p.add(trunk)
        lobes = [(Vector((tx, 0, 2.95)), 1.15), (Vector((tx + 0.7, 0.25, 2.55)), 0.8), (Vector((tx - 0.6, -0.2, 2.6)), 0.85),
                 (Vector((tx + 0.2, -0.1, 3.55)), 0.75)]
        for (lc, lr) in lobes:
            crown = L.ico(1.0, 1, LEAF, (0.6, 0.62, 0.58, 0.0), 70.0)
            L.displace(crown, lambda q, lc=lc, lr=lr: q * lr * 0.85 + lc)
            p.add(strip_uv(crown))
        for k in range((70, 30, 10)[lod]):
            lc, lr = lobes[k % len(lobes)]
            th = rng.uniform(0, 2 * math.pi)
            ph = rng.uniform(-0.5, 1.4)
            n = Vector((math.cos(th) * math.cos(ph), math.sin(th) * math.cos(ph), math.sin(ph)))
            c = lc + n * lr * rng.uniform(0.8, 1.0)
            p.add(leafcard(c, n + Vector((rng.gauss(0, 0.3), rng.gauss(0, 0.3), 0.2)), size * rng.uniform(1.0, 1.4), rng,
                           col=wt(rng, 0.85, 1.0)))
        for k in range(3 if lod == 0 else 0):   # a few branches reaching into the lobes
            lc, lr = lobes[k + 1]
            p.add(tube([(tx + 0.08, 0.0, 1.9), lc], [0.06, 0.035], 4, WOODR, wt(rng, 0.6, 0.75)))
    return p


# =========================================================================== crops (4 x 4 m tiles, rows along X)
def crop_grain(lod, rng):
    p = Part()
    rows = 8
    for k in range(rows):
        y = -1.75 + k * 0.5
        nv = (3, 1, 0)[lod]
        for j in range(nv):
            off = rng.uniform(0, 1)
            dy = (-0.07, 0.07, 0.0)[j]
            c = card(4.0, rng.uniform(0.95, 1.05), (off, 0.3, off + 4.0, 1.0), STALK, wt(rng, 0.85, 1.0),
                     bend=rng.uniform(-0.05, 0.05), cols=2)
            c.at((0, y + dy, 0), (0, 0, rng.uniform(-0.02, 0.02) + (math.pi if j == 1 else 0.0)))
            p.add(c)
        off = rng.uniform(0, 1)
        p.add(hcard(4.0, 0.48, (off, 0.0, off + 4.0, 0.3), STALK, wt(rng, 0.85, 1.0), z=0.82 + rng.uniform(-0.03, 0.03)).at((0, y, 0)))
    return p


def crop_veg(lod, rng):
    p = Part()
    if lod >= 2:   # verifier fix: rows of low mounds (~70 triangles) instead of 18 heads (360) per tile at 1 km
        for r in range(6):
            y = -1.5 + r * 0.6
            row = tube([(-1.95, y, 0.12), (0.0, y, 0.13), (1.95, y, 0.12)], 0.2, 5, LEAF, wt(rng, 0.8, 1.0), caps=False)
            L.displace(row, lambda q, y=y: Vector((q.x, y + (q.y - y) * 0.8, max(-0.03, q.z))))
            p.add(strip_uv(row, 0.5))
        return p
    for r in range(6):
        y = -1.5 + r * 0.6
        n = (7, 4, 3)[lod]
        for k in range(n):
            x = -1.75 + (k + 0.5) * 3.5 / n + rng.uniform(-0.08, 0.08)
            s = rng.uniform(0.18, 0.26)
            hd = L.ico(1.0, 1, LEAF, wt(rng, 0.8, 1.0), 70.0)
            sd = Vector((rng.uniform(-9, 9), 0, 0))
            L.displace(hd, lambda q, s=s, x=x, y=y, sd=sd: Vector((q.x * s, q.y * s, q.z * s * 0.75)) *
                       (1 + 0.15 * noise.noise(q * 2.5 + sd)) + Vector((x, y, s * 0.55)))
            p.add(strip_uv(hd, 0.5))
            if lod == 0:
                for a in range(2):   # two splayed outer leaves
                    ang = rng.uniform(0, 6.28)
                    nrm = Vector((math.cos(ang) * 0.8, math.sin(ang) * 0.8, 0.6))
                    p.add(leafcard(Vector((x, y, s * 0.4)) + nrm * s * 0.8, nrm, s * 2.2, rng, col=wt(rng, 0.85, 1.0)))
    return p


def crop_vine(lod, rng):
    p = Part()
    for x in (-2.0, 0.0, 2.0):
        p.add(beam((x, 0, -0.3), (x + rng.gauss(0, 0.02), 0, 1.65), 0.08, 0.08, WOOD, rng, wt(rng)))
    if lod == 0:   # wires and trunks only up close (12 mm wires vanish beyond 40 m)
        for z in (0.75, 1.15, 1.55):
            p.add(beam((-2, 0, z), (2, 0, z), 0.012, 0.012, 'Structure:Wood', rng, (0.3, 0.3, 0.3, 0)))
    for k in range(4):
        x = -1.5 + k
        if lod < 2:
            p.add(tube([(x, 0, 0.0), (x + 0.05, 0.03, 0.35), (x - 0.04, 0.0, 0.7), (x + 0.3, 0.0, 0.78)],
                       [0.045, 0.04, 0.035, 0.025], 5 if lod == 0 else 3, WOOD, wt(rng, 0.55, 0.7), caps=False))
    # verifier fix: the canopy was one ellipsoid tapering to nothing at the tile ends, so tiled rows read as
    # sausage links every 4 m. Now a continuous leafy wall along the wires: an elliptic tube from end to end of
    # the tile, lumpy in the middle and exactly round at x = +-2 so neighbouring tiles join without a seam.
    n = (10, 5, 2)[lod]
    pts = [(-2.0 + 4.0 * k / n, 0.0, 1.18) for k in range(n + 1)]
    canopy = tube(pts, 0.44, (8, 6, 4)[lod], LEAFR, W, caps=lod < 2)
    sd = Vector((3.0, 5.0, 7.0))

    def shape(q):
        edge = max(0.0, 1.0 - (abs(q.x) / 2.0) ** 6)
        d = Vector((0.0, q.y, q.z - 1.18))
        d *= 1.0 + 0.22 * edge * noise.fractal(q * 2.5 + sd, 0.5, 2, 3)
        return Vector((q.x, d.y * 0.72, 1.18 + d.z * (1.0 if d.z > 0 else 0.85)))
    L.displace(canopy, shape)
    p.add(strip_uv(canopy, 0.6))
    for k in range((30, 12, 4)[lod]):
        x = rng.uniform(-2.0, 2.0)
        side = 1 if k % 2 else -1
        n_ = Vector((rng.gauss(0, 0.3), side, rng.uniform(-0.2, 0.7)))
        p.add(leafcard((x, side * 0.3, rng.uniform(0.85, 1.55)), n_, rng.uniform(0.55, 0.75) * (1.5 if lod else 1.0),
                       rng, mat=LEAFR, col=wt(rng, 0.85, 1.0)))
    return p


# =========================================================================== hay
def hay_stack(lod, rng):
    p = Part()
    seg = (18, 10, 7)[lod]
    prof = [(1.5, -0.12), (1.68, 0.25), (1.78, 0.9), (1.68, 1.6), (1.32, 2.25), (0.85, 2.75), (0.35, 3.1), (0.0, 3.22)]
    if lod >= 2:   # verifier fix: the decimated cone folded into slivers at LOD2 (only the pole was left)
        prof = [(1.5, -0.12), (1.78, 0.9), (1.5, 1.95), (0.85, 2.75), (0.0, 3.22)]
        seg = 6
    st = lathe(prof, seg, STRAW, W, cap_bot=False)
    sd = Vector((rng.uniform(-9, 9), 1.0, 2.0))
    L.displace(st, lambda q: q + Vector((q.x, q.y, 0)).normalized() * 0.07 * noise.fractal(q * 1.4 + sd, 0.5, 2, 3)
               if (q.x * q.x + q.y * q.y) > 1e-6 else q)
    p.add(st)
    pl = pole((0, 0, 2.6), (0.05, 0.02, 3.9), 0.045, 5, 'Detail:Wood', rng, wt(rng, 0.6, 0.75))
    if lod < 2:
        p.add(pl)
    if lod == 0:
        # two weighting ropes over the top, ending in stones (a common rick tie-down)
        for a in (0.4, 2.0):
            pts = []
            for k in range(9):
                t = k / 8.0
                ang = -math.pi / 2 + t * math.pi
                r = 1.78 * math.cos(ang) * 0.98
                z = 0.9 + 2.32 * math.sin(math.pi * t) ** 0.8
                pts.append((math.cos(a) * r * (1 if t < 0.5 else 1), math.sin(a) * r, z))
            pts = [(math.cos(a) * (-(1.8 - 1.8 * 2 * k / 8)), math.sin(a) * (-(1.8 - 1.8 * 2 * k / 8)),
                    0.85 + 2.4 * math.sin(math.pi * k / 8) ** 0.85) for k in range(9)]
            p.add(tube(pts, 0.018, 3, 'Detail:Wood', (0.45, 0.4, 0.33, 0), caps=False))
    return p


def hay_bales(lod, rng):
    p = Part()
    seg = (16, 10, 6)[lod]
    prof = [(0.0, 0.0), (0.7, 0.0), (0.76, 0.08), (0.77, 0.6), (0.76, 1.12), (0.7, 1.2), (0.0, 1.2)]
    for (x, y, z, rz) in ((-0.78, 0.0, 0.76, 0.05), (0.78, 0.1, 0.76, -0.04), (0.0, 0.05, 2.0, 0.08)):
        b = lathe(prof, seg, STRAW, wt(rng, 0.85, 1.0), cap_bot=False, cap_top=False)
        b.at((0, 0, -0.6))
        b.xf(Matrix.Rotation(math.pi / 2, 4, 'X'))
        b.at((x, y, z - 0.02 if z > 1 else z - 0.02), (0, 0, rz))
        p.add(b)
    return p


def hay_stook(lod, rng):
    p = Part()
    n = 8 if lod < 2 else 5
    seg = (6, 5, 4)[lod]
    for k in range(n):
        a = 2 * math.pi * k / n + rng.uniform(-0.15, 0.15)
        prof = [(0.07, 0.0), (0.12, 0.05), (0.1, 0.45), (0.12, 0.7), (0.2, 0.98), (0.13, 1.22), (0.0, 1.32)]
        sh = lathe(prof, seg, STRAW, wt(rng, 0.85, 1.0), cap_top=False)
        tilt = 0.22
        sh.at((math.cos(a) * 0.28, math.sin(a) * 0.28, -0.05), (0, 0, 0))
        M = Matrix.Translation(Vector((math.cos(a) * 0.28, math.sin(a) * 0.28, 0))) @ \
            Matrix.Rotation(-tilt, 4, Vector((-math.sin(a), math.cos(a), 0))) @ \
            Matrix.Translation(-Vector((math.cos(a) * 0.28, math.sin(a) * 0.28, 0)))
        sh.xf(M)
        p.add(sh)
    return p


# =========================================================================== barrels, crates
BARREL = [(0.29, 0.0), (0.33, 0.15), (0.355, 0.45), (0.33, 0.75), (0.29, 0.9), (0.27, 0.9), (0.27, 0.86)]


def _barrel_r(z):
    for (r0, z0), (r1, z1) in zip(BARREL, BARREL[1:]):
        if z0 <= z <= z1 and z1 > z0:
            return r0 + (r1 - r0) * (z - z0) / (z1 - z0)
    return 0.3


def barrel_part(rng, seg=16, hoops=(0.1, 0.3, 0.6, 0.8), lod=0):
    p = lathe(BARREL, seg, WOOD, W)
    rings = len(BARREL) - 1
    tints = [wt(rng, 0.72, 1.0) for _ in range(seg)]
    for i in range(rings):
        for k in range(seg):
            p.col[i * seg + k] = tints[k]
    for z in hoops:
        r = _barrel_r(z)
        hp = lathe([(r + 0.003, z - 0.028), (r + 0.011, z - 0.024), (r + 0.011, z + 0.024), (r + 0.003, z + 0.028)],
                   seg, METALD, W, cap_bot=False, cap_top=False)
        p.add(hp)
    return p


def barrel(lod, rng):
    return barrel_part(rng)


def barrel_stack(lod, rng):
    p = Part()
    for (x, z) in ((-0.36, 0.355), (0.36, 0.355), (0.0, 0.355 + 0.62)):
        b = barrel_part(rng, 10, (0.12, 0.78))
        b.at((0, 0, -0.45))
        b.xf(Matrix.Rotation(math.pi / 2, 4, 'X'))
        b.at((x, 0, z - 0.03), (0, 0, rng.uniform(-0.05, 0.05)))   # verifier fix: was 7 mm above ground
        p.add(b)
    return p


def crate(s, rng, lod=0):
    p = Part()
    if lod >= 2:   # verifier fix: a plain box (12 triangles) instead of a chamfered hull folded by decimation
        p.add(box(s[0], s[1], s[2], WOOD, wt(rng, 0.8, 1.0), rng=rng))
    else:
        p.add(bbox(s[0], s[1], s[2], 0.025, WOOD, wt(rng, 0.8, 1.0), rng=rng))
    if lod == 0:
        for z in (0.12 * s[2], 0.88 * s[2]):
            for y in (-s[1] / 2 - 0.012, s[1] / 2 + 0.012):
                bt = box(s[0] * 1.02, 0.03, 0.07, WOOD, wt(rng, 0.65, 0.85), rng=rng, z0=False)
                bt.at((0, y, z))
                p.add(bt)
    return p


def crate_stack(lod, rng):
    p = Part()
    for (x, y, z, s, r) in ((-0.42, 0, 0, (0.75, 0.6, 0.6), 0.03), (0.4, 0.05, 0, (0.7, 0.6, 0.6), -0.06),
                            (0.0, 0.62, 0, (0.6, 0.5, 0.5), 0.4), (-0.25, 0.02, 0.6, (0.6, 0.55, 0.5), 0.12),
                            (0.5, 0.65, 0, (0.45, 0.4, 0.35), -0.3)):
        c = crate(s, rng, lod)
        c.at((x, y, z), (0, 0, r))
        p.add(c)
    return p


# =========================================================================== carts
def wheel(r, spokes, rng, seg=16, w=0.08, lod=0):
    """Wheel in the XZ plane (axle along X), centred at the origin."""
    p = Part()
    rim = lathe([(r - 0.055, -w / 2), (r, -w / 2), (r, w / 2), (r - 0.055, w / 2), (r - 0.055, -w / 2)],
                seg, WOOD, wt(rng, 0.7, 0.85), cap_bot=False, cap_top=False, ang=50)
    tyre = lathe([(r, -w / 2 - 0.004), (r + 0.012, -w / 2), (r + 0.012, w / 2), (r, w / 2 + 0.004)], seg, METALD, W,
                 cap_bot=False, cap_top=False)
    hub = cyl(0.085, w * 2.2, 8 if lod == 0 else 6, WOOD, wt(rng, 0.6, 0.75), z0=-w * 1.1)
    p.add(rim, tyre if lod < 2 else None, hub)
    for k in range(spokes):
        a = 2 * math.pi * (k + 0.5) / spokes
        p.add(beam((math.cos(a) * 0.07, math.sin(a) * 0.07, 0), (math.cos(a) * (r - 0.04), math.sin(a) * (r - 0.04), 0),
                   0.035, 0.028, WOOD, rng, wt(rng, 0.75, 0.9)))
    p.xf(Matrix.Rotation(math.pi / 2, 4, 'Y'))
    return p


def cart_hand(lod, rng):
    p = Part()
    seg = (16, 10, 8)[lod]
    wr = 0.6
    for sx in (-1, 1):
        wl = wheel(wr, 8 if lod < 2 else 4, rng, seg, lod=lod)
        wl.at((sx * 0.68, 0.15, wr))
        p.add(wl)
    p.add(pole((-0.8, 0.15, wr), (0.8, 0.15, wr), 0.035, 6, METALD))
    # bed
    bed = bbox(1.1, 1.5, 0.06, 0.012, WOOD, wt(rng), rng=rng).at((0, 0.1, 0.72))
    p.add(Part().add(bed))
    for (x0, y0, x1, y1) in ((-0.55, -0.65, -0.55, 0.85), (0.55, -0.65, 0.55, 0.85), (-0.55, 0.85, 0.55, 0.85)):
        for zz in (0.86, 1.02):
            p.add(beam((x0, y0, zz), (x1, y1, zz), 0.04, 0.13, WOOD, rng, wt(rng)))
    for (x, y) in ((-0.55, -0.6), (0.55, -0.6), (-0.55, 0.8), (0.55, 0.8)):
        p.add(beam((x, y, 0.72), (x, y, 1.1), 0.05, 0.05, WOOD, rng, wt(rng, 0.7, 0.85)))
    # shafts resting on the ground in front, and a prop stick at the back
    for sx in (-1, 1):
        p.add(beam((sx * 0.45, 0.85, 0.72), (sx * 0.4, -1.9, 0.12), 0.07, 0.07, WOOD, rng, wt(rng, 0.75, 0.9)))
    return p


def cart_wagon(lod, rng):
    p = Part()
    seg = (16, 10, 8)[lod]
    for (y, r) in ((1.15, 0.68), (-1.05, 0.52)):
        for sx in (-1, 1):
            wl = wheel(r, 10 if lod == 0 else 6, rng, seg, w=0.09, lod=lod)
            wl.at((sx * 0.86, y, r))
            p.add(wl)
        p.add(pole((-0.95, y, r), (0.95, y, r), 0.045, 6, METALD))
    p.add(Part().add(bbox(1.4, 3.6, 0.08, 0.015, WOOD, wt(rng), rng=rng).at((0, 0.05, 0.95))))
    p.add(beam((0, -1.05, 0.52), (0, 1.15, 0.68), 0.12, 0.12, WOOD, rng, wt(rng, 0.7, 0.85)))
    for sx in (-1, 1):
        for zz in (1.08, 1.25, 1.42):
            p.add(beam((sx * 0.7, -1.75, zz), (sx * 0.7, 1.85, zz), 0.04, 0.16, WOOD, rng, wt(rng)))
    for y in (-1.75, 1.85):
        p.add(beam((-0.7, y, 1.1), (0.7, y, 1.1), 0.04, 0.3, WOOD, rng, wt(rng)))
    # tongue to the front
    p.add(beam((0, -1.2, 0.55), (0, -3.3, 0.35), 0.08, 0.08, WOOD, rng, wt(rng, 0.75, 0.9)))
    # canvas cover on hoops (Roof slot, cloth): sags between the hoops
    nx = (12, 8, 5)[lod]
    ny = (14, 8, 4)[lod]
    hoops = 5
    cv = Part()
    y0, y1 = -1.6, 1.7
    for j in range(ny + 1):
        t = j / ny
        y = y0 + (y1 - y0) * t
        hp = abs(math.cos(math.pi * t * (hoops - 1)))
        sag = 0.06 * (1 - hp)
        for i in range(nx + 1):
            a = math.pi * i / nx
            rr = 0.78 - sag
            cv.v.append(Vector((math.cos(a) * rr, y, 1.42 + math.sin(a) * (0.95 - sag))))
    arc = math.pi * 0.85
    for j in range(ny):
        for i in range(nx):
            a = j * (nx + 1) + i
            b, c, d = a + 1, a + nx + 2, a + nx + 1
            uv = lambda k: ((k % (nx + 1)) / nx * arc / 0.5, (y0 + (y1 - y0) * (k // (nx + 1)) / ny) / 0.5)
            cv.face((a, d, c, b), (uv(a), uv(d), uv(c), uv(b)), CLOTHR, W, 60.0)
    p.add(cv)
    return p


# =========================================================================== market
def _awning(x0, x1, yf, zf, yb, zb, nx, ny, rng, drop=None, torn=False):
    cv = Part()
    for j in range(ny + 1):
        t = j / ny
        for i in range(nx + 1):
            s = i / nx
            x = x0 + (x1 - x0) * s
            y = yf + (yb - yf) * t
            z = zf + (zb - zf) * t - 0.12 * math.sin(math.pi * t) - 0.05 * math.sin(math.pi * s)
            if drop is not None:
                z -= drop * max(0.0, 1 - s * 1.4) * max(0.0, 1 - t * 1.2)
            cv.v.append(Vector((x, y, z)))
    for j in range(ny):
        for i in range(nx):
            if torn and rng.random() < 0.07 and i < nx // 2:
                continue
            a = j * (nx + 1) + i
            b, c, d = a + 1, a + nx + 2, a + nx + 1
            uv = lambda k: ((cv.v[k].x) / 0.5, (cv.v[k].y) / 0.5)
            cv.face((a, b, c, d), (uv(a), uv(b), uv(c), uv(d)), CLOTHR, W, 60.0)
    # valance: a scalloped flap hanging from the front edge
    base = len(cv.v)
    for i in range(nx * 2 + 1):
        s = i / (nx * 2)
        x = x0 + (x1 - x0) * s
        z = zf - 0.05 * math.sin(math.pi * s)
        if drop is not None:
            z -= drop * max(0.0, 1 - s * 1.4)
        cv.v.append(Vector((x, yf, z)))
        cv.v.append(Vector((x, yf - 0.01, z - 0.22 - 0.07 * abs(math.sin(math.pi * s * nx)))))
    for i in range(nx * 2):
        a = base + i * 2
        if torn and rng.random() < 0.3:
            continue
        uvs = [(cv.v[k].x / 0.5, cv.v[k].z / 0.5) for k in (a, a + 2, a + 3, a + 1)]
        cv.face((a, a + 2, a + 3, a + 1), uvs, CLOTHR, W, 60.0)
    return cv


def goods_on(p, rng, x0, x1, y, z, n, burnt=False):
    for k in range(n):
        x = x0 + (x1 - x0) * (k + 0.5) / n + rng.uniform(-0.05, 0.05)
        kind = rng.random()
        if burnt:
            sh = L.ico(1.0, 1, STONED, (0.3, 0.2, 0.16, 0))
            L.displace(sh, lambda q, x=x: Vector((q.x * 0.12, q.y * 0.1, q.z * 0.04)) + Vector((x, y, z + 0.03)))
            p.add(L.box_uv_part(sh, 2.0))
        elif kind < 0.5:
            pot = lathe([(0.0, 0), (0.09, 0.01), (0.13, 0.12), (0.11, 0.22), (0.06, 0.27), (0.07, 0.3), (0.0, 0.29)],
                        8, STONED, (1.0, 0.56, 0.36, 0))
            pot.at((x, y + rng.uniform(-0.1, 0.1), z))
            p.add(pot)
        else:
            col = rng.choice([(0.85, 0.2, 0.12, 0), (0.95, 0.55, 0.12, 0), (0.45, 0.62, 0.18, 0), (0.6, 0.15, 0.35, 0)])
            for m in range(5):
                f = L.ico(1.0, 1, STONED, col)
                c = Vector((x + rng.uniform(-0.12, 0.12), y + rng.uniform(-0.12, 0.12), z + 0.05 + 0.06 * (m // 3)))
                L.displace(f, lambda q, c=c: q * 0.06 + c)
                p.add(L.box_uv_part(f, 2.0))


def market_stall(lod, rng, state='Intact'):
    p = Part()
    burnt = state == 'Burnt'
    dmg = state == 'Damaged'
    col = (lambda: CHAR) if burnt else (lambda: wt(rng))
    posts = [(-1.45, -0.95, 2.35), (1.45, -0.95, 2.35), (-1.45, 0.95, 2.0), (1.45, 0.95, 2.0)]
    for i, (x, y, h) in enumerate(posts):
        top = Vector((x, y, h))
        if (dmg or burnt) and i == 0:
            top = Vector((x - 0.2, y - 0.5, h * 0.55))   # snapped and leaning
        if burnt and i == 3:
            top = Vector((x, y, h * 0.7))
        p.add(beam((x, y, -0.3), top, 0.1, 0.1, WOOD, rng, col(), ch=0.015 if lod == 0 else 0))
    if not burnt:
        for (a, b) in ((0, 1), (2, 3), (1, 3)):
            pa, pb = Vector(posts[a]), Vector(posts[b])
            if dmg and a == 0:
                continue
            p.add(beam(pa - Vector((0, 0, 0.06)), pb - Vector((0, 0, 0.06)), 0.07, 0.09, WOOD, rng, col()))
        p.add(beam(Vector(posts[0]) - Vector((0, 0, 0.06)) if not dmg else Vector((-1.4, -0.95, 1.4)),
                   Vector(posts[2]) - Vector((0, 0, 0.06)), 0.07, 0.09, WOOD, rng, col()))
    # counter: top, front panel, legs
    tilt = 0.25 if burnt else 0.0
    ct = bbox(2.8, 0.75, 0.07, 0.012, WOOD, col(), rng=rng)
    ct.at((0, -0.55, 0.88 - (0.3 if burnt else 0)), (0, tilt, 0))
    p.add(Part().add(ct))
    fp = box(2.8, 0.04, 0.82, WOOD, col(), rng=rng)
    fp.at((0, -0.93, 0.02), (0.0 if not burnt else -0.4, 0, 0))
    p.add(fp)
    for x in (-1.3, 1.3):
        p.add(beam((x, -0.25, 0), (x, -0.25, 0.86), 0.06, 0.06, WOOD, rng, col()))
    if not burnt:
        p.add(_awning(-1.6, 1.6, -1.2, 2.38, 1.05, 2.03, (8, 5, 3)[lod], (4, 3, 2)[lod], rng,
                      drop=1.3 if dmg else None, torn=dmg))
        if lod < 2:
            goods_on(p, rng, -1.2, 1.2, -0.55, 0.92 if not dmg else 0.92, 6 if lod == 0 else 3)
        if dmg and lod == 0:
            goods_on(p, rng, -1.4, 0.2, -1.6, 0.0, 3)
    else:
        # charred rags hanging from the back beam and broken pots on the ground
        rag = _awning(0.2, 1.4, 0.7, 1.9, 0.95, 1.95, 3, 1, rng, torn=True)
        colour_faces(rag, (0.2, 0.18, 0.16, 0))
        p.add(rag)
        p.add(beam((-1.45, 0.95, 1.94), (1.45, 0.95, 1.4), 0.07, 0.09, WOOD, rng, CHAR))
        goods_on(p, rng, -1.2, 1.0, -1.4, 0.0, 4, burnt=True)
    return p


def goods_sacks(lod, rng):
    p = Part()
    sub = (2, 1, 1)[lod]
    spots = [(-0.45, 0, 0, 0), (0.15, 0.05, 0, 0.3), (0.7, -0.1, 0, -0.2), (-0.15, 0.55, 0, 0.6),
             (-0.2, 0.15, 0.55, 1.4), (0.45, 0.25, 0.5, 1.6)]
    for (x, y, z, r) in spots:
        lying = z > 0.1
        s = lump(1.0, sub, CLOTH, rng, amp=0.12, freq=1.6, squash=(0.3, 0.25, 0.42), col=wt(rng, 0.85, 1.0))
        s.at((0, 0, 0.36))
        if lying:
            s.xf(Matrix.Rotation(math.pi / 2 * 0.85, 4, 'X'))
            s.at((0, 0, 0.0))
        s.at((x, y, z + (0.0 if not lying else 0.06)), (0, 0, r))
        p.add(s)
        if not lying and lod < 2:
            k = lump(1.0, 1, CLOTH, rng, amp=0.2, squash=(0.08, 0.08, 0.1), col=wt(rng, 0.8, 0.95))
            k.at((x, y, 0.82))
            p.add(k)
    return p


def goods_pots(lod, rng):
    p = Part()
    seg = (10, 7, 5)[lod]
    amph = [(0.0, 0.0), (0.06, 0.02), (0.17, 0.2), (0.21, 0.42), (0.19, 0.58), (0.1, 0.7), (0.07, 0.78), (0.09, 0.84),
            (0.06, 0.85), (0.05, 0.75), (0.0, 0.75)]
    jar = [(0.0, 0.0), (0.12, 0.01), (0.2, 0.15), (0.21, 0.3), (0.14, 0.42), (0.15, 0.46), (0.12, 0.47), (0.11, 0.4),
           (0.0, 0.4)]
    for (prof, x, y, sc, rot) in ((amph, -0.35, 0.1, 1.15, 0), (amph, 0.05, 0.25, 1.0, 0.3), (amph, 0.45, 0.0, 1.1, 0.0),
                                  (jar, -0.1, -0.3, 1.0, 0), (jar, 0.3, -0.4, 0.8, 0), (jar, -0.55, -0.35, 0.9, 0),
                                  (jar, 0.75, 0.3, 1.2, 0)):
        pt = lathe(prof, seg, STONE, (rng.uniform(0.85, 1.0), rng.uniform(0.85, 1.0), rng.uniform(0.85, 1.0), 0.0))
        pt.at((x, y, 0), (0, 0, rng.uniform(0, 6)), sc)
        p.add(pt)
    return p


def goods_baskets(lod, rng):
    p = Part()
    seg = (12, 8, 6)[lod]
    prof = [(0.0, 0.0), (0.22, 0.0), (0.3, 0.22), (0.31, 0.25), (0.28, 0.25), (0.26, 0.2), (0.2, 0.06), (0.0, 0.06)]
    for (x, y, colr) in ((-0.4, 0.0, (0.85, 0.2, 0.12, 0)), (0.3, 0.1, (0.95, 0.55, 0.12, 0)),
                         (-0.05, -0.55, (0.45, 0.62, 0.18, 0))):
        b = lathe(prof, seg, STRAW, wt(rng, 0.8, 1.0))
        b.at((x, y, 0))
        p.add(b)
        for m in range((9, 5, 0)[lod]):
            a = rng.uniform(0, 6.28)
            r = rng.uniform(0, 0.17)
            c = Vector((x + math.cos(a) * r, y + math.sin(a) * r, 0.17 + 0.07 * (1 - r / 0.17) + rng.uniform(0, 0.03)))
            f = L.ico(1.0, 1, STONED, colr)
            L.displace(f, lambda q, c=c: q * 0.065 + c)
            p.add(L.box_uv_part(f, 2.0))
        if lod == 2:
            top = L.ico(1.0, 1, STONED, colr)
            L.displace(top, lambda q, x=x, y=y: Vector((q.x * 0.25, q.y * 0.25, q.z * 0.06)) + Vector((x, y, 0.2)))
            p.add(L.box_uv_part(top, 2.0))
    return p


# =========================================================================== signposts
def sign_fingerpost(lod, rng):
    p = Part()
    p.add(beam((0, 0, -0.45), (0, 0, 2.55), 0.12, 0.12, WOOD, rng, wt(rng), ch=0.015 if lod == 0 else 0))
    cap = lathe([(0.1, 0), (0.0, 0.12)], 4, WOOD, wt(rng, 0.7, 0.85))
    cap.at((0, 0, 2.55), (0, 0, math.pi / 4))
    p.add(cap)
    for (z, a) in ((2.3, 0.3), (2.08, 2.4), (1.86, 4.1)):
        d = Vector((math.cos(a), math.sin(a), 0))
        f = beam(d * 0.05 + Vector((0, 0, z)), d * 0.78 + Vector((0, 0, z)), 0.035, 0.16, WOOD, rng, wt(rng, 0.8, 1.0))
        p.add(f)
        tip = lathe([(0.09, 0), (0.0, 0.13)], 3, WOOD, wt(rng, 0.8, 1.0))
        tip.xf(Matrix.Diagonal((0.2, 1.0, 1.0, 1.0)))
        p.add(_orient(tip, d * 0.78 + Vector((0, 0, z)), d * 0.95 + Vector((0, 0, z))))
    return p


def sign_milestone(lod, rng):
    p = Part()
    body = bbox(0.46, 0.3, 0.72, 0.03, STONE, wt(rng, 0.85, 1.0, 0.3), rng=rng)
    body.at((0, 0, -0.25))
    p.add(Part().add(body))
    top = cyl(0.23, 0.3, (12, 8, 6)[lod], STONE, wt(rng, 0.85, 1.0, 0.4))
    top.at((0, 0, -0.15))
    top.xf(Matrix.Rotation(math.pi / 2, 4, 'X'))
    top.at((0, 0, 0.47))
    p.add(top)
    face = box(0.32, 0.02, 0.3, STONE, (0.8, 0.78, 0.74, 0.0), rng=rng)
    face.at((0, -0.155, 0.12))
    p.add(face)
    return p


def sign_street(lod, rng):
    p = Part()
    seg = (8, 6, 4)[lod]
    p.add(cyl(0.045, 3.2, seg, METAL, W, z0=-0.4))
    p.add(cyl(0.07, 0.35, seg, METAL, W, r_top=0.05, z0=0.0))
    p.add(Part().add(bbox(0.62, 0.025, 0.17, 0.008, METAL, W, rng=rng).at((0.31, 0, 2.62))))
    p.add(Part().add(bbox(0.62, 0.025, 0.17, 0.008, METAL, W, rng=rng).at((0.31, 0, 2.38)).xf(Matrix.Rotation(1.57, 4, 'Z'))))
    p.add(lathe([(0.05, 0), (0.06, 0.03), (0.0, 0.08)], seg, METAL).at((0, 0, 2.8)))
    return p


def sign_cairn(lod, rng):
    p = Part()
    z = -0.05
    r = 0.45
    for k in range(9):
        s = r * rng.uniform(0.85, 1.05)
        st = lump(1.0, 2 if lod == 0 else 1, ROCKT, rng, amp=0.2, freq=1.2, squash=(s, s * 0.85, s * 0.45),
                  col=wt(rng, 0.75, 1.0, rng.uniform(0.3, 0.9)))
        st.at((rng.uniform(-0.05, 0.05), rng.uniform(-0.05, 0.05), z + s * 0.4), (0, 0, rng.uniform(0, 6)))
        p.add(st)
        z += s * 0.62
        r *= 0.86
    return p


# =========================================================================== lamps per age (Detail = Glow)
def lamp_age0(lod, rng):
    """Stone age: a pitch torch on a stake."""
    p = Part()
    p.add(tube([(0, 0, -0.3), (0.02, 0.01, 0.8), (0.0, 0.03, 1.55)], [0.035, 0.03, 0.03], 5, WOOD, wt(rng, 0.6, 0.75)))
    head = lump(1.0, 2 if lod == 0 else 1, STRAWR, rng, amp=0.15, squash=(0.07, 0.07, 0.16), col=(0.45, 0.38, 0.3, 0))
    head.at((0.0, 0.03, 1.6))
    p.add(head)
    fl = lump(1.0, 2 if lod == 0 else 1, GLOW, rng, amp=0.25, squash=(0.07, 0.07, 0.2))
    fl.at((0.0, 0.03, 1.82))
    p.add(fl)
    return p


def lamp_age1(lod, rng):
    """Bronze age: a clay fire bowl on a lashed tripod."""
    p = Part()
    for k in range(3):
        a = 2 * math.pi * k / 3
        p.add(pole((math.cos(a) * 0.45, math.sin(a) * 0.45, -0.05), (math.cos(a + 3.1) * 0.08, math.sin(a + 3.1) * 0.08, 1.25),
                   0.03, 5, WOOD, rng, wt(rng, 0.6, 0.8)))
    lash = lathe([(0.07, 1.0), (0.075, 1.04), (0.07, 1.1)], 6, WOOD, (0.5, 0.42, 0.32, 0), cap_bot=False, cap_top=False)
    p.add(lash)
    bowl = lathe([(0.0, 0.0), (0.12, 0.0), (0.26, 0.12), (0.3, 0.2), (0.27, 0.2), (0.22, 0.13), (0.0, 0.1)], 12,
                 STONER, (1.0, 0.56, 0.36, 0.0))
    bowl.at((0, 0, 1.18))
    p.add(bowl)
    emb = lump(1.0, 2 if lod == 0 else 1, GLOW, rng, amp=0.3, squash=(0.2, 0.2, 0.07))
    emb.at((0, 0, 1.32))
    p.add(emb)
    fl = lump(1.0, 2 if lod == 0 else 1, GLOW, rng, amp=0.3, squash=(0.1, 0.1, 0.22))
    fl.at((0, 0, 1.5))
    p.add(fl)
    return p


def lamp_age2(lod, rng):
    """Iron age: an iron cresset (fire basket) on a timber post."""
    p = Part()
    p.add(beam((0, 0, -0.4), (0, 0, 2.3), 0.16, 0.16, WOOD, rng, wt(rng), ch=0.02 if lod == 0 else 0))
    p.add(beam((0, 0, 2.3), (0, 0, 2.45), 0.05, 0.05, METALR, rng))
    for z in (2.42, 2.75):
        p.add(lathe([(0.17 if z < 2.5 else 0.27, z - 0.015), (0.19 if z < 2.5 else 0.29, z),
                     (0.17 if z < 2.5 else 0.27, z + 0.015)], 10, METALR, W, cap_bot=False, cap_top=False))
    for k in range(8):
        a = 2 * math.pi * k / 8
        p.add(beam((math.cos(a) * 0.17, math.sin(a) * 0.17, 2.42), (math.cos(a) * 0.3, math.sin(a) * 0.3, 2.85),
                   0.018, 0.018, METALR, rng))
    fl = lump(1.0, 2 if lod == 0 else 1, GLOW, rng, amp=0.3, squash=(0.2, 0.2, 0.18))
    fl.at((0, 0, 2.62))
    p.add(fl)
    fl2 = lump(1.0, 2 if lod == 0 else 1, GLOW, rng, amp=0.35, squash=(0.12, 0.12, 0.25))
    fl2.at((0.02, 0, 2.9))
    p.add(fl2)
    return p


def _lantern(p, c, s, rng, mat=METALR, panes=4):
    """A four-sided lantern centred at c (size s): metal frame, glowing panes, roof and ring."""
    hw = s * 0.5
    glass = cyl(hw * 0.92, s * 1.1, panes, GLOW, W, r_top=hw * 1.05)
    glass.xf(Matrix.Rotation(math.pi / 4, 4, 'Z'))
    glass.at(Vector(c) + Vector((0, 0, -s * 0.55)))
    p.add(glass)
    for k in range(panes):
        a = 2 * math.pi * k / panes
        b0 = Vector(c) + Vector((math.cos(a) * hw * 1.3, math.sin(a) * hw * 1.3, -s * 0.58))
        b1 = Vector(c) + Vector((math.cos(a) * hw * 1.48, math.sin(a) * hw * 1.48, s * 0.58))
        p.add(beam(b0, b1, s * 0.07, s * 0.07, mat, rng))
    roof = lathe([(hw * 1.75, 0), (hw * 1.2, s * 0.25), (hw * 0.3, s * 0.45), (0.0, s * 0.5)], panes, mat)
    roof.xf(Matrix.Rotation(math.pi / 4, 4, 'Z'))
    roof.at(Vector(c) + Vector((0, 0, s * 0.56)))
    p.add(roof)
    base = lathe([(0.0, 0.0), (hw * 1.4, 0.0), (hw * 1.4, s * 0.08), (0.0, s * 0.08)], panes, mat)
    base.xf(Matrix.Rotation(math.pi / 4, 4, 'Z'))
    base.at(Vector(c) + Vector((0, 0, -s * 0.66)))
    p.add(base)


def lamp_age3(lod, rng):
    """Sail age: an oil lantern hanging from a bracket on a timber post."""
    p = Part()
    p.add(beam((0, 0, -0.4), (0, 0, 2.9), 0.14, 0.14, WOOD, rng, wt(rng), ch=0.02 if lod == 0 else 0))
    p.add(beam((0, 0.0, 2.75), (0, -0.75, 2.75), 0.06, 0.08, WOOD, rng, wt(rng, 0.7, 0.9)))
    p.add(beam((0, -0.06, 2.25), (0, -0.5, 2.72), 0.04, 0.05, WOOD, rng, wt(rng, 0.7, 0.9)))
    p.add(beam((0, -0.68, 2.72), (0, -0.68, 2.55), 0.012, 0.012, METALR, rng))
    _lantern(p, (0, -0.68, 2.33), 0.3, rng)
    return p


def lamp_age4(lod, rng):
    """Industry age: a cast-iron gas lamp with a fluted column and a glazed lantern."""
    p = Part()
    seg = (12, 8, 6)[lod]
    base = [(0.0, -0.2), (0.24, -0.2), (0.24, 0.05), (0.2, 0.12), (0.2, 0.35), (0.13, 0.5), (0.1, 0.62), (0.08, 0.7)]
    p.add(lathe(base, seg, METAL))
    col = lathe([(0.075, 0.7), (0.07, 1.6), (0.06, 2.6), (0.07, 2.7), (0.09, 2.75), (0.06, 2.8), (0.06, 3.05),
                 (0.11, 3.12), (0.0, 3.14)], seg, METAL)
    p.add(col)
    p.add(beam((-0.32, 0, 2.55), (0.32, 0, 2.55), 0.03, 0.04, METAL, rng))   # ladder rest
    for sx in (-1, 1):
        p.add(lathe([(0.03, 0), (0.0, 0.05)], 6, METAL).at((sx * 0.32, 0, 2.57)))
    _lantern(p, (0, 0, 3.55), 0.42, rng, mat=METAL)
    fin = lathe([(0.03, 0), (0.04, 0.05), (0.0, 0.16)], 6, METAL)
    fin.at((0, 0, 3.55 + 0.42 * 0.98))
    p.add(fin)
    return p


def lamp_age5(lod, rng):
    """Atomic age: a 9 m steel street light with a sodium cobra-head luminaire."""
    p = Part()
    seg = (10, 8, 6)[lod]
    p.add(lathe([(0.0, -0.3), (0.2, -0.3), (0.2, 0.0), (0.16, 0.05), (0.12, 0.6), (0.075, 8.4), (0.0, 8.45)], seg, METAL))
    arm = [(0.0, 0.0, 8.0), (0.0, -0.25, 8.55), (0.0, -0.8, 8.85), (0.0, -1.6, 8.95), (0.0, -2.1, 8.95)]
    p.add(tube(arm, 0.045, 6 if lod == 0 else 4, METAL, caps=True))
    head = lump(1.0, 2 if lod == 0 else 1, METAL, rng, amp=0.02, squash=(0.22, 0.42, 0.11))
    head.at((0, -2.3, 8.93))
    p.add(head)
    gl = hcard(0.3, 0.55, (0, 0, 1, 1), GLOW, z=0.0)
    gl.xf(Matrix.Rotation(math.pi, 4, 'X'))
    gl.at((0, -2.33, 8.84))
    p.add(gl)
    return p


def lamp_age6(lod, rng):
    """Space age: a slender white pole curving into a ring luminaire with an LED band."""
    p = Part()
    seg = (10, 8, 6)[lod]
    pts = []
    for k in range(10):
        t = k / 9.0
        a = t * t * 1.2
        pts.append((0.0, -math.sin(a) * 0.9 * t, -0.3 + t * 7.3 - (1 - math.cos(a)) * 0.6 * t))
    p.add(tube(pts, [0.09 - 0.045 * (k / 9.0) for k in range(10)], seg, METAL, caps=True))
    ring_c = Vector(pts[-1]) + Vector((0, -0.45, -0.05))
    rp = []
    n = (20, 12, 8)[lod]
    for k in range(n + 1):
        a = 2 * math.pi * k / n
        rp.append(ring_c + Vector((math.cos(a) * 0.45, math.sin(a) * 0.45, 0)))
    p.add(tube(rp, 0.045, 6 if lod == 0 else 4, METAL, caps=False))
    led = []
    for k in range(n + 1):
        a = 2 * math.pi * k / n
        led.append(ring_c + Vector((math.cos(a) * 0.45, math.sin(a) * 0.45, -0.04)))
    p.add(tube(led, 0.022, 4, GLOW, caps=False))
    return p


# =========================================================================== statues (Intact, Damaged, Ruin)
def _rubble(p, rng, mat, centre, radius, n, size=(0.15, 0.4), col=None):
    for k in range(n):
        a = rng.uniform(0, 6.28)
        r = rng.uniform(0.2, 1.0) * radius
        s = rng.uniform(*size)
        st = lump(1.0, 1, mat, rng, amp=0.3, freq=1.0, squash=(s, s * 0.8, s * 0.55),
                  col=col or wt(rng, 0.75, 0.95, 0.3))
        st.at((centre[0] + math.cos(a) * r, centre[1] + math.sin(a) * r, s * 0.15), (0, 0, rng.uniform(0, 6)))
        p.add(st)


def _plinth(p, rng, steps, mat=STONE):
    z = -0.3
    for (w, h) in steps:
        p.add(Part().add(bbox(w, w, h + (0.3 if z < 0 else 0), 0.03, mat, wt(rng, 0.85, 1.0, 0.25), rng=rng).at((0, 0, z))))
        z += h + (0.3 if z < 0 else 0)
    return z


def statue_totem(lod, rng, state='Intact'):
    p = Part()
    seg = (10, 8, 6)[lod]
    prof = [(0.26, -0.4), (0.26, 0.6), (0.3, 0.75), (0.22, 0.95), (0.3, 1.25), (0.24, 1.6), (0.32, 1.85), (0.22, 2.1),
            (0.28, 2.45), (0.2, 2.75), (0.3, 3.05), (0.24, 3.4), (0.16, 3.7), (0.0, 3.85)]
    if state == 'Intact':
        t = lathe(prof, seg, WOOD, wt(rng, 0.7, 0.85))
        p.add(t)
        for sx in (-1, 1):   # carved wing boards
            w = bbox(0.75, 0.05, 0.32, 0.01, WOOD, wt(rng, 0.7, 0.85), rng=rng) if lod == 0 else                 box(0.75, 0.05, 0.32, WOOD, wt(rng, 0.7, 0.85), rng=rng)
            w.at((sx * 0.55, 0, 2.95), (0, sx * 0.35, 0))
            p.add(Part().add(w))
        for z in (1.25, 2.45):   # eyes / beaks: small wedges facing -Y
            bk = lathe([(0.07, 0), (0.0, 0.22)], 4, WOOD, wt(rng, 0.6, 0.75))
            bk.xf(Matrix.Rotation(math.pi / 2, 4, 'X'))
            bk.at((0, -0.25, z))
            p.add(bk)
    elif state == 'Damaged':
        low = [q for q in prof if q[1] <= 1.85] + [(0.2, 1.95), (0.0, 1.98)]
        t = lathe(low, seg, WOOD, wt(rng, 0.6, 0.75))
        t.xf(Matrix.Rotation(0.18, 4, 'X'))
        p.add(t)
        top = lathe([(r, z - 1.85) for (r, z) in prof if z >= 1.85], seg, WOOD, wt(rng, 0.6, 0.75))
        top.xf(Matrix.Rotation(math.pi / 2, 4, 'Y'))
        top.at((0.5, 1.1, 0.28), (0, 0, 0.6))
        p.add(top)
    else:
        stump = lathe([(0.26, -0.4), (0.26, 0.55), (0.22, 0.75), (0.0, 0.7)], seg, WOOD, wt(rng, 0.45, 0.6))
        p.add(stump)
        for k in range(3):
            pc = lathe([(0.25, 0), (0.28, 0.4), (0.22, 0.9)], seg, WOOD, wt(rng, 0.45, 0.6))
            pc.xf(Matrix.Rotation(math.pi / 2, 4, 'Y'))
            pc.at((rng.uniform(-1.4, 1.4), rng.uniform(0.4, 1.6), 0.24), (0, 0, rng.uniform(0, 6)))
            p.add(pc)
    return p


def statue_idol(lod, rng, state='Intact'):
    p = Part()
    seg = (14, 9, 6)[lod]
    top = _plinth(p, rng, [(1.3, 0.55), (1.0, 0.35)])
    body = [(0.0, 0.0), (0.42, 0.0), (0.46, 0.25), (0.4, 0.55), (0.3, 0.85), (0.22, 1.05), (0.12, 1.12), (0.0, 1.14)]
    if state != 'Ruin':
        b = lathe(body if state == 'Intact' else body[:5] + [(0.0, 0.62)], seg, STONE, wt(rng, 0.85, 1.0, 0.3))
        b.xf(Matrix.Diagonal((1.0, 0.8, 1.0, 1.0)))
        b.at((0, 0, top))
        p.add(b)
        for sx in (-1, 1):   # folded arms
            if state == 'Damaged' and sx == 1:
                continue
            arm = tube([(sx * 0.33, -0.1, top + 0.72), (sx * 0.4, -0.28, top + 0.5), (sx * 0.12, -0.36, top + 0.42)],
                       [0.08, 0.075, 0.065], 6, STONE, wt(rng, 0.85, 1.0, 0.3))
            p.add(arm)
        if state == 'Intact':
            hd = lump(1.0, 2, STONE, rng, amp=0.04, squash=(0.17, 0.19, 0.22), col=wt(rng, 0.85, 1.0, 0.3))
            hd.at((0, -0.03, top + 1.3))
            p.add(hd)
        else:
            hd = lump(1.0, 1, STONE, rng, amp=0.1, squash=(0.17, 0.19, 0.2), col=wt(rng, 0.7, 0.85, 0.5))
            hd.at((0.85, -0.7, 0.18))
            p.add(hd)
            _rubble(p, rng, STONE, (0.3, -0.9), 0.6, 4)
    else:
        b = lathe([(0.0, 0.0), (0.42, 0.0), (0.46, 0.2), (0.38, 0.32), (0.0, 0.3)], seg, STONE, wt(rng, 0.7, 0.85, 0.6))
        b.at((0, 0, top))
        p.add(b)
        _rubble(p, rng, STONE, (0.0, 0.0), 1.3, 8 if lod == 0 else 4, (0.12, 0.3))
    return p


def statue_figure(lod, rng, state='Intact'):
    """The robed figure with both arms raised to the sky (the people's image of the demiurge)."""
    p = Part()
    seg = (16, 10, 7)[lod]
    top = _plinth(p, rng, [(2.3, 0.45), (1.8, 0.4), (1.3, 0.9)])
    robe = [(0.0, 0.0), (0.52, 0.0), (0.5, 0.15), (0.42, 0.9), (0.34, 1.6), (0.3, 2.05), (0.34, 2.35), (0.28, 2.55),
            (0.14, 2.65), (0.0, 2.66)]
    folds = 0.035
    col = wt(rng, 0.9, 1.0, 0.35)

    def robe_part(profile):
        r = lathe(profile, seg, STONE, col)
        L.displace(r, lambda q: q + Vector((q.x, q.y, 0)) * (folds * math.sin(math.atan2(q.y, q.x) * 7) *
                                                             max(0.0, 1.0 - q.z / 2.0))
                   if q.x * q.x + q.y * q.y > 1e-6 else q)
        return r
    if state == 'Ruin':
        rb = robe_part([q for q in robe if q[1] <= 0.9] + [(0.38, 1.05), (0.0, 1.0)])
        rb.at((0, 0, top))
        p.add(rb)
        _rubble(p, rng, STONE, (0, 0), 2.6, 12 if lod == 0 else 5, (0.15, 0.4))
        return p
    p.add(robe_part(robe).at((0, 0, top)))
    sh = top + 2.38
    for sx in (-1, 1):
        if state == 'Damaged':
            # arms broken at the elbow
            arm = tube([(sx * 0.3, 0, sh), (sx * 0.62, -0.05, sh + 0.42)], [0.13, 0.11], 7, STONE, col)
            p.add(arm)
            continue
        pts = [(sx * 0.3, 0, sh), (sx * 0.62, -0.05, sh + 0.42), (sx * 0.9, -0.08, sh + 0.95), (sx * 1.0, -0.06, sh + 1.3)]
        p.add(tube(pts, [0.13, 0.11, 0.09, 0.08], 7 if lod == 0 else 5, STONE, col))
        # wide sleeves hang from the raised forearms
        sl = lathe([(0.07, 0.0), (0.2, 0.08), (0.24, 0.5), (0.2, 0.75)], 7 if lod == 0 else 5, STONE, col,
                   cap_bot=False, cap_top=False)
        sl.xf(Matrix.Rotation(math.pi + 0.35 * sx, 4, 'Y'))
        sl.at((sx * 0.86, -0.07, sh + 0.92))
        p.add(sl)
        hand = lump(1.0, 1, STONE, rng, amp=0.1, squash=(0.07, 0.05, 0.12), col=col)
        hand.at((sx * 1.02, -0.06, sh + 1.42))
        p.add(hand)
    if state == 'Intact':
        head = lump(1.0, 2, STONE, rng, amp=0.03, squash=(0.17, 0.19, 0.23), col=col)
        head.at((0, -0.04, sh + 0.45))
        p.add(head)
        hood = lathe([(0.0, 0.0), (0.24, 0.02), (0.27, 0.28), (0.2, 0.5), (0.0, 0.56)], seg, STONE, col)
        hood.at((0, 0.06, sh + 0.16))
        p.add(hood)
    else:
        for (x, y, rz, ln) in ((1.6, -0.6, 0.4, 0.7), (-1.4, 1.2, 2.0, 0.6)):
            frag = tube([(0, 0, 0), (ln, 0, 0)], 0.1, 6, STONE, wt(rng, 0.8, 0.95, 0.4))
            frag.at((x, y, 0.1), (0, 0, rz))
            p.add(frag)
        hd = lump(1.0, 1, STONE, rng, amp=0.08, squash=(0.16, 0.17, 0.2), col=wt(rng, 0.75, 0.9, 0.5))
        hd.at((-1.2, -1.3, 0.17))
        p.add(hd)
        _rubble(p, rng, STONE, (0.4, -1.4), 0.8, 4)
    return p


def statue_obelisk(lod, rng, state='Intact'):
    p = Part()
    top = _plinth(p, rng, [(2.4, 0.4), (1.9, 0.5)])
    H = 8.0
    shaft = [(0.62, 0.0), (0.42, H), (0.0, H + 0.55)]
    col = wt(rng, 0.88, 1.0, 0.3)
    if state == 'Intact':
        o = lathe(shaft, 4, STONE, col, ang=30)
        o.xf(Matrix.Rotation(math.pi / 4, 4, 'Z'))
        p.add(o.at((0, 0, top)))
    else:
        cut = 5.2 if state == 'Damaged' else 1.6
        rc = 0.62 - 0.2 * cut / H
        o = lathe([(0.62, 0.0), (rc, cut), (rc * 0.6, cut + 0.12), (0.0, cut + 0.05)], 4, STONE, col, ang=30)
        o.xf(Matrix.Rotation(math.pi / 4, 4, 'Z'))
        p.add(o.at((0, 0, top)))
        if state == 'Damaged':
            fall = lathe([(rc, 0.0), (0.42, H - cut), (0.0, H - cut + 0.55)], 4, STONE, col, ang=30)
            fall.xf(Matrix.Rotation(math.pi / 4, 4, 'Z'))
            fall.xf(Matrix.Rotation(math.pi / 2 - 0.05, 4, 'Y'))
            fall.at((1.4, 0.6, 0.4), (0, 0, 0.5))
            p.add(fall)
        else:
            for k in range(3):
                ln = rng.uniform(1.4, 2.4)
                pc = lathe([(0.55, 0.0), (0.5, ln)], 4, STONE, wt(rng, 0.75, 0.9, 0.5), ang=30)
                pc.xf(Matrix.Rotation(math.pi / 4, 4, 'Z'))
                pc.xf(Matrix.Rotation(math.pi / 2, 4, 'Y'))
                pc.at((rng.uniform(-3, 3), rng.uniform(1.4, 3.0) * (1 if k % 2 else -1), 0.35), (0, 0, rng.uniform(0, 6)))
                p.add(pc)
            _rubble(p, rng, STONE, (0, 0), 2.6, 8 if lod == 0 else 4, (0.15, 0.35))
    return p


def statue_monument(lod, rng, state='Intact'):
    """Atomic / Space age: a polished steel arc rising from a round granite plaza disc."""
    p = Part()
    seg = (24, 14, 8)[lod]
    disc = lathe([(0.0, -0.3), (3.0, -0.3), (3.0, 0.25), (2.85, 0.32), (0.0, 0.32)], seg, STONER, wt(rng, 0.85, 1.0, 0.1))
    p.add(disc)
    n = (28, 16, 8)[lod]
    pts = []
    for k in range(n + 1):
        a = math.pi * k / n
        pts.append((math.cos(a) * 2.2, 0.25 * math.sin(2 * a), 0.3 + math.sin(a) * 5.5))
    if state == 'Damaged':
        pts = pts[: int(n * 0.62)]
        frag = [(x * 0.35 + 1.5, y + 1.2, 0.35 + (z - 0.3) * 0.0) for (x, y, z) in pts[int(n * 0.3):int(n * 0.45)]]
    if state == 'Ruin':
        pts = pts[: int(n * 0.25)]
    p.add(tube(pts, 0.28 if lod < 2 else 0.3, (10, 7, 5)[lod], METAL, caps=True))
    if state == 'Damaged':
        fr = tube([(0, 0, 0), (2.2, 0.2, 0.0)], 0.28, 8, METAL)
        fr.at((0.6, -1.6, 0.6), (0, 0, 0.3))
        p.add(fr)
    if state == 'Ruin':
        for k in range(3):
            fr = tube([(0, 0, 0), (rng.uniform(0.8, 1.6), 0.0, 0.0)], 0.28, 7, METAL, wt(rng, 0.7, 0.85))
            fr.at((rng.uniform(-1.5, 1.5), rng.uniform(-1.5, 1.5), 0.6), (0, 0, rng.uniform(0, 6)))
            p.add(fr)
        _rubble(p, rng, STONER, (0, 0), 2.6, 6 if lod == 0 else 3, (0.15, 0.3))
    return p
