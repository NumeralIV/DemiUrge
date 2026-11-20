# DEMIURGE art/culture - the culture overlay meshes (roofs, ornaments, structures, landmarks).
#
# Three invented cultures (docs/concept/settlements/culture-a|b|c.png, index.md):
#   A  Cliff-Terrace  rose-ochre plaster cubes with low domes; every roof is the terrace of the house above;
#                     switchback stairs, cisterns, cobalt doors; a great arched niche carved in the cliff.
#   B  Lagoon-Weaver  houses on tarred piles in a lagoon; tall asymmetric saddle roofs of silver shingle with
#                     upswept horns and saffron ribbons; curved boardwalks, floating gardens, a spiral temple.
#   C  Ring-Builder   pale limestone drum houses with tall dark slate cones and ring finials; dry-stone ring
#                     walls; a spire of stacked stone discs; standing stones.
#
# Every generator is f(lod) -> cul_geo.Geo. Space: metres, Z up, front -Y.
# Roof overlays: pivot at the centre of the host footprint ON THE HOST'S EAVE PLANE (z = 0 = wall top).
# Ornaments: pivot at the bottom centre of the host's front door, on the wall face (y = 0), relief toward -Y.
# Structures / landmarks / props: pivot at the centre of the footprint at ground (water) level.
import math
import random
import bmesh
import bpy
from mathutils import Vector, Matrix, noise
import cul_geo as cg
from cul_geo import V, Geo, BODY, SHELL, FRAME, DETAIL, FIX, UV_SET, UV_BOX

Q = (1.0, 0.55, 0.3)

# nominal host size classes (W along X, D along Y, eave height); replaced per host from the Age2/Age3
# manifests when those exist (build_culture.host_classes)
HOST = {
    'S': (7.0, 5.5, 2.9),     # Age2 one-storey house
    'M': (9.5, 6.5, 5.8),     # Age2/3 two-storey house
    'T': (7.2, 11.0, 9.0),    # Age3 townhouse (narrow street front, deep)
    'L': (14.0, 7.0, 3.2),    # long house / hall
}


def sg(n, lod, lo=3):
    return max(lo, int(round(n * Q[lod])))


_DUMMY = []


def dummy_mats():
    if not _DUMMY:
        for n in cg.MATS:
            m = bpy.data.materials.get('_tmp_' + n) or bpy.data.materials.new('_tmp_' + n)
            _DUMMY.append(m)
    return _DUMMY


def scratch_coll():
    c = bpy.data.collections.get('_scratch')
    if not c:
        c = bpy.data.collections.new('_scratch')
        bpy.context.scene.collection.children.link(c)
    return c


def geo_from_mesh(me):
    g = Geo.__new__(Geo)
    g.bm = bmesh.new()
    g.bm.from_mesh(me)
    g.uv = g.bm.loops.layers.uv.get('UVMap') or g.bm.loops.layers.uv.new('UVMap')
    g.uvm = g.bm.faces.layers.int.get('uvm') or g.bm.faces.layers.int.new('uvm')
    g.band = g.bm.faces.layers.int.get('band') or g.bm.faces.layers.int.new('band')
    g.part = g.bm.faces.layers.int.get('part') or g.bm.faces.layers.int.new('part')
    return g


def geo_bool(g, cutter, op='DIFFERENCE'):
    import cul_states as cs
    coll = scratch_coll()
    ob = cg.bm_to_object(g.bm, '_gb', coll, dummy_mats())
    cs.boolean(ob, cutter, op, coll)
    g.bm.free()
    g2 = geo_from_mesh(ob.data)
    cs.remove(ob)
    return g2


def lathe_solid(g, loop, seg, c=(0, 0, 0), mat='Structure', part=BODY, **kw):
    """Closed solid of revolution from a closed (r, z) loop (last point != first)."""
    prof = list(loop) + [loop[0]]
    faces = g.lathe(prof, seg, c, mat, part, **kw)
    vs = list({v for f in faces if f.is_valid for v in f.verts})
    bmesh.ops.remove_doubles(g.bm, verts=vs, dist=1e-4)
    return [f for f in faces if f.is_valid]


def solid8(g, pts, mat='Structure', part=BODY, band='oak'):
    """Hexahedron from 8 points: bottom 0-3 (ccw from above), top 4-7."""
    vs = [g.bm.verts.new(V(p)) for p in pts]
    idx = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    fs = [g.bm.faces.new([vs[i] for i in q]) for q in idx]
    bmesh.ops.recalc_face_normals(g.bm, faces=fs)
    md = cg.UV_TRIM if mat == 'Detail' else UV_BOX
    return g.mark(fs, mat, md, band, part)


def prism_xz(g, pts, y0, y1, mat='Structure', part=BODY, band='oak'):
    """Extrude a polygon given in the XZ plane between y0 and y1."""
    a = [g.bm.verts.new(V(x, y0, z)) for x, z in pts]
    b = [g.bm.verts.new(V(x, y1, z)) for x, z in pts]
    n = len(pts)
    fs = [g.bm.faces.new(a), g.bm.faces.new(b[::-1])]
    for i in range(n):
        j = (i + 1) % n
        fs.append(g.bm.faces.new([a[i], b[i], b[j], a[j]]))
    bmesh.ops.recalc_face_normals(g.bm, faces=fs)
    md = cg.UV_TRIM if mat == 'Detail' else UV_BOX
    return g.mark(fs, mat, md, band, part)


def prism_yz(g, pts, x0, x1, mat='Structure', part=BODY, band='oak'):
    a = [g.bm.verts.new(V(x0, y, z)) for y, z in pts]
    b = [g.bm.verts.new(V(x1, y, z)) for y, z in pts]
    n = len(pts)
    fs = [g.bm.faces.new(a), g.bm.faces.new(b[::-1])]
    for i in range(n):
        j = (i + 1) % n
        fs.append(g.bm.faces.new([a[i], b[i], b[j], a[j]]))
    bmesh.ops.recalc_face_normals(g.bm, faces=fs)
    md = cg.UV_TRIM if mat == 'Detail' else UV_BOX
    return g.mark(fs, mat, md, band, part)


def ring_pts(c, r, n, plane='XY', a0=0.0):
    c = V(c)
    out = []
    for k in range(n):
        a = a0 + 2 * math.pi * k / n
        if plane == 'XY':
            out.append(c + V(r * math.cos(a), r * math.sin(a), 0))
        elif plane == 'XZ':
            out.append(c + V(r * math.cos(a), 0, r * math.sin(a)))
        else:
            out.append(c + V(0, r * math.cos(a), r * math.sin(a)))
    return out


def sweep(g, path, prof, mat='Structure', part=BODY, closed_path=False, tile=None, band='oak', caps=True):
    """Sweep a closed 2D profile [(offset_left, z)] along a path in the XY plane (z added).
    UVs: U = distance along the path, V = distance around the profile (metres / slot tile)."""
    path = [V(p) for p in path]
    n = len(path)
    m = len(prof)
    rings = []
    for k, p in enumerate(path):
        a = path[max(k - 1, 0)] if not closed_path else path[k - 1]
        b = path[min(k + 1, n - 1)] if not closed_path else path[(k + 1) % n]
        d = (b - a)
        d.z = 0
        d.normalize()
        left = V(-d.y, d.x, 0)
        rings.append([g.bm.verts.new(V(p.x, p.y, 0) + left * o + V(0, 0, p.z + z)) for o, z in prof])
    sl = [0.0]
    for k in range(1, n):
        sl.append(sl[-1] + (path[k] - path[k - 1]).length)
    pv = [0.0]
    for j in range(1, m + 1):
        pv.append(pv[-1] + math.hypot(prof[j % m][0] - prof[j - 1][0], prof[j % m][1] - prof[j - 1][1]))
    t = tile or cg.TILE[cg.MI[mat]]
    faces = []
    segs = n if closed_path else n - 1
    for k in range(segs):
        k2 = (k + 1) % n
        for j in range(m):
            j2 = (j + 1) % m
            q = [(k, j, j), (k2, j, j), (k2, j2, j + 1), (k, j2, j + 1)]
            f = g.bm.faces.new([rings[kk][jj] for kk, jj, _ in q])
            mode = cg.UV_TRIM if mat == 'Detail' else UV_SET
            g.mark([f], mat, mode, band, part)
            for lp, (kk, _, pj) in zip(f.loops, q):
                su = sl[k] if kk == k else (sl[k] + (path[k2] - path[k]).length)
                lp[g.uv].uv = (su / t, pv[pj] / t)
            faces.append(f)
    if caps and not closed_path:
        for ring, rev in ((rings[0], True), (rings[-1], False)):
            f = g.bm.faces.new(ring[::-1] if rev else ring)
            g.mark([f], mat, cg.UV_TRIM if mat == 'Detail' else UV_BOX, band, part)
            faces.append(f)
    bmesh.ops.recalc_face_normals(g.bm, faces=faces)
    return faces


def hip_roof(g, Wb, Db, levels, thick, mat='Roof', part=SHELL, cx=0.0, cy=0.0):
    """Hipped / pyramid roof shell over a Wb x Db eave rectangle. levels = [(inset, z), ...] from the eave
    up; the top level is clamped to the ridge. Returns the top (ridge) points of the outer surface."""
    def rect(inset, z):
        hx = max(Wb / 2 - inset, (Wb - Db) / 2 if Wb > Db else 0.0)
        hy = max(Db / 2 - inset, (Db - Wb) / 2 if Db > Wb else 0.0)
        return [V(cx - hx, cy - hy, z), V(cx + hx, cy - hy, z), V(cx + hx, cy + hy, z), V(cx - hx, cy + hy, z)]

    def build(lv, zoff, outer):
        rows = []
        cache = {}
        for (ins, z) in lv:
            row = []
            for p in rect(ins, z + zoff):
                key = (round(p.x, 4), round(p.y, 4), round(p.z, 4))
                if key not in cache:
                    cache[key] = g.bm.verts.new(p)
                row.append(cache[key])
            rows.append(row)
        sv = [0.0]
        for k in range(1, len(lv)):
            sv.append(sv[-1] + math.hypot(lv[k][0] - lv[k - 1][0], lv[k][1] - lv[k - 1][1]))
        t = cg.TILE[cg.MI[mat]]
        fs = []
        for k in range(len(rows) - 1):
            for i in range(4):
                i2 = (i + 1) % 4
                q = [(k, i), (k, i2), (k + 1, i2), (k + 1, i)]
                vs = []
                qq = []
                for (kk, ii) in q:
                    v = rows[kk][ii]
                    if v not in vs:
                        vs.append(v)
                        qq.append((kk, ii))
                if len(vs) < 3:
                    continue
                if not outer:
                    vs = vs[::-1]
                    qq = qq[::-1]
                try:
                    f = g.bm.faces.new(vs)
                except ValueError:
                    continue
                g.mark([f], mat, UV_SET, 'oak', part)
                for lp, (kk, ii) in zip(f.loops, qq):
                    co = lp.vert.co
                    u = co.x if i in (0, 2) else co.y
                    if i in (1, 2):
                        u = -u if i == 2 else u
                    lp[g.uv].uv = (u / t, sv[kk] / t)
                fs.append(f)
        return rows, fs

    ro, fo = build(levels, 0.0, True)
    inner = [(ins + thick * 0.4, z) for ins, z in levels]
    ri, fi = build(inner, -thick, False)
    rim = []
    for i in range(4):
        i2 = (i + 1) % 4
        f = g.bm.faces.new([ro[0][i2], ro[0][i], ri[0][i], ri[0][i2]])
        g.mark([f], mat, UV_BOX, 'oak', part)
        rim.append(f)
    bmesh.ops.recalc_face_normals(g.bm, faces=fo + fi + rim)
    top = []
    for v in ro[-1]:
        if v.co not in [t for t in top]:
            top.append(v.co.copy())
    return top


