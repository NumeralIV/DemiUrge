# DEMIURGE art / wonders-b - procedural atlas textures (numpy, runs inside Blender's Python).
# Builds on art/tools/dmtex.py (periodic noise, colour helpers, PNG writer).
import os
import sys
import math
import zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import dmtex  # noqa: E402
from dmtex import hex_lin, sstep, blur  # noqa: E402

BH, BW = 512, 2048          # band texels (v, u)


class F:
    """Field helper for one band: u, v in 0..1 (periodic), x, y in metres."""

    def __init__(self, U, V, seed):
        self.U, self.V = U, V
        uu = ((np.arange(BW, dtype=np.float32) + 0.5) / BW)
        vv = ((np.arange(BH, dtype=np.float32) + 0.5) / BH)
        self.u = np.ascontiguousarray(np.broadcast_to(uu[None, :], (BH, BW)))
        self.v = np.ascontiguousarray(np.broadcast_to(vv[:, None], (BH, BW)))
        self.x = self.u * U
        self.y = self.v * V
        self.t = dmtex.Tex(8, seed)
        self.rng = np.random.default_rng(seed + 17)
        self.texel = V / BH

    def noise(self, cx, cy=None, octaves=4, gain=0.5, u=None, v=None):
        """Periodic fbm, feature size cx (u) by cy (v) metres, range about -1..1."""
        cy = cx if cy is None else cy
        px = max(1, int(round(self.U / cx)))
        py = max(1, int(round(self.V / cy)))
        u = self.u if u is None else u
        v = self.v if v is None else v
        s = np.zeros_like(u)
        amp, tot = 1.0, 0.0
        for o in range(octaves):
            s += amp * self.t.value(px << o, py << o, u, v)
            tot += amp
            amp *= gain
        return s / tot

    def ridged(self, cx, octaves=4):
        px = max(1, int(round(self.U / cx)))
        py = max(1, int(round(self.V / cx)))
        s = np.zeros_like(self.u)
        amp, tot = 1.0, 0.0
        for o in range(octaves):
            s += amp * (1.0 - np.abs(self.t.value(px << o, py << o, self.u, self.v)))
            tot += amp
            amp *= 0.5
        return s / tot

    def worley(self, cell, jitter=0.85, cy=None):
        cy = cell if cy is None else cy
        gx = max(1, int(round(self.U / cell)))
        gy = max(1, int(round(self.V / cy)))
        aspect = (self.U / gx) / (self.V / gy)
        return self.t.worley(gx, gy, self.u, self.v, jitter=jitter, aspect=aspect)


def col(h):
    return hex_lin(h)


def base(Fd, c, rough=0.8, metal=0.0):
    return dict(alb=np.ones((BH, BW, 3), np.float32) * col(c) if isinstance(c, str) else c,
                hgt=np.zeros((BH, BW), np.float32), rough=np.full((BH, BW), rough, np.float32),
                metal=np.full((BH, BW), metal, np.float32), em=np.zeros((BH, BW), np.float32),
                hue=np.zeros((BH, BW), np.float32), tint=np.zeros((BH, BW), np.float32))


def mix(a, b, t):
    return a + (b - a) * t[..., None]


# ------------------------------------------------------------------------------------------ generators

def g_blocks(Fd, bw, bh, c, mortar_c, mortar=0.012, var=0.12, huevar=0.04, rough=0.82, mrough=0.95,
             depth=0.008, stagger=0.5, dirt=0.18, speck=0.10, chip=0.25, metal=0.0, tint=0.0, edge_soft=0.002):
    cols = max(1, int(round(Fd.U / bw)))
    rows = max(1, int(round(Fd.V / bh)))
    if stagger and rows % 2:
        rows += 1
    r = np.floor(Fd.v * rows).astype(np.int32)
    off = (r % 2) * stagger
    fu = Fd.u * cols + off
    c_i = np.floor(fu).astype(np.int32) % cols
    lu = fu - np.floor(fu)
    lv = Fd.v * rows - r
    bwm, bhm = Fd.U / cols, Fd.V / rows
    du = np.minimum(lu, 1 - lu) * bwm
    dv = np.minimum(lv, 1 - lv) * bhm
    e = np.minimum(du, dv) + Fd.noise(0.05, octaves=3) * mortar * 0.35
    m = sstep(mortar * 0.5 - edge_soft, mortar * 0.5 + edge_soft * 2, e)
    ids = r * cols + c_i
    n = rows * cols
    rr = Fd.rng.random((n, 4)).astype(np.float32)
    R = rr[ids]
    cc = col(c)
    alb = cc[None, None, :] * (1.0 + var * (R[..., 0:1] - 0.5) * 2.0)
    alb = alb * (1.0 + huevar * (R[..., 1:2] - 0.5) * 2.0 * np.array([1.0, 0.0, -1.0], np.float32))
    sp = Fd.noise(0.03, octaves=3)
    alb = alb * (1.0 + speck * sp[..., None])
    if dirt:
        d = np.clip(Fd.noise(1.5, octaves=4) * 1.4, 0, 1)
        alb = alb * (1.0 - dirt * d[..., None])
    alb = mix(np.ones_like(alb) * col(mortar_c), alb, m)
    hgt = m * depth + (R[..., 2] - 0.5) * depth * 0.35 * m + sp * 0.0012
    if chip:
        f1, f2, _, _, _ = Fd.worley(max(bh * 0.6, 0.02))
        pit = sstep(0.25, 0.0, f1) * (Fd.noise(0.4, octaves=2) > 0.25)
        hgt = hgt - pit * depth * chip * m
    rough_a = mrough + (rough + (R[..., 3] - 0.5) * 0.08 - mrough) * m
    out = dict(alb=alb.astype(np.float32), hgt=hgt.astype(np.float32), rough=rough_a.astype(np.float32),
               metal=np.full((BH, BW), metal, np.float32), em=np.zeros((BH, BW), np.float32),
               hue=np.zeros((BH, BW), np.float32), tint=np.full((BH, BW), tint, np.float32))
    out['_mask'] = m
    out['_rand'] = R
    return out


