# DEMIURGE props - tileable texture sets of the Prop family (numpy, runs inside Blender's Python).
#
# Every set is 1024 x 1024 and periodic, built from a fixed seed: T_Prop_<Name>_D (sRGB base colour, alpha
# for the two card atlases), _N (tangent normal, OpenGL +Y; UE flips green on import) and _ORM (linear:
# R ambient occlusion, G roughness, B metallic). Wood and Straw reuse the shared generators of
# art/tools/make_textures.py (oak, thatch) so the props match the shrine and the building kits.
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import dmtex as T  # noqa: E402
from dmtex import hex_lin as H, sstep, mix3, blur  # noqa: E402
import make_textures as MT  # noqa: E402  (shared generators, read-only use)

N = 1024


def ramp(t, cols):
    return MT.ramp(t, cols)


def dilate(col, al):
    """Push colour into transparent texels so mips do not fringe dark."""
    filled = al > 0.5
    col = col.copy()
    for r in (2, 4, 8, 16, 32):
        wsum = blur(filled.astype(np.float32), r)
        csum = np.stack([blur(col[..., i] * filled, r) for i in range(3)], -1)
        est = csum / np.maximum(wsum, 1e-4)[..., None]
        col = np.where(filled[..., None], col, np.where((wsum > 1e-3)[..., None], est, col))
        filled = filled | (wsum > 1e-3)
    return col


# --------------------------------------------------------------------------- rock (detail, tiles every 2 m)
def rock(n):
    t = T.Tex(n, 7101)
    tile = 2.0
    wu = t.u + t.fbm(6, 6, 4) * 0.03
    wv = t.v + t.fbm(6, 6, 4, u=t.u + 0.41) * 0.03
    f1, f2, cid, _, _ = t.worley(9, 9, u=wu, v=wv, jitter=0.95)
    edge = (f2 - f1) * (tile / 9)
    crack = 1.0 - sstep(0.002, 0.009, edge + t.fbm(50, 50, 3) * 0.003)
    crack *= sstep(0.3, 0.65, t.fbm(5, 5, 4) * 0.5 + 0.5)   # only some cell borders are open fractures
    rid = t.cell_rand(cid, 81)
    facets = t.ridged(14, 14, 5) * 0.03 + t.fbm(60, 60, 4) * 0.004 + t.fbm(200, 200, 2) * 0.0008
    plates = (rid - 0.5) * 0.004 + sstep(0.0, 0.05, edge) * 0.003
    h = facets + plates - crack * 0.008
    # granite: grey-beige with feldspar flecks, darker mafic grains, lichen rosettes
    tone = np.clip(0.45 + t.fbm(8, 8, 5) * 0.55 + (rid - 0.5) * 0.08 + t.ridged(3, 3, 4) * 0.2 - 0.1, 0, 1)
    bc = ramp(tone, ['#5b564e', '#777166', '#8c857a', '#a19a8d'])
    grains = t.fbm(260, 260, 2)
    bc *= (1.0 + grains[..., None] * 0.22)
    dark = np.clip(t.fbm(180, 180, 2) - 0.45, 0, 1) * 3.0
    bc = mix3(bc, H('#2e2c2a'), np.clip(dark, 0, 0.8))
    lich_m = np.clip(t.fbm(22, 22, 4) * 2.2 - 0.75, 0, 1) * np.clip(t.fbm(90, 90, 3) * 2 + 0.6, 0, 1)
    bc = mix3(bc, H('#a3a383'), lich_m * 0.75)
    lich_o = np.clip(t.fbm(30, 30, 4, u=t.u + 0.3) * 2.5 - 1.15, 0, 1)
    bc = mix3(bc, H('#9a6a38'), lich_o * 0.6)
    bc = mix3(bc, H('#3a3530'), crack * 0.4)
    rough = 0.86 + t.fbm(30, 30, 3) * 0.05 + lich_m * 0.06 - (1 - crack) * 0.0
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=40.0, target='#77716a')