def finial_ring(g, c, r, tube=0.07, n=16, part=DETAIL, band='palestone', lod=0):
    pts = ring_pts(V(c), r, sg(n, lod, 6), 'XZ')
    g.tube(pts, tube, seg=sg(6, lod, 4), mat='Detail', band=band, part=part, closed=True)


def ribbon(g, top, length, sway, w=0.22, segs=6, seed=0, part=DETAIL):
    rng = random.Random(seed)
    pts = []
    for k in range(segs + 1):
        f = k / segs
        pts.append(V(top) + V(sway[0] * f * f + rng.uniform(-.04, .04), sway[1] * f * f, -length * f))
    for k in range(segs):
        g.beam(pts[k], pts[k + 1], w, 0.025, mat='Detail', band='saffron', part=part)


# =====================================================================================================
# Culture A - Cliff-Terrace
# =====================================================================================================

def _a_dome(g, cx, cy, z0, s, age, lod):
    seg = sg(24, lod, 8)
    if age == 2:
        g.box((cx, cy, z0 + 0.225), (s, s, 0.45), 'Structure', chamfer=0.05 if lod < 2 else 0)
        zd = z0 + 0.45
        R = s * 0.49
        Hd = R * 0.8
    else:
        dr = s * 0.53
        g.lathe([(dr, z0), (dr, z0 + 0.95)], 8, (cx, cy, 0), 'Structure', BODY, cap_top=True, cap_bot=True,
                phase=math.pi / 8, uv_r=dr)
        if lod < 2:
            for k in range(8):
                a = 2 * math.pi * k / 8
                fd = dr * math.cos(math.pi / 8)
                p = V(cx + math.cos(a) * (fd + 0.01), cy + math.sin(a) * (fd + 0.01), z0 + 0.5)
                g.box(p, (0.035, 0.24, 0.42), 'Detail', rz=a, band='window', part=BODY)
        zd = z0 + 0.95
        R = s * 0.47
        Hd = R * 0.92
    g.lathe([(R + 0.08, zd), (R + 0.08, zd + 0.1), (R, zd + 0.13)], seg, (cx, cy, 0), 'Structure', BODY, uv_r=R)
    n = sg(8, lod, 3)
    prof = [(R * math.cos(0.5 * math.pi * k / n), zd + 0.13 + Hd * math.sin(0.5 * math.pi * k / n)) for k in range(n + 1)]
    prof[-1] = (0.0, prof[-1][1])
    g.lathe(prof, seg, (cx, cy, 0), 'Roof', SHELL, cap_bot=True, uv_r=R)
    top = zd + 0.13 + Hd
    if age == 3 and lod < 2:
        nr = 8 if lod == 0 else 4
        for k in range(nr):
            a = 2 * math.pi * k / nr
            pts = [V(cx + math.cos(a) * (r + 0.02), cy + math.sin(a) * (r + 0.02), z + 0.01) for r, z in prof[:-2]]
            g.tube(pts, 0.05, seg=4, mat='Roof', band='oak', part=SHELL)
    # skeleton for the construction / damaged states: ribs just inside the shell
    for k in range(8):
        a = 2 * math.pi * k / 8 + 0.2
        pts = [V(cx + math.cos(a) * r * 0.93, cy + math.sin(a) * r * 0.93, z - 0.05) for r, z in prof[:-1]]
        pts.append(V(cx, cy, top - 0.08))
        g.tube(pts, 0.06, seg=4, mat='Detail', band='oak', part=FRAME)
    if age == 3:
        g.cyl((cx, cy, top - 0.05), 0.3, 0.5, sg(12, lod, 6), 'Structure', BODY)
        g.lathe([(0.36, top + 0.45), (0.3, top + 0.6), (0.18, top + 0.72), (0.0, top + 0.78)], sg(12, lod, 6),
                (cx, cy, 0), 'Roof', SHELL, cap_bot=True)
        top += 0.78
    g.lathe([(0.0, top - 0.02), (0.1, top + 0.06), (0.07, top + 0.2), (0.0, top + 0.3)], sg(8, lod, 4), (cx, cy, 0),
            'Detail', DETAIL, band='cobalt')


def _a_pavilion(g, px, py, z0, ps, lod, pc):
    for sx in (-1, 1):
        for sy in (-1, 1):
            g.box((px + sx * ps / 2, py + sy * ps / 2, z0 + 0.9), (0.24, 0.24, 1.8), 'Structure', chamfer=pc)
    g.box((px, py, z0 + 1.9), (ps + 0.4, ps + 0.4, 0.22), 'Structure', chamfer=pc)
    g.lathe([(ps * 0.5, z0 + 2.0), (ps * 0.47, z0 + 2.3), (ps * 0.3, z0 + 2.75), (0.0, z0 + 2.95)],
            sg(16, lod, 6), (px, py, 0), 'Roof', SHELL, cap_bot=True)
    g.lathe([(0.0, z0 + 2.93), (0.07, z0 + 3.0), (0.0, z0 + 3.2)], 6, (px, py, 0), 'Detail', DETAIL, band='cobalt')


def _a_terrace_top(g, x0, x1, y0, y1, z0, age, lod, deep=False, dome=True):
    """Parapet, dome(s), spouts (and Age3 pinnacles) on a roof slab whose top is z0, outer edge x0..x1, y0..y1."""
    W, D = x1 - x0, y1 - y0
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    pt, ph = (0.24, 0.55) if age == 2 else (0.26, 0.72)
    pc = 0.08 if lod < 2 else 0.0
    g.box((cx, y1 - pt / 2, z0 + ph / 2), (W, pt, ph), chamfer=pc)
    for sx in (x0 + pt / 2, x1 - pt / 2):
        g.box((sx, cy, z0 + ph / 2), (pt, D - 2 * pt, ph), chamfer=pc)
    gx, gw = cx - W * 0.22, 1.3
    ll = (gx - gw / 2) - x0
    rl = x1 - (gx + gw / 2)
    g.box((x0 + ll / 2, y0 + pt / 2, z0 + ph / 2), (ll, pt, ph), chamfer=pc)
    g.box((x1 - rl / 2, y0 + pt / 2, z0 + ph / 2), (rl, pt, ph), chamfer=pc)
    if not dome:
        pass
    elif age == 2:
        _a_dome(g, cx + W * 0.1, cy + D * 0.08, z0, min(W, D) * 0.6, age, lod)
    elif deep and D < W:      # long along X: big dome toward +X, pavilion toward -X
        _a_dome(g, cx + W * 0.17, cy, z0, min(D * 0.66, W * 0.45), age, lod)
        _a_pavilion(g, cx - W * 0.27, cy - D * 0.12, z0, min(2.0, D * 0.3), lod, pc)
    elif deep:
        _a_dome(g, cx, cy + D * 0.17, z0, min(W * 0.66, D * 0.45), age, lod)
        _a_pavilion(g, cx - W * 0.18, cy - D * 0.27, z0, min(2.0, W * 0.3), lod, pc)
    else:
        _a_dome(g, cx + W * 0.08, cy + D * 0.06, z0, min(W, D) * 0.66, age, lod)
    for sx in (-0.32, 0.3):
        g.box((cx + sx * W, y0 - 0.22, z0 + 0.06), (0.14, 0.55, 0.12), 'Detail', band='palestone', part=DETAIL)
    if age == 3:
        for sx in (x0 + 0.22, x1 - 0.22):
            for sy in (y0 + 0.22, y1 - 0.22):
                g.box((sx, sy, z0 + ph + 0.2), (0.44, 0.44, 0.4), 'Structure', chamfer=pc)
                g.lathe([(0.22, z0 + ph + 0.4), (0.16, z0 + ph + 0.62), (0.0, z0 + ph + 0.78)], sg(8, lod, 4),
                        (sx, sy, 0), 'Roof', SHELL, cap_bot=True)


def _rects(h):
    return h.get('roofs') or [h['roof']]


def _roof_frame(r, m=0.0):
    return (r['min'][0] - m, r['max'][0] + m, r['min'][1] - m, r['max'][1] + m, r['eave_z'], r['ridge_z'])


def a_roof_host(h, age):
    """Cliff-Terrace roof for a host house, in host space. The host's pitched roof is hidden inside a plaster
    attic storey (a jettied cube) whose flat roof is a terrace with a parapet and a dome."""
    def fn(lod):
        g = Geo()
        for k, r in enumerate(_rects(h)):
            _a_roof_rect(g, r, age, lod if k == 0 else min(2, lod + 1), main=(k == 0))
        return g
    return fn


def _a_roof_rect(g, r, age, lod, main=True):
    if True:
        x0, x1, y0, y1, ez, rz = _roof_frame(r, 0.12)
        W, D = x1 - x0, y1 - y0
        zl, zt = ez - 0.3, rz + 0.12
        t = 0.3
        ch = 0.04 if lod < 2 else 0.0
        g.box(((x0 + x1) / 2, y0 + t / 2, (zl + zt) / 2), (W, t, zt - zl), chamfer=ch)
        g.box(((x0 + x1) / 2, y1 - t / 2, (zl + zt) / 2), (W, t, zt - zl), chamfer=ch)
        g.box((x0 + t / 2, (y0 + y1) / 2, (zl + zt) / 2), (t, D - 2 * t, zt - zl), chamfer=ch)
        g.box((x1 - t / 2, (y0 + y1) / 2, (zl + zt) / 2), (t, D - 2 * t, zt - zl), chamfer=ch)
        # a plastered cove under the jetty, from the attic face down to the host wall line (~0.6 m in)
        cw = 0.62
        for (ya, yb) in ((y0, y0 + cw), (y1, y1 - cw)):
            prism_yz(g, [(ya, zl + 0.15), (ya, zl), (yb, zl - cw), (yb, zl + 0.15)], x0, x1)
        for (xa, xb) in ((x0, x0 + cw), (x1, x1 - cw)):
            prism_xz(g, [(xa, zl + 0.15), (xa, zl), (xb, zl - cw), (xb, zl + 0.15)], y0 + 0.01, y1 - 0.01)
        # a few rain spouts / beam ends just above the cove
        if lod < 2:
            for (yy, sgn) in ((y0, -1), (y1, 1)):
                n = max(2, int(W / 2.6))
                for k in range(n):
                    x = x0 + 0.8 + (W - 1.6) * k / (n - 1)
                    g.box((x, yy + sgn * 0.12, zl + 0.25), (0.14, 0.3, 0.14), 'Detail', band='oak', part=DETAIL)
        zw = zl + 0.55 + min(1.1, (zt - zl) * 0.3)
        for side in range(4):
            L = W if side < 2 else D
            n = max(1, int(L / (3.4 if side < 2 else 4.0)))
            for k in range(n):
                f = (k + 0.5) / n
                if side == 0:
                    c, rz_ = V(x0 + W * f, y0 - 0.02, zw), 0.0
                elif side == 1:
                    c, rz_ = V(x0 + W * f, y1 + 0.02, zw), 0.0
                elif side == 2:
                    c, rz_ = V(x0 - 0.02, y0 + D * f, zw), math.pi / 2
                else:
                    c, rz_ = V(x1 + 0.02, y0 + D * f, zw), math.pi / 2
                g.box(c, (0.55, 0.06, 0.72), 'Detail', rz=rz_, band='window', part=BODY)
                if lod < 2:
                    g.box(c + V(0, 0, 0.42), (0.75, 0.1, 0.1), 'Detail', rz=rz_, band='cobalt', part=DETAIL)
                    g.box(c - V(0, 0, 0.42), (0.75, 0.14, 0.08), 'Detail', rz=rz_, band='palestone', part=DETAIL)
        g.box(((x0 + x1) / 2, (y0 + y1) / 2, zt + 0.15), (W + 0.1, D + 0.1, 0.3), chamfer=0.05 if lod < 2 else 0)
        _a_terrace_top(g, x0 - 0.05, x1 + 0.05, y0 - 0.05, y1 + 0.05, zt + 0.3, age, lod,
                       deep=(max(D, W) > min(D, W) * 1.35 and age == 3), dome=main)
        if age == 3:
            zb = zl + 0.35
            for (c, sz) in ((((x0 + x1) / 2, y0 - 0.03, zb), (W + 0.06, 0.06, 0.16)), (((x0 + x1) / 2, y1 + 0.03, zb), (W + 0.06, 0.06, 0.16)),
                            ((x0 - 0.03, (y0 + y1) / 2, zb), (0.06, D + 0.06, 0.16)), ((x1 + 0.03, (y0 + y1) / 2, zb), (0.06, D + 0.06, 0.16))):
                g.box(c, sz, 'Detail', band='cobalt', part=DETAIL)


