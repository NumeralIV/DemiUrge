# DEMIURGE art - Age3 (Sail Age) family textures: three trim/atlas sets, all procedural (numpy, seeded).
#
#   T_Age3_Structure_{D,N,ORM,M}  trim sheet, 1024 x 1024, horizontal bands that tile in U (and in V within
#                                 the band, so long faces are cut into band-height strips by the mesh builder)
#   T_Age3_Roof_{D,N,ORM}         four equal roof-covering bands (terracotta, slate, shingle, lead). A material
#                                 instance can shift V by k * 0.25 to swap the covering per culture.
#   T_Age3_Detail_{D,N,ORM,M}     4 x 4 atlas of 256 px cells (windows, doors, glass, iron, rope, canvas ...)
#
# M texture: R = night emissive mask (window glass, lantern), G = tint mask (where a culture instance
# recolours: plaster on Structure, painted wood on Detail), B = 0.
# Arrays are [row, col] with row 0 = bottom (UV v = 0), the order Blender's Image.pixels uses.
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import dmtex as T  # noqa: E402
from dmtex import hex_lin as H, sstep  # noqa: E402

N = 1024

# ----------------------------------------------------------------------------------------------- layout
# band: (slot, v0, v1 in UV, metres tall, metres wide per U tile)
BANDS = {
    # Structure trim sheet (102.4 px/m on the big bands, 102.4 px/m on the strips)
    'plaster':  ('Structure', 0 / 1024, 256 / 1024, 2.5, 10.0),
    'ashlar':   ('Structure', 256 / 1024, 512 / 1024, 2.5, 10.0),
    'quay':     ('Structure', 512 / 1024, 768 / 1024, 2.5, 10.0),
    'planks':   ('Structure', 768 / 1024, 832 / 1024, 0.625, 10.0),
    'beam':     ('Structure', 832 / 1024, 896 / 1024, 0.625, 10.0),
    'dressed':  ('Structure', 896 / 1024, 960 / 1024, 0.625, 10.0),
    'rubble':   ('Structure', 960 / 1024, 1024 / 1024, 0.625, 10.0),
    # Roof (128 px/m)
    'tile':     ('Roof', 0.00, 0.25, 2.0, 8.0),
    'slate':    ('Roof', 0.25, 0.50, 2.0, 8.0),
    'shingle':  ('Roof', 0.50, 0.75, 2.0, 8.0),
    'lead':     ('Roof', 0.75, 1.00, 2.0, 8.0),
}
ROOF_BANDS = ['tile', 'slate', 'shingle', 'lead']

# Detail atlas cells (index -> name). Cell i: column i % 4, row i // 4 counted from the TOP of the image.
CELLS = ['window', 'window_shut', 'window_arch', 'shutter',
         'door', 'door_wide', 'arcade', 'lantern',
         'iron', 'rope', 'canvas', 'gilt',
         'crate', 'charred', 'boarded', 'window_broken']


def cell_uv(name, inset=3.0 / 1024):
    i = CELLS.index(name)
    c, r = i % 4, i // 4
    u0 = c * 0.25 + inset
    u1 = (c + 1) * 0.25 - inset
    v1 = 1.0 - r * 0.25 - inset
    v0 = 1.0 - (r + 1) * 0.25 + inset
    return u0, v0, u1, v1


