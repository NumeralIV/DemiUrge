"""Key art for T-018, drawn in code over the locked T-013 planet (state-healthy.png).

Usage (from the repo root): python docs/concept/key-art/src/keyart.py
Needs numpy, scipy and Pillow. Reuses the T-016 night-side code (docs/concept/night/src/night.py):
the planet is relit so the terminator runs near the middle of the disc, Industry-age city lights are
drawn on the night half, then the frame is laid out at 3840x2160 with the planet on the right,
a thin moon, a warm light along the terminator (the god's presence, no figure) and two arriving
comets. All crops are cut from the master; none carries text.
"""
import importlib.util, os, tempfile
import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage as ndi

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..')
spec = importlib.util.spec_from_file_location('night', os.path.join(HERE, '..', '..', 'night', 'src', 'night.py'))
N = importlib.util.module_from_spec(spec); spec.loader.exec_module(N)

# ---- 1. relight: sun from the left and slightly toward the camera, terminator a little right of centre
L = np.array([-0.95, 0.14, 0.22]); L /= np.linalg.norm(L)
N.L = L
N.ndl = N.nx * L[0] + N.ny * L[1] + N.nz * L[2]
N.day = N.smooth(-0.06, 0.22, N.ndl)
N.night = 1 - N.smooth(-0.12, 0.04, N.ndl)
tmp = tempfile.mkdtemp(); N.OUT = tmp

rng = np.random.default_rng(31); img = N.relit(N.base, moon=0.9); E = np.zeros_like(img)
noise = N.fbm(rng)
cities = N.settle(rng, noise, 85, 9, 11, 1.4, 10)
for x, y, s in cities:
    N.sprawl(E, rng, noise, x, y, s, N.SODIUM * 0.6 + N.LED * 0.4, 0.8)
    for sx, sy, ss in N.satellites(rng, x, y, s, int(1 + s)): N.sprawl(E, rng, noise, sx, sy, ss, N.SODIUM, 0.55)
for i, (x, y, s) in enumerate(cities):
    d = sorted(((x - a) ** 2 + (y - b) ** 2, j) for j, (a, b, _) in enumerate(cities) if j != i)
    for dd, j in d[:2]:
        if dd < 90 ** 2: N.thread(E, rng, (x, y), cities[j][:2], N.SODIUM, 0.40, keep=0.6)
# the god's presence: warm light gathered along the terminator, strongest mid-disc, no figure
band = np.exp(-((N.ndl - 0.03) / 0.09) ** 2) * N.disc
pool = np.exp(-(((N.yy - N.CY) / (0.55 * N.R)) ** 2)) * N.limb
god = (band * pool)[..., None] * np.array([1.0, 0.70, 0.36]) * 0.22
N.finish(img, E, 'planet', haze=0.3, extra=ndi.gaussian_filter(god, (14, 14, 0)) * 1.2)
planet = np.asarray(Image.open(os.path.join(tmp, 'planet.png'))).astype(np.float32) / 255

# ---- 2. lay out the 3840x2160 master
S = 2.35                                   # 1280-frame px -> master px
W, H = 3840, 2160
PX, PY = 2600.0, 1110.0                    # planet centre in the master
pr = N.R * S
x0, y0 = PX - N.CX * S, PY - N.CY * S
up = Image.fromarray((planet * 255).astype(np.uint8)).resize((int(1280 * S), int(720 * S)), Image.LANCZOS)
up = up.filter(ImageFilter.UnsharpMask(radius=2.2, percent=70, threshold=2))
up = np.asarray(up).astype(np.float32) / 255

yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
r = np.hypot(xx - PX, yy - PY) / pr
rng = np.random.default_rng(7)
# star field: sparse, varied, faint
sky = np.zeros((H, W, 3), np.float32) + np.array([0.008, 0.010, 0.018])
n = 2600
sx, sy = rng.integers(0, W, n), rng.integers(0, H, n)
mag = rng.random(n) ** 6 * 0.9 + 0.04
tint = np.stack([1 - 0.2 * rng.random(n), 0.95 * np.ones(n), 0.9 + 0.1 * rng.random(n)], -1)
sky[sy, sx] += mag[:, None] * tint
sky = sky + 0.6 * np.stack([ndi.gaussian_filter(sky[..., c], 1.0) for c in range(3)], -1)
# faint warm glow in space beside the lit limb (the light the planet is held in)
sky += (np.exp(-((r - 1.0) / 0.25) ** 2) * np.clip((PX - xx) / pr, 0, 1))[..., None] * np.array([0.06, 0.045, 0.03])
canvas = sky.copy()
ys, xs = int(round(y0)), int(round(x0))
h2, w2 = up.shape[:2]
cy0, cy1 = max(0, ys), min(H, ys + h2); cx0, cx1 = max(0, xs), min(W, xs + w2)
patch = up[cy0 - ys:cy1 - ys, cx0 - xs:cx1 - xs]
m = (r[cy0:cy1, cx0:cx1] < 1.0 + 1.5 / pr).astype(np.float32)
m = ndi.gaussian_filter(m, 1.2)[..., None]
canvas[cy0:cy1, cx0:cx1] = canvas[cy0:cy1, cx0:cx1] * (1 - m) + patch * m
# atmosphere rim: blue on the day limb, thin on the night limb
ang = np.arctan2(yy - PY, xx - PX)
dayside = np.clip(-np.cos(ang) * 0.95 + 0.25, 0, 1)
rim = np.exp(-((r - 0.998) / 0.008) ** 2) * (0.05 + 0.95 * dayside ** 2)
canvas += rim[..., None] * np.array([0.30, 0.55, 1.0]) * 0.35
canvas += (np.exp(-((r - 1.0) / 0.05) ** 2) * (r > 1) * dayside)[..., None] * np.array([0.08, 0.14, 0.28])

