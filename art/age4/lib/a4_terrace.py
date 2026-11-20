# DEMIURGE art - Age4 brick terraces (4 variants). Front (street, doors) toward -Y, ridge along X.
# The plot includes the front step zone, the main block, rear outshots and walled back yards with privies.
import math
import random

from mathutils import Vector

import a4geo as G
from a4geo import S, R, D, Opening, v3

PLOT_FRONT = 0.7     # front step zone
YARD = 5.6           # rear yard depth behind the outshots line

VARIANTS = {
    'A': dict(units=4, uw=5.0, depth=8.0, storeys=2, sh=3.0, pitch=35, win='sash6', door='door_green', bays=False,
              parapet=False, gable_parapet=True, outshot_storeys=1, shop=False, string=False, seed=101),
    'B': dict(units=5, uw=5.2, depth=8.2, storeys=2, sh=3.05, pitch=33, win='sash2', door='door_red', bays=True,
              parapet=False, gable_parapet=False, outshot_storeys=1, shop=False, string=True, seed=202),
    'C': dict(units=6, uw=4.8, depth=8.6, storeys=3, sh=2.95, pitch=30, win='sash6', door='door_green', bays=False,
              parapet=False, gable_parapet=True, outshot_storeys=2, shop=False, string=False, seed=303),
    'D': dict(units=4, uw=5.6, depth=9.0, storeys=3, sh=3.2, pitch=16, win='sash2', door='door_red', bays=False,
              parapet=True, gable_parapet=False, outshot_storeys=2, shop=True, string=True, seed=404),
}


def plot_dims(v):
    p = VARIANTS[v]
    W = p['units'] * p['uw']
    Dp = PLOT_FRONT + p['depth'] + YARD
    return W, Dp