# ----------------------------------------------------------------------------------------------- noise on rectangles
class Rect:
    """A periodic canvas of w x h px covering wm x hm metres (same density both ways)."""

    def __init__(self, w, h, wm, hm, seed):
        self.w, self.h, self.wm, self.hm = w, h, wm, hm
        self.t = T.Tex(4, seed)
        x = (np.arange(w, dtype=np.float32) + 0.5) / w
        y = (np.arange(h, dtype=np.float32) + 0.5) / h
        self.u = np.broadcast_to(x[None, :], (h, w)).astype(np.float32)
        self.v = np.broadcast_to(y[:, None], (h, w)).astype(np.float32)
        self.X = self.u * wm
        self.Y = self.v * hm
        self.rng = self.t.rng

    def cells(self, size_m):
        return max(1, int(round(self.wm / size_m))), max(1, int(round(self.hm / size_m)))

    def noise(self, size_m, u=None, v=None):
        cx, cy = self.cells(size_m)
        return self.t.value(cx, cy, self.u if u is None else u, self.v if v is None else v)

    def fbm(self, size_m, octaves=4, gain=0.5, u=None, v=None, stretch=(1, 1)):
        cx, cy = self.cells(size_m)
        cx = max(1, cx // int(stretch[0]))
        cy = max(1, cy // int(stretch[1]))
        uu = self.u if u is None else u
        vv = self.v if v is None else v
        s = np.zeros(uu.shape, np.float32)
        amp, tot = 1.0, 0.0
        for o in range(octaves):
            s += amp * self.t.value(cx << o, cy << o, uu, vv)
            tot += amp
            amp *= gain
        return s / tot

    def worley(self, size_m, jitter=0.9, u=None, v=None):
        cx, cy = self.cells(size_m)
        return self.t.worley(cx, cy, self.u if u is None else u, self.v if v is None else v, jitter,
                             aspect=(self.wm / cx) / (self.hm / cy))


def ramp(t, cols):
    cs = np.stack([H(c) for c in cols])
    x = np.clip(t, 0, 1) * (len(cols) - 1)
    i = np.clip(np.floor(x).astype(np.int32), 0, len(cols) - 2)
    f = (x - i)[..., None]
    return cs[i] * (1 - f) + cs[i + 1] * f


def mix(a, b, t):
    return a + (b - a) * np.asarray(t, np.float32)[..., None]


def courses(R, course_h, lo, hi, stagger=True):
    """Periodic running-bond blocks. Returns block id, dist to nearest joint (m), local x/y in block (m), block len."""
    rows = int(round(R.hm / course_h))
    ch = R.hm / rows
    row = np.clip(np.floor(R.Y / ch).astype(np.int32), 0, rows - 1)
    ly = R.Y - row * ch
    dy = np.minimum(ly, ch - ly)
    bid = np.zeros(R.X.shape, np.int32)
    dx = np.zeros(R.X.shape, np.float32)
    lx = np.zeros(R.X.shape, np.float32)
    bl = np.zeros(R.X.shape, np.float32)
    base = 0
    for r in range(rows):
        parts = []
        s = 0.0
        while R.wm - s > hi:
            L = R.rng.uniform(lo, hi)
            if R.wm - s - L < lo:
                L = (R.wm - s) / 2.0
            parts.append(L)
            s += L
        parts.append(R.wm - s)
        edges = np.concatenate([[0.0], np.cumsum(parts)])
        off = R.rng.uniform(0, R.wm) if stagger else 0.0
        m = row == r
        x = (R.X[m] - off) % R.wm
        k = np.clip(np.searchsorted(edges, x, side='right') - 1, 0, len(parts) - 1)
        e0 = edges[k]
        e1 = edges[k + 1]
        lx[m] = x - e0
        dx[m] = np.minimum(x - e0, e1 - x)
        bid[m] = base + k
        bl[m] = (e1 - e0)
        base += len(parts)
    d = np.minimum(dx, dy)
    return dict(bid=bid, d=d, dx=dx, dy=dy, lx=lx, ly=ly, bl=bl, count=base, ch=ch)


def cell_rand(R, ids, count, off=0):
    r = np.random.default_rng(int(R.rng.integers(1 << 30)) + off).random(count, dtype=np.float32)
    return r[ids]


# ----------------------------------------------------------------------------------------------- Structure bands
def band_plaster(w, h):
    R = Rect(w, h, 10.0, 2.5, 3101)
    trowel = R.fbm(0.8, 5) * 0.002 + np.clip(1 - np.abs(R.fbm(0.5, 3)) - 0.8, 0, 1) * 0.003
    # spalled patches showing rubble stone beneath
    spall_n = R.fbm(1.2, 5)
    spall = sstep(0.55, 0.62, spall_n) * sstep(0.1, 0.4, R.fbm(0.5, 3) + 0.3)
    W = R.worley(0.12)
    stones = sstep(0.0, 0.05, W[1] - W[0])
    hgt = trowel - spall * 0.008 + spall * stones * 0.006
    crack = np.zeros_like(hgt)
    tone = np.clip(0.5 + R.fbm(2.0, 5) * 0.9, 0, 1)
    bc = ramp(tone, ['#cdb38c', '#dcc59e', '#e6d4b2'])
    mott = R.fbm(0.35, 4)
    bc *= (1.0 + mott[..., None] * 0.07)
    # rain streaks (vertical) and soft grime blotches
    streak = np.clip(R.fbm(1.0, 3, stretch=(1, 4)) * 0.5 + 0.45, 0, 1) ** 3
    bc *= (1.0 - streak[..., None] * 0.22)
    blot = np.clip(R.fbm(3.0, 4) * 1.3 - 0.15, 0, 1)
    bc = mix(bc, H('#a89272'), blot * 0.2)
    stone_col = ramp(np.clip(cell_rand(R, W[2], W[2].max() + 1) * 0.8 + 0.1, 0, 1), ['#8d7a5e', '#a48e6c', '#b29c78'])
    stone_col = mix(stone_col, H('#7e6e58'), (1 - stones) * 0.6)
    bc = mix(bc, stone_col, spall)
    bc = mix(bc, H('#7a6650'), crack * 0.7)
    rough = 0.9 + R.fbm(0.5, 3) * 0.05 - spall * 0.02
    tint = 1.0 - spall
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=tint, ao_k=50.0)


def band_ashlar(w, h, quay=False):
    R = Rect(w, h, 10.0, 2.5, 3203 if not quay else 3307)
    if not quay:
        B = courses(R, 2.5 / 6, 0.55, 1.25)
        jw = 0.008
    else:
        B = courses(R, 2.5 / 4, 0.9, 1.7)
        jw = 0.016
    d = B['d'] + R.fbm(0.3, 3) * (0.003 if not quay else 0.008)
    joint = 1.0 - sstep(jw * 0.5, jw * 1.5, d)
    rb = cell_rand(R, B['bid'], B['count'])
    rb2 = cell_rand(R, B['bid'], B['count'], 5)
    if not quay:
        pillow = sstep(0.0, 0.03, d) ** 0.7 * 0.008
        rough_face = R.fbm(0.15, 4) * 0.0015
    else:
        pillow = sstep(0.0, 0.06, d) ** 0.6 * 0.03   # rusticated: strongly bulged faces
        rough_face = R.fbm(0.12, 5) * 0.010 + np.clip(1 - np.abs(R.fbm(0.3, 3)) - 0.75, 0, 1) * 0.02
    chips = np.clip(1 - np.abs(R.fbm(0.2, 3)) - 0.85, 0, 1) * -0.03 * sstep(0.05, 0.0, d)
    hgt = pillow + rough_face + chips - joint * (0.01 if not quay else 0.03)
    tone = np.clip(rb * 0.75 + R.fbm(1.5, 4) * 0.3 + 0.12, 0, 1)
    if not quay:
        bc = ramp(tone, ['#a88c62', '#bea07a', '#cdb38e'])
    else:
        bc = ramp(tone, ['#8c7c62', '#a08e72', '#b2a084'])
    bc *= (1.0 + R.fbm(0.12, 4)[..., None] * 0.10)
    bc *= (1.0 - (rb2 > 0.8)[..., None] * 0.12)
    streak = np.clip(R.fbm(1.2, 3, stretch=(1, 4)) * 0.5 + 0.45, 0, 1) ** 3
    bc *= (1.0 - streak[..., None] * 0.2)
    if quay:
        algae = np.clip(R.fbm(1.0, 5) * 1.6 - 0.2, 0, 1) * 0.18
        bc = mix(bc, H('#6e6a52'), algae)
        salt = np.clip(R.fbm(0.4, 4) * 2 - 0.7, 0, 1)
        bc = mix(bc, H('#b8b2a0'), salt * 0.12)
    bc = mix(bc, H('#b4a284') if not quay else H('#6e6452'), joint * (0.6 if not quay else 0.85))
    rough = 0.82 + R.fbm(0.4, 3) * 0.06 + joint * 0.08
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=np.zeros_like(hgt), ao_k=40.0 if not quay else 25.0)


