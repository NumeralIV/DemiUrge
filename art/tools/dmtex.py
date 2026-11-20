# DEMIURGE art pipeline - dmtex: tileable PBR texture synthesis in numpy (runs inside Blender's Python).
#
# Every texture is periodic (tiles seamlessly) and generated from a seed, so a material set can be rebuilt
# bit-for-bit by `make_textures.py`. Arrays are float32 indexed [v, u] with v UP (row 0 = bottom = UV v 0),
# which is the order Blender's Image.pixels uses, so writing them needs no flip.
#
# A material set is three PNGs:  T_<Name>_BC (sRGB base colour, alpha optional),
# T_<Name>_N (tangent-space normal, OpenGL convention +Y up; the UE import flips green), T_<Name>_ORM
# (linear: R ambient occlusion, G roughness, B metallic).
import math
import numpy as np

# --------------------------------------------------------------------------- colour


def hex_lin(h):
    """'#a39276' -> linear RGB float32[3]."""
    h = h.lstrip('#')
    c = np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], np.float32)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4).astype(np.float32)


def lin_to_srgb(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055).astype(np.float32)


def srgb_to_lin(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4).astype(np.float32)


def lerp(a, b, t):
    return a + (b - a) * t


def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def mix3(a, b, t):
    """a, b: (...,3) or (3,); t: (...) -> (...,3)."""
    return a + (b - a) * t[..., None]


# --------------------------------------------------------------------------- periodic noise


class Tex:
    """Canvas of N x N texels covering one tile; helpers produce periodic fields."""

    def __init__(self, n, seed):
        self.n = n
        self.rng = np.random.default_rng(seed)
        t = (np.arange(n, dtype=np.float32) + 0.5) / n
        self.u = np.broadcast_to(t[None, :], (n, n))   # 0..1 across
        self.v = np.broadcast_to(t[:, None], (n, n))   # 0..1 up

    # value noise on a periodic lattice of (px, py) cells, sampled at (u, v) in tile units (may be warped)
    def value(self, px, py, u=None, v=None, smooth=True):
        u = self.u if u is None else u
        v = self.v if v is None else v
        g = self.rng.random((py, px), dtype=np.float32) * 2.0 - 1.0
        x = (u % 1.0) * px
        y = (v % 1.0) * py
        x0 = np.floor(x).astype(np.int32)
        y0 = np.floor(y).astype(np.int32)
        fx = x - x0
        fy = y - y0
        if smooth:
            fx = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
            fy = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
        x0 %= px
        y0 %= py
        x1 = (x0 + 1) % px
        y1 = (y0 + 1) % py
        a = g[y0, x0]
        b = g[y0, x1]
        c = g[y1, x0]
        d = g[y1, x1]
        return lerp(lerp(a, b, fx), lerp(c, d, fx), fy).astype(np.float32)

    def fbm(self, px, py, octaves=5, gain=0.5, u=None, v=None):
        s = np.zeros((self.n, self.n), np.float32)
        amp, tot = 1.0, 0.0
        for o in range(octaves):
            s += amp * self.value(px << o, py << o, u, v)
            tot += amp
            amp *= gain
        return s / tot

    def ridged(self, px, py, octaves=4, u=None, v=None):
        s = np.zeros((self.n, self.n), np.float32)
        amp, tot = 1.0, 0.0
        for o in range(octaves):
            s += amp * (1.0 - np.abs(self.value(px << o, py << o, u, v)))
            tot += amp
            amp *= 0.5
        return s / tot

    def worley(self, gx, gy, u=None, v=None, jitter=0.9, aspect=1.0):
        """Periodic jittered-grid Voronoi. Returns (F1, F2, cell id, cell centre u, cell centre v).
        Distances are in units of one cell height (aspect = cell width / height in texture space)."""
        u = self.u if u is None else u
        v = self.v if v is None else v
        pts = (0.5 + (self.rng.random((gy, gx, 2), dtype=np.float32) - 0.5) * jitter).astype(np.float32)
        x = (u % 1.0) * gx
        y = (v % 1.0) * gy
        cx = np.floor(x).astype(np.int32)
        cy = np.floor(y).astype(np.int32)
        f1 = np.full(x.shape, 1e9, np.float32)
        f2 = np.full(x.shape, 1e9, np.float32)
        idc = np.zeros(x.shape, np.int32)
        ccx = np.zeros(x.shape, np.float32)
        ccy = np.zeros(x.shape, np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                nx = cx + dx
                ny = cy + dy
                wx = nx % gx
                wy = ny % gy
                p = pts[wy, wx]
                px_ = nx + p[..., 0]
                py_ = ny + p[..., 1]
                d = np.sqrt(((px_ - x) * aspect) ** 2 + (py_ - y) ** 2)
                closer = d < f1
                f2 = np.where(closer, f1, np.minimum(f2, d))
                idc = np.where(closer, wy * gx + wx, idc)
                ccx = np.where(closer, px_ / gx, ccx)
                ccy = np.where(closer, py_ / gy, ccy)
                f1 = np.where(closer, d, f1)
        return f1, f2, idc, ccx, ccy

    def cell_rand(self, ids, count, seed_off=0):
        r = np.random.default_rng(int(self.rng.integers(1 << 30)) + seed_off).random(count, dtype=np.float32)
        return r[ids]


# --------------------------------------------------------------------------- maps from height


def blur(a, r):
    """Periodic box blur, radius r texels (3 passes ~ gaussian)."""
    if r < 1:
        return a
    out = a.astype(np.float32)
    for _ in range(3):
        for ax in (0, 1):
            c = np.cumsum(np.concatenate([out.take(range(-r - 1, 0), axis=ax), out, out.take(range(0, r), axis=ax)], axis=ax), axis=ax)
            n = out.shape[ax]
            hi = c.take(range(2 * r + 1, 2 * r + 1 + n), axis=ax)
            lo = c.take(range(0, n), axis=ax)
            out = (hi - lo) / (2 * r + 1)
    return out.astype(np.float32)


def normal_from_height(h, metres_per_tile, n):
    """h in metres; returns (n, n, 3) in 0..1 (OpenGL +Y). Periodic central differences."""
    texel = metres_per_tile / n
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) / (2 * texel)
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) / (2 * texel)
    nrm = np.stack([-dx, -dy, np.ones_like(h)], axis=-1)
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    return (nrm * 0.5 + 0.5).astype(np.float32)