def terrace(variant, lod):
    p = VARIANTS[variant]
    rng = random.Random(p['seed'])
    asm = G.Asm()
    U, uw, dep, ns, sh = p['units'], p['uw'], p['depth'], p['storeys'], p['sh']
    W, Dp = plot_dims(variant)
    x0, x1 = -W / 2, W / 2
    yf = -Dp / 2 + PLOT_FRONT          # front facade
    yr = yf + dep                       # rear facade
    yend = Dp / 2                       # end of the yards
    floor = 0.15
    ze = floor + ns * sh + 0.1          # eave
    zb = -0.3
    # ---------------------------------------------------------------- openings
    front, rear = [], []
    door_u = []
    for k in range(U):
        xa = k * uw
        mirror = k % 2 == 1
        shop_unit = p['shop'] and k == 0
        if shop_unit:
            dpos = xa + uw - 1.45
            front.append(Opening(dpos, dpos + 1.0, floor, floor + 2.45, p['door'], rnd=rng.random(), accent=1.0, sill=False))
            front.append(Opening(xa + 0.55, xa + uw - 1.95, floor + 0.45, floor + 2.75, 'shop', rnd=rng.random(), sill=False, lintel='timber'))
            wpos = [xa + 0.8, xa + uw - 2.0]
        else:
            dpos = xa + (uw - 1.5 if mirror else 0.5)
            wx = xa + (0.6 if mirror else uw - 1.95)
            front.append(Opening(dpos, dpos + 1.0, floor, floor + 2.4, p['door'], rnd=rng.random(), accent=1.0, sill=False))
            if not p['bays']:
                front.append(Opening(wx, wx + 1.35, floor + 0.9, floor + 2.4, p['win'], rnd=rng.random()))
            wpos = [dpos, wx + 0.15] if not mirror else [wx + 0.15, dpos]
        door_u.append(dpos)
        for s in range(1, ns):
            zf = floor + s * sh
            hgt = 1.6 if s == 1 else 1.4
            for wp in wpos:
                front.append(Opening(wp, wp + 1.05, zf + 0.75, zf + 0.75 + hgt, p['win'], rnd=rng.random()))
        # rear: outshot on the door side (paired with the neighbour)
        out_right = k % 2 == 0          # outshots pair up across the shared party wall
        ox0 = xa + uw - 2.5 if out_right else xa
        rwin = xa + (0.5 if out_right else uw - 1.6)
        rear.append(Opening(rwin, rwin + 1.0, floor + 0.0, floor + 2.3, 'door_green', rnd=rng.random(), accent=1.0, sill=False))
        for s in range(1, ns):
            zf = floor + s * sh
            rear.append(Opening(rwin, rwin + 1.0, zf + 0.8, zf + 2.2, p['win'], rnd=rng.random()))
            # small window over the outshot roof on upper floors
            if s >= p['outshot_storeys']:
                xo = ox0 + 0.8
                rear.append(Opening(xo, xo + 0.8, zf + 1.05, zf + 2.1, 'sash2', rnd=rng.random()))
    # convert rear (N side runs from x1 to x0)
    rear_n = []
    for o in rear:
        rear_n.append(Opening(W - (o.u1), W - (o.u0), o.z0, o.z1, o.tile, rnd=o.rnd, accent=o.accent, sill=o.sill and U < 6, lintel=None))
    # gable-end windows on end units of variant D (corner shop side)
    side_w = []
    if p['shop']:
        for s in range(1, ns):
            zf = floor + s * sh
            side_w.append(Opening(dep * 0.5 - 0.5, dep * 0.5 + 0.5, zf + 0.8, zf + 2.3, p['win'], rnd=rng.random()))
    ops = {'S': front, 'N': rear_n, 'W': side_w}
    # ---------------------------------------------------------------- main mass
    if lod <= 2:
        wall, dress, decals = G.body(x0, x1, yf, yr, zb, ze, ops, lod)
        asm.add(wall)
        asm.add(dress)
        asm.add(decals)
    else:
        wall = G.Part('solid', ('body',))
        G.block(wall, x0, x1, yf, yr, zb, ze)
        asm.add(wall)
    if lod <= 1:
        asm.add(G.band_ring(x0, x1, yf, yr, zb, floor + 0.35, 0.035, 'stone', ('plinth',)))
    if lod == 0 and p['string']:
        for s in range(1, ns):
            zf = floor + s * sh
            asm.add(G.band_ring(x0, x1, yf, yr, zf - 0.02, zf + 0.14, 0.03, 'stone'))
    # ---------------------------------------------------------------- roof
    if p['parapet']:
        par_h = 0.95
        pw = 0.3
        if lod <= 2:
            pp = G.Part('solid', ('parapet',))
            for (a, b, c, d) in ((x0, x1, yf, yf + pw), (x0, x1, yr - pw, yr), (x0, x0 + pw, yf + pw, yr - pw), (x1 - pw, x1, yf + pw, yr - pw)):
                G.block(pp, a, b, c, d, ze, ze + par_h)
            asm.add(pp)
        if lod <= 1:
            cop = G.Part('detail', ('coping',), 'stone')
            G.prism(cop, [(x0 - 0.05, yf - 0.05), (x1 + 0.05, yf - 0.05), (x1 + 0.05, yf + pw + 0.05), (x0 - 0.05, yf + pw + 0.05)], ze + par_h, ze + par_h + 0.12, D, band='stone')
            asm.add(cop)
        if lod == 0:
            asm.add(G.band_ring(x0, x1, yf, yr, ze - 0.45, ze - 0.12, 0.12, 'stone', ('cornice',)))
        roof, zr = G.gable_roof(x0 + pw, x1 - pw, yf + pw, yr - pw, ze + 0.2, p['pitch'], ox=0.0, oy=0.0, fascia=0.3, hip=True)
        asm.add(roof)
    else:
        roof, zr = G.gable_roof(x0, x1, yf, yr, ze, p['pitch'], ox=0.04, oy=0.32, fascia=0.22)
        asm.add(roof)
    asm.info['eave'] = ze
    asm.info['ridge'] = zr
    yc = (yf + yr) * 0.5
    # gable parapets with stone coping (end walls rise above the slates)
    if p['gable_parapet'] and lod <= 1:
        for xe in (x0, x1 - 0.32):
            gp = G.Part('solid', ('roof', 'gableparapet'))
            prof = [(yf - 0.05, ze - 0.1), (yr + 0.05, ze - 0.1), (yc, zr + 0.28)]
            G.extrude_profile(gp, prof, lambda a, z, s: v3(s, a, z), xe, xe + 0.32, [S, S, S], S)
            asm.add(gp)
            if lod == 0:
                cp = G.Part('detail', ('roof', 'coping'), 'stone')
                for (ya, yb) in ((yf - 0.05, yc), (yr + 0.05, yc)):
                    G.beam(cp, (xe + 0.16, ya, ze + 0.25), (xe + 0.16, yb, zr + 0.36), 0.42, 0.12, D, 'stone')
                asm.add(cp)
            asm.sockets.append(('Ornament_Gable_%d' % (0 if xe == x0 else 1), v3(xe + 0.16, yc, zr + 0.42), 'ornament'))
    # ---------------------------------------------------------------- chimney stacks on party walls (and ends)
    zst0 = ze + (0.4 if p['parapet'] else 0.0)
    ztop = zr + (1.0 if not p['parapet'] else 1.6)
    sx = [x0 + 0.35] + [x0 + k * uw for k in range(1, U)] + [x1 - 0.35]
    if U >= 6:
        sx = [x0 + 0.35] + [x0 + k * uw for k in range(2, U, 2)] + [x1 - 0.35]
    for i, x in enumerate(sx):
        if lod == 3 and 0 < i < len(sx) - 1 and i % 2:
            continue
        parts, tops = G.chimney_stack(x, yc + (0.0 if not p['parapet'] else 0.0), zst0, ztop, 0.6, 1.25 if 0 < i < len(sx) - 1 else 0.9,
                                      (4 if U < 6 else 3) if 0 < i < len(sx) - 1 else 2, lod)
        asm.add(parts)
        for t in tops:
            asm.sockets.append(('Smoke_%d' % len([s for s in asm.sockets if s[2] == 'smoke']), t, 'smoke'))
        asm.sockets.append(('Ornament_Chimney_%d' % i, v3(x, yc, ztop + 0.14), 'ornament'))
    # ---------------------------------------------------------------- bay windows (variant B)
    if p['bays'] and lod <= 2:
        for k in range(U):
            xa = x0 + k * uw
            mirror = k % 2 == 1
            cx = xa + (1.25 if mirror else uw - 1.35)
            asm.add(bay_window(cx, yf, floor, lod, rng.random(), p['win']))
    # ---------------------------------------------------------------- rear outshots, yards, privies
    os_ = p['outshot_storeys']
    oz = floor + os_ * sh - 0.2
    for k in range(U):
        xa = x0 + k * uw
        out_right = k % 2 == 0
        ox0 = xa + uw - 2.5 if out_right else xa
        ox1 = ox0 + 2.5
        oy1 = yr + 3.6
        if lod <= 2:
            o_ops = {'W' if out_right else 'E': [Opening(1.2, 2.1, floor + 0.0, floor + 2.2, 'door_green', rnd=rng.random(), accent=1.0, sill=False)],
                     'N': [Opening(0.8, 1.7, floor + 0.95, floor + 2.1, 'sash2', rnd=rng.random())]}
            if os_ == 2:
                o_ops['N'].append(Opening(0.8, 1.7, floor + sh + 0.8, floor + sh + 2.0, 'sash2', rnd=rng.random()))
            ow, od, ode = G.body(ox0 + 0.02, ox1 - 0.02, yr - 0.05, oy1, zb, oz, o_ops, lod, tags=('body', 'outshot'))
            asm.add(ow)
            asm.add(od)
            asm.add(ode)
            asm.add(G.lean_roof(ox0 + 0.02, ox1 - 0.02, yr - 0.05, oy1, oz, oz + 1.1, 'N', ox=0.08, oy=0.18))
        else:
            ow = G.Part('solid', ('body', 'outshot'))
            G.block(ow, ox0, ox1, yr - 0.05, oy1, zb, oz + 0.5)
            asm.add(ow)
    if lod <= 2:
        yw = G.Part('solid', ('yard',))
        t = 0.23
        hw = 1.9
        for k in range(U + 1):
            x = x0 + k * uw
            xa, xb = (x, x + t) if k == 0 else ((x - t, x) if k == U else (x - t / 2, x + t / 2))
            if lod <= 1 or k in (0, U):
                G.block(yw, xa, xb, yr, yend - t, zb, hw)
        # back wall with a gate per unit
        if lod <= 1:
            edges = [x0] + [e for k in range(U) for e in (x0 + k * uw + uw * 0.5 - 0.5, x0 + k * uw + uw * 0.5 + 0.5)] + [x1]
            for k in range(0, len(edges), 2):
                G.block(yw, edges[k], edges[k + 1], yend - t, yend, zb, hw)
        else:
            G.block(yw, x0, x1, yend - t, yend, zb, hw)
        asm.add(yw)
        if lod == 0:
            cop = G.Part('detail', ('coping',), 'lead')
            G.block(cop, x0 - 0.03, x1 + 0.03, yend - t - 0.04, yend + 0.04, hw, hw + 0.08, D, band='lead')
            asm.add(cop)
            # front steps
            st = G.Part('detail', ('steps',), 'stone')
            for k in range(U):
                du = door_u[k]
                G.block(st, x0 + du - 0.1, x0 + du + 1.1, yf - 0.45, yf, zb, floor, D, band='stone')
            asm.add(st)
        if lod <= 1:
            # privies / wash-houses at the end of each yard
            for k in range(U):
                xa = x0 + k * uw
                mirror = k % 2 == 1
                px0 = xa + (0.25 if not mirror else uw - 1.65)
                pv = G.Part('solid', ('body', 'privy'))
                G.block(pv, px0, px0 + 1.4, yend - t - 1.5, yend - t, zb, 2.1)
                asm.add(pv)
                asm.add(G.lean_roof(px0, px0 + 1.4, yend - t - 1.5, yend - t, 2.1, 2.5, 'S', ox=0.06, oy=0.12, fascia=0.1))
    asm.info['n_holes'] = 2 + U // 2
    asm.info['con_frac'] = 0.6
    # Damaged: place the holes on the roof slopes and one in the upper street facade. The generic placement
    # drew from the whole plot box (yards included, z near the chimney tops), so most blobs missed the
    # house and Terrace_A showed a single hole on the rear (verifier fix, 2026-11-19).
    drng = random.Random(p['seed'] + 7)
    tanp = math.tan(math.radians(p['pitch']))
    half = (yr - yf) * 0.5
    blobs = []
    nh = asm.info['n_holes']
    for k in range(nh):
        x = x0 + W * (k + 0.5 + drng.uniform(-0.25, 0.25)) / nh
        side = -1 if k % 2 == 0 else 1
        dy = side * half * drng.uniform(0.3, 0.6)
        z = zr - abs(dy) * tanp
        blobs.append(((x, yc + dy, z), drng.uniform(1.7, 2.4), p['seed'] + 50 + k, 1.4))
    xf = x0 + W * drng.uniform(0.3, 0.7)
    blobs.append(((xf, yf, ze - 1.3), 1.5, p['seed'] + 70, 1.0))
    asm.info['blobs'] = blobs
    return asm


