# DEMIURGE texture pilot - make_maps.py: turn one AI-generated (Google Flow / Nano Banana) colour image into a
# production texture set: seamless, de-lit diffuse + height, normal (OpenGL and DirectX), roughness, AO, ORM.
#
# Runs in Blender's Python (numpy; bpy only to decode the JPEG/PNG input), headless:
#   blender -b --factory-startup --python art/textures-pilot/make_maps.py -- <input.jpg> <Name> <kind> <tile_m>
#           [--target=#rrggbb] [--size=2048] [--debug=<dir>] [--out=<dir>, default art/textures-pilot/out]
#           [--prefix=T_Shrine_]   (file name prefix; e.g. --prefix=T_ for a kit set)
#   blender -b --factory-startup --python art/textures-pilot/make_maps.py -- --pilot      (the four pilot inputs)
#   kind: stone | thatch   (picks the cut cost, the height remap and the roughness rule; add a kind for a new
#                           family of material, see KINDS)
# Writes art/textures-pilot/out/T_Shrine_<Name>_{D,H,N,N_DX,R,AO,ORM}.png at 2048 and the same names at 1024
# in out/1k/, plus out/<Name>.json (seam metrics before/after, albedo, tile size).
#
# THE RECIPE (every step is a function below, in this order):
#  1. decode, sRGB -> linear.
#  2. pre-flatten: divide luminance by a very wide blur (sigma = N/8, reflect padding) so the two image edges that
#     are about to meet have the same brightness (the generator bakes soft light gradients into the frame).
#  3. offset by half (np.roll N/2, N/2): the wrap seam becomes a cross in the middle of the tile and the tile's
#     outer edges are the image's untouched centre lines.
#  4. heal the horizontal arm, then the vertical arm: the strips either side of the seam (w px each, the image's
#     original top and bottom / left and right edges) are laid on top of each other and joined along a
#     minimum-cost cut found by dynamic programming. The cut is periodic (it closes on itself across the tile) and
#     its cost is colour difference + a material term: for stone, cutting through stone (instead of mortar) is
#     expensive, so the cut follows mortar joints and never halves a stone if a mortar path exists; for thatch,
#     the horizontal cut is cheap only along a course butt line (where one course's reed ends overlap the next,
#     which is what a real thatch course junction looks like) and the vertical cut runs between reeds.
#     The two sides are joined with multi-band blending (5 Gaussian bands, each band crossfaded over a width
#     proportional to its scale), so colour matches over a wide zone while reed and gravel detail stays sharp.
#     Each arm removes w px, so the tile is (N-wy) x (N-wx).
#  5. resample back to N x N with an FFT (exact for a periodic image: no seam can appear at the edges).
#  6. de-light: luminance high-pass at sigma = N/10 (periodic FFT blur), ratio clamped to 0.6..1.6, so a 4 x 4
#     tiling shows no light/dark repeat; optional cavity lift (photo crevice shadows are partly baked AO).
#  7. albedo calibration: scale the mean luma to the material's target (art direction sheet, linear space), keep
#     the photo's hue.
#  8. maps from the colour: height (kind-aware remap), normal (Sobel on height, periodic, OpenGL +Y; _N_DX has
#     green flipped for Unreal without the importer's flip), roughness (kind rule), AO (cavity from height),
#     ORM (R AO, G roughness, B metallic 0).
#  9. write 2048 and 1024 (2 x 2 box average of the 2048 maps; the normal is renormalised).
import json
import math
import os
import struct
import sys
import time
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'out')
LUMA = np.array([0.2126, 0.7152, 0.0722], np.float32)

# ------------------------------------------------------------------------------------------------ colour / io


def srgb_to_lin(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4).astype(np.float32)


def lin_to_srgb(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055).astype(np.float32)


def hex_lin(h):
    h = h.lstrip('#')
    return srgb_to_lin(np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], np.float32))


def lin_hex(c):
    s = lin_to_srgb(np.asarray(c, np.float32))
    return '#%02x%02x%02x' % tuple(int(round(float(v) * 255)) for v in s)


def load_image(path):
    """-> float32 (H, W, 3) sRGB-encoded 0..1, row 0 = top. Uses Blender's decoder (JPEG, PNG, ...)."""
    import bpy
    img = bpy.data.images.load(path)
    w, h = img.size
    px = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(px)   # byte images: raw stored values (no colour management)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)[::-1, :, :3].copy()


def _png_chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)


