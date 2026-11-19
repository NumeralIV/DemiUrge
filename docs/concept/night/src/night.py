"""Night-side frames for T-016, drawn in code over the locked T-013 planet.

Usage (from the repo root): python docs/concept/night/src/night.py
Needs numpy, scipy and Pillow. Every frame starts from docs/concept/world-states/state-healthy.png,
relit with a sphere-lighting pass so most of the disc is night, then city lights are placed on the
land mask of that same image, so continents and coastlines are identical in every frame.
"""
import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage as ndi
import os, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, '..', '..', 'world-states', 'state-healthy.png')
OUT = os.path.join(ROOT, '..')

CX, CY, R = 644.0, 354.0, 362.0           # globe centre and radius in the 1280x720 frame (measured from the limb)
L = np.array([-0.86, 0.12, -0.50]); L /= np.linalg.norm(L)   # sun: behind-left, so a lit crescent on the left

base = np.asarray(Image.open(SRC).convert('RGB')).astype(np.float32) / 255.0
H, W, _ = base.shape
_raw = base.copy()
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
nx = (xx - CX) / R; ny = -(yy - CY) / R
rr = nx * nx + ny * ny
disc = rr < 1.0
nz = np.sqrt(np.clip(1 - rr, 0, 1))
ndl = nx * L[0] + ny * L[1] + nz * L[2]

def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

day = smooth(-0.06, 0.22, ndl)                 # 1 lit, 0 night
night = 1 - smooth(-0.12, 0.04, ndl)          # where lights may show (fades in past the terminator)

# ---------- land mask (from the healthy day image; the original night strip uses its own lights) ----------
r, g, b = base[..., 0], base[..., 1], base[..., 2]
lum = base.mean(2)
land = ((r + g) / 2 > b + 0.02) & (lum > 0.12) & (lum < 0.80) & disc
warm = (r > b + 0.15) & (lum > 0.30) & (xx > 820)        # the healthy frame's own night-side city lights
land |= ndi.binary_dilation(warm, iterations=6) & disc & (xx > 820)
land = ndi.binary_opening(land, iterations=1)
land = ndi.median_filter(land.astype(np.uint8), 5).astype(bool)
water = disc & ~land
dist_coast = ndi.distance_transform_edt(land)
# strip the healthy frame's own city lights from the base (they are re-drawn per age)
_med = np.stack([ndi.median_filter(base[..., c], 7) for c in range(3)], -1)
_w = ndi.binary_dilation((r > b + 0.05) & (lum > 0.08) & (xx > 780) & (rr < 1.12), iterations=2)
base = np.where(_w[..., None], _med * 0.4, base)            # px from the sea, on land
edge_keep = rr < 0.93                                     # keep lights off the extreme limb

def relit(img, moon=0.9):
    """Day colour kept on the lit crescent, a warm dusk band, a deep blue night; moon adds cold fill."""
    nightc = img * np.array([0.030, 0.038, 0.070]) + moon * img * np.array([0.10, 0.12, 0.18])
    dusk = np.exp(-((ndl - 0.03) / 0.07) ** 2)[..., None]
    out = img * day[..., None] + nightc * (1 - day[..., None])
    out = out * (1 - 0.25 * dusk) + dusk * img * np.array([0.40, 0.26, 0.20]) * 0.35
    # thin atmosphere rim on the night limb
    rim = np.exp(-((np.sqrt(rr) - 0.995) / 0.012) ** 2) * (1 - day) * disc
    out += rim[..., None] * np.array([0.05, 0.09, 0.20])
    # outside the disc: keep the original background (stars, moon)
    out = np.where((rr < 1.12)[..., None] & ~disc[..., None], img * 0.35, out) if False else np.where(disc[..., None], out, np.where((rr < 1.12)[..., None] & (xx > CX)[..., None], img * 0.45, img))
    return np.clip(out, 0, 1)