def a_ornament(age):
    def fn(lod):
        g = Geo()
        dw, dh, jam, dep = 1.1, 2.1, 0.2, 0.07
        if age == 2:
            for sx in (-1, 1):
                g.box((sx * (dw / 2 + jam / 2), -dep / 2, dh / 2), (jam, dep, dh), 'Detail', band='cobalt', part=DETAIL)
            g.box((0, -dep / 2, dh + 0.12), (dw + 2 * jam, dep, 0.24), 'Detail', band='cobalt', part=DETAIL)
            g.box((0, -0.025, dh + 0.45), (4.0, 0.05, 0.16), 'Detail', band='cobalt', part=DETAIL)
            g.box((0, -0.2, 0.06), (dw + 0.5, 0.4, 0.12), 'Detail', band='palestone', part=DETAIL)
        else:
            za = dh - 0.55
            ra = dw / 2 + jam / 2
            for sx in (-1, 1):
                g.box((sx * ra, -dep / 2, za / 2), (jam, dep, za), 'Detail', band='cobalt', part=DETAIL)
            n = sg(9, lod, 4)
            for k in range(n):
                a0 = math.pi * k / n
                a1 = math.pi * (k + 1) / n
                p0 = V(ra * math.cos(a0), -dep / 2, za + ra * math.sin(a0))
                p1 = V(ra * math.cos(a1), -dep / 2, za + ra * math.sin(a1))
                g.beam(p0, p1, dep, jam, mat='Detail', band='cobalt', part=DETAIL, up=(0, -1, 0))
            g.box((0, -dep - 0.03, za + ra + 0.05), (0.26, 0.1, 0.34), 'Detail', band='palestone', part=DETAIL)
            g.box((0, -0.025, dh + 0.6), (5.0, 0.05, 0.18), 'Detail', band='cobalt', part=DETAIL)
            for sx in (-1, 1):   # flanking screened windows
                x = sx * 1.65
                g.box((x, -0.03, 1.45), (0.62, 0.04, 0.95), 'Detail', band='window', part=DETAIL)
                for (c, s) in (((x - 0.36, -0.05, 1.45), (0.1, 0.06, 1.15)), ((x + 0.36, -0.05, 1.45), (0.1, 0.06, 1.15)),
                               ((x, -0.05, 2.0), (0.82, 0.06, 0.1)), ((x, -0.08, 0.92), (0.9, 0.16, 0.08))):
                    g.box(c, s, 'Detail', band='cobalt', part=DETAIL)
            for k in range(2):
                g.box((0, -0.2 - 0.25 * k, 0.06 + 0.12 * (1 - k)), (dw + 0.8 - 0.3 * k, 0.4, 0.12 + 0.12 * (1 - k)),
                      'Detail', band='palestone', part=DETAIL)
        return g
    return fn


def a_terrace(lod):
    g = Geo()
    W, Dp, H = 12.0, 8.0, 3.0
    x0, x1, y0, y1 = -W / 2, W / 2, -Dp / 2, Dp / 2
    bat = 0.3
    solid8(g, [(x0, y0, -0.3), (x1, y0, -0.3), (x1, y1, -0.3), (x0, y1, -0.3),
               (x0, y0 + bat, H), (x1, y0 + bat, H), (x1, y1, H), (x0, y1, H)])
    # coping and parapet on the front edge, gap at the stair head (x -5.6 .. -4.2)
    ch = 0.05 if lod < 2 else 0.0
    g.box((0, y0 + bat - 0.02, H + 0.08), (W + 0.1, 0.5, 0.16), chamfer=ch)
    g.box(((-4.2 + x1) / 2, y0 + bat + 0.12, H + 0.4), (x1 + 4.2, 0.3, 0.5), chamfer=0.06 if lod < 2 else 0)
    # beam ends under the coping
    if lod < 2:
        for k in range(sg(13, lod, 5)):
            x = x0 + 0.5 + (W - 1.0) * k / max(1, sg(13, lod, 5) - 1)
            g.box((x, y0 + bat - 0.12, H - 0.35), (0.16, 0.5, 0.16), 'Detail', band='oak', part=DETAIL)
    # stair along the front face rising toward -x
    # 15 risers of 0.2 m (verifier fix: 10 risers of 0.3 m were knee-high against the 1.8 m figure); same flight length
    ns, rise, run, sx0 = 15, H / 15, 0.44, 2.6
    sw = 1.35
    yo0, yo1 = y0 - sw, y0 + 0.2
    if lod < 2:
        for i in range(ns):
            xa = sx0 - (i + 1) * run
            xb = sx0 - i * run
            g.box(((xa + xb) / 2 - 0.02, (yo0 + yo1) / 2, (-0.3 + (i + 1) * rise) / 2), (xb - xa + 0.04, yo1 - yo0, 0.3 + (i + 1) * rise))
    else:
        solid8(g, [(sx0 - ns * run, yo0, -0.3), (sx0, yo0, -0.3), (sx0, yo1, -0.3), (sx0 - ns * run, yo1, -0.3),
                   (sx0 - ns * run, yo0, H), (sx0, yo0, 0.0), (sx0, yo1, 0.0), (sx0 - ns * run, yo1, H)])
    xl0, xl1 = sx0 - ns * run - 1.4, sx0 - ns * run
    g.box(((xl0 + xl1) / 2, (yo0 + yo1) / 2, (H - 0.3) / 2), (xl1 - xl0, yo1 - yo0, H + 0.3))
    # outer balustrade following the flight
    bt = 0.24
    solid8(g, [(xl0, yo0 - bt, -0.3), (sx0 + 0.1, yo0 - bt, -0.3), (sx0 + 0.1, yo0, -0.3), (xl0, yo0, -0.3),
               (xl0, yo0 - bt, H + 0.55), (sx0 + 0.1, yo0 - bt, 0.55), (sx0 + 0.1, yo0, 0.55), (xl0, yo0, H + 0.55)])
    # storeroom doors and a water chute on the face
    for x in (4.3,):
        g.box((x, y0 - 0.02, 0.95), (1.0, 0.06, 1.9), 'Detail', band='tar', part=BODY)
        for (c, s) in (((x - 0.6, y0 - 0.05, 1.0), (0.2, 0.08, 2.0)), ((x + 0.6, y0 - 0.05, 1.0), (0.2, 0.08, 2.0)),
                       ((x, y0 - 0.05, 2.06), (1.4, 0.08, 0.22))):
            g.box(c, s, 'Detail', band='cobalt', part=DETAIL)
    g.box((5.5, y0 + bat * 0.5 - 0.06, H / 2), (0.5, 0.14, H + 0.2), 'Detail', band='palestone', part=BODY,
          rot=Matrix.Rotation(-math.atan2(bat, H + 0.3), 4, 'X'))
    g.lathe([(0.0, -0.3), (0.85, -0.3), (0.85, 0.45), (0.65, 0.45), (0.65, 0.2), (0.0, 0.2)], sg(16, lod, 6),
            (5.5, y0 - 0.9, 0), 'Structure', BODY, cap_bot=False)
    return g


def a_cistern(lod):
    g = Geo()
    seg = sg(28, lod, 8)
    lathe_solid(g, [(1.25, -0.3), (1.6, -0.3), (1.6, 0.82), (1.25, 0.82)], seg, mat='Structure', uv_r=1.6)
    lathe_solid(g, [(1.18, 0.8), (1.68, 0.8), (1.68, 0.96), (1.18, 0.96)], seg, mat='Detail', band='palestone', part=DETAIL)
    # water disc: profile from the rim inward so the surface faces up (verifier fix: it faced down and was
    # culled from every camera above it)
    g.lathe([(1.27, 0.62), (0.0, 0.62)], seg, (0, 0, 0), 'Roof', BODY, mode=UV_BOX)
    # outflow chute toward the front
    g.box((0, -1.95, 0.55), (0.36, 0.9, 0.12), 'Detail', band='palestone', part=DETAIL)
    for sx in (-1, 1):
        g.box((sx * 0.2, -1.95, 0.64), (0.06, 0.9, 0.14), 'Detail', band='palestone', part=DETAIL)
    return g


def a_niche(lod):
    g = Geo()
    W, Dp, H = 18.0, 7.0, 22.0
    cuts = (12, 6, 2)[lod]
    M = Matrix.Translation((0, 0, H / 2 - 0.3)) @ Matrix.Diagonal((W, Dp, H + 0.6, 1))
    ret = bmesh.ops.create_cube(g.bm, size=1.0, matrix=M)
    edges = list({e for v in ret['verts'] for e in v.link_edges})
    bmesh.ops.subdivide_edges(g.bm, edges=edges, cuts=cuts, use_grid_fill=True)
    faces = list(g.bm.faces)
    g.mark(faces, 'Structure', UV_BOX, 'oak', BODY)
    off = V(13.1, 7.7, 2.2)
    for v in g.bm.verts:
        p = v.co
        n = V(0, 0, 0)
        if abs(p.x) > W / 2 - 1e-3:
            n.x = math.copysign(1, p.x)
        if abs(p.y) > Dp / 2 - 1e-3:
            n.y = math.copysign(1, p.y)
        if p.z > H - 0.31:
            n.z = 1
        if n.length == 0:
            continue
        amp = 0.9
        if n.y < 0 and abs(p.x) < 6.5 and p.z < 17.5 and n.z == 0:
            amp = 0.12                       # the carved front stays flat around the niche
        if n.z > 0:
            amp = 1.6
        d = noise.noise(p * 0.18 + off) + 0.5 * noise.noise(p * 0.45 + off)
        v.co = p + n.normalized() * amp * d - (V(0, 0, 0.6) * max(0, d) if n.z > 0 else V(0, 0, 0))
    # the arched niche
    hw, zs, za = 3.6, 0.9, 11.2
    pts = [(-hw, zs), (hw, zs)]
    na = sg(14, lod, 6)
    for k in range(na + 1):
        a = math.pi * k / na
        pts.append((hw * math.cos(a), za + hw * math.sin(a)))
    pts = [pts[0], pts[1]] + pts[2:]
    poly = [(-hw, zs)] + [(hw, zs)] + [(hw * math.cos(math.pi * k / na), za + hw * math.sin(math.pi * k / na)) for k in range(na + 1)]
    cut = Geo()
    prism_xz(cut, poly, -Dp / 2 - 2.0, -Dp / 2 + 2.6)
    g = geo_bool(g, cut, 'DIFFERENCE')
    # stepped archivolts carved round the opening
    for k, d in enumerate((0.45, 1.05)):
        r = hw + d
        path = [V(-r, -Dp / 2 - 0.1 - 0.12 * k, zs - 0.3)]
        for j in range(na + 1):
            a = math.pi * (1 - j / na)
            path.append(V(r * math.cos(a), -Dp / 2 - 0.1 - 0.12 * k, za + r * math.sin(a)))
        path.append(V(r, -Dp / 2 - 0.1 - 0.12 * k, zs - 0.3))
        g.tube(path, 0.32 - 0.06 * k, seg=4, mat='Structure', band='oak', part=BODY)
    # doorway at the back of the niche with a cobalt surround
    yb = -Dp / 2 + 2.6
    g.box((0, yb - 0.05, zs + 2.3), (2.4, 0.1, 4.6), 'Detail', band='tar', part=BODY)
    for (c, s) in (((-1.4, yb - 0.12, zs + 2.4), (0.4, 0.16, 4.8)), ((1.4, yb - 0.12, zs + 2.4), (0.4, 0.16, 4.8)),
                   ((0, yb - 0.12, zs + 4.9), (3.2, 0.16, 0.45))):
        g.box(c, s, 'Detail', band='cobalt', part=DETAIL)
    # steps and two pilasters
    for k in range(4):
        g.box((0, -Dp / 2 - 1.8 + 0.45 * k, -0.15 + 0.15 * k + 0.15), (10.0 - 0.6 * k, 3.6 - 0.9 * k, 0.3 + 0.3 * k), chamfer=0.04 if lod < 2 else 0)
    for sx in (-1, 1):
        x = sx * 5.6
        g.box((x, -Dp / 2 - 0.35, 4.6), (1.0, 0.8, 9.6), chamfer=0.06 if lod < 2 else 0)
        g.box((x, -Dp / 2 - 0.4, 9.6), (1.4, 1.0, 0.5), chamfer=0.05 if lod < 2 else 0)
        g.box((x, -Dp / 2 - 0.4, 0.15), (1.3, 1.0, 0.6), chamfer=0.05 if lod < 2 else 0)
    return g