def g_metal(Fd, c, rough=0.35, rvar=0.12, tarnish_c=None, tarnish=0.35, scale=0.25, tint=1.0, metal=1.0, streak=False):
    d = base(Fd, c, rough, metal)
    n1 = Fd.noise(scale, octaves=5)
    n2 = Fd.noise(scale * 0.15, octaves=3)
    if streak:
        n1 = 0.6 * n1 + 0.4 * Fd.noise(scale * 4, scale * 0.2, octaves=3)
    tc = col(tarnish_c) if tarnish_c else col(c) * 0.55
    t = np.clip(n1 * 1.5 + 0.1, 0, 1) * tarnish
    d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * tc, t) * (1 + 0.05 * n2[..., None])
    d['rough'] = np.clip(rough + rvar * n1 + 0.25 * t, 0.04, 1)
    d['metal'] = np.clip(metal - 0.4 * t, 0, 1)
    d['hgt'] = n2 * 0.0004
    d['tint'][:] = tint
    return d


def g_concrete(Fd, c, panel=(4.0, 2.0), var=0.035, streaks=0.10, rough=0.86, tint=0.0, board=0.0, tie=True):
    d = g_blocks(Fd, panel[0], panel[1], c, '#9a9892', mortar=0.018, var=var, huevar=0.01, rough=rough,
                 mrough=0.9, depth=0.004, stagger=0.0, dirt=0.10, speck=0.06, chip=0.0, tint=tint)
    m = d['_mask']
    if streaks:
        s = np.clip(Fd.noise(0.35, 3.5, octaves=3) * 1.6, 0, 1)
        d['alb'] = d['alb'] * (1.0 - streaks * s[..., None])
        d['rough'] = d['rough'] + 0.04 * s
    if board:
        b = np.sin(Fd.y / 0.15 * math.pi) * 0.5 + 0.5
        d['alb'] = d['alb'] * (1.0 - board * (b ** 8)[..., None])
        d['hgt'] = d['hgt'] - 0.0008 * (b ** 8)
    if tie:
        # form-tie holes on a 0.6 m grid inside panels
        gx = (Fd.x / 0.6) % 1.0 - 0.5
        gy = (Fd.y / 0.6) % 1.0 - 0.5
        hole = sstep(0.06, 0.03, np.sqrt(gx * gx + gy * gy)) * m
        d['alb'] = d['alb'] * (1.0 - 0.25 * hole[..., None])
        d['hgt'] = d['hgt'] - hole * 0.003
    return d


def g_glass_grid(Fd, cols, rows, glass_c, frame_c, rib=0.09, sub=2, emiss=1.0, frame_metal=0.55, dark=0.12,
                 frame_rough=0.45, lvar=0.35):
    """cols / rows panes per band. Thin secondary mullions split panes into sub x sub."""
    fu = Fd.u * cols
    fv = Fd.v * rows
    pw, ph = Fd.U / cols, Fd.V / rows
    lu, lv = fu - np.floor(fu), fv - np.floor(fv)
    du = np.minimum(lu, 1 - lu) * pw
    dv = np.minimum(lv, 1 - lv) * ph
    main = sstep(rib * 0.5, rib * 0.5 + 0.012, np.minimum(du, dv))
    su, sv = (fu * sub) % 1.0, (fv * sub) % 1.0
    ds = np.minimum(np.minimum(su, 1 - su) * pw / sub, np.minimum(sv, 1 - sv) * ph / sub)
    secondary = sstep(rib * 0.22, rib * 0.22 + 0.01, ds)
    glass = main * secondary
    ids = (np.floor(fv).astype(np.int32) % rows) * cols + (np.floor(fu).astype(np.int32) % cols)
    R = Fd.rng.random((rows * cols, 3)).astype(np.float32)[ids]
    gc = col(glass_c)[None, None, :] * (0.85 + 0.3 * R[..., 0:1])
    alb = mix(np.ones((BH, BW, 3), np.float32) * col(frame_c), gc, glass)
    grime = np.clip(Fd.noise(2.0, octaves=3) * 1.5, 0, 1)
    alb = alb * (1 - 0.15 * grime[..., None])
    d = base(Fd, '#000000')
    d['alb'] = alb.astype(np.float32)
    d['rough'] = (frame_rough + (0.06 + 0.1 * R[..., 1] + 0.1 * grime - frame_rough) * glass).astype(np.float32)
    d['metal'] = (frame_metal * (1 - glass)).astype(np.float32)
    d['hgt'] = ((1 - main) * 0.03 + (1 - secondary) * 0.008 - glass * 0.002 * R[..., 2]).astype(np.float32)
    lit = (R[..., 2] > dark).astype(np.float32) * (1.0 - lvar + lvar * R[..., 1])
    d['em'] = (glass * lit * emiss).astype(np.float32)
    d['_glass'] = glass
    return d


