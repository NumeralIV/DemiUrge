# DEMIURGE art/culture - geometry helpers (bmesh builders, UVs, object utilities).
#
# Every asset is built into one Geo (a bmesh) whose faces carry:
#   material_index  0 Structure, 1 Roof, 2 Detail (ASSETS.md: at most these three slots)
#   'uvm'  0 box-projected world UVs (recomputed at the end), 1 UVs set by the primitive, 2 trim band
#   'band' trim-sheet band for uvm 2 (cul_tex.TRIM_BANDS)
#   'part' state tag: BODY, SHELL, FRAME, DETAIL, FIX (see cul_states)
# Units are metres, Z up, front toward -Y.
import math
import random
import bmesh
import bpy
from mathutils import Vector, Matrix, noise

MATS = ('Structure', 'Roof', 'Detail')
MI = {'Structure': 0, 'Roof': 1, 'Detail': 2}
TILE = {0: 4.0, 1: 4.0, 2: 2.0}        # metres per UV tile per slot (matches cul_tex.TILE_M)
UV_BOX, UV_SET, UV_TRIM = 0, 1, 2
BODY, SHELL, FRAME, DETAIL, FIX = 0, 1, 2, 3, 4
BAND = {n: i for i, n in enumerate(['oak', 'tar', 'cobalt', 'saffron', 'rope', 'palestone', 'reed', 'window'])}


def V(*a):
    return Vector(a if len(a) == 3 else a[0])