# --------------------------------------------------------------------------- moss (tiles every 1 m)
def moss(n):
    t = T.Tex(n, 7202)
    tile = 1.0
    f1, f2, cid, _, _ = t.worley(26, 26, jitter=0.9)
    rid = t.cell_rand(cid, 26 * 26)
    cush = blur(np.sqrt(np.clip(1.0 - f1 * 1.3, 0, 1)) * (0.006 + rid * 0.006), 3)
    fuzz = t.fbm(300, 300, 2) * 0.0012
    h = cush + fuzz + t.fbm(10, 10, 4) * 0.004
    tone = np.clip(0.42 + t.fbm(12, 12, 4) * 0.6 + blur(rid, 4) * 0.2 - 0.1 + cush * 30, 0, 1)
    bc = ramp(tone, ['#2c3416', '#434f1e', '#5c6a26', '#7c8436'])
    bc *= (1 + t.fbm(200, 200, 2)[..., None] * 0.2)
    dry = np.clip(t.fbm(5, 5, 4) * 2 - 0.4, 0, 1)
    bc = mix3(bc, H('#7a6e40'), dry * 0.45)
    rough = 0.94 + t.fbm(40, 40, 2) * 0.03
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=60.0, target='#4d5a22')


# --------------------------------------------------------------------------- smooth carved stone (statues, plinths)
def stone(n):
    t = T.Tex(n, 7303)
    tile = 2.0
    veins = np.clip(1.0 - np.abs(t.fbm(5, 5, 5, u=t.u + t.fbm(4, 4, 3) * 0.2) * 3.0), 0, 1) ** 8
    pits = np.clip(-t.fbm(140, 140, 3) - 0.4, 0, 1) * -0.002
    h = t.fbm(10, 10, 4) * 0.0015 + t.fbm(90, 90, 3) * 0.0004 + pits
    tone = np.clip(0.5 + t.fbm(6, 6, 5) * 0.45, 0, 1)
    bc = ramp(tone, ['#9c9282', '#b3a995', '#c6bda9'])
    bc *= (1 + t.fbm(120, 120, 3)[..., None] * 0.05)
    bc = mix3(bc, H('#8c8270'), veins * 0.35)
    # weathering: soot and rain streaks down the faces (v up), lighter washed patches
    streak = np.clip(t.fbm(40, 2, 3) * 0.5 + 0.5, 0, 1) ** 3 * sstep(0.2, 0.9, t.fbm(3, 3, 3) * 0.5 + 0.5)
    bc *= (1 - streak[..., None] * 0.3)
    bc *= (1 + pits[..., None] * 60)
    rough = 0.7 + t.fbm(20, 20, 3) * 0.06 + streak * 0.1
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=80.0, target='#b3a995')


# --------------------------------------------------------------------------- wrought / cast iron (metallic)
def metal(n):
    t = T.Tex(n, 7404)
    tile = 1.0
    rust = np.clip(t.fbm(8, 8, 5) * 1.8 - 0.35 + t.fbm(60, 60, 3) * 0.4, 0, 1)
    rust = sstep(0.15, 0.6, rust)
    pits = np.clip(t.fbm(160, 160, 3) - 0.3, 0, 1)
    h = rust * 0.0008 + pits * rust * 0.0006 + t.fbm(40, 40, 3) * 0.0002
    base = ramp(np.clip(0.5 + t.fbm(20, 20, 3) * 0.5, 0, 1), ['#33312e', '#45423e', '#55514b'])
    rc = ramp(np.clip(0.5 + t.fbm(50, 50, 3) * 0.7, 0, 1), ['#3a2216', '#5e3420', '#7e4a28'])
    bc = mix3(base, rc, rust)
    rough = 0.45 + rust * 0.45 + t.fbm(80, 80, 2) * 0.06
    metal_ = 1.0 - rust * 0.95
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=40.0, metal=metal_, target='#45423e')


# --------------------------------------------------------------------------- clean steel (modern lamps, signs, monument)
def steel(n):
    t = T.Tex(n, 7454)
    tile = 1.0
    brush = t.fbm(400, 3, 3) * 0.5 + t.fbm(900, 6, 2) * 0.3
    smudge = np.clip(t.fbm(5, 5, 5) * 1.4, -1, 1)
    h = brush * 0.00008 + t.fbm(30, 30, 3) * 0.0001
    bc = ramp(np.clip(0.55 + brush * 0.15 + smudge * 0.12, 0, 1), ['#7c7c7a', '#959593', '#a9a9a6'])
    grime = np.clip(t.fbm(6, 6, 4) * 1.6 - 0.5, 0, 1)
    bc = mix3(bc, H('#5c5a55'), grime * 0.35)
    rough = 0.32 + brush * 0.05 + grime * 0.25 + smudge * 0.05
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=20.0, metal=np.ones_like(h), target='#959593')