def windows(d, Fd, spacing, ww, wh, sill, arch=True, glass_c='#2a2f33', frame_c='#2e2e30', mull=0.5, emiss=1.0,
            reveal=0.18, recess=0.12, lit_frac=0.8, hue=0.0, frame_metal=0.4):
    """Punch windows into wall fields d (in place)."""
    xx = (Fd.x % spacing) - spacing * 0.5
    y = Fd.y
    top = sill + wh
    inside_rect = (np.abs(xx) < ww * 0.5) & (y > sill) & (y < top - (ww * 0.5 if arch else 0))
    if arch:
        cy = top - ww * 0.5
        inside_arch = ((xx * xx + (y - cy) ** 2) < (ww * 0.5) ** 2) & (y >= cy)
        inside = inside_rect | inside_arch
        # distance-ish to opening edge for the frame
        dist_side = ww * 0.5 - np.abs(xx)
        dist_arch = ww * 0.5 - np.sqrt(xx * xx + np.maximum(y - cy, 0) ** 2)
        dist = np.where(y >= cy, dist_arch, np.minimum(dist_side, y - sill))
    else:
        inside = inside_rect
        dist = np.minimum(np.minimum(ww * 0.5 - np.abs(xx), y - sill), top - y)
    inside = inside.astype(np.float32)
    frame = inside * (dist < 0.07).astype(np.float32)
    gx = (xx / mull) % 1.0
    gy = ((y - sill) / mull) % 1.0
    mul = ((np.minimum(gx, 1 - gx) * mull < 0.025) | (np.minimum(gy, 1 - gy) * mull < 0.025)).astype(np.float32) * inside
    glass = inside * (1 - frame) * (1 - mul)
    # surround / reveal (stone dressing)
    surround = ((dist > -reveal) & (dist <= 0)).astype(np.float32) * (1 - inside)
    win_id = np.floor(Fd.x / spacing).astype(np.int32)
    nwin = int(round(Fd.U / spacing))
    R = Fd.rng.random((nwin + 2, 2)).astype(np.float32)[win_id % (nwin + 1)]
    lit = (R[..., 0] < lit_frac).astype(np.float32) * (0.6 + 0.4 * R[..., 1])
    d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col(glass_c), glass)
    d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col(frame_c), np.clip(frame + mul, 0, 1))
    d['alb'] = d['alb'] * (1 - 0.18 * surround[..., None])
    d['rough'] = np.where(glass > 0.5, 0.08, d['rough']).astype(np.float32)
    d['metal'] = np.maximum(d['metal'], (frame + mul) * frame_metal).astype(np.float32)
    d['hgt'] = (d['hgt'] - inside * recess + (frame + mul) * recess * 0.6 + surround * 0.01).astype(np.float32)
    d['em'] = np.maximum(d['em'], glass * lit * emiss).astype(np.float32)
    d['hue'] = np.where(glass > 0.5, hue, d['hue']).astype(np.float32)
    return d


def g_grass(Fd, c1, c2, c3=None, rough=0.92, stripes=0.0, clumps=0.4):
    n1 = Fd.noise(1.2, octaves=5)
    n2 = Fd.noise(0.12, octaves=3)
    f1, f2, ids, _, _ = Fd.worley(0.35)
    a = np.clip(0.5 + 0.6 * n1, 0, 1)
    alb = mix(np.ones((BH, BW, 3), np.float32) * col(c1), np.ones((BH, BW, 3), np.float32) * col(c2), a)
    if c3:
        b = np.clip((Fd.noise(3.0, octaves=3) - 0.25) * 2.5, 0, 1)
        alb = mix(alb, np.ones_like(alb) * col(c3), b * 0.7)
    cl = np.clip(1 - f1 * 1.4, 0, 1) * clumps
    alb = alb * (0.85 + 0.25 * cl[..., None] + 0.1 * n2[..., None])
    if stripes:
        s = (np.floor(Fd.y / (Fd.V / 2)) % 2).astype(np.float32)
        alb = alb * (1 + stripes * (s - 0.5))[..., None]
    d = base(Fd, '#000000', rough)
    d['alb'] = alb.astype(np.float32)
    d['hgt'] = (cl * 0.02 + n2 * 0.006).astype(np.float32)
    d['rough'] = (rough - 0.05 * cl).astype(np.float32)
    return d


def g_foliage(Fd, palette, leaf=0.22, gap_c='#1f2a16', rough=0.78, bloom=None):
    """Leaf / blossom canopy texture. palette: list of (hex, weight)."""
    f1, f2, ids, _, _ = Fd.worley(leaf)
    f1b, f2b, ids2, _, _ = Fd.worley(leaf * 3.5)
    w = np.array([p[1] for p in palette], np.float32)
    w = np.cumsum(w / w.sum())
    nc = int(ids2.max()) + 1
    rr = Fd.rng.random(nc).astype(np.float32)
    pick = np.searchsorted(w, rr)[ids2]
    cols = np.stack([col(p[0]) for p in palette])
    alb = cols[pick]
    r2 = Fd.rng.random(int(ids.max()) + 1).astype(np.float32)[ids]
    alb = alb * (0.75 + 0.45 * r2[..., None])
    edge = sstep(0.0, 0.25, f2 - f1)
    big = sstep(0.0, 0.35, f2b - f1b)
    shade = np.clip(edge * 0.7 + big * 0.5, 0, 1)
    alb = mix(np.ones_like(alb) * col(gap_c), alb, 0.25 + 0.75 * shade)
    n = Fd.noise(1.5, octaves=3)
    alb = alb * (1 + 0.12 * n[..., None])
    d = base(Fd, '#000000', rough)
    d['alb'] = alb.astype(np.float32)
    d['hgt'] = (edge * 0.03 + big * 0.06 + (1 - f1 / (f1.max() + 1e-6)) * 0.02).astype(np.float32)
    d['rough'] = (rough + 0.12 * (1 - shade)).astype(np.float32)
    return d


