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


# ---------- organic light fields (v2, after the lead's review) ----------
sat = base.max(2) - base.min(2)
cloud = smooth(0.50, 0.75, lum) * (1 - smooth(0.10, 0.22, sat)) * disc     # white, unsaturated = cloud
cloud_soft = ndi.gaussian_filter(cloud, 3)                                   # soft-edged occluder
limb = (0.30 + 0.70 * np.sqrt(np.clip(nz, 0, 1))) * disc                     # atmospheric dimming toward the limb

def fbm(rng, octaves=5, base_scale=96):
    """Fractal value noise in 0..1 over the frame."""
    f = np.zeros((H, W), np.float32); amp = 1.0; tot = 0.0; sc = base_scale
    for _ in range(octaves):
        g = rng.random((H // sc + 3, W // sc + 3)).astype(np.float32)
        up = ndi.zoom(g, sc, order=3)[:H, :W]
        f += amp * up; tot += amp; amp *= 0.55; sc = max(2, sc // 2)
    f /= tot
    return (f - f.min()) / (f.max() - f.min() + 1e-6)

def sprawl(E, rng, noise, x, y, size, col, gain):
    """One settlement: bright core, irregular noisy falloff, sparkly speckle texture, only on land."""
    rc = 1.6 + 2.6 * np.sqrt(size)
    m = int(rc * 4) + 3
    x0, x1 = max(0, int(x) - m), min(W, int(x) + m + 1); y0, y1 = max(0, int(y) - m), min(H, int(y) + m + 1)
    if x1 <= x0 or y1 <= y0: return
    dx = xx[y0:y1, x0:x1] - x; dy = (yy[y0:y1, x0:x1] - y) * 1.15
    d = np.sqrt(dx * dx + dy * dy)
    nloc = noise[y0:y1, x0:x1]
    dens = np.exp(-(d / (rc * (0.55 + 0.9 * nloc))) ** 1.4)            # noise bends the falloff: organic edge
    speck = rng.random(dens.shape) ** 3.0                                  # sparkle: many dim, few bright pixels
    on = (rng.random(dens.shape) < np.clip(dens * 1.6, 0, 1)).astype(np.float32)
    e = (0.25 * dens + speck * on) * dens ** 0.6
    e *= land[y0:y1, x0:x1]
    E[y0:y1, x0:x1] += e[..., None] * col * gain * (0.7 + 0.3 * np.sqrt(size))

def thread(E, rng, p, q, col, gain, keep=0.6):
    """A faint road/rail: a noisy, curving walk from p toward q, broken into dashes, land only."""
    x, y = p; tx, ty = q
    ang = np.arctan2(ty - y, tx - x); dash_on = True; steps = int(np.hypot(tx - x, ty - y) * 1.6) + 5
    for i in range(steps):
        want = np.arctan2(ty - y, tx - x)
        ang += 0.18 * np.angle(np.exp(1j * (want - ang))) + rng.normal(0, 0.22)
        x += 0.8 * np.cos(ang); y += 0.8 * np.sin(ang)
        if rng.random() < 0.06: dash_on = not dash_on if rng.random() < 0.8 else dash_on
        xi, yi = int(x), int(y)
        if not (0 <= xi < W and 0 <= yi < H): break
        if dash_on and land[yi, xi] and rng.random() < keep:
            E[yi, xi] += col * gain * (0.5 + 0.5 * rng.random())
        if (x - tx) ** 2 + (y - ty) ** 2 < 9: break

def satellites(rng, x, y, size, k):
    out = []
    for _ in range(k):
        r_ = (2.5 + 3.0 * rng.random()) * (1.6 + 2.6 * np.sqrt(size)); a = rng.uniform(0, 2 * np.pi)
        sx, sy = x + r_ * np.cos(a), y + r_ * np.sin(a)
        if 0 <= int(sx) < W and 0 <= int(sy) < H and land[int(sy), int(sx)]:
            out.append((sx, sy, rng.uniform(0.15, 0.5)))
    return out

def finish(img, E, name, haze=0.0, extra=None):
    E = E * (night * limb)[..., None] * (1 - 0.8 * cloud_soft)[..., None]
    g1 = np.stack([ndi.gaussian_filter(E[..., c], 1.2) for c in range(3)], -1)
    g2 = np.stack([ndi.gaussian_filter(E[..., c], 4.0) for c in range(3)], -1)
    g3 = np.stack([ndi.gaussian_filter(E[..., c], 14.0) for c in range(3)], -1)
    tot = 0.8 * E + 1.3 * g1 + 1.3 * g2 + (1.2 + 6.0 * haze) * g3        # soft bloom around dense cores
    if extra is not None: tot = tot + extra
    out = 1 - (1 - img) * np.exp(-tot)
    Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8)).save(os.path.join(OUT, name + '.png'), optimize=True)

FIRE = np.array([1.00, 0.55, 0.22]); OIL = np.array([1.0, 0.68, 0.32])
SODIUM = np.array([1.00, 0.78, 0.40]); LED = np.array([1.0, 0.94, 0.82]); COOL = np.array([0.80, 0.88, 1.0])

def settle(rng, noise, n, coast, sep, alpha, smax):
    sites = candidates(rng, n, coast, sep)
    sizes = np.clip(rng.pareto(alpha, len(sites)) + 1, 1, smax)
    return [(x, y, s) for (x, y, _), s in zip(sites, sizes)]

def stone():
    rng = np.random.default_rng(21); img = relit(base); E = np.zeros_like(img)
    for x, y, _ in candidates(rng, 60, 6, 12):                 # lone campfires: no sprawl, no lines
        E[int(y), int(x)] += FIRE * rng.uniform(3.0, 5.0)
        if rng.random() < 0.4:                                  # a second fire close by
            E[int(y + rng.integers(-2, 3)), int(x + rng.integers(-2, 3))] += FIRE * 2.0
    finish(img, E, 'night-stone')

def iron_field(rng, E):
    noise = fbm(rng)
    towns = settle(rng, noise, 40, 8, 18, 2.2, 3)
    for x, y, s in towns:
        sprawl(E, rng, noise, x, y, s, OIL, 0.55)
        for sx, sy, ss in satellites(rng, x, y, s, 1): sprawl(E, rng, noise, sx, sy, ss, OIL, 0.4)
    for i, (x, y, s) in enumerate(towns):
        d = sorted(((x - a) ** 2 + (y - b2) ** 2, j) for j, (a, b2, _) in enumerate(towns) if j != i)
        for dd, j in d[:1]:
            if dd < 70 ** 2: thread(E, rng, (x, y), towns[j][:2], OIL, 0.22, keep=0.35)

def iron():
    rng = np.random.default_rng(22); img = relit(base); E = np.zeros_like(img)
    iron_field(rng, E); finish(img, E, 'night-iron')

def industry():
    rng = np.random.default_rng(23); img = relit(base); E = np.zeros_like(img)
    noise = fbm(rng)
    cities = settle(rng, noise, 75, 7, 14, 1.5, 9)
    for x, y, s in cities:
        sprawl(E, rng, noise, x, y, s, SODIUM, 0.75)
        for sx, sy, ss in satellites(rng, x, y, s, int(1 + s)): sprawl(E, rng, noise, sx, sy, ss, SODIUM, 0.55)
    for i, (x, y, s) in enumerate(cities):
        d = sorted(((x - a) ** 2 + (y - b2) ** 2, j) for j, (a, b2, _) in enumerate(cities) if j != i)
        for dd, j in d[:2]:
            if dd < 85 ** 2: thread(E, rng, (x, y), cities[j][:2], SODIUM, 0.40, keep=0.55)
    finish(img, E, 'night-industry', haze=0.35)

def space():
    rng = np.random.default_rng(24); img = relit(base); E = np.zeros_like(img)
    noise = fbm(rng)
    cities = settle(rng, noise, 90, 9, 12, 1.3, 12)
    for x, y, s in cities:
        sprawl(E, rng, noise, x, y, s, LED, 0.85)
        for sx, sy, ss in satellites(rng, x, y, s, int(2 + s)): sprawl(E, rng, noise, sx, sy, ss, LED, 0.6)
    for i, (x, y, s) in enumerate(cities):
        d = sorted(((x - a) ** 2 + (y - b2) ** 2, j) for j, (a, b2, _) in enumerate(cities) if j != i)
        for dd, j in d[:3]:
            if dd < 110 ** 2: thread(E, rng, (x, y), cities[j][:2], LED, 0.50, keep=0.8)
    coastal = [(s, (x, y)) for x, y, s in cities if dist_coast[int(y), int(x)] < 5]
    s, (lx, ly) = max(coastal)
    E[int(ly), int(lx)] += COOL * 6.0                          # launch site: a hot cool-white point that blooms
    for t in range(1, 12): E[int(ly - 1.6 * t), int(lx - 0.5 * t)] += COOL * (1.2 - 0.09 * t)   # rising plume
    finish(img, E, 'night-space', haze=0.25)
    print('launch site', lx, ly)

def aurora():
    rng = np.random.default_rng(25)
    img = relit(base, moon=1.4)
    img = img + (ndi.gaussian_filter(cloud, 1.5) * (1 - day))[..., None] * np.array([0.16, 0.19, 0.26])   # moonlit clouds
    E = np.zeros_like(img); iron_field(np.random.default_rng(22), E)
    # auroral oval round a magnetic pole tilted toward the upper-right limb, so part of it is seen in profile
    P = np.array([0.36, 0.86, 0.36]); P /= np.linalg.norm(P)
    u = np.cross(P, [0, 0, 1.0]); u /= np.linalg.norm(u); v = np.cross(P, u)
    co = np.radians(34.0)
    N = 5200; phis = np.linspace(0, 2 * np.pi, N, endpoint=False)
    walk = rng.normal(0, 1, N).cumsum(); walk = (walk - np.linspace(walk[0], walk[-1], N)) / (np.abs(walk).max() + 1e-6)
    fold = 0.5 * np.sin(phis * 6 + 1.3) + 0.35 * np.sin(phis * 15 + 0.4) + 0.9 * walk
    rays = rng.gamma(1.4, 0.7, N) * (0.6 + 0.4 * np.sin(phis * 140 + rng.normal(0, 0.6, N)))   # sharp vertical striations
    rays = np.clip(rays, 0, 3)
    tops = 0.07 + 0.06 * fbm(rng, 3, 64)[0, (np.linspace(0, W - 1, N)).astype(int)]          # curtain height varies
    A = np.zeros_like(img)
    GREEN = np.array([0.30, 1.0, 0.55]); VIOLET = np.array([0.72, 0.32, 1.0])
    for k in range(N):
        c = co + 0.028 * fold[k]
        d = P * np.cos(c) + (u * np.cos(phis[k]) + v * np.sin(phis[k])) * np.sin(c)
        top = tops[k]
        for h in np.linspace(0.002, top, 30):
            q = d * (1 + h)
            if q[2] < 0 and (q[0] ** 2 + q[1] ** 2) < 1.0: continue
            t = (h - 0.002) / (top - 0.002 + 1e-9)
            col = GREEN * (0.35 + 0.65 * np.exp(-t * 1.6)) * (1 - 0.5 * np.exp(-t * 60)) * (1 - smooth(0.55, 1.0, t)) \
                + VIOLET * 0.35 * np.exp(-((t - 0.85) / 0.14) ** 2)
            x = CX + R * q[0]; y = CY - R * q[1]; xi, yi = int(x), int(y)
            if 0 <= xi < W and 0 <= yi < H:
                on_disc = (q[0] ** 2 + q[1] ** 2) < 1.0
                kk = (night[yi, xi] ** 0.8) if on_disc else 1.0
                A[yi, xi] += col * rays[k] * 0.034 * kk
    A = np.stack([ndi.gaussian_filter(A[..., c], 0.7) for c in range(3)], -1) * 1.6 + \
        np.stack([ndi.gaussian_filter(A[..., c], 3.0) for c in range(3)], -1) * 2.2 + \
        np.stack([ndi.gaussian_filter(A[..., c], 12.0) for c in range(3)], -1) * 2.5
    # the aurora's glow tints the ground and clouds under it
    A += (np.stack([ndi.gaussian_filter(A[..., c], 20.0) for c in range(3)], -1) * (0.4 + 2.0 * ndi.gaussian_filter(cloud, 2))[..., None]) * 0.6
    finish(img, E, 'night-aurora', extra=A)

if __name__ == '__main__':
    for f in sys.argv[1:] or ['stone', 'iron', 'industry', 'space', 'aurora']:
        globals()[f]()
