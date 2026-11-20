# DEMIURGE art/age5 - procedural texture sets for the Atomic Age kit (runs inside Blender's Python).
#
# Three 1024x1024 sets, each D (sRGB base colour), N (tangent normal, OpenGL +Y), ORM (linear AO, roughness,
# metallic) and, for the facade set, M (linear: R night-window mask, G culture-tint mask, B reserved).
#
#   T_Age5_Facade_*  trim sheet: 8 horizontal strips of 128 px, each ONE storey tall, tiling in U every 24 m.
#                    The mesh maps every storey of every wall to one strip (see STRIPS), so windows land on
#                    floors exactly and never stretch. M.R is 0 on walls and a per-window random rank in
#                    (0.15, 1] on glass: a material lights a fraction f of the windows with step(1 - f, M.R).
#   T_Age5_Roof_*    fully tiling flat-roof membrane and gravel ballast, 16 m per tile (world-projected).
#   T_Age5_Detail_*  trim sheet: road decks (3-lane carriageway, 2-lane ramp), smooth concrete, painted steel,
#                    copper, rubble, lawn. Rows in DETAIL_ROWS.
#
# Every pixel comes from a seeded generator, so `build_age5.py --textures` rebuilds the PNGs bit for bit.
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import dmtex as D  # noqa: E402

N = 1024
SH = 128                     # strip height in px
REPEAT_M = 24.0              # metres per U tile on facade and detail strips

# name -> (strip index from the bottom, storey height in metres the strip depicts)
STRIPS = {
    'apt_panel': (0, 3.0),
    'apt_brick': (1, 3.0),
    'office_curtain': (2, 3.6),
    'office_ribbon': (3, 3.6),
    'ground_shop': (4, 4.2),
    'concrete_blank': (5, 3.0),
    'industrial': (6, 3.0),
    'civic_fin': (7, 3.6),
}

# Detail sheet rows (v0 px, v1 px, metres across the rows). U always 24 m.
DETAIL_ROWS = {
    'ramp': (0, 128, 9.0),
    'concrete': (128, 256, 3.0),
    'steel': (256, 320, 3.0),      # U 0-12 m painted grey steel, U 12-24 m safety-yellow paint
    'copper': (320, 384, 3.0),     # verdigris copper sheet (domes, accents)
    'rubble': (384, 512, 3.0),
    'lawn': (512, 640, 3.0),
    'road': (640, 1024, 12.0),
}


# --------------------------------------------------------------------------- small periodic field helpers


class Field:
    """A (rows x cols) canvas covering w x h metres, periodic in both directions."""

    def __init__(self, rows, cols, w, h, seed):
        self.rows, self.cols, self.w, self.h = rows, cols, w, h
        self.rng = np.random.default_rng(seed)
        self.x = np.broadcast_to(((np.arange(cols, dtype=np.float32) + 0.5) / cols * w)[None, :], (rows, cols))
        self.y = np.broadcast_to(((np.arange(rows, dtype=np.float32) + 0.5) / rows * h)[:, None], (rows, cols))
        self.tx = w / cols
        self.ty = h / rows

    def value(self, cx, cy, x=None, y=None):
        """Value noise with cx x cy cells over the canvas (periodic). Returns -1..1."""
        x = self.x if x is None else x
        y = self.y if y is None else y
        g = self.rng.random((cy, cx), dtype=np.float32) * 2.0 - 1.0
        fx = (x / self.w % 1.0) * cx
        fy = (y / self.h % 1.0) * cy
        x0 = np.floor(fx).astype(np.int32)
        y0 = np.floor(fy).astype(np.int32)
        tx = fx - x0
        ty = fy - y0
        tx = tx * tx * (3 - 2 * tx)
        ty = ty * ty * (3 - 2 * ty)
        x0 %= cx
        y0 %= cy
        x1 = (x0 + 1) % cx
        y1 = (y0 + 1) % cy
        a = g[y0, x0]; b = g[y0, x1]; c = g[y1, x0]; d = g[y1, x1]
        return (a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty).astype(np.float32)

    def fbm(self, cx, cy, octaves=4, gain=0.5):
        s = np.zeros((self.rows, self.cols), np.float32)
        amp = 1.0
        tot = 0.0
        for o in range(octaves):
            s += amp * self.value(cx << o, cy << o)
            tot += amp
            amp *= gain
        return s / tot

    def white(self):
        return self.rng.random((self.rows, self.cols), dtype=np.float32)


def soft_box(lx, ly, x0, x1, y0, y1, aa=0.02):
    """Anti-aliased rectangle mask in local metres."""
    ax = D.sstep(x0 - aa, x0 + aa, lx) * (1.0 - D.sstep(x1 - aa, x1 + aa, lx))
    ay = D.sstep(y0 - aa, y0 + aa, ly) * (1.0 - D.sstep(y1 - aa, y1 + aa, ly))
    return (ax * ay).astype(np.float32)