def save_png(path, a, bits=8):
    """a: (H, W) or (H, W, 3|4) float 0..1, row 0 = top, values stored as given (encode sRGB yourself)."""
    a = np.clip(np.asarray(a, np.float32), 0, 1)
    if a.ndim == 2:
        a = a[..., None]
    h, w, c = a.shape
    ctype = {1: 0, 3: 2, 4: 6}[c]
    if bits == 16:
        q = np.round(a * 65535).astype('>u2')
    else:
        q = np.round(a * 255).astype(np.uint8)
    raw = np.concatenate([np.zeros((h, 1), np.uint8), q.reshape(h, -1).view(np.uint8)], axis=1).tobytes()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(_png_chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, bits, ctype, 0, 0, 0)))
        f.write(_png_chunk(b'IDAT', zlib.compress(raw, 6)))
        f.write(_png_chunk(b'IEND', b''))


def luma(rgb):
    return (rgb @ LUMA).astype(np.float32)

# ------------------------------------------------------------------------------------------------ filters


def gblur(a, sigma, wrap=(True, True)):
    """Gaussian blur over axes 0, 1 by FFT (sigma: number or (sy, sx)). Axes with wrap=False are
    reflect-padded by 3 sigma first."""
    sy, sx = (sigma, sigma) if np.isscalar(sigma) else sigma
    if max(sy, sx) <= 0:
        return a.astype(np.float32)
    a = a.astype(np.float32)
    pad = int(math.ceil(3 * max(sy, sx)))
    pads = [(0, 0) if wrap[i] else (min(pad, a.shape[i] - 1),) * 2 for i in (0, 1)] + [(0, 0)] * (a.ndim - 2)
    p = np.pad(a, pads, mode='reflect') if any(not w for w in wrap) else a
    fy = np.fft.fftfreq(p.shape[0])[:, None]
    fx = np.fft.rfftfreq(p.shape[1])[None, :]
    g = np.exp(-2.0 * math.pi ** 2 * ((sx * fx) ** 2 + (sy * fy) ** 2)).astype(np.float32)
    if p.ndim == 3:
        g = g[..., None]
    out = np.fft.irfft2(np.fft.rfft2(p, axes=(0, 1)) * g, s=p.shape[:2], axes=(0, 1)).astype(np.float32)
    sl = tuple(slice(pads[i][0], pads[i][0] + a.shape[i]) for i in (0, 1))
    return out[sl]


def fft_resize(a, nh, nw):
    """Band-limited resize of a PERIODIC image (exact tiling preserved)."""
    h, w = a.shape[:2]
    F = np.fft.fft2(a, axes=(0, 1))
    F = np.fft.fftshift(F, axes=(0, 1))
    out = np.zeros((nh, nw) + a.shape[2:], np.complex128)
    ch, cw = min(h, nh), min(w, nw)
    sy, sx = h // 2 - ch // 2, w // 2 - cw // 2
    dy, dx = nh // 2 - ch // 2, nw // 2 - cw // 2
    out[dy:dy + ch, dx:dx + cw] = F[sy:sy + ch, sx:sx + cw]
    out = np.fft.ifftshift(out, axes=(0, 1))
    r = np.fft.ifft2(out, axes=(0, 1)).real * (nh * nw) / (h * w)
    return r.astype(np.float32)


def half(a):
    """2 x 2 box average (periodic-safe for even sizes)."""
    return (0.25 * (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2])).astype(np.float32)

# ------------------------------------------------------------------------------------------------ metrics


def seam_score(rgb):
    """Seam visibility per axis. For every pixel boundary line (including the wrap) take the mean luminance step
    across it; report wrap = step at the wrap / median step and worst = the largest line / median (a straight
    healed seam would show up there too). 1.0 = indistinguishable from any other column/row boundary."""
    L = luma(rgb) if rgb.ndim == 3 else rgb
    out = {}
    for ax, k in ((1, 'x'), (0, 'y')):
        p = np.abs(np.roll(L, -1, axis=ax) - L).mean(axis=1 - ax)      # p[i] = step between i and i+1
        med = float(np.median(p))
        out[k + '_wrap'] = round(float(p[-1]) / med, 2)
        out[k + '_worst'] = round(float(p.max()) / med, 2)
    return out


def light_repeat(rgb):
    """Std of the very-low-pass luminance relative to the mean: how much a light/dark blotch repeats per tile."""
    L = luma(rgb)
    lo = gblur(L, L.shape[0] / 10.0)
    return round(float(lo.std() / lo.mean()), 4)

