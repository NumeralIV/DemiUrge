# DEMIURGE art - Age0 family: mesh builder and primitives (pure Python, no bpy except MB.to_mesh).
#
# Metres, Z up, front toward -Y. Faces are quads or triangles only (FBX tangents need that).
# Material slots: 0 Structure, 1 Roof, 2 Detail. UVs address the trim-sheet bands of a0_tex.py.
import math

TAU = 2.0 * math.pi
STRUCT, ROOF, DETAIL = 0, 1, 2
PAD = 4.0 / 1024.0


# --------------------------------------------------------------------------- noise

def _h(ix, iy, iz, seed):
    n = (ix * 374761393 + iy * 668265263 + iz * 2147483647 + seed * 144665) & 0xffffffff
    n = ((n ^ (n >> 13)) * 1274126177) & 0xffffffff
    return ((n ^ (n >> 16)) & 0xffffff) / float(0xffffff)


def vnoise(x, y, z, seed=0):
    """Smooth value noise in [-1, 1]."""
    ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz)
    def L(a, b, t): return a + (b - a) * t
    c = [[[_h(ix + i, iy + j, iz + k, seed) for k in (0, 1)] for j in (0, 1)] for i in (0, 1)]
    x00 = L(c[0][0][0], c[1][0][0], fx); x10 = L(c[0][1][0], c[1][1][0], fx)
    x01 = L(c[0][0][1], c[1][0][1], fx); x11 = L(c[0][1][1], c[1][1][1], fx)
    return L(L(x00, x10, fy), L(x01, x11, fy), fz) * 2.0 - 1.0


def fbm(x, y, z, seed=0, oct=3):
    s, a, t, f = 0.0, 1.0, 0.0, 1.0
    for o in range(oct):
        s += a * vnoise(x * f, y * f, z * f, seed + o * 31)
        t += a; a *= 0.5; f *= 2.03
    return s / t


class Rng:
    def __init__(self, seed):
        self.s = (seed * 2654435761 + 12345) & 0xffffffff

    def r(self, a=0.0, b=1.0):
        self.s = (self.s * 1664525 + 1013904223) & 0xffffffff
        return a + (b - a) * (self.s / 4294967296.0)

    def i(self, n):
        return int(self.r(0, n - 1e-6))


# --------------------------------------------------------------------------- vectors

def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))


def norm(a):
    l = length(a)
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-12 else (0.0, 0.0, 1.0)


def lerp3(a, b, t): return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def rotz(p, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2])


def rotx(p, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c)


def roty(p, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)


# --------------------------------------------------------------------------- UV bands

class Band:
    """A horizontal strip of a trim sheet. mu: metres per U unit (tiling); v0..v1 the strip; mv metres per V unit."""

    def __init__(self, v0, v1, mu, mv, u0=None, u1=None):
        self.v0, self.v1, self.mu, self.mv = v0, v1, mu, mv
        self.u0, self.u1 = u0, u1          # set for non-tiling patches
        self.hm = (v1 - v0) * mv           # band height in metres

    def uv(self, xm, ym):
        """xm metres along U, ym metres up the band (clamped into the band)."""
        v = self.v0 + PAD + ym / self.mv
        v = min(max(v, self.v0 + PAD), self.v1 - PAD)
        if self.u0 is not None:
            u = self.u0 + PAD + (self.u1 - self.u0 - 2 * PAD) * min(max(xm, 0.0), 1.0)
            return (u, v)
        return (xm / self.mu, v)

    def uvt(self, xm, t):
        """t in 0..1 across the band height."""
        return self.uv(xm, t * self.hm)


