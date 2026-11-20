# DEMIURGE art - Age1 (Bronze Age) texture sets, generated procedurally with art/tools/dmtex.py.
#
# Every set is 1024 x 1024 PNG, tiling (periodic) unless noted:
#   T_Age1_<Set>_D   base colour, sRGB
#   T_Age1_<Set>_N   tangent-space normal, OpenGL (+Y up); Unreal: tick "Flip Green Channel"
#   T_Age1_<Set>_ORM linear: R ambient occlusion, G roughness, B metallic
#   T_Age1_Trim_M    linear mask: R night-window emissive (the dark "opening" strip), G/B unused
#
# Sets and their real-world tile size (metres per 0..1 UV):
#   Mudbrick 2.0   adobe brick courses half covered by cracked mud render (house and temple walls)
#   Render   3.0   smooth clay plaster / packed clay (granary drums, flat roofs, floors, temple tops)
#   Thatch   2.0   straw thatch, strands down the slope (v toward the ridge), course butts every 0.25 m
#   Earth    4.0   rampart sod: packed soil with dry grass patches
#   Crop     2.0   ripe cereal canopy seen from above (tinted green for growing fields by MI)
#   Trim     -     horizontal trim sheet: strips tile along U only (see STRIPS below)
import numpy as np
import dmtex as T

N = 1024

# trim-sheet strips: name -> (v0, v1) in UV space (v up). Geometry maps parts into these bands.
STRIPS = {
    'planks':   (0.000, 0.156),   # weathered grey timber, grain along U
    'logs':     (0.156, 0.281),   # bark-on round logs, fissures along U
    'stone':    (0.281, 0.406),   # dry-stone coursed fieldstone (a wall face up to ~2 m tall)
    'reed':     (0.406, 0.484),   # reed bundle with lashing bands every 0.5 m
    'wicker':   (0.484, 0.547),   # woven wattle / basket
    'frieze':   (0.547, 0.625),   # painted plaster band (ochre red, blue, cream)
    'opening':  (0.625, 0.688),   # dark interior seen through doors / windows (emissive at night)
    'grain':    (0.688, 0.813),   # loose grain / straw heaps
    'water':    (0.813, 0.891),   # still pool water
    'hide':     (0.891, 1.000),   # hide and cloth (door curtains, awnings)
}
# metres of U per 0..1 of each strip (geometry divides the length along the part by this)
STRIP_TILE = {'planks': 2.0, 'logs': 3.0, 'stone': 3.0, 'reed': 1.0, 'wicker': 1.0, 'frieze': 3.0,
              'opening': 2.0, 'grain': 2.0, 'water': 4.0, 'hide': 2.0}


def _cols(*hexes):
    return [T.hex_lin(h) for h in hexes]


def _finish(name, outdir, alb, h, mpt, rough, ao_strength=60.0, ao_r=0.03, metal=None, mask=None, log=None):
    nrm = T.normal_from_height(h, mpt, N)
    ao = T.cavity_ao(h, N, ao_r, mpt, ao_strength)
    alb = alb * (0.55 + 0.45 * ao[..., None])          # bake a little cavity into the colour
    orm = np.stack([ao, np.clip(rough, 0.02, 1.0), np.zeros_like(ao) if metal is None else metal], -1)
    T.save_png('%s/T_Age1_%s_D.png' % (outdir, name), alb, srgb=True)
    T.save_png('%s/T_Age1_%s_N.png' % (outdir, name), nrm, srgb=False)
    T.save_png('%s/T_Age1_%s_ORM.png' % (outdir, name), orm, srgb=False)
    if mask is not None:
        T.save_png('%s/T_Age1_%s_M.png' % (outdir, name), mask, srgb=False)
    if log is not None:
        log.append(T.stats(name, alb))


# --------------------------------------------------------------------------- sets