def cavity_ao(h, n, radius_m, metres_per_tile, strength):
    """Height-based occlusion: how far a texel sits below its blurred neighbourhood, plus a broad term."""
    r = max(1, int(radius_m / metres_per_tile * n))
    hb = blur(h, r)
    hb2 = blur(h, r * 4)
    occ = np.clip((hb - h) * strength, 0, 1) * 0.75 + np.clip((hb2 - h) * strength * 0.5, 0, 1) * 0.25
    return np.clip(1.0 - occ, 0.0, 1.0).astype(np.float32)


# --------------------------------------------------------------------------- writing (Blender)


def save_png(path, rgb, alpha=None, srgb=True):
    """rgb: (n, n, 3) float, linear if srgb=True (encoded here), else stored as is. v-up rows."""
    import bpy
    n = rgb.shape[0]
    data = lin_to_srgb(rgb) if srgb else np.clip(rgb, 0, 1)
    a = np.ones((n, n, 1), np.float32) if alpha is None else np.clip(alpha, 0, 1)[..., None]
    px = np.concatenate([data, a], axis=-1).astype(np.float32)
    name = '_dmtex_tmp'
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    img = bpy.data.images.new(name, n, n, alpha=alpha is not None, float_buffer=False)
    img.colorspace_settings.name = 'Non-Color'   # store the values exactly as computed
    img.pixels.foreach_set(px.ravel())
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


def stats(name, rgb_lin, target_hex=None):
    m = rgb_lin.reshape(-1, 3).mean(axis=0)
    s = lin_to_srgb(m)
    h = '#%02x%02x%02x' % tuple(int(round(c * 255)) for c in s)
    t = ''
    if target_hex:
        tl = hex_lin(target_hex)
        t = ' target %s (luma ratio %.2f)' % (target_hex, float((m @ np.array([0.2126, 0.7152, 0.0722])) / max(1e-6, tl @ np.array([0.2126, 0.7152, 0.0722]))))
    return '%s mean albedo %s%s' % (name, h, t)