# ------------------------------------------------------------------------------------------------ material kinds


def stone_mask(lin, n, wrap=(True, True), mortar_frac=0.46):
    """Soft 0..1 'this is a stone' mask. In these images the stones are lighter and greyer than the brown clay
    bedding at the 5-70 px scale: score = local brightness (z) - 0.6 x saturation (z), threshold so ~42 % is
    mortar, then a morphological closing (fills lichen spots inside stones) and opening (drops gravel)."""
    s = n / 2048.0
    L = luma(lin)
    Lb = gblur(L, 4 * s, wrap) / (gblur(L, 70 * s, wrap) + 1e-4)
    c = gblur(lin, 4 * s, wrap)
    sat = 1 - c.min(-1) / (c.max(-1) + 1e-4)
    z = lambda a: (a - a.mean()) / (a.std() + 1e-6)
    Ls = gblur(L, 2 * s, wrap)
    gy, gx = np.gradient(Ls)
    edge = gblur(np.sqrt(gx * gx + gy * gy) / (gblur(L, 70 * s, wrap) + 1e-4), 3 * s, wrap)
    sc = z(Lb) - 0.6 * z(sat) - 0.5 * z(edge)              # stone outlines and gravel are busy, stone faces calm
    m = (sc > np.percentile(sc, mortar_frac * 100)).astype(np.float32)
    m = (gblur(m, 5 * s, wrap) > 0.3).astype(np.float32)       # closing: dilate ...
    m = (gblur(m, 5 * s, wrap) > 0.7).astype(np.float32)       # ... then erode
    m = (gblur(m, 7 * s, wrap) > 0.65).astype(np.float32)      # opening: erode (splits touching stones) ...
    m = (gblur(m, 6 * s, wrap) > 0.4).astype(np.float32)       # ... then dilate
    m = np.maximum(m, fill_holes(m, n, wrap))
    return np.clip(gblur(m, 1.5 * s, wrap), 0, 1).astype(np.float32)


def fill_holes(m, n, wrap, max_frac=0.0015):
    """Small mortar patches (dark lichen or pits inside a stone read as 'mortar') -> stone. Connected
    components of the mortar on a 4x smaller grid (BFS, periodic where the image is); components smaller than
    max_frac of the tile are filled."""
    from collections import deque
    f = 4
    H, W = m.shape
    hh, ww = H // f, W // f
    mort = (m[:hh * f, :ww * f].reshape(hh, f, ww, f).mean((1, 3)) < 0.5)
    lab = np.full(hh * ww, -1, np.int32)
    flat = mort.ravel()
    sizes = []
    for start in np.flatnonzero(flat):
        if lab[start] >= 0:
            continue
        k = len(sizes)
        lab[start] = k
        q = deque([start])
        cnt = 0
        while q:
            p = q.popleft()
            cnt += 1
            y, x = divmod(int(p), ww)
            for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if yy < 0 or yy >= hh:
                    if not wrap[0]:
                        continue
                    yy %= hh
                if xx < 0 or xx >= ww:
                    if not wrap[1]:
                        continue
                    xx %= ww
                r = yy * ww + xx
                if flat[r] and lab[r] < 0:
                    lab[r] = k
                    q.append(r)
        sizes.append(cnt)
    sizes = np.array(sizes + [0])
    small = (sizes < max_frac * hh * ww * (n * n) / (H * W)) & (sizes < sizes.max())
    holes = (small[lab] & flat).reshape(hh, ww).astype(np.float32)
    up = np.repeat(np.repeat(holes, f, 0), f, 1)
    out = np.zeros_like(m)
    out[:up.shape[0], :up.shape[1]] = up
    return out


def butt_lines(lin, n, wrap=(True, True)):
    """Thatch: 'a course's butt ends end here' map. Butt ends are bright, the shadow below them dark, and the
    reeds run vertically, so the signal is a strong bright-over-dark vertical step pooled horizontally."""
    L = luma(lin)
    s = n / 2048.0
    Lv = gblur(L, 4 * s, wrap)
    k = max(2, int(10 * s))
    up = np.pad(Lv, ((k, k), (0, 0)), mode='wrap' if wrap[0] else 'edge')
    step = up[:-2 * k] - up[2 * k:]                   # L[y-k] - L[y+k]: bright above, dark below
    step = np.clip(step / (Lv.mean() + 1e-4), 0, None)
    acc = gblur(step, (2 * s, 30 * s), wrap)          # pool along the butt line (horizontal)
    return np.clip(acc / (np.percentile(acc, 99) + 1e-6), 0, 1).astype(np.float32)