def mudbrick(outdir, log):
    t = T.Tex(N, 101)
    u, v = t.u, t.v
    rows, cols = 16, 5                       # ~0.125 m courses, ~0.40 m bricks over a 2 m tile
    vw = v + t.value(8, 1) * 0.004
    rowf = np.floor(vw * rows)
    row = rowf % rows
    roff = t.cell_rand(row.astype(np.int32) % rows, rows, 11)
    x = u * cols + roff + 0.22 * t.value(cols * 3, rows)       # uneven brick lengths, still periodic
    col = np.floor(x) % cols
    fx, fy = x - np.floor(x), vw * rows - rowf
    bid = (row * cols + col).astype(np.int32)
    rnd = t.cell_rand(bid, rows * cols, 1)
    rnd2 = t.cell_rand(bid, rows * cols, 2)
    edge_x = np.minimum(fx, 1 - fx) * 0.40
    edge_y = np.minimum(fy * 1.4, 1 - fy) * 0.125           # tops wear more than bottoms
    edge = np.minimum(edge_x, edge_y)
    wob = t.fbm(48, 48, 4) * 0.012
    brick = T.sstep(0.006, 0.03 + 0.015 * rnd, edge + wob)
    lump = t.fbm(24, 24, 5)
    h_brick = brick * (0.012 + 0.006 * rnd + lump * 0.006) + t.fbm(96, 96, 3) * 0.002
    # mud render: large ragged patches that have flaked off in places
    w = t.fbm(3, 3, 4) * 0.35
    cov = t.fbm(3, 3, 6, u=u + w, v=v + w * 0.6) + 0.24 + 0.05 * t.fbm(40, 40, 3)
    rend = T.sstep(-0.01, 0.025, cov)
    rid = t.ridged(6, 6, 4)
    crack = T.sstep(0.90, 0.97, rid) * T.sstep(0.02, 0.12, cov)
    h_rend = 0.018 + t.fbm(10, 10, 5) * 0.006 - crack * 0.008
    h = T.lerp(h_brick, np.maximum(h_rend, h_brick), rend)
    c_brick_a, c_brick_b, c_mortar, c_rend, c_rend2 = _cols('#8f7254', '#86694c', '#7a644c', '#95785b', '#886b4e')
    cb = T.mix3(c_brick_a, c_brick_b, rnd) * (0.92 + 0.16 * rnd2[..., None]) * (0.9 + 0.2 * lump[..., None] + 0.1)
    cb = T.mix3(c_mortar, cb, brick)
    cr = T.mix3(c_rend, c_rend2, np.clip(t.fbm(6, 6, 5) * 1.6 + 0.5, 0, 1))
    cr = cr * (1.0 - 0.35 * crack[..., None])
    edge_r = T.sstep(0.025, -0.005, cov) * rend              # darker lip where render breaks off
    alb = T.mix3(cb, cr, rend) * (1.0 - 0.12 * edge_r[..., None])
    streak = np.clip(t.value(60, 2) * 0.5 + t.value(140, 3) * 0.3, -1, 1)   # rain streaks down the wall
    alb *= (0.94 + 0.10 * t.fbm(12, 12, 4) + 0.05 * streak)[..., None]
    rough = 0.9 + 0.05 * t.fbm(20, 20, 3) - 0.04 * rend
    _finish('Mudbrick', outdir, alb, h, 2.0, rough, log=log)


def render(outdir, log):
    t = T.Tex(N, 202)
    w = t.fbm(4, 4, 4) * 0.3
    strokes = t.fbm(3, 14, 4, u=t.u + w, v=t.v + w * 0.3)          # trowel sweeps
    crack = T.sstep(0.92, 0.975, t.ridged(5, 5, 4, u=t.u + w * 0.2, v=t.v))
    crack *= T.sstep(0.0, 0.3, t.fbm(4, 4, 3) + 0.1)
    pf1, _, pid, _, _ = t.worley(90, 90)
    peb = T.sstep(0.22, 0.05, pf1) * (t.cell_rand(pid, 8100, 3) > 0.82)
    h = strokes * 0.004 + t.fbm(40, 40, 4) * 0.0015 - crack * 0.004 + peb * 0.002
    c1, c2, c3 = _cols('#b49470', '#a07f5a', '#c2a782')
    alb = T.mix3(c1, c2, np.clip(t.fbm(5, 5, 5) + 0.5, 0, 1))
    alb = T.mix3(alb, c3, np.clip(strokes * 1.2, 0, 1) * 0.5)
    alb = alb * (1 - 0.3 * crack[..., None]) * (1 - 0.12 * peb[..., None])
    rough = 0.82 + 0.08 * t.fbm(10, 10, 3)
    _finish('Render', outdir, alb, h, 3.0, rough, log=log)


