# DEMIURGE art/culture - building states on the same footprint (ASSETS.md section 2, "States").
#
# A generator tags every face with a part:
#   BODY   load-bearing solid (walls, slabs, drums, piles, decks)
#   SHELL  roof covering (dome, shingle or slate shell)
#   FRAME  hidden skeleton (rafters, ribs, battens) - only shown in Construction / Damaged / Ruin
#   DETAIL ornament, cloth, finials - dropped or knocked loose in damaged states
#   FIX    never changes (plinths, foundations under the waterline)
#
# kind 'struct' (stands on the ground) or 'roof' (overlay; pivot at the host's eave, so ruined roof debris is
# dropped to z = -eave, the host's ground).
#
#   Construction  struct: body built to ~55 % height (ragged course line) + scaffold; shell lower 30 %; frame shown
#                 roof:   frame + slab/drum + lower 30 % of the covering, bundles of roofing on the host's ground
#   Damaged       2-3 holes punched through the covering (frame visible), 1-2 bites out of the wall tops,
#                 a third of the ornaments gone, a little rubble
#   Ruin          struct: walls broken to 30-50 % with a jagged top, covering gone but for a fragment, rubble mounds
#                 roof:   nothing at the eave; collapsed covering fragments, beams and rubble on the host's ground
import math
import random
import bmesh
import bpy
from mathutils import Vector, Matrix
import cul_geo as cg
from cul_geo import V, Geo, BODY, SHELL, FRAME, DETAIL, FIX

STATES = ('Intact', 'Construction', 'Damaged', 'Ruin')


# ---------------------------------------------------------------------- object helpers

def geo_to_obj(g, name, mats, coll):
    ob = cg.bm_to_object(g.bm, name, coll, mats)
    return ob


def split_parts(ob, coll):
    """-> {part: object} (each a copy containing only that part's faces)."""
    out = {}
    pa = ob.data.attributes.get('part')
    vals = [0] * len(ob.data.polygons)
    if pa:
        pa.data.foreach_get('value', vals)
    for p in set(vals):
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        lay = bm.faces.layers.int.get('part')
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f[lay] != p], context='FACES')
        me = bpy.data.meshes.new(ob.name + '_p%d' % p)
        bm.to_mesh(me)
        bm.free()
        for m in ob.data.materials:
            me.materials.append(m)
        o = bpy.data.objects.new(me.name, me)
        coll.objects.link(o)
        out[p] = o
    return out


def remove(ob):
    me = ob.data
    bpy.data.objects.remove(ob, do_unlink=True)
    if me and me.users == 0:
        bpy.data.meshes.remove(me)


def _bool_one(me, cutter_ob, op, mats, coll):
    """Boolean of one closed island mesh with the cutter -> new mesh."""
    o = bpy.data.objects.new('_isl', me)
    coll.objects.link(o)
    for m in mats:
        me.materials.append(m)
    md = o.modifiers.new('b', 'BOOLEAN')
    md.operation = op
    md.object = cutter_ob
    md.solver = 'EXACT'
    md.use_self = False
    md.use_hole_tolerant = True
    try:
        md.material_mode = 'INDEX'
    except AttributeError:
        pass
    cg.apply_mods(o)
    out = o.data
    bpy.data.objects.remove(o, do_unlink=True)
    return out