S_BARK = Band(0.0, 0.25, 4.0, 4.0)
S_WOOD = Band(0.25, 0.5, 4.0, 4.0)
S_WATTLE = Band(0.5, 0.75, 4.0, 4.0)
S_EARTH = Band(0.75, 1.0, 4.0, 4.0)
R_THATCH = Band(0.0, 0.5, 4.0, 4.0)
R_HIDE = Band(0.5, 1.0, 4.0, 4.0)
D_STONE = Band(0.0, 0.5, 8.0, 8.0)
D_LEATHER = Band(0.5, 0.625, 4.0, 4.0)
D_PAINT = Band(0.625, 0.75, 4.0, 4.0)
D_EMBER = Band(0.75, 0.875, 1.0, 0.5, 0.0, 0.25)
D_CHAR = Band(0.75, 0.875, 1.0, 0.5, 0.25, 0.5)
D_INTERIOR = Band(0.75, 0.875, 1.0, 0.5, 0.5, 1.0)
D_ROPE = Band(0.875, 1.0, 4.0, 4.0)

BAND_MAT = {}
for _b in (S_BARK, S_WOOD, S_WATTLE, S_EARTH):
    BAND_MAT[id(_b)] = STRUCT
for _b in (R_THATCH, R_HIDE):
    BAND_MAT[id(_b)] = ROOF
for _b in (D_STONE, D_LEATHER, D_PAINT, D_EMBER, D_CHAR, D_INTERIOR, D_ROPE):
    BAND_MAT[id(_b)] = DETAIL


def mat_of(band):
    return BAND_MAT[id(band)]


# --------------------------------------------------------------------------- builder

class MB:
    def __init__(self):
        self.V, self.F, self.UV, self.M = [], [], [], []

    def v(self, p):
        self.V.append((float(p[0]), float(p[1]), float(p[2])))
        return len(self.V) - 1

    def f(self, ids, uvs, m):
        self.F.append(tuple(ids)); self.UV.append(tuple(uvs)); self.M.append(m)

    def tris(self):
        return sum(len(f) - 2 for f in self.F)

    def append(self, o, xf=None):
        base = len(self.V)
        for p in o.V:
            self.V.append(xf(p) if xf else p)
        for f, uv, m in zip(o.F, o.UV, o.M):
            self.F.append(tuple(i + base for i in f)); self.UV.append(uv); self.M.append(m)

    def bounds(self):
        xs = [p[0] for p in self.V]; ys = [p[1] for p in self.V]; zs = [p[2] for p in self.V]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

    def wrapped_uvs(self):
        """UVs with every connected piece shifted by a whole number of U tiles so it sits around U = 0.
        The trim bands tile in U (and the patch bands sample the same texels one tile over), so an integer shift is
        invisible, but it keeps |U| small: the per-piece random offsets (seed * 0.37 m and similar) reached U = 340,
        which 16-bit UVs in Unreal cannot hold (half-float step 0.25 there, i.e. 1 m of texture)."""
        par = list(range(len(self.V)))

        def find(i):
            while par[i] != i:
                par[i] = par[par[i]]
                i = par[i]
            return i
        for f in self.F:
            r0 = find(f[0])
            for i in f[1:]:
                ri = find(i)
                if ri != r0:
                    par[ri] = r0
        lo, hi = {}, {}
        for f, uvs in zip(self.F, self.UV):
            r = find(f[0])
            for uv in uvs:
                lo[r] = min(lo.get(r, uv[0]), uv[0]); hi[r] = max(hi.get(r, uv[0]), uv[0])
        shift = {r: math.floor((lo[r] + hi[r]) * 0.5) for r in lo}
        return [tuple((uv[0] - shift[find(f[0])], uv[1]) for uv in uvs) for f, uvs in zip(self.F, self.UV)]

    def to_mesh(self, name):
        import bpy
        me = bpy.data.meshes.new(name)
        me.from_pydata(self.V, [], [list(f) for f in self.F])
        uvl = me.uv_layers.new(name='UVMap')
        flat = [c for uvs in self.wrapped_uvs() for uv in uvs for c in uv]
        uvl.data.foreach_set('uv', flat)
        me.polygons.foreach_set('material_index', self.M)
        me.polygons.foreach_set('use_smooth', [True] * len(self.F))
        me.validate(clean_customdata=False)
        me.update()
        return me


# --------------------------------------------------------------------------- primitives

