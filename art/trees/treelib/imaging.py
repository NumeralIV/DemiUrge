# DEMIURGE tree families: numpy image helpers (Blender's bundled numpy; no other dependencies).
#
# Conventions: images are float32 arrays [rows, cols, C] in Blender's pixel order (row 0 = BOTTOM of the
# picture, like bpy.types.Image.pixels and Blender UVs). Colour is sRGB-encoded 0..1 unless a function says
# otherwise. Everything is deterministic for a given seed.
import os
import numpy as np


# ------------------------------------------------------------------ load / save through bpy
def load_png(path):
    """Returns float32 [h, w, 4] (raw stored values, i.e. sRGB for colour PNGs), row 0 = bottom."""
    import bpy
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)


def save_png(path, arr, name='tmp_save'):
    """arr float [h, w, 3|4] 0..1, row 0 = bottom. Writes an 8-bit RGBA PNG (no colour management)."""
    import bpy
    h, w = arr.shape[:2]
    if arr.shape[2] == 3:
        arr = np.concatenate([arr, np.ones((h, w, 1), np.float32)], axis=2)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img = bpy.data.images.new(name, w, h, alpha=True, float_buffer=False)
    img.colorspace_settings.name = 'Non-Color'   # write the bytes exactly as given
    img.pixels.foreach_set(np.clip(arr, 0, 1).astype(np.float32).ravel())
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


# ------------------------------------------------------------------ colour
def srgb_to_lin(c):
    c = np.asarray(c, np.float32)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(np.asarray(c, np.float32), 0, None)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


# ------------------------------------------------------------------ resampling and alpha
def downsample2(img):
    """2x2 box filter with alpha-premultiplied colour (no dark fringes). img [h, w, 4] sRGB colour + alpha."""
    h, w = img.shape[:2]
    a = img[..., 3:4]
    lin = srgb_to_lin(img[..., :3]) * a
    def box(x):
        return 0.25 * (x[0::2, 0::2] + x[1::2, 0::2] + x[0::2, 1::2] + x[1::2, 1::2])
    a2 = box(a)
    c2 = box(lin) / np.maximum(a2, 1e-6)
    return np.concatenate([lin_to_srgb(c2), a2], axis=2).astype(np.float32)


def dilate(img, thr=0.02):
    """Push-pull fill: transparent texels take the average colour of the nearest opaque ones (alpha unchanged).
    Prevents dark/black fringes in the mips of alpha-tested foliage."""
    h, w = img.shape[:2]
    a = img[..., 3]
    wgt = (a > thr).astype(np.float32)
    col = srgb_to_lin(img[..., :3]) * wgt[..., None]
    pyr_c, pyr_w = [col], [wgt]
    while pyr_c[-1].shape[0] > 1 and pyr_c[-1].shape[1] > 1:
        c, ww = pyr_c[-1], pyr_w[-1]
        if c.shape[0] % 2 or c.shape[1] % 2:
            break
        c2 = c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]
        w2 = ww[0::2, 0::2] + ww[1::2, 0::2] + ww[0::2, 1::2] + ww[1::2, 1::2]
        pyr_c.append(c2)
        pyr_w.append(w2)
    # pull: from coarse to fine, fill empty texels with the normalised coarse colour
    fill = pyr_c[-1] / np.maximum(pyr_w[-1], 1e-6)[..., None]
    for lv in range(len(pyr_c) - 2, -1, -1):
        c, ww = pyr_c[lv], pyr_w[lv]
        up = np.repeat(np.repeat(fill, 2, axis=0), 2, axis=1)[:c.shape[0], :c.shape[1]]
        mine = c / np.maximum(ww, 1e-6)[..., None]
        fill = np.where((ww > 0)[..., None], mine, up)
    out = img.copy()
    keep = (a > thr)[..., None]
    out[..., :3] = np.where(keep, img[..., :3], lin_to_srgb(fill))
    return out


# ------------------------------------------------------------------ tileable noise
def fft_noise(h, w, seed, beta=2.0, ax=1.0, ay=1.0, kmin=0.0, kmax=None):
    """Periodic (perfectly tileable) fractal noise, zero mean, unit std. ax/ay stretch frequencies along x/y
    (ax > 1 => finer detail across x, i.e. features elongated along y)."""
    rng = np.random.default_rng(seed)
    white = rng.standard_normal((h, w)).astype(np.float32)
    F = np.fft.rfft2(white)
    ky = np.fft.fftfreq(h)[:, None] * h
    kx = np.fft.rfftfreq(w)[None, :] * w
    k = np.sqrt((kx * ax) ** 2 + (ky * ay) ** 2)
    k[0, 0] = 1.0
    filt = k ** (-beta / 2.0)
    if kmin > 0:
        filt *= 1.0 - np.exp(-(k / kmin) ** 2)
    if kmax:
        filt *= np.exp(-(k / kmax) ** 2)
    F *= filt
    F[0, 0] = 0
    out = np.fft.irfft2(F, s=(h, w)).astype(np.float32)
    return out / (out.std() + 1e-8)


def worley(h, w, cells_x, cells_y, seed, stretch=(1.0, 1.0)):
    """Periodic Worley noise on a jittered grid: returns (F1, F2, cell id) in cell units."""
    rng = np.random.default_rng(seed)
    jit = rng.random((cells_y, cells_x, 2)).astype(np.float32)
    ids = rng.random((cells_y, cells_x)).astype(np.float32)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    gx = (xx + 0.5) / w * cells_x
    gy = (yy + 0.5) / h * cells_y
    ix = np.floor(gx).astype(np.int32)
    iy = np.floor(gy).astype(np.int32)
    f1 = np.full((h, w), 1e9, np.float32)
    f2 = np.full((h, w), 1e9, np.float32)
    idv = np.zeros((h, w), np.float32)
    sx, sy = stretch
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            cx = ix + dx
            cy = iy + dy
            wx = cx % cells_x
            wy = cy % cells_y
            px = cx + jit[wy, wx, 0]
            py = cy + jit[wy, wx, 1]
            d = np.sqrt(((gx - px) * sx) ** 2 + ((gy - py) * sy) ** 2)
            closer = d < f1
            f2 = np.where(closer, f1, np.minimum(f2, d))
            idv = np.where(closer, ids[wy, wx], idv)
            f1 = np.where(closer, d, f1)
    return f1, f2, idv


def height_to_normal(hgt, strength):
    """Tileable tangent-space normal map from a height field (row 0 = bottom). DirectX convention (UE): the green
    channel points DOWN the image, i.e. G = 0.5 + 0.5 * (+dh/dy_up) * strength / len."""
    dx = (np.roll(hgt, -1, axis=1) - np.roll(hgt, 1, axis=1)) * 0.5
    dy = (np.roll(hgt, -1, axis=0) - np.roll(hgt, 1, axis=0)) * 0.5   # +y = up the image (row 0 is the bottom)
    nx = -dx * strength
    ny = dy * strength            # DirectX: flip of the OpenGL (-dy) green
    nz = np.ones_like(hgt)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([0.5 + 0.5 * nx / ln, 0.5 + 0.5 * ny / ln, 0.5 + 0.5 * nz / ln, np.ones_like(hgt)], axis=2).astype(np.float32)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)