def g_blossom(Fd, c_main, c_light, c_deep, c_leaf, rough=0.8):
    """Flowering canopy (verifier fix): soft clumps of florets instead of hard-edged Voronoi cells, which read as
    a stained-glass mosaic at 40 m. Clumps ~0.9 m are broad domes (bright centre, shadowed leafy hollows, no
    outlines), colour drifts in soft patches, florets are a fine speckle that averages out with distance. The
    height is kept low so the tangent-space normal map cannot facet the low-poly crowns."""
    f1, _, _, _, _ = Fd.worley(0.9, jitter=0.9)
    clump = 1.0 - sstep(0.15, 0.8, f1)                      # 1 at clump centres, 0 in the hollows between
    g1, _, _, _, _ = Fd.worley(0.3, jitter=0.9)
    sub = 1.0 - sstep(0.2, 0.85, g1)
    flo1, flo2, fid, _, _ = Fd.worley(0.055, jitter=0.95)
    floret = sstep(0.0, 0.5, flo2 - flo1)
    fr = Fd.rng.random(int(fid.max()) + 1).astype(np.float32)[fid]
    n1 = Fd.noise(0.45, octaves=3)
    n2 = Fd.noise(0.25, octaves=2)
    ones = np.ones((BH, BW, 3), np.float32)
    alb = mix(ones * col(c_main), ones * col(c_light), sstep(-0.15, 0.45, n1) * (0.5 + 0.5 * clump))
    alb = mix(alb, ones * col(c_deep), sstep(0.1, 0.6, -n2) * 0.5)
    alb = alb * (0.86 + 0.24 * fr[..., None] * floret[..., None])
    hollow = np.clip((1.0 - clump) * (1.0 - 0.5 * sub) + 0.12 * Fd.noise(0.15, octaves=2), 0, 1)
    alb = mix(alb, ones * col(c_leaf), sstep(0.45, 0.95, hollow) * 0.7)
    alb = alb * (0.7 + 0.3 * np.clip(clump * 0.6 + sub * 0.4, 0, 1))[..., None]
    d = base(Fd, '#000000', rough)
    d['alb'] = alb.astype(np.float32)
    d['hgt'] = (clump * 0.025 + sub * 0.012 + floret * 0.004).astype(np.float32)
    d['rough'] = (rough + 0.1 * hollow).astype(np.float32)
    return d


def g_water(Fd, c, rough=0.03, ripple=0.004, em=0.0, hue=1.0, foam=0.0):
    d = base(Fd, c, rough)
    n = Fd.noise(0.6, octaves=5)
    n2 = Fd.noise(0.15, octaves=3)
    d['hgt'] = (n * ripple + n2 * ripple * 0.4).astype(np.float32)
    d['alb'] = d['alb'] * (1 + 0.08 * n[..., None])
    if foam:
        f = np.clip((Fd.noise(0.8, octaves=4) - 0.35) * 3, 0, 1) * foam
        d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col('#e8efee'), f)
        d['rough'] = d['rough'] + f * 0.5
    d['em'][:] = em
    d['hue'][:] = hue
    return d


def g_fall(Fd):
    """Falling water: u runs DOWN the fall (tiles), v across the sheet."""
    s1 = Fd.noise(3.0, 0.05, octaves=4)
    s2 = Fd.noise(1.0, 0.22, octaves=3)
    f = np.clip(0.42 + 1.6 * s1 + 0.5 * s2, 0, 1)
    edge = np.minimum(Fd.v, 1 - Fd.v)
    f = f * sstep(0.0, 0.15, edge) + (1 - sstep(0.0, 0.15, edge)) * 0.25
    d = base(Fd, '#000000', 0.25)
    d['alb'] = mix(np.ones((BH, BW, 3), np.float32) * col('#6f9ea3'), np.ones((BH, BW, 3), np.float32) * col('#f2f6f4'), f)
    d['rough'] = (0.08 + 0.45 * f).astype(np.float32)
    d['hgt'] = (s1 * 0.02).astype(np.float32)
    d['em'] = (0.25 * f).astype(np.float32)
    d['hue'][:] = 1.0
    return d


def g_rock(Fd, c, c2, rough=0.9, strata=0.4):
    r = Fd.ridged(2.5, octaves=5)
    n = Fd.noise(4.0, octaves=5)
    st = np.sin((Fd.y / 0.9 + n * 1.5) * math.pi) * 0.5 + 0.5
    a = np.clip(0.5 + n * 0.8 + (st - 0.5) * strata, 0, 1)
    alb = mix(np.ones((BH, BW, 3), np.float32) * col(c), np.ones((BH, BW, 3), np.float32) * col(c2), a)
    alb = alb * (0.8 + 0.4 * r[..., None])
    d = base(Fd, '#000000', rough)
    d['alb'] = alb.astype(np.float32)
    d['hgt'] = (r * 0.12 + st * 0.03).astype(np.float32)
    return d


def g_solar(Fd, cell=(1.0, 2.0), frame='#c8ccd0', c='#2c3f5a'):
    d = g_blocks(Fd, cell[0], cell[1], c, frame, mortar=0.05, var=0.05, huevar=0.02, rough=0.12, mrough=0.35,
                 depth=0.01, stagger=0.0, dirt=0.05, speck=0.02, chip=0.0)
    # busbar lines across cells
    b = ((Fd.x / (cell[0] / 6.0)) % 1.0)
    bus = (np.minimum(b, 1 - b) < 0.03).astype(np.float32) * d['_mask']
    d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col('#5a6a7c'), bus * 0.6)
    d['metal'] = (0.25 + 0.5 * (1 - d['_mask'])).astype(np.float32)
    return d