def strip_planks(w, h):
    R = Rect(w, h, 10.0, 0.625, 3401)
    B = courses(R, 0.625 / 3, 1.8, 4.2)
    gap = 1.0 - sstep(0.004, 0.012, B['dy']) * sstep(0.003, 0.008, B['dx'])
    rb = cell_rand(R, B['bid'], B['count'])
    grain = R.fbm(0.05, 4, u=R.u * 0.08 + rb[..., None][..., 0] * 3, v=R.v)
    grain2 = np.sin(R.Y * 180 + R.fbm(0.6, 3) * 8 + rb * 20) * 0.5 + 0.5
    hgt = grain * 0.0015 + grain2 * 0.0008 - gap * 0.012 + R.fbm(0.3, 3) * 0.001
    tone = np.clip(rb * 0.8 + 0.1 + R.fbm(1.0, 3) * 0.2, 0, 1)
    bc = ramp(tone, ['#5e4c3a', '#7a6650', '#8e7c66'])
    bc *= (1.0 + (grain2 - 0.5)[..., None] * 0.12)
    weather = np.clip(R.fbm(1.5, 4) * 1.2 + 0.4, 0, 1)
    bc = mix(bc, H('#8f8a80'), weather * 0.35)
    bc = mix(bc, H('#2a2018'), gap * 0.9)
    rough = 0.82 + R.fbm(0.3, 3) * 0.06
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=np.zeros_like(hgt), ao_k=60.0)


def strip_beam(w, h):
    R = Rect(w, h, 10.0, 0.625, 3503)
    grain = np.sin(R.Y * 90 + R.fbm(0.8, 4) * 10) * 0.5 + 0.5
    crack = np.clip(1 - np.abs(R.fbm(0.4, 3, stretch=(4, 1))) - 0.94, 0, 1) / 0.06
    hgt = grain * 0.0015 - crack * 0.006 + R.fbm(0.2, 3) * 0.001
    tone = np.clip(0.5 + R.fbm(1.5, 4) * 0.8, 0, 1)
    bc = ramp(tone, ['#3e2e22', '#55402e', '#6a5340'])
    bc *= (1.0 + (grain - 0.5)[..., None] * 0.18)
    bc = mix(bc, H('#1c140e'), crack * 0.8)
    rough = 0.78 + R.fbm(0.3, 3) * 0.06
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=np.zeros_like(hgt), ao_k=60.0)


def strip_dressed(w, h):
    R = Rect(w, h, 10.0, 0.625, 3607)
    hgt = R.fbm(0.1, 4) * 0.0008 + np.clip(-R.fbm(0.05, 3) - 0.4, 0, 1) * -0.002
    tone = np.clip(0.5 + R.fbm(1.0, 4) * 0.8, 0, 1)
    bc = ramp(tone, ['#c4ab84', '#d2bc98', '#ddcaa8'])
    bc *= (1.0 + R.fbm(0.08, 3)[..., None] * 0.05)
    streak = np.clip(R.fbm(0.8, 3) * 0.5 + 0.45, 0, 1) ** 3
    bc *= (1.0 - streak[..., None] * 0.15)
    rough = 0.75 + R.fbm(0.3, 3) * 0.05
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=np.zeros_like(hgt), ao_k=50.0)