def grid(mb, P, U, mat, keep=None, wrap=False, cap=False):
    """P[i][j] points, U[i][j] uvs (i rows bottom->top, j around/along). wrap: j wraps (U has segs+1 cols).
    Quads (i,j)-(i,j+1)-(i+1,j+1)-(i+1,j) face outward when j runs counter-clockwise seen from outside."""
    rows = len(P)
    cols = len(P[0])
    idx = [[mb.v(p) for p in row] for row in P]
    nj = cols if wrap else cols - 1
    for i in range(rows - 1):
        for j in range(nj):
            j1 = (j + 1) % cols
            if keep is not None and not keep(i, j):
                continue
            ids = [idx[i][j], idx[i][j1], idx[i + 1][j1], idx[i + 1][j]]
            uvs = [U[i][j], U[i][j + 1], U[i + 1][j + 1], U[i + 1][j]]
            # collapse degenerate (apex) quads into triangles
            if ids[2] == ids[3] or P[i + 1][j1] == P[i + 1][j]:
                if length(sub(P[i + 1][j1], P[i + 1][j])) < 1e-7:
                    mb.f([ids[0], ids[1], ids[2]], [uvs[0], uvs[1], uvs[2]], mat); continue
            if length(sub(P[i][j1], P[i][j])) < 1e-7:
                mb.f([ids[0], ids[2], ids[3]], [uvs[0], uvs[2], uvs[3]], mat); continue
            mb.f(ids, uvs, mat)
    return idx


def fan(mb, center, ring_pts, uvc, ring_uvs, mat, flip=False):
    c = mb.v(center)
    ids = [mb.v(p) for p in ring_pts]
    n = len(ids)
    for j in range(n):
        j1 = (j + 1) % n
        if flip:
            mb.f([c, ids[j1], ids[j]], [uvc, ring_uvs[j1], ring_uvs[j]], mat)
        else:
            mb.f([c, ids[j], ids[j1]], [uvc, ring_uvs[j], ring_uvs[j1]], mat)


def frame_for(t):
    t = norm(t)
    ref = (0.0, 0.0, 1.0) if abs(t[2]) < 0.9 else (1.0, 0.0, 0.0)
    nrm = norm(cross(ref, t))
    bin_ = cross(t, nrm)
    return nrm, bin_


def tube(mb, pts, radii, sides, band, cap0=False, cap1=False, tip=None, seed=0, rough=0.0, keep=None):
    """Tube along a polyline. U runs along the length (metres), V around (fits the band height).
    tip: length of a sharpened point added after the last ring. rough: radial noise amplitude (fraction)."""
    mat = mat_of(band)
    n = len(pts)
    # parallel-transport frames
    tans = []
    for k in range(n):
        a = pts[max(k - 1, 0)]; b = pts[min(k + 1, n - 1)]
        tans.append(norm(sub(b, a)))
    nr, bi = frame_for(tans[0])
    frames = []
    for k in range(n):
        t = tans[k]
        nr = norm(sub(nr, mul(t, dot(nr, t))))
        bi = cross(t, nr)
        frames.append((nr, bi))
    s = [0.0]
    for k in range(1, n):
        s.append(s[-1] + length(sub(pts[k], pts[k - 1])))
    rmax = max(radii)
    circ = TAU * rmax
    vt = min(1.0, circ / band.hm)
    P, U = [], []
    for k in range(n):
        nr, bi = frames[k]
        row, urow = [], []
        for j in range(sides + 1):
            a = TAU * j / sides
            jj = j % sides
            rr = radii[k] * (1.0 + rough * vnoise(math.cos(a) * 1.3, math.sin(a) * 1.3, s[k] * 1.7, seed))
            off = add(mul(nr, math.cos(a) * rr), mul(bi, math.sin(a) * rr))
            if j < sides:
                row.append(add(pts[k], off))
            urow.append(band.uvt(s[k] + (seed * 0.37) % band.mu, j / sides * vt))
        P.append(row); U.append(urow)
    idx = grid(mb, P, U, mat, keep=keep, wrap=True)
    # wrap=True with U of sides+1 columns: grid uses U[i][j+1] for the seam (j+1 == sides)
    if cap0:
        c = mb.v(pts[0])
        for j in range(sides):
            j1 = (j + 1) % sides
            a0 = TAU * j / sides; a1 = TAU * j1 / sides
            mb.f([c, idx[0][j1], idx[0][j]],
                 [S_WOOD.uv(0.3, 0.5), S_WOOD.uv(0.3 + 0.2 * math.cos(a1), 0.5 + 0.2 * math.sin(a1)),
                  S_WOOD.uv(0.3 + 0.2 * math.cos(a0), 0.5 + 0.2 * math.sin(a0))], mat)
    if tip is not None or cap1:
        nrm_t = tans[-1]
        end = add(pts[-1], mul(nrm_t, tip if tip else 0.0))
        c = mb.v(end)
        for j in range(sides):
            j1 = (j + 1) % sides
            if tip:
                mb.f([idx[-1][j], idx[-1][j1], c], [U[-1][j], U[-1][j + 1], band.uvt(s[-1] + tip + (seed * 0.37) % band.mu, (j + 0.5) / sides * vt)], mat)
            else:
                a0 = TAU * j / sides; a1 = TAU * j1 / sides
                mb.f([idx[-1][j], idx[-1][j1], c],
                     [S_WOOD.uv(0.3 + 0.2 * math.cos(a0), 0.5 + 0.2 * math.sin(a0)),
                      S_WOOD.uv(0.3 + 0.2 * math.cos(a1), 0.5 + 0.2 * math.sin(a1)), S_WOOD.uv(0.3, 0.5)], mat)
    return s[-1]


