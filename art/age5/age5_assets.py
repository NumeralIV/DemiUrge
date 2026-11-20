# DEMIURGE art/age5 - asset builders for the Atomic Age kit.
#
# Every builder is f(mb, P, S, lod) -> list of grids/cylinders (for state dressing). mb: MeshBuilder,
# P: parameter dict (storeys, ...), S: State or None (= intact), lod: 0..3.
# Front faces -Y, pivot at the centre of the footprint on the ground, metres.
import math

from age5_geo import (STRUCT, ROOF, DETAIL, UREP, Grid, CylGrid, MeshBuilder, Rng, State, beam, box, clamp, dome,
                      frustum, hash01, hquad, quad_auto, rows_uv, rubble, shell, strip_v, wall_quad, disc, roof_uv,
                      rows_v)

GREY = (0.0, 0.48, 12.0)      # steel row: grey half
YELLOW = (0.52, 1.0, 12.0)    # steel row: yellow half


def keep(S, p, jitter=0.0):
    return S is None or S.kind == 'Intact' or not S.removed(p, jitter)


def plinth(mb, x0, y0, x1, y1, pad=0.25, top=0.15):
    box(mb, ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (top - 0.3) * 0.5), ((x1 - x0) * 0.5 + pad, (y1 - y0) * 0.5 + pad, (top + 0.3) * 0.5),
        ('rows', 'concrete'))


def tiled_plate(mb, x0, y0, x1, y1, z, row, thick=0.0):
    """Horizontal plate split so the detail row never stretches in V (24 m x span pieces)."""
    _, _, span = rows_v(row)
    xs = [x0]
    while xs[-1] < x1 - 1e-6:
        xs.append(min(x1, xs[-1] + UREP))
    ys = [y0]
    while ys[-1] < y1 - 1e-6:
        ys.append(min(y1, ys[-1] + span))
    for i in range(len(xs) - 1):
        for j in range(len(ys) - 1):
            hquad(mb, xs[i], ys[j], xs[i + 1], ys[j + 1], z, ('rows', row))
    if thick > 0:
        for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
            wall_quad(mb, a, b, z - thick, z, ('rows', 'concrete', 0.0, z - thick, thick, None))


def parapets(mb, g, S, h=0.6, t=0.25, mapping=('rows', 'concrete'), lod=0):
    """Parapet boxes on every exterior roof edge of a grid. Merged into runs when intact, per bay otherwise
    (so damage cuts them). Skipped at LOD2+ (invisible at that screen size)."""
    if lod >= 2:
        return
    sol = g.solid_map(S)
    intact = S is None or S.kind == 'Intact'
    runs = {}
    for (ix, iy, iz), s in sol.items():
        if not s:
            continue
        if iz < g.nz - 1 and g.occ(ix, iy, iz + 1):
            continue
        cx, cy, _ = g.centre(ix, iy, iz)
        z = g.zs[iz + 1]
        for side, (dx, dy) in (('S', (0, -1)), ('E', (1, 0)), ('N', (0, 1)), ('W', (-1, 0))):
            n = (ix + dx, iy + dy, iz)
            if g.inside(*n) and g.occ(*n):
                continue
            if dx:
                key = (side, iz, ix)
                runs.setdefault(key, []).append((iy, cx + dx * (g.bx * 0.5 - t * 0.5), z))
            else:
                key = (side, iz, iy)
                runs.setdefault(key, []).append((ix, cy + dy * (g.by * 0.5 - t * 0.5), z))
    for (side, iz, line_), cells in runs.items():
        cells.sort()
        groups = []
        for c in cells:
            if intact and groups and groups[-1][-1][0] == c[0] - 1:
                groups[-1].append(c)
            else:
                groups.append([c])
        for gr in groups:
            i0, i1 = gr[0][0], gr[-1][0]
            fixed, z = gr[0][1], gr[0][2]
            if side in 'EW':
                y0 = g.y0 + i0 * g.by
                y1 = g.y0 + (i1 + 1) * g.by
                box(mb, (fixed, (y0 + y1) * 0.5, z + h * 0.5), (t * 0.5, (y1 - y0) * 0.5, h * 0.5), mapping)
            else:
                x0 = g.x0 + i0 * g.bx
                x1 = g.x0 + (i1 + 1) * g.bx
                box(mb, ((x0 + x1) * 0.5, fixed, z + h * 0.5), ((x1 - x0) * 0.5, t * 0.5, h * 0.5), mapping)


def crane(mb, x, y, top, jib_len=40.0, yaw=math.pi, lod=0):
    """Tower crane (yellow) standing at (x, y), jib height top."""
    w = 1.8
    box(mb, (x, y, top * 0.5), (w * 0.5, w * 0.5, top * 0.5), ('rows', 'steel'), ur=YELLOW)
    box(mb, (x, y, -0.1), (3.0, 3.0, 0.4), ('rows', 'concrete'))
    c, s = math.cos(yaw), math.sin(yaw)
    p0 = (x, y, top + 1.0)
    p1 = (x + c * jib_len, y + s * jib_len, top + 1.0)
    p2 = (x - c * jib_len * 0.3, y - s * jib_len * 0.3, top + 1.0)
    beam(mb, p0, p1, 1.4, ur=YELLOW, h=1.8)
    beam(mb, p0, p2, 1.4, ur=YELLOW, h=1.4)
    box(mb, (x - c * jib_len * 0.27, y - s * jib_len * 0.27, top - 0.6), (2.0, 2.0, 1.6), ('rows', 'concrete'), yaw=yaw)
    box(mb, (x + c * 1.8, y + s * 1.8, top - 1.0), (1.2, 1.2, 1.2), ('rows', 'steel'), yaw=yaw, ur=YELLOW)
    if lod <= 1:
        apex = (x, y, top + 7.0)
        beam(mb, (x, y, top + 1.8), apex, 0.8, ur=YELLOW)
        beam(mb, apex, (x + c * jib_len * 0.6, y + s * jib_len * 0.6, top + 1.9), 0.15, ur=GREY)
        beam(mb, apex, (x - c * jib_len * 0.28, y - s * jib_len * 0.28, top + 1.9), 0.15, ur=GREY)
        hx = x + c * jib_len * 0.55
        hy = y + s * jib_len * 0.55
        beam(mb, (hx, hy, top), (hx, hy, top * 0.55), 0.12, ur=GREY)
        box(mb, (hx, hy, top * 0.55 - 0.5), (0.5, 0.5, 0.5), ('rows', 'steel'), ur=YELLOW)


def frame_storeys(mb, g, S, lod, n=3):
    """Construction: bare concrete frame (columns + slabs) for n storeys above the finished part of a grid."""
    iz_c = None
    for iz in range(g.nz):
        if g.centre(0, 0, iz)[2] > S.cut_z:
            iz_c = iz
            break
    if iz_c is None:
        return
    for iz in range(iz_c, min(g.nz, iz_c + n)):
        cells = [(ix, iy) for ix in range(g.nx) for iy in range(g.ny) if g.occ(ix, iy, iz)]
        if not cells:
            continue
        z0, z1 = g.zs[iz], g.zs[iz + 1]
        last = iz == min(g.nz, iz_c + n) - 1
        # slab per row of cells (keeps setbacks)
        for ix, iy in cells:
            x0 = g.x0 + ix * g.bx
            y0 = g.y0 + iy * g.by
            if not last or hash01(g.seed, ix, iy, 77) < 0.6:
                box(mb, (x0 + g.bx * 0.5, y0 + g.by * 0.5, z1 - 0.15), (g.bx * 0.5, g.by * 0.5, 0.15), ('rows', 'concrete'),
                    skip_bottom=False)
        step = 1 if lod == 0 else 2
        if lod >= 2:
            continue
        cs = set(cells)
        for ix in range(0, g.nx + 1, step):
            for iy in range(0, g.ny + 1, step):
                if not any(((ix - a, iy - b) in cs) for a in (0, 1) for b in (0, 1)):
                    continue
                x = g.x0 + ix * g.bx
                y = g.y0 + iy * g.by
                hz = (z1 - z0 - 0.3) * 0.5
                box(mb, (x, y, z0 + hz), (0.25, 0.25, hz), ('rows', 'concrete'))