def bay_window(cx, yf, floor, lod, rnd, win):
    """Canted single-storey bay: 2.1 m wide at the wall, 1.3 m at the front, 0.65 m deep."""
    part = G.Part('solid', ('bay', 'body'))
    w0, w1, dpt = 1.05, 0.65, 0.65
    z0, zs, zh, zt = -0.3, floor + 0.85, floor + 2.35, floor + 2.75
    # plan polygon CCW from above: wall-left, front-left, front-right, wall-right (y decreases to the front)
    plan = [(cx + w0, yf), (cx - w0, yf), (cx - w1, yf - dpt), (cx + w1, yf - dpt)]
    plan = plan[::-1]   # CCW
    part.begin()
    k = len(plan)
    for i in range(k):
        a, b = plan[i], plan[(i + 1) % k]
        A = lambda z, p=a: v3(p[0], p[1], z)   # noqa: E731
        B = lambda z, p=b: v3(p[0], p[1], z)   # noqa: E731
        is_wall = abs(a[1] - yf) < 1e-6 and abs(b[1] - yf) < 1e-6
        if lod >= 2:
            part.add([A(z0), B(z0), B(zt), A(zt)], S)
            continue
        if is_wall:
            for (za_, zb_) in ((z0, zs), (zs, zh), (zh, zt)):
                part.add([A(za_), B(za_), B(zb_), A(zb_)], S)
            continue
        part.add([A(z0), B(z0), B(zs), A(zs)], S)
        q = [A(zs), B(zs), B(zh), A(zh)]
        L = (Vector(b) - Vector(a)).length
        dirv = (v3(b[0], b[1], 0) - v3(a[0], a[1], 0)).normalized()
        part.add(q, D, G.tile_uv(q, win, A(zs), dirv, (0, 0, 1), L, zh - zs), (rnd, 0, 0, 1))
        part.add([A(zh), B(zh), B(zt), A(zt)], S)
    part.add([v3(p[0], p[1], zt) for p in plan], S)
    part.add([v3(p[0], p[1], z0) for p in plan][::-1], S)
    part.end_solid()
    roof = G.Part('solid', ('roof',), 'lead')
    # small lead-and-slate hipped cap
    pl2 = [(cx + w0 + 0.08, yf + 0.02), (cx - w0 - 0.08, yf + 0.02), (cx - w1 - 0.08, yf - dpt - 0.1), (cx + w1 + 0.08, yf - dpt - 0.1)][::-1]
    roof.begin()
    base = [v3(p[0], p[1], zt) for p in pl2]
    top = [v3(cx + (p[0] - cx) * 0.75, yf + 0.02, zt + 0.5) if abs(p[1] - (yf + 0.02)) < 1e-6 else v3(cx + (p[0] - cx) * 0.75, yf - 0.1, zt + 0.5) for p in pl2]
    roof.add(base[::-1], S)
    for i in range(4):
        j = (i + 1) % 4
        roof.add([base[i], base[j], top[j], top[i]], R)
    roof.add(top, R)
    roof.end_solid()
    parts = [part, roof]
    return parts