# --------------------------------------------------------------------------- canvas / sacking (tiles every 0.5 m)
def cloth(n):
    t = T.Tex(n, 7505)
    tile = 0.5
    th = 160   # threads per tile
    wu = np.sin(t.u * 2 * np.pi * th) * 0.5 + 0.5
    wv = np.sin(t.v * 2 * np.pi * th) * 0.5 + 0.5
    checker = (np.floor(t.u * th) + np.floor(t.v * th)) % 2
    weave = np.where(checker > 0, wu, wv)
    slub = t.fbm(8, 120, 3) * 0.5 + t.fbm(120, 8, 3, u=t.u + 0.2) * 0.5
    h = weave * 0.0004 + slub * 0.0002 + t.fbm(6, 6, 4) * 0.0015
    tone = np.clip(0.55 + slub * 0.35 + weave * 0.15 + t.fbm(5, 5, 4) * 0.3, 0, 1)
    bc = ramp(tone, ['#9a8a6c', '#b4a483', '#cbbd9c'])
    grime = np.clip(t.fbm(4, 4, 5) * 1.5 - 0.1, 0, 1)
    bc = mix3(bc, H('#6e604a'), grime * 0.3)
    rough = 0.92 + t.fbm(30, 30, 2) * 0.04
    return dict(bc=bc, h=h, rough=rough, tile=tile, ao_k=30.0, target='#b4a483')