def strip_rubble(w, h):
    R = Rect(w, h, 10.0, 0.625, 3709)
    W = R.worley(0.16, 1.0)
    edge = sstep(0.0, 0.12, W[1] - W[0])
    rb = cell_rand(R, W[2], int(W[2].max()) + 1)
    hgt = edge * 0.04 * (0.6 + rb * 0.6) + R.fbm(0.05, 3) * 0.004
    tone = np.clip(rb * 0.8 + 0.1, 0, 1)
    bc = ramp(tone, ['#6f6250', '#93826a', '#b09c7e'])
    bc = mix(bc, H('#3a3228'), (1 - edge) * 0.8)
    dust = np.clip(R.fbm(0.6, 3) + 0.2, 0, 1)
    bc = mix(bc, H('#a39276'), dust * 0.25)
    rough = 0.9 + R.fbm(0.3, 3) * 0.04
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=np.zeros_like(hgt), ao_k=25.0)


# ----------------------------------------------------------------------------------------------- Roof bands
def band_tile(w, h):
    R = Rect(w, h, 8.0, 2.0, 4101)
    rows = 6
    ch = R.hm / rows
    tw = 0.25
    row = np.clip(np.floor(R.Y / ch).astype(np.int32), 0, rows - 1)
    fy = (R.Y - row * ch) / ch                     # 0 at the exposed lower edge, 1 under the next course
    jit = R.noise(0.25) * 0.01
    col = np.floor((R.X + jit) / tw).astype(np.int32)
    fx = ((R.X + jit) / tw) - col
    prof = 0.5 - 0.5 * np.cos(2 * np.pi * fx)      # barrel ridge between pan troughs
    tid = (row * 1000 + col % 1000).astype(np.int64)
    uid, inv = np.unique(tid, return_inverse=True)
    inv = inv.reshape(tid.shape)
    rt = np.random.default_rng(41).random(len(uid)).astype(np.float32)[inv]
    rt2 = np.random.default_rng(42).random(len(uid)).astype(np.float32)[inv]
    lift = fy * 0.012 + rt2 * 0.004                 # each course rises toward the course above
    edge_shadow = 1.0 - sstep(0.0, 0.06, fy)
    hgt = prof * 0.035 + lift - edge_shadow * 0.01 + R.fbm(0.08, 3) * 0.0015
    tone = np.clip(rt * 0.85 + R.fbm(1.2, 4) * 0.35 + 0.1, 0, 1)
    bc = ramp(tone, ['#8a4630', '#a0573c', '#b0654a', '#bb7556'])
    bc *= (0.78 + 0.22 * prof)[..., None]
    lich = np.clip(R.fbm(0.9, 5) * 1.8 - 0.35, 0, 1) * (prof > 0.4)
    bc = mix(bc, H('#8a8466'), lich * 0.45)
    soot = np.clip(R.fbm(2.5, 4) * 1.3, 0, 1)
    bc = mix(bc, H('#5a3a2c'), soot * 0.25)
    bc = mix(bc, H('#3a2018'), edge_shadow * 0.55)
    bc = mix(bc, H('#4a2a1e'), (1 - prof) ** 4 * 0.35)
    rough = 0.72 + R.fbm(0.5, 3) * 0.08 + lich * 0.1
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=np.zeros_like(hgt), ao_k=40.0)


def band_slate(w, h):
    R = Rect(w, h, 8.0, 2.0, 4203)
    B = courses(R, 0.2, 0.22, 0.42)
    fy = B['ly'] / B['ch']
    rb = cell_rand(R, B['bid'], B['count'])
    edge = 1.0 - sstep(0.0, 0.07, fy)
    side = 1.0 - sstep(0.002, 0.007, B['dx'])
    hgt = fy * 0.008 - edge * 0.004 - side * 0.003 + R.fbm(0.05, 3) * 0.0008
    tone = np.clip(rb * 0.8 + R.fbm(1.0, 4) * 0.3 + 0.1, 0, 1)
    bc = ramp(tone, ['#3f444c', '#4f5560', '#626a74'])
    lich = np.clip(R.fbm(0.8, 5) * 1.8 - 0.5, 0, 1)
    bc = mix(bc, H('#7c806c'), lich * 0.3)
    bc = mix(bc, H('#1e2024'), np.maximum(edge * 0.6, side * 0.7))
    rough = 0.6 + R.fbm(0.4, 3) * 0.08 + lich * 0.15
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=np.zeros_like(hgt), ao_k=40.0)