def g_spot(Fd, c, hue=0.0, base_c='#3a3a3c'):
    """Light spot band: the emissive light colour lives only in a small spot at u < 0.03, v = 0.5 (light geometry
    is mapped onto that spot), the rest of the band is a dull housing. Keeps the atlas average emission tiny so thin
    parts sharing the atlas never glow at far mips."""
    d = g_metal(Fd, base_c, rough=0.5, tarnish=0.1, metal=0.4)
    spot = ((Fd.u < 0.03) & (np.abs(Fd.v - 0.5) < 0.12)).astype(np.float32)
    d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col(c), spot)
    d['rough'] = np.where(spot > 0, 0.25, d['rough']).astype(np.float32)
    d['metal'] = d['metal'] * (1 - spot)
    d['em'] = spot
    d['hue'] = spot * hue
    d['hgt'] = d['hgt'] * (1 - spot)
    return d


def finish(d):
    for k in ('_mask', '_rand', '_glass'):
        d.pop(k, None)
    return d


# ------------------------------------------------------------------------------------------ band table

def gen_band(asset, slot, name, V, seed):
    U = 4.0 * V
    Fd = F(U, V, seed)
    k = name
    # ---- choir
    if k == 'brick':
        return g_blocks(Fd, 0.225, 0.075, '#8a4632', '#b8aa98', mortar=0.011, var=0.16, huevar=0.08, rough=0.86, depth=0.007, dirt=0.22)
    if k == 'brick_soot':
        d = g_blocks(Fd, 0.225, 0.075, '#6e3a2b', '#7a7068', mortar=0.011, var=0.14, huevar=0.06, rough=0.9, depth=0.007, dirt=0.45)
        s = np.clip(Fd.noise(0.4, 2.5, octaves=4) * 1.3 + 0.2, 0, 1)
        d['alb'] = d['alb'] * (1 - 0.55 * s[..., None])
        return d
    if k == 'ashlar':
        return g_blocks(Fd, 1.6, 0.5, '#b9a68a', '#8f8372', mortar=0.012, var=0.07, huevar=0.03, rough=0.8, depth=0.012, dirt=0.2, tint=0.4)
    if k == 'slate':
        return g_blocks(Fd, 0.3, 0.25, '#4a4e52', '#2c2e31', mortar=0.01, var=0.18, rough=0.7, depth=0.01, dirt=0.15, tint=0.5)
    if k == 'dome_glass':
        # verifier fix: no dark panes and a narrow brightness spread (the random panes read as a checkerboard)
        return g_glass_grid(Fd, cols=8, rows=2, glass_c='#4a4234', frame_c='#2a2a2c', rib=0.16, sub=1, emiss=1.0, dark=0.0, lvar=0.12)
    if k == 'lantern_glass':
        return g_glass_grid(Fd, cols=8, rows=1, glass_c='#9a8462', frame_c='#2e2e30', rib=0.12, sub=3, emiss=1.0, dark=0.0)
    if k == 'lead':
        d = g_blocks(Fd, 0.8, 4.0, '#6a6f72', '#4a4e50', mortar=0.03, var=0.06, rough=0.6, depth=0.02, stagger=0.0, dirt=0.25, metal=0.3, tint=0.3)
        return d
    if k == 'ironplate':
        return g_metal(Fd, '#34343a', rough=0.5, tarnish_c='#5a3a2a', tarnish=0.35, scale=0.3, tint=0.0, metal=0.7)
    if k == 'brass':
        return g_metal(Fd, '#c49a4a', rough=0.28, tarnish_c='#6f5a32', tarnish=0.45, scale=0.15, tint=1.0, streak=True)
    if k == 'castiron':
        return g_metal(Fd, '#2e2e30', rough=0.55, tarnish_c='#4a3328', tarnish=0.3, scale=0.2, tint=0.2, metal=0.65)
    if k == 'arch_windows':
        d = g_blocks(Fd, 0.225, 0.075, '#8a4632', '#b8aa98', mortar=0.011, var=0.16, huevar=0.08, rough=0.86, depth=0.007, dirt=0.18)
        # stone string course bottom and top
        band_ = ((Fd.y < 0.45) | (Fd.y > V - 0.35)).astype(np.float32)
        d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col('#b9a68a') * (1 + 0.1 * Fd.noise(0.5)[..., None]), band_)
        d['hgt'] = d['hgt'] + band_ * 0.03
        windows(d, Fd, spacing=4.0, ww=2.2, wh=5.6, sill=1.1, arch=True, glass_c='#5a4a36', frame_c='#2e2e30',
                mull=0.55, emiss=1.0, lit_frac=0.92)
        return d
    if k == 'setts':
        return g_blocks(Fd, 0.2, 0.12, '#6f6a62', '#4e4a44', mortar=0.012, var=0.2, rough=0.85, depth=0.012, dirt=0.3)
    # ---- ear
    if k == 'concrete':
        return g_concrete(Fd, '#e6e4de', panel=(4.0, 2.0), streaks=0.08, board=0.12)
    if k == 'concrete_weathered':
        d = g_concrete(Fd, '#e2e0d8', panel=(4.0, 4.0), streaks=0.22, board=0.08)
        return d
    if k == 'concrete_smooth':
        return g_concrete(Fd, '#e8e6e0', panel=(2.0, 4.0), streaks=0.06, board=0.0, tie=False)
    if k == 'asphalt':
        d = g_blocks(Fd, 24.0, 6.0, '#3a3b3d', '#3a3b3d', mortar=0.0, var=0.05, rough=0.9, depth=0.0, stagger=0.0, dirt=0.2, speck=0.25, chip=0.0)
        line = ((np.abs(Fd.y - V * 0.5) < 0.08) & ((Fd.x % 6.0) < 3.0)).astype(np.float32)
        d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col('#d8d4c6'), line)
        return d
    if k == 'dish_panels':
        d = g_concrete(Fd, '#d6dadc', panel=(4.0, 2.0), var=0.05, streaks=0.05, board=0.0, tie=False, rough=0.55)
        d['metal'][:] = 0.15
        return d
    if k == 'dish_panels_dark':
        d = g_concrete(Fd, '#c4c9cc', panel=(4.0, 2.0), var=0.07, streaks=0.12, board=0.0, tie=False, rough=0.6)
        d['metal'][:] = 0.15
        return d
    if k == 'dish_rib':
        return g_metal(Fd, '#6a6e72', rough=0.5, tarnish_c='#7a5a3a', tarnish=0.25, scale=0.3, metal=0.8)
    if k == 'grate':
        d = g_blocks(Fd, 0.1, 0.1, '#1e2022', '#5a5e62', mortar=0.02, var=0.05, rough=0.6, depth=0.01, stagger=0.0, metal=0.6, dirt=0.1, chip=0.0)
        return d
    if k == 'steel':
        return g_metal(Fd, '#5a5e62', rough=0.45, tarnish_c='#6a4a32', tarnish=0.2, scale=0.2, tint=0.6, metal=0.85)
    if k == 'aviation_red':
        return g_spot(Fd, '#ff3a2a', hue=1.0, base_c='#d8d6d0')
    if k == 'grass_berm':
        return g_grass(Fd, '#56703a', '#6f7f45', '#8a8a55', stripes=0.06)
    if k == 'office_windows':
        d = g_concrete(Fd, '#dcdad4', panel=(4.0, 8.0), streaks=0.1, tie=False)
        windows(d, Fd, spacing=2.0, ww=1.7, wh=2.4, sill=1.0, arch=False, glass_c='#26303a', frame_c='#8a8e92',
                mull=0.85, emiss=1.0, lit_frac=0.7, reveal=0.05, frame_metal=0.8)
        windows(d, Fd, spacing=2.0, ww=1.7, wh=2.4, sill=4.9, arch=False, glass_c='#26303a', frame_c='#8a8e92',
                mull=0.85, emiss=1.0, lit_frac=0.6, reveal=0.05, frame_metal=0.8)
        return d
    # ---- ladder
    if k == 'composite':
        return g_concrete(Fd, '#f2f0ea', panel=(3.0, 1.5), var=0.025, streaks=0.03, tie=False, rough=0.45)
    if k == 'glazing':
        d = g_concrete(Fd, '#f2f0ea', panel=(3.0, 6.0), var=0.02, streaks=0.02, tie=False, rough=0.45)
        # full-height ribbon glazing with a spandrel
        g = g_glass_grid(Fd, cols=16, rows=2, glass_c='#3a5068', frame_c='#e8e6e0', rib=0.12, sub=1, emiss=1.0, frame_metal=0.3, dark=0.25)
        rib = ((Fd.y > 0.9) & (Fd.y < 5.6)).astype(np.float32)
        for kk in ('alb',):
            d[kk] = mix(d[kk], g[kk], rib)
        for kk in ('rough', 'metal', 'hgt', 'em'):
            d[kk] = d[kk] + (g[kk] - d[kk]) * rib
        d['hgt'] = d['hgt'] - rib * 0.08
        return d
    if k == 'composite_smooth':
        return g_concrete(Fd, '#f4f2ec', panel=(6.0, 3.0), var=0.015, streaks=0.02, tie=False, rough=0.35)
    if k == 'plaza_paving':
        return g_blocks(Fd, 1.5, 0.75, '#d8d2c4', '#b8b2a4', mortar=0.008, var=0.06, rough=0.7, depth=0.004, dirt=0.08)
    if k == 'garden_solar':
        gg = g_grass(Fd, '#4f7a3a', '#6f8f45', '#9a9a5a', rough=0.9)
        fo = g_foliage(Fd, [('#5f7f45', 3), ('#4a6a35', 2), ('#7a9a50', 1), ('#c8b060', 0.3)], leaf=0.35)
        so = g_solar(Fd)
        # band = 48 m x 12 m of roof: two solar arrays of different lengths, planted beds, one path on the outer edge
        x, y = Fd.x, Fd.y
        msol = (((x > 4.0) & (x < 15.0) & (y > 2.0) & (y < 9.0)) | ((x > 27.0) & (x < 34.0) & (y > 3.5) & (y < 10.5))).astype(np.float32)
        wob = Fd.noise(3.0, octaves=3) * 1.2
        mbed = (((x > 17.0) & (x < 25.5) & (np.abs(y - 6.0 + wob) < 3.6)) | ((x > 37.0) & (x < 46.0) & (np.abs(y - 5.0 + wob) < 3.2))).astype(np.float32)
        mbed = mbed * (1 - msol)
        path = ((y < 1.1) | ((x > 15.6) & (x < 16.6))).astype(np.float32) * (1 - msol) * (1 - mbed)
        d = base(Fd, '#d8d2c4', 0.75)
        d['alb'] = mix(mix(gg['alb'], fo['alb'], mbed), so['alb'], msol)
        d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col('#d8d2c4'), path)
        for kk in ('rough', 'metal', 'hgt'):
            d[kk] = (gg[kk] * (1 - mbed - msol).clip(0, 1) + fo[kk] * mbed + so[kk] * msol).astype(np.float32)
        d['hgt'] = d['hgt'] + mbed * 0.15
        return d
    if k == 'solar':
        return g_solar(Fd)
    if k == 'garden':
        return g_foliage(Fd, [('#5f7f45', 3), ('#4a6a35', 2), ('#7a9a50', 1), ('#d0c070', 0.2)], leaf=0.35)
    if k == 'reflecting_pool':
        return g_water(Fd, '#2a4552', ripple=0.002)
    if k == 'gold':
        return g_metal(Fd, '#d9c08a', rough=0.22, tarnish_c='#a08a5a', tarnish=0.2, scale=0.3, tint=1.0)
    if k == 'tether_core':
        return g_spot(Fd, '#fffaf0', hue=0.0, base_c='#e8e6e0')
    if k == 'light_slit':
        d = g_metal(Fd, '#d9c08a', rough=0.25, tarnish=0.1, tint=1.0)
        slit = (np.abs(Fd.y - V * 0.5) < V * 0.18).astype(np.float32)
        d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col('#fff6e0'), slit)
        d['metal'] = d['metal'] * (1 - slit)
        d['em'] = slit
        return d
    if k == 'climber':
        d = g_concrete(Fd, '#f4f2ec', panel=(2.0, 2.0), var=0.01, streaks=0.0, tie=False, rough=0.3)
        win = ((np.abs(Fd.y - V * 0.55) < 0.25) & ((Fd.x % 1.0) < 0.75)).astype(np.float32)
        d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col('#2a3442'), win)
        d['rough'] = np.where(win > 0, 0.08, d['rough']).astype(np.float32)
        d['em'] = win * 0.8
        return d
    # ---- sunwell
    if k == 'white_stone':
        return g_blocks(Fd, 2.0, 1.0, '#ebe5d6', '#cfc6b2', mortar=0.006, var=0.05, huevar=0.02, rough=0.38, depth=0.004, dirt=0.06, speck=0.05, stagger=0.5)
    if k == 'riser_gold':
        d = g_blocks(Fd, 2.8, 1.4, '#e8e2d2', '#cfc6b2', mortar=0.006, var=0.04, rough=0.38, depth=0.004, dirt=0.05, stagger=0.0)
        gb = ((Fd.y > V * 0.58) & (Fd.y < V * 0.86)).astype(np.float32)
        d['alb'] = mix(d['alb'], np.ones_like(d['alb']) * col('#d4af5a') * (1 + 0.1 * Fd.noise(0.3)[..., None]), gb)
        d['metal'] = gb
        d['rough'] = np.where(gb > 0, 0.22, d['rough']).astype(np.float32)
        d['hgt'] = d['hgt'] + gb * 0.004
        d['tint'] = gb
        return d
    if k == 'balustrade':
        d = g_blocks(Fd, 4.8, 1.2, '#ebe5d6', '#cfc6b2', mortar=0.005, var=0.04, rough=0.4, depth=0.003, dirt=0.06, stagger=0.0)
        # balusters: vertical slots between a top rail and a base
        xx = (Fd.x % 0.3) / 0.3
        slot_ = ((Fd.y > 0.22) & (Fd.y < V - 0.2)).astype(np.float32) * (np.abs(xx - 0.5) > 0.32)
        d['alb'] = d['alb'] * (1 - 0.65 * slot_[..., None])
        d['hgt'] = d['hgt'] - slot_ * 0.06
        return d
    if k == 'avenue_paving':
        return g_blocks(Fd, 1.2, 0.6, '#d9d0bd', '#b5ab96', mortar=0.008, var=0.07, rough=0.6, depth=0.004, dirt=0.1)
    if k == 'pool_water':
        if asset == 'stair':
            return g_water(Fd, '#3f6f74', ripple=0.003, em=0.35, hue=1.0)
        return g_water(Fd, '#1e3a44', ripple=0.003, em=0.25, hue=1.0)
    if k == 'gold_leaf':
        return g_metal(Fd, '#d4af5a', rough=0.2, tarnish_c='#b08a40', tarnish=0.2, scale=0.2, tint=1.0)
    if k == 'planter_grass':
        return g_grass(Fd, '#4f6e35', '#6f8a45', '#7a6a40')
    if k == 'earth_cut':
        return g_rock(Fd, '#8a7258', '#6a5844', rough=0.95, strata=0.6)
    if k == 'bronze':
        return g_metal(Fd, '#b08440', rough=0.24, tarnish_c='#6a5a3a', tarnish=0.3, scale=0.2, tint=1.0, streak=True)
    if k == 'mirror':
        d = g_spot(Fd, '#f2d898', hue=0.0, base_c='#b08440')
        d['metal'] = np.maximum(d['metal'], 0.9 * (d['em'] > 0)).astype(np.float32)
        d['rough'] = np.where(d['em'] > 0, 0.06, d['rough']).astype(np.float32)
        d['em'] = d['em'] * 0.6
        return d
    if k == 'beam_core':
        return g_spot(Fd, '#fff1c8', hue=0.0, base_c='#b08440')
    if k == 'lamp':
        return g_spot(Fd, '#ffe0a0', hue=1.0, base_c='#6a5a3a')
    # ---- stair
    if k == 'honey_stone':
        d = g_blocks(Fd, 0.9, 0.45, '#c79e62', '#a88a60', mortar=0.012, var=0.12, huevar=0.05, rough=0.85, depth=0.015, dirt=0.25, chip=0.4, tint=0.4)
        return d
    if k == 'honey_vines':
        d = g_blocks(Fd, 0.9, 0.45, '#c79e62', '#a88a60', mortar=0.012, var=0.12, huevar=0.05, rough=0.85, depth=0.015, dirt=0.2, chip=0.4, tint=0.4)
        fo = g_foliage(Fd, [('#4f6e35', 3), ('#3f5a2a', 2), ('#6a8a40', 1), ('#c890b8', 0.25)], leaf=0.12)
        hang = np.clip((Fd.y / V) * 1.4 + Fd.noise(0.5, 2.0, octaves=4) * 0.9 - 0.25, 0, 1)
        hang = sstep(0.35, 0.6, hang)
        for kk in ('alb',):
            d[kk] = mix(d[kk], fo[kk], hang)
        for kk in ('rough', 'hgt'):
            d[kk] = d[kk] + (fo[kk] + (0.04 if kk == 'hgt' else 0) - d[kk]) * hang
        d['tint'] = d['tint'] * (1 - hang)
        return d
    if k == 'rock':
        return g_rock(Fd, '#9a8670', '#6f6250', rough=0.92)
    if k == 'marble':
        return g_blocks(Fd, 1.0, 0.5, '#ece8df', '#d2ccbe', mortar=0.004, var=0.03, rough=0.3, depth=0.003, dirt=0.05, tint=0.3)
    if k == 'garden_terrace':
        gg = g_grass(Fd, '#5d7a38', '#7a8f45', '#9a8a50')
        fo = g_foliage(Fd, [('#d98fb0', 2), ('#a98cc8', 2), ('#4f6e35', 3), ('#f0e0e8', 0.5)], leaf=0.15)
        bed = sstep(0.1, 0.35, Fd.noise(3.0, octaves=3) + 0.15)
        path = ((np.abs(Fd.y - V * 0.15) < 0.6)).astype(np.float32)
        d = base(Fd, '#000000', 0.9)
        d['alb'] = mix(mix(gg['alb'], fo['alb'], bed), np.ones((BH, BW, 3), np.float32) * col('#c8b48a'), path)
        d['rough'] = gg['rough'] + (fo['rough'] - gg['rough']) * bed
        d['hgt'] = gg['hgt'] + bed * 0.12 + fo['hgt'] * bed - path * 0.03
        return d
    if k == 'blossom_pink':
        return g_blossom(Fd, '#e494b8', '#f6cfe0', '#c87098', '#4f6a35')
    if k == 'blossom_lilac':
        return g_blossom(Fd, '#ad90cc', '#d8c8ee', '#8468a8', '#4f6a35')
    if k == 'foliage':
        return g_blossom(Fd, '#4f6e35', '#6f8c45', '#3e5a2a', '#2a3a1c', rough=0.82)
    if k == 'waterfall':
        return g_fall(Fd)
    if k == 'lattice':
        d = g_metal(Fd, '#ece8df', rough=0.4, tarnish=0.05, metal=0.0, tint=0.6)
        return d
    if k == 'lantern':
        return g_spot(Fd, '#ffe2b0', hue=0.0, base_c='#ece8df')
    raise KeyError(name)