class Geo:
    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new('UVMap')
        self.uvm = self.bm.faces.layers.int.new('uvm')
        self.band = self.bm.faces.layers.int.new('band')
        self.part = self.bm.faces.layers.int.new('part')

    # ------------------------------------------------------------------ marking
    def mark(self, faces, mat, mode=UV_BOX, band='oak', part=BODY):
        mi = MI[mat]
        b = BAND[band] if isinstance(band, str) else band
        for f in faces:
            f.material_index = mi
            f[self.uvm] = mode
            f[self.band] = b
            f[self.part] = part
        return faces

    @staticmethod
    def _faces_of(verts):
        fs = set()
        for v in verts:
            fs.update(v.link_faces)
        return list(fs)

    # ------------------------------------------------------------------ primitives
    def box(self, c, size, mat='Structure', rz=0.0, chamfer=0.0, part=BODY, band='oak', mode=None, rot=None):
        """Axis box centred at c with size (sx, sy, sz), rotated rz about Z (or full rot Matrix)."""
        R = rot if rot is not None else Matrix.Rotation(rz, 4, 'Z')
        M = Matrix.Translation(V(c)) @ R @ Matrix.Diagonal((size[0], size[1], size[2], 1.0))
        ret = bmesh.ops.create_cube(self.bm, size=1.0, matrix=M)
        verts = ret['verts']
        faces = self._faces_of(verts)
        if chamfer > 0:
            edges = list({e for f in faces for e in f.edges})
            r = bmesh.ops.bevel(self.bm, geom=edges + verts, offset=min(chamfer, min(size) * 0.45), segments=1,
                                affect='EDGES', profile=0.5, clamp_overlap=True)
            faces = list(set(r['faces']) | {f for v in r['verts'] for f in v.link_faces})
        md = mode if mode is not None else (UV_TRIM if mat == 'Detail' else UV_BOX)
        return self.mark(faces, mat, md, band, part)

    def beam(self, a, b, w, h=None, mat='Detail', band='oak', part=FRAME, up=(0, 0, 1), chamfer=0.0):
        """Rectangular beam from point a to point b, cross-section w x h."""
        a, b = V(a), V(b)
        h = w if h is None else h
        d = b - a
        L = d.length
        if L < 1e-6:
            return []
        x = d / L
        upv = V(up)
        if abs(x.dot(upv)) > 0.95:
            upv = V(1, 0, 0) if abs(x.x) < 0.9 else V(0, 1, 0)
        y = upv.cross(x).normalized()
        z = x.cross(y)
        R = Matrix((x, y, z)).transposed().to_4x4()
        return self.box((a + b) * 0.5, (L, w, h), mat, rot=R, part=part, band=band, chamfer=chamfer)

    def lathe(self, prof, seg, c=(0, 0, 0), mat='Structure', part=BODY, cap_top=False, cap_bot=False,
              uv_r=None, sx=1.0, sy=1.0, phase=0.0, arc=None, mats=None, band='oak', mode=UV_SET, jitter=0.0, seed=0):
        """Surface of revolution. prof = [(r, z), ...] from bottom to top. UVs: u around (metres at uv_r),
        v along the profile arc length (metres), divided by the slot tile size.
        arc=(a0, a1) builds an open sector. mats: per-profile-segment material names."""
        c = V(c)
        n = len(prof)
        closed = arc is None
        a0, a1 = (0.0, 2 * math.pi) if closed else arc
        cols = seg if closed else seg + 1
        rng = random.Random(seed)
        ring = []
        for i in range(cols):
            a = a0 + (a1 - a0) * i / seg + phase
            ca, sa = math.cos(a), math.sin(a)
            row = []
            for j, (r, z) in enumerate(prof):
                jr = 1.0 + (rng.uniform(-jitter, jitter) if jitter and 0 < j < n - 1 else 0.0)
                row.append(self.bm.verts.new(c + V(r * ca * sx * jr, r * sa * sy * jr, z)))
            ring.append(row)
        # arc lengths along the profile
        sv = [0.0]
        for j in range(1, n):
            sv.append(sv[-1] + math.hypot(prof[j][0] - prof[j - 1][0], prof[j][1] - prof[j - 1][1]))
        rref = uv_r if uv_r is not None else max(p[0] for p in prof)
        faces = []
        for i in range(seg):
            i2 = (i + 1) % cols if closed else i + 1
            for j in range(n - 1):
                if prof[j][0] < 1e-6 and prof[j + 1][0] < 1e-6:
                    continue
                q = [(i, i, j), (i2, i + 1, j), (i2, i + 1, j + 1), (i, i, j + 1)]   # (column, u index, profile)
                if prof[j][0] < 1e-6:
                    q = [q[0], q[2], q[3]]
                elif prof[j + 1][0] < 1e-6:
                    q = [q[0], q[1], q[2]]
                try:
                    f = self.bm.faces.new([ring[cc][jj] for cc, _, jj in q])
                except ValueError:
                    continue
                mname = mats[j] if mats else mat
                md = UV_TRIM if mname == 'Detail' else mode
                self.mark([f], mname, md, band, part)
                tile = TILE[MI[mname]]
                ua = (a1 - a0) * rref / tile
                for lp, (_, ui, jj) in zip(f.loops, q):
                    lp[self.uv].uv = (ui / seg * ua, sv[jj] / tile)
                faces.append(f)
        if cap_top and prof[-1][0] > 1e-6:
            vs = [ring[i][n - 1] for i in range(cols)]
            f = self.bm.faces.new(vs)
            self.mark([f], mats[-1] if mats else mat, UV_BOX, band, part)
            faces.append(f)
        if cap_bot and prof[0][0] > 1e-6:
            vs = [ring[i][0] for i in range(cols)][::-1]
            f = self.bm.faces.new(vs)
            self.mark([f], mats[0] if mats else mat, UV_BOX, band, part)
            faces.append(f)
        # merge pole vertices (radius 0) into one
        poles = [ring[i][j] for j in (0, n - 1) for i in range(cols) if prof[j][0] < 1e-6]
        if poles:
            bmesh.ops.remove_doubles(self.bm, verts=poles, dist=1e-5)
        return [f for f in faces if f.is_valid]

    def cyl(self, c, r, h, seg, mat='Structure', part=BODY, r2=None, caps=True, band='oak', mode=UV_SET, phase=0.0):
        r2 = r if r2 is None else r2
        return self.lathe([(r, 0.0), (r2, h)], seg, c, mat, part, cap_top=caps, cap_bot=caps, band=band,
                          mode=mode if mat != 'Detail' else UV_TRIM, phase=phase)

    def tube(self, pts, r, seg=6, mat='Detail', band='rope', part=DETAIL, caps=True, closed=False, up=(0, 0, 1),
             r_fn=None):
        """Sweep a circle of radius r (or r_fn(t)) along a polyline."""
        pts = [V(p) for p in pts]
        n = len(pts)
        rings = []
        upv = V(up)
        for k, p in enumerate(pts):
            if closed:
                d = (pts[(k + 1) % n] - pts[k - 1])
            else:
                d = (pts[min(k + 1, n - 1)] - pts[max(k - 1, 0)])
            d = d.normalized()
            u0 = upv if abs(d.dot(upv)) < 0.95 else V(1, 0, 0)
            x = u0.cross(d).normalized()
            y = d.cross(x)
            rr = r_fn(k / max(1, n - 1)) if r_fn else r
            ring = [self.bm.verts.new(p + (x * math.cos(2 * math.pi * s / seg) + y * math.sin(2 * math.pi * s / seg)) * rr)
                    for s in range(seg)]
            rings.append(ring)
        faces = []
        segs = n if closed else n - 1
        for k in range(segs):
            a, b = rings[k], rings[(k + 1) % n]
            for s in range(seg):
                faces.append(self.bm.faces.new([a[s], a[(s + 1) % seg], b[(s + 1) % seg], b[s]]))
        if caps and not closed:
            faces.append(self.bm.faces.new(rings[0][::-1]))
            faces.append(self.bm.faces.new(rings[-1]))
        md = UV_TRIM if mat == 'Detail' else UV_BOX
        return self.mark(faces, mat, md, band, part)

    def prism(self, pts2, z0, z1, mat='Structure', part=BODY, band='oak', c=(0, 0, 0), rz=0.0):
        """Extrude a closed 2D polygon (counter-clockwise) between z0 and z1."""
        c = V(c)
        R = Matrix.Rotation(rz, 3, 'Z')
        bot = [self.bm.verts.new(c + R @ V(x, y, z0)) for x, y in pts2]
        top = [self.bm.verts.new(c + R @ V(x, y, z1)) for x, y in pts2]
        n = len(pts2)
        faces = [self.bm.faces.new(bot[::-1]), self.bm.faces.new(top)]
        for i in range(n):
            j = (i + 1) % n
            faces.append(self.bm.faces.new([bot[i], bot[j], top[j], top[i]]))
        md = UV_TRIM if mat == 'Detail' else UV_BOX
        return self.mark(faces, mat, md, band, part)

    def grid_shell(self, fn, nu, nv, thick, mat='Roof', part=SHELL, uv_fn=None, edge_mat=None, band='oak',
                   closed_u=False):
        """Solid shell from a parametric surface fn(u, v) -> Vector, u, v in 0..1, offset by -thick along
        the surface normal (downwards / inwards). uv_fn(u, v) -> (U, V) in metres (divided by the tile).
        Returns faces. Rim faces use edge_mat (default mat)."""
        cu = nu if closed_u else nu + 1
        P = [[fn(i / nu, j / nv) for j in range(nv + 1)] for i in range(cu)]
        # normals by central differences
        def nrm(i, j):
            i0, i1 = (i - 1) % cu if closed_u else max(i - 1, 0), (i + 1) % cu if closed_u else min(i + 1, cu - 1)
            j0, j1 = max(j - 1, 0), min(j + 1, nv)
            du = P[i1][j] - P[i0][j]
            dv = P[i][j1] - P[i][j0]
            n = du.cross(dv)
            return n.normalized() if n.length > 1e-9 else V(0, 0, 1)
        top = [[self.bm.verts.new(P[i][j]) for j in range(nv + 1)] for i in range(cu)]
        bot = [[self.bm.verts.new(P[i][j] - nrm(i, j) * thick) for j in range(nv + 1)] for i in range(cu)]
        tile = TILE[MI[mat]]
        faces_top, faces_rim = [], []
        segu = nu
        for i in range(segu):
            i2 = (i + 1) % cu
            for j in range(nv):
                qt = [(i, i, j), (i2, i + 1, j), (i2, i + 1, j + 1), (i, i, j + 1)]
                qb = qt[::-1]
                ft = self.bm.faces.new([top[cc][jj] for cc, _, jj in qt])
                fb = self.bm.faces.new([bot[cc][jj] for cc, _, jj in qb])
                for f, q in ((ft, qt), (fb, qb)):
                    self.mark([f], mat, UV_SET if uv_fn else UV_BOX, band, part)
                    if uv_fn:
                        for lp, (_, ui, jj) in zip(f.loops, q):
                            U, Vv = uv_fn(ui / nu, jj / nv)
                            lp[self.uv].uv = (U / tile, Vv / tile)
                faces_top.append(ft)
                faces_top.append(fb)
        em = edge_mat or mat
        rims = []
        if not closed_u:
            for j in range(nv):
                rims.append([top[0][j + 1], top[0][j], bot[0][j], bot[0][j + 1]])
                rims.append([top[segu][j], top[segu][j + 1], bot[segu][j + 1], bot[segu][j]])
        for i in range(segu):
            i2 = (i + 1) % cu
            rims.append([top[i][0], top[i2][0], bot[i2][0], bot[i][0]][::-1])
            rims.append([top[i][nv], top[i2][nv], bot[i2][nv], bot[i][nv]])
        for vs in rims:
            try:
                f = self.bm.faces.new(vs)
            except ValueError:
                continue
            md = UV_TRIM if em == 'Detail' else UV_BOX
            self.mark([f], em, md, band, part)
            faces_rim.append(f)
        bmesh.ops.recalc_face_normals(self.bm, faces=faces_top + faces_rim)
        return faces_top + faces_rim

    def blob(self, c, r, mat='Structure', part=BODY, subdiv=2, squash=1.0, rough=0.3, seed=0, band='oak', scale=None):
        """Lumpy rock / rubble / plant clump from an icosphere."""
        ret = bmesh.ops.create_icosphere(self.bm, subdivisions=subdiv, radius=1.0, matrix=Matrix.Identity(4))
        verts = ret['verts']
        rng = random.Random(seed)
        off = V(rng.uniform(-50, 50), rng.uniform(-50, 50), rng.uniform(-50, 50))
        sc = V(scale) if scale else V(r, r, r * squash)
        for v in verts:
            d = v.co.normalized()
            k = 1.0 + rough * noise.noise(d * 1.7 + off) + rough * 0.5 * noise.noise(d * 4.1 + off)
            v.co = V(c) + V(d.x * sc.x, d.y * sc.y, d.z * sc.z) * k
        faces = self._faces_of(verts)
        md = UV_TRIM if mat == 'Detail' else UV_BOX
        return self.mark(faces, mat, md, band, part)

    def quad(self, pts, mat='Detail', band='oak', part=DETAIL, double=False):
        vs = [self.bm.verts.new(V(p)) for p in pts]
        fs = [self.bm.faces.new(vs)]
        if double:
            vs2 = [self.bm.verts.new(V(p)) for p in pts]
            fs.append(self.bm.faces.new(vs2[::-1]))
        md = UV_TRIM if mat == 'Detail' else UV_BOX
        return self.mark(fs, mat, md, band, part)

    def transform(self, faces, M):
        vs = {v for f in faces for v in f.verts}
        bmesh.ops.transform(self.bm, matrix=M, verts=list(vs))

    def stats(self):
        return sum(len(f.verts) - 2 for f in self.bm.faces)