def band_shingle(w, h):
    R = Rect(w, h, 8.0, 2.0, 4307)
    B = courses(R, 0.25, 0.12, 0.26)
    fy = B['ly'] / B['ch']
    rb = cell_rand(R, B['bid'], B['count'])
    edge = 1.0 - sstep(0.0, 0.08, fy)
    side = 1.0 - sstep(0.002, 0.006, B['dx'])
    grain = np.sin(R.X * 260 + R.fbm(0.3, 3) * 6 + rb * 30) * 0.5 + 0.5
    hgt = fy * 0.012 - edge * 0.005 - side * 0.004 + grain * 0.0008
    tone = np.clip(rb * 0.7 + R.fbm(1.0, 4) * 0.3 + 0.15, 0, 1)
    bc = ramp(tone, ['#6a665e', '#8a867c', '#a5a196'])
    bc *= (1.0 + (grain - 0.5)[..., None] * 0.12)
    moss = np.clip(R.fbm(1.0, 5) * 2.0 - 0.15, 0, 1)
    bc = mix(bc, H('#5d6a38'), moss * 0.55)
    bc = mix(bc, H('#25231f'), np.maximum(edge * 0.6, side * 0.7))
    rough = 0.85 + R.fbm(0.3, 3) * 0.05
    return dict(bc=bc, h=hgt, rough=rough, metal=0.0, tint=np.zeros_like(hgt), ao_k=40.0)


def band_lead(w, h):
    R = Rect(w, h, 8.0, 2.0, 4409)
    sx = 0.5
    fx = (R.X / sx) % 1.0
    roll = np.exp(-((np.minimum(fx, 1 - fx)) / 0.03) ** 2)
    fy = (R.Y / 1.0) % 1.0
    lap = 1.0 - sstep(0.0, 0.03, fy)
    hgt = roll * 0.02 + fy * 0.003 - lap * 0.003 + R.fbm(0.3, 4) * 0.002
    tone = np.clip(0.5 + R.fbm(1.0, 4) * 0.8, 0, 1)
    bc = ramp(tone, ['#5d6268', '#737a80', '#868c90'])
    oxid = np.clip(R.fbm(0.6, 3, stretch=(1, 3)) * 1.5 + 0.2, 0, 1)
    bc = mix(bc, H('#a9adaa'), oxid * 0.35)
    bc = mix(bc, H('#3e4246'), lap * 0.5)
    bc *= (1.0 + roll[..., None] * 0.15)
    rough = 0.5 + R.fbm(0.4, 3) * 0.1 + oxid * 0.15
    return dict(bc=bc, h=hgt, rough=rough, metal=0.35 - oxid * 0.25, tint=np.ones_like(hgt), ao_k=40.0)


# ----------------------------------------------------------------------------------------------- Detail atlas
def _cell_canvas(seed):
    n = 256
    R = Rect(n, n, 2.0, 2.0, seed)
    x = R.u
    y = R.v
    return R, x, y


def box_mask(x, y, x0, y0, x1, y1, soft=0.004):
    return sstep(x0 - soft, x0 + soft, x) * (1 - sstep(x1 - soft, x1 + soft, x)) * \
        sstep(y0 - soft, y0 + soft, y) * (1 - sstep(y1 - soft, y1 + soft, y))


def wood_paint(R, col_hex, worn=0.3):
    grain = np.sin(R.Y * 120 + R.fbm(0.6, 3) * 6) * 0.5 + 0.5
    bc = H(col_hex) * (1.0 + (R.fbm(0.3, 4)[..., None]) * 0.12)
    wear = np.clip(R.fbm(0.25, 4) * 2.0 - (1 - worn), 0, 1)
    bc = mix(bc, H('#6e5a44'), wear)
    bc *= (1.0 + (grain - 0.5)[..., None] * 0.05)
    return bc, 1.0 - wear


def glass(R, x, y):
    sky = ramp(np.clip(y * 1.1, 0, 1), ['#1c2228', '#3a4650', '#6a7a86'])
    refl = np.clip(R.fbm(0.4, 3) * 0.8 + 0.2, 0, 1)
    sky = sky * (0.7 + refl[..., None] * 0.5)
    curtain = sstep(0.3, 0.6, R.fbm(0.8, 2)) * 0.5
    return mix(sky, H('#4a3a2c'), curtain * 0.5)