KINDS = {
    # w: overlap per arm as a fraction of N (y arm, x arm); lam: weight of the material term in the cut cost
    # tilt_deg: mean angle of the normal map from flat. Height derived from a photo is dominated by pixel-scale
    #   detail (and JPEG noise); taken at its metric relief it gave a mean tilt of 61-67 deg (procedural sets:
    #   rubble 23, thatch 31, limestone 8), which darkened every Flow surface by about 30 % in Cycles. The normal
    #   is now computed from a lightly pre-blurred height and its strength scaled down until the mean tilt is
    #   tilt_deg (never stronger than relief_m). Independent check, 2026-11-19.
    # rough (cavities / mortar, body, tops): stone body and tops raised from 0.80 / 0.66 so the set's mean
    #   (about 0.9) matches the project's procedural rubble (0.917); 0.64 tops read as wet stone.
    'stone': dict(w=(0.16, 0.16), lam=2.5, rough=(0.96, 0.88, 0.78), relief_m=0.045, delit_cavity=0.35,
                  tilt_deg=24.0),
    'thatch': dict(w=(0.20, 0.12), lam=2.0, rough=(0.97, 0.88, 0.80), relief_m=0.025, delit_cavity=0.15,
                   tilt_deg=30.0),
}

# ------------------------------------------------------------------------------------------------ seam healing


def min_cut(E, periodic=True, max_step=2, starts=12, jump=None):
    """E: (w, W) cost; returns cut[x] in 0..w-1 minimising sum E[cut[x], x] with |cut[x+1]-cut[x]| <= max_step,
    except at columns where jump[x] is finite: there the cut may move to any row for that extra cost (thatch:
    a course butt line hides a sideways step, so vertical joins stagger from course to course like bundles).
    periodic: the cut must end within 1 row of where it started (the strip wraps across the tile)."""
    w, W = E.shape
    order = np.argsort(E[:, 0])
    cand = []
    for r in order:
        if all(abs(int(r) - c) > 3 for c in cand):
            cand.append(int(r))
        if len(cand) >= starts:
            break
    K = len(cand) if periodic else 1
    INF = np.float32(1e30)
    acc = np.full((K, w), INF, np.float32)
    if periodic:
        acc[np.arange(K), cand] = E[cand, 0]
    else:
        acc[0] = E[:, 0]
    back = np.zeros((W, K, w), np.int16)
    rows = np.arange(w)
    steps = list(range(-max_step, max_step + 1))
    pen = {d: abs(d) * 0.02 * float(np.median(E)) for d in steps}
    for x in range(1, W):
        best = np.full((K, w), INF, np.float32)
        arg = np.broadcast_to(rows, (K, w)).astype(np.int16)
        for d in steps:
            # coming from row y-d at column x-1
            sh = np.full((K, w), INF, np.float32)
            if d > 0:
                sh[:, d:] = acc[:, :-d]
            elif d < 0:
                sh[:, :d] = acc[:, -d:]
            else:
                sh[:] = acc
            sh = sh + pen[d]
            better = sh < best
            best = np.where(better, sh, best)
            arg = np.where(better, (rows - d)[None, :].astype(np.int16), arg)
        if jump is not None and np.isfinite(jump[x]):
            src = acc.argmin(1)
            jv = acc[np.arange(K), src] + np.float32(jump[x])
            better = jv[:, None] < best
            best = np.where(better, jv[:, None], best)
            arg = np.where(better, src[:, None].astype(np.int16), arg)
        acc = best + E[:, x][None, :]
        back[x] = arg
    if periodic:
        tot = np.full(K, INF, np.float32)
        endr = np.zeros(K, int)
        for k, r0 in enumerate(cand):
            lo, hi = max(0, r0 - 1), min(w, r0 + 2)
            j = lo + int(np.argmin(acc[k, lo:hi]))
            tot[k], endr[k] = acc[k, j], j
        k = int(np.argmin(tot))
        y = int(endr[k])
    else:
        k, y = 0, int(np.argmin(acc[0]))
    cut = np.zeros(W, int)
    for x in range(W - 1, -1, -1):
        cut[x] = y
        y = int(back[x, k, y])
    return cut