def cyl_frame(mb, cg, S, lod, n=2):
    iz_c = None
    for iz in range(cg.nz):
        if (cg.zs[iz] + cg.zs[iz + 1]) * 0.5 > S.cut_z:
            iz_c = iz
            break
    if iz_c is None:
        return
    for iz in range(iz_c, min(cg.nz, iz_c + n)):
        z0, z1 = cg.zs[iz], cg.zs[iz + 1]
        frustum(mb, cg.cx, cg.cy, z1 - 0.3, z1, cg.rad(z1), cg.rad(z1), max(8, cg.K // 2), ('rows', 'concrete', 0.3),
                cap_top=('rows', 'concrete'), smooth=False)
        if lod <= 1:
            K = cg.K // (1 if lod == 0 else 2)
            for k in range(K):
                t = 2 * math.pi * k / K
                rr = cg.rad(z0) - 0.4
                box(mb, (cg.cx + rr * math.cos(t), cg.cy + rr * math.sin(t), (z0 + z1 - 0.3) * 0.5), (0.25, 0.25, (z1 - z0 - 0.3) * 0.5),
                    ('rows', 'concrete'))


def dress_state(mb, S, lod, grids, crane_at=None):
    """Generic per-state dressing: construction frame and crane, rubble for damage and ruin."""
    if S is None or S.kind == 'Intact':
        return
    x0, y0, x1, y1, h = S.box
    if S.kind == 'Construction':
        for g in grids:
            if isinstance(g, Grid):
                frame_storeys(mb, g, S, lod)
            elif isinstance(g, CylGrid):
                cyl_frame(mb, g, S, lod)
        if lod <= 2:
            cx, cy = crane_at if crane_at else (x1 + 5.0, (y0 + y1) * 0.5)
            jib = clamp(0.75 * max(x1 - x0, y1 - y0), 25.0, 55.0)
            crane(mb, cx, cy, max(20.0, S.cut_z + 3 * 3.6 + 10.0), jib_len=jib, yaw=math.pi, lod=lod)
        if lod <= 1:
            # material stacks on the ground
            rng = Rng(S.seed + 41)
            for i in range(4):
                box(mb, (x0 + rng.u(0, 1) * (x1 - x0), y0 - 4.0 - rng.u(0, 3), 0.5), (rng.u(1, 2.5), rng.u(0.8, 1.6), 0.5),
                    ('rows', 'concrete'), yaw=rng.u(0, 3))
    elif S.kind == 'Damaged':
        if lod <= 2:
            for i, (c, r) in enumerate(S.blasts):
                gx = clamp(c[0], x0 + 2, x1 - 2)
                rubble(mb, (gx, y0 - r * 0.15, 0.0), r * 0.55 + 2.0, max(1.5, r * 0.28), S.seed + i, K=9 if lod == 0 else 6,
                       lod=lod, soot=0.5)
    elif S.kind == 'Ruin':
        if lod <= 2:
            rng = Rng(S.seed + 17)
            n = 3 if lod == 2 else 6
            w, d = x1 - x0, y1 - y0
            for i in range(n):
                side = i % 4
                t = rng.u(0.15, 0.85)
                if side == 0:
                    p = (x0 + t * w, y0 - 1.0, 0.0)
                elif side == 1:
                    p = (x1 + 1.0, y0 + t * d, 0.0)
                elif side == 2:
                    p = (x0 + t * w, y1 + 1.0, 0.0)
                else:
                    p = (x0 - 1.0, y0 + t * d, 0.0)
                r = clamp(0.18 * min(w, d) + 2.0, 3.0, 14.0) * rng.u(0.8, 1.3)
                rubble(mb, p, r, r * 0.45, S.seed + 100 + i, K=8 if lod == 0 else 6, lod=lod, soot=0.6)
            # rubble heaped inside the shell
            rubble(mb, ((x0 + x1) * 0.5, (y0 + y1) * 0.5, 0.0), 0.35 * min(w, d) + 2, clamp(0.08 * h, 2.0, 9.0), S.seed + 300,
                   K=10 if lod == 0 else 6, lod=lod, soot=0.6)


def roof_plant(mb, S, x0, y0, x1, y1, z, seed, lod, n=5):
    """Rooftop mechanical clutter: AC units, cooling fans, ducts, a hatch. LOD0 only (LOD1 keeps the big ones)."""
    if lod >= 2:
        return
    rng = Rng(seed)
    for i in range(n):
        w, d, h = rng.u(1.2, 3.2), rng.u(1.0, 2.2), rng.u(0.8, 1.8)
        c = (rng.u(x0 + w, x1 - w), rng.u(y0 + d, y1 - d), z + h * 0.5)
        if not keep(S, c) or (lod == 1 and w < 2.2):
            continue
        box(mb, c, (w * 0.5, d * 0.5, h * 0.5), ('rows', 'steel'), ur=GREY)
        if lod == 0 and rng.r() < 0.6:
            for k in (-1, 1):
                frustum(mb, c[0] + k * w * 0.25, c[1], z + h, z + h + 0.25, min(d, w) * 0.22, min(d, w) * 0.22, 8,
                        ('rows', 'steel', 0.3, GREY), cap_top=('rows', 'steel'), smooth=False)
    if lod == 0:
        for i in range(2):
            a = (rng.u(x0 + 1, x1 - 1), rng.u(y0 + 1, y1 - 1), z + 0.5)
            b = (a[0] + rng.u(-6, 6), a[1], z + 0.5)
            if keep(S, a):
                beam(mb, a, b, 0.6, ur=GREY)


def warn_light(mb, p):
    mb.socket('SOCKET_WarnLight', p)


# =========================================================================== apartment blocks


def apt_slab(mb, P, S, lod):
    n = P['storeys']
    hs = [4.2] + [3.0] * (n - 1)

    def strip(side, iz, ix, iy):
        if side in 'EW':
            return 'concrete_blank'
        return 'ground_shop' if iz == 0 else 'apt_panel'
    g = Grid(-18.0, -6.0, 12, 4, 3.0, 3.0, 0.0, hs, strip=strip, seed=11)
    core = Grid(-2.4, 6.0, 1, 1, 4.8, 2.4, 0.0, hs + [3.0], strip=lambda *a: 'concrete_blank', seed=12)
    grids = [g, core]
    if lod == 3:
        g.hull(mb, S)
        core.hull(mb, S)
        return grids
    plinth(mb, -18.0, -6.0, 18.0, 8.4)
    g.build(mb, S, lod)
    core.build(mb, S, lod)
    top = g.top
    if lod <= 1:
        for iz in range(1, n):
            zb = g.zs[iz]
            for sy, pairs in ((-1, ((1, 2), (4, 5), (7, 8), (10, 11))), (1, ((2, 3), (8, 9)))):
                yf = 6.0 + 2.4 * (sy > 0)      # back balconies sit beside the stair core line
                for p0, p1 in pairs:
                    xa, xb = -18 + 3 * p0 + 0.1, -18 + 3 * (p1 + 1) - 0.1
                    xc = (xa + xb) * 0.5
                    yc = sy * 6.7 if sy < 0 else 6.7
                    if not keep(S, (xc, yc, zb + 1.0)):
                        continue
                    if lod == 1:
                        box(mb, (xc, sy * 6.7, zb + 0.6), ((xb - xa) * 0.5, 0.7, 0.6), ('strip', 'concrete_blank'))
                        continue
                    box(mb, (xc, sy * 6.7, zb + 0.09), ((xb - xa) * 0.5, 0.7, 0.09), ('rows', 'concrete'), skip_bottom=False)
                    box(mb, (xc, sy * 7.34, zb + 0.68), ((xb - xa) * 0.5, 0.06, 0.5), ('strip', 'concrete_blank'))
                    for sx in (xa, xb):
                        box(mb, (sx, sy * 6.75, zb + 0.68), (0.05, 0.6, 0.5), ('strip', 'concrete_blank'))
        for x in (-9.0, 9.0):
            if keep(S, (x, -7.2, 3.6)):
                box(mb, (x, -7.2, 3.7), (2.6, 1.2, 0.12), ('rows', 'concrete'), skip_bottom=False)
    parapets(mb, g, S, 0.55, 0.25, lod=lod)
    for x in (-9.0, 9.0):
        if keep(S, (x, 0.0, top + 1.2)):
            box(mb, (x, 0.0, top + 1.2), (1.6, 1.6, 1.2), ('strip', 'concrete_blank'))
    if lod == 0:
        rng = Rng(P['storeys'] * 3 + 1)
        for i in range(7):
            p = (rng.u(-16, 16), rng.u(-4.5, 4.5), top)
            if keep(S, p):
                frustum(mb, p[0], p[1], top, top + rng.u(0.8, 1.6), 0.3, 0.3, 6, ('rows', 'steel', 1.5, GREY), cap_top=('rows', 'steel'),
                        smooth=False)
        for i in range(4):
            p = (rng.u(-15, 15), rng.u(-4, 4), top)
            if keep(S, p):
                beam(mb, p, (p[0], p[1], top + 3.5), 0.08)
                beam(mb, (p[0] - 0.8, p[1], top + 3.0), (p[0] + 0.8, p[1], top + 3.0), 0.06)
    if lod <= 1 and keep(S, (14.0, 2.5, top + 2)):
        frustum(mb, 14.0, 2.5, top + 1.2, top + 3.4, 1.4, 1.4, 10 if lod == 0 else 8, ('rows', 'steel', 2.2, GREY),
                cap_top=('rows', 'steel'), smooth=True)
        for dx, dy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            beam(mb, (14.0 + dx, 2.5 + dy, top), (14.0 + dx, 2.5 + dy, top + 1.2), 0.15)
    mb.socket('SOCKET_Crown', (0.0, 0.0, top))
    return grids


def apt_point(mb, P, S, lod):
    n = P['storeys']
    hs = [4.2] + [3.0] * (n - 1)

    def occ(ix, iy, iz):
        return not ((ix < 2 or ix > 4) and (iy < 2 or iy > 4))

    def strip(side, iz, ix, iy):
        return 'ground_shop' if iz == 0 else 'apt_panel'
    g = Grid(-10.5, -10.5, 7, 7, 3.0, 3.0, 0.0, hs, occ=occ, strip=strip, seed=21)
    crown = Grid(-3.0, -3.0, 2, 2, 3.0, 3.0, g.top, [3.4], strip=lambda *a: 'concrete_blank', seed=22)
    grids = [g, crown]
    if lod == 3:
        g.hull(mb, S)
        crown.hull(mb, S)
        return grids
    plinth(mb, -10.5, -10.5, 10.5, 10.5)
    g.build(mb, S, lod)
    if keep(S, (0, 0, g.top + 1.5)):
        crown.build(mb, S, lod)
    top = g.top
    if lod <= 1:
        # balconies in the four re-entrant corners: L-shaped, every storey
        for iz in range(1, n):
            zb = g.zs[iz]
            for sx in (-1, 1):
                for sy in (-1, 1):
                    cx, cy = sx * 7.5, sy * 7.5
                    if not keep(S, (cx, cy, zb + 1.0)):
                        continue
                    if lod == 1:
                        box(mb, (sx * 7.5, sy * (4.5 + 0.65), zb + 0.55), (3.0, 0.65, 0.55), ('strip', 'concrete_blank'))
                        box(mb, (sx * (4.5 + 0.65), sy * 7.5, zb + 0.55), (0.65, 3.0, 0.55), ('strip', 'concrete_blank'))
                        continue
                    # slab along the arm walls, 6 x 1.3
                    box(mb, (sx * 7.5, sy * (4.5 + 0.65), zb + 0.09), (3.0, 0.65, 0.09), ('rows', 'concrete'), skip_bottom=lod > 0)
                    box(mb, (sx * (4.5 + 0.65), sy * 7.5, zb + 0.09), (0.65, 3.0, 0.09), ('rows', 'concrete'), skip_bottom=lod > 0)
                    box(mb, (sx * 7.5, sy * (4.5 + 1.25), zb + 0.66), (3.0, 0.05, 0.48), ('strip', 'concrete_blank'))
                    box(mb, (sx * (4.5 + 1.25), sy * 7.5, zb + 0.66), (0.05, 3.0, 0.48), ('strip', 'concrete_blank'))
        if keep(S, (0, -11.5, 3.6)):
            box(mb, (0.0, -11.7, 3.8), (3.2, 1.2, 0.12), ('rows', 'concrete'), skip_bottom=False)
    parapets(mb, g, S, 0.6, 0.25, lod=lod)
    if lod <= 1 and keep(S, (6.0, 6.0, top + 2)):
        frustum(mb, 6.0, 0.0, top, top + 2.6, 1.6, 1.6, 10, ('rows', 'steel', 2.6, GREY), cap_top=('rows', 'steel'))
    if keep(S, (0, 0, top + 6)) and lod <= 2:
        beam(mb, (1.5, 1.5, crown.top), (1.5, 1.5, crown.top + 9.0), 0.35)
        warn_light(mb, (1.5, 1.5, crown.top + 9.2))
    mb.socket('SOCKET_Crown', (0.0, 0.0, crown.top))
    return grids


def apt_brick(mb, P, S, lod):
    n = P['storeys']
    hs = [3.6] + [3.0] * (n - 1)

    def strip(side, iz, ix, iy):
        if iz == 0 and side == 'S':
            return 'ground_shop'
        return 'apt_brick'
    g = Grid(-15.0, -7.5, 10, 5, 3.0, 3.0, 0.0, hs, strip=strip, seed=31)
    stair = Grid(-19.0, 3.0, 1, 1, 4.0, 6.3, 0.0, hs + [2.6], strip=lambda *a: 'concrete_blank', seed=32)
    grids = [g, stair]
    if lod == 3:
        g.hull(mb, S)
        stair.hull(mb, S)
        return grids
    plinth(mb, -19.0, -7.5, 15.0, 9.3)
    g.build(mb, S, lod)
    stair.build(mb, S, lod)
    top = g.top
    if lod <= 1:
        for iz in range(1, n):
            zb = g.zs[iz]
            # gallery walkway on the back
            for seg in range(5):
                xa, xb = -15 + 6 * seg, -15 + 6 * (seg + 1)
                if not keep(S, ((xa + xb) * 0.5, 8.4, zb + 1)):
                    continue
                if lod == 1:
                    box(mb, ((xa + xb) * 0.5, 8.4, zb + 0.55), (3.0, 0.9, 0.55), ('strip', 'concrete_blank'))
                    continue
                box(mb, ((xa + xb) * 0.5, 8.4, zb + 0.1), (3.0, 0.9, 0.1), ('rows', 'concrete'), skip_bottom=False)
                box(mb, ((xa + xb) * 0.5, 9.25, zb + 0.65), (3.0, 0.05, 0.45), ('strip', 'concrete_blank'))
            # oriel bays on the front
            for bx in (2, 7):
                xc = -15 + 3 * bx + 1.5
                if keep(S, (xc, -8.0, zb + 1.5)):
                    box(mb, (xc, -7.95, zb + 1.5), (1.45, 0.45, 1.5), ('strip', 'apt_brick'), skip_bottom=False)
    parapets(mb, g, S, 0.7, 0.3, mapping=('rows', 'concrete'), lod=lod)
    roof_plant(mb, S, -14.0, -6.5, 0.0, 6.5, g.top, 301, lod, n=4)
    if lod <= 1:
        rng = Rng(n * 5 + 3)
        for i in range(4):
            x = -12 + 8 * i
            if keep(S, (x, 2, top + 1)):
                box(mb, (x, 3.0, top + 1.0), (0.5, 0.9, 1.0), ('strip', 'apt_brick'))
        if keep(S, (6, -1, top + 1)):
            box(mb, (6.0, -1.0, top + 1.1), (3.0, 2.2, 1.1), ('strip', 'concrete_blank'))
    mb.socket('SOCKET_Crown', (0.0, 0.0, top))
    return grids


# =========================================================================== office towers


def office_glass(mb, P, S, lod):
    n = P['storeys']
    pod = Grid(-21.0, -15.0, 7, 5, 6.0, 6.0, 0.0, [4.2, 4.2],
               strip=lambda side, iz, ix, iy: 'ground_shop' if iz == 0 else 'office_ribbon', seed=41)
    tw = Grid(-15.0, -9.0, 10, 6, 3.0, 3.0, pod.top, [3.6] * (n - 2), strip=lambda *a: 'office_curtain', seed=42)
    ph = Grid(-9.0, -6.0, 6, 4, 3.0, 3.0, tw.top, [4.5], strip=lambda *a: 'industrial', seed=43)
    grids = [pod, tw, ph]
    if lod == 3:
        for g in grids:
            g.hull(mb, S)
        return grids
    plinth(mb, -21.0, -15.0, 21.0, 15.0)
    for g in grids:
        g.build(mb, S, lod)
    parapets(mb, pod, S, 0.8, 0.3, lod=lod)
    parapets(mb, ph, S, 0.4, 0.2, lod=lod)
    top = tw.top
    # cornice: a projecting band around the tower top
    if lod <= 2:
        for (a, b, c, half) in (((0, -9.6, top - 0.6), None, None, (15.6, 0.6, 0.6)), ((0, 9.6, top - 0.6), None, None, (15.6, 0.6, 0.6)),
                                ((-15.6, 0, top - 0.6), None, None, (0.6, 9.0, 0.6)), ((15.6, 0, top - 0.6), None, None, (0.6, 9.0, 0.6))):
            if keep(S, a):
                box(mb, a, half, ('rows', 'concrete'), skip_bottom=False)
    # vertical aluminium fins (full height when intact, per storey otherwise)
    if lod <= 1:
        step = 1 if lod == 0 else 2
        intact = S is None or S.kind == 'Intact'
        fins = []
        for k in range(0, 11, step):
            x = -15.0 + 3.0 * k
            fins.append(((x, -9.3), (0.12, 0.3)))
            fins.append(((x, 9.3), (0.12, 0.3)))
        for k in range(0, 7, step):
            y = -9.0 + 3.0 * k
            fins.append(((-15.3, y), (0.3, 0.12)))
            fins.append(((15.3, y), (0.3, 0.12)))
        z0, z1 = tw.z0, top - 1.2
        for (x, y), (hx, hy) in fins:
            if intact:
                box(mb, (x, y, (z0 + z1) * 0.5), (hx, hy, (z1 - z0) * 0.5), ('rows', 'steel'), ur=GREY)
            else:
                for iz in range(tw.nz):
                    za, zb = tw.zs[iz], min(z1, tw.zs[iz + 1])
                    if za >= z1 or not keep(S, (x, y, (za + zb) * 0.5)):
                        continue
                    box(mb, (x, y, (za + zb) * 0.5), (hx, hy, (zb - za) * 0.5), ('rows', 'steel'), ur=GREY)
        if keep(S, (0, -17, 4)):
            box(mb, (0.0, -17.0, 4.3), (8.0, 2.0, 0.18), ('rows', 'concrete'), skip_bottom=False)
            for x in (-7.5, 7.5):
                beam(mb, (x, -18.6, 0.0), (x, -18.6, 4.2), 0.25)
    roof_plant(mb, S, -14.5, -8.5, -9.5, 8.5, top, 401, lod, n=4)
    roof_plant(mb, S, 9.5, -8.5, 14.5, 8.5, top, 402, lod, n=4)
    roof_plant(mb, S, -20.0, -14.0, 20.0, -10.0, pod.top, 403, lod, n=6)
    if lod <= 1 and keep(S, (0, 0, ph.top + 2)):
        beam(mb, (-6.0, 0.0, ph.top), (-6.0, 0.0, ph.top + 12.0), 0.4)
        beam(mb, (6.0, 2.0, ph.top), (6.0, 2.0, ph.top + 7.0), 0.25)
        for x in (-12.0, 12.0):
            beam(mb, (x, -7.5, top + 0.6), (x, 7.5, top + 0.6), 0.3)
    if keep(S, (0, 0, ph.top + 2)):
        warn_light(mb, (-6.0, 0.0, ph.top + 12.2))
    mb.socket('SOCKET_Crown', (0.0, 0.0, ph.top))
    return grids


def office_ribbon(mb, P, S, lod):
    n = P['storeys']
    hs = [4.2] + [3.6] * (n - 1)
    t1 = int(round(0.5 * n))
    t2 = int(round(0.8 * n))

    def occ(ix, iy, iz):
        if iz < t1:
            return True
        if iz < t2:
            return 1 <= ix <= 8 and 1 <= iy <= 8
        return 2 <= ix <= 7 and 2 <= iy <= 7

    def strip(side, iz, ix, iy):
        return 'ground_shop' if iz == 0 else 'office_ribbon'
    g = Grid(-15.0, -15.0, 10, 10, 3.0, 3.0, 0.0, hs, occ=occ, strip=strip, seed=51)
    ph = Grid(-3.0, -3.0, 2, 2, 3.0, 3.0, g.top, [4.0], strip=lambda *a: 'industrial', seed=52)
    grids = [g, ph]
    if lod == 3:
        g.hull(mb, S)
        ph.hull(mb, S)
        return grids
    plinth(mb, -15.0, -15.0, 15.0, 15.0)
    g.build(mb, S, lod)
    ph.build(mb, S, lod)
    parapets(mb, g, S, 1.0, 0.25, lod=lod)
    # horizontal sun-shade blades at every spandrel
    if lod <= 1:
        step = 1 if lod == 0 else 2
        intact = S is None or S.kind == 'Intact'
        for iz in range(1, n, step):
            if iz < t1:
                a, b = 0, 10
            elif iz < t2:
                a, b = 1, 9
            else:
                a, b = 2, 8
            lo, hi = -15.0 + 3 * a, -15.0 + 3 * b
            z = g.zs[iz] + 1.05
            sides = [((lo + hi) * 0.5, lo - 0.22, (hi - lo) * 0.5 + 0.44, 0.22), ((lo + hi) * 0.5, hi + 0.22, (hi - lo) * 0.5 + 0.44, 0.22),
                     (lo - 0.22, (lo + hi) * 0.5, 0.22, (hi - lo) * 0.5), (hi + 0.22, (lo + hi) * 0.5, 0.22, (hi - lo) * 0.5)]
            for cx, cy, hx, hy in sides:
                if intact:
                    box(mb, (cx, cy, z), (hx, hy, 0.05), ('rows', 'concrete'), skip_bottom=lod > 0)
                else:
                    # per 3 m piece so damage cuts it
                    long_x = hx > hy
                    L = 2 * (hx if long_x else hy)
                    m = max(1, int(round(L / 3.0)))
                    for k in range(m):
                        off = -L * 0.5 + (k + 0.5) * L / m
                        c = (cx + off, cy, z) if long_x else (cx, cy + off, z)
                        if keep(S, c):
                            box(mb, c, ((L / m) * 0.5 if long_x else hx, hy if long_x else (L / m) * 0.5, 0.07), ('rows', 'concrete'),
                                skip_bottom=False)
        # setback terrace planters
        for iz, a in ((t1, 1), (t2, 2)):
            if iz >= n:
                continue
            z = g.zs[iz]
            lo, hi = -15.0 + 3 * a, -15.0 + 3 * (10 - a)
            for k in range(4):
                c = (lo + (hi - lo) * (k + 0.5) / 4, lo - 1.2, z + 0.4)
                if keep(S, c):
                    box(mb, c, (1.6, 0.6, 0.4), ('rows', 'concrete'))
                    hquad(mb, c[0] - 1.5, c[1] - 0.5, c[0] + 1.5, c[1] + 0.5, z + 0.82, ('rows', 'lawn'))
    roof_plant(mb, S, -9.0, -9.0, -3.5, 9.0, g.top, 501, lod, n=4)
    roof_plant(mb, S, 3.5, -9.0, 9.0, 9.0, g.top, 502, lod, n=4)
    if lod <= 2 and keep(S, (0, 0, ph.top + 3)):
        beam(mb, (2.0, 2.0, ph.top), (2.0, 2.0, ph.top + 14.0), 0.35)
        warn_light(mb, (2.0, 2.0, ph.top + 14.2))
    mb.socket('SOCKET_Crown', (0.0, 0.0, ph.top))
    return grids


def annulus(mb, cx, cy, z, r0, r1, K, thick=0.18, soot=0.0, keepfn=None, bottom=True):
    """Flat ring (balcony slab) with outer edge."""
    v0, v1, span = rows_v('concrete')
    for k in range(K):
        t0 = 2 * math.pi * k / K
        t1 = 2 * math.pi * (k + 1) / K
        if keepfn and not keepfn((cx + r1 * math.cos((t0 + t1) / 2), cy + r1 * math.sin((t0 + t1) / 2), z)):
            continue
        a0 = (cx + r0 * math.cos(t0), cy + r0 * math.sin(t0), z)
        a1 = (cx + r0 * math.cos(t1), cy + r0 * math.sin(t1), z)
        b1 = (cx + r1 * math.cos(t1), cy + r1 * math.sin(t1), z)
        b0 = (cx + r1 * math.cos(t0), cy + r1 * math.sin(t0), z)
        s0, s1 = r1 * t0, r1 * t1
        uv = [(s0 / UREP, v0), (s1 / UREP, v0), (s1 / UREP, v0 + (v1 - v0) * (r1 - r0) / span),
              (s0 / UREP, v0 + (v1 - v0) * (r1 - r0) / span)]
        mb.face([a0, b0, b1, a1], [uv[0], uv[3], uv[2], uv[1]], DETAIL, soot=soot)
        e0 = (b0[0], b0[1], z - thick)
        e1 = (b1[0], b1[1], z - thick)
        mb.face([e0, e1, b1, b0], [(s0 / UREP, v0), (s1 / UREP, v0), (s1 / UREP, v0 + 0.01), (s0 / UREP, v0 + 0.01)], DETAIL,
                soot=soot)
        if bottom:
            f0 = (a0[0], a0[1], z - thick)
            f1 = (a1[0], a1[1], z - thick)
            mb.face([f0, e0, e1, f1][::-1], uv, DETAIL, soot=max(soot, 0.15))


def office_round(mb, P, S, lod):
    n = P['storeys']
    K = 24 if lod <= 1 else 16
    pod = Grid(-18.0, -18.0, 6, 6, 6.0, 6.0, 0.0, [4.8], strip=lambda *a: 'ground_shop', seed=61)
    cg = CylGrid(0.0, 0.0, 11.5, K, pod.top, [3.6] * (n - 1), strip=lambda iz: 'office_curtain', seed=62, circ_u=72.0,
                 a0=math.pi / K)
    grids = [pod, cg]
    if lod == 3:
        pod.hull(mb, S)
        cg.hull(mb, S)
        return grids
    plinth(mb, -18.0, -18.0, 18.0, 18.0)
    pod.build(mb, S, lod)
    cg.build(mb, S, lod)
    parapets(mb, pod, S, 0.9, 0.3, lod=lod)
    top = cg.top
    kf = (lambda p: keep(S, p))
    if lod <= 1:
        step = 1 if lod == 0 else 2
        for iz in range(1, cg.nz, step):
            annulus(mb, 0.0, 0.0, cg.zs[iz] + 0.2, 11.5, 12.6, K, keepfn=kf, bottom=lod == 0)
    # crown: a deep concrete band and roof ring
    if lod <= 2:
        frustum(mb, 0.0, 0.0, top, top + 2.2, 12.2, 12.2, K, ('rows', 'concrete', 2.2), smooth=True, a0=math.pi / K,
                seg_keep=lambda k: keep(S, (12.2 * math.cos(2 * math.pi * (k + 0.5) / K), 12.2 * math.sin(2 * math.pi * (k + 0.5) / K), top + 1)))
        if keep(S, (0, 0, top + 1)):
            frustum(mb, 0.0, 0.0, top, top + 3.8, 5.0, 5.0, 12, ('strip', 'industrial', 0.0, top, 3.8), cap_top=('roof',), smooth=True)
            beam(mb, (0.0, 0.0, top + 3.8), (0.0, 0.0, top + 22.0), 0.45)
            warn_light(mb, (0.0, 0.0, top + 22.2))
    if lod <= 1 and keep(S, (0, -19.5, 4)):
        box(mb, (0.0, -19.5, 4.4), (6.0, 1.8, 0.15), ('rows', 'concrete'), skip_bottom=False)
    mb.socket('SOCKET_Crown', (0.0, 0.0, top + 3.8))
    return grids


# =========================================================================== power plant


def cooling_profile(H=112.0, z0=8.0, rings=13, throat=22.0, zt=88.0, rb=36.0):
    c2 = (zt - z0) ** 2 / ((rb / throat) ** 2 - 1.0)
    prof = []
    for j in range(rings + 1):
        z = z0 + (H - z0) * j / rings
        r = throat * math.sqrt(1.0 + (z - zt) ** 2 / c2)
        prof.append((z, r))
    return prof


def cooling_tower(mb, P, S, lod):
    H = 112.0
    prof = cooling_profile(H, rings={0: 13, 1: 13, 2: 9, 3: 5}[lod])
    K = {0: 40, 1: 32, 2: 20, 3: 12}[lod]
    rb = prof[0][1]
    if lod == 3:
        for j in range(len(prof) - 1):
            (za, ra), (zb, rb_) = prof[j], prof[j + 1]
            if S and S.kind != 'Intact' and S.removed((0.0, -ra, (za + zb) * 0.5)) and S.removed((ra, 0.0, (za + zb) * 0.5)):
                continue
            frustum(mb, 0, 0, za, zb, ra, rb_, K, ('strip', 'concrete_blank', 0.0, za, zb - za), smooth=True)
        frustum(mb, 0, 0, -0.3, prof[0][0], rb + 0.5, rb, K, ('rows', 'concrete', 8.0), smooth=False, soot=0.85)
        return []
    shell(mb, 0.0, 0.0, prof, K, 0.9, S, inner_soot=0.45, circ_u=216.0)
    # supporting legs: V pairs from the ground ring to the shell foot
    if lod <= 1:
        NL = 36 if lod == 0 else 18
        for k in range(NL):
            t = 2 * math.pi * k / NL
            dt = math.pi / NL
            top = (rb * math.cos(t), rb * math.sin(t), prof[0][0] + 0.2)
            if not keep(S, top):
                continue
            for sgn in (-1, 1):
                g = (rb * 1.02 * math.cos(t + sgn * dt), rb * 1.02 * math.sin(t + sgn * dt), -0.2)
                b = beam
                b(mb, g, top, 0.8, mapping=('rows', 'concrete'), ur=None)
    else:
        frustum(mb, 0, 0, 0.0, prof[0][0], rb + 0.3, rb, K, ('rows', 'concrete', 8.0), smooth=False, soot=0.85)
    # basin: rim wall and dark water
    frustum(mb, 0, 0, -0.3, 1.4, rb + 2.6, rb + 2.6, K, ('rows', 'concrete', 1.7), cap_top=None, smooth=True)
    annulus(mb, 0, 0, 1.4, rb + 1.9, rb + 2.6, K, thick=0.0, bottom=False)
    disc(mb, 0, 0, 0.8, rb + 1.95, K, ('rows', 'concrete'), soot=0.9)
    if lod <= 1:
        # top cornice ring
        zt, rt = prof[-1]
        if S is None or S.kind == 'Intact':
            frustum(mb, 0, 0, zt - 1.2, zt, rt + 0.6, rt + 0.6, K, ('rows', 'concrete', 1.2), smooth=True)
        # ladders up the outside (two thin beams)
        if lod == 0:
            for (z0_, r0_), (z1_, r1_) in zip(prof[:-1], prof[1:]):
                pa = (0.0, -(r0_ + 0.25), z0_)
                pb = (0.0, -(r1_ + 0.25), z1_)
                if keep(S, pb):
                    beam(mb, pa, pb, 0.25, ur=GREY)
    zt, rt = prof[-1]
    mb.socket('SOCKET_Steam', (0.0, 0.0, zt))
    for k in range(4):
        t = math.pi / 4 + k * math.pi / 2
        p = (rt * math.cos(t), rt * math.sin(t), zt)
        if keep(S, p):
            warn_light(mb, p)
    return []


def cooling_dress(mb, S, lod):
    if S is None or S.kind == 'Intact':
        return
    zt = 112.0
    if S.kind == 'Construction':
        prof = cooling_profile()
        zc = max(z for z, r in prof if z <= S.cut_z + 1e-6)
        rc = [r for z, r in prof if z == zc][0]
        if lod <= 2:
            K = 32 if lod <= 1 else 16
            annulus(mb, 0, 0, zc + 1.2, rc - 1.5, rc + 1.8, K, thick=1.2)
            crane(mb, 0.0, 0.0, zc + 22.0, jib_len=34.0, yaw=-0.6, lod=lod)
    elif S.kind == 'Damaged' and lod <= 2:
        rubble(mb, (20.0, -38.0, 0.0), 12.0, 4.5, S.seed + 1, lod=lod, soot=0.5)
        rubble(mb, (8.0, -18.0, 0.8), 9.0, 3.0, S.seed + 2, lod=lod, soot=0.5)
    elif S.kind == 'Ruin' and lod <= 2:
        rubble(mb, (0.0, 0.0, 0.8), 24.0, 9.0, S.seed + 3, K=12 if lod == 0 else 7, lod=lod, soot=0.6)
        for i, (x, y) in enumerate(((30, -30), (-38, -12), (12, 40))):
            rubble(mb, (x, y, 0.0), 10.0, 4.0, S.seed + 10 + i, lod=lod, soot=0.6)


def reactor(mb, P, S, lod):
    """Main plant complex: containment dome, turbine hall, auxiliary and control buildings, stack."""
    ccx, ccy = -8.0, 10.0
    K = {0: 28, 1: 28, 2: 16, 3: 12}[lod]
    cont = CylGrid(ccx, ccy, 19.0, K, 0.0, [5.0] * 7, strip=lambda iz: 'concrete_blank', seed=71, roof_kind='concrete')
    turb = Grid(12.0, -22.0, 8, 4, 6.0, 6.5, 0.0, [6.0] * 4, strip=lambda *a: 'industrial', seed=72)
    aux = Grid(-40.0, -24.0, 5, 4, 6.0, 6.0, 0.0, [5.0] * 4,
               strip=lambda side, iz, ix, iy: 'office_ribbon' if (side == 'S' and iz >= 2) else 'concrete_blank', seed=73)
    fuel = Grid(12.0, 6.0, 4, 3, 6.0, 6.0, 0.0, [6.0] * 3, strip=lambda *a: 'concrete_blank', seed=74)
    admin = Grid(-56.0, -46.0, 10, 4, 3.0, 3.0, 0.0, [4.2] + [3.6] * 3,
                 strip=lambda side, iz, ix, iy: 'ground_shop' if iz == 0 else 'office_ribbon', seed=75)
    pump = Grid(-14.0, -46.0, 5, 2, 6.0, 6.0, 0.0, [7.0], strip=lambda *a: 'industrial', seed=76)
    grids = [cont, turb, aux, fuel, admin, pump]
    dome_keep = (lambda k, j, p: keep(S, p))
    if lod == 3:
        for g in grids:
            g.hull(mb, S)
        if keep(S, (ccx, ccy, 45)):
            dome(mb, ccx, ccy, 35.0, 19.0, 0.8, 12, 3)
        if keep(S, (10, 26, 50)):
            frustum(mb, 10.0, 26.0, 0.0, 78.0, 2.6, 2.0, 6, ('strip', 'concrete_blank', 0.0, 0.0, 78.0), smooth=False)
        return grids
    # yard: gravel/concrete hardstanding on a foundation slab
    box(mb, (0.0, 0.0, -0.1), (60.0, 50.0, 0.2), ('roof',))
    for g in grids:
        g.build(mb, S, lod)
    for g in (turb, aux, fuel, admin, pump):
        parapets(mb, g, S, 0.7, 0.3, lod=lod)
    dome(mb, ccx, ccy, 35.0, 19.0, 0.8, K, 7 if lod <= 1 else 4, mapping=('rows', 'concrete'), panel_keep=dome_keep, thick=0.9)
    # turbine hall roof monitor
    if lod <= 2:
        for k in range(4):
            x = 18.0 + 12.0 * k
            if keep(S, (x, -9.0, 25.0)):
                box(mb, (x, -9.0, 25.0), (6.0, 3.0, 1.0), ('strip', 'industrial'))
    # ventilation stack
    if keep(S, (10.0, 26.0, 40.0)) or (S and S.kind == 'Construction'):
        top = 78.0 if keep(S, (10.0, 26.0, 70.0)) else max(10.0, (S.cut_z if S.kind == 'Construction' else 30.0))
        frustum(mb, 10.0, 26.0, -0.3, top, 2.6, 2.6 - 0.6 * top / 78.0, 12 if lod <= 1 else 8,
                ('strip', 'concrete_blank', 0.0, 0.0, 6.0), smooth=True)
        if top >= 78.0:
            annulus(mb, 10.0, 26.0, 70.0, 2.1, 3.4, 12, thick=0.3)
            warn_light(mb, (10.0, 26.0, 78.2))
    if lod <= 1:
        # transformers along the turbine hall
        for k in range(3):
            x = 22.0 + 12.0 * k
            c = (x, 7.0 + 2.0, 2.6)
            if keep(S, c):
                box(mb, (x, 9.0, 2.6), (3.2, 2.4, 2.6), ('rows', 'steel'), ur=GREY)
                if lod == 0:
                    for f in range(-2, 3):
                        box(mb, (x + f * 1.2, 11.7, 2.4), (0.08, 0.3, 2.0), ('rows', 'steel'), ur=GREY)
                    for f in (-1.2, 0.0, 1.2):
                        frustum(mb, x + f, 9.0, 5.2, 7.0, 0.22, 0.12, 6, ('rows', 'steel', 1.8, GREY), smooth=False)
        # pipe bridges between buildings
        for p0, p1 in (((-10.0, -12.0, 12.0), (12.0, -12.0, 12.0)), ((-10.0, -6.0, 9.0), (12.0, -6.0, 9.0)),
                       ((12.0, 0.0, 14.0), (12.0, 6.0, 14.0))):
            if keep(S, mul3(add3(p0, p1), 0.5)):
                beam(mb, p0, p1, 1.4, h=1.0, ur=GREY)
        # water intake culverts toward the river
        for x in (-10.0, 0.0, 10.0):
            if keep(S, (x, -50.0, 0.5)):
                box(mb, (x, -49.0, 0.6), (2.2, 3.0, 0.9), ('rows', 'concrete'))
    if lod == 0:
        # roof clutter
        rng = Rng(77)
        for g in (aux, fuel, admin):
            for i in range(4):
                p = (rng.u(g.x0 + 2, g.x1 - 2), rng.u(g.y0 + 2, g.y1 - 2), g.top)
                if keep(S, p):
                    box(mb, (p[0], p[1], g.top + 0.8), (rng.u(0.8, 2.0), rng.u(0.8, 1.6), 0.8), ('rows', 'steel'), ur=GREY)
    if lod <= 1:
        # demineralised-water and fuel-oil tanks, utility sheds
        for i, (x, y, r, h) in enumerate(((36.0, 30.0, 6.0, 12.0), (50.0, 30.0, 6.0, 12.0), (-34.0, 30.0, 4.5, 9.0),
                                          (-46.0, 36.0, 3.5, 7.0))):
            if keep(S, (x, y, h * 0.5)):
                frustum(mb, x, y, -0.3, h, r, r, 16 if lod == 0 else 10, ('strip', 'industrial', 0.0, 0.0, 3.0), smooth=True)
                # (verify fix: the steel row is half grey, half safety yellow and dome() spans its full U, which
                # painted yellow pie wedges on every tank roof; pale concrete-grey reads as painted tank roofs)
                dome(mb, x, y, h, r, 0.25, 16 if lod == 0 else 10, 2, mapping=('rows', 'concrete'))
        sheds = Grid(30.0, 38.0, 4, 1, 6.0, 8.0, 0.0, [5.0], strip=lambda *a: 'industrial', seed=79)
        sheds.build(mb, S, lod)
        gate = Grid(-58.0, 10.0, 2, 2, 4.0, 4.0, 0.0, [3.6], strip=lambda side, iz, ix, iy: 'office_ribbon', seed=78)
        gate.build(mb, S, lod)
        parapets(mb, sheds, S, 0.4, 0.2, lod=lod)
    if lod == 0:
        # perimeter fence: posts and two rails (security fence of the concept)
        pts = [(-59.5, -49.5), (59.5, -49.5), (59.5, 49.5), (-59.5, 49.5)]
        for i in range(4):
            a, b = pts[i], pts[(i + 1) % 4]
            L = math.hypot(b[0] - a[0], b[1] - a[1])
            n = int(L / 8.0)
            for k in range(n + 1):
                x = a[0] + (b[0] - a[0]) * k / n
                y = a[1] + (b[1] - a[1]) * k / n
                if keep(S, (x, y, 1.2)):
                    beam(mb, (x, y, 0.0), (x, y, 2.6), 0.12, caps=False)
            for z in (1.3, 2.5):
                if keep(S, ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, z)):
                    beam(mb, (a[0], a[1], z), (b[0], b[1], z), 0.06, caps=False)
        for (x, y) in ((-50.0, 0.0), (0.0, -40.0), (50.0, 0.0), (0.0, 44.0)):
            if keep(S, (x, y, 8)):
                beam(mb, (x, y, 0.0), (x, y, 18.0), 0.35, caps=False)
                box(mb, (x, y, 18.3), (1.2, 0.4, 0.3), ('rows', 'steel'), ur=GREY)
                mb.socket('SOCKET_Floodlight', (x, y, 18.0))
    mb.socket('SOCKET_Exhaust', (10.0, 26.0, 78.0))
    return grids