def draw_window(kind, seed):
    R, x, y = _cell_canvas(seed)
    hgt = np.zeros_like(x)
    emis = np.zeros_like(x)
    tint = np.zeros_like(x)
    rough = np.full_like(x, 0.8)
    frame_col, fpaint = wood_paint(R, '#5b6b58', 0.35)
    if kind in ('window', 'window_arch'):
        g = glass(R, x, y)
        bc = frame_col.copy()
        fw = 0.07
        inner = box_mask(x, y, fw, fw, 1 - fw, 1 - fw)
        if kind == 'window_arch':
            # round head: circle in the top part (cell aspect is stretched onto a tall quad)
            cy = 0.72
            arch = ((x - 0.5) / 0.5) ** 2 + ((y - cy) / 0.28) ** 2 < 1.0
            body = (y < cy) & (x > 0.0) & (x < 1.0)
            opening = (arch | body).astype(np.float32)
            arch_in = ((x - 0.5) / (0.5 - fw)) ** 2 + ((y - cy) / (0.28 - fw)) ** 2 < 1.0
            inner = ((arch_in | ((y < cy) & (x > fw) & (x < 1 - fw))) & (y > fw)).astype(np.float32)
            stone = ramp(np.clip(0.5 + R.fbm(0.3, 3), 0, 1), ['#a08a68', '#b8a07c'])
            vous = np.abs(np.arctan2(y - cy, x - 0.5) * 7 % 1.0 - 0.5) < 0.05
            stone = mix(stone, H('#6a5a44'), vous.astype(np.float32) * (y > cy) * 0.6)
            bc = mix(stone, bc, opening)
            hgt += opening * -0.01
        mull = (np.abs(x - 0.5) < 0.025) | (np.abs(((y - fw) / (1 - 2 * fw)) * 3 % 1.0) < 0.04)
        pane = inner * (1 - mull.astype(np.float32))
        bc = mix(bc, g, pane)
        hgt += (1 - pane) * 0.01
        emis = pane
        tint = (1 - pane) * fpaint * (inner > 0) if kind == 'window' else np.zeros_like(x)
        if kind == 'window':
            tint = (1 - pane) * fpaint
        rough = np.where(pane > 0.5, 0.08, 0.7)
    elif kind == 'window_shut':
        bc, fp = wood_paint(R, '#4f6a5c', 0.4)
        slat = (y * 22) % 1.0
        hgt = (slat * 0.004) - (np.abs(x - 0.5) < 0.01) * 0.01
        bc *= (0.8 + 0.2 * slat)[..., None]
        bc = mix(bc, H('#20241e'), (np.abs(x - 0.5) < 0.008).astype(np.float32))
        frame = 1 - box_mask(x, y, 0.06, 0.04, 0.94, 0.96)
        bc = mix(bc, H('#3c3a32'), frame * 0.5)
        tint = fp * (1 - frame)
        rough = np.full_like(x, 0.7)
    elif kind == 'shutter':
        bc, fp = wood_paint(R, '#4f6a5c', 0.45)
        slat = (y * 18) % 1.0
        stile = 1 - box_mask(x, y, 0.1, 0.05, 0.9, 0.95)
        hgt = slat * 0.004 * (1 - stile) + stile * 0.006
        bc *= (0.78 + 0.22 * np.where(stile > 0.5, 1.0, slat))[..., None]
        tint = fp
        rough = np.full_like(x, 0.7)
    elif kind == 'door':
        bc, fp = wood_paint(R, '#6a4632', 0.35)
        plank = (x * 6) % 1.0
        hgt = -(plank < 0.04).astype(np.float32) * 0.004
        bc = mix(bc, H('#2a1a12'), (plank < 0.04).astype(np.float32) * 0.7)
        split = np.abs(x - 0.5) < 0.012
        bc = mix(bc, H('#140c08'), split.astype(np.float32))
        studs = ((((x * 6) % 1.0 - 0.5) ** 2 + (((y * 9) % 1.0) - 0.5) ** 2) < 0.012).astype(np.float32)
        bc = mix(bc, H('#2c2a28'), studs)
        hgt += studs * 0.004
        surround = 1 - box_mask(x, y, 0.05, 0.0, 0.95, 0.95)
        stone = ramp(np.clip(0.5 + R.fbm(0.3, 3), 0, 1), ['#a8916c', '#c2aa84'])
        bc = mix(bc, stone, surround)
        hgt += surround * 0.01
        tint = fp * (1 - surround)
        rough = np.full_like(x, 0.75)
        fanlight = box_mask(x, y, 0.12, 0.8, 0.88, 0.92)
        bc = mix(bc, glass(R, x, y), fanlight)
        emis = fanlight
    elif kind == 'door_wide':
        bc = ramp(np.clip(0.5 + R.fbm(0.4, 3), 0, 1), ['#4a3828', '#5e4834', '#6e5640'])
        plank = (x * 9) % 1.0
        bc = mix(bc, H('#1e140c'), (plank < 0.05).astype(np.float32) * 0.8)
        brace = (np.abs((x - y) % 1.0) < 0.03) | (np.abs(y - 0.5) < 0.03) | (np.abs(y - 0.12) < 0.03) | (np.abs(y - 0.88) < 0.03)
        bc = mix(bc, H('#3a2a1c'), brace.astype(np.float32))
        hgt = brace * 0.01 - (plank < 0.05) * 0.004
        gap = np.abs(x - 0.5) < 0.01
        bc = mix(bc, H('#0c0806'), gap.astype(np.float32))
        rough = np.full_like(x, 0.82)
    elif kind == 'arcade':
        # dark shop interior seen through an opening, crates and warm lamp
        bc = ramp(np.clip(y * 0.6 + R.fbm(0.5, 3) * 0.3 + 0.2, 0, 1), ['#140e0a', '#2a1e14', '#3e2c1c'])
        crates = (box_mask(x, y, 0.1, 0.0, 0.35, 0.28) + box_mask(x, y, 0.55, 0.0, 0.9, 0.22) + box_mask(x, y, 0.62, 0.22, 0.82, 0.38)).clip(0, 1)
        bc = mix(bc, H('#6a5236'), crates * 0.8)
        lamp = np.exp(-(((x - 0.5) / 0.25) ** 2 + ((y - 0.75) / 0.2) ** 2))
        bc = mix(bc, H('#c8904e'), lamp * 0.45)
        emis = np.clip(lamp * 1.2, 0, 1) * 0.8
        rough = np.full_like(x, 0.9)
    elif kind == 'lantern':
        bc = mix(H('#d8c890') * np.ones(x.shape + (3,), np.float32), H('#fff2c8'), np.exp(-((x - 0.5) / 0.3) ** 2))
        bar = ((x * 3) % 1.0 < 0.05) | ((y * 4) % 1.0 < 0.04)
        bc = mix(bc, H('#202020'), bar.astype(np.float32))
        emis = 1.0 - bar.astype(np.float32)
        hgt = bar * 0.004
        rough = np.where(bar, 0.5, 0.05)
    elif kind == 'boarded':
        bc = ramp(np.clip(0.5 + R.fbm(0.4, 3), 0, 1), ['#4e3e2e', '#685440', '#7c6650'])
        plank = (y * 5 + np.where(x > 0.5, 0.3, 0.0)) % 1.0
        bc = mix(bc, H('#0e0a08'), (plank < 0.08).astype(np.float32))
        diag = np.abs((x - 0.1) - (y - 0.1) * 0.9) < 0.05
        bc = mix(bc, H('#5a4632'), diag.astype(np.float32))
        hgt = diag * 0.01 - (plank < 0.08) * 0.004
        rough = np.full_like(x, 0.85)
    elif kind == 'window_broken':
        soot = np.clip(1.2 - y * 0.2 + R.fbm(0.5, 3), 0, 1)
        bc = ramp(np.clip(R.fbm(0.3, 3) + 0.3, 0, 1), ['#0c0a08', '#1e1812', '#2a2218'])
        frame = (1 - box_mask(x, y, 0.06, 0.06, 0.94, 0.94)) + (np.abs(x - 0.5) < 0.025)
        broken = R.fbm(0.3, 3) > 0.15
        frame = np.clip(frame, 0, 1) * broken
        bc = mix(bc, H('#2a2016'), frame)
        bc *= (0.7 + 0.3 * (1 - soot * 0.5))[..., None]
        hgt = frame * 0.008
        rough = np.full_like(x, 0.9)
    return bc, hgt, rough, np.zeros_like(x), emis, tint