def boolean(ob, cutter_geo, op, coll, use_self=False, zband=None):
    """EXACT boolean applied island by island: every connected part of the asset is a closed solid on its own
    (boxes, lathes, sweeps are never welded), so each is cut separately and only if the cutter touches it.
    That stays correct where parts interpenetrate (stakes through wattle) and keeps untouched parts as built.
    zband=(lo, hi): for a jagged keep-below cutter, islands entirely below lo are inside, above hi outside."""
    if len(ob.data.polygons) == 0:
        return ob
    c = cg.bm_to_object(cutter_geo.bm, ob.name + '_cut', coll)
    for m in ob.data.materials:
        c.data.materials.append(m)
    cmn, cmx = cg.bbox(c)
    c_area = sum(p.area for p in c.data.polygons)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.faces.ensure_lookup_table()
    seen = set()
    isls = []
    for f in bm.faces:
        if f.index in seen:
            continue
        st = [f]
        seen.add(f.index)
        cur = []
        while st:
            x = st.pop()
            cur.append(x)
            for e in x.edges:
                for y in e.link_faces:
                    if y.index not in seen:
                        seen.add(y.index)
                        st.append(y)
        isls.append(cur)
    drop = set()
    results = []
    mats = list(ob.data.materials)
    for isl in isls:
        vs = {v for f in isl for v in f.verts}
        mn = Vector((min(v.co.x for v in vs), min(v.co.y for v in vs), min(v.co.z for v in vs)))
        mx = Vector((max(v.co.x for v in vs), max(v.co.y for v in vs), max(v.co.z for v in vs)))
        overlap = all(mn[i] <= cmx[i] and mx[i] >= cmn[i] for i in range(3))
        inside = False
        if overlap and zband is not None:
            within = all(mn[i] >= cmn[i] and mx[i] <= cmx[i] for i in range(2))
            if within and mx.z < zband[0]:
                inside = True
            elif mn.z > zband[1]:
                overlap = False
        if not overlap:
            if op == 'INTERSECT':
                drop.update(f.index for f in isl)
            continue
        if inside:
            if op == 'DIFFERENCE':
                drop.update(f.index for f in isl)
            continue
        # cut this island on its own
        sub = bm.copy()
        sub.faces.ensure_lookup_table()
        keep = {f.index for f in isl}
        bmesh.ops.delete(sub, geom=[f for f in sub.faces if f.index not in keep], context='FACES')
        me = bpy.data.meshes.new('_isl')
        sub.to_mesh(me)
        sub.free()
        a0 = sum(p.area for p in me.polygons)
        backup = me.copy()
        res = _bool_one(me, c, op, mats, coll)
        # Verifier fix (2026-11-19): on thin roof shells the EXACT solver sometimes misclassifies and throws
        # away a whole slope (the damaged Age3 Townhouse_B saddle lost its entire front covering). A
        # DIFFERENCE can only remove what lies inside the cutter, i.e. at most about the cutter's own surface
        # area; if far more vanished, keep the island uncut.
        if op == 'DIFFERENCE' and a0 - sum(p.area for p in res.polygons) > 1.5 * c_area + 0.5:
            bpy.data.meshes.remove(res)
            res = backup
        else:
            bpy.data.meshes.remove(backup)
        results.append(res)
        drop.update(keep)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index in drop], context='FACES')
    for me in results:
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    bm.to_mesh(ob.data)
    bm.free()
    remove(c)
    return ob


def join(objs, name, coll, mats):
    bm = bmesh.new()
    for o in objs:
        if o is None or len(o.data.polygons) == 0:
            continue
        tmp = o.data.copy()
        tmp.transform(o.matrix_world)
        bm.from_mesh(tmp)
        bpy.data.meshes.remove(tmp)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(m)
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def islands(ob):
    """Connected face islands -> list of lists of polygon indices."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.faces.ensure_lookup_table()
    seen = set()
    out = []
    for f in bm.faces:
        if f.index in seen:
            continue
        stack = [f]
        isl = []
        seen.add(f.index)
        while stack:
            x = stack.pop()
            isl.append(x.index)
            for e in x.edges:
                for y in e.link_faces:
                    if y.index not in seen:
                        seen.add(y.index)
                        stack.append(y)
        out.append(isl)
    bm.free()
    return out


def keep_islands(ob, keep, xforms=None):
    """Keep the listed islands (lists of polygon indices); optional per-island 4x4 transform."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.faces.ensure_lookup_table()
    keepset = set()
    for k, isl in enumerate(keep):
        keepset.update(isl)
        if xforms and xforms[k] is not None:
            vs = {v for i in isl for v in bm.faces[i].verts}
            bmesh.ops.transform(bm, matrix=xforms[k], verts=list(vs))
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index not in keepset], context='FACES')
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def obj_bbox(ob):
    if len(ob.data.vertices) == 0:
        return None
    return cg.bbox(ob)


# ---------------------------------------------------------------------- cutters