def add3(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul3(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def lattice_tower(mb, x, y, h, base, topw, panels, lod, ur=GREY, w=0.35):
    legs = [(sx, sy) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]

    def P(sx, sy, f):
        hw = base + (topw - base) * f
        return (x + sx * hw, y + sy * hw, h * f)
    for sx, sy in legs:
        beam(mb, P(sx, sy, 0.0), P(sx, sy, 1.0), w * 1.4, ur=ur, caps=False)
    if lod >= 2:
        return
    for i in range(panels):
        f0, f1 = i / panels, (i + 1) / panels
        for a in range(4):
            sa, sb = legs[a], legs[(a + 1) % 4]
            beam(mb, P(*sa, f0), P(*sb, f1), w, ur=ur, caps=False)
            if lod == 0:
                beam(mb, P(*sb, f0), P(*sa, f1), w, ur=ur, caps=False)
            beam(mb, P(*sa, f1), P(*sb, f1), w * 0.8, ur=ur, caps=False)


def pylon(mb, P, S, lod):
    h = 40.0
    lattice_tower(mb, 0.0, 0.0, h, 4.5, 1.0, 7 if lod == 0 else 4, lod, w=0.3)
    if lod <= 2:
        # concrete footings under the four legs, 0.3 m into the ground (ASSETS.md: sit on sloped terrain)
        for sx in (-1, 1):
            for sy in (-1, 1):
                box(mb, (sx * 4.5, sy * 4.5, 0.05), (0.75, 0.75, 0.35), ('rows', 'concrete'))
    arms = ((26.0, 9.0), (31.0, 11.0), (36.0, 7.5))
    for z, L in arms:
        hw = 4.5 + (1.0 - 4.5) * z / h
        if lod <= 2:
            beam(mb, (-L, 0.0, z), (L, 0.0, z), 0.5, h=0.8)
        if lod <= 1:
            beam(mb, (-L, 0.0, z), (-hw, 0.0, z - 3.0), 0.3)
            beam(mb, (L, 0.0, z), (hw, 0.0, z - 3.0), 0.3)
        if lod <= 1:
            for sx in (-1, 1):
                xx = sx * (L - 0.6)
                frustum(mb, xx, 0.0, z - 3.2, z, 0.22, 0.22, 6, ('rows', 'steel', 3.2, GREY), smooth=False)
                mb.socket('SOCKET_Wire', (xx, 0.0, z - 3.2))
    if lod <= 1:
        beam(mb, (0.0, 0.0, h), (0.0, 0.0, h + 4.0), 0.3)
    return []


def switchyard(mb, P, S, lod):
    grids = []
    hut = Grid(-30.0, 14.0, 4, 3, 3.0, 3.0, 0.0, [4.0], strip=lambda *a: 'concrete_blank', seed=81)
    grids.append(hut)
    if lod == 3:
        box(mb, (0, 0, -0.1), (35.0, 25.0, 0.2), ('roof',))
        hut.hull(mb, S)
        for y in (-12.0, 0.0, 12.0):
            if keep(S, (0, y, 10)):
                box(mb, (0.0, y, 12.0), (30.0, 0.4, 0.5), ('rows', 'steel'), ur=GREY)
        return grids
    box(mb, (0.0, 0.0, -0.1), (35.0, 25.0, 0.2), ('roof',))
    hut.build(mb, S, lod)
    parapets(mb, hut, S, 0.4, 0.2, lod=lod)
    for y in (-12.0, 0.0, 12.0):
        xs = [-28.0, -14.0, 0.0, 14.0, 28.0]
        for x in xs:
            if keep(S, (x, y, 6.0)):
                if lod <= 1:
                    lattice_tower(mb, x, y, 13.0, 0.9, 0.6, 3 if lod == 0 else 2, lod, w=0.22)
                else:
                    beam(mb, (x, y, 0.0), (x, y, 13.0), 1.2)
        for i in range(len(xs) - 1):
            c = ((xs[i] + xs[i + 1]) * 0.5, y, 12.6)
            if keep(S, c):
                beam(mb, (xs[i], y, 12.6), (xs[i + 1], y, 12.6), 0.6, h=0.9)
                if lod == 0:
                    for k in range(3):
                        xx = xs[i] + (k + 1) * 3.5
                        frustum(mb, xx, y, 9.8, 12.2, 0.18, 0.18, 6, ('rows', 'steel', 2.4, GREY), smooth=False)
        if lod <= 1:
            # equipment rows: breakers and busbar supports
            for k in range(9):
                x = -26.0 + k * 6.5
                c = (x, y + 4.5, 2.0)
                if keep(S, c):
                    beam(mb, (x, y + 4.5, 0.0), (x, y + 4.5, 3.6), 0.35)
                    box(mb, (x, y + 4.5, 4.3), (0.6, 0.6, 0.7), ('rows', 'steel'), ur=GREY)
            if keep(S, (0, y + 4.5, 5.5)):
                beam(mb, (-27.0, y + 4.5, 5.4), (27.0, y + 4.5, 5.4), 0.2)
    for x in (-12.0, 12.0):
        if keep(S, (x, -20.0, 3.0)):
            box(mb, (x, -20.0, 3.0), (4.0, 3.0, 3.0), ('rows', 'steel'), ur=GREY)
            if lod <= 1:
                for f in range(-3, 4):
                    box(mb, (x + f * 1.1, -23.3, 2.8), (0.08, 0.35, 2.2), ('rows', 'steel'), ur=GREY)
    return grids


# =========================================================================== university


def university(mb, P, S, lod):
    def teach_occ(ix, iy, iz):
        return iz > 0 or 12 <= ix <= 17

    def teach_strip(side, iz, ix, iy):
        if iz == 0:
            return 'ground_shop'
        return 'civic_fin' if side in 'SN' else 'concrete_blank'
    teach = Grid(-43.2, 12.0, 30, 4, 2.4, 3.6, 0.0, [4.2] + [3.6] * 4, occ=teach_occ, strip=teach_strip, seed=91,
                 bottom_visible=False)
    lib = Grid(26.0, -30.0, 6, 6, 3.0, 3.0, 0.0, [4.2] + [3.6] * 10,
               strip=lambda side, iz, ix, iy: 'ground_shop' if iz == 0 else 'office_ribbon', seed=92)
    libph = Grid(29.0, -27.0, 4, 4, 3.0, 3.0, lib.top, [3.6], strip=lambda *a: 'industrial', seed=93)
    hall = Grid(-46.0, -32.0, 4, 5, 6.0, 6.0, 0.0, [5.5, 5.5],
                strip=lambda side, iz, ix, iy: 'ground_shop' if (iz == 0 and side == 'E') else 'concrete_blank', seed=94)
    K = {0: 20, 1: 20, 2: 12, 3: 10}[lod]
    planet = CylGrid(-8.0, -24.0, 8.0, K, 0.0, [4.5, 4.5], strip=lambda iz: 'civic_fin', seed=95, roof_kind='concrete',
                     circ_u=48.0)
    grids = [teach, lib, libph, hall, planet]
    if lod == 3:
        for g in grids:
            g.hull(mb, S)
        if keep(S, (-8, -24, 12)):
            dome(mb, -8.0, -24.0, 9.0, 8.6, 0.95, 10, 3, mapping=('rows', 'copper'))
        box(mb, (0, 0, -0.1), (50.0, 38.0, 0.2), ('roof',))
        return grids
    # grounds: paving, lawn quad, paths
    tiled_plate(mb, -50.0, -38.0, 50.0, 38.0, 0.12, 'concrete', thick=0.42)
    tiled_plate(mb, -20.0, -34.0, 22.0, 8.0, 0.16, 'lawn')
    if lod <= 1:
        tiled_plate(mb, -2.0, -34.0, 2.0, 8.0, 0.19, 'concrete')
        tiled_plate(mb, -20.0, -15.0, 22.0, -12.0, 0.19, 'concrete')
    for g in grids:
        g.build(mb, S, lod)
    for g in (teach, lib, libph, hall):
        parapets(mb, g, S, 0.6 if g is not hall else 0.9, 0.3, lod=lod)
    # raked roof over the lecture hall (rises to the north)
    if keep(S, (-34.0, -17.0, 13.0)):
        x0, x1, y0, y1, z = -46.0, -22.0, -32.0, -2.0, hall.top
        zn = z + 4.5
        a, b, c, d = (x0, y0, z + 0.9), (x1, y0, z + 0.9), (x1, y1, zn), (x0, y1, zn)
        mb.face([a, b, c, d], [roof_uv(p) for p in (a, b, c, d)], ROOF)
        v0, v1 = strip_v('concrete_blank')
        # gable triangles: the apex r_ sits above p on the west end and above q on the east end (verify fix:
        # the east one used the west UVs, a sheared 244 m/UV smear)
        for (p, q, r_, ur_) in (((x0, y1, z), (x0, y0, z), (x0, y1, zn), 0.0), ((x1, y0, z), (x1, y1, z), (x1, y1, zn), 30 / UREP)):
            quad_auto(mb, [p, q, r_], (-34.0, -17.0, z), STRUCT, [(0, v0), (30 / UREP, v0), (ur_, v1)])
        quad_auto(mb, [(x0, y0, z), (x1, y0, z), (x1, y0, z + 0.9), (x0, y0, z + 0.9)], (-34.0, -17.0, z), STRUCT,
                  [(0, v0), (1, v0), (1, v0 + 0.02), (0, v0 + 0.02)])
        wall_quad(mb, (x1, y1), (x0, y1), z, zn, ('strip', 'concrete_blank', 0.0, z, 4.5))
        # (verify fix: a triangle here duplicated the 0.9 m front band above, coplanar -> z-fighting)
    # planetarium dome (copper, a nod to the academy's observatory)
    dome(mb, -8.0, -24.0, 9.0, 8.6, 0.95, K, 6 if lod <= 1 else 4, mapping=('rows', 'copper'),
         panel_keep=lambda k, j, p: keep(S, p), thick=0.5)
    if lod <= 2:
        # pilotis under the teaching slab
        for ix in range(0, 31, 3 if lod <= 1 else 6):
            if 12 <= ix <= 18:
                continue
            for y in (12.6, 25.8):
                x = -43.2 + 2.4 * ix
                if keep(S, (x, y, 2.0)):
                    box(mb, (x, y, 2.1), (0.4, 0.4, 2.1), ('rows', 'concrete'))
    if lod == 0:
        # deep vertical fins on the long faces of the teaching slab
        for ix in range(0, 31):
            x = -43.2 + 2.4 * ix
            for y, sy in ((12.0, -1), (26.4, 1)):
                for iz in range(1, teach.nz):
                    za, zb = teach.zs[iz], teach.zs[iz + 1]
                    if keep(S, (x, y, (za + zb) * 0.5)):
                        box(mb, (x, y + sy * 0.3, (za + zb) * 0.5), (0.14, 0.3, (zb - za) * 0.5), ('strip', 'concrete_blank'))
    if lod <= 1:
        # covered walkway along the quad's north edge, from the hall to the library
        for k in range(12):
            x = -20.0 + 4.0 * k
            c = (x + 2.0, 10.0, 3.6)
            if keep(S, c):
                box(mb, c, (2.0, 2.0, 0.15), ('rows', 'concrete'), skip_bottom=False)
                beam(mb, (x, 8.6, 0.1), (x, 8.6, 3.45), 0.25)
        # atom sculpture: three tilted rings around a sphere, the atomic age's emblem
        if keep(S, (0.0, -12.0, 4.0)):
            cxs, cys, czs, R = 0.0, -22.0, 5.0, 3.2
            box(mb, (cxs, cys, 1.0), (0.8, 0.8, 1.0), ('rows', 'concrete'))
            dome(mb, cxs, cys, czs, 0.9, 1.0, 10, 3, mapping=('rows', 'copper'))
            dome(mb, cxs, cys, czs, 0.9, -1.0, 10, 3, mapping=('rows', 'copper'))
            for tilt, spin in ((1.1, 0.0), (1.1, 2.094), (1.1, 4.189)):
                pts = []
                for k in range(16):
                    t = 2 * math.pi * k / 16
                    x, y = R * math.cos(t), R * math.sin(t) * math.cos(tilt)
                    z = R * math.sin(t) * math.sin(tilt)
                    xr = x * math.cos(spin) - y * math.sin(spin)
                    yr = x * math.sin(spin) + y * math.cos(spin)
                    pts.append((cxs + xr, cys + yr, czs + z))
                for k in range(16):
                    beam(mb, pts[k], pts[(k + 1) % 16], 0.18, ur=GREY)
    if lod <= 1 and keep(S, (35, -21, lib.top + 2)):
        beam(mb, (33.0, -24.0, libph.top), (33.0, -24.0, libph.top + 6.0), 0.2)
    return grids


# =========================================================================== highways


def deck_path(mb, path, width, thick, row, S, lod, barrier=True, flip=False, ground_fill=False, soot=0.0):
    """Sweeps a road deck along path [(x, y, z_top)]. row: 'road' (12 m) or 'ramp' (9 m).
    flip: lane layout mirrored (v = 0 on the left edge). ground_fill: sides run down to -0.3 when low."""
    v0, v1, span = rows_v(row)
    n = len(path)
    # tangents
    lat = []
    for i in range(n):
        a = path[max(0, i - 1)]
        b = path[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        l = math.hypot(dx, dy) or 1.0
        lat.append((-dy / l, dx / l))
    s = [0.0]
    for i in range(1, n):
        s.append(s[-1] + math.dist(path[i - 1], path[i]))
    hw = width * 0.5
    L = [(p[0] + l[0] * hw, p[1] + l[1] * hw, p[2]) for p, l in zip(path, lat)]
    R = [(p[0] - l[0] * hw, p[1] - l[1] * hw, p[2]) for p, l in zip(path, lat)]

    def vv(f):
        return v0 + (v1 - v0) * clamp(f, 0, 1)
    fw = width / span
    for i in range(n - 1):
        mid = ((path[i][0] + path[i + 1][0]) * 0.5, (path[i][1] + path[i + 1][1]) * 0.5, (path[i][2] + path[i + 1][2]) * 0.5)
        if not keep(S, mid):
            continue
        u0, u1 = s[i] / UREP, s[i + 1] / UREP
        if flip:
            uv = [(u0, vv(fw)), (u1, vv(fw)), (u1, vv(0)), (u0, vv(0))]
        else:
            uv = [(u0, vv(0)), (u1, vv(0)), (u1, vv(fw)), (u0, vv(fw))]
        mb.face([R[i], R[i + 1], L[i + 1], L[i]], uv, DETAIL, soot=soot)
        for side, E in ((0, R), (1, L)):
            za0 = E[i][2] - thick
            za1 = E[i + 1][2] - thick
            if ground_fill:
                if E[i][2] < 3.5:
                    za0 = -0.3
                if E[i + 1][2] < 3.5:
                    za1 = -0.3
            a = (E[i][0], E[i][1], za0)
            b = (E[i + 1][0], E[i + 1][1], za1)
            pts = [a, b, E[i + 1], E[i]]
            uvs = rows_uv('concrete', [(s[i], 0), (s[i + 1], 0), (s[i + 1], 2), (s[i], 2)], ext=3.0)
            quad_auto(mb, pts, mid, DETAIL, uvs, soot=soot)
        # underside
        zb0, zb1 = path[i][2] - thick, path[i + 1][2] - thick
        if not ground_fill or (path[i][2] >= 3.5 and path[i + 1][2] >= 3.5):
            pts = [(R[i][0], R[i][1], zb0), (L[i][0], L[i][1], zb0), (L[i + 1][0], L[i + 1][1], zb1), (R[i + 1][0], R[i + 1][1], zb1)]
            uvs = rows_uv('concrete', [(s[i], 0), (s[i], 3), (s[i + 1], 3), (s[i + 1], 0)], ext=width)
            mb.face(pts, uvs, DETAIL, soot=max(soot, 0.25))
        if barrier and lod <= 2:
            for E, sgn in ((R, 1), (L, -1)):
                # jersey-style barrier: inner face, top, outer face (0.4 wide, 1.0 tall), sitting on the deck edge
                lx = lat[i][0] * sgn * 0.4
                ly = lat[i][1] * sgn * 0.4
                lx1 = lat[i + 1][0] * sgn * 0.4
                ly1 = lat[i + 1][1] * sgn * 0.4
                o0, o1 = E[i], E[i + 1]
                i0 = (o0[0] + lx, o0[1] + ly, o0[2])
                i1 = (o1[0] + lx1, o1[1] + ly1, o1[2])
                t = 1.0
                ot0, ot1 = (o0[0], o0[1], o0[2] + t), (o1[0], o1[1], o1[2] + t)
                it0, it1 = (i0[0], i0[1], i0[2] + t), (i1[0], i1[1], i1[2] + t)
                uvs = rows_uv('concrete', [(s[i], 0), (s[i + 1], 0), (s[i + 1], 1), (s[i], 1)], ext=3.0)
                bc = ((o0[0] + o1[0] + i0[0] + i1[0]) * 0.25, (o0[1] + o1[1] + i0[1] + i1[1]) * 0.25, (o0[2] + o1[2]) * 0.5 + 0.5)
                for pts in ([i0, i1, it1, it0], [it0, it1, ot1, ot0], [o1, o0, ot0, ot1]):
                    quad_auto(mb, pts, bc, DETAIL, uvs, soot=soot)
    return s


def hammer_pier(mb, x, y, ztop, half_len, S, lod, yaw=0.0):
    if not keep(S, (x, y, ztop * 0.5)):
        if S and S.kind in ('Ruin', 'Damaged'):
            box(mb, (x, y, 1.5), (1.4, 2.8, 1.8), ('rows', 'rubble'), yaw=yaw, soot=0.6)
        return
    zc = ztop - 2.2
    box(mb, (x, y, (zc - 0.3) * 0.5 - 0.15), (1.4, 2.8, (zc + 0.3) * 0.5), ('rows', 'concrete'), yaw=yaw)
    box(mb, (x, y, zc + 0.6), (1.8, half_len, 0.6), ('rows', 'concrete'), yaw=yaw, skip_bottom=False)
    if lod <= 1:
        box(mb, (x, y, -0.15), (3.0, 4.5, 0.3), ('rows', 'concrete'), yaw=yaw)


def lamp(mb, x, y, z, S, lod, both=True):
    if lod > 1 or not keep(S, (x, y, z + 5)):
        return
    if lod == 1:
        beam(mb, (x, y, z), (x, y, z + 11.0), 0.3)
        return
    beam(mb, (x, y, z), (x, y, z + 11.0), 0.25)
    for sgn in ((-1, 1) if both else (1,)):
        beam(mb, (x, y, z + 11.0), (x, y + sgn * 2.5, z + 11.3), 0.15)
        box(mb, (x, y + sgn * 2.8, z + 11.2), (0.35, 0.45, 0.12), ('rows', 'steel'), ur=GREY)
        mb.socket('SOCKET_Lamp', (x, y + sgn * 2.8, z + 11.0))


def viaduct_pair(mb, x0, x1, ztop, S, lod, seg=15.0):
    """Twin 12 m carriageways along X with a median barrier."""
    n = max(1, int(round((x1 - x0) / (seg if lod <= 1 else seg * 2))))
    for yc, flip in ((7.0, False), (-7.0, True)):
        path = [(x0 + (x1 - x0) * i / n, yc, ztop) for i in range(n + 1)]
        deck_path(mb, path, 12.0, 2.2, 'road', S, lod, barrier=True, flip=flip)
    # median: deck strip + low barrier
    path = [(x0 + (x1 - x0) * i / n, 0.0, ztop) for i in range(n + 1)]
    deck_path(mb, path, 2.0, 2.2, 'concrete', S, lod, barrier=False)
    if lod <= 2:
        for i in range(n):
            xa = x0 + (x1 - x0) * i / n
            xb = x0 + (x1 - x0) * (i + 1) / n
            if keep(S, ((xa + xb) * 0.5, 0.0, ztop)):
                box(mb, ((xa + xb) * 0.5, 0.0, ztop + 0.45), ((xb - xa) * 0.5, 0.3, 0.45), ('rows', 'concrete'))


def highway_viaduct(mb, P, S, lod):
    zt = 9.0
    if lod == 3:
        if keep(S, (-15, 0, zt)):
            box(mb, (-15.0, 0.0, zt - 1.1), (15.0, 13.0, 1.1), ('rows', 'concrete'))
        if keep(S, (15, 0, zt)):
            box(mb, (15.0, 0.0, zt - 1.1), (15.0, 13.0, 1.1), ('rows', 'concrete'))
        box(mb, (0.0, 0.0, zt * 0.5 - 1), (1.4, 2.8, zt * 0.5 - 1), ('rows', 'concrete'))
        return []
    viaduct_pair(mb, -30.0, 30.0, zt, S, lod)
    hammer_pier(mb, 0.0, 0.0, zt, 12.5, S, lod, yaw=0.0)
    for x in (-15.0, 15.0):
        lamp(mb, x, 0.0, zt + 0.9, S, lod)
    if S and S.kind == 'Damaged':
        # the east span has dropped: two broken deck slabs leaning from the pier cap to the ground
        if lod <= 2:
            for yc in (7.0, -7.0):
                beam(mb, (2.5, yc, zt - 1.2), (27.0, yc * 1.05, 1.0), 12.0, mapping=('rows', 'concrete'), ur=None, h=2.2, soot=0.4)
            rubble(mb, (26.0, 0.0, 0.0), 9.0, 2.5, 5, lod=lod, soot=0.5)
    if S and S.kind == 'Construction' and lod <= 2:
        # falsework towers under the unfinished east span
        for x in (8.0, 16.0, 24.0):
            for y in (-7.0, 7.0):
                lattice_tower(mb, x, y, zt - 2.2, 1.2, 1.2, 2, max(lod, 1), w=0.3)
        crane(mb, 20.0, 18.0, 26.0, jib_len=30.0, yaw=-math.pi / 2, lod=lod)
    if S and S.kind == 'Ruin' and lod <= 2:
        rubble(mb, (-15.0, 0.0, 0.0), 10.0, 3.0, 7, lod=lod, soot=0.5)
        rubble(mb, (16.0, 3.0, 0.0), 11.0, 3.5, 8, lod=lod, soot=0.5)
    return []


def highway_grade(mb, P, S, lod):
    """60 m at-grade dual carriageway on a low embankment."""
    zt = 1.0
    if lod == 3:
        box(mb, (0.0, 0.0, (zt - 0.3) * 0.5), (30.0, 15.0, (zt + 0.3) * 0.5), ('rows', 'concrete'))
        return []
    # embankment slopes (lawn)
    for sgn in (1, -1):
        a, b = (30.0, sgn * 13.6), (-30.0, sgn * 13.6)
        if sgn < 0:
            a, b = b, a
        y_out = sgn * 17.5
        p = [(a[0], y_out, -0.3), (b[0], y_out, -0.3), (b[0], b[1], zt), (a[0], a[1], zt)]
        uvs = rows_uv('lawn', [(0, 0), (60, 0), (60, 3), (0, 3)], ext=3.0)
        quad_auto(mb, p, (0, 0, -5.0), DETAIL, uvs)
    n = 4 if lod <= 1 else 2
    for yc, flip in ((7.0, False), (-7.0, True)):
        path = [(-30.0 + 60.0 * i / n, yc, zt) for i in range(n + 1)]
        deck_path(mb, path, 12.0, 1.3, 'road', S, lod, barrier=lod <= 1, flip=flip)
    path = [(-30.0 + 60.0 * i / n, 0.0, zt) for i in range(n + 1)]
    deck_path(mb, path, 2.0, 1.3, 'lawn', S, lod, barrier=False)
    if lod <= 2:
        box(mb, (0.0, 0.0, zt + 0.45), (30.0, 0.3, 0.45), ('rows', 'concrete'))
    for x in (-15.0, 15.0):
        lamp(mb, x, 0.0, zt + 0.9, S, lod)
    return []


def bezier(p0, p1, p2, p3, n):
    out = []
    for i in range(n + 1):
        t = i / n
        a = (1 - t) ** 3
        b = 3 * (1 - t) ** 2 * t
        c = 3 * (1 - t) * t * t
        d = t ** 3
        out.append(tuple(a * p0[k] + b * p1[k] + c * p2[k] + d * p3[k] for k in range(3)))
    return out


def ease(t):
    return t * t * (3 - 2 * t)


def ramp_piers(mb, path, S, lod, every=24.0):
    if lod >= 3:
        return
    acc = 0.0
    for i in range(1, len(path)):
        acc += math.dist(path[i - 1], path[i])
        p = path[i]
        if acc >= every and p[2] > 3.6:
            acc = 0.0
            if keep(S, (p[0], p[1], p[2] * 0.5)):
                zc = p[2] - 1.6
                box(mb, (p[0], p[1], (zc - 0.3) * 0.5), (1.0, 1.0, (zc + 0.3) * 0.5), ('rows', 'concrete'))
                if lod <= 1:
                    box(mb, (p[0], p[1], zc + 0.2), (1.6, 1.6, 0.2), ('rows', 'concrete'))


def highway_interchange(mb, P, S, lod):
    """Cloverleaf: elevated highway along X (z 9) over an at-grade arterial along Y, four loops, four outer ramps."""
    zt = 9.0
    E = 120.0
    nseg = {0: 0.6, 1: 1.2, 2: 2.0, 3: 4.0}[lod]
    if lod == 3:
        box(mb, (0.0, 0.0, zt - 1.1), (E, 13.0, 1.1), ('rows', 'concrete'))
        box(mb, (0.0, 0.0, 0.0), (10.0, E, 0.3), ('rows', 'concrete'))
        for sx in (-1, 1):
            for sy in (-1, 1):
                frustum(mb, sx * 46.5, sy * 51.5, 0.0, 5.0, 38.5, 38.5, 10, ('rows', 'concrete', 5.0), smooth=False)
        return []
    viaduct_pair(mb, -E, E, zt, S, lod, seg=15.0)
    for x in (-90.0, -30.0, 30.0, 90.0):
        hammer_pier(mb, x, 0.0, zt, 12.5, S, lod)
    for x in (-105.0, -75.0, -45.0, -15.0, 15.0, 45.0, 75.0, 105.0):
        lamp(mb, x, 0.0, zt + 0.9, S, lod)
    # arterial at grade along Y, two 9 m carriageways
    ny = int(16 / nseg)
    for xc, flip in ((5.5, True), (-5.5, False)):
        path = [(xc, -E + 2 * E * i / ny, 0.15) for i in range(ny + 1)]
        deck_path(mb, path, 9.0, 0.45, 'ramp', S, lod, barrier=lod <= 1, flip=flip, ground_fill=True)
    path = [(0.0, -E + 2 * E * i / ny, 0.15) for i in range(ny + 1)]
    deck_path(mb, path, 2.0, 0.45, 'lawn', S, lod, barrier=False, ground_fill=True)
    # loops (270 degrees) and outer connectors in each quadrant
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy, r = sx * 46.5, sy * 51.5, 34.0
            # start: on the arterial side (tangent heading away from the highway), end: tangent on the highway side
            a_start = math.atan2(0, -sx)           # point at x = cx - sx*r (toward the arterial)
            n = int(36 / nseg)
            pts = []
            for i in range(n + 1):
                f = i / n
                # sweep 270 degrees: from the arterial side around the far side to the highway side
                ang = a_start - sx * sy * (-1) * 1.5 * math.pi * f if False else a_start + (-sx * sy) * 1.5 * math.pi * f
                z = 0.4 + (zt - 0.4) * ease(f)
                pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang), z))
            deck_path(mb, pts, 9.0, 1.6, 'ramp', S, lod, barrier=lod <= 1, ground_fill=True)
            ramp_piers(mb, pts, S, lod)
            # outer connector ramp
            pts = bezier((sx * 12.5, sy * 118.0, 0.4), (sx * 60.0, sy * 118.0, 1.5), (sx * 117.0, sy * 62.0, 8.0),
                         (sx * 117.0, sy * 17.5, zt), int(28 / nseg))
            deck_path(mb, pts, 9.0, 1.6, 'ramp', S, lod, barrier=lod <= 1, ground_fill=True)
            ramp_piers(mb, pts, S, lod)
    if S and S.kind == 'Damaged' and lod <= 2:
        for yc in (7.0, -7.0):
            beam(mb, (32.5, yc, zt - 1.2), (57.0, yc * 1.05, 1.0), 12.0, mapping=('rows', 'concrete'), ur=None, h=2.2, soot=0.4)
        rubble(mb, (56.0, 0.0, 0.0), 10.0, 3.0, 11, lod=lod, soot=0.5)
    if S and S.kind == 'Ruin' and lod <= 2:
        for i, (x, y) in enumerate(((-60, 0), (0, 40), (60, -3), (-40, -55), (70, 60))):
            rubble(mb, (x, y, 0.0), 11.0, 3.0, 20 + i, lod=lod, soot=0.5)
    return []