def draw_fill(kind, seed):
    R, x, y = _cell_canvas(seed)
    hgt = np.zeros_like(x)
    emis = np.zeros_like(x)
    tint = np.zeros_like(x)
    metal = np.zeros_like(x)
    if kind == 'iron':
        bc = ramp(np.clip(0.5 + R.fbm(0.3, 4), 0, 1), ['#1e1e20', '#2a2a2c', '#3a3634'])
        rust = np.clip(R.fbm(0.4, 4) * 2 - 0.3, 0, 1)
        bc = mix(bc, H('#5a3420'), rust * 0.5)
        hgt = R.fbm(0.05, 3) * 0.001
        rough = 0.55 + rust * 0.3
        metal = 0.8 - rust * 0.6
    elif kind == 'rope':
        tw = (x * 18 + y * 4) % 1.0
        bc = ramp(np.clip(0.4 + tw * 0.4 + R.fbm(0.3, 3) * 0.3, 0, 1), ['#5a4a32', '#7a6644', '#968058'])
        hgt = np.sin(tw * np.pi) * 0.004
        rough = np.full_like(x, 0.9)
    elif kind == 'canvas':
        weave = (np.sin(x * 700) * np.sin(y * 700)) * 0.5 + 0.5
        bc = ramp(np.clip(0.5 + R.fbm(0.5, 4) * 0.8, 0, 1), ['#bfae8a', '#d6c6a2', '#e4d8bc'])
        dirt = np.clip(R.fbm(0.6, 4) * 1.5, 0, 1)
        bc = mix(bc, H('#8a7a5e'), dirt * 0.3)
        bc *= (0.95 + 0.05 * weave)[..., None]
        hgt = weave * 0.0005
        rough = np.full_like(x, 0.9)
        tint = np.ones_like(x)
    elif kind == 'gilt':
        bc = ramp(np.clip(0.5 + R.fbm(0.3, 4), 0, 1), ['#8a6a2a', '#b8903c', '#d4b060'])
        tarn = np.clip(R.fbm(0.3, 4) * 2 - 0.2, 0, 1)
        bc = mix(bc, H('#3e5a48'), tarn * 0.4)
        rough = 0.3 + tarn * 0.4
        metal = 1.0 - tarn * 0.6
    elif kind == 'crate':
        bc = ramp(np.clip(0.5 + R.fbm(0.4, 3), 0, 1), ['#6a5436', '#86704c', '#9a845e'])
        plank = (y * 4) % 1.0
        bc = mix(bc, H('#2a2014'), (plank < 0.05).astype(np.float32))
        band = (np.abs(x - 0.1) < 0.04) | (np.abs(x - 0.9) < 0.04)
        bc = mix(bc, H('#3a2c1c'), band.astype(np.float32))
        hgt = band * 0.006 - (plank < 0.05) * 0.004
        rough = np.full_like(x, 0.85)
    elif kind == 'charred':
        W = R.worley(0.12, 1.0)
        cr = sstep(0.0, 0.05, W[1] - W[0])
        bc = ramp(np.clip(R.fbm(0.3, 3) + 0.3, 0, 1), ['#0c0a09', '#1a1512', '#2a221c'])
        ember = np.clip(R.fbm(0.4, 3) * 2 - 0.6, 0, 1)
        bc = mix(bc, H('#3a2a20'), ember * 0.4)
        bc = mix(bc, H('#050404'), (1 - cr) * 0.8)
        hgt = cr * 0.008
        rough = 0.9 - cr * 0.1
    return bc, hgt, rough, metal, emis, tint


