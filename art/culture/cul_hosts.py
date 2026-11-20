# DEMIURGE art/culture - read the Age2 / Age3 kits through their manifests and measure every host house.
#
# The host kits bake their roofs into the house meshes, so a culture roof must ENVELOPE the host roof. For each
# house listed in art/age2/manifest.json and art/age3/manifest.json (never edited) this module imports the
# Intact FBX the manifest points to (read-only, into a throw-away scene) and measures, in host space:
#   walls    bbox of the Structure faces between the plinth and the eave
#   roof     bbox of the main roof (the connected roof-material islands that reach the ridge; porches and
#            lower wings are left out), eave z (lowest point of that roof), ridge z, ridge axis
# Results are cached in art/culture/hosts.json (with the manifest's mtime) for the build and the previews.
import os
import json
import bpy
import bmesh
from mathutils import Vector

FAMS = ('age2', 'age3')


def _manifest(root, fam):
    p = os.path.join(root, 'art', fam, 'manifest.json')
    if not os.path.exists(p):
        return None, None
    with open(p, encoding='utf-8') as f:
        return json.load(f), os.path.getmtime(p)


def list_hosts(root):
    """-> [(family, asset, mesh_name, fbx_path, entry, roof_material_names)] for the Intact house meshes."""
    out = []
    for fam in FAMS:
        m, _ = _manifest(root, fam)
        if not m:
            continue
        meshes = m.get('meshes', {})
        if isinstance(meshes, list):
            meshes = {e.get('name'): e for e in meshes}
        assets = m.get('assets', {})
        for name, e in sorted(meshes.items()):
            if not isinstance(e, dict) or e.get('state', 'Intact') != 'Intact':
                continue
            kind = e.get('kind') or assets.get(e.get('asset', ''), {}).get('kind')
            if kind != 'house':
                continue
            fbx = e.get('fbx')
            if not fbx:
                continue
            mats = e.get('materials')
            roofnames = {'Roof'}
            if isinstance(mats, dict) and 'Roof' in mats:
                roofnames.add(mats['Roof'])
            if isinstance(e.get('material_names'), list) and isinstance(mats, list) and 'Roof' in mats:
                roofnames.add(e['material_names'][mats.index('Roof')])
            out.append((fam, e.get('asset', name), name, os.path.join(root, fbx), e, roofnames,
                        assets.get(e.get('asset', ''), {}).get('maps_to') or m.get('mapping_hint', {}).get(e.get('asset', '').split('_')[0])))
    return out


def _rect_info(vs, x0, x1, y0, y1, vz=None):
    """vs: main-roof points (eave), vz: points that count for the ridge height (main + caps / dormers)."""
    ins = [p for p in vs if x0 - 0.05 <= p.x <= x1 + 0.05 and y0 - 0.05 <= p.y <= y1 + 0.05] or vs
    insz = [p for p in (vz or vs) if x0 - 0.05 <= p.x <= x1 + 0.05 and y0 - 0.05 <= p.y <= y1 + 0.05] or ins
    zmin = min(p.z for p in ins)
    zmax = max(p.z for p in insz)
    top = [p for p in insz if p.z > zmax - 0.25]
    ex = max(p.x for p in top) - min(p.x for p in top)
    ey = max(p.y for p in top) - min(p.y for p in top)
    if abs(ex - ey) < 0.3:      # pyramid / hip with a point: ridge along the longer side
        axis = 'X' if (x1 - x0) >= (y1 - y0) else 'Y'
    else:
        axis = 'X' if ex > ey else 'Y'
    # Verifier fix (2026-11-19): the top-points test above picked the wrong axis for the second wing of the
    # L-shaped Age2 House_D (the ridge points of the main wing that fall inside the wing's rectangle win), so
    # the wing's overlay ran across its ridge and the host gable stuck out of all three culture roofs. Fit
    # both gable models z = a - k * |offset from the rect's centre line| and keep the clearly better one.
    if len(ins) >= 8:
        import numpy as np
        P = np.array([(p.x, p.y, p.z) for p in ins])
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        res = {}
        for ax_, off in (('X', np.abs(P[:, 1] - cy)), ('Y', np.abs(P[:, 0] - cx))):
            A = np.stack([np.ones_like(off), off], 1)
            coef, *_ = np.linalg.lstsq(A, P[:, 2], rcond=None)
            res[ax_] = (float(np.mean((A @ coef - P[:, 2]) ** 2)), float(coef[1]))
        best = min(res, key=lambda k: res[k][0])
        other = 'Y' if best == 'X' else 'X'
        if res[best][1] < -0.3 and res[best][0] < 0.5 * res[other][0]:
            axis = best
    return dict(min=[x0, y0, zmin], max=[x1, y1, zmax], eave_z=zmin, ridge_z=zmax, ridge_axis=axis,
                ridge_len=max(ex, ey))