# =====================================================================================================
# Culture B - Lagoon-Weaver
# =====================================================================================================

def _saddle(g, L, Dw, age, lod, zb, host_rise=None, gable_hw=None, res=1.0, host_prof=None):
    """Saddle roof in a local frame: ridge along X over a host roof L long and Dw wide whose eave is at zb.
    host_rise: the host ridge height above zb (the shell is raised until it clears a straight gable)."""
    if age == 2:
        A, e, ov, asym, th, gam, z0 = 0.34 * Dw, 0.9, 0.85, 0.12, 0.16, 1.0, -0.55
        Hr = 0.5 * Dw
    else:
        A, e, ov, asym, th, gam, z0 = 0.55 * Dw, 1.6, 0.95, 0.3, 0.2, 1.1, -0.6
        Hr = 0.78 * Dw
    hx = L / 2 + e
    hw0 = Dw / 2 + ov

    def hw(t):
        px = max(0.0, (abs(t) * hx - L / 2) / e)
        return hw0 * (1 - 0.62 * px ** 1.6)

    def ze(t):
        return z0 + 0.5 * A * abs(t) ** 3

    if host_rise is not None:
        for it in range(80):
            ok = True
            for k in range(21):
                y = (Dw / 2) * k / 20
                s_ = y / hw0
                zme = ze(0) + (Hr - ze(0)) * (1 - s_) ** gam - th * 1.3
                zh = host_rise * (1 - y / (Dw / 2))
                if host_prof:   # verifier: measured envelope (z above zb) where the host is not a plain gable
                    f = min(20.0, y / (Dw / 2) * 20.0)
                    kk = min(19, int(f))
                    zh = max(zh, host_prof[kk] + (host_prof[kk + 1] - host_prof[kk]) * (f - kk) - zb)
                if zme < zh + 0.12:
                    ok = False
                    break
            if ok:
                break
            Hr += 0.1

    def zr(t):
        return Hr + A * abs(t) ** 2.6 * (1 + asym * (1 if t < 0 else -1))

    def surf(u, v):
        t, s_ = 2 * u - 1, 2 * v - 1
        return V(t * hx, s_ * hw(t), zb + ze(t) + (zr(t) - ze(t)) * (1 - abs(s_)) ** gam)

    def uvf(u, v):
        t, s_ = 2 * u - 1, 2 * v - 1
        sl = math.hypot(hw(t), zr(t) - ze(t))
        return (t * hx, (1 - abs(s_)) * sl)

    nu = max(8, int(sg(22, lod, 8) * res))
    nv = 2 * max(2, int(sg(4, lod, 2) * res + 0.5))
    g.grid_shell(surf, nu, nv, th, 'Roof', SHELL, uv_fn=uvf)
    ridge = [surf(i / nu, 0.5) + V(0, 0, 0.05) for i in range(nu + 1)]
    g.tube(ridge, 0.1, seg=sg(6, lod, 4), mat='Detail', band='tar', part=SHELL)
    ghw = gable_hw if gable_hw is not None else Dw / 2 - 0.45
    for sx in (-1, 1):
        xg = sx * (L / 2 - 0.3)
        tg = xg / hx
        pts = [(-ghw, zb - 0.35), (ghw, zb - 0.35)]
        k = sg(8, lod, 3)
        for i in range(k + 1):
            y = ghw - 2 * ghw * i / k
            s_ = max(-1.0, min(1.0, y / hw(tg)))
            z = zb + ze(tg) + (zr(tg) - ze(tg)) * (1 - abs(s_)) ** gam - th * 1.5
            pts.append((y, max(zb - 0.3, z)))
        prism_yz(g, pts, xg - 0.06, xg + 0.06, 'Detail', BODY, band='reed')
    for sx in (-1, 1):
        tip = surf(0.0 if sx < 0 else 1.0, 0.5)
        back = surf(0.04 if sx < 0 else 0.96, 0.5)
        d = (tip - back).normalized()
        hl = 0.6 if age == 2 else 1.3
        pts = [tip - d * 0.3, tip + d * hl * 0.5, tip + d * hl + V(0, 0, 0.15 if age == 3 else 0.0)]
        g.tube(pts, 0.14, seg=sg(6, lod, 4), mat='Detail', band='oak', part=DETAIL, r_fn=lambda f: 0.15 * (1 - 0.8 * f))
        nrib = 1 if age == 2 else 2
        for r_ in range(nrib):
            yoff = 0.0 if nrib == 1 else (-0.25 + 0.5 * r_)
            ribbon(g, tip + V(0, yoff, -0.15), 1.6 if age == 2 else 2.6, (sx * 0.5, yoff * 0.6), seed=r_ + (sx > 0) * 7,
                   segs=sg(6, lod, 2))
    nr = max(3, int(L / 1.3))
    for i in range(nr + 1):
        u = 0.5 + (i / nr - 0.5) * (L - 0.6) / (2 * hx)
        for side in (0.0, 1.0):
            pts = [surf(u, side + (0.5 - side) * f / 4) - V(0, 0, th + 0.09) for f in range(5)]
            for k in range(4):
                g.beam(pts[k], pts[k + 1], 0.1, 0.13, mat='Detail', band='oak', part=FRAME)
    rp = [surf(i / nu, 0.5) - V(0, 0, th + 0.12) for i in range(nu + 1)]
    for k in range(nu):
        g.beam(rp[k], rp[k + 1], 0.16, 0.16, mat='Detail', band='oak', part=FRAME)
    return Hr


def _local_to_host(g, r, verts=None):
    """Rotate a ridge-along-X local build onto the roof rect's ridge axis and move it to the rect centre."""
    cx = (r['min'][0] + r['max'][0]) / 2
    cy = (r['min'][1] + r['max'][1]) / 2
    M = Matrix.Translation((cx, cy, 0))
    if r['ridge_axis'] == 'Y':
        M = M @ Matrix.Rotation(math.pi / 2, 4, 'Z')
    bmesh.ops.transform(g.bm, matrix=M, verts=list(verts if verts is not None else g.bm.verts))


def _host_dims(r):
    ex, ey = r['max'][0] - r['min'][0], r['max'][1] - r['min'][1]
    return (ex, ey) if r['ridge_axis'] == 'X' else (ey, ex)


def b_roof_host(h, age):
    """Lagoon-Weaver saddle roof enveloping the host roof (host space)."""
    def fn(lod):
        g = Geo()
        rs = _rects(h)
        for r in rs:
            before = set(g.bm.verts)
            L, Dw = _host_dims(r)
            _saddle(g, L, Dw, age, lod, r['eave_z'], host_rise=r['ridge_z'] - r['eave_z'], res=0.7 if len(rs) > 1 else 1.0,
                    host_prof=r.get('profile'))
            _local_to_host(g, r, [v for v in g.bm.verts if v not in before])
        return g
    return fn


def _stilts(g, x0, x1, y0, y1, age, lod, zt, seed=1):
    """Pile platform with its deck top at zt (water surface zt - 1.8, piles to zt - 4.8)."""
    rng = random.Random(seed)
    dt_ = 0.16
    zw = zt - 1.8
    g.box(((x0 + x1) / 2, (y0 + y1) / 2, zt - dt_ / 2), (x1 - x0, y1 - y0, dt_), 'Structure', chamfer=0.02 if lod < 2 else 0)
    for (a, b) in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        g.beam(V(a[0], a[1], zt - dt_ - 0.14), V(b[0], b[1], zt - dt_ - 0.14), 0.16, 0.3, mat='Detail', band='tar', part=BODY)
    nx = max(2, int(math.ceil((x1 - x0) / 2.6)) + 1)
    ny = max(2, int(math.ceil((y1 - y0) / 2.6)) + 1)
    seg = sg(6, lod, 4)
    P = {}
    for i in range(nx):
        for j in range(ny):
            x = x0 + 0.15 + (x1 - x0 - 0.3) * i / (nx - 1) + rng.uniform(-0.12, 0.12)
            y = y0 + 0.15 + (y1 - y0 - 0.3) * j / (ny - 1) + rng.uniform(-0.12, 0.12)
            top = zt - dt_ - 0.2
            tall = (i == 0 and j == 0)
            if tall:
                top = zt + 1.2
            r = 0.13 if age == 2 else 0.15
            fs = g.cyl((0, 0, zw - 3.0), r, top - zw + 3.0, seg, 'Detail', FIX, r2=r * 0.9, band='tar')
            M = Matrix.Translation((x, y, 0)) @ Matrix.Rotation(rng.uniform(-0.05, 0.05), 4, 'X') @ \
                Matrix.Rotation(rng.uniform(-0.05, 0.05), 4, 'Y')
            g.transform(fs, M)
            P[(i, j)] = V(x, y, 0)
            if tall and lod < 2:
                g.tube(ring_pts(V(x, y, zt + 0.9), r + 0.06, 8), 0.035, seg=4, mat='Detail', band='rope', part=DETAIL, closed=True)
    for j in range(ny):
        y = y0 + 0.15 + (y1 - y0 - 0.3) * j / (ny - 1)
        g.beam(V(x0, y, zt - dt_ - 0.12), V(x1, y, zt - dt_ - 0.12), 0.18, 0.22, mat='Detail', band='tar', part=BODY)
    if age == 3:
        for j in (0, ny - 1):
            for i in range(nx - 1):
                a, b = P[(i, j)], P[(i + 1, j)]
                g.beam(a + V(0, 0, zw + 1.2), b + V(0, 0, zt - 0.6), 0.08, 0.12, mat='Detail', band='tar', part=BODY)
                g.beam(b + V(0, 0, zw + 1.2), a + V(0, 0, zt - 0.6), 0.08, 0.12, mat='Detail', band='tar', part=BODY)
        np_ = max(2, int((x1 - x0) / 1.4))
        for k in range(np_ + 1):
            x = x0 + 0.1 + (x1 - x0 - 0.2) * k / np_
            g.box((x, y0 + 0.08, zt + 0.5), (0.1, 0.1, 1.0), 'Detail', band='oak', part=DETAIL)
        g.beam(V(x0 + 0.1, y0 + 0.08, zt + 1.0), V(x1 - 0.1, y0 + 0.08, zt + 1.0), 0.09, 0.09, mat='Detail', band='oak', part=DETAIL)
    lx = x0 + (x1 - x0) * 0.62
    for sx in (-1, 1):
        g.beam(V(lx + sx * 0.3, y0 - 0.45, zw - 0.9), V(lx + sx * 0.3, y0 - 0.05, zt + 0.1), 0.08, 0.08, mat='Detail', band='oak', part=DETAIL)
    if lod < 2:
        for k in range(7):
            f = k / 7
            z = zw - 0.6 + (zt - zw + 0.6) * f
            g.beam(V(lx - 0.3, y0 - 0.45 + 0.4 * f, z), V(lx + 0.3, y0 - 0.45 + 0.4 * f, z), 0.05, 0.05, mat='Detail', band='oak', part=DETAIL)