# ----------------------------------------------------------------------------------------------- assembly
def _place(dst, src, r0, c0):
    h, w = src.shape[:2]
    dst[r0:r0 + h, c0:c0 + w] = src


def build_structure():
    bands = [('plaster', band_plaster(N, 256)), ('ashlar', band_ashlar(N, 256)), ('quay', band_ashlar(N, 256, quay=True)),
             ('planks', strip_planks(N, 64)), ('beam', strip_beam(N, 64)), ('dressed', strip_dressed(N, 64)),
             ('rubble', strip_rubble(N, 64))]
    return _assemble_bands(bands, 10.0)


def build_roof():
    bands = [('tile', band_tile(N, 256)), ('slate', band_slate(N, 256)), ('shingle', band_shingle(N, 256)), ('lead', band_lead(N, 256))]
    return _assemble_bands(bands, 8.0)


def _assemble_bands(bands, w_m):
    bc = np.zeros((N, N, 3), np.float32)
    nrm = np.zeros((N, N, 3), np.float32)
    orm = np.zeros((N, N, 3), np.float32)
    msk = np.zeros((N, N, 3), np.float32)
    stats = {}
    for name, b in bands:
        v0 = int(round(BANDS[name][1] * N))
        hh = b['bc'].shape[0]
        _place(bc, b['bc'], v0, 0)
        nm = T.normal_from_height(b['h'], w_m, N)
        _place(nrm, nm, v0, 0)
        ao = T.cavity_ao(b['h'], N, 0.02, w_m, b['ao_k'])
        metal = b['metal'] if np.ndim(b['metal']) else np.full_like(ao, b['metal'])
        _place(orm, np.stack([ao, np.clip(b['rough'], 0.04, 1), np.clip(metal, 0, 1)], -1), v0, 0)
        _place(msk, np.stack([np.zeros_like(ao), np.clip(b['tint'], 0, 1), np.zeros_like(ao)], -1), v0, 0)
        stats[name] = T.stats(name, b['bc'])
    return bc, nrm, orm, msk, stats


def build_detail():
    bc = np.zeros((N, N, 3), np.float32)
    hh = np.zeros((N, N), np.float32)
    orm = np.zeros((N, N, 3), np.float32)
    msk = np.zeros((N, N, 3), np.float32)
    windows = {'window', 'window_shut', 'window_arch', 'shutter', 'door', 'door_wide', 'arcade', 'lantern', 'boarded', 'window_broken'}
    for i, name in enumerate(CELLS):
        c, r = i % 4, i // 4
        r0 = N - (r + 1) * 256        # row from the top -> bottom-origin array row
        c0 = c * 256
        if name in windows:
            b, h, ro, me, em, ti = draw_window(name, 5000 + i)
        else:
            b, h, ro, me, em, ti = draw_fill(name, 5000 + i)
        _place(bc, np.clip(b, 0, 1), r0, c0)
        _place(hh, h, r0, c0)
        ao = T.cavity_ao(h, 256, 0.02, 2.0, 40.0)
        _place(orm, np.stack([ao, np.clip(np.broadcast_to(ro, h.shape), 0.04, 1), np.clip(np.broadcast_to(me, h.shape), 0, 1)], -1), r0, c0)
        _place(msk, np.stack([np.clip(em, 0, 1), np.clip(np.broadcast_to(ti, h.shape), 0, 1), np.zeros_like(h)], -1), r0, c0)
    nrm = T.normal_from_height(hh, 8.0, N)
    return bc, nrm, orm, msk, {}


def write_all(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    info = {}
    for nm, fn, has_m in (('Structure', build_structure, True), ('Roof', build_roof, False), ('Detail', build_detail, True)):
        bc, nrm, orm, msk, st = fn()
        base = os.path.join(out_dir, 'T_Age3_' + nm)
        T.save_png(base + '_D.png', np.clip(bc, 0, 1), None, srgb=True)
        T.save_png(base + '_N.png', nrm, None, srgb=False)
        T.save_png(base + '_ORM.png', orm, None, srgb=False)
        files = ['T_Age3_%s_D.png' % nm, 'T_Age3_%s_N.png' % nm, 'T_Age3_%s_ORM.png' % nm]
        if has_m:
            T.save_png(base + '_M.png', msk, None, srgb=False)
            files.append('T_Age3_%s_M.png' % nm)
        info[nm] = dict(files=files, stats=st)
        print('AGE3TEX', nm, st)
    return info