def thatch(outdir, log):
    t = T.Tex(N, 303)
    u, v = t.u, t.v
    courses = 6                                   # ~0.33 m courses over a 2 m tile
    vw = v + 0.025 * t.value(10, 1) + 0.012 * t.value(40, 2)
    rowf = np.floor(vw * courses)
    row = (rowf % courses).astype(np.int32)
    fy = vw * courses - rowf
    # bundles across each course: uneven widths, each bundle sits a little higher or lower
    bx = u * 16 + t.cell_rand(row, courses, 21) + 0.35 * t.value(32, courses)
    bid = (row * 16 + (np.floor(bx) % 16)).astype(np.int32)
    bfx = bx - np.floor(bx)
    br = t.cell_rand(bid, courses * 16, 22)
    br2 = t.cell_rand(bid, courses * 16, 23)
    fy2 = np.clip(fy + (br - 0.5) * 0.25, 0, 1)
    bulge = np.sin(np.pi * bfx) ** 0.5
    strands = t.value(380, 7) * 0.55 + t.value(160, 4) * 0.3 + t.value(900, 11) * 0.15
    h = (1.0 - fy2) ** 1.3 * 0.04 + bulge * 0.006 + strands * 0.009 + t.fbm(6, 6, 3) * 0.01
    butt = T.sstep(0.22, 0.0, fy2)
    c_light, c_mid, c_dark, c_grey, c_moss = _cols('#a88a5c', '#8a6d48', '#4a3a28', '#8a8072', '#5a5c3a')
    base = T.mix3(c_mid, c_light, np.clip(strands * 0.9 + 0.45 + (br - 0.5) * 0.3, 0, 1))
    weather = np.clip(t.fbm(3, 3, 4) * 1.4 + 0.45 + (br2 - 0.5) * 0.3, 0, 1)
    base = T.mix3(base, c_grey, weather * 0.35)
    base = T.mix3(base, c_dark, np.clip(butt * 0.35 + (1 - bulge) * 0.12, 0, 1))
    moss = T.sstep(0.28, 0.5, t.fbm(5, 5, 5) + 0.08 * t.value(60, 60))
    base = T.mix3(base, c_moss, moss * 0.2)
    rough = 0.9 + 0.06 * strands
    _finish('Thatch', outdir, base, h, 2.0, rough, ao_strength=30.0, ao_r=0.06, log=log)


def earth(outdir, log):
    """Rampart / bank sod: dry grass over packed earth, soft mottled transitions."""
    t = T.Tex(N, 404)
    soil = t.fbm(16, 16, 5)
    pf1, _, pid, _, _ = t.worley(60, 60)
    peb = T.sstep(0.3, 0.1, pf1) * (t.cell_rand(pid, 3600, 5) > 0.88) * 0.4
    cover = t.fbm(5, 5, 5) * 0.7 + t.fbm(40, 40, 3) * 0.5 + 0.3
    grass_m = T.sstep(-0.35, 0.35, cover)
    blades = np.abs(t.value(400, 60)) + np.abs(t.value(150, 260)) * 0.6
    h = soil * 0.01 + peb * 0.008 + grass_m * (0.008 + blades * 0.012)
    c_soil, c_soil2, c_peb, c_g1, c_g2 = _cols('#6c5940', '#5e4c37', '#77705f', '#626537', '#7e7547')
    alb = T.mix3(c_soil, c_soil2, np.clip(soil + 0.5, 0, 1))
    alb = T.mix3(alb, c_peb, peb)
    g = T.mix3(c_g1, c_g2, np.clip(t.fbm(9, 9, 3) + 0.5 + blades * 0.25, 0, 1))
    alb = T.mix3(alb, g, grass_m * 0.9)
    rough = 0.92 - 0.05 * peb
    _finish('Earth', outdir, alb, h, 4.0, rough, log=log)


def soil(outdir, log):
    """Bare field soil: clods, crumbs and a few stones (field plots, ploughed furrows, field margins)."""
    t = T.Tex(N, 707)
    f1, f2, cid, _, _ = t.worley(48, 48)
    clod = np.clip(1.0 - f1 * 1.3, 0, 1) ** 0.8
    r = t.cell_rand(cid, 48 * 48, 3)
    crumbs = t.fbm(128, 128, 3)
    pf1, _, pid, _, _ = t.worley(30, 30)
    stone = T.sstep(0.25, 0.08, pf1) * (t.cell_rand(pid, 900, 4) > 0.9)
    damp = t.fbm(4, 4, 4)
    h = clod * (0.015 + 0.01 * r) + crumbs * 0.004 + stone * 0.01
    c1, c2, c3, cs = _cols('#6b543b', '#4f3d2b', '#7d6549', '#7c7466')
    alb = T.mix3(c2, c1, np.clip(clod * 0.9 + r * 0.3, 0, 1))
    alb = T.mix3(alb, c3, np.clip(damp + 0.3, 0, 1) * 0.4)
    alb = T.mix3(alb, cs, stone)
    alb *= (0.9 + 0.2 * crumbs)[..., None]
    rough = 0.93 - 0.06 * stone
    _finish('Soil', outdir, alb, h, 3.0, rough, log=log)