def _largest_rect(occ):
    """Largest all-True axis rectangle in a boolean grid -> (i0, i1, j0, j1) inclusive-exclusive, area."""
    import numpy as np
    nx, ny = occ.shape
    h = np.zeros(ny, np.int32)
    best = (0, None)
    for i in range(nx):
        h = np.where(occ[i], h + 1, 0)
        st = []
        for j in range(ny + 1):
            cur = h[j] if j < ny else 0
            start = j
            while st and st[-1][1] >= cur:
                sj, sh = st.pop()
                area = sh * (j - sj)
                if area > best[0]:
                    best = (area, (i - sh + 1, i + 1, sj, j))
                start = sj
            st.append((start, cur))
    return best


def _roof_rects(isl):
    """Main roof as 1-2 rectangles (L-shaped blocks get two). Small high islands (towers, dormers, chimney
    caps) are left out so they may rise through the culture roof."""
    import numpy as np
    areas = [sum(f.calc_area() for f in fs) for fs in isl]
    tot = sum(areas)
    main = [fs for fs, a in zip(isl, areas) if a >= 0.15 * tot] or [isl[int(np.argmax(areas))]]
    vs = [v.co.copy() for fs in main for f in fs for v in f.verts]
    top = max(p.z for p in vs)
    # ridge caps, dormers and porches below the main ridge count for the heights (towers above it do not)
    vz = vs + [v.co.copy() for fs in isl if not any(fs is m for m in main) for f in fs for v in f.verts
               if max(vv.co.z for ff in fs for vv in ff.verts) <= top + 0.8]
    x0, x1 = min(p.x for p in vs), max(p.x for p in vs)
    y0, y1 = min(p.y for p in vs), max(p.y for p in vs)
    cell = 0.5
    nx, ny = max(1, int(np.ceil((x1 - x0) / cell))), max(1, int(np.ceil((y1 - y0) / cell)))
    occ = np.zeros((nx, ny), bool)
    for fs in main:
        for f in fs:
            vv = [v.co for v in f.verts]
            for k in range(1, len(vv) - 1):
                a, b, c = vv[0], vv[k], vv[k + 1]
                n = 2 + int(max((b - a).length, (c - a).length, (c - b).length) / 0.2)
                for u in np.linspace(0, 1, n):
                    for w in np.linspace(0, 1 - u, max(2, int(n * (1 - u)))):
                        p = a + (b - a) * u + (c - a) * w
                        i = min(nx - 1, int((p.x - x0) / cell))
                        j = min(ny - 1, int((p.y - y0) / cell))
                        occ[i, j] = True
    fill = occ.mean()
    # Verifier fix (2026-11-19): small roof islands hugging the main roof (the Age2 straw fringe along the
    # eaves, verge boards) hang lower and further out than the main thatch; they showed as a red line under
    # every A attic and B saddle. Grow each rectangle over such pieces (within 0.6 m) and drop its eave to
    # them. Porches and lean-tos further away are left alone.
    extra = [v.co.copy() for fs in isl if not any(fs is m for m in main) for f in fs for v in f.verts]

    def grow(r):
        ext = [p for p in extra if r['min'][0] - 0.6 <= p.x <= r['max'][0] + 0.6
               and r['min'][1] - 0.6 <= p.y <= r['max'][1] + 0.6 and p.z <= r['ridge_z'] + 0.1]
        if ext:
            r['min'][0] = min(r['min'][0], min(p.x for p in ext))
            r['max'][0] = max(r['max'][0], max(p.x for p in ext))
            r['min'][1] = min(r['min'][1], min(p.y for p in ext))
            r['max'][1] = max(r['max'][1], max(p.y for p in ext))
            r['eave_z'] = r['min'][2] = min(r['eave_z'], min(p.z for p in ext))
        return r
    # dense samples (~0.25 m) over every roof face, for the cross-ridge envelope below
    samp = []
    for fs in isl:
        if not any(fs is m for m in main) and max(v.co.z for f in fs for v in f.verts) > top + 0.1:
            continue        # towers / belvederes above the main ridge rise through on purpose
        for f in fs:
            vv = [v.co for v in f.verts]
            for k in range(1, len(vv) - 1):
                a, b, c = vv[0], vv[k], vv[k + 1]
                n = 1 + int(max((b - a).length, (c - a).length, (c - b).length) / 0.25)
                for i in range(n + 1):
                    for j in range(n + 1 - i):
                        samp.append(a + (b - a) * (i / n) + (c - a) * (j / n))

    def prof(r, others=()):
        """Verifier addition: the host roof's upper envelope across the ridge, profile[k] = highest roof point
        at distance (k / 20) * half-width from the rect's centre line (either side). Hosts are not symmetric
        (House_D's back eave sits 0.7 m above its front fringe), so overlays clear this, not a straight gable."""
        ax = 1 if r['ridge_axis'] == 'X' else 0      # the cross-ridge coordinate
        c = (r['min'][ax] + r['max'][ax]) / 2
        hw = (r['max'][ax] - r['min'][ax]) / 2
        pts = [p for p in samp if r['min'][0] - 0.05 <= p.x <= r['max'][0] + 0.05
               and r['min'][1] - 0.05 <= p.y <= r['max'][1] + 0.05 and p.z <= r['ridge_z'] + 0.1
               and not any(o['min'][0] <= p.x <= o['max'][0] and o['min'][1] <= p.y <= o['max'][1] for o in others)]
        pr = [r['eave_z']] * 21
        for p in pts:
            k = min(20, int(round(abs(p[ax] - c) / max(hw, 1e-3) * 20)))
            pr[k] = max(pr[k], p.z)
        for k in range(19, -1, -1):          # monotone toward the ridge
            pr[k] = max(pr[k], pr[k + 1])
        r['profile'] = [round(z, 3) for z in pr]
        return r
    if fill >= 0.9:
        return [prof(grow(_rect_info(vs, x0, x1, y0, y1, vz)))]
    rects = []
    left = occ.copy()
    for _ in range(2):
        area, r = _largest_rect(left)
        if not r or area < 0.2 * occ.sum():
            break
        i0, i1, j0, j1 = r
        rects.append(_rect_info(vs, x0 + i0 * cell, min(x1, x0 + i1 * cell), y0 + j0 * cell, min(y1, y0 + j1 * cell), vz))
        left[i0:i1, j0:j1] = False
    if len(rects) == 2:
        # Verifier fix (2026-11-19): the second wing's ridge runs on into the first wing as a valley; that
        # stretch lay outside both rectangles and showed through the culture roofs at the junction. Extend
        # the wing along its ridge to where the host's ridge line really ends.
        r2 = rects[1]
        ax = 1 if r2['ridge_axis'] == 'Y' else 0
        cr = 1 - ax
        cc = (r2['min'][cr] + r2['max'][cr]) / 2
        zr2 = r2['ridge_z']
        along = sorted(p[ax] for p in vs if abs(p[cr] - cc) < 0.6 and zr2 - 0.6 < p.z <= zr2 + 0.1)
        c1 = (rects[0]['min'][ax] + rects[0]['max'][ax]) / 2
        if c1 < r2['min'][ax]:      # walk from the wing's edge toward the first wing while the ridge continues
            e = r2['min'][ax]
            for q in reversed([q for q in along if q < e]):
                if e - q > 0.8:
                    break
                e = q
            r2['min'][ax] = e
        else:
            e = r2['max'][ax]
            for q in [q for q in along if q > e]:
                if q - e > 0.8:
                    break
                e = q
            r2['max'][ax] = e
    rects = [grow(r) for r in rects]
    return [prof(r, [o for o in rects if o is not r]) for r in rects] or [prof(grow(_rect_info(vs, x0, x1, y0, y1, vz)))]