# ---------------------------------------------------------------------- finishing


def finish_uvs(bm):
    """Box UVs for uvm==0 faces (metres / tile) and trim-band UVs for uvm==2 faces."""
    uv = bm.loops.layers.uv.get('UVMap') or bm.loops.layers.uv.new('UVMap')
    uvm = bm.faces.layers.int.get('uvm')
    band = bm.faces.layers.int.get('band')
    for f in bm.faces:
        m = f[uvm] if uvm else UV_BOX
        if m == UV_SET:
            continue
        n = f.normal
        ax = max(range(3), key=lambda k: abs(n[k]))
        if ax == 2:
            A, B = 0, 1
        elif ax == 0:
            A, B = 1, 2
        else:
            A, B = 0, 2
        sgn = 1.0 if n[ax] >= 0 else -1.0
        if m == UV_TRIM:
            cs = [(l.vert.co[A], l.vert.co[B]) for l in f.loops]
            ea = max(c[0] for c in cs) - min(c[0] for c in cs)
            eb = max(c[1] for c in cs) - min(c[1] for c in cs)
            # u runs along the longer side of the face
            if eb > ea:
                cs = [(b, a) for a, b in cs]
                ea, eb = eb, ea
            bmin = min(c[1] for c in cs)
            span = max(eb, 0.25)
            k = f[band] if band else 0
            for l, (a, b) in zip(f.loops, cs):
                vr = (b - bmin) / span
                l[uv].uv = (a * sgn / TILE[2], (k + 0.06 + 0.88 * vr) / 8.0)
        else:
            t = TILE.get(f.material_index, 4.0)
            for l in f.loops:
                co = l.vert.co
                l[uv].uv = (co[A] * sgn / t, co[B] / t)