def b_stilts_host(h, age):
    """Pile platform under a host house (host space: deck top at z = 0, water at -1.8). Place host and
    platform with the same transform, 1.8 m above the water."""
    def fn(lod):
        g = Geo()
        rs = _rects(h)
        ymin = min(r['min'][1] for r in rs)
        for r in rs:
            x0, x1 = r['min'][0] - 0.1, r['max'][0] + 0.1
            y0 = r['min'][1] - (1.7 if r['min'][1] <= ymin + 0.01 else 0.1)
            y1 = r['max'][1] + 0.1
            _stilts(g, x0, x1, y0, y1, age, lod, 0.0, seed=int(abs(x1 - x0) * 100 + abs(y1 - y0) * 10))
        return g
    return fn


def _b_piles_along(g, pts_pairs, zt, lod, seg=5):
    for a in pts_pairs:
        fs = g.cyl((a.x, a.y, -2.6), 0.11, zt - 0.2 + 2.6, sg(seg, lod, 4), 'Detail', BODY, band='tar')


def b_walkway(kind):
    def fn(lod):
        g = Geo()
        # deck top 1.8 m over the water, level with the B*Stilts* house decks (verifier fix: was 1.3 m, a 0.5 m
        # step down from every house platform it is meant to join)
        w, zt, th = 1.6, 1.8, 0.14
        if kind == 'Straight':
            L = 8.0
            path = [V(-L / 2 + L * k / 8, 0, 0) for k in range(9)]
        elif kind == 'Curve':
            R, ang = 10.0, math.radians(45)
            n = sg(10, lod, 4)
            path = [V(R * math.sin(ang * k / n), R - R * math.cos(ang * k / n), 0) for k in range(n + 1)]
        else:   # Bridge: arched footbridge
            L, rise = 10.0, 2.0
            n = sg(14, lod, 6)
            path = [V(-L / 2 + L * k / n, 0, rise * (1 - (2 * (-L / 2 + L * k / n) / L) ** 2)) for k in range(n + 1)]
        # centre the footprint
        mnx, mxx = min(p.x for p in path), max(p.x for p in path)
        mny, mxy = min(p.y for p in path), max(p.y for p in path)
        c = V((mnx + mxx) / 2, (mny + mxy) / 2, 0)
        path = [p - c for p in path]
        # deck: planks run across the path (texture u across, v along)
        prof = [(-w / 2, zt - th), (w / 2, zt - th), (w / 2, zt), (-w / 2, zt)]
        fs = sweep(g, path, prof, 'Structure', BODY)
        for f in fs:   # swap the plank direction on the top / bottom faces
            if abs(f.normal.z) > 0.7:
                for lp in f.loops:
                    u, v = lp[g.uv].uv
                    lp[g.uv].uv = (v, u)
        # side stringers
        for s in (-1, 1):
            sp = [p + V(0, 0, 0) for p in path]
            sweep(g, sp, [(s * w / 2 - 0.08, zt - th - 0.22), (s * w / 2 + 0.04, zt - th - 0.22), (s * w / 2 + 0.04, zt - th + 0.02), (s * w / 2 - 0.08, zt - th + 0.02)],
                  'Detail', BODY, band='tar')
        # piles in pairs every ~2 m, cross beams
        sl = 0.0
        acc = 2.0
        for k in range(len(path)):
            if k > 0:
                sl += (path[k] - path[k - 1]).length
            if k == 0 or k == len(path) - 1 or sl >= acc:
                acc = sl + 2.0
                a = path[max(k - 1, 0)]
                b = path[min(k + 1, len(path) - 1)]
                d = (b - a)
                d.z = 0
                d.normalize()
                left = V(-d.y, d.x, 0)
                p = path[k]
                ztop = zt + p.z - th
                for s in (-1, 1):
                    q = p + left * s * (w / 2 - 0.1)
                    g.cyl((q.x, q.y, -2.6), 0.11, ztop - 0.15 + 2.6, sg(6, lod, 4), 'Detail', FIX, band='tar')
                g.beam(p - left * (w / 2 + 0.1) + V(0, 0, ztop - 0.12), p + left * (w / 2 + 0.1) + V(0, 0, ztop - 0.12),
                       0.14, 0.16, mat='Detail', band='tar', part=BODY)
                if kind == 'Bridge' or k % 2 == 0:
                    rail_s = (-1, 1) if kind == 'Bridge' else (1,)
                    for s in rail_s:
                        q = p + left * s * (w / 2 - 0.05)
                        g.box((q.x, q.y, ztop + th + 0.5), (0.09, 0.09, 1.0), 'Detail', band='oak', part=DETAIL)
        # hand rails / rope
        rails = (-1, 1) if kind == 'Bridge' else (1,)
        for s in rails:
            rp = []
            for k in range(len(path)):
                a = path[max(k - 1, 0)]
                b = path[min(k + 1, len(path) - 1)]
                d = (b - a)
                d.z = 0
                d.normalize()
                left = V(-d.y, d.x, 0)
                rp.append(path[k] + left * s * (w / 2 - 0.05) + V(0, 0, zt + 0.95))
            g.tube(rp, 0.045 if kind == 'Bridge' else 0.03, seg=4, mat='Detail', band='oak' if kind == 'Bridge' else 'rope', part=DETAIL)
        return g
    return fn


def b_raft(lod):
    g = Geo()
    L, Wd = 6.0, 4.0
    nl = sg(11, lod, 5)
    for k in range(nl):
        y = -Wd / 2 + 0.2 + (Wd - 0.4) * k / (nl - 1)
        fs = g.cyl((0, 0, -L / 2), 0.19, L + (0.3 if k % 2 else 0.0), sg(7, lod, 4), 'Structure', BODY)
        g.transform(fs, Matrix.Translation((0, y, 0.06)) @ Matrix.Rotation(math.pi / 2, 4, 'Y'))
    for x in (-2.5, 0.0, 2.5):
        g.beam(V(x, -Wd / 2 - 0.1, 0.27), V(x, Wd / 2 + 0.1, 0.27), 0.1, 0.1, mat='Detail', band='rope', part=BODY)
    for sy in (-1, 1):
        yc = sy * 0.95
        g.box((0, yc, 0.42), (5.4, 1.5, 0.26), 'Detail', band='reed', part=BODY)
        g.box((0, yc, 0.56), (5.2, 1.3, 0.04), 'Roof', BODY)
        if lod < 2:
            for r in (-0.38, 0.0, 0.38):
                pts = [V(-2.4 + 4.8 * i / 9, yc + r, 0.6) for i in range(10)]
                g.tube(pts, 0.2, seg=4, mat='Roof', band='oak', part=BODY,
                       r_fn=lambda f: 0.17 + 0.05 * math.sin(f * 41.0))
    g.box((2.9, 0, 0.5), (0.12, 0.12, 1.2), 'Detail', band='oak', part=BODY)
    return g


def b_breakwater(lod):
    g = Geo()
    R, L = 30.0, 12.0
    ang = L / R
    n = sg(16, lod, 6)
    path = [V(R * math.sin(-ang / 2 + ang * k / n), R * math.cos(-ang / 2 + ang * k / n) - R, 0) for k in range(n + 1)]
    cy = (min(p.y for p in path) + max(p.y for p in path)) / 2
    path = [p - V(0, cy, 0) for p in path]

    def uvf_band(u, v):
        return (u * L, ((6 + 0.06 + 0.88 * v) / 8) * 2.0)

    # woven wattle body between the two stake rows (2 woven faces + top)
    prof = [(-0.33, -1.4), (0.33, -1.4), (0.3, 1.25), (-0.3, 1.25)]
    sweep(g, path, prof, 'Detail', BODY, band='reed')
    # stakes
    step = (0.45, 0.8, 1.6)[lod]
    sl = 0.0
    k = 0
    tot = sum((path[i] - path[i - 1]).length for i in range(1, len(path)))
    nst = int(tot / step)
    rng = random.Random(5)
    for i in range(nst + 1):
        f = i / nst
        a = -ang / 2 + ang * f
        p = V(R * math.sin(a), R * math.cos(a) - R - cy, 0)
        d = V(math.cos(a), -math.sin(a), 0)
        left = V(-d.y, d.x, 0)
        for s in (-1, 1):
            q = p + left * s * 0.36
            h = 1.45 + rng.uniform(0.0, 0.35)
            g.cyl((q.x, q.y, -1.6), 0.07, h + 1.6, sg(5, lod, 3), 'Structure', BODY)
    # stone toe on the sea side
    if lod < 2:
        for i in range(sg(9, lod, 4)):
            f = (i + 0.5) / sg(9, lod, 4)
            a = -ang / 2 + ang * f
            p = V(R * math.sin(a), R * math.cos(a) - R - cy, 0)
            nrm = V(math.sin(a), math.cos(a), 0)
            g.blob(p + nrm * 0.9 + V(0, 0, -0.3), 0.7, 'Detail', BODY, subdiv=1, squash=0.6, rough=0.3, seed=i, band='palestone')
    return g


def b_temple(lod):
    g = Geo()
    rng = random.Random(11)
    zd = 2.0
    seg = sg(48, lod, 14)
    lathe_solid(g, [(0.0, zd - 0.32), (10.0, zd - 0.32), (10.0, zd), (0.0, zd)], seg, mat='Structure', uv_r=10.0)
    lathe_solid(g, [(9.85, zd - 0.6), (10.15, zd - 0.6), (10.15, zd - 0.25), (9.85, zd - 0.25)], seg, mat='Detail', band='tar')
    for (rr, n) in ((9.5, sg(24, lod, 8)), (5.6, sg(12, lod, 6))):
        for k in range(n):
            a = 2 * math.pi * (k + 0.5 * (rr < 6)) / n
            g.cyl((rr * math.cos(a), rr * math.sin(a), -3.0), 0.17, zd - 0.3 + 3.0, sg(7, lod, 4), 'Detail', FIX, band='tar')
    # railing
    npst = sg(40, lod, 12)
    for k in range(npst):
        a = 2 * math.pi * k / npst
        if abs(a - 1.5 * math.pi) < 0.12:
            continue
        g.box((9.75 * math.cos(a), 9.75 * math.sin(a), zd + 0.5), (0.1, 0.1, 1.0), 'Detail', band='oak', part=DETAIL, rz=a)
    g.tube(ring_pts(V(0, 0, zd + 1.0), 9.75, sg(48, lod, 12)), 0.05, seg=4, mat='Detail', band='oak', part=DETAIL, closed=True)
    # drum: posts and woven walls
    rw, zw = 6.0, zd + 4.6
    lathe_solid(g, [(rw - 0.2, zd), (rw + 0.05, zd), (rw + 0.05, zw), (rw - 0.2, zw)], sg(32, lod, 10), mat='Detail', band='reed')
    npo = sg(16, lod, 8)
    for k in range(npo):
        a = 2 * math.pi * k / npo
        g.box(((rw + 0.12) * math.cos(a), (rw + 0.12) * math.sin(a), (zd + zw) / 2), (0.32, 0.32, zw - zd), 'Detail',
              band='oak', part=BODY, rz=a)
    lathe_solid(g, [(rw - 0.25, zw - 0.05), (rw + 0.35, zw - 0.05), (rw + 0.35, zw + 0.3), (rw - 0.25, zw + 0.3)],
                sg(32, lod, 10), mat='Detail', band='tar')
    # doorway (front, -Y) with saffron hangings
    g.box((0, -rw - 0.12, zd + 1.4), (1.8, 0.1, 2.8), 'Detail', band='tar', part=BODY)
    for sx in (-1, 1):
        g.box((sx * 1.05, -rw - 0.2, zd + 1.5), (0.3, 0.14, 3.0), 'Detail', band='oak', part=BODY)
        ribbon(g, V(sx * 0.75, -rw - 0.3, zd + 2.9), 2.4, (sx * 0.1, -0.2), seed=sx + 3, segs=sg(5, lod, 2))
    # spiral roof: a helical shingle skirt winding up a core cone
    turns = 4.25
    th_max = 2 * math.pi * turns
    z0, z1, r0, r1 = zw - 0.2, zw + 12.2, 8.8, 1.7

    def outer(f):
        return r0 + (r1 - r0) * f, z0 + (z1 - z0) * f

    def surf(u, v):
        f = u
        th = th_max * f - math.pi / 2
        r, z = outer(f)
        wdt = 2.5 * (1 - 0.45 * f)
        rr = r - wdt * v
        zz = z + 1.75 * (1 - 0.3 * f) * (v ** 0.85)
        return V(rr * math.cos(th), rr * math.sin(th), zz)

    def uvf(u, v):
        f = u
        r, z = outer(f)
        return (th_max * f * (r0 * 0.6), 2.5 * (1 - v))

    g.grid_shell(surf, sg(190, lod, 48), 2, 0.2, 'Roof', SHELL, uv_fn=uvf)
    g.lathe([(7.4, zw - 0.3), (5.6, zw + 3.2), (3.4, zw + 7.4), (1.5, zw + 11.6), (0.0, zw + 13.2)], sg(32, lod, 10),
            (0, 0, 0), 'Roof', SHELL, cap_bot=True)
    zt = zw + 13.0
    g.lathe([(0.55, zt), (0.42, zt + 1.6), (0.12, zt + 3.6), (0.0, zt + 4.2)], sg(10, lod, 5), (0, 0, 0), 'Detail', DETAIL, band='oak')
    finial_ring(g, (0, 0, zt + 2.2), 0.55, 0.06, lod=lod)
    for k in range(2):
        ribbon(g, V(0.1, 0, zt + 3.4 - 0.4 * k), 3.2, (1.6, 0.4 * (k - 0.5)), w=0.3, segs=sg(6, lod, 2), seed=40 + k)
    # steps down to the water
    for k in range(6):
        g.box((0, -10.2 - 0.45 * k, zd - 0.3 * (k + 1) + 0.15), (3.2, 0.5, 0.3), 'Structure', BODY)
    # rafters under the core cone (construction)
    for k in range(12):
        a = 2 * math.pi * k / 12
        g.beam(V(7.0 * math.cos(a), 7.0 * math.sin(a), zw), V(0.4 * math.cos(a), 0.4 * math.sin(a), zw + 12.8), 0.18, 0.22,
               mat='Detail', band='oak', part=FRAME)
    return g