# =========================================================================== culture crowns


def crown_a(mb, P, S, lod):
    """Cliff-Terrace: a roof terrace with small plaster domes and a pergola."""
    K = 12 if lod <= 1 else 8
    for x in (-3.0, 3.0):
        frustum(mb, x, 1.5, 0.0, 2.4, 2.0, 2.0, K, ('strip', 'concrete_blank', 0.0, 0.0, 3.0), smooth=True)
        dome(mb, x, 1.5, 2.4, 2.05, 0.8, K, 4 if lod <= 1 else 2, mapping=('rows', 'concrete'))
    if lod <= 1:
        for k in range(5):
            y = -5.0 + k * 1.2
            beam(mb, (-5.5, y, 2.8), (5.5, y, 2.8), 0.15, mapping=('rows', 'concrete'), ur=None)
        for x in (-5.5, 5.5):
            for y in (-5.0, -0.2):
                beam(mb, (x, y, 0.0), (x, y, 2.8), 0.25, mapping=('rows', 'concrete'), ur=None)
        for k in range(3):
            box(mb, (-4.0 + 4.0 * k, -5.8, 0.35), (1.4, 0.4, 0.35), ('strip', 'concrete_blank'))
            hquad(mb, -5.3 + 4.0 * k, -6.1, -2.7 + 4.0 * k, -5.5, 0.71, ('rows', 'lawn'))
    return []