# --------------------------------------------------------------------------- leaf atlas (hedges, vines, cabbages)
def leaf(n):
    """Upper 3/4 (v 0.25..1): one dense leaf cluster with a ragged alpha edge, used by foliage cards.
    Lower 1/4 (v 0..0.25): an opaque leaf mat that tiles in u, for the solid cores of hedges and crops."""
    rng = np.random.default_rng(7606)
    t = T.Tex(n, 7606)
    bc = np.zeros((n, n, 3), np.float32)
    al = np.zeros((n, n), np.float32)
    hg = np.zeros((n, n), np.float32)
    pal = ['#26331a', '#33421c', '#465a22', '#5d6e2a', '#7d8a3c']

    def leaf_at(cx, cy, L, W, ang, tone, layer, wrap):
        r = int(L * 0.6) + 2
        x0, x1 = int(cx) - r, int(cx) + r
        y0, y1 = max(0, int(cy) - r), min(n, int(cy) + r)
        if y1 <= y0:
            return
        xs = np.arange(x0, x1)
        ys = np.arange(y0, y1)
        X, Y = np.meshgrid(xs, ys)
        ca, sa = np.cos(ang), np.sin(ang)
        px = (X - cx) * ca + (Y - cy) * sa
        py = -(X - cx) * sa + (Y - cy) * ca
        s = px / L + 0.5
        wid = W * np.sqrt(np.clip(1 - (2 * s - 1) ** 2, 0, 1)) * (0.75 + 0.25 * (1 - s))
        m = (s > 0) & (s < 1) & (np.abs(py) < wid)
        if not m.any():
            return
        xi = X % n if wrap else X
        ok = m & (xi >= 0) & (xi < n)
        yi, xi = Y[ok], xi[ok]
        rib = 1 - np.abs(py[ok]) / np.maximum(wid[ok], 1e-3)
        c = ramp(np.clip(tone + (rib - 0.5) * 0.15 + (s[ok] - 0.5) * 0.1, 0, 1), pal)
        bc[yi, xi] = c
        al[yi, xi] = 1.0
        hg[yi, xi] = layer + rib * 0.4 + np.sin(s[ok] * np.pi) * 0.3

    # opaque mat, v in [0, 0.25 n): background first, then 2600 leaves wrapping in u
    strip = int(n * 0.25)
    bg = ramp(np.clip(0.2 + t.fbm(16, 4, 3)[:strip] * 0.4, 0, 1), pal)
    bc[:strip] = bg * 0.55
    al[:strip] = 1.0
    for i in range(2600):
        cx, cy = rng.uniform(0, n), rng.uniform(8, strip - 8)
        L = rng.uniform(16, 30)
        leaf_at(cx, cy, L, L * rng.uniform(0.3, 0.45), rng.uniform(0, 2 * np.pi),
                rng.uniform(0.15, 0.95), i / 2600.0, True)
    # the cluster card, v in [0.25, 1): a spray of leafy sprigs radiating from a dense centre, with gaps
    # between the sprigs so a card reads as foliage with a ragged edge, not as one blob
    cx0, cy0 = n * 0.5, strip + (n - strip) * 0.5
    R = (n - strip) * 0.47
    layer = 0
    nsp = 11
    for k in range(nsp):
        a0 = 2 * np.pi * (k + rng.uniform(-0.3, 0.3)) / nsp
        ln = R * rng.uniform(0.7, 1.0)
        bend = rng.normal(0, 0.35)
        nleaf = int(ln / 14)
        for j in range(nleaf):
            tt = (j + 0.5) / nleaf
            a = a0 + bend * tt * tt
            px, py = cx0 + np.cos(a) * ln * tt * 1.05, cy0 + np.sin(a) * ln * tt * 0.95
            for side in (-1, 1):
                ang = a + side * rng.uniform(0.5, 1.0)
                L = rng.uniform(24, 40) * (1.0 - 0.35 * tt)
                off = L * 0.45
                lx, ly = px + np.cos(ang) * off, py + np.sin(ang) * off
                tone = np.clip(rng.uniform(0.15, 0.85) + 0.25 * tt * (0.5 + 0.5 * np.sin(a)), 0, 1)
                leaf_at(lx, ly, L, L * rng.uniform(0.32, 0.45), ang, tone, layer / 2000.0, False)
                layer += 1
    for i in range(500):   # the dense, shaded heart of the spray
        a = rng.uniform(0, 2 * np.pi)
        rr = R * 0.42 * np.sqrt(rng.uniform(0, 1))
        L = rng.uniform(22, 36)
        leaf_at(cx0 + np.cos(a) * rr, cy0 + np.sin(a) * rr, L, L * 0.4, rng.uniform(0, 2 * np.pi),
                rng.uniform(0.05, 0.6), (layer + i) / 2000.0 * 0.5, False)
    # ambient darkening toward the cluster centre (inner leaves are shaded)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    rr = np.sqrt((xx - cx0) ** 2 + (yy - cy0) ** 2) / R
    shade = np.where(yy >= strip, 0.65 + 0.35 * np.clip(rr, 0, 1), 1.0)
    bc *= shade[..., None]
    col = dilate(bc, al)
    h = blur(hg, 1) * 0.0015
    rough = 0.62 + t.fbm(30, 30, 3) * 0.06
    return dict(bc=col, h=h, rough=rough, tile=0.6, ao_k=0.0, alpha=al, target=None)