def b_ornament(age):
    """Woven-reed facade panel set in an oak frame for the host's front wall (3 m wide)."""
    def fn(lod):
        g = Geo()
        Wp, Hp = 3.0, 2.3
        g.box((0, -0.03, Hp / 2 + 0.25), (Wp, 0.06, Hp), 'Detail', band='reed', part=DETAIL)
        for x in (-Wp / 2, -Wp / 6, Wp / 6, Wp / 2):
            g.box((x, -0.08, Hp / 2 + 0.25), (0.14, 0.1, Hp + 0.1), 'Detail', band='oak', part=DETAIL)
        for z in (0.25, Hp + 0.25):
            g.box((0, -0.08, z), (Wp + 0.14, 0.1, 0.14), 'Detail', band='oak', part=DETAIL)
        if age == 3:
            # diagonal braces in each panel and a saffron garland
            for k in range(3):
                xa = -Wp / 2 + Wp / 3 * k + 0.1
                xb = xa + Wp / 3 - 0.2
                g.beam(V(xa, -0.1, 0.35), V(xb, -0.1, Hp + 0.15), 0.07, 0.04, mat='Detail', band='oak', part=DETAIL, up=(0, -1, 0))
                g.beam(V(xb, -0.1, 0.35), V(xa, -0.1, Hp + 0.15), 0.07, 0.04, mat='Detail', band='oak', part=DETAIL, up=(0, -1, 0))
            pts = [V(-Wp / 2 + Wp * i / 10, -0.18, Hp + 0.2 - 0.45 * math.sin(math.pi * i / 10)) for i in range(11)]
            g.tube(pts, 0.06, seg=4, mat='Detail', band='saffron', part=DETAIL)
            for sx in (-1, 1):
                ribbon(g, V(sx * Wp / 2, -0.18, Hp + 0.2), 1.4, (0, -0.1), w=0.18, segs=sg(4, lod, 2), seed=sx + 9)
        return g
    return fn


# =====================================================================================================
# Culture C - Ring-Builder
# =====================================================================================================

def c_drum(age):
    def fn(lod):
        g = Geo()
        R, H = (3.3, 2.75) if age == 2 else (3.9, 5.6)
        seg = sg(40, lod, 12)
        lathe_solid(g, [(0.0, -0.3), (R + 0.3, -0.3), (R + 0.3, 0.12), (R + 0.12, 0.24), (0.0, 0.24)], seg,
                    mat='Structure', part=FIX, uv_r=R)
        lathe_solid(g, [(R - 0.55, 0.22), (R, 0.22), (R, H), (R - 0.55, H)], seg, mat='Structure', part=BODY, uv_r=R)
        if age == 3:
            lathe_solid(g, [(R - 0.1, 2.75), (R + 0.09, 2.75), (R + 0.09, 3.0), (R - 0.1, 3.0)], seg, mat='Detail',
                        band='palestone', part=DETAIL)
            lathe_solid(g, [(R - 0.1, H - 0.3), (R + 0.12, H - 0.3), (R + 0.12, H), (R - 0.1, H)], seg, mat='Detail',
                        band='palestone', part=BODY)
        wins = [(70, 1.05, 1.7), (-70, 1.05, 1.7), (180, 1.05, 1.7)] if age == 2 else \
               [(55, 1.0, 1.75), (-55, 1.0, 1.75), (125, 1.0, 1.75), (-125, 1.0, 1.75),
                (0, 3.6, 4.5), (65, 3.6, 4.5), (-65, 3.6, 4.5), (130, 3.6, 4.5), (-130, 3.6, 4.5), (180, 3.6, 4.5)]
        ww = 0.55
        if lod < 2:
            cut = Geo()
            cut.box((0, -R, 1.15), (1.1, 1.2, 1.86))
            for (a, z0, z1) in wins:
                ar = math.radians(a)
                p = V(R * math.sin(ar), -R * math.cos(ar), (z0 + z1) / 2)
                cut.box(p, (ww, 0.5, z1 - z0), rz=ar)
            g = geo_bool(g, cut, 'DIFFERENCE')
        dy = -R + (0.32 if lod < 2 else -0.02)
        g.box((0, dy + 0.02, 1.12), (1.1, 0.08, 1.82), 'Detail', band='oak', part=BODY)
        g.box((0, -R - 0.06, 2.24), (1.6, 0.32, 0.36), 'Detail', band='palestone', part=BODY)
        for (a, z0, z1) in wins:
            ar = math.radians(a)
            rr = R - (0.22 if lod < 2 else -0.02)
            p = V(rr * math.sin(ar), -rr * math.cos(ar), (z0 + z1) / 2)
            g.box(p, (ww, 0.05, z1 - z0), 'Detail', rz=ar, band='window', part=BODY)
            ps = V((R + 0.02) * math.sin(ar), -(R + 0.02) * math.cos(ar), z0 - 0.06)
            g.box(ps, (ww + 0.2, 0.18, 0.1), 'Detail', rz=ar, band='palestone', part=BODY)
        # door threshold step
        g.box((0, -R - 0.35, 0.12), (1.5, 0.5, 0.24), 'Structure', FIX)
        return g
    return fn


def c_cone(age):
    def fn(lod):
        g = Geo()
        Rd = 3.3 if age == 2 else 3.9
        R = Rd + (0.6 if age == 2 else 0.7)
        pitch = math.radians(55 if age == 2 else 60)
        H = R * math.tan(pitch) * 0.88
        n = sg(9, lod, 3)
        outer = []
        for k in range(n + 1):
            f = k / n
            outer.append((R * (1 - f), -0.3 + H * f ** 1.12))
        inner = []
        for k in range(n, -1, -1):
            f = k / n
            inner.append((max(0.0, (R - 0.12) * (1 - f) - 0.08), -0.48 + (H - 0.3) * f ** 1.12))
        inner[0] = (0.0, inner[0][1])
        loop = outer + inner
        lathe_solid(g, loop, sg(36, lod, 10), mat='Roof', part=SHELL, uv_r=R)
        top = -0.3 + H
        g.lathe([(0.17, top - 0.4), (0.15, top + 0.3), (0.0, top + 0.36)], sg(8, lod, 4), (0, 0, 0), 'Detail', DETAIL,
                band='palestone')
        if age == 2:
            finial_ring(g, (0, 0, top + 0.5), 0.19, 0.05, lod=lod)
        else:
            for k, (r, z) in enumerate(((0.32, top + 0.32), (0.25, top + 0.58), (0.18, top + 0.8))):
                g.lathe([(r * 0.8, z), (r, z + 0.05), (r, z + 0.15), (r * 0.8, z + 0.2)], sg(10, lod, 5), (0, 0, 0),
                        'Detail', DETAIL, band='palestone', cap_top=True, cap_bot=True)
            finial_ring(g, (0, 0, top + 1.2), 0.24, 0.06, lod=lod)
            # an eave ring of pale stone (the Age3 cone sits on a cornice)
            lathe_solid(g, [(R - 0.25, -0.62), (R - 0.05, -0.62), (R - 0.05, -0.36), (R - 0.25, -0.36)], sg(36, lod, 10),
                        mat='Detail', band='palestone', part=DETAIL)
        # rafters and purlin rings (shown in Construction / Damaged / Ruin)
        for k in range(12):
            a = 2 * math.pi * k / 12
            g.beam(V((R - 0.3) * math.cos(a), (R - 0.3) * math.sin(a), -0.55),
                   V(0.2 * math.cos(a), 0.2 * math.sin(a), top - 0.6), 0.12, 0.14, mat='Detail', band='oak', part=FRAME)
        for f in (0.35, 0.65):
            g.tube(ring_pts(V(0, 0, -0.55 + (top - 0.05) * f), (R - 0.4) * (1 - f) + 0.15, 16), 0.06, seg=4, mat='Detail',
                   band='oak', part=FRAME, closed=True)
        return g
    return fn


def c_roof_host(h, age):
    """Ring-Builder roof for a rectangular host: a steep bell-cast slate gable with pale stone gable walls,
    stone copings and ring finials; it envelopes the host roof (host space)."""
    def fn(lod):
        g = Geo()
        for k, r in enumerate(_rects(h)):
            before = set(g.bm.verts)
            _c_roof_rect(g, r, age, lod if k == 0 else min(2, lod + 1))
            _local_to_host(g, r, [v for v in g.bm.verts if v not in before])
        return g
    return fn