def _measure(ob, roofnames):
    me = ob.data
    M = ob.matrix_world
    mats = [m.name if m else '' for m in me.materials]

    def is_roof(mi):
        n = mats[mi] if mi < len(mats) else ''
        base = n.split('.')[0]
        return base in roofnames or base.endswith('Roof') or 'Roof' in base
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.transform(M)
    bm.faces.ensure_lookup_table()
    roof_faces = [f for f in bm.faces if is_roof(f.material_index)]
    res = {'roof_by': 'material'}
    if not roof_faces:
        # the FBX lost its per-face slots (seen on the Age2 exports, 2026-11-19): find the roof by shape -
        # pitched faces well above the ground
        res['roof_by'] = 'geometry (FBX had no per-face material slots)'
        roof_faces = [f for f in bm.faces if 0.25 < f.normal.z < 0.97 and f.calc_center_median().z > 1.6
                      and f.calc_area() > 0.02]
    # islands of roof faces
    seen = set()
    isl = []
    rset = set(f.index for f in roof_faces)
    for f in roof_faces:
        if f.index in seen:
            continue
        st = [f]
        seen.add(f.index)
        cur = []
        while st:
            x = st.pop()
            cur.append(x)
            for v in x.verts:
                for y in v.link_faces:
                    if y.index in rset and y.index not in seen:
                        seen.add(y.index)
                        st.append(y)
        isl.append(cur)
    if isl:
        res['roofs'] = _roof_rects(isl)
        res['roof'] = res['roofs'][0]
    # walls: structure faces between 0.3 m and the eave
    ez = res.get('roof', {}).get('eave_z', 99.0)
    rf = set(f.index for f in roof_faces)
    wv = [v.co for f in bm.faces if f.index not in rf for v in f.verts if 0.3 < v.co.z < ez - 0.1]
    if wv:
        res['walls'] = dict(min=[min(p.x for p in wv), min(p.y for p in wv), min(p.z for p in wv)],
                            max=[max(p.x for p in wv), max(p.y for p in wv), max(p.z for p in wv)])
    allv = [v.co for v in bm.verts]
    res['bounds'] = dict(min=[min(p.x for p in allv), min(p.y for p in allv), min(p.z for p in allv)],
                         max=[max(p.x for p in allv), max(p.y for p in allv), max(p.z for p in allv)])
    bm.free()
    return res