def pole(mb, a, b, r0, r1=None, sides=6, band=S_BARK, segs=1, bend=0.0, seed=0, tip=None, caps=(False, True), rough=0.08, keep=None):
    """Straight-ish pole from a to b, optional sideways bend (metres at mid)."""
    r1 = r0 * 0.8 if r1 is None else r1
    pts, rad = [], []
    d = sub(b, a)
    side = norm(cross(d, (0.0, 0.0, 1.0))) if abs(norm(d)[2]) < 0.95 else (1.0, 0.0, 0.0)
    rng = Rng(seed)
    side = norm(add(side, (rng.r(-0.5, 0.5), rng.r(-0.5, 0.5), 0.0)))
    for k in range(segs + 1):
        t = k / segs
        p = add(a, mul(d, t))
        p = add(p, mul(side, bend * math.sin(math.pi * t)))
        pts.append(p); rad.append(r0 + (r1 - r0) * t)
    return tube(mb, pts, rad, sides, band, cap0=caps[0], cap1=caps[1] and tip is None, tip=tip, seed=seed, rough=rough, keep=keep)


def rock(mb, c, size, seed, segs=7, rings=4, flat=0.75, sink=0.25, band=D_STONE, sharp=0.35, yaw=None, vlift=0.25):
    """Field stone: deformed spheroid sitting on the ground with `sink` of its height below c.z.
    vlift: metres added to V so a small stone does not sample only the mossy ground strip of the stone band."""
    rng = Rng(seed)
    sx, sy, sz = size
    yaw = rng.r(0, TAU) if yaw is None else yaw
    P, U = [], []
    zb = c[2] - sz * sink
    for i in range(rings + 1):
        th = math.pi * (i / rings)          # 0 bottom .. pi top
        z0 = -math.cos(th)
        rr = math.sin(th)
        row, urow = [], []
        for j in range(segs + 1):
            a = TAU * j / segs
            if i in (0, rings):
                pt = (0.0, 0.0, z0)
            else:
                pt = (math.cos(a) * rr, math.sin(a) * rr, z0)
            k = 1.0 + sharp * fbm(pt[0] * 1.3 + seed, pt[1] * 1.3, pt[2] * 1.3, seed, 2)
            q = (pt[0] * sx * 0.5 * k, pt[1] * sy * 0.5 * k, (pt[2] * 0.5 + 0.5) * sz * (flat if pt[2] > 0 else 1.0) * (k if pt[2] > 0 else 1.0))
            q = rotz(q, yaw)
            q = (c[0] + q[0], c[1] + q[1], zb + q[2])
            if j < segs:
                row.append(q)
            urow.append(band.uv(j / segs * TAU * max(sx, sy) * 0.5 + (seed * 1.7) % band.mu, max(0.0, q[2] - zb) + vlift))
        P.append(row); U.append(urow)
    # rows 0 and `rings` are poles: grid collapses them into triangles
    return grid(mb, P, U, mat_of(band), wrap=True)