def _c_roof_rect(g, r, age, lod):
    if True:
        L, Dw = _host_dims(r)
        ez, rz = r['eave_z'], r['ridge_z']
        tp = math.tan(math.radians(58 if age == 2 else 62))
        hy = Dw / 2 + 0.25
        hx = L / 2 + 0.35
        ze_ = ez - 0.3
        th = 0.2
        kick = 0.6
        tk = math.tan(math.radians(25))
        z1 = ze_ + kick * tk
        zr = max(z1 + tp * (hy - kick), rz + 0.6)
        # Verifier fix (2026-11-19): the shallow bell-cast kick sat BELOW the host roof near its eaves, so on
        # every Age2 host about a third of the host thatch showed through the slate (a band from the eave to
        # half-way up each slope). Widen the roof (wider overhang, steeper slope landing outside the host
        # eave) until the underside clears a straight host gable by 0.12 m everywhere; lift it only if that
        # is not enough.
        hh = Dw / 2
        hy0 = hy          # gable walls and the Age3 cornice keep the host's width

        hp = r.get('profile')

        def _host_z(a):
            zs = ez + (rz - ez) * max(0.0, 1.0 - a / hh)
            if hp:      # measured cross-ridge envelope (asymmetric eaves, fringes, dormers)
                f = min(20.0, max(0.0, a / hh * 20.0))
                k = min(19, int(f))
                zs = max(zs, hp[k] + (hp[k + 1] - hp[k]) * (f - k))
            return zs

        def _prof(a, hy_, ze__, z1_, zr_):
            if a > hy_ - kick:
                return ze__ + (hy_ - a) * tk
            return z1_ + (zr_ - z1_) * (hy_ - kick - a) / (hy_ - kick)

        def _clear(hy_, ze__):
            z1_ = ze__ + kick * tk
            zr_ = max(z1_ + tp * (hy_ - kick), rz + 0.6)
            ok = all(_prof(hh * k / 40, hy_, ze__, z1_, zr_) - th * 1.3 >= _host_z(hh * k / 40) + 0.12 for k in range(41))
            return ok, z1_, zr_
        for it in range(80):
            ok, z1, zr = _clear(hy, ze_)
            if ok:
                break
            if hy < hh + 1.4:
                hy += 0.05
            else:
                ze_ += 0.05

        def zprof(y):
            a = abs(y)
            if a > hy - kick:
                return ze_ + (hy - a) * tk
            return z1 + (zr - z1) * (hy - kick - a) / (hy - kick)

        nside = sg(5, lod, 2)

        def yv(v):
            s_ = 2 * v - 1
            q = 1 - abs(s_)
            if q <= 1.0 / nside:
                d = q * nside * kick
            else:
                d = kick + (q - 1.0 / nside) / (1 - 1.0 / nside) * (hy - kick)
            return math.copysign(hy - d, s_ if s_ != 0 else 1.0)

        def surf(u, v):
            y = yv(v)
            return V((2 * u - 1) * hx, y, zprof(y))

        def uvf(u, v):
            y = yv(v)
            return ((2 * u - 1) * hx, (hy - abs(y)) * 1.8)

        # the first row from each eave is the bell-cast kick, so the kink is a real edge
        g.grid_shell(surf, sg(6, lod, 2), 2 * nside, th, 'Roof', SHELL, uv_fn=uvf)
        for sx in (-1, 1):
            xg = sx * (hx - 0.25)
            pts = [(-(hy0 - 0.35), ze_ - 0.4), ((hy0 - 0.35), ze_ - 0.4)]
            k = sg(6, lod, 2)
            for i in range(k + 1):
                y = (hy0 - 0.35) - 2 * (hy0 - 0.35) * i / k
                pts.append((y, zprof(y) - th * 0.6))
            prism_yz(g, pts, xg - 0.2, xg + 0.2, 'Structure', BODY)
            for side in (-1, 1):
                a_ = V(xg, side * (hy - 0.2), zprof(hy - 0.2) + 0.02)
                b_ = V(xg, 0.0, zr + 0.05)
                g.beam(a_, b_, 0.5, 0.18, mat='Detail', band='palestone', part=DETAIL, up=(0, 0, 1))
            g.lathe([(0.16, zr - 0.2), (0.14, zr + 0.4), (0.0, zr + 0.45)], sg(8, lod, 4), (xg, 0, 0), 'Detail', DETAIL, band='palestone')
            if age == 2:
                finial_ring(g, (xg, 0, zr + 0.82), 0.36, 0.075, lod=lod)
            else:
                for (rr, zz) in ((0.3, zr + 0.42), (0.23, zr + 0.66)):
                    g.lathe([(rr * 0.8, zz), (rr, zz + 0.05), (rr, zz + 0.14), (rr * 0.8, zz + 0.19)], sg(10, lod, 5), (xg, 0, 0),
                            'Detail', DETAIL, band='palestone', cap_top=True, cap_bot=True)
                finial_ring(g, (xg, 0, zr + 1.3), 0.42, 0.085, lod=lod)
        g.tube([V(-hx, 0, zr + 0.06), V(hx, 0, zr + 0.06)], 0.09, seg=5, mat='Detail', band='palestone', part=SHELL)
        if age == 3:
            for sy in (-1, 1):
                g.box((0, sy * (hy0 - 0.55), ze_ - 0.28), (2 * hx - 0.5, 0.35, 0.26), 'Detail', band='palestone', part=DETAIL)
        n = max(3, int(L / 1.0))
        for i in range(n + 1):
            x = -L / 2 + L * i / n
            for sy in (-1, 1):
                g.beam(V(x, sy * (hy - 0.5), zprof(hy - 0.5) - th - 0.1), V(x, 0, zr - th - 0.15), 0.1, 0.14,
                       mat='Detail', band='oak', part=FRAME)
        g.beam(V(-hx + 0.3, 0, zr - th - 0.2), V(hx - 0.3, 0, zr - th - 0.2), 0.18, 0.2, mat='Detail', band='oak', part=FRAME)


def c_ringwall(kind):
    def fn(lod):
        g = Geo()
        L = 10.0
        n = sg(12, lod, 3)
        if kind == 'Curve':
            R = 25.0
            ang = L / R
            path = [V(R * math.sin(-ang / 2 + ang * k / n), R * math.cos(-ang / 2 + ang * k / n) - R, 0) for k in range(n + 1)]
            cy = (min(p.y for p in path) + max(p.y for p in path)) / 2
            path = [p - V(0, cy, 0) for p in path]
        else:
            path = [V(-L / 2 + L * k / n, 0, 0) for k in range(n + 1)]
        prof = [(-0.46, -0.3), (0.46, -0.3), (0.43, 0.1), (0.3, 1.12), (-0.3, 1.12), (-0.43, 0.1)]
        sweep(g, path, prof, 'Structure', BODY)
        if lod < 2:
            rng = random.Random(3 if kind == 'Curve' else 4)
            step = 0.3 if lod == 0 else 0.6
            tot = sum((path[i] - path[i - 1]).length for i in range(1, len(path)))
            m = int(tot / step)
            for i in range(m):
                f = (i + 0.5) / m
                k = min(int(f * n), n - 1)
                fl = f * n - k
                p = path[k].lerp(path[k + 1], fl)
                d = (path[k + 1] - path[k]).normalized()
                h = rng.uniform(0.26, 0.42)
                g.box((p.x, p.y, 1.12 + h / 2 - 0.05), (0.13, 0.62, h), 'Structure', BODY,
                      rot=Matrix.Rotation(math.atan2(d.y, d.x), 4, 'Z') @ Matrix.Rotation(rng.uniform(-0.18, 0.18), 4, 'Y'))
        else:
            sweep(g, path, [(-0.3, 1.05), (0.3, 1.05), (0.0, 1.42)], 'Structure', BODY)
        return g
    return fn


def c_spire(lod):
    g = Geo()
    seg = sg(32, lod, 10)
    lathe_solid(g, [(0.0, -0.3), (4.4, -0.3), (4.4, 0.4), (3.7, 0.4), (3.7, 0.85), (0.0, 0.85)], seg, mat='Structure',
                part=FIX, uv_r=4.0)
    n = 17
    prof = []
    z = 0.85
    for i in range(n):
        f = i / (n - 1)
        r = 2.5 * (0.42 / 2.5) ** f
        h = 1.95 - 0.75 * f
        if lod == 0:
            prof += [(r * 0.78, z), (r * 0.78, z + 0.08), (r * 0.97, z + 0.16), (r, z + 0.28), (r, z + h - 0.22),
                     (r * 0.95, z + h)]
        elif lod == 1:
            prof += [(r * 0.8, z), (r, z + 0.2), (r, z + h - 0.18), (r * 0.94, z + h)]
        else:
            prof += [(r * 0.85, z), (r, z + 0.15), (r * 0.96, z + h)]
        z += h
    prof += [(0.2, z), (0.0, z + 0.3)]
    g.lathe(prof, seg, (0, 0, 0), 'Structure', BODY, cap_bot=True, uv_r=2.5)
    finial_ring(g, (0, 0, z + 0.85), 0.5, 0.1, n=20, lod=lod)
    g.lathe([(0.12, z + 0.1), (0.1, z + 0.3), (0.0, z + 0.33)], 6, (0, 0, 0), 'Detail', DETAIL, band='palestone')
    return g


def c_standing_stone(v):
    def fn(lod):
        g = Geo()
        rng = random.Random(70 + v)
        h = (2.7, 3.3, 2.2)[v]
        wx, wy = (0.95, 1.1, 0.8)[v], (0.55, 0.6, 0.5)[v]
        cuts = (3, 1, 0)[lod]
        M = Matrix.Translation((0, 0, (h - 0.45) / 2)) @ Matrix.Diagonal((wx, wy, h + 0.45, 1))
        ret = bmesh.ops.create_cube(g.bm, size=1.0, matrix=M)
        if cuts:
            edges = list({e for vv in ret['verts'] for e in vv.link_edges})
            bmesh.ops.subdivide_edges(g.bm, edges=edges, cuts=cuts, use_grid_fill=True)
        off = V(rng.uniform(0, 50), rng.uniform(0, 50), 0)
        for vv in g.bm.verts:
            p = vv.co
            f = min(1.0, max(0.0, (p.z + 0.45) / (h + 0.45)))
            taper = 1.0 - 0.35 * f ** 1.5
            n_ = noise.noise(p * 1.3 + off)
            vv.co = V(p.x * taper * (1 + 0.15 * n_), p.y * taper * (1 + 0.15 * n_), p.z + (0.25 * n_ if p.z > h - 0.5 else 0))
        bmesh.ops.transform(g.bm, matrix=Matrix.Rotation(rng.uniform(-0.06, 0.06), 4, 'X') @ Matrix.Rotation(rng.uniform(-0.6, 0.6), 4, 'Z'),
                            verts=list(g.bm.verts))
        g.mark(list(g.bm.faces), 'Structure', UV_BOX, 'oak', BODY)
        return g
    return fn


def c_ornament(age):
    def fn(lod):
        g = Geo()
        g.box((0, -0.07, 2.24), (1.75, 0.14, 0.38), 'Detail', band='palestone', part=DETAIL)
        for sx in (-1, 1):
            g.box((sx * 0.68, -0.05, 1.05), (0.26, 0.1, 2.1), 'Detail', band='palestone', part=DETAIL)
        segn = sg(12, lod, 6)
        for r in (0.15, 0.1, 0.05):
            g.tube(ring_pts(V(0, -0.15, 2.24), r, segn, 'XZ'), 0.022, seg=3, mat='Detail', band='palestone', part=DETAIL, closed=True)
        for sx in (-1, 1):
            for r in (0.1, 0.05):
                g.tube(ring_pts(V(sx * 0.6, -0.15, 2.24), r, segn, 'XZ'), 0.018, seg=3, mat='Detail', band='palestone',
                       part=DETAIL, closed=True)
        if age == 3:
            c = V(0, -0.12, 2.95)
            g.tube(ring_pts(c, 0.42, sg(20, lod, 8), 'XZ'), 0.07, seg=sg(6, lod, 4), mat='Detail', band='palestone',
                   part=DETAIL, closed=True)
            for k in range(6):
                a = math.pi * k / 3
                g.beam(c, c + V(0.4 * math.cos(a), 0, 0.4 * math.sin(a)), 0.06, 0.06, mat='Detail', band='palestone', part=DETAIL)
            g.lathe([(0.0, -0.18), (0.1, -0.18), (0.1, -0.06), (0.0, -0.06)], 8, (0, 0, 0), 'Detail', DETAIL, band='palestone')
            for sx in (-1, 1):
                g.box((sx * 1.25, -0.25, 0.55), (0.36, 0.3, 1.4), 'Detail', band='palestone', part=DETAIL)
        return g
    return fn


# =====================================================================================================
# registry
# =====================================================================================================