def orientation(lin, n, wrap):
    """Local stroke angle (structure tensor, radians) and coherence 0..1."""
    L = luma(lin)
    s = n / 2048.0
    Ls = gblur(L, 1.0 * s, wrap)
    gy, gx = np.gradient(Ls)
    jxx, jyy, jxy = (gblur(a, 8 * s, wrap) for a in (gx * gx, gy * gy, gx * gy))
    th = 0.5 * np.arctan2(2 * jxy, jxx - jyy)
    coh = np.sqrt((jxx - jyy) ** 2 + 4 * jxy ** 2) / (jxx + jyy + 1e-9)
    return th, coh


def multiband(A, B, M, sigmas=(1.2, 3.5, 9.0, 22.0, 50.0)):
    """Blend A (where M=1) and B (M=0); strips are periodic along axis 1 only. Band k = G_{k-1} - G_k, blended
    with the mask blurred at that band's scale (Burt-Adelson without decimation)."""
    wrap = (False, True)
    out = np.zeros_like(A)
    pa, pb = A, B
    prev_s = 0.0
    for s in sigmas:
        ga, gb = gblur(A, s, wrap), gblur(B, s, wrap)
        mk = gblur(M, max(0.7, prev_s), wrap)[..., None]
        # variance-preserving crossfade (Heitz & Neyret 2018): a plain 50/50 mix of two unrelated detail bands
        # halves their contrast, which left a soft, detail-less smear along every cut (independent check)
        out += ((pa - ga) * mk + (pb - gb) * (1 - mk)) / np.sqrt(mk * mk + (1 - mk) ** 2)
        pa, pb, prev_s = ga, gb, s
    mk = gblur(M, prev_s, wrap)[..., None]
    out += pa * mk + pb * (1 - mk)
    return out.astype(np.float32)


def heal_rows(S, w, kind, lam, n_full, dbg=None, tag=''):
    """S is periodic along axis 1 and has a seam between rows c-1 and c (c = H/2). Overlap the w rows above
    (A) with the w rows below (B) and join along a min-cost cut. Returns an image H-w rows high."""
    H = S.shape[0]
    c = H // 2
    A = S[c - w:c]
    B = S[c:c + w]
    diff = np.sqrt(((A - B) ** 2).sum(-1))
    diff = gblur(diff, 1.5 * n_full / 2048, (False, True))
    E = diff / (diff.mean() + 1e-6)
    if kind == 'stone':
        mA, mB = stone_mask(A, n_full, (False, True)), stone_mask(B, n_full, (False, True))
        E = E + lam * (mA + mB)                    # cut through mortar in both, never across a stone
    elif kind == 'thatch' and tag.startswith('y'):
        # cut just under A's butt line (A's course ends there, B's course shows below) or just over B's
        bA, bB = butt_lines(A, n_full, (False, True)), butt_lines(B, n_full, (False, True))
        E = E * 0.5 + lam * (1.0 - np.maximum(bA, bB))
    jump = None
    if kind == 'thatch' and tag.startswith('x'):
        # the strip is transposed (strip rows = x across the seam, columns = y down the roof)
        At, Bt = A.transpose(1, 0, 2), B.transpose(1, 0, 2)
        tA, cA = orientation(At, n_full, (True, False))
        tB, cB = orientation(Bt, n_full, (True, False))
        E = E + lam * (np.abs(np.sin(tA - tB)) * np.minimum(cA, cB)).T * 2.0   # reeds must lean the same way
        b = np.maximum(butt_lines(At, n_full, (True, False)), butt_lines(Bt, n_full, (True, False))).mean(1)
        jump = np.where(b > 0.5 * b.max(), 1.0, np.inf)
    # keep the cut away from the strip ends so every blend band has room
    m = max(8, int(w * 0.12))
    ramp = np.ones(w, np.float32)
    ramp[:m] = np.linspace(4, 1, m)
    ramp[-m:] = np.linspace(1, 4, m)
    E = E + (ramp[:, None] - 1) * 3.0
    cut = min_cut(E.astype(np.float32), periodic=True, max_step=2 if kind == 'stone' else 1, jump=jump)
    M = (np.arange(w)[:, None] < cut[None, :]).astype(np.float32)
    C = multiband(A, B, M)
    if dbg is not None:
        vis = lin_to_srgb(C).copy()
        yy = cut
        vis[np.clip(yy, 0, w - 1), np.arange(len(yy))] = (1, 0, 0)
        save_png(os.path.join(dbg, 'cut_%s.png' % tag), vis)
        save_png(os.path.join(dbg, 'cost_%s.png' % tag), np.clip(E / (np.percentile(E, 99) + 1e-6), 0, 1))
    return np.concatenate([S[:c - w], C, S[c + w:]], axis=0), float(E[cut, np.arange(len(cut))].mean())