def splat(buf, pts, col, core, halo, a_core, a_halo):
    """Add gaussian lights. pts: (N,3) x, y, weight."""
    for x, y, w in pts:
        x0, x1 = int(max(0, x - 4 * halo)), int(min(W, x + 4 * halo + 1))
        y0, y1 = int(max(0, y - 4 * halo)), int(min(H, y + 4 * halo + 1))
        if x1 <= x0 or y1 <= y0: continue
        dx = xx[y0:y1, x0:x1] - x; dy = yy[y0:y1, x0:x1] - y
        d2 = dx * dx + dy * dy
        f = w * (a_core * np.exp(-d2 / (2 * core * core)) + a_halo * np.exp(-d2 / (2 * halo * halo)))
        buf[y0:y1, x0:x1] += f[..., None] * col

def line(buf, p, q, col, width, amp, step=1.2, jitter=0.0, rng=None, dash=1.0):
    ln = np.hypot(q[0] - p[0], q[1] - p[1]); n = max(2, int(ln * 1.15 / step))
    # gently curved (quadratic Bezier with a sideways control point) so routes are not ruler-straight
    off = (rng.normal(0, 0.18 * ln) if rng is not None else 0.0)
    mx = (p[0] + q[0]) / 2 - (q[1] - p[1]) / max(ln, 1e-6) * off
    my = (p[1] + q[1]) / 2 + (q[0] - p[0]) / max(ln, 1e-6) * off
    pts = []
    for i in range(n + 1):
        t = i / n
        if rng is not None and dash < 1.0 and rng.random() > dash: continue
        x = (1 - t) ** 2 * p[0] + 2 * (1 - t) * t * mx + t * t * q[0]
        y = (1 - t) ** 2 * p[1] + 2 * (1 - t) * t * my + t * t * q[1]
        if jitter and rng is not None: x += rng.normal(0, jitter); y += rng.normal(0, jitter)
        xi, yi = int(x), int(y)
        if 0 <= xi < W and 0 <= yi < H and land[yi, xi] and night[yi, xi] > 0.3:
            pts.append((x, y, 1.0))
    splat(buf, pts, col, width, width * 3, amp, amp * 0.15)

def candidates(rng, n, coast_bias, min_sep):
    """Pick n city sites on night-side land, favouring coasts (coast_bias px scale)."""
    ok = land & (night > 0.5) & edge_keep
    ys, xs = np.nonzero(ok)
    wgt = np.exp(-dist_coast[ys, xs] / coast_bias) + 0.15
    wgt /= wgt.sum()
    picks = []
    for idx in rng.choice(len(xs), size=min(len(xs), n * 8), replace=False, p=wgt):
        x, y = xs[idx], ys[idx]
        if all((x - a) ** 2 + (y - b2) ** 2 >= min_sep ** 2 for a, b2, _ in picks):
            picks.append((float(x), float(y), 1.0))
        if len(picks) >= n: break
    return picks

def cluster(rng, cx, cy, n, spread):
    pts = []
    for _ in range(n):
        x = cx + rng.normal(0, spread); y = cy + rng.normal(0, spread * 0.8)
        xi, yi = int(x), int(y)
        if 0 <= xi < W and 0 <= yi < H and land[yi, xi]:
            pts.append((x, y, rng.uniform(0.5, 1.0)))
    return pts

def finish(img, buf, haze=None, name='x'):
    buf *= (night * disc)[..., None]
    glow = np.stack([ndi.gaussian_filter(buf[..., c], 6) for c in range(3)], -1)
    buf = buf + 0.25 * glow
    if haze is not None:
        hz = np.stack([ndi.gaussian_filter(buf[..., c], 22) for c in range(3)], -1)
        buf = buf + haze * hz
    out = 1 - (1 - img) * np.exp(-buf)        # soft additive (screen-like) so cores saturate gently
    Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8)).save(os.path.join(OUT, name + '.png'), optimize=True)

FIRE = np.array([1.00, 0.55, 0.22]); OIL = np.array([1.0, 0.68, 0.32])
SODIUM = np.array([1.00, 0.78, 0.40]); LED = np.array([1.0, 0.94, 0.82]); COOL = np.array([0.80, 0.88, 1.0])

def stone():
    rng = np.random.default_rng(11); img = relit(base); buf = np.zeros_like(img)
    pts = candidates(rng, 60, 6, 12)
    splat(buf, [(x, y, rng.uniform(0.6, 1.0)) for x, y, _ in pts], FIRE, 0.8, 3.0, 3.4, 0.30)
    finish(img, buf, name='night-stone')