def crown_b(mb, P, S, lod):
    """Lagoon-Weaver: an upswept saddle canopy whose ridge rises to points at both ends."""
    n = 8 if lod <= 1 else 4
    hw = 5.0
    rows = []
    for i in range(n + 1):
        x = -7.0 + 14.0 * i / n
        t = abs(x) / 7.0
        z = 3.2 + 2.6 * t ** 2.2
        rows.append((x, z))
    for i in range(n):
        (xa, za), (xb, zb) = rows[i], rows[i + 1]
        for sy in (-1, 1):
            a, b = (xa, 0.0, za), (xb, 0.0, zb)
            c, d = (xb, sy * hw, zb - 1.6), (xa, sy * hw, za - 1.6)
            pts = [a, b, c, d] if sy < 0 else [b, a, d, c]
            quad_auto(mb, pts, (0.0, 0.0, 0.0), ROOF, [roof_uv(p) for p in pts])
            pts2 = [(p[0], p[1], p[2] - 0.08) for p in pts]
            quad_auto(mb, pts2, (0.0, 0.0, 20.0), ROOF, [roof_uv(p) for p in pts2])
    for x in (-5.0, 0.0, 5.0):
        for sy in (-1, 1):
            beam(mb, (x, sy * (hw - 0.4), 0.0), (x, sy * (hw - 0.4), 3.2 + 2.6 * (abs(x) / 7.0) ** 2.2 - 1.5), 0.3,
                 mapping=('rows', 'concrete'), ur=None)
    return []


def crown_c(mb, P, S, lod):
    """Ring-Builder: a pale drum with a tall dark conical roof and a stone finial."""
    K = 16 if lod <= 1 else 8
    frustum(mb, 0.0, 0.0, 0.0, 4.5, 4.0, 4.0, K, ('strip', 'concrete_blank', 0.0, 0.0, 3.0), smooth=True)
    frustum(mb, 0.0, 0.0, 4.5, 11.5, 4.6, 0.25, K, ('roof',), smooth=True)
    if lod <= 1:
        frustum(mb, 0.0, 0.0, 11.5, 12.6, 0.4, 0.15, 8, ('rows', 'concrete', 1.1), smooth=True)
        annulus(mb, 0.0, 0.0, 4.5, 3.9, 4.7, K, thick=0.25)
    return []