def crop(outdir, log):
    t = T.Tex(N, 505)
    f1, f2, cid, _, _ = t.worley(72, 72, jitter=1.0, aspect=0.6)
    head = np.clip(1.0 - f1 * 1.6, 0, 1) ** 1.5
    lean = t.fbm(3, 3, 3)                          # wind-combed patches
    r = t.cell_rand(cid, 72 * 72, 6)
    rows_ = np.abs(np.sin(np.pi * (t.v * 10 + 0.05 * t.value(6, 3))))
    h = head * (0.03 + 0.02 * r) + lean * 0.02 + rows_ * 0.01
    c_gold, c_pale, c_dark, c_green = _cols('#c49a4a', '#d8b56c', '#5e4826', '#8a8a42')
    alb = T.mix3(c_gold, c_pale, np.clip(r * 0.8 + lean, 0, 1))
    alb = T.mix3(c_dark, alb, np.clip(head * 1.6, 0, 1) * (0.75 + 0.25 * rows_))
    alb = T.mix3(alb, c_green, T.sstep(0.2, 0.5, t.fbm(4, 4, 4)) * 0.25)
    rough = 0.85 + 0.1 * (1 - head)
    _finish('Crop', outdir, alb, h, 2.0, rough, ao_strength=30.0, ao_r=0.04, log=log)