# the moon: the same half-lit moon as in state-healthy (sun from the left there too), moved to the upper right
src = N._raw
MC, MRAD = (112.0, 88.0), 27.0               # moon centre and radius in the 1280 frame
mcrop = src[int(MC[1] - 34):int(MC[1] + 34), int(MC[0] - 34):int(MC[0] + 34)]
k = 68 * S
mm = np.asarray(Image.fromarray((mcrop * 255).astype(np.uint8)).resize((int(k), int(k)), Image.LANCZOS)).astype(np.float32) / 255
my, mx = np.mgrid[0:mm.shape[0], 0:mm.shape[1]].astype(np.float32)
mrr = np.hypot(mx - 34 * S, my - 34 * S) / (MRAD * S)
amask = ndi.gaussian_filter((mrr < 1.0).astype(np.float32), 1.5)[..., None]
MX, MY = 3470, 300
oy, ox = int(MY - 34 * S), int(MX - 34 * S)
reg = canvas[oy:oy + mm.shape[0], ox:ox + mm.shape[1]]
canvas[oy:oy + mm.shape[0], ox:ox + mm.shape[1]] = reg * (1 - amask) + np.maximum(reg, mm * 1.1) * amask

# two comets arriving from the upper right, aimed at the planet (genesis echo)
def comet(hx, hy, d, length, w0, w1, b):
    """head at (hx, hy) moving along d; tail streams back along -d, widening and fading"""
    x0, x1 = int(max(0, min(hx, hx - d[0] * length) - 80)), int(min(W, max(hx, hx - d[0] * length) + 80))
    y0, y1 = int(max(0, min(hy, hy - d[1] * length) - 80)), int(min(H, max(hy, hy - d[1] * length) + 80))
    gy, gx = yy[y0:y1, x0:x1] - hy, xx[y0:y1, x0:x1] - hx
    along = -(gx * d[0] + gy * d[1]) / length          # 0 at the head, 1 at the tail end
    perp = np.abs(gx * d[1] - gy * d[0])
    t = np.clip(along, 0, 1); w = w0 + w1 * t
    tail = np.exp(-perp ** 2 / (2 * w ** 2)) * (1 - t) ** 1.6 * (along > -0.002) * b
    head = np.exp(-(gx ** 2 + gy ** 2) / (2 * (w0 * 1.3) ** 2)) * b * 3.0
    coma = np.exp(-(gx ** 2 + gy ** 2) / (2 * (w0 * 6) ** 2)) * b * 0.35
    out = np.zeros((H, W), np.float32); out[y0:y1, x0:x1] = tail + head + coma
    return out
d = np.array([-0.62, 0.78]); d /= np.linalg.norm(d)        # heading down-left toward the planet
cm = comet(3560, 720, d, 700, 2.2, 14.0, 0.9) + comet(3700, 1030, d, 460, 1.7, 10.0, 0.6)
canvas = 1 - (1 - canvas) * np.exp(-cm[..., None] * np.array([0.72, 0.87, 1.0]))

# grade: gentle filmic shoulder and a little grain
canvas = np.clip(canvas, 0, None)
canvas = canvas / (1 + 0.12 * canvas)
# (no grain: it would push the PNGs past the 4 MB limit)
master = Image.fromarray((np.clip(canvas, 0, 1) * 255).astype(np.uint8))
master.save(os.path.join(OUT, 'key-art-master.png'), optimize=True)

# ---- 3. crops (all cut from the master, no text)
def crop(name, w, h, cx, cy, scale):
    """cut a (w*scale x h*scale) box centred at (cx, cy) from the master and resize to w x h"""
    bw, bh = w * scale, h * scale
    l = int(np.clip(cx - bw / 2, 0, W - bw)); t = int(np.clip(cy - bh / 2, 0, H - bh))
    master.crop((l, t, int(l + bw), int(t + bh))).resize((w, h), Image.LANCZOS).save(os.path.join(OUT, name), optimize=True)
    return (l, t, int(l + bw), int(t + bh))
boxes = {
    'title-1920x1080.png': crop('title-1920x1080.png', 1920, 1080, W / 2, H / 2, 2.0),
    'capsule-header-920x430.png': crop('capsule-header-920x430.png', 920, 430, 2000, 1110, 3.2),
    'capsule-main-1232x706.png': crop('capsule-main-1232x706.png', 1232, 706, 2000, 1110, 2.6),
    'capsule-vertical-748x896.png': crop('capsule-vertical-748x896.png', 748, 896, 2650, 1060, 2.35),
    'library-hero-3840x1240.png': crop('library-hero-3840x1240.png', 3840, 1240, W / 2, 1110, 1.0),
}
for k, v in boxes.items(): print(k, 'master box', v)