def iron_lights(rng, buf):
    towns = candidates(rng, 40, 8, 20)
    for x, y, _ in towns:
        n = int(rng.integers(8, 22)); splat(buf, cluster(rng, x, y, n, 2.0), OIL, 0.55, 2.4, 1.2, 0.10)
    # first lit roads: dim dotted lines between near towns
    for i, (x, y, _) in enumerate(towns):
        d = sorted(((x - a) ** 2 + (y - b2) ** 2, j) for j, (a, b2, _) in enumerate(towns) if j != i)
        for dd, j in d[:2]:
            if dd < 80 ** 2: line(buf, (x, y), towns[j][:2], OIL, 0.45, 0.45, step=2.2, rng=rng, dash=0.7)
    for x, y, _ in pts_extra(rng, 60): splat(buf, [(x, y, 0.6)], FIRE, 0.5, 2, 1.0, 0.15)

def iron():
    rng = np.random.default_rng(12); img = relit(base); buf = np.zeros_like(img)
    iron_lights(rng, buf)
    finish(img, buf, name='night-iron')

def pts_extra(rng, n):
    return candidates(rng, n, 30, 6)

def industry():
    rng = np.random.default_rng(13); img = relit(base); buf = np.zeros_like(img)
    cities = candidates(rng, 70, 7, 16)
    sizes = np.clip(rng.pareto(1.6, len(cities)) + 1, 1, 8)
    for (x, y, _), s in zip(cities, sizes):
        n = int(16 * s); splat(buf, cluster(rng, x, y, n, 1.4 + 1.2 * np.sqrt(s)), SODIUM, 0.5, 2.2, 0.55, 0.06)
    # rail lines: thin continuous chains between neighbours
    for i, (x, y, _) in enumerate(cities):
        d = sorted(((x - a) ** 2 + (y - b2) ** 2, j) for j, (a, b2, _) in enumerate(cities) if j != i)
        for dd, j in d[:2]:
            if dd < 90 ** 2: line(buf, (x, y), cities[j][:2], SODIUM, 0.4, 0.22, step=1.6, jitter=0.4, rng=rng, dash=0.85)
    # ports: brighter on the coast
    for (x, y, _), s in zip(cities, sizes):
        if dist_coast[int(y), int(x)] < 4: splat(buf, [(x, y, 1.0)], OIL, 0.8, 4, 0.7, 0.12)
    finish(img, buf, haze=0.5, name='night-industry')

def space():
    rng = np.random.default_rng(14); img = relit(base); buf = np.zeros_like(img)
    cities = candidates(rng, 85, 9, 13)
    sizes = np.clip(rng.pareto(1.4, len(cities)) + 1, 1, 10)
    for (x, y, _), s in zip(cities, sizes):
        n = int(22 * s); splat(buf, cluster(rng, x, y, n, 1.6 + 1.4 * np.sqrt(s)), LED, 0.5, 2.2, 0.5, 0.05)
    # lit corridors: continuous bright lines, a spanning tree plus a few loops
    done = set()
    for i, (x, y, _) in enumerate(cities):
        d = sorted(((x - a) ** 2 + (y - b2) ** 2, j) for j, (a, b2, _) in enumerate(cities) if j != i)
        for dd, j in d[:3]:
            k = tuple(sorted((i, j)))
            if k in done or dd > 110 ** 2: continue
            done.add(k); line(buf, (x, y), cities[j][:2], LED, 0.4, 0.30, step=1.0, jitter=0.2, rng=rng)
    # launch site: the biggest coastal city gets a cool white bloom and a faint plume streak
    coastal = [(s, c) for c, s in zip(cities, sizes) if dist_coast[int(c[1]), int(c[0])] < 5]
    s, (lx, ly, _) = max(coastal)
    splat(buf, [(lx, ly, 1.0)], COOL, 1.2, 7, 2.0, 0.35)
    splat(buf, [(lx - 3 * t, ly - 9 * t, 0.9 - 0.08 * t) for t in range(10)], COOL, 0.9, 3.5, 0.9, 0.25)
    finish(img, buf, haze=0.25, name='night-space')
    print('launch site', lx, ly)