def analyse(root, cache_path, log=print, force=False):
    hosts = list_hosts(root)
    stamp = {fam: (_manifest(root, fam)[1] or 0) for fam in FAMS}
    if os.path.exists(cache_path) and not force:
        try:
            with open(cache_path, encoding='utf-8') as f:
                c = json.load(f)
            if c.get('stamp') == stamp and len(c.get('hosts', [])) == len(hosts):
                return c['hosts']
        except Exception:
            pass
    out = []
    for fam, asset, name, fbx, e, roofnames, maps_to in hosts:
        if not os.path.exists(fbx):
            log('host fbx missing', fbx)
            continue
        before = set(bpy.data.objects)
        try:
            bpy.ops.import_scene.fbx(filepath=fbx)
        except Exception as ex:
            log('host import failed', fbx, ex)
            continue
        new = [o for o in bpy.data.objects if o not in before]
        meshes = [o for o in new if o.type == 'MESH']
        lod0 = [o for o in meshes if o.name.split('.')[0].endswith('LOD0')] or meshes[:1]
        if not lod0:
            continue
        bpy.context.view_layer.update()
        r = _measure(lod0[0], roofnames)
        for o in new:
            d = o.data if o.type == 'MESH' else None
            bpy.data.objects.remove(o, do_unlink=True)
            if d is not None and d.users == 0:
                bpy.data.meshes.remove(d)
        if 'roof' not in r:
            log('host has no roof faces:', name)
            continue
        h = dict(id='%s%s' % (fam.capitalize(), asset.replace('_', '')), family=fam.capitalize(), age=int(fam[-1]),
                 asset=asset, mesh=name, fbx=os.path.relpath(fbx, root).replace('\\', '/'), maps_to=maps_to, **r)
        out.append(h)
        rf = r['roof']
        log('host %-26s roof %.1f x %.1f eave %.2f ridge %.2f axis %s' % (
            name, rf['max'][0] - rf['min'][0], rf['max'][1] - rf['min'][1], rf['eave_z'], rf['ridge_z'], rf['ridge_axis']))
    with open(cache_path, 'w', encoding='utf-8') as f:
        json.dump(dict(stamp=stamp, hosts=out), f, indent=1)
    return out