# ------------------------------------------------------------------------------------------ atlas

def ao_from_height(h, texel, radius_m, strength):
    r = max(1, int(radius_m / texel))
    hb = blur(h, r)
    hb2 = blur(h, r * 4)
    occ = np.clip((hb - h) * strength, 0, 1) * 0.7 + np.clip((hb2 - h) * strength * 0.5, 0, 1) * 0.3
    return np.clip(1 - occ, 0, 1)


def normal_from_h(h, texel):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) / (2 * texel)
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) / (2 * texel)
    n = np.stack([-dx, -dy, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return (n * 0.5 + 0.5).astype(np.float32)


def build_atlas(asset, slot, bands, outdir, prefix, seed=1):
    """bands: list of (name, V). Writes prefix_D/N/ORM/M.png; returns list of file names."""
    D, N, ORM, M = [], [], [], []
    for i, (name, V) in enumerate(bands):
        d = finish(gen_band(asset, slot, name, V, seed * 101 + i * 7 + zlib.crc32(name.encode()) % 997))
        texel = V / BH
        h = d['hgt'].astype(np.float32)
        N.append(normal_from_h(h, texel))
        ao = ao_from_height(h, texel, 0.05, 25.0)
        ORM.append(np.stack([ao, np.clip(d['rough'], 0.02, 1), np.clip(d['metal'], 0, 1)], -1))
        D.append(np.clip(d['alb'], 0, 1))
        vv = (np.arange(BH, dtype=np.float32) + 0.5) / BH
        win = sstep(0.0, 0.035, np.minimum(vv, 1 - vv))[:, None]
        M.append(np.stack([np.clip(d['em'], 0, 1) * win, np.clip(d['hue'], 0, 1), np.clip(d['tint'], 0, 1)], -1))
        print('  band %s/%s/%s  %s' % (asset, slot, name, dmtex.stats(name, d['alb'])))
    files = []
    for tag, arrs, srgb in (('D', D, True), ('N', N, False), ('ORM', ORM, False), ('M', M, False)):
        a = np.concatenate(arrs, axis=0).astype(np.float32)
        p = os.path.join(outdir, '%s_%s.png' % (prefix, tag))
        dmtex.save_png(p, a, None, srgb=srgb)
        files.append(os.path.basename(p))
    return files