# --------------------------------------------------------------------------- wheat atlas (crop rows, stooks)
def stalk(n):
    """Upper 70 % (v 0.3..1): side view of ripe wheat stalks with ears (card along a crop row).
    Lower 30 % (v 0..0.3): ears seen from above, scattered (horizontal canopy card). Alpha = coverage."""
    rng = np.random.default_rng(7707)
    t = T.Tex(n, 7707)
    bc = np.zeros((n, n, 3), np.float32)
    al = np.zeros((n, n), np.float32)
    hg = np.zeros((n, n), np.float32)
    pal = ['#6e5428', '#8c6c34', '#a88746', '#c4a25e', '#d8b874']
    split = int(n * 0.3)

    def disc(cx, cy, rx, ry, ang, tone, layer, ylo, yhi):
        r = int(max(rx, ry)) + 2
        y0, y1 = max(ylo, int(cy) - r), min(yhi, int(cy) + r)
        if y1 <= y0:
            return
        xs = np.arange(int(cx) - r, int(cx) + r)
        ys = np.arange(y0, y1)
        X, Y = np.meshgrid(xs, ys)
        ca, sa = np.cos(ang), np.sin(ang)
        px = ((X - cx) * ca + (Y - cy) * sa) / rx
        py = (-(X - cx) * sa + (Y - cy) * ca) / ry
        d2 = px * px + py * py
        m = d2 < 1
        xi = X % n
        yi = Y[m]
        xi = xi[m]
        c = ramp(np.clip(tone + (1 - d2[m]) * 0.2, 0, 1), pal)
        bc[yi, xi] = c
        al[yi, xi] = 1.0
        hg[yi, xi] = layer + np.sqrt(1 - d2[m])

    # side view: 150 stalks
    for i in range(150):
        x0 = rng.uniform(0, n)
        top = split + (n - split) * rng.uniform(0.72, 0.98)
        lean = rng.normal(0, 0.06)
        tone = rng.uniform(0.25, 0.85)
        ys = np.arange(split, int(top))
        s = (ys - split) / max(top - split, 1)
        xs = x0 + lean * (ys - split) * s
        w = 1.6 + 0.8 * (1 - s)
        for y, x, ww in zip(ys, xs, w):
            xa = np.arange(int(x - ww), int(x + ww) + 1) % n
            bc[y, xa] = ramp(np.array([tone * 0.8]), pal)[0] * (0.7 + 0.3 * (y - split) / (n - split))
            al[y, xa] = 1.0
            hg[y, xa] = i / 150.0
        # ear: two rows of grains along the last 60 px, drooping slightly
        ex, ey = xs[-1] if len(xs) else x0, top
        droop = rng.normal(0, 0.25)
        for k in range(12):
            gy = ey - k * 5.5
            gx = ex + droop * k * 1.5
            for sgn in (-1, 1):
                disc(gx + sgn * 3.2, gy, 4.2, 6.5, sgn * 0.35 + droop * 0.3, tone + 0.1, i / 150.0 + 0.5, split, n)
        # awns
        for a in range(5):
            ang = rng.normal(0, 0.25)
            for k in range(30):
                yy_ = int(ey + k)
                xx_ = int(ex + np.sin(ang) * k)
                if yy_ < n:
                    bc[yy_, xx_ % n] = ramp(np.array([tone + 0.2]), pal)[0]
                    al[yy_, xx_ % n] = 1.0
    # top view: ears scattered
    for i in range(700):
        cx, cy = rng.uniform(0, n), rng.uniform(10, split - 10)
        ang = rng.uniform(0, np.pi)
        tone = rng.uniform(0.3, 0.95)
        for k in range(-4, 5):
            gx, gy = cx + np.cos(ang) * k * 5, cy + np.sin(ang) * k * 5
            disc(gx, gy, 4.5, 3.4, ang, tone, i / 700.0, 0, split)
    col = dilate(bc, al)
    h = blur(hg, 1) * 0.0012
    rough = 0.75 + t.fbm(20, 20, 2) * 0.05
    return dict(bc=col, h=h, rough=rough, tile=1.0, ao_k=0.0, alpha=al, target=None)


def wood(n):
    return MT.oak(n)


def straw(n):
    return MT.thatch(n)


SETS = {
    'Rock': rock, 'Moss': moss, 'Stone': stone, 'Metal': metal, 'Steel': steel, 'Cloth': cloth,
    'Wood': wood, 'Straw': straw, 'Leaf': leaf, 'Stalk': stalk,
}


def build(name, outdir, n=N):
    r = SETS[name](n)
    base = os.path.join(outdir, 'T_Prop_%s' % name)
    bc = np.clip(r['bc'], 0, 1)
    T.save_png(base + '_D.png', bc, r.get('alpha'), srgb=True)
    nrm = T.normal_from_height(r['h'], r['tile'], n)
    T.save_png(base + '_N.png', nrm, None, srgb=False)
    ao = T.cavity_ao(r['h'], n, 0.01, r['tile'], r['ao_k']) if r['ao_k'] > 0 else np.ones_like(r['h'])
    met = r.get('metal', np.zeros_like(ao))
    orm = np.stack([ao, np.clip(r['rough'], 0.04, 1), met], -1)
    T.save_png(base + '_ORM.png', orm, None, srgb=False)
    sel = bc if 'alpha' not in r else bc[r['alpha'] > 0.5]
    info = {'tile_m': r['tile'], 'size': n, 'alpha': 'alpha' in r,
            'albedo': T.stats(name, sel, r.get('target'))}
    print('PROPTEX', name, info)
    return info