def line(d, half_w, aa=0.015):
    """Mask of a line at distance d (metres) from its centre."""
    return (1.0 - D.sstep(half_w - aa, half_w + aa, np.abs(d))).astype(np.float32)


def periodic_dist(a, period):
    """Distance to the nearest multiple of period."""
    m = a % period
    return np.minimum(m, period - m)


def C(h):
    return D.hex_lin(h)


def normal_aniso(hgt, tx, ty, strength=1.0):
    dx = (np.roll(hgt, -1, axis=1) - np.roll(hgt, 1, axis=1)) / (2 * tx) * strength
    dy = (np.roll(hgt, -1, axis=0) - np.roll(hgt, 1, axis=0)) / (2 * ty) * strength
    n = np.stack([-dx, -dy, np.ones_like(hgt)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return (n * 0.5 + 0.5).astype(np.float32)


def ao_from_height(hgt, rpx, k):
    hb = D.blur(hgt, rpx)
    return np.clip(1.0 - np.clip((hb - hgt) * k, 0, 1), 0, 1).astype(np.float32)


def glass(f, lx, ly, gx0, gx1, gy0, gy1, win_id, n_win, base_hex, rng, blinds=True, curtains=True):
    """Glass colour with interior hints: curtains/blinds per window, a faint sky gradient."""
    r = rng.random((n_win, 6), dtype=np.float32)
    rid = r[win_id]
    base = C(base_hex)
    col = np.broadcast_to(base, lx.shape + (3,)).copy()
    t = np.clip((ly - gy0) / max(1e-3, gy1 - gy0), 0, 1)
    col *= (0.8 + 0.4 * t)[..., None]                   # sky reflection gets lighter toward the top
    # per-pane variety: some catch the sky (cool, brighter), some the warm street, some are dark rooms
    refl = np.array([C('#6f8597'), C('#3a4650'), C('#8a7a66'), C('#232a30')], np.float32)[(rid[..., 4] * 3.999).astype(np.int32)]
    col = D.mix3(col, refl * (0.75 + 0.5 * t)[..., None], 0.45 * rid[..., 5])
    if curtains:
        # drawn curtains from both sides, random coverage and colour per window
        cov = rid[..., 0] * 0.45
        w = gx1 - gx0
        u = (lx - gx0) / max(1e-3, w)
        cmask = ((u < cov) | (u > 1 - cov)).astype(np.float32) * (rid[..., 1] > 0.25)
        palette = np.array([C('#c9bfa8'), C('#d8d2c4'), C('#a9b3b8'), C('#c49a6c'), C('#8c9a7c')], np.float32)
        ccol = palette[(rid[..., 2] * 4.999).astype(np.int32)] * 0.55
        col = D.mix3(col, ccol, cmask * 0.8)
    if blinds:
        drop = gy1 - (gy1 - gy0) * rid[..., 3] * 0.7
        bmask = (ly > drop).astype(np.float32) * (rid[..., 0] > 0.55)
        bands = 0.85 + 0.15 * (np.sin(ly * 2 * math.pi / 0.05) > 0)
        col = D.mix3(col, C('#9fa4a3') * 0.6 * bands[..., None], bmask * 0.75)
    return col


# --------------------------------------------------------------------------- facade strips


def strip_apt_panel(seed):
    Hs = 3.0
    f = Field(SH, N, REPEAT_M, Hs, seed)
    bay = 3.0
    nb = int(REPEAT_M / bay)
    bi = np.floor(f.x / bay).astype(np.int32) % nb
    lx = f.x - np.floor(f.x / bay) * bay
    ly = f.y
    rng = f.rng
    tone = rng.random(nb, dtype=np.float32)[bi] * 0.12 - 0.06
    stain = f.fbm(6, 2, 4) * 0.5 + 0.5
    grain = f.white()
    conc = C('#aba293') * (1.0 + tone + (stain - 0.5) * 0.14 + (grain - 0.5) * 0.06)[..., None]
    # slab band at the floor line: a slightly darker, rougher strip that articulates every storey
    fband = soft_box(f.x, ly, -1, 99, -0.05, 0.28)
    conc = D.mix3(conc, C('#958d80') * (0.95 + 0.1 * stain)[..., None], fband * 0.85)
    # panel joints (vertical at bay edge, horizontal at slab line)
    jv = line(periodic_dist(f.x, bay), 0.018)
    jh = line(periodic_dist(f.y, Hs), 0.025)
    joint = np.maximum(jv, jh)
    col = D.mix3(conc, C('#7d776c'), joint * 0.8)
    hgt = -joint * 0.012
    # rain streaks below the sill ends
    for sx in (0.62, 2.38):
        s = line(lx - sx, 0.05, 0.04) * np.clip((0.78 - ly) / 0.7, 0, 1) * (0.6 + 0.4 * rng.random(nb, dtype=np.float32)[bi])
        col *= (1.0 - 0.25 * s)[..., None]
    # sill
    sill = soft_box(lx, ly, 0.52, 2.48, 0.74, 0.84)
    col = D.mix3(col, C('#c9c3b7'), sill)
    hgt += sill * 0.05
    # window opening: frame then glass
    frame = soft_box(lx, ly, 0.6, 2.4, 0.84, 2.42)
    gl = soft_box(lx, ly, 0.67, 2.33, 0.91, 2.35) * (1.0 - line(lx - 1.5, 0.03)) * (1.0 - line(ly - 1.95, 0.025) * (lx > 1.5))
    gcol = glass(f, lx, ly, 0.67, 2.33, 0.91, 2.35, bi, nb, '#262f36', rng)
    col = D.mix3(col, C('#cfcac0'), frame)
    col = D.mix3(col, gcol, gl)
    # reveal shadow: the window sits 15 cm deep, so its top and left edges are shaded
    rev = soft_box(lx, ly, 0.6, 2.4, 2.25, 2.42) + soft_box(lx, ly, 0.6, 0.72, 0.84, 2.42)
    col *= (1.0 - 0.35 * np.clip(rev, 0, 1) * frame)[..., None]
    hgt += -frame * 0.05 - gl * 0.06
    rough = 0.86 - 0.1 * stain
    rough = D.lerp(rough, 0.45, frame)
    rough = D.lerp(rough, 0.05, gl)
    rank = (0.15 + 0.85 * rng.random(nb, dtype=np.float32))[bi]
    win = gl * rank
    tint = np.clip(1.0 - frame, 0, 1)
    return f, col, hgt, rough, np.zeros_like(rough), win, tint


def strip_apt_brick(seed):
    Hs = 3.0
    f = Field(SH, N, REPEAT_M, Hs, seed)
    bay = 3.0
    nb = int(REPEAT_M / bay)
    bi = np.floor(f.x / bay).astype(np.int32) % nb
    lx = f.x - np.floor(f.x / bay) * bay
    ly = f.y
    rng = f.rng
    # bricks: 0.075 courses, 0.25 long, running bond; per-brick colour
    course = np.floor(ly / 0.075).astype(np.int32)
    off = (course % 2) * 0.125
    bx = np.floor((f.x + off) / 0.25).astype(np.int32)
    bid = (course * 131 + bx * 7) % 4096
    br = rng.random(4096, dtype=np.float32)[bid]
    bcol = D.mix3(C('#8c5641'), C('#a8735a'), br * 0.7)
    bcol = D.mix3(bcol, C('#5e3a2e'), (br > 0.93).astype(np.float32) * 0.6)
    mort = np.maximum(line(periodic_dist(ly, 0.075), 0.006, 0.008), line(periodic_dist(f.x + off, 0.25), 0.006, 0.008))
    col = D.mix3(bcol, C('#a39887'), mort * 0.6)
    hgt = -mort * 0.008
    stain = f.fbm(5, 2, 4) * 0.5 + 0.5
    col *= (0.92 + 0.12 * stain)[..., None]
    # concrete slab band at floor line
    band = soft_box(f.x, ly, -1, 100, -0.02, 0.32)
    band = np.maximum(band, soft_box(f.x, ly, -1, 100, Hs - 0.02, Hs + 0.5))
    ccol = C('#b7afa1') * (0.95 + 0.08 * stain)[..., None]
    col = D.mix3(col, ccol, band)
    hgt += band * 0.03
    # lintel and sill
    lint = soft_box(lx, ly, 0.5, 2.5, 2.52, 2.68)
    sill = soft_box(lx, ly, 0.52, 2.48, 0.84, 0.94)
    col = D.mix3(col, C('#c8c0b2'), np.maximum(lint, sill))
    hgt += sill * 0.05 + lint * 0.02
    frame = soft_box(lx, ly, 0.6, 2.4, 0.94, 2.52)
    gl = soft_box(lx, ly, 0.67, 2.33, 1.0, 2.45) * (1 - line(lx - 1.5, 0.035)) * (1 - line(ly - 2.05, 0.03))
    gcol = glass(f, lx, ly, 0.67, 2.33, 1.0, 2.45, bi, nb, '#283038', rng)
    col = D.mix3(col, C('#e1ddd3'), frame)
    col = D.mix3(col, gcol, gl)
    hgt += -frame * 0.08 - gl * 0.04
    rough = np.full_like(ly, 0.9)
    rough = D.lerp(rough, 0.5, frame)
    rough = D.lerp(rough, 0.05, gl)
    rank = (0.15 + 0.85 * rng.random(nb, dtype=np.float32))[bi]
    win = gl * rank
    tint = np.clip(1.0 - frame - band * 0.5, 0, 1)
    return f, col, hgt, rough, np.zeros_like(rough), win, tint


def strip_office_curtain(seed):
    Hs = 3.6
    f = Field(SH, N, REPEAT_M, Hs, seed)
    mod = 1.5
    nb = int(REPEAT_M / mod)
    bi = np.floor(f.x / mod).astype(np.int32) % nb
    lx = f.x - np.floor(f.x / mod) * mod
    ly = f.y
    rng = f.rng
    vis = soft_box(lx, ly, -1, 9, 0.92, 3.38)
    gcol = glass(f, lx, ly, 0.0, mod, 0.92, 3.38, bi, nb, '#2b3d47', rng, curtains=False)
    # ceiling light panels visible high in the pane
    ceil = soft_box(lx, ly, 0.2, 1.3, 3.18, 3.3) * (rng.random(nb, dtype=np.float32)[bi] > 0.4)
    gcol = D.mix3(gcol, C('#8f9a9c'), ceil * 0.5)
    pane_t = (rng.random(nb, dtype=np.float32)[bi] - 0.5) * 0.12
    gcol *= (1.0 + pane_t)[..., None]
    span = D.mix3(np.broadcast_to(C('#3b4b52'), lx.shape + (3,)).copy(), C('#46575d'), (f.fbm(4, 2, 3) * 0.5 + 0.5) * 0.6)
    col = D.mix3(span, gcol, vis)
    mull = np.maximum(line(periodic_dist(f.x, mod), 0.035), np.maximum(line(ly - 0.92, 0.03), line(ly - 3.38, 0.03)))
    mull = np.maximum(mull, line(periodic_dist(ly, Hs), 0.03))
    col = D.mix3(col, C('#a4a9ab'), mull)
    hgt = mull * 0.06 - vis * 0.01
    rough = D.lerp(np.full_like(ly, 0.14), 0.04, vis)
    rough = D.lerp(rough, 0.35, mull)
    metal = mull * 0.9
    rank = (0.15 + 0.85 * rng.random(nb, dtype=np.float32))[bi]
    win = vis * (1 - mull) * rank
    tint = (1 - vis) * (1 - mull) * 0.6
    return f, col, hgt, rough, metal, win, tint


def strip_office_ribbon(seed):
    Hs = 3.6
    f = Field(SH, N, REPEAT_M, Hs, seed)
    mod = 1.5
    nb = int(REPEAT_M / mod)
    bi = np.floor(f.x / mod).astype(np.int32) % nb
    lx = f.x - np.floor(f.x / mod) * mod
    ly = f.y
    rng = f.rng
    stain = f.fbm(6, 2, 4) * 0.5 + 0.5
    conc = C('#d0c8b7') * (0.95 + 0.07 * stain + (f.white() - 0.5) * 0.04)[..., None]
    rib = soft_box(lx, ly, -1, 9, 1.12, 3.28)
    gcol = glass(f, lx, ly, 0.0, mod, 1.12, 3.28, bi, nb, '#2e3a43', rng, curtains=False)
    # overhang shadow at the top of the glass
    gcol *= (1.0 - 0.35 * D.sstep(2.9, 3.28, ly))[..., None]
    col = D.mix3(conc, gcol, rib)
    mull = line(periodic_dist(f.x, mod), 0.028) * rib
    col = D.mix3(col, C('#4a433d'), mull)
    lip = soft_box(lx, ly, -1, 9, 1.02, 1.12)
    col = D.mix3(col, C('#e2dccf'), lip)
    joint = line(periodic_dist(f.x, 6.0), 0.012) * (1 - rib)
    col = D.mix3(col, C('#8e877a'), joint * 0.6)
    # streaks under the sill
    streak = (f.value(160, 1) * 0.5 + 0.5) ** 3 * np.clip((1.02 - ly) / 0.9, 0, 1) * (1 - rib)
    col *= (1.0 - 0.2 * streak)[..., None]
    hgt = -rib * 0.18 + lip * 0.04 + mull * 0.04 - joint * 0.01
    rough = D.lerp(0.82 - 0.08 * stain, 0.05, rib * (1 - mull))
    rough = D.lerp(rough, 0.4, mull)
    rank = (0.15 + 0.85 * rng.random(nb, dtype=np.float32))[bi]
    win = rib * (1 - mull) * rank
    tint = (1 - rib) * 0.9
    return f, col, hgt, rough, mull * 0.8, win, tint


def strip_ground_shop(seed):
    Hs = 4.2
    f = Field(SH, N, REPEAT_M, Hs, seed)
    bay = 6.0
    nb = int(REPEAT_M / bay)
    bi = np.floor(f.x / bay).astype(np.int32) % nb
    lx = f.x - np.floor(f.x / bay) * bay
    ly = f.y
    rng = f.rng
    stain = f.fbm(5, 2, 3) * 0.5 + 0.5
    col = np.broadcast_to(C('#c3bcae'), lx.shape + (3,)).copy() * (0.94 + 0.08 * stain)[..., None]
    pier = (lx < 0.6).astype(np.float32)
    col = D.mix3(col, C('#77716b') * (0.9 + 0.1 * stain)[..., None], pier)
    base = soft_box(lx, ly, -1, 9, -1, 0.3)
    col = D.mix3(col, C('#5e5955'), base)
    shop = soft_box(lx, ly, 0.75, 5.85, 0.3, 3.15)
    # interior: warm, varied per shop; stock/shelves as noise blocks
    r = rng.random((nb, 3), dtype=np.float32)
    inter = np.array([C('#5a4a3a'), C('#3e4a4e'), C('#6b5a48'), C('#4c463f')], np.float32)[(r[bi, 0] * 3.999).astype(np.int32)]
    shelves = (np.sin(ly * 2 * math.pi / 0.55) > 0.55).astype(np.float32) * (f.value(160, 2) > -0.2) * 0.3
    gcol = inter * (0.7 + shelves)[..., None]
    gcol = D.mix3(gcol, C('#b8c4c8') * 0.35, D.sstep(1.6, 3.15, ly) * 0.5)   # reflection
    frames = np.maximum(line(periodic_dist(lx - 0.75, 1.7), 0.03), np.maximum(line(ly - 0.35, 0.04), line(ly - 2.6, 0.03)))
    col = D.mix3(col, gcol, shop)
    col = D.mix3(col, C('#2a2a2a'), frames * shop)
    # fascia band and its shadow (awning)
    fascia = soft_box(lx, ly, 0.6, 6.0, 3.3, 3.95)
    fcol = np.array([C('#8a3b2a'), C('#2f4a5e'), C('#c9a64a'), C('#3d5a3c')], np.float32)[(r[bi, 1] * 3.999).astype(np.int32)]
    col = D.mix3(col, fcol, fascia * (r[bi, 2] > 0.3))
    col *= (1.0 - 0.3 * soft_box(lx, ly, 0.6, 6.0, 2.9, 3.3))[..., None]
    hgt = -shop * 0.25 + pier * 0.1 + fascia * 0.08
    rough = D.lerp(np.full_like(ly, 0.75), 0.06, shop * (1 - frames))
    rank = (0.6 + 0.4 * r[:, 2])[bi]
    win = shop * (1 - frames) * rank
    tint = (1 - shop) * (1 - pier) * (1 - fascia)
    return f, col, hgt, rough, frames * shop * 0.6, win, tint


def strip_concrete_blank(seed, base='#aaa396', Hs=3.0):
    f = Field(SH, N, REPEAT_M, Hs, seed)
    stain = f.fbm(8, 1, 4) * 0.5 + 0.5
    boards = (f.value(1, 20) * 0.5 + 0.5)
    streak_x = (f.value(240, 1) * 0.5 + 0.5)
    streak = np.clip(streak_x * 1.6 - 0.75, 0, 1) * (0.5 + 0.5 * (f.value(24, 1) * 0.5 + 0.5))
    grain = f.white()
    col = C(base) * (0.97 + 0.05 * boards + 0.08 * (stain - 0.5) + 0.05 * (grain - 0.5))[..., None]
    col *= (1.0 - 0.22 * streak)[..., None]
    lift = line(periodic_dist(f.y, Hs), 0.015)
    col *= (1.0 - 0.15 * lift)[..., None]
    hgt = (boards - 0.5) * 0.004 - lift * 0.006 + (grain - 0.5) * 0.002
    rough = 0.88 - 0.06 * streak
    return f, col, hgt, rough, np.zeros_like(rough), np.zeros_like(rough), np.ones_like(rough)


def strip_industrial(seed):
    Hs = 3.0
    f = Field(SH, N, REPEAT_M, Hs, seed)
    rib = 0.5 + 0.5 * np.cos(f.x * 2 * math.pi / 0.2)
    rib = rib ** 2
    stain = f.fbm(6, 2, 4) * 0.5 + 0.5
    sheet = np.floor(f.x / 1.0).astype(np.int32) % 24
    st = f.rng.random(24, dtype=np.float32)[sheet] * 0.05
    col = C('#a9b0b2') * (0.93 + st + 0.08 * (stain - 0.5) + 0.06 * rib)[..., None]
    lap = line(periodic_dist(f.y, Hs), 0.02)
    col *= (1.0 - 0.25 * lap)[..., None]
    streak = (f.value(120, 1) * 0.5 + 0.5) ** 4 * (1 - f.y / Hs) * 0.3
    col *= (1 - streak)[..., None]
    hgt = rib * 0.025 - lap * 0.01
    rough = 0.5 + 0.1 * stain
    return f, col, hgt, rough, np.full_like(rough, 0.15), np.zeros_like(rough), np.ones_like(rough) * 0.8


def strip_civic_fin(seed):
    Hs = 3.6
    f = Field(SH, N, REPEAT_M, Hs, seed)
    bay = 2.4
    nb = int(REPEAT_M / bay)
    bi = np.floor(f.x / bay).astype(np.int32) % nb
    lx = f.x - np.floor(f.x / bay) * bay
    ly = f.y
    rng = f.rng
    stain = f.fbm(6, 2, 4) * 0.5 + 0.5
    grain = f.white()
    conc = C('#c9c0ad') * (0.95 + 0.08 * stain + 0.04 * (grain - 0.5))[..., None]
    fin = soft_box(lx, ly, 0.0, 0.32, -1, 9)
    gl = soft_box(lx, ly, 0.5, 2.25, 0.55, 3.3)
    gcol = glass(f, lx, ly, 0.5, 2.25, 0.55, 3.3, bi, nb, '#2a343b', rng, curtains=False)
    tr = line(ly - 2.6, 0.03) * gl
    col = D.mix3(conc, gcol, gl)
    col = D.mix3(col, C('#5a554e'), tr)
    col = D.mix3(col, conc * 1.06, fin)
    # fin side shadow (light from the left at golden hour reads as relief)
    col *= (1.0 - 0.2 * soft_box(lx, ly, 0.32, 0.5, -1, 9))[..., None]
    hgt = fin * 0.25 - gl * 0.12
    rough = D.lerp(0.84 - 0.05 * stain, 0.05, gl * (1 - tr))
    rank = (0.15 + 0.85 * rng.random(nb, dtype=np.float32))[bi]
    win = gl * (1 - tr) * rank
    tint = (1 - gl) * 0.9
    return f, col, hgt, rough, np.zeros_like(rough), win, tint


STRIP_GEN = {
    'apt_panel': strip_apt_panel,
    'apt_brick': strip_apt_brick,
    'office_curtain': strip_office_curtain,
    'office_ribbon': strip_office_ribbon,
    'ground_shop': strip_ground_shop,
    'concrete_blank': strip_concrete_blank,
    'industrial': strip_industrial,
    'civic_fin': strip_civic_fin,
}


def build_facade(out_dir, log):
    D_ = np.zeros((N, N, 3), np.float32)
    N_ = np.zeros((N, N, 3), np.float32)
    O_ = np.zeros((N, N, 3), np.float32)
    M_ = np.zeros((N, N, 3), np.float32)
    for name, (k, Hs) in STRIPS.items():
        f, col, hgt, rough, metal, win, tint = STRIP_GEN[name](1000 + k * 17)
        r0 = k * SH
        nrm = normal_aniso(hgt, f.tx, f.ty, 1.0)
        ao = ao_from_height(hgt, 3, 18.0)
        D_[r0:r0 + SH] = np.clip(col, 0, 1)
        N_[r0:r0 + SH] = nrm
        O_[r0:r0 + SH, :, 0] = ao
        O_[r0:r0 + SH, :, 1] = np.clip(rough, 0.02, 1)
        O_[r0:r0 + SH, :, 2] = np.clip(metal, 0, 1)
        M_[r0:r0 + SH, :, 0] = np.clip(win, 0, 1)
        M_[r0:r0 + SH, :, 1] = np.clip(tint, 0, 1)
        log(D.stats('facade/' + name, col))
    D.save_png(os.path.join(out_dir, 'T_Age5_Facade_D.png'), D_)
    D.save_png(os.path.join(out_dir, 'T_Age5_Facade_N.png'), N_, srgb=False)
    D.save_png(os.path.join(out_dir, 'T_Age5_Facade_ORM.png'), O_, srgb=False)
    D.save_png(os.path.join(out_dir, 'T_Age5_Facade_M.png'), M_, srgb=False)


# --------------------------------------------------------------------------- roof


def build_roof(out_dir, log):
    f = Field(N, N, 16.0, 16.0, 2024)
    grain = f.white()
    g2 = f.value(512, 512)
    stain = f.fbm(4, 4, 5) * 0.5 + 0.5
    pond = np.clip((f.fbm(3, 3, 4) * 0.5 + 0.5 - 0.58) * 4, 0, 1)
    col = C('#a39e93') * (0.9 + 0.12 * grain[..., None] + 0.06 * g2[..., None])
    col *= (0.9 + 0.16 * stain)[..., None]
    col = D.mix3(col, C('#6f6d68'), pond * 0.55)
    # membrane laps every 1 m along x and patch repairs
    lap = line(periodic_dist(f.x, 1.0), 0.02) * 0.25
    col *= (1 - lap)[..., None]
    rng = f.rng
    for i in range(14):
        cx, cy = rng.random() * 16, rng.random() * 16
        w, h = 0.6 + rng.random() * 2.2, 0.5 + rng.random() * 1.6
        dx = periodic_dist(f.x - cx, 16.0)
        dy = periodic_dist(f.y - cy, 16.0)
        p = soft_box(dx, dy, -1, w / 2, -1, h / 2, 0.03)
        col = D.mix3(col, C('#7a766e') if i % 3 else C('#b2ac9f'), p * 0.35)
    hgt = grain * 0.006 + g2 * 0.004 - lap * 0.02
    nrm = normal_aniso(hgt, f.tx, f.ty, 1.0)
    orm = np.stack([ao_from_height(hgt, 2, 60.0), np.clip(0.9 - pond * 0.4, 0, 1), np.zeros_like(hgt)], -1)
    D.save_png(os.path.join(out_dir, 'T_Age5_Roof_D.png'), np.clip(col, 0, 1))
    D.save_png(os.path.join(out_dir, 'T_Age5_Roof_N.png'), nrm, srgb=False)
    D.save_png(os.path.join(out_dir, 'T_Age5_Roof_ORM.png'), orm, srgb=False)
    log(D.stats('roof', col))


# --------------------------------------------------------------------------- detail sheet


def road_rows(rows, w_m, seed, lines):
    f = Field(rows, N, REPEAT_M, w_m, seed)
    grain = f.white()
    agg = f.value(700, int(w_m * 30))
    stain = f.fbm(6, 1, 4) * 0.5 + 0.5
    # verify pass: sun-bleached, aggregate-rich asphalt (was #4a4946) so highways read as the concept's pale ribbons
    col = C('#5f5d58') * (0.85 + 0.15 * grain + 0.08 * agg + 0.18 * (stain - 0.5))[..., None]
    # tyre tracks: darker bands 0.6 m wide in each lane
    for (a, b, kind) in lines:
        pass
    lanes = [(lines[i][1], lines[i + 1][0]) for i in range(len(lines) - 1)]
    for a, b in lanes:
        for t in (0.28, 0.72):
            c = a + (b - a) * t
            col *= (1 - 0.12 * line(f.y - c, 0.35, 0.25))[..., None]
    hgt = grain * 0.002
    paint = np.zeros_like(grain)
    for (a, b, kind) in lines:
        m = soft_box(f.x, f.y, -1, 99, a, b, 0.02)
        if kind == 'dash':
            m *= soft_box(f.x % 12.0, f.y, 0.5, 3.5, -1, 99, 0.03)
        wear = np.clip(0.75 + 0.4 * f.value(90, 2), 0, 1)
        paint = np.maximum(paint, m * wear)
    col = D.mix3(col, C('#d9d5c8'), paint)
    hgt += paint * 0.002
    rough = 0.92 - paint * 0.3
    return f, col, hgt, rough


def build_detail(out_dir, log):
    D_ = np.zeros((N, N, 3), np.float32)
    N_ = np.zeros((N, N, 3), np.float32)
    O_ = np.zeros((N, N, 3), np.float32)

    def put(name, f, col, hgt, rough, metal):
        r0, r1, _ = DETAIL_ROWS[name]
        D_[r0:r1] = np.clip(col, 0, 1)
        N_[r0:r1] = normal_aniso(hgt, f.tx, f.ty)
        O_[r0:r1, :, 0] = ao_from_height(hgt, 3, 20.0)
        O_[r0:r1, :, 1] = np.clip(rough, 0.03, 1)
        O_[r0:r1, :, 2] = np.clip(metal, 0, 1)
        log(D.stats('detail/' + name, col))

    # main 3-lane carriageway, 12 m: shoulder, solid, 3 lanes with dashes, solid, shoulder
    f, col, hgt, rough = road_rows(384, 12.0, 3001, [(0.6, 0.75, 'solid'), (4.25, 4.4, 'dash'), (7.9, 8.05, 'dash'), (11.55, 11.7, 'solid')])
    put('road', f, col, hgt, rough, np.zeros_like(rough))
    f, col, hgt, rough = road_rows(128, 9.0, 3002, [(0.6, 0.75, 'solid'), (4.35, 4.5, 'dash'), (8.1, 8.25, 'solid')])
    put('ramp', f, col, hgt, rough, np.zeros_like(rough))
    # smooth concrete with formwork panel joints
    f = Field(128, N, REPEAT_M, 3.0, 3003)
    stain = f.fbm(8, 1, 4) * 0.5 + 0.5
    col = C('#b8b2a6') * (0.94 + 0.1 * stain + 0.04 * (f.white() - 0.5))[..., None]
    j = np.maximum(line(periodic_dist(f.x, 2.4), 0.01), line(periodic_dist(f.y, 1.5), 0.01))
    col *= (1 - 0.15 * j)[..., None]
    put('concrete', f, col, -j * 0.004, 0.85 - 0.05 * stain, np.zeros_like(stain))
    # painted steel: light grey paint (U 0-12 m) and safety yellow (U 12-24 m), chips and rust spots
    f = Field(64, N, REPEAT_M, 3.0, 3004)
    n1 = f.fbm(24, 3, 4)
    chips = np.clip((f.value(300, 20) - 0.72) * 6, 0, 1)
    rust = np.clip((n1 - 0.45) * 4, 0, 1)
    yellow = (f.x >= 12.0).astype(np.float32)
    paint = D.mix3(np.broadcast_to(C('#9a9fa1'), f.x.shape + (3,)).copy(), np.broadcast_to(C('#d0a321'), f.x.shape + (3,)).copy(), yellow)
    col = paint * (0.93 + 0.08 * n1)[..., None]
    col = D.mix3(col, C('#5d4a3c'), np.maximum(chips * 0.6, rust * 0.5))
    put('steel', f, col, -chips * 0.001, 0.45 + 0.3 * np.maximum(chips, rust), 0.6 * (1 - rust) * (1 - yellow))
    # verdigris copper sheet with standing seams every 0.6 m
    f = Field(64, N, REPEAT_M, 3.0, 3008)
    n1 = f.fbm(10, 2, 5) * 0.5 + 0.5
    seam = line(periodic_dist(f.x, 0.6), 0.02)
    col = D.mix3(np.broadcast_to(C('#5f9a86'), f.x.shape + (3,)).copy(), C('#3f6f60'), n1 * 0.7)
    col = D.mix3(col, C('#8a6a45'), np.clip((n1 - 0.8) * 4, 0, 1) * 0.5)
    col *= (1 + 0.12 * seam)[..., None]
    put('copper', f, col, seam * 0.01, 0.55 + 0.1 * n1, np.full_like(n1, 0.2))
    # rubble: broken concrete lumps, rebar, soot and dust
    f = Field(128, N, REPEAT_M, 3.0, 3005)
    tx = D.Tex(256, 3006)
    # worley via dmtex on a square proxy, resampled across the strip
    f1, f2, ids, _, _ = tx.worley(48, 48)
    rx = (np.arange(N) * 256 // N) % 256
    ry = (np.arange(128) * 256 // 128) % 256
    F1 = f1[np.ix_(ry, rx)]
    F2 = f2[np.ix_(ry, rx)]
    ID = ids[np.ix_(ry, rx)]
    cr = tx.cell_rand(ID, 48 * 48)
    edge = np.clip((F2 - F1) * 6, 0, 1)
    col = D.mix3(C('#6d6a64'), C('#b4ada0'), cr)
    col *= (0.55 + 0.45 * edge)[..., None]
    reb = line(periodic_dist(f.x * 0.7 + f.y * 0.4, 1.7), 0.01) * (f.value(30, 3) > 0.3)
    col = D.mix3(col, C('#5b3b2a'), reb)
    soot = np.clip(f.fbm(6, 2, 4) * 1.2, 0, 1)
    col *= (1 - 0.45 * soot)[..., None]
    put('rubble', f, col, edge * 0.12 + cr * 0.06, 0.92 - 0.1 * reb, reb * 0.5)
    # lawn: mowing stripes, clover patches
    f = Field(128, N, REPEAT_M, 3.0, 3007)
    gr = f.white()
    pat = f.fbm(12, 2, 4) * 0.5 + 0.5
    stripe = (np.floor(f.x / 2.0) % 2).astype(np.float32)
    col = D.mix3(C('#4b6a33'), C('#6a7f3c'), pat * 0.7)
    col *= (0.9 + 0.12 * stripe + 0.12 * gr)[..., None]
    put('lawn', f, col, gr * 0.01, 0.9 + 0.05 * gr, np.zeros_like(gr))

    D.save_png(os.path.join(out_dir, 'T_Age5_Detail_D.png'), D_)
    D.save_png(os.path.join(out_dir, 'T_Age5_Detail_N.png'), N_, srgb=False)
    D.save_png(os.path.join(out_dir, 'T_Age5_Detail_ORM.png'), O_, srgb=False)


def build_all(out_dir, log=print):
    os.makedirs(out_dir, exist_ok=True)
    build_facade(out_dir, log)
    build_roof(out_dir, log)
    build_detail(out_dir, log)