# ---------- aurora and moonlit clouds ----------
sat = base.max(2) - base.min(2)
cloud = smooth(0.50, 0.75, lum) * (1 - smooth(0.10, 0.22, sat)) * disc     # white, unsaturated = cloud

def aurora():
    rng = np.random.default_rng(15)
    img = relit(base, moon=1.4)
    # moonlit clouds: cold silver on the night side
    img = img + (cloud * (1 - day))[..., None] * np.array([0.16, 0.19, 0.26])
    buf = np.zeros_like(img)
    iron_lights(np.random.default_rng(12), buf)
    buf *= (1 - 0.75 * cloud)[..., None]           # clouds dim the lights beneath them
    # auroral oval: ring around the north magnetic pole, curtains standing 0.5-5 % of R above the surface
    P = np.array([0.30, 0.68, 0.67]); P /= np.linalg.norm(P)
    u = np.cross(P, [0, 0, 1.0]); u /= np.linalg.norm(u); v = np.cross(P, u)
    co = np.radians(19.0)
    aur = np.zeros_like(img)
    N = 3000
    phis = np.linspace(0, 2 * np.pi, N, endpoint=False)
    walk = rng.normal(0, 1, N).cumsum(); walk = (walk - np.linspace(walk[0], walk[-1], N)) / (np.abs(walk).max() + 1e-6)
    fold = 0.6 * np.sin(phis * 5 + 1.3) + 0.4 * np.sin(phis * 13 + 0.4) + 0.8 * walk      # wavy, folded oval
    broad = np.clip(0.45 + 0.55 * np.sin(phis * 2 + 2.6) + 0.2 * np.sin(phis * 9), 0.08, 1.2)  # brighter on the night arc
    rays = np.clip(rng.gamma(2.0, 0.5, N), 0, 3)                                               # fine vertical ray structure
    rays = np.convolve(rays, np.ones(3) / 3, mode='same')
    GREEN = np.array([0.30, 1.0, 0.55]); VIOLET = np.array([0.75, 0.35, 1.0])
    for k in range(N):
        phi = phis[k]
        for layer in (-1.0, 0.0, 1.0):                       # three parallel folds give the band its width
            c = co + 0.030 * fold[k] + 0.012 * layer
            d = P * np.cos(c) + (u * np.cos(phi) + v * np.sin(phi)) * np.sin(c)
            for h in np.linspace(0.003, 0.085, 22):
                q = d * (1 + h)
                if q[2] < 0 and (q[0] ** 2 + q[1] ** 2) < 1.0: continue
                t = min(1.0, max(0.0, (h - 0.003) / 0.082))
                col = GREEN * np.exp(-t * 3.2) * (1 - np.exp(-t * 40 - 0.3)) + VIOLET * 0.30 * np.exp(-((t - 0.75) / 0.2) ** 2)
                x = CX + R * q[0]; y = CY - R * q[1]; xi, yi = int(x), int(y)
                if 0 <= xi < W and 0 <= yi < H:
                    kk = 1.0 if (q[0] ** 2 + q[1] ** 2) >= 1.0 else night[yi, xi] ** 0.7
                    aur[yi, xi] += col * broad[k] * rays[k] * (1.0 if layer == 0 else 0.55) * 0.05 * kk
    aur = np.stack([ndi.gaussian_filter(aur[..., c], 1.1) for c in range(3)], -1) * 2.2 + \
          np.stack([ndi.gaussian_filter(aur[..., c], 5) for c in range(3)], -1) * 5.0 + \
          np.stack([ndi.gaussian_filter(aur[..., c], 16) for c in range(3)], -1) * 6.0
    # the aurora also lights the ground and clouds under it a little
    buf *= (night * disc)[..., None]
    glow = np.stack([ndi.gaussian_filter(buf[..., c], 6) for c in range(3)], -1)
    tot = buf + 0.25 * glow + aur
    out = 1 - (1 - img) * np.exp(-tot)
    Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8)).save(os.path.join(OUT, 'night-aurora.png'), optimize=True)

if __name__ == '__main__':
    for f in sys.argv[1:] or ['stone', 'iron', 'industry', 'space', 'aurora']:
        globals()[f]()