def trim(outdir, log):
    t = T.Tex(N, 606)
    u, v = t.u, t.v
    alb = np.zeros((N, N, 3), np.float32)
    h = np.zeros((N, N), np.float32)
    rough = np.full((N, N), 0.85, np.float32)
    mask = np.zeros((N, N, 3), np.float32)

    def band(name):
        v0, v1 = STRIPS[name]
        r0, r1 = int(round(v0 * N)), int(round(v1 * N))
        s = np.zeros((N,), bool)
        s[r0:r1] = True
        sv = np.clip((v - v0) / (v1 - v0), 0, 1)       # 0..1 across the strip
        return s, sv

    # planks: 4 boards across the strip, grain along u
    s, sv = band('planks')
    board = np.floor(sv * 4)
    bf = sv * 4 - board
    grain = t.value(6, 260) * 0.5 + t.value(3, 520) * 0.5
    knots = T.sstep(0.35, 0.1, t.worley(14, 30)[0]) * 0.5
    hh = T.sstep(0.0, 0.06, np.minimum(bf, 1 - bf)) * 0.006 + grain * 0.0015 - knots * 0.002
    ca, cb, cd = _cols('#7f776b', '#5f584e', '#2b2620')
    col = T.mix3(cb, ca, np.clip(grain * 1.2 + 0.5 + 0.2 * (board % 2), 0, 1))
    col = T.mix3(col, cd, np.clip(1 - T.sstep(0.0, 0.05, np.minimum(bf, 1 - bf)), 0, 1) * 0.8 + knots * 0.6)
    alb[s], h[s], rough[s] = col[s], hh[s], 0.82

    # logs: bark fissures along u
    s, sv = band('logs')
    fis = 1.0 - np.abs(t.value(3, 700)) ** 0.5 * 0.7 - np.abs(t.value(6, 300)) * 0.3
    plate = t.value(12, 160)
    hh = fis * 0.012 + plate * 0.003
    ca, cb, cc = _cols('#6a5c4c', '#3c332a', '#8b8478')
    col = T.mix3(cb, ca, np.clip(fis * 1.4, 0, 1))
    col = T.mix3(col, cc, T.sstep(0.1, 0.4, t.fbm(6, 3, 3)) * 0.5)
    alb[s], h[s], rough[s] = col[s], hh[s], 0.9

    # stone: coursed fieldstone
    s, sv = band('stone')
    f1, f2, sid, _, _ = t.worley(26, 28, jitter=0.8, aspect=0.45)
    stn = T.sstep(0.0, 0.12, f2 - f1)
    sr = t.cell_rand(sid, 26 * 28, 7)
    hh = stn * (0.03 + 0.02 * sr) + t.fbm(60, 60, 3) * 0.004
    ca, cb, cm = _cols('#8e8574', '#6f6656', '#40392d')
    col = T.mix3(cb, ca, sr) * (0.9 + 0.2 * t.fbm(30, 30, 3))[..., None]
    col = T.mix3(cm, col, stn)
    alb[s], h[s], rough[s] = col[s], hh[s], 0.88

    # reed bundle: stems along u, lashing every 0.5 m (1 m tile -> 2 bands)
    s, sv = band('reed')
    stems = np.abs(t.value(2, 900)) ** 0.7 * 0.7 + np.abs(t.value(5, 400)) * 0.3
    lash = T.sstep(0.02, 0.0, np.abs((u % 1.0) - 0.5) - 0.47) * 0.7
    hh = stems * 0.004 + lash * 0.006
    ca, cb, cl = _cols('#a8955e', '#857244', '#5e4b30')
    col = T.mix3(cb, ca, np.clip(stems * 1.3, 0, 1))
    col = T.mix3(col, cl, lash)
    alb[s], h[s], rough[s] = col[s], hh[s], 0.8

    # wicker weave
    s, sv = band('wicker')
    wa = np.sin(u * np.pi * 2 * 40) * np.sin(sv * np.pi * 6)
    hh = (wa * 0.5 + 0.5) * 0.008
    col = T.mix3(*_cols('#4f3f2c', '#8a7350'), np.clip(wa * 0.5 + 0.5, 0, 1))
    alb[s], h[s], rough[s] = col[s], hh[s], 0.85

    # painted frieze: cream band with ochre-red zigzag and blue dots, weathered
    s, sv = band('frieze')
    zz = np.abs(((u * 24) % 1.0) - 0.5) * 2.0
    zig = (np.abs(sv - (0.3 + 0.4 * zz)) < 0.09)
    stripe = (sv < 0.14) | (sv > 0.86)
    dots = (np.hypot(((u * 24) % 1.0) - 0.5, (sv - 0.5) * 0.4) < 0.08) & ~zig
    cream, red, blue = _cols('#cdb58a', '#8a3a24', '#2e566f')
    col = np.broadcast_to(cream, (N, N, 3)).copy()
    col[zig | stripe] = red
    col[dots] = blue
    wear = T.sstep(0.15, 0.35, t.fbm(20, 20, 4) + 0.1)
    col = T.mix3(col, T.hex_lin('#a8875e'), wear * 0.7)
    alb[s], h[s], rough[s] = col[s], (wear * 0.002)[s], 0.8

    # opening: dark interior with a faint warm gradient; emissive mask
    s, sv = band('opening')
    col = T.mix3(*_cols('#0d0907', '#2a1c12'), np.clip(sv * 0.6 + t.fbm(8, 2, 3) * 0.5, 0, 1))
    alb[s], h[s], rough[s] = col[s], 0.0, 0.95
    mask[s, :, 0] = np.clip(0.75 + 0.25 * t.fbm(6, 2, 3), 0, 1)[s]

    # grain heap
    s, sv = band('grain')
    f1, _, gid, _, _ = t.worley(256, 256)
    gr = np.clip(1 - f1 * 1.8, 0, 1)
    rr = t.cell_rand(gid, 256 * 256, 8)
    hh = gr * 0.003 + t.fbm(20, 6, 3) * 0.004
    col = T.mix3(*_cols('#a07a36', '#d6b066'), np.clip(rr * 0.7 + gr * 0.4, 0, 1))
    alb[s], h[s], rough[s] = col[s], hh[s], 0.8

    # water
    s, sv = band('water')
    rip = t.fbm(16, 4, 4)
    col = T.mix3(*_cols('#13292c', '#24474a'), np.clip(rip + 0.5, 0, 1))
    alb[s], h[s], rough[s] = col[s], (rip * 0.002)[s], 0.06

    # hide / cloth
    s, sv = band('hide')
    weave = np.sin(u * np.pi * 2 * 300) * np.sin(sv * np.pi * 60) * 0.3 + t.fbm(12, 6, 4)
    col = T.mix3(*_cols('#5c3f28', '#8a6646'), np.clip(weave + 0.5, 0, 1))
    alb[s], h[s], rough[s] = col[s], (weave * 0.002)[s], 0.9

    _finish('Trim', outdir, alb, h, 2.0, rough, mask=mask, log=log)


ALL = [mudbrick, render, thatch, earth, soil, crop, trim]


def build_all(outdir):
    log = []
    for f in ALL:
        f(outdir, log)
    return log