def bm_to_object(bm, name, coll=None, mats=None):
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    ob = bpy.data.objects.new(name, me)
    (coll or bpy.context.scene.collection).objects.link(ob)
    if mats:
        for m in mats:
            me.materials.append(m)
    return ob


def object_to_bm(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    return bm


def apply_mods(ob):
    """Bake the evaluated mesh (modifiers) into the object's data without operators."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)
    return ob


def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def shade(ob, angle_deg=38.0):
    me = ob.data
    me.polygons.foreach_set('use_smooth', [True] * len(me.polygons))
    try:
        me.set_sharp_from_angle(angle=math.radians(angle_deg))
    except AttributeError:
        pass


def triangulate(ob):
    bm = object_to_bm(ob)
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    bm.to_mesh(ob.data)
    bm.free()


def decimate_to(ob, target):
    t = tri_count(ob)
    if t <= target:
        return t
    m = ob.modifiers.new('dec', 'DECIMATE')
    m.decimate_type = 'COLLAPSE'
    m.ratio = max(0.01, target / t)
    m.use_collapse_triangulate = True
    apply_mods(ob)
    return tri_count(ob)


def bbox(ob):
    cs = [ob.matrix_world @ v.co for v in ob.data.vertices]
    if not cs:
        return V(0, 0, 0), V(0, 0, 0)
    mn = V(min(c.x for c in cs), min(c.y for c in cs), min(c.z for c in cs))
    mx = V(max(c.x for c in cs), max(c.y for c in cs), max(c.z for c in cs))
    return mn, mx