CULTURES = {
    'A': {'name': 'Cliff-Terrace', 'concept': 'docs/concept/settlements/culture-a.png'},
    'B': {'name': 'Lagoon-Weaver', 'concept': 'docs/concept/settlements/culture-b.png'},
    'C': {'name': 'Ring-Builder', 'concept': 'docs/concept/settlements/culture-c.png'},
}

ALL_STATES = ('Intact', 'Construction', 'Damaged', 'Ruin')


def nominal_hosts(ages=(2, 3)):
    """Stand-in host records (used only when the Age2 / Age3 manifests list no houses): a straight gable
    roof over the nominal footprint, 0.45 m overhang, 45 degrees."""
    out = []
    for k, (W, D, ev) in HOST.items():
        age = 3 if k == 'T' else 2
        if age not in ages:
            continue
        axis = 'X' if W >= D else 'Y'
        half = min(W, D) / 2 + 0.45
        out.append(dict(id='Nominal%s' % k, family='Age%d' % age, age=age, asset='Nominal%s' % k, mesh=None, fbx=None,
                        maps_to='size class %s' % k, roof=dict(min=[-W / 2 - 0.45, -D / 2 - 0.45, ev - 0.15],
                                                               max=[W / 2 + 0.45, D / 2 + 0.45, ev + half],
                                                               eave_z=ev - 0.15, ridge_z=ev + half, ridge_axis=axis)))
    return out


def registry(hosts=None):
    R = []

    def add(asset, culture, ages, role, kind, fn, mats, desc, host=None, states=ALL_STATES, sockets=None):
        e = dict(asset=asset, culture=culture, ages=ages, role=role, kind=kind, fn=fn, mats=mats, desc=desc,
                 host=host, states=list(states), sockets=sockets or {})
        R.append(e)

    mA = {'Structure': 'MI_Culture_Plaster_A', 'Roof': 'MI_Culture_Dome_A', 'Detail': 'MI_Culture_Trim_A'}
    mAr = {'Structure': 'MI_Culture_Rubble_A', 'Roof': 'MI_Culture_Water_A', 'Detail': 'MI_Culture_Trim_A'}
    mAk = {'Structure': 'MI_Culture_Rock_A', 'Detail': 'MI_Culture_Trim_A'}
    mB = {'Structure': 'MI_Culture_Timber_B', 'Roof': 'MI_Culture_Shingle_B', 'Detail': 'MI_Culture_Trim_B'}
    mBg = {'Structure': 'MI_Culture_Timber_B', 'Roof': 'MI_Culture_Garden_B', 'Detail': 'MI_Culture_Trim_B'}
    mC = {'Structure': 'MI_Culture_Stone_C', 'Roof': 'MI_Culture_Slate_C', 'Detail': 'MI_Culture_Trim_C'}
    mCd = {'Structure': 'MI_Culture_DryStone_C', 'Detail': 'MI_Culture_Trim_C'}
    mCk = {'Structure': 'MI_Culture_Rock_C', 'Detail': 'MI_Culture_Trim_C'}
    mCs = {'Structure': 'MI_Culture_SpireStone_C', 'Detail': 'MI_Culture_Trim_C'}
    doorsock = {'Attach': "host front door: bottom centre of the doorway on the wall face (relief toward -Y)"}

    hosts = hosts or []
    if not any(h['age'] == 2 for h in hosts):
        hosts = hosts + nominal_hosts((2,))
    if not any(h['age'] == 3 for h in hosts):
        hosts = hosts + nominal_hosts((3,))

    # --- per-host overlays: roofs for A, B, C and the pile platform for B
    for h in hosts:
        age = h['age']
        tag = h['asset'].replace('_', '')
        hs = {'Host': h.get('mesh') or h['id'], 'Attach': 'same transform as the host (host space)'}
        add('A%dRoof%s' % (age, tag), 'A', [age], 'roof', 'roof', a_roof_host(h, age), mA,
            'Cliff-Terrace roof for %s: the pitched host roof is hidden inside a jettied plaster attic storey with '
            'cobalt-framed lit windows; its flat roof is a terrace with a parapet (open toward the front stair) and a '
            'lime-washed dome on a %s.' % (h.get('mesh') or h['id'], 'square drum' if age == 2 else
                                            'windowed octagonal drum, ribbed with a lantern, domed corner pinnacles, cobalt band'),
            host=h, sockets=hs)
        add('B%dRoof%s' % (age, tag), 'B', [age], 'roof', 'roof', b_roof_host(h, age), mB,
            'Lagoon-Weaver saddle roof for %s: silver shingle with moss, ridge sweeping up into horns beyond both gables'
            '%s, woven-reed gable infill.' % (h.get('mesh') or h['id'], ' (moderate rise, one saffron ribbon per horn)' if age == 2
                                               else ' (tall, asymmetric, carved horn caps, paired saffron ribbons)'),
            host=h, sockets=hs)
        add('B%dStilts%s' % (age, tag), 'B', [age], 'structure', 'struct', b_stilts_host(h, age), mB,
            'Tarred pile platform under %s: deck top at the host ground (z 0) with a 1.6 m verandah in front, water '
            'surface at -1.8, piles to -4.8, bearers, rim joists, ladder and mooring post%s.'
            % (h.get('mesh') or h['id'], ', cross bracing and a verandah railing' if age == 3 else ''),
            host=h, sockets=dict(hs, Water='z = -1.8 (raise host + platform 1.8 m above the water)'))
        add('C%dRoof%s' % (age, tag), 'C', [age], 'roof', 'roof', c_roof_host(h, age), mC,
            'Ring-Builder roof for %s: steep bell-cast slate gable (%d deg) with pale stone gable walls and copings, '
            'ring finials at both ridge ends%s.' % (h.get('mesh') or h['id'], 58 if age == 2 else 62,
                                                   '' if age == 2 else ', stacked-disc finials and a stone eave cornice'),
            host=h, sockets=hs)

    # --- culture A
    add('A2Ornament', 'A', [2], 'ornament', 'prop', a_ornament(2), mA,
        'Cobalt-painted door surround, lintel and 4 m painted band; pale stone threshold.', states=('Intact',), sockets=doorsock)
    add('A3Ornament', 'A', [3], 'ornament', 'prop', a_ornament(3), mA,
        'Cobalt arched door surround with keystone, 5 m painted band, two flanking screened windows (night-lit), '
        'two-step threshold.', states=('Intact',), sockets=doorsock)
    add('ATerraceStair', 'A', [2, 3], 'structure', 'struct', a_terrace, mAr,
        '12 x 8 m red-sandstone retaining terrace (3 m) with a battered face, beam ends, parapet, a stair flight '
        'climbing the face, a storeroom door and a water chute into a basin. Houses stack on its top.',
        sockets={'TerraceTop': [0, 0, 3.0], 'StairFoot': [2.9, -5.0, 0.0], 'StairHead': [-4.9, -4.6, 3.0]})
    add('ACistern', 'A', [2, 3], 'prop', 'prop', a_cistern, mAr,
        'Round red-stone cistern (3.2 m) with a pale coping, water surface (Roof slot = water MI) and outflow chute.',
        states=('Intact',))
    add('ATempleNiche', 'A', [2, 3], 'landmark', 'struct', a_niche, mAk,
        'The Cliff-Terrace temple: an 18 x 22 m rock face with a 15 m arched niche, carved archivolts, a cobalt '
        'doorway, pilasters and steps. Set into a cliff or against the top terrace.')
    # --- culture B
    add('BWalkwayStraight', 'B', [2, 3], 'structure', 'struct', b_walkway('Straight'), mB,
        '8 m straight boardwalk on paired piles, deck +1.8 m over the water (level with the house decks), rope rail on one side.',
        sockets={'Ends': [[-4.0, 0, 1.8], [4.0, 0, 1.8]], 'Width': 1.6, 'Water': 'z = 0'})
    add('BWalkwayCurve', 'B', [2, 3], 'structure', 'struct', b_walkway('Curve'), mB,
        '45 degree curved boardwalk (centre-line radius 10 m) for the round water courts.',
        sockets={'Radius': 10.0, 'Angle': 45, 'Water': 'z = 0'})
    add('BBridge', 'B', [2, 3], 'structure', 'struct', b_walkway('Bridge'), mB,
        '10 m arched footbridge rising 2 m over a channel, rails both sides.', sockets={'Ends': [[-5.0, 0, 1.8], [5.0, 0, 1.8]], 'Water': 'z = 0'})
    add('BGardenRaft', 'B', [2, 3], 'prop', 'prop', b_raft, mBg,
        '6 x 4 m floating garden raft: lashed logs, two woven beds of vegetables (Roof slot = garden MI).',
        states=('Intact',), sockets={'Water': 'z = 0'})
    add('BBreakwater', 'B', [2, 3], 'structure', 'struct', b_breakwater, mB,
        '12 m segment of the woven breakwater (arc radius 30 m): two stake rows with wattle between, stone toe.',
        sockets={'Water': 'z = 0'})
    add('BTempleSpiral', 'B', [2, 3], 'landmark', 'struct', b_temple, mB,
        'The Lagoon-Weaver pier temple: a 20 m round platform on piles, woven drum, and a spiral shingle roof '
        'winding up to an oak spire with a ring finial and saffron pennants.', sockets={'Water': 'z = 0'})
    for age in (2, 3):
        add('B%dOrnament' % age, 'B', [age], 'ornament', 'prop', b_ornament(age), mB,
            'Woven-reed facade panels in an oak frame (3 m, scale X to the wall)'
            + (', with braces and a saffron garland.' if age == 3 else '.'), states=('Intact',),
            sockets={'Attach': 'host front wall: bottom centre of the panel on the wall face'})
    # --- culture C
    for age in (2, 3):
        add('C%dDrumHouse' % age, 'C', [age], 'structure', 'struct', c_drum(age), mC,
            ('Pale limestone drum house (%s), recessed door with a plain lintel, deep night-lit windows, '
             'stone plinth. Replaces the host body for this culture (the Ring-Builders have one house type).')
            % ('6.6 m, one storey' if age == 2 else '7.8 m, two storeys with string course and cornice'),
            sockets={'RoofBase': [0, 0, 2.75 if age == 2 else 5.6], 'Door': [0, -(3.3 if age == 2 else 3.9), 0.24]})
        add('C%dRoofCone' % age, 'C', [age], 'roof', 'roofeave', c_cone(age), mC,
            'Tall bell-cast slate cone (%d deg) for the drum house, pale stone finial post and ring%s.'
            % (55 if age == 2 else 60, '' if age == 2 else ', stacked-disc finial and stone eave cornice'),
            sockets={'Attach': 'C%dDrumHouse RoofBase (pivot on the eave plane)' % age},
            host=dict(eave_c=2.75 if age == 2 else 5.6, foot=(3.9 if age == 2 else 4.6)))
    add('CRingWallCurve', 'C', [2, 3], 'structure', 'struct', c_ringwall('Curve'), mCd,
        '10 m dry-stone ring wall segment (radius 25 m), battered, upright coping stones.')
    add('CRingWallStraight', 'C', [2, 3], 'structure', 'struct', c_ringwall('Straight'), mCd,
        '10 m straight dry-stone wall (radial streets, folds, large rings).')
    add('CSpire', 'C', [2, 3], 'landmark', 'struct', c_spire, mCs,
        'The Ring-Builder spire: 17 stacked weathered limestone discs narrowing to 29 m on a stepped round plinth, '
        'ring finial.')
    for v in range(3):
        add('CStandingStone%d' % (v + 1), 'C', [2, 3], 'prop', 'prop', c_standing_stone(v), mCk,
            'Weathered standing stone %.1f m (approach avenues, ring gates).' % ((2.7, 3.3, 2.2)[v]), states=('Intact',))
    for age in (2, 3):
        add('C%dOrnament' % age, 'C', [age], 'ornament', 'prop', c_ornament(age), mC,
            'Pale stone door jambs and lintel carved with concentric rings'
            + ('; a spoked stone ring above the door and two flanking pillar stones.' if age == 3 else '.'),
            states=('Intact',), sockets=doorsock)
    return R