def box(mb, c, half, band, yaw=0.0, top=True, bottom=False):
    hx, hy, hz = half
    corners = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
               (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]
    W = [add(c, rotz(p, yaw)) for p in corners]
    m = mat_of(band)
    faces = [((0, 1, 5, 4), 2 * hx, 2 * hz), ((1, 2, 6, 5), 2 * hy, 2 * hz), ((2, 3, 7, 6), 2 * hx, 2 * hz), ((3, 0, 4, 7), 2 * hy, 2 * hz)]
    if top:
        faces.append(((4, 5, 6, 7), 2 * hx, 2 * hy))
    if bottom:
        faces.append(((3, 2, 1, 0), 2 * hx, 2 * hy))
    for f, w, h in faces:
        ids = [mb.v(W[i]) for i in f]
        uvs = [band.uv(0, 0), band.uv(w, 0), band.uv(w, h), band.uv(0, h)]
        mb.f(ids, uvs, m)


def disc(mb, c, r, segs, band, z=None, noise=0.0, seed=0, up=True, rings=1):
    """Flat (or gently domed) disc; UV: planar in metres for tiling bands, or 0..1 for patches."""
    m = mat_of(band)
    cz = c[2] if z is None else z
    def uvp(x, y):
        if band.u0 is not None:
            return band.uv(0.5 + x / (2 * r), 0.5 * band.hm + y / (2 * r) * band.hm * 0.98)
        return band.uv(x, (y / (2 * r) + 0.5) * min(band.hm, 2 * r))
    P, U = [], []
    for i in range(rings + 1):
        rr = r * i / rings
        row, urow = [], []
        for j in range(segs + 1):
            a = TAU * j / segs
            x, y = math.cos(a) * rr, math.sin(a) * rr
            dz = noise * fbm(x * 0.8 + seed, y * 0.8, 0.3, seed, 2) * (i / rings)
            if j < segs:
                row.append((c[0] + x, c[1] + y, cz + dz))
            urow.append(uvp(x, y))
        P.append(row); U.append(urow)
    # rings go outward; with j counter-clockwise the quads face up when rows run inner->outer reversed
    P.reverse(); U.reverse()
    if not up:
        P.reverse(); U.reverse()
    return grid(mb, P, U, m, wrap=True)


def lathe(mb, prof, segs, band, c=(0, 0, 0), vmetres=None, uscale=None, keep=None, disp=None, a0=0.0, rx=1.0, ry=1.0):
    """Surface of revolution. prof: [(r, z)] bottom->top. V follows profile arc length (metres, or vmetres list).
    disp(a, i) -> radial multiplier."""
    m = mat_of(band)
    s = [0.0]
    for i in range(1, len(prof)):
        s.append(s[-1] + math.hypot(prof[i][0] - prof[i - 1][0], prof[i][1] - prof[i - 1][1]))
    vm = vmetres if vmetres is not None else s
    if vm[-1] > band.hm:
        k = band.hm / vm[-1]
        vm = [x * k for x in vm]
    rref = max(r for r, z in prof)
    ut = uscale if uscale is not None else max(1, round(TAU * rref / band.mu)) * band.mu
    P, U = [], []
    for i, (r, z) in enumerate(prof):
        row, urow = [], []
        for j in range(segs + 1):
            a = a0 + TAU * j / segs
            k = disp(a, i) if disp else 1.0
            if j < segs:
                row.append((c[0] + math.cos(a) * r * k * rx, c[1] + math.sin(a) * r * k * ry, c[2] + z))
            urow.append(band.uv(j / segs * ut, vm[i]))
        P.append(row); U.append(urow)
    return grid(mb, P, U, m, keep=keep, wrap=True)