def make_seamless(lin, kind, dbg=None):
    n = lin.shape[0]
    P = KINDS[kind]
    wy, wx = int(P['w'][0] * n), int(P['w'][1] * n)
    S = np.roll(lin, (n // 2, n // 2), axis=(0, 1))      # 3. offset by half: the seam is now a centred cross
    if dbg:
        save_png(os.path.join(dbg, 'offset.png'), lin_to_srgb(S))
    S, cy = heal_rows(S, wy, kind, P['lam'], n, dbg, 'y')                       # horizontal arm
    S, cx = heal_rows(S.transpose(1, 0, 2), wx, kind, P['lam'], n, dbg, 'x')     # vertical arm
    S = S.transpose(1, 0, 2)
    S = fft_resize(S, n, n)                                                     # 5. back to N x N, periodic
    return np.clip(S, 0, 1), dict(overlap_px=[wy, wx], cut_cost=[round(cy, 3), round(cx, 3)])

# ------------------------------------------------------------------------------------------------ de-light


def flatten(lin, sigma, wrap, lo=0.6, hi=1.6):
    L = luma(lin)
    low = gblur(L, sigma, wrap)
    r = np.clip(L.mean() / (low + 1e-5), lo, hi)
    return (lin * r[..., None]).astype(np.float32)

# ------------------------------------------------------------------------------------------------ maps


def height_map(lin, kind, n):
    """0..1 height, periodic. Stone: domed stones (smooth mask) over a low, gravelly mortar, plus surface detail
    from luminance band-pass. Thatch: luminance band-pass (reeds up, gaps down) plus a ramp that rises to each
    course's butt line."""
    L = luma(lin)
    s = n / 2048.0
    Ln = L / (gblur(L, 60 * s) + 1e-4)
    fine = Ln - gblur(Ln, 3 * s)
    mid = gblur(Ln, 2 * s) - gblur(Ln, 25 * s)
    if kind == 'stone':
        m = stone_mask(lin, n)
        dome = gblur(m, 14 * s)
        dome = np.sqrt(np.clip(dome, 0, 1))                    # rounded fieldstones: steep sides, flat-ish tops
        h = 0.62 * dome + 0.22 * np.tanh(mid * 3) * (0.5 + 0.5 * m) + 0.10 * np.tanh(fine * 6)
        h += 0.08 * (1 - m) * np.tanh(mid * 4)                 # gravel in the clay
    else:
        b = butt_lines(lin, n)
        # course ramp: a course is thin where it comes out from under the course above and thickest at its own
        # butt ends, then the surface drops into the shadow under them. So integrate the butt signal UPWARD
        # (toward the ridge, image top) with decay -> high just above each butt line, low just under it.
        # (Independent check: the first version integrated downward, i.e. high just under each butt line, which
        # is inverted; the luminance term hid it in the total height.)
        ramp = np.zeros_like(b)
        acc = np.zeros(b.shape[1], np.float32)
        decay = math.exp(-1.0 / (180 * s))
        for _ in range(2):                                     # two passes so the periodic wrap is settled
            for y in range(b.shape[0] - 1, -1, -1):
                acc = np.maximum(acc * decay, b[y])
                ramp[y] = acc
        ramp = gblur(ramp, 3 * s)
        h = 0.35 * ramp + 0.45 * np.tanh(mid * 4) + 0.20 * np.tanh(fine * 6)
    h = (h - h.min()) / (h.max() - h.min() + 1e-6)
    return h.astype(np.float32)


def normal_map(h, relief_m, tile_m, tilt_deg=None, pre_sigma=1.0):
    """Sobel on height (periodic). Returns OpenGL (+Y = up the texture) and DirectX (green flipped) in 0..1, and
    the strength used (fraction of relief_m). The height is pre-blurred by pre_sigma px (at 2048) so JPEG and
    single-pixel noise do not become steep normals; with tilt_deg the strength is lowered until the mean angle
    from flat is tilt_deg (it is never raised above the metric relief)."""
    n = h.shape[0]
    texel = tile_m / n
    hm = (gblur(h, pre_sigma * n / 2048.0) if pre_sigma > 0 else h) * relief_m
    r = lambda a, dy, dx: np.roll(a, (dy, dx), axis=(0, 1))
    # Sobel: d/dx (columns, right) and d/drow (rows, down)
    gx = (r(hm, 1, -1) + 2 * r(hm, 0, -1) + r(hm, -1, -1) - r(hm, 1, 1) - 2 * r(hm, 0, 1) - r(hm, -1, 1)) / (8 * texel)
    gr = (r(hm, -1, 1) + 2 * r(hm, -1, 0) + r(hm, -1, -1) - r(hm, 1, 1) - 2 * r(hm, 1, 0) - r(hm, 1, -1)) / (8 * texel)
    k = 1.0
    if tilt_deg:
        g = np.sqrt(gx * gx + gr * gr)[::2, ::2]           # every 4th texel is plenty for the mean
        tilt = lambda kk: float(np.degrees(np.arctan(kk * g)).mean())
        if tilt(1.0) > tilt_deg:
            lo, hi = 0.0, 1.0
            for _ in range(25):
                mid = 0.5 * (lo + hi)
                lo, hi = (mid, hi) if tilt(mid) < tilt_deg else (lo, mid)
            k = 0.5 * (lo + hi)
    # texture v runs UP (row 0 is the top), so dh/dv = -dh/drow
    nx, ny = -gx * k, gr * k
    nz = np.ones_like(h)
    l = np.sqrt(nx * nx + ny * ny + nz * nz)
    gl = np.stack([nx / l, ny / l, nz / l], -1) * 0.5 + 0.5
    dx = gl.copy()
    dx[..., 1] = 1.0 - dx[..., 1]
    return gl.astype(np.float32), dx.astype(np.float32), k


def ao_map(h, n, mean_occ=0.11):
    """Cavity AO from height at three radii, normalised so the mean occlusion is mean_occ (the vertex AO and the
    engine's own AO do the large-scale part)."""
    s = n / 2048.0
    occ = 0.0
    for rad, k, wgt in ((4, 5.0, 0.5), (16, 3.0, 0.3), (48, 2.0, 0.2)):
        occ = occ + wgt * np.clip((gblur(h, rad * s) - h) * k, 0, 1)
    occ = occ * min(2.0, mean_occ / (float(occ.mean()) + 1e-6))
    return np.clip(1.0 - occ, 0, 1).astype(np.float32)


def rough_map(lin, h, kind, n):
    lo_, mid_, hi_ = KINDS[kind]['rough']   # (cavities / mortar, body, tops)
    if kind == 'stone':
        m = stone_mask(lin, n)
        top = np.clip((h - 0.55) / 0.35, 0, 1)
        r = lo_ * (1 - m) + m * (mid_ + (hi_ - mid_) * top)
    else:
        cav = 1 - ao_map(h, n)
        top = np.clip((h - 0.5) / 0.4, 0, 1)
        r = mid_ + (lo_ - mid_) * np.clip(cav * 3, 0, 1) + (hi_ - mid_) * top * 0.6
    L = luma(lin)
    r = r + 0.03 * np.tanh((gblur(L, 2) - L) / (L.mean() + 1e-4) * 4)     # micro variation
    return np.clip(r, 0, 1).astype(np.float32)

# ------------------------------------------------------------------------------------------------ driver


def process(src, name, kind, tile_m, target=None, size=2048, dbg=None, out=None, prefix='T_Shrine_'):
    t0 = time.time()
    OUT = out or globals()['OUT']
    P = KINDS[kind]
    srgb = load_image(src)
    if srgb.shape[0] != srgb.shape[1]:
        k = min(srgb.shape[:2])
        srgb = srgb[:k, :k]
    lin = srgb_to_lin(srgb)
    if lin.shape[0] != size:
        lin = np.clip(fft_resize(lin, size, size), 0, 1)       # not periodic yet, fine for a pre-scale only
    n = size
    rep = dict(source=os.path.relpath(src, HERE).replace('\\', '/'), name=name, kind=kind, tile_m=tile_m,
               seam_before=seam_score(lin), light_repeat_before=light_repeat(lin))
    # 2. pre-flatten (not periodic yet: reflect padding)
    lin = flatten(lin, n / 8.0, (False, False), 0.7, 1.4)
    # 3-5. seamless
    if dbg:
        os.makedirs(dbg, exist_ok=True)
    lin, info = make_seamless(lin, kind, dbg)
    rep.update(info)
    # 6. de-light (periodic now)
    lin = flatten(lin, n / 10.0, (True, True))
    h = height_map(lin, kind, n)
    ao = ao_map(h, n)
    if P['delit_cavity'] > 0:                                  # lift crevice shadow the photo baked in
        lift = 1.0 / (1.0 - P['delit_cavity'] * (1.0 - ao))
        lin = lin * np.clip(lift, 1, 1.5)[..., None]
    # 7. albedo calibration
    mean = lin.reshape(-1, 3).mean(0)
    if target:
        tl = hex_lin(target)
        lin = lin * float((tl @ LUMA) / (mean @ LUMA))
        # half-way hue match: the photo's own colour variation stays, the average cast moves toward the sheet
        m2 = lin.reshape(-1, 3).mean(0)
        tint = np.sqrt(tl / (m2 + 1e-6))
        tint = tint * float((m2 @ LUMA) / ((m2 * tint) @ LUMA))
        lin = lin * tint
    lin = np.clip(lin, 0, 1)
    rep['albedo_mean'] = lin_hex(lin.reshape(-1, 3).mean(0))
    rep['albedo_target'] = target
    rep['seam_after'] = seam_score(lin)
    rep['light_repeat_after'] = light_repeat(lin)
    # 8. maps
    h = height_map(lin, kind, n)
    if dbg:
        save_png(os.path.join(dbg, 'mask.png'), stone_mask(lin, n) if kind == 'stone' else butt_lines(lin, n))
    ao = ao_map(h, n)
    rough = rough_map(lin, h, kind, n)
    ngl, ndx, nk = normal_map(h, P['relief_m'], tile_m, P.get('tilt_deg'))
    rep['normal_strength'] = round(nk, 4)                     # fraction of relief_m the normal map uses
    rep['normal_tilt_deg'] = round(float(np.degrees(np.arccos(np.clip(ngl[..., 2] * 2 - 1, -1, 1))).mean()), 1)
    orm = np.stack([ao, rough, np.zeros_like(ao)], -1)
    maps = dict(D=lin_to_srgb(lin), H=h, N=ngl, N_DX=ndx, R=rough, AO=ao, ORM=orm)
    for res, sub in ((n, ''), (n // 2, '1k')):
        for k, a in maps.items():
            if res != n:
                if k == 'D':
                    a = lin_to_srgb(half(lin))
                else:
                    a = half(a)
                if k in ('N', 'N_DX'):
                    v = a * 2 - 1
                    v /= np.linalg.norm(v, axis=-1, keepdims=True)
                    a = v * 0.5 + 0.5
            save_png(os.path.join(OUT, sub, '%s%s_%s.png' % (prefix, name, k)), a, 16 if k == 'H' else 8)
    rep['rough_mean'] = round(float(rough.mean()), 3)
    rep['ao_mean'] = round(float(ao.mean()), 3)
    rep['seconds'] = round(time.time() - t0, 1)
    with open(os.path.join(OUT, '%s.json' % name), 'w') as f:
        json.dump(rep, f, indent=1)
    print('MAKE_MAPS', json.dumps(rep))
    return rep


PILOT = [
    # input, Name, kind, tile size (m), albedo target (T-014 sheet)
    ('flow/stone-a.jpg', 'StoneA', 'stone', 1.4, '#8a6c4c'),
    ('flow/stone-b.jpg', 'StoneB', 'stone', 2.0, '#8a6c4c'),
    ('flow/thatch-a.jpg', 'ThatchA', 'thatch', 1.0, '#9a7a4a'),
    ('flow/thatch-b.jpg', 'ThatchB', 'thatch', 1.1, '#9a7a4a'),
]


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    flags = dict(a[2:].split('=', 1) for a in argv if a.startswith('--') and '=' in a)
    pos = [a for a in argv if not a.startswith('--')]
    only = flags.get('only')
    if '--pilot' in argv:
        for src, name, kind, tile, tgt in PILOT:
            if only and name not in only.split(','):
                continue
            d = os.path.join(flags['debug'], name) if 'debug' in flags else None
            process(os.path.join(HERE, src), name, kind, tile, tgt, int(flags.get('size', 2048)), d,
                    os.path.abspath(flags['out']) if 'out' in flags else None)
    else:
        src, name, kind, tile = pos[0], pos[1], pos[2], float(pos[3])
        process(os.path.abspath(src), name, kind, tile, flags.get('target'), int(flags.get('size', 2048)),
                flags.get('debug'), os.path.abspath(flags['out']) if 'out' in flags else None,
                flags.get('prefix', 'T_Shrine_'))


if __name__ == '__main__':
    main()