def jagged_block(mn, mx, h, amp, seed, mat='Structure', res=None, below=True):
    """Closed block covering the footprint, top surface ragged around height h (keeps what is below when
    intersected)."""
    g = Geo()
    if res is None:
        res = 1.2 if CUT_SUB[0] == 2 else 2.4
    rng = random.Random(seed)
    pad = 1.0
    x0, x1, y0, y1 = mn.x - pad, mx.x + pad, mn.y - pad, mx.y + pad
    nx = max(4, int((x1 - x0) / res))
    ny = max(4, int((y1 - y0) / res))
    zb = mn.z - 5.0
    from mathutils import noise
    off = V(rng.uniform(0, 99), rng.uniform(0, 99), 0)
    top = [[None] * (ny + 1) for _ in range(nx + 1)]
    for i in range(nx + 1):
        for j in range(ny + 1):
            x = x0 + (x1 - x0) * i / nx
            y = y0 + (y1 - y0) * j / ny
            z = h + amp * (noise.noise(V(x * 0.35, y * 0.35, 0) + off) + 0.5 * noise.noise(V(x * 1.1, y * 1.1, 3) + off))
            top[i][j] = g.bm.verts.new(V(x, y, z))
    faces = []
    for i in range(nx):
        for j in range(ny):
            faces.append(g.bm.faces.new([top[i][j], top[i + 1][j], top[i + 1][j + 1], top[i][j + 1]]))
    # sides and bottom
    ring = [top[i][0] for i in range(nx + 1)] + [top[nx][j] for j in range(1, ny + 1)] + \
           [top[i][ny] for i in range(nx - 1, -1, -1)] + [top[0][j] for j in range(ny - 1, 0, -1)]
    bot = [g.bm.verts.new(V(v.co.x, v.co.y, zb)) for v in ring]
    n = len(ring)
    for k in range(n):
        faces.append(g.bm.faces.new([ring[(k + 1) % n], ring[k], bot[k], bot[(k + 1) % n]]))
    faces.append(g.bm.faces.new(bot))
    bmesh.ops.recalc_face_normals(g.bm, faces=faces)
    g.mark(faces, mat, cg.UV_BOX, 'oak', BODY)
    return g


CUT_SUB = [2]
LOD = [0]


def blob_cutter(c, r, seed, mat='Structure', squash=1.0, subdiv=None):
    g = Geo()
    g.blob(c, r, mat, BODY, subdiv=CUT_SUB[0] if subdiv is None else subdiv, squash=squash, rough=0.35, seed=seed)
    return g


# ---------------------------------------------------------------------- additions

def rubble(g, c, r, seed, mat='Structure', n_chunks=6, squash=0.38):
    rng = random.Random(seed)
    lod = LOD[0]
    g.blob(c, r, mat, FIX, subdiv=(2, 1, 1)[lod], squash=squash, rough=0.45, seed=seed)
    for k in range((n_chunks, n_chunks // 2, 0)[lod]):
        a = rng.uniform(0, 2 * math.pi)
        d = r * rng.uniform(0.7, 1.4)
        s = r * rng.uniform(0.08, 0.18)
        p = V(c) + V(math.cos(a) * d, math.sin(a) * d, s * 0.4)
        g.box(p, (s * 2.2, s * 1.4, s), mat, rz=rng.uniform(0, 3.14), part=FIX)


def scaffold(g, mn, mx, top, seed):
    """Timber scaffold around a footprint up to height top."""
    rng = random.Random(seed)
    off = 0.9
    x0, x1, y0, y1 = mn.x - off, mx.x + off, mn.y - off, mx.y + off
    poles = []
    for (ax, ay, bx, by) in ((x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)):
        L = math.hypot(bx - ax, by - ay)
        n = max(1, int(L / 2.6))
        for k in range(n):
            t = k / n
            poles.append(V(ax + (bx - ax) * t, ay + (by - ay) * t, 0))
    for p in poles:
        g.beam(p + V(rng.uniform(-.05, .05), rng.uniform(-.05, .05), -0.3), p + V(0, 0, top + 1.2), 0.12, mat='Detail',
               band='oak', part=FIX)
    lv = 1.8
    z = lv
    while z < top + 0.6:
        corners = [V(x0, y0, z), V(x1, y0, z), V(x1, y1, z), V(x0, y1, z)]
        for k in range(4):
            g.beam(corners[k], corners[(k + 1) % 4], 0.09, mat='Detail', band='oak', part=FIX)
        # a plank walk on the front side
        g.box(((x0 + x1) / 2, y0 + 0.35, z + 0.06), (x1 - x0, 0.6, 0.05), 'Detail', part=FIX, band='oak')
        z += lv
    # a ladder-like diagonal brace on each side
    g.beam(V(x0, y0, 0), V(x0 + 2.4, y0, min(top, 4.0)), 0.08, mat='Detail', band='oak', part=FIX)
    g.beam(V(x1, y1, 0), V(x1 - 2.4, y1, min(top, 4.0)), 0.08, mat='Detail', band='oak', part=FIX)


def bundles(g, c, seed, mat='Roof', n=5):
    rng = random.Random(seed)
    for k in range(n):
        p = V(c) + V(rng.uniform(-1.2, 1.2), rng.uniform(-0.6, 0.6), 0)
        h = rng.uniform(0.25, 0.5)
        g.box(p + V(0, 0, h / 2), (rng.uniform(0.7, 1.1), rng.uniform(0.5, 0.7), h), mat, rz=rng.uniform(-0.3, 0.3), part=FIX)


# ---------------------------------------------------------------------- the state builder

def lay_flat(frame, fmn, fmx, gz, rng, nmax=8):
    """Pick up to nmax frame islands (rafters, ribs) and stack them flat on the ground in front of the footprint."""
    import numpy as np
    isl = islands(frame)
    rng.shuffle(isl)
    isl = isl[:nmax]
    xf = []
    x0 = fmn.x + 0.6
    for k, il in enumerate(isl):
        vs = {vi for pi in il for vi in frame.data.polygons[pi].vertices}
        P = np.array([tuple(frame.data.vertices[vi].co) for vi in vs], dtype=np.float64)
        c = P.mean(axis=0)
        u, s_, vt = np.linalg.svd(P - c, full_matrices=False)
        d = V(*vt[0])
        if d.length < 1e-6:
            d = V(1, 0, 0)
        R = d.rotation_difference(V(1, 0, 0)).to_matrix().to_4x4()
        Lx = float(np.ptp((P - c) @ vt[0]))
        col, lay = k % 2, k // 2
        tgt = V(x0 + Lx / 2 + col * 0.5, fmn.y - 0.9 - 0.35 * col, gz + 0.12 + 0.16 * lay)
        xf.append(Matrix.Translation(tgt) @ R @ Matrix.Translation(-V(*c)))
    keep_islands(frame, isl, xf)
    return frame


def build(geo_fn, lod, state, seed, kind, name, mats, coll, eave=0.0, foot=None, build_axis='z'):
    """geo_fn(lod) -> Geo. Returns one object for this state."""
    CUT_SUB[0] = 2 if lod == 0 else 1
    LOD[0] = lod
    g = geo_fn(lod)
    whole = geo_to_obj(g, name + '_src', mats, coll)
    g.bm.free()
    parts = split_parts(whole, coll)
    remove(whole)
    rng = random.Random(seed * 7919 + {'Intact': 0, 'Construction': 1, 'Damaged': 2, 'Ruin': 3}[state])
    extra = Geo()
    keep = []

    allb = [obj_bbox(o) for o in parts.values()]
    allb = [b for b in allb if b]
    mn = V(min(b[0].x for b in allb), min(b[0].y for b in allb), min(b[0].z for b in allb))
    mx = V(max(b[1].x for b in allb), max(b[1].y for b in allb), max(b[1].z for b in allb))
    if foot and len(foot) == 4:   # explicit footprint bounds (host-space roofs)
        fmn, fmx = V(foot[0], foot[1], 0), V(foot[2], foot[3], 0)
    elif foot:   # the host footprint for roofs (debris goes inside it)
        fmn, fmx = V(-foot[0] / 2, -foot[1] / 2, 0), V(foot[0] / 2, foot[1] / 2, 0)
    else:
        fmn, fmx = V(mn.x, mn.y, 0), V(mx.x, mx.y, 0)
    size = mx - mn
    smin = max(0.5, min(size.x, size.y))

    body, shell, frame, detail, fix = (parts.get(k) for k in (BODY, SHELL, FRAME, DETAIL, FIX))

    if state == 'Intact':
        keep = [body, shell, detail, fix]
        if frame:
            remove(frame)

    elif state == 'Construction':
        if kind == 'roof':
            # nothing is up yet: cut timbers and bundles of roofing wait on the host's ground
            for o in (body, shell, detail):
                if o:
                    remove(o)
            if frame:
                lay_flat(frame, fmn, fmx, -eave, rng)
            keep = [frame, fix]
            bundles(extra, V(fmx.x - 1.6, fmn.y - 1.2, -eave), rng.randint(0, 999), 'Roof' if shell else 'Structure')
        else:
            if body:
                b0, b1 = obj_bbox(body)
                if build_axis == 'x':      # decks and boardwalks are laid from one end
                    xc = b0.x + (b1.x - b0.x) * 0.55
                    cut = Geo()
                    cut.box(((b0.x - 2 + xc) / 2, (b0.y + b1.y) / 2, (b0.z + b1.z) / 2),
                            (xc - b0.x + 2, b1.y - b0.y + 4, b1.z - b0.z + 4))
                    boolean(body, cut, 'INTERSECT', coll)
                else:
                    base = max(b0.z, 0.0)
                    h = base + max(0.8, (b1.z - base) * 0.55)
                    boolean(body, jagged_block(b0, b1, h, 0.12, rng.randint(0, 999)), 'INTERSECT', coll, zband=(h - 0.3, h + 0.3))
                    if b1.z - base > 2.0:
                        scaffold(extra, b0, b1, h, rng.randint(0, 999))
            if shell:
                s0, s1 = obj_bbox(shell)
                boolean(shell, jagged_block(s0, s1, s0.z + (s1.z - s0.z) * 0.3, 0.05, rng.randint(0, 999), 'Roof'),
                        'INTERSECT', coll)
            if detail:
                remove(detail)
            keep = [body, shell, frame, fix]
            if mn.z > -1.0:       # (water structures get no ground stacks)
                bundles(extra, V(mn.x + 1.5, mn.y - 1.8, max(0.0, mn.z)), rng.randint(0, 999), 'Structure', 4)

    elif state == 'Damaged':
        if shell:
            s0, s1 = obj_bbox(shell)
            sz = s1 - s0
            nh = 2 + rng.randint(0, 1)
            pts = _surface_points(shell, nh, rng, min_nz=0.15)
            for k, p in enumerate(pts):
                r = max(0.5, min(sz.x, sz.y) * rng.uniform(0.13, 0.2))
                boolean(shell, blob_cutter(p, r, rng.randint(0, 999), 'Roof', subdiv=1), 'DIFFERENCE', coll)
        if body:
            b0, b1 = obj_bbox(body)
            nb = 1 + (1 if kind == 'struct' else 0)
            for k in range(nb):
                p = V(rng.uniform(b0.x, b1.x), rng.choice([b0.y, b1.y]) if rng.random() < 0.5 else rng.uniform(b0.y, b1.y), b1.z)
                if kind == 'struct':
                    r = max(0.6, smin * rng.uniform(0.16, 0.24))
                else:
                    r = max(0.4, smin * 0.12)
                boolean(body, blob_cutter(p, r, rng.randint(0, 999)), 'DIFFERENCE', coll)
        if detail:
            isl = islands(detail)
            rng.shuffle(isl)
            kept = isl[:max(0, int(len(isl) * 0.65))]
            xf = []
            for isl_ in kept:
                if rng.random() < 0.25:
                    c = _isl_center(detail, isl_)
                    xf.append(Matrix.Translation(c) @ Matrix.Rotation(rng.uniform(-0.35, 0.35), 4, 'Y') @ Matrix.Translation(-c))
                else:
                    xf.append(None)
            keep_islands(detail, kept, xf)
        keep = [body, shell, frame, detail, fix]
        gz = -eave if kind == 'roof' else 0.0
        rubble(extra, V(rng.uniform(fmn.x, fmx.x) * 0.6, fmn.y - 0.6, gz), max(0.5, smin * 0.1), rng.randint(0, 999),
               'Roof' if shell else 'Structure', 4)

    elif state == 'Ruin':
        if kind == 'struct':
            if body:
                b0, b1 = obj_bbox(body)
                base = max(b0.z, 0.0)
                hh = b1.z - base
                h = base + hh * rng.uniform(0.3, 0.48)
                amp = max(0.2, hh * 0.18)
                boolean(body, jagged_block(b0, b1, h, amp, rng.randint(0, 999)), 'INTERSECT', coll, zband=(h - 2 * amp, h + 2 * amp))
            if shell:
                s0, s1 = obj_bbox(shell)
                boolean(shell, jagged_block(s0, s1, s0.z + (s1.z - s0.z) * 0.22, (s1.z - s0.z) * 0.12,
                                            rng.randint(0, 999), 'Roof'), 'INTERSECT', coll)
                # only one side survives
                c = (s0 + s1) * 0.5
                boolean(shell, blob_cutter(c + V((s1.x - s0.x) * 0.35, 0, 0), max(s1.x - s0.x, s1.y - s0.y) * 0.6,
                                           rng.randint(0, 999), 'Roof', 1.4), 'DIFFERENCE', coll)
            if frame:
                isl = islands(frame)
                rng.shuffle(isl)
                keep_islands(frame, isl[:max(1, len(isl) // 3)])
                f0, f1 = obj_bbox(frame) or (mn, mx)
                boolean(frame, jagged_block(f0, f1, f0.z + (f1.z - f0.z) * 0.5, 0.4, rng.randint(0, 999), 'Detail'),
                        'INTERSECT', coll)
            if detail:
                remove(detail)
            keep = [body, shell, frame, fix]
            nm = 2 + rng.randint(0, 2)
            for k in range(nm):
                p = V(rng.uniform(fmn.x, fmx.x) * 0.7, rng.uniform(fmn.y, fmx.y) * 0.7, 0)
                rubble(extra, p, max(0.6, smin * rng.uniform(0.14, 0.22)), rng.randint(0, 999), 'Structure', 5)
            if shell:
                rubble(extra, V(rng.uniform(fmn.x, fmx.x) * 0.5, rng.uniform(fmn.y, fmx.y) * 0.5, 0),
                       max(0.5, smin * 0.12), rng.randint(0, 999), 'Roof', 3)
        else:   # roof overlay: everything comes down to the host's ground
            frags = []
            if shell:
                s0, s1 = obj_bbox(shell)
                for k in range(2):
                    piece = shell.copy()
                    piece.data = shell.data.copy()
                    coll.objects.link(piece)
                    c = V(rng.uniform(s0.x, s1.x), rng.uniform(s0.y, s1.y), (s0.z + s1.z) / 2)
                    r = max(0.8, min(s1.x - s0.x, s1.y - s0.y) * 0.22)
                    boolean(piece, blob_cutter(c, r, rng.randint(0, 999), 'Roof', 2.5), 'INTERSECT', coll)
                    pb = obj_bbox(piece)
                    if pb:
                        pc = (pb[0] + pb[1]) * 0.5
                        tgt = V(rng.uniform(fmn.x, fmx.x) * 0.5, rng.uniform(fmn.y, fmx.y) * 0.5, -eave + 0.35)
                        M = Matrix.Translation(tgt) @ Matrix.Rotation(rng.uniform(-0.6, 0.6), 4, 'X') @ \
                            Matrix.Rotation(rng.uniform(0, 6.28), 4, 'Z') @ Matrix.Translation(-pc)
                        piece.data.transform(M)
                        frags.append(piece)
                    else:
                        remove(piece)
                remove(shell)
            if frame:
                isl = islands(frame)
                rng.shuffle(isl)
                kept = isl[:min(5, len(isl))]
                xf = []
                for isl_ in kept:
                    c = _isl_center(frame, isl_)
                    tgt = V(rng.uniform(fmn.x, fmx.x) * 0.6, rng.uniform(fmn.y, fmx.y) * 0.6, -eave + 0.5)
                    xf.append(Matrix.Translation(tgt) @ Matrix.Rotation(rng.uniform(-0.5, 0.5), 4, 'Y') @
                              Matrix.Rotation(rng.uniform(0, 6.28), 4, 'Z') @
                              Matrix.Rotation(rng.uniform(1.0, 1.4), 4, 'X') @ Matrix.Translation(-c))
                keep_islands(frame, kept, xf)
                b = obj_bbox(frame)
                if b and b[0].z < -eave - 0.2:
                    frame.data.transform(Matrix.Translation(V(0, 0, -eave - 0.15 - b[0].z)))
            for o in (body, detail):
                if o:
                    remove(o)
            keep = frags + [frame, fix]
            rubble(extra, V(fmn.x * 0.3, fmn.y * 0.2, -eave), max(0.7, smin * 0.16), rng.randint(0, 999), 'Roof', 4)
            rubble(extra, V(fmx.x * 0.35, fmx.y * 0.25, -eave), max(0.6, smin * 0.12), rng.randint(0, 999), 'Structure', 3)

    ex = geo_to_obj(extra, name + '_extra', mats, coll) if len(extra.bm.faces) else None
    extra.bm.free()
    ob = join([o for o in keep if o] + ([ex] if ex else []), name, coll, mats)
    for o in [o for o in keep if o] + ([ex] if ex else []):
        remove(o)
    return ob


def _isl_center(ob, isl):
    vs = {vi for pi in isl for vi in ob.data.polygons[pi].vertices}
    s = V(0, 0, 0)
    for vi in vs:
        s += ob.data.vertices[vi].co
    return s / max(1, len(vs))


def _surface_points(ob, n, rng, min_nz=0.0):
    polys = [p for p in ob.data.polygons if p.normal.z >= min_nz and p.area > 0.01]
    if not polys:
        polys = list(ob.data.polygons)
    pts = []
    tot = sum(p.area for p in polys)
    for k in range(n):
        x = rng.uniform(0, tot)
        acc = 0.0
        for p in polys:
            acc += p.area
            if acc >= x:
                pts.append(p.center.copy())
                break
    return pts
