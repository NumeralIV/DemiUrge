# DEMIURGE art / wonders-b - the five wonders, their damage states, culture ornaments and extras.
# Every builder returns (MB, sockets). Units metres, Z up, front faces -Y, pivot at the footprint centre.
import math
import random

import wb_defs as D
from wb_geo import (MB, SB, SpotSlot, CTX, S, TAU, revolve, wall_quad, flat_quad, box, tube, beam, blob, rubble,
                    scaffold, scaffold_ring, crane, xform)

DEG = math.pi / 180.0
FRONT = -90.0 * DEG


def sbf(asset):
    cache = {}

    def f(slot, name):
        key = (slot, name)
        if key not in cache:
            si = D.SLOTS.index(slot)
            bi = D.band_index(asset, slot, name)
            U, V = D.band_dims(asset, slot, name)
            if name in SPOTS:
                # light spot: every vertex samples the emissive spot (u ~ 0.015, v = 0.5 of the band)
                cache[key] = SB(SpotSlot(si), (bi, 0.5, 0.5), 1.0e6, V)
            else:
                cache[key] = SB(si, bi, U, V)
        return cache[key]
    return f


SPOTS = ('aviation_red', 'tether_core', 'mirror', 'beam_core', 'lamp', 'lantern')


def wrap(a):
    return (a + math.pi) % TAU - math.pi


def adist(a, b):
    return abs(wrap(a - b))


def in_arc(th, c, half):
    return adist(th, c) <= half


def hnoise(th, seed, oct=3):
    """Smooth periodic noise of angle, about -1..1."""
    rnd = random.Random(seed)
    s, amp, tot = 0.0, 1.0, 0.0
    for o in range(oct):
        f = 3 * (2 ** o)
        for k in range(2):
            ph = rnd.uniform(0, TAU)
            fk = f + k
            s += amp * math.sin(fk * th + ph) * 0.5
        tot += amp
        amp *= 0.5
    return s / tot


def polar(r, th, z=0.0):
    return (r * math.cos(th), r * math.sin(th), z)


def sockets_empty():
    return {'ornament': [], 'fx': [], 'trees': [], 'lights': []}


def lod_at_least(n):
    return CTX.lod >= n


# ============================================================================================ CHOIR
CHOIR_R = 75.0


def choir_chimneys():
    out = []
    for k in range(12):
        th = FRONT + (15.0 + 30.0 * k) * DEG
        h = 60.0 + 35.0 * (1.0 + math.sin(th)) * 0.5 + (3.0 if k % 2 else 0.0)
        out.append((k, th, round(h, 2)))
    return out


def choir(state):
    f = sbf('choir')
    brick, soot, ashlar, slate = f('Structure', 'brick'), f('Structure', 'brick_soot'), f('Structure', 'ashlar'), f('Structure', 'slate')
    glass, lglass, lead, iron_p = f('Roof', 'dome_glass'), f('Roof', 'lantern_glass'), f('Roof', 'lead'), f('Roof', 'ironplate')
    brass, iron, arch, setts = f('Detail', 'brass'), f('Detail', 'castiron'), f('Detail', 'arch_windows'), f('Detail', 'setts')
    mb = MB()
    so = sockets_empty()
    big = S(128, 8, 8)
    hull = CTX.lod >= 3

    # ---- plinth and plaza
    revolve(mb, [(90.0, -1.5, ashlar), (90.0, 0.5, ashlar), (88.5, 0.5, ashlar), (88.5, 1.2, setts), (46.0, 1.2, None)], big)
    if not hull:
        old = CTX.vmul
        CTX.vmul = max(old, 4.0)
        revolve(mb, [(46.5, 1.2, setts), (0.0, 1.2, None)], S(32, 8, 4))
        CTX.vmul = old

    # ---- drum (closed ring: outer up, top, inner down)
    if state == 'Intact':
        jag = None
    elif state == 'Construction':
        jag = lambda th: 10.6 + (0.0 if in_arc(th, FRONT, 100 * DEG) else -4.0) + 1.2 * hnoise(th, 3)
    elif state == 'Damaged':
        jag = lambda th: (8.0 + 3.0 * hnoise(th, 5)) if in_arc(th, 55 * DEG, 16 * DEG) else 99.0
    else:
        jag = lambda th: 6.5 + 5.0 * hnoise(th, 7) + 2.0 * hnoise(th * 3, 8)
    drum = [(50.8, 1.2, ashlar), (50.8, 2.0, ashlar), (50.0, 2.0, arch), (50.0, 10.0, ashlar), (50.6, 10.0, ashlar),
            (50.6, 10.6, ashlar), (50.0, 10.6, arch), (50.0, 18.6, ashlar), (51.4, 19.2, ashlar), (51.4, 20.0, lead),
            (47.6, 20.6, ashlar), (47.6, 21.8, ashlar), (46.6, 21.8, brick), (46.6, 1.2, None)]
    if hull:
        # verifier fix: closed top and inner wall (the open hull showed its inside between drum and dome)
        drum = [(50.8, 1.2, ashlar), (50.8, 20.0, lead), (47.6, 21.8, ashlar), (46.6, 21.8, brick), (46.6, 1.2, None)]
    revolve(mb, drum, big, jag=jag)

    # pilasters between the windows (80 windows around)
    if CTX.lod == 0 and state != 'Ruin':
        for k in range(80):
            th = TAU * k / 80.0
            if jag and jag(th) < 18.0:
                continue
            x, y, _ = polar(50.25, th)
            box(mb, brick, x, y, 2.0, 18.6, 0.9, 0.8, rot=th - math.pi / 2, top=ashlar)

    # ---- dome
    a, b, z0 = 47.2, 22.0, 21.8
    tmax = math.acos(6.5 / a)
    ztop = z0 + b * math.sin(tmax)
    nd = S(14, 4)
    dome_prof = []
    for i in range(nd + 1):
        t = tmax * i / nd
        dome_prof.append((a * math.cos(t), z0 + b * math.sin(t), glass if i < nd else None))
    dome_segs = S(144, 18, 18) if not hull else 16

    def dome_keep(th, r, z, i):
        if state == 'Damaged':
            if in_arc(th, 55 * DEG, 20 * DEG) and z > 24.0:
                return False
            if in_arc(th, 130 * DEG, 12 * DEG) and 28.0 < z < 38.0:
                return False
            if in_arc(th, -20 * DEG, 7 * DEG) and 33.0 < z:
                return False
        return True
    if state in ('Intact', 'Damaged'):
        revolve(mb, dome_prof, dome_segs, ucount=18 if not hull else None, keep=dome_keep)
        if state == 'Damaged' and not hull:
            # verifier fix: an inner skin so the far side of the dome is not culled when seen through the holes
            inner = [((a - 0.25) * math.cos(tmax * i / nd), z0 + (b - 0.25) * math.sin(tmax * i / nd), glass if i > 0 else None)
                     for i in range(nd, -1, -1)]
            inner = [(p_[0], p_[1], glass) for p_ in inner[:-1]] + [(inner[-1][0], inner[-1][1], None)]
            revolve(mb, inner, dome_segs, ucount=18)
        # lantern
        lan = [(6.5, ztop, iron), (6.5, ztop + 0.6, iron), (6.2, ztop + 0.6, lglass), (6.2, ztop + 4.6, iron),
               (7.0, ztop + 5.0, lead), (0.9, ztop + 8.5, brass), (0.5, ztop + 9.4, brass), (0.0, ztop + 12.0, None)]
        revolve(mb, lan, S(24, 8, 4) if not hull else 8, ucount=3 if not hull else None)
        so['fx'].append({'name': 'dome_glow', 'pos': [0, 0, round(ztop - 10, 2)], 'kind': 'point_light',
                         'note': 'warm interior light #ffc766, night only'})

    # ribs: meridians + rings (also the skeleton in construction / ruin)
    if not hull and CTX.lod <= 1:
        nrib = 18
        for k in range(nrib):
            th = TAU * k / nrib
            if state == 'Damaged' and in_arc(th, 55 * DEG, 14 * DEG):
                continue
            if state == 'Construction':
                t_end = tmax * (0.55 + 0.35 * (0.5 + 0.5 * math.sin(k * 1.7)))
            elif state == 'Ruin':
                if k % 6 != 1:
                    continue
                t_end = tmax * (0.25 + 0.2 * (k % 3))
            else:
                t_end = tmax
            n = S(16, 4)
            pts = []
            for i in range(n + 1):
                t = t_end * i / n
                rr, zz = (a + 0.3) * math.cos(t), z0 + (b + 0.3) * math.sin(t)
                pts.append(polar(rr, th, zz))
            tube(mb, iron, pts, 0.38, sides=4)
        if state in ('Intact', 'Damaged', 'Construction'):
            for tt in ((0.33, 0.62, 0.95, 1.25) if state != 'Construction' else (0.33, 0.62)):
                rr, zz = (a + 0.35) * math.cos(tt), z0 + (b + 0.35) * math.sin(tt)
                n = S(72, 8, 4)
                pts = [polar(rr, TAU * i / n, zz) for i in range(n + 1)]
                if state == 'Damaged':
                    cut = [i for i in range(n + 1) if in_arc(TAU * i / n, 55 * DEG, 18 * DEG)]
                    if cut:
                        i0, i1 = min(cut), max(cut)
                        pts = pts[i1:] + pts[1:i0 + 1]
                tube(mb, iron, pts, 0.3, sides=4)

    # ---- porticoes (4 entrances)
    for k, th in enumerate((FRONT, 0.0, math.pi / 2, math.pi)):
        x, y, _ = polar(55.5, th)
        rot = th - math.pi / 2
        if state == 'Ruin' or (state == 'Construction' and k > 1) or (state == 'Damaged' and k == 1):
            box(mb, arch, x, y, 1.2, 4.5 if state != 'Construction' else 9.2, 18.0, 9.0, rot=rot, top=setts)
            continue
        box(mb, arch, x, y, 1.2, 9.2, 18.0, 9.0, rot=rot, top=lead)
        box(mb, ashlar, x, y, 9.2, 10.4, 19.2, 10.0, rot=rot, top=ashlar)
        if not hull:
            box(mb, slate, x, y, 10.4, 13.6, 18.4, 9.6, rot=rot, taper=1.0, taper_d=0.08)

    # ---- chimneys
    chims = choir_chimneys()
    for k, th, h in chims:
        x, y, _ = polar(CHOIR_R, th)
        rot = th
        broken = None
        if state == 'Construction':
            broken = h * (0.3 + 0.25 * ((k * 7) % 5) / 4.0)
        elif state == 'Damaged' and k in (1, 4, 7, 10):
            broken = h * (0.38 + 0.08 * (k % 4))
        elif state == 'Ruin':
            broken = 55.0 if k == 6 else 7.0 + 21.0 * (((k * 5) % 7) / 6.0)
        # pedestal
        box(mb, ashlar, x, y, -0.3, 2.2, 10.2, 10.2, rot=rot, top=ashlar)
        ped_top = 14.0 if not (state == 'Ruin' and broken < 14) else max(4.0, broken)
        box(mb, brick, x, y, 2.2, ped_top, 9.0, 9.0, rot=rot, top=ashlar)
        if ped_top >= 14.0:
            box(mb, ashlar, x, y, 13.2, 14.4, 9.8, 9.8, rot=rot, top=ashlar)
        if state == 'Ruin' and broken < 14:
            rubble(mb, brick, x, y, 9.0, S(10, 2), 100 + k, size=(0.8, 2.4), z=0.0)
            continue
        zs = 14.4
        hs = h if broken is None else broken
        sootz = 0.72 * h
        prof = [(3.3, zs, brick)]
        if sootz < h - 6:
            prof.append((3.3 + (2.6 - 3.3) * (sootz - zs) / (h - 6 - zs), sootz, soot))
        prof += [(2.6, h - 6.0, soot), (3.2, h - 5.0, soot), (3.2, h - 2.2, soot), (2.9, h - 1.6, soot),
                 (2.9, h, iron_p), (2.1, h, soot), (2.1, h - 5.0, soot), (0.0, h - 5.0, None)]   # verifier fix: sooted flue floor (was see-through)
        if hull:
            prof = [(3.3, zs, brick), (2.6, h, iron_p), (0.0, h, None)]
        cj = None
        if broken is not None:
            ph = k * 1.3
            cj = lambda t, hs=hs, ph=ph: hs + 1.6 * math.sin(3 * t + ph) + 0.8 * math.sin(7 * t + ph * 2)
            prof = [(3.3, zs, brick), (2.6, h, iron_p), (2.0, h, iron_p), (2.0, zs + 1.0, None)]
        revolve(mb, prof, S(16, 6, 2) if not hull else 4, jag=cj, xf=xform(0, (x, y, 0)))
        if broken is None:
            so['ornament'].append({'pos': [round(x, 3), round(y, 3), round(h, 3)], 'rot_z_deg': round(th / DEG + 90, 2), 'slot': 'chimney_cap'})
            so['fx'].append({'name': 'steam_%02d' % k, 'pos': [round(x, 3), round(y, 3), round(h + 3.2, 3)], 'kind': 'steam_plume',
                             'note': 'white #f0ece6; plumes lean together downwind into one cloud 300-600 m; lit orange from below at night'})
        else:
            if state != 'Construction':
                rubble(mb, brick, x + 7 * math.cos(th + 0.6), y + 7 * math.sin(th + 0.6), 8.0, S(8, 2), 200 + k, size=(0.8, 2.2))
            else:
                if k % 3 == 0 and not hull and CTX.lod <= 1:
                    scaffold(mb, iron, x, y, 14.4, broken + 3.0, 8.4, 8.4, rot=rot, bay=4.2, lift=4.0, r=0.14)
        # boiler duct to the drum
        if not hull and CTX.lod <= 1 and not (state == 'Ruin') and abs(wrap(th - FRONT)) > 20 * DEG:
            p0 = polar(CHOIR_R - 4.6, th, 7.0)
            p1 = polar(51.2, th, 7.0)
            tube(mb, iron, [p0, p1], 1.1, sides=S(8, 4))

    # ---- arcade ring between the chimneys (gap at the front entrance)
    a0, a1 = FRONT + 15 * DEG, FRONT + 345 * DEG
    if state == 'Intact':
        ajag = None
    elif state == 'Construction':
        ajag = lambda th: 10.2 if wrap(th - FRONT) < 0 else 3.0
    elif state == 'Damaged':
        ajag = lambda th: (3.5 + 2 * hnoise(th, 11)) if in_arc(th, 150 * DEG, 12 * DEG) else 99.0
    else:
        ajag = lambda th: 2.5 + 2.5 * hnoise(th, 13)
    if hull:
        # verifier fix: inner wall added; the open hull showed the inside of the far arcade as a band behind the dome
        revolve(mb, [(77.0, 1.2, ashlar), (77.0, 10.2, lead), (73.0, 10.2, ashlar), (73.0, 1.2, None)], 24, a0=a0, a1=a1)
    else:
        arc = [(77.0, 1.2, arch), (77.0, 9.2, ashlar), (77.4, 9.6, ashlar), (77.4, 10.2, lead), (72.6, 10.2, ashlar),
               (72.6, 9.6, ashlar), (73.0, 9.2, arch), (73.0, 1.2, None)]
        revolve(mb, arc, S(120, 12, 4), a0=a0, a1=a1, jag=ajag, caps=True)
        if CTX.lod == 0 and state == 'Intact':
            # iron railing on the walkway
            n = 96
            pts = [polar(76.6, a0 + (a1 - a0) * i / n, 11.2) for i in range(n + 1)]
            tube(mb, iron, pts, 0.07, sides=3)
            # ornamental iron arch over the entrance
            c0 = polar(CHOIR_R, FRONT - 15 * DEG, 14.4)
            c1 = polar(CHOIR_R, FRONT + 15 * DEG, 14.4)
            mid = ((c0[0] + c1[0]) / 2, (c0[1] + c1[1]) / 2)
            half = math.dist(c0[:2], c1[:2]) / 2
            pts = []
            for i in range(17):
                t = math.pi * i / 16
                pts.append((mid[0] + (c0[0] - mid[0]) * math.cos(t), mid[1] + (c0[1] - mid[1]) * math.cos(t), 14.4 + half * 0.55 * math.sin(t)))
            tube(mb, iron, pts, 0.45, sides=4)
            tube(mb, brass, [(mid[0], mid[1], 14.4 + half * 0.55), (mid[0], mid[1], 14.4 + half * 0.55 + 4.0)], 0.35, sides=4, r_end=0.05)

    # ---- construction / damage dressing
    if state == 'Construction' and not hull:
        if CTX.lod <= 1:
            scaffold_ring(mb, iron, 0, 0, 52.6, 1.2, 12.0, n=S(40, 8))
        crane(mb, iron, -18.0, 12.0, 58.0, 42.0, rot=0.6)
        crane(mb, iron, 24.0, -20.0, 52.0, 38.0, rot=2.4)
        for k in range(S(8, 2)):
            th = TAU * k / 8 + 0.3
            rubble(mb, brick, *polar(64.0, th)[:2], 3.5, 4, 300 + k, size=(0.8, 1.6))
    if state == 'Damaged' and not hull:
        rubble(mb, brick, *polar(58.0, 55 * DEG)[:2], 9.0, S(16, 4), 400, size=(1.0, 2.6))
        rubble(mb, iron_p, *polar(30.0, 55 * DEG)[:2], 10.0, S(10, 2), 401, size=(0.8, 1.8), z=1.2)
    if state == 'Ruin' and not hull:
        for k in range(S(14, 3)):
            th = TAU * k / 14 + 0.2
            rubble(mb, brick, *polar(46.0 + 12 * (k % 3), th)[:2], 7.0, S(9, 2), 500 + k, size=(1.0, 2.8), z=1.0)
    so['fx'].append({'name': 'fire_marker', 'pos': [0, 0, 30], 'kind': 'night_marker', 'note': 'dome glows #ffc766 at night; steam lit orange from below'})
    return mb, so


def choir_ornament(culture):
    f = sbf('choir')
    brass, iron, ashlar, slate = f('Detail', 'brass'), f('Detail', 'castiron'), f('Structure', 'ashlar'), f('Structure', 'slate')
    mb = MB()
    seg = S(20, 6, 2)
    if culture == 'A':      # polished brass bell crown
        revolve(mb, [(3.0, -0.4, brass), (3.4, 0.5, brass), (3.0, 1.5, brass), (3.7, 2.6, brass), (3.5, 3.1, iron),
                     (2.5, 3.1, iron), (2.5, 2.0, None)], seg)
        for k in range(8):
            th = TAU * k / 8
            tube(mb, brass, [polar(3.55, th, 2.9), polar(3.9, th, 4.4)], 0.18, sides=4, r_end=0.02)
    elif culture == 'B':    # ribbed copper crown with fins (verdigris under culture B tint)
        revolve(mb, [(3.0, -0.4, brass), (3.3, 0.8, brass), (3.3, 1.4, brass), (2.6, 1.4, iron), (2.6, 0.6, None)], seg)
        for k in range(12):
            th = TAU * k / 12
            x, y, _ = polar(3.1, th)
            box(mb, brass, x, y, 1.0, 4.2, 0.25, 0.9, rot=th, taper=0.4, taper_d=0.1)
    else:                   # stepped stone cornice with a slate cone on four legs (Ring-Builder)
        revolve(mb, [(3.2, -0.4, ashlar), (3.2, 0.5, ashlar), (3.6, 0.5, ashlar), (3.6, 1.1, ashlar), (2.4, 1.1, iron), (2.4, 0.4, None)], seg)
        for k in range(4):
            th = TAU * k / 4 + math.pi / 4
            x, y, _ = polar(2.6, th)
            box(mb, ashlar, x, y, 1.1, 2.8, 0.7, 0.7, rot=th)
        revolve(mb, [(3.5, 2.8, slate), (0.15, 6.6, slate), (0.0, 6.7, None)], seg)
    return mb


# ============================================================================================ EAR
EAR_PYL = [90 * DEG, 210 * DEG, 330 * DEG]
EAR_PR = 133.0


def ear_dish_z(r):
    return 2.5 + 38.1 * (r / 123.8) ** 2


def ear(state):
    f = sbf('ear')
    conc, concw, concs, asph = f('Structure', 'concrete'), f('Structure', 'concrete_weathered'), f('Structure', 'concrete_smooth'), f('Structure', 'asphalt')
    pan, pand, rib, grate = f('Roof', 'dish_panels'), f('Roof', 'dish_panels_dark'), f('Roof', 'dish_rib'), f('Roof', 'grate')
    steel, red, grass, office = f('Detail', 'steel'), f('Detail', 'aviation_red'), f('Detail', 'grass_berm'), f('Detail', 'office_windows')
    mb = MB()
    so = sockets_empty()
    hull = CTX.lod >= 3
    big = S(160, 8, 8) if not hull else 24

    # ---- berm
    n = S(9, 3)
    berm = []
    for i in range(n + 1):
        s = i / n
        r = 178.0 + (123.0 - 178.0) * s
        z = -0.5 + 14.5 * (1 - math.cos(math.pi * s)) / 2
        berm.append((r, z, grass if i < n else None))
    revolve(mb, berm, big)
    # ---- wall + lip + dish (one closed profile)
    if state == 'Intact':
        wj = None
    elif state == 'Construction':
        wj = lambda th: 41.2 if wrap(th - FRONT) < 0.9 and wrap(th - FRONT) > -1.6 else 24.0 + 2 * hnoise(th, 21)
    elif state == 'Damaged':
        wj = lambda th: (26.0 + 3 * hnoise(th, 23)) if in_arc(th, 18 * DEG, 8 * DEG) else 99.0
    else:
        wj = lambda th: 30.0 + 7.0 * hnoise(th, 25) + 2.0 * hnoise(th * 2, 26)
    wall = [(122.6, 13.5, concw), (124.6, 21.0, concw), (126.1, 30.0, concw), (126.9, 40.0, concs), (127.0, 41.2, concs),
            (124.2, 41.2, None)]
    revolve(mb, wall, big, jag=wj)
    if state != 'Intact' and not hull:
        # verifier fix: the rim wall had no inner face (the dish closed it). Where dish panels are missing
        # (Construction, Damaged holes, Ruin) the far wall was seen from behind and culled, so the rim vanished.
        # An inner wall down to a concrete floor at +1 m now closes the bowl.
        revolve(mb, [(124.2, 41.2, concw), (122.4, 13.5, conc), (122.4, 1.0, None)], big, jag=wj)
        old = CTX.vmul
        CTX.vmul = max(old, 8.0)
        revolve(mb, [(122.4, 1.0, conc), (0.0, 1.0, None)], S(64, 16, 8))
        CTX.vmul = old
    nd = S(16, 4)
    dish = []
    for i in range(nd + 1):
        r = 123.8 + (9.0 - 123.8) * i / nd
        dish.append((r, ear_dish_z(r), pan if i < nd else grate))
    dish = [(124.2, 41.2, pan)] + dish + [(9.0, 1.0, grate), (0.0, 1.0, None)]

    def dish_sb(i, th, sb):
        if sb is pan and (i // 2) % 2 == 1:
            return pand
        return sb

    def dish_keep(th, r, z, i):
        if r < 10:
            return True
        if state == 'Construction':
            return r < 64.0 or (in_arc(th, FRONT, 50 * DEG) and r < 100.0)
        if state == 'Damaged':
            return not (in_arc(th, 330 * DEG, 18 * DEG) and 28.0 < r < 82.0) and not (in_arc(th, 18 * DEG, 9 * DEG) and r > 110)
        if state == 'Ruin':
            n = hnoise(th, 27) + 0.6 * hnoise(th * 3.0 + r * 0.05, 28) + (0.35 if r < 45 else 0.0)
            return n > 0.15 and r < 118
        return True
    revolve(mb, dish, big, keep=dish_keep, sbfn=dish_sb)
    # radial ribs under the panels (visible where panels are missing)
    if state != 'Intact' and not hull:
        for k in range(48 if CTX.lod == 0 else 24):
            th = TAU * k / (48 if CTX.lod == 0 else 24)
            if state == 'Ruin' and k % 3 == 0:
                continue
            if state == 'Damaged' and not in_arc(th, 330 * DEG, 30 * DEG) and not in_arc(th, 18 * DEG, 14 * DEG):
                continue
            pts = [polar(r, th, ear_dish_z(r) - 0.7) for r in [12 + i * 11.2 for i in range(11)]]
            tube(mb, rib, pts, 0.45, sides=4)
        for rr in (40.0, 70.0, 100.0):
            n2 = S(96, 8)
            pts = [polar(rr, TAU * i / n2, ear_dish_z(rr) - 1.0) for i in range(n2 + 1)]
            tube(mb, rib, pts, 0.35, sides=4)

    # ---- pylons
    tops = []
    for k, th in enumerate(EAR_PYL):
        x, y, _ = polar(EAR_PR, th)
        H = 110.6
        pj = None
        if state == 'Construction':
            pj_h = (58.0, 84.0, 46.0)[k]
            pj = lambda t, hh=pj_h: hh
        elif state == 'Damaged' and k == 2:
            pj = lambda t: 62.0 + 1.5 * math.sin(5 * t)
        elif state == 'Ruin':
            hh = (44.0, 21.0, 30.0)[k]
            pj = lambda t, hh=hh: hh + 2.0 * math.sin(4 * t + hh)
        prof = [(7.2, -0.3, conc), (7.2, 14.0, concs), (3.7, 16.0, concs), (2.25, 104.0, concs), (2.85, 105.8, concs),
                (2.85, 108.6, concs), (1.25, 109.6, red), (0.95, 110.3, red), (0.0, 110.6, None)]
        if pj:
            prof = [(7.2, -0.3, conc), (7.2, 14.0, concs), (3.7, 16.0, concs), (2.25, 104.0, concs), (1.6, 104.0, conc), (1.6, 16.0, None)]
        if hull:
            prof = [(5.0, -0.3, concs), (2.2, 108.0, red), (0.0, 110.6, None)]
        revolve(mb, prof, S(20, 6, 2) if not hull else 4, jag=pj, xf=xform(0, (x, y, 0)))
        if pj is None:
            tops.append((x, y, 107.5))
            so['lights'].append({'name': 'aviation_%d' % k, 'pos': [round(x, 3), round(y, 3), 110.4], 'color': '#ff3a2a',
                                 'blink': '1 s on, 1 s off', 'phase_s': round(k * 0.667, 3)})
            so['ornament'].append({'pos': [round(x, 3), round(y, 3), 103.6], 'rot_z_deg': round(th / DEG, 2), 'slot': 'pylon_collar'})
        else:
            tops.append(None)
            if state == 'Construction' and not hull and CTX.lod <= 1:
                scaffold(mb, steel, x, y, 14.0, pj(0) + 4.0, 7.0, 7.0, rot=th, bay=3.5, lift=6.0, r=0.16)
            if state == 'Damaged' and k == 2 and not hull:
                # the fallen top section lying across the berm
                fall_dir = th + 0.25
                p0 = (x + 6 * math.cos(fall_dir), y + 6 * math.sin(fall_dir), 6.0)
                p1 = (x + 52 * math.cos(fall_dir), y + 52 * math.sin(fall_dir), 1.2)
                tube(mb, concs, [p0, p1], 2.2, sides=S(12, 6), caps=True)
                rubble(mb, conc, x, y, 10.0, S(10, 3), 700, size=(1.0, 2.5), z=8.0)
            if state == 'Ruin' and not hull:
                rubble(mb, conc, x, y, 12.0, S(10, 3), 710 + k, size=(1.0, 2.8), z=6.0)

    # ---- receiver triangle
    rz = 84.0
    Rv = 40.0 / math.sqrt(3.0)
    verts = [polar(Rv, th, rz) for th in EAR_PYL]
    rx = None
    if state == 'Damaged':
        # hangs from pylons 0 and 1; vertex 2 rests on the dish
        rx = ('tilt', 0, 1)
    elif state == 'Ruin':
        rx = ('ground',)

    def place(p):
        if rx is None:
            return p
        if rx[0] == 'ground':
            c, s = math.cos(0.7), math.sin(0.7)
            x, y = p[0] * c - p[1] * s + 18.0, p[0] * s + p[1] * c - 10.0
            return (x, y, p[2] - rz + ear_dish_z(math.hypot(x, y)) + 3.0)
        # tilt about the axis through vertices 0 and 1
        a, b = verts[0], verts[1]
        ax = [b[i] - a[i] for i in range(3)]
        la = math.sqrt(sum(t * t for t in ax))
        ax = [t / la for t in ax]
        v = [p[i] - a[i] for i in range(3)]
        ang = -0.62
        c, s = math.cos(ang), math.sin(ang)
        dot = sum(ax[i] * v[i] for i in range(3))
        cr = [ax[1] * v[2] - ax[2] * v[1], ax[2] * v[0] - ax[0] * v[2], ax[0] * v[1] - ax[1] * v[0]]
        r = [v[i] * c + cr[i] * s + ax[i] * dot * (1 - c) for i in range(3)]
        return (a[0] + r[0], a[1] + r[1], a[2] + r[2])

    if state != 'Construction':
        sub = MB()
        for e in range(3):
            if state == 'Ruin' and e == 2:
                continue
            A, B = verts[e], verts[(e + 1) % 3]
            dx, dy = B[0] - A[0], B[1] - A[1]
            L = math.hypot(dx, dy)
            nx, ny = -dy / L * 1.6, dx / L * 1.6
            chords = [(nx, ny, 2.0), (-nx, -ny, 2.0), (nx, ny, -2.0), (-nx, -ny, -2.0)]
            for cx_, cy_, cz in chords:
                tube(sub, steel, [(A[0] + cx_, A[1] + cy_, A[2] + cz), (B[0] + cx_, B[1] + cy_, B[2] + cz)], 0.32, sides=4)
            if not hull and CTX.lod <= 1:
                nb = 10 if CTX.lod == 0 else 5
                for i in range(nb):
                    t0, t1 = i / nb, (i + 1) / nb
                    P0 = (A[0] + dx * t0, A[1] + dy * t0, A[2])
                    P1 = (A[0] + dx * t1, A[1] + dy * t1, A[2])
                    for sgn in (1, -1):
                        tube(sub, steel, [(P0[0] + sgn * nx, P0[1] + sgn * ny, P0[2] - 2.0), (P1[0] + sgn * nx, P1[1] + sgn * ny, P1[2] + 2.0)], 0.16, sides=3)
                    tube(sub, steel, [(P0[0] + nx, P0[1] + ny, P0[2] + 2.0), (P1[0] - nx, P1[1] - ny, P1[2] + 2.0)], 0.14, sides=3)
        # feed cabin with a white radome under the centre
        if not hull:
            revolve(sub, [(0.0, rz - 11.0, concs), (4.5, rz - 9.5, concs), (6.0, rz - 6.5, steel), (5.5, rz - 2.0, None)], S(20, 6))
            revolve(sub, [(5.5, rz - 2.0, steel), (5.5, rz + 2.6, steel), (0.0, rz + 2.6, None)], S(20, 6))
            for e in range(3):
                A, B = verts[e], verts[(e + 1) % 3]
                mid = ((A[0] + B[0]) / 2, (A[1] + B[1]) / 2, rz + 2.0)
                tube(sub, steel, [(0, 0, rz + 2.4), mid], 0.3, sides=4)
        mb.extend(sub, place)
    # ---- cables
    if not hull and state != 'Construction':
        for k in range(3):
            if tops[k] is None:
                continue
            T = tops[k]
            V_ = place(verts[k])
            for off in (-0.7, 0.0, 0.7):
                n = S(12, 3)
                pts = []
                for i in range(n + 1):
                    t = i / n
                    sag = 4.0 * t * (1 - t) * 4.0
                    pts.append((T[0] + (V_[0] - T[0]) * t + off * math.sin(EAR_PYL[k]), T[1] + (V_[1] - T[1]) * t - off * math.cos(EAR_PYL[k]),
                                T[2] + (V_[2] + 1.5 - T[2]) * t - sag))
                tube(mb, steel, pts, 0.32, sides=4)
            # backstays to ground anchors outside the berm
            for d_ in (-14 * DEG, 14 * DEG):
                G = polar(186.0, EAR_PYL[k] + d_, 0.4)
                tube(mb, steel, [T, G], 0.22, sides=3)
                if CTX.lod == 0:
                    box(mb, conc, G[0], G[1], -0.3, 1.4, 4.0, 4.0, rot=EAR_PYL[k] + d_)
            # tie-down lines from the receiver corners to the rim
            if CTX.lod <= 1:
                R_ = polar(125.0, EAR_PYL[k] + 60 * DEG, 41.6)
                tube(mb, steel, [place(verts[k]), R_], 0.14, sides=3)

    # ---- control building, lift tower and bridge at the front
    by = -194.0
    if state != 'Ruin':
        box(mb, office, 0.0, by, -0.3, 8.0, 52.0, 22.0, top=concs)
        if not hull:
            box(mb, concs, 0.0, by, 8.0, 9.0, 53.0, 23.0, top=concs)
            box(mb, office, 12.0, by + 2.0, 9.0, 17.0, 16.0, 12.0, top=concs)
    else:
        box(mb, office, 0.0, by, -0.3, 4.0, 52.0, 22.0, top=concw)
        rubble(mb, conc, 0, by, 16.0, S(12, 3), 800, size=(1.0, 2.6), z=3.0)
    if not hull:
        tz = 44.0 if state in ('Intact', 'Damaged') else 22.0
        box(mb, concs, 0.0, -132.0, -0.3, tz, 7.0, 7.0, top=concs)
        if state in ('Intact', 'Damaged'):
            beam(mb, conc, (0.0, -135.5, 39.5), (0.0, -125.5, 40.2), 3.2)
        beam(mb, conc, (0.0, -183.0, 3.0), (0.0, -135.5, 3.0), 3.4)
        # forecourt
        flat_quad(mb, asph, (-12.0, -226.0, 0.05), (12.0, -226.0, 0.05), (12.0, -205.0, 0.05), (-12.0, -205.0, 0.05))
    if state == 'Construction' and not hull:
        crane(mb, steel, 40.0, -60.0, 70.0, 50.0, rot=2.0)
        crane(mb, steel, -150.0, 70.0, 60.0, 40.0, rot=-0.4)
        # truss sections waiting on the ground
        for i in range(3):
            A = (-60.0 + i * 8, -168.0, 1.0)
            B = (-60.0 + i * 8, -168.0 + 38.0, 1.0)
            tube(mb, steel, [A, B], 0.9, sides=4)
            rubble(mb, conc, 60, -160, 12.0, 6, 900, size=(0.8, 1.6), z=1.0)
        if CTX.lod <= 1:
            scaffold_ring(mb, steel, 0, 0, 128.5, 13.0, 30.0, n=S(36, 8), a0=10 * DEG, a1=150 * DEG)
    if state == 'Damaged' and not hull:
        rubble(mb, pan, *polar(55.0, 330 * DEG)[:2], 16.0, S(14, 3), 910, size=(1.0, 2.8), z=ear_dish_z(55) - 0.5)
        rubble(mb, concw, *polar(130.0, 18 * DEG)[:2], 14.0, S(14, 3), 911, size=(1.0, 2.8), z=14.0)
    if state == 'Ruin' and not hull:
        for k in range(S(10, 2)):
            th = TAU * k / 10 + 0.4
            rubble(mb, pan, *polar(30 + 9 * (k % 4), th)[:2], 9.0, 6, 920 + k, size=(1.0, 2.4), z=ear_dish_z(30 + 9 * (k % 4)))
        for k in range(S(8, 2)):
            th = TAU * k / 8 + 0.1
            rubble(mb, concw, *polar(132.0, th)[:2], 10.0, 6, 940 + k, size=(1.2, 3.0), z=12.0)
    for k in range(S(28, 4)):
        th = TAU * k / 28
        if in_arc(th, FRONT, 12 * DEG):
            continue
        so['trees'].append({'pos': [round(v, 2) for v in polar(150.0 + 10 * (k % 2), th, 8.0 if k % 2 == 0 else 5.0)], 'kind': 'broadleaf'})
    return mb, so


def ear_ornament(culture):
    f = sbf('ear')
    concs, steel, conc = f('Structure', 'concrete_smooth'), f('Detail', 'steel'), f('Structure', 'concrete')
    mb = MB()
    seg = S(20, 6, 2)
    if culture == 'A':      # fluted concrete collar
        revolve(mb, [(2.4, 0.0, concs), (3.4, 1.0, concs), (3.4, 3.6, concs), (2.9, 4.4, concs), (2.3, 4.4, None)], seg)
        for k in range(12):
            th = TAU * k / 12
            x, y, _ = polar(3.45, th)
            box(mb, concs, x, y, 0.6, 3.8, 0.35, 0.4, rot=th)
    elif culture == 'B':    # steel halo ring on struts
        n = S(32, 8)
        tube(mb, steel, [polar(5.2, TAU * i / n, 2.6) for i in range(n + 1)], 0.32, sides=6)
        for k in range(6):
            th = TAU * k / 6
            tube(mb, steel, [polar(2.4, th, 1.0), polar(5.2, th, 2.6)], 0.16, sides=4)
    else:                   # stepped slate-grey collar
        revolve(mb, [(2.4, 0.0, conc), (4.0, 0.0, conc), (4.0, 1.2, conc), (3.4, 1.2, conc), (3.4, 2.6, conc),
                     (2.8, 2.6, conc), (2.8, 4.0, conc), (2.3, 4.0, None)], seg)
    return mb


# ============================================================================================ LADDER

def ladder(state):
    f = sbf('ladder')
    comp, glaz, comps, pav = f('Structure', 'composite'), f('Structure', 'glazing'), f('Structure', 'composite_smooth'), f('Structure', 'plaza_paving')
    gsol, solar, garden, pool = f('Roof', 'garden_solar'), f('Roof', 'solar'), f('Roof', 'garden'), f('Roof', 'reflecting_pool')
    gold, core, slit, climb = f('Detail', 'gold'), f('Detail', 'tether_core'), f('Detail', 'light_slit'), f('Detail', 'climber')
    mb = MB()
    so = sockets_empty()
    hull = CTX.lod >= 3
    big = S(256, 16, 16) if not hull else 32

    if state == 'Intact':
        rj = None
    elif state == 'Construction':
        rj = lambda th: 26.5 if in_arc(th, FRONT, 125 * DEG) else 7.2 + 0.5 * hnoise(th, 31)
    elif state == 'Damaged':
        rj = lambda th: (7.0 + 4.0 * hnoise(th, 33)) if in_arc(th, 35 * DEG, 17 * DEG) else 99.0
    else:
        rj = lambda th: 3.0 + max(0.0, 11.0 * hnoise(th, 35)) + 2.0 * hnoise(th * 2, 36)
    ring = [(200.6, -0.3, comps), (200.6, 1.2, comp), (198.0, 1.2, glaz), (198.0, 7.2, glaz), (198.0, 13.2, glaz),
            (198.0, 19.2, gold), (199.0, 19.2, comps), (201.2, 20.2, comps), (202.2, 22.4, comps), (201.8, 24.6, comps),
            (200.0, 25.6, comps), (197.0, 26.0, gold), (195.8, 25.9, gsol), (149.0, 23.0, comps), (147.5, 23.6, gold),
            (146.2, 23.0, glaz), (146.2, 17.0, glaz), (146.2, 11.0, garden), (137.0, 10.6, comps), (136.2, 11.2, comps),
            (135.4, 10.6, glaz), (135.4, 4.6, comps), (135.4, -0.3, None)]
    if hull:
        ring = [(200.6, -0.3, comps), (202.0, 22.0, comps), (197.0, 26.0, gsol), (149.0, 23.0, comps), (135.4, 10.6, comps), (135.4, -0.3, None)]
    rkeep = None
    if state == 'Ruin':
        def rkeep(th, r, z, i):
            if z < 1.5:
                return True
            if (150.0 < r < 195.0 or 137.5 < r < 145.5) and hnoise(th * 3.0, 38) < 0.3:
                return False      # roof gardens fallen in: the ruin shows its broken shell walls
            return hnoise(th * 1.7, 37) > -0.45
    revolve(mb, ring, big, jag=rj, keep=rkeep)
    if state == 'Ruin' and not hull:
        # verifier fix: with the roof gardens fallen in, the single-sided shell walls were seen from behind and
        # culled, so the far walls vanished from the game camera. Back skins for the three walls.
        for prof_ in ([(197.4, 26.0, comps), (197.4, -0.3, None)], [(146.8, -0.3, comps), (146.8, 23.0, None)],
                      [(136.0, -0.3, comps), (136.0, 10.6, None)]):
            revolve(mb, prof_, big, jag=rj, keep=rkeep)
        # and lids on the broken wall tops (clamped to the same jagged height), so the cavities do not show
        for r_out, r_in, hmax in ((202.4, 197.3, 26.0), (146.9, 146.1, 23.6), (136.1, 135.3, 10.6)):
            revolve(mb, [(r_out, 40.0, comps), (r_in, 40.0, None)], big, jag=lambda th, h=hmax: min(rj(th), h), keep=rkeep)
    # facade fins
    if CTX.lod == 0:
        for k in range(128):
            th = TAU * (k + 0.5) / 128
            top = 19.2
            if rj and rj(th) < 19.2:
                continue
            x, y, _ = polar(198.7, th)
            box(mb, comps, x, y, 1.2, top, 0.5, 1.4, rot=th - math.pi / 2)
    # roof trees (sockets)
    if state == 'Intact':
        for k in range(64):
            th = TAU * (k + 0.25) / 64
            so['trees'].append({'pos': [round(v, 2) for v in polar(172.0 + 9.0 * (k % 3 - 1), th, 25.0 - 1.4 * (k % 3))], 'kind': 'garden_small'})
    # ---- plaza and anchor
    plaza = [(62.0, -0.3, pav), (62.0, 0.4, pav), (52.0, 0.4, pav), (52.0, 1.0, pav), (42.0, 1.0, pav), (42.0, 1.6, pav),
             (32.0, 1.6, pav), (32.0, 2.2, pav), (22.0, 2.2, pav), (22.0, 2.8, pav), (14.0, 2.8, slit), (12.8, 2.8, comps),
             (12.8, 8.0, gold), (12.8, 9.0, comps), (9.0, 10.5, comps), (4.0, 22.0, gold), (3.2, 22.6, comps), (1.4, 26.0, comps),
             (1.05, 26.0, None)]
    if hull:
        plaza = [(62.0, -0.3, pav), (62.0, 2.8, comps), (12.8, 9.0, comps), (1.4, 26.0, None)]
    aj = None
    if state == 'Construction':
        aj = lambda th: 15.0
    elif state == 'Damaged':
        aj = lambda th: 18.0 + 2.0 * math.sin(3 * th)
    elif state == 'Ruin':
        aj = lambda th: 8.5 + 1.5 * math.sin(4 * th)
    revolve(mb, plaza, S(96, 16, 8) if not hull else 16, jag=aj)
    so['ornament'].append({'pos': [0.0, 0.0, 22.6], 'rot_z_deg': 0.0, 'slot': 'anchor_crown'} if state == 'Intact' else None)
    if so['ornament'][-1] is None:
        so['ornament'].pop()
    # radial walkways to the ring
    if not hull:
        for k in range(4):
            th = 45 * DEG + k * 90 * DEG
            c0, c1 = 62.0, 135.4
            p = lambda r, s: (r * math.cos(th) - s * math.sin(th), r * math.sin(th) + s * math.cos(th), 0.25)
            flat_quad(mb, pav, p(c0, -5.0), p(c1, -5.0), p(c1, 5.0), p(c0, 5.0))
    # ---- construction / damage dressing
    if state == 'Construction' and not hull:
        for k, th in enumerate((40 * DEG, 160 * DEG, 280 * DEG)):
            crane(mb, gold, *polar(170.0, th)[:2], 62.0, 46.0, rot=th + 2.6)
        if CTX.lod <= 1:
            scaffold_ring(mb, gold, 0, 0, 203.5, 0.0, 9.0, n=S(48, 8), a0=60 * DEG, a1=240 * DEG)
            scaffold_ring(mb, gold, 0, 0, 10.5, 2.8, 24.0, n=12)
    if state in ('Damaged', 'Ruin') and not hull:
        secs = [35 * DEG] if state == 'Damaged' else [TAU * k / 7 for k in range(7)]
        for i, th in enumerate(secs):
            rubble(mb, comp, *polar(178.0, th)[:2], 22.0, S(14, 3), 1000 + i, size=(1.2, 3.4), z=0.0)
            rubble(mb, gsol, *polar(165.0, th + 0.05)[:2], 12.0, S(6, 2), 1100 + i, size=(1.0, 2.6), z=0.5)
        if state == 'Ruin':
            rubble(mb, comps, 0, 0, 16.0, S(10, 3), 1200, size=(1.0, 2.8), z=2.8)
    so['fx'].append({'name': 'anchor_glow', 'pos': [0, 0, 3.0], 'kind': 'point_light', 'note': 'ring of light at the tether foot, #fffaf0'})
    so['fx'].append({'name': 'tether_light_line', 'pos': [0, 0, 26.0], 'kind': 'beam_line', 'note': 'a faint pulse running up the tether (UE panner on the Detail emissive)'})
    return mb, so


def ladder_tether(state):
    f = sbf('ladder')
    core = f('Detail', 'tether_core')
    mb = MB()
    top = {'Intact': 3000.0, 'Construction': 190.0, 'Damaged': 420.0, 'Ruin': 58.0}[state]
    zs = [26.0, 40.0, 80.0, 160.0, 300.0, 500.0, 800.0, 1200.0, 1700.0, 2300.0, 3000.0]
    zs = [z for z in zs if z < top] + [top]
    pts = [(0.0, 0.0, z) for z in zs]
    sides = S(8, 4) if CTX.lod < 3 else 4
    tube(mb, core, pts, 1.0, sides=sides, r_end=0.8 if state == 'Intact' else 1.0, caps=state != 'Intact')
    if state in ('Damaged', 'Ruin') and CTX.lod < 2:
        # frayed strands at the break
        for k in range(5):
            th = TAU * k / 5
            tube(mb, core, [(0, 0, top - 2), (2.5 * math.cos(th), 2.5 * math.sin(th), top + 9 + 3 * (k % 2))], 0.25, sides=3)
    fade = (1800.0, 3000.0) if state == 'Intact' else None
    return mb, fade


def ladder_climber():
    f = sbf('ladder')
    climb, gold = f('Detail', 'climber'), f('Detail', 'gold')
    mb = MB()
    prof = [(1.3, 0.0, gold), (2.2, 0.7, gold), (2.45, 1.6, climb), (2.45, 8.4, gold), (2.2, 9.3, gold), (1.3, 10.0, gold), (1.3, 0.0, None)]
    revolve(mb, prof, S(16, 6, 2))
    return mb


def ladder_ornament(culture):
    f = sbf('ladder')
    gold, comps, slit = f('Detail', 'gold'), f('Structure', 'composite_smooth'), f('Detail', 'light_slit')
    mb = MB()
    if culture == 'A':      # gold petal crown
        for k in range(12):
            th = TAU * k / 12
            pts = [polar(3.6 + 2.2 * t + 1.8 * t * t, th, 0.2 + 7.0 * t) for t in [i / 6 for i in range(7)]]
            tube(mb, gold, pts, 0.55, sides=4, r_end=0.12, up=(0, 0, 1))
        revolve(mb, [(4.6, 0.0, gold), (4.6, 1.2, gold), (3.2, 1.2, None)], S(32, 8))
    elif culture == 'B':    # ribbed ring
        n = S(48, 8)
        tube(mb, gold, [polar(7.0, TAU * i / n, 3.0) for i in range(n + 1)], 0.8, sides=8)
        for k in range(24):
            th = TAU * k / 24
            tube(mb, comps, [polar(3.4, th, 0.4), polar(7.0, th, 3.0)], 0.22, sides=4)
    else:                   # stacked discs narrowing upward
        z = 0.0
        prof = []
        for i, r in enumerate((7.5, 6.0, 4.6, 3.3)):
            prof += [(r, z, comps), (r, z + 1.1, gold if i % 2 else comps)]
            z += 1.6
            nxt = (6.0, 4.6, 3.3, 1.6)[i]
            prof.append((nxt, z - 0.5, comps))
        prof.append((1.3, z, None))
        revolve(mb, prof, S(32, 8))
    return mb


# ============================================================================================ SUNWELL

def sunwell_steps():
    """Returns list of (r_outer, z_top) of each step and the key radii."""
    up = [(100.0 - 3.4 * j, 0.9 - 1.4 * j) for j in range(10)]
    lo = [(56.0 - 3.4 * j, -13.1 - 1.4 * j) for j in range(9)]
    return up, lo


def sunwell_profile(state, f, hull):
    white, riser, balu, pav = f('Structure', 'white_stone'), f('Structure', 'riser_gold'), f('Structure', 'balustrade'), f('Structure', 'avenue_paving')
    water, goldl, grass, earth = f('Roof', 'pool_water'), f('Roof', 'gold_leaf'), f('Roof', 'planter_grass'), f('Roof', 'earth_cut')
    up, lo = sunwell_steps()
    prof = [(114.0, -0.3, pav), (114.0, 0.9, pav)]
    if hull:
        return [(114.0, -0.3, pav), (114.0, 0.9, pav), (100.0, 0.9, white), (66.0, -13.1, white), (56.0, -13.1, white),
                (25.4, -25.7, white), (22.4, -26.4, water), (0.0, -26.4, None)], None
    prof.append((101.0, 0.9, white))
    for j, (r, z) in enumerate(up):
        prof.append((r, z, riser))
        prof.append((r, z - 1.4, white))
    # broad terrace with a raised planter ring
    zt = -13.1
    prof += [(66.0, zt, white), (63.0, zt, white), (63.0, zt + 0.55, grass), (59.0, zt + 0.55, white), (59.0, zt, white)]
    prof.append((56.6, zt, white))
    for j, (r, z) in enumerate(lo):
        rr = r if j else 56.0
        prof.append((rr, z, riser))
        prof.append((rr, z - 1.4, white))
    zp = lo[-1][1] - 1.4
    prof += [(25.4, zp, white), (22.4, zp, white), (22.4, zp - 0.7, water), (0.0, zp - 0.7, None)]
    # alternative (collapsed / unfinished) profile: a rough straight slope from the rim to the pool
    alt = []
    for p in prof:
        r = p[0]
        if r >= 101.0:
            alt.append((r, p[1]))
        elif r <= 22.4:
            alt.append((r, p[1] - (0.6 if state == 'Ruin' else 0.0)))
        else:
            s = (101.0 - r) / (101.0 - 22.4)
            alt.append((r, 0.9 + (zp - 0.9) * (s ** 0.9)))
    return prof, alt


def sunwell(state):
    f = sbf('sunwell')
    white, riser, balu, pav = f('Structure', 'white_stone'), f('Structure', 'riser_gold'), f('Structure', 'balustrade'), f('Structure', 'avenue_paving')
    water, goldl, grass, earth = f('Roof', 'pool_water'), f('Roof', 'gold_leaf'), f('Roof', 'planter_grass'), f('Roof', 'earth_cut')
    bronze, mirror, beamc, lamp = f('Detail', 'bronze'), f('Detail', 'mirror'), f('Detail', 'beam_core'), f('Detail', 'lamp')
    mb = MB()
    so = sockets_empty()
    hull = CTX.lod >= 3
    big = S(192, 16, 16) if not hull else 32
    prof, alt = sunwell_profile(state, f, hull)
    up, lo = sunwell_steps()

    morph = None
    if state == 'Construction':
        morph = lambda th: 0.0 if in_arc(th, -105 * DEG, 45 * DEG) else (1.0 if not in_arc(th, -105 * DEG, 60 * DEG) else (adist(th, -105 * DEG) - 45 * DEG) / (15 * DEG))
    elif state == 'Damaged':
        morph = lambda th: max(0.0, 1.0 - adist(th, 50 * DEG) / (20 * DEG)) ** 0.6 * (0.85 + 0.15 * hnoise(th, 41))
    elif state == 'Ruin':
        morph = lambda th: min(1.0, 0.25 + max(0.0, 0.9 * hnoise(th, 43) + 0.3) + max(0.0, 1.0 - adist(th, 50 * DEG) / (22 * DEG)))

    def alt_sb(i, sb):
        if sb is water:
            return earth if state == 'Ruin' else water
        if sb is pav:
            return pav
        return earth

    if state == 'Ruin':
        # verifier fix: the earth/stone split followed morph > 0.5 exactly, so the ruin read as clean radial pie
        # wedges from far away; a per-ring jitter leaves ragged stone remnants on some steps instead
        def ruin_sb(i, th, sb):
            w = morph(th) + 0.28 * math.sin(i * 1.93 + th * 7.0) + 0.12 * math.sin(i * 0.71 - th * 13.0)
            return alt_sb(i, sb) if w > 0.5 else sb
        revolve(mb, prof, big, alt=alt, morph=morph, sbfn=ruin_sb if not hull else None,
                alt_sb=alt_sb if hull else None)
    else:
        revolve(mb, prof, big, alt=alt, morph=morph, alt_sb=alt_sb if morph else None)
    # balustrades: rim and broad terrace
    if not hull:
        def bkeep(th, r, z, i):
            if morph and morph(th) > 0.3:
                return False
            if state == 'Ruin':
                return False
            return True
        bal = [(101.4, 0.9, balu), (101.4, 2.1, white), (100.6, 2.1, balu), (100.6, 0.9, None)]
        revolve(mb, bal, S(192, 16, 16), keep=bkeep)
        bal2 = [(56.8, -13.1, balu), (56.8, -11.9, white), (56.0, -11.9, balu), (56.0, -13.1, None)]
        revolve(mb, bal2, S(128, 16, 16), keep=bkeep)
        # rim walkway planter ring with the outer tree row
        if state != 'Ruin':
            revolve(mb, [(109.5, 0.9, white), (109.5, 1.45, grass), (106.5, 1.45, white), (106.5, 0.9, None)], S(160, 16, 16))
    # ---- radial stairs (avenue axes)
    if not hull:
        for k, th in enumerate((FRONT, 0.0, math.pi / 2, math.pi)):
            if morph and morph(th) > 0.5:
                continue
            rot = th - math.pi / 2
            steps = [(r, z) for r, z in up] + [(56.0, -13.1)] + [(r, z) for r, z in lo[1:]]
            if state == 'Construction':
                steps = steps[10:]
            for j, (r, z) in enumerate(steps):
                if CTX.lod == 0:
                    for s in range(3):
                        rc = r - 0.85 * (s + 0.5)
                        x, y, _ = polar(rc, th)
                        box(mb, white, x, y, z - 1.45, z - 0.35 * (s + 1), 12.0, 0.85, rot=rot)
                else:
                    x, y, _ = polar(r - 1.3, th)
                    box(mb, white, x, y, z - 1.45, z - 0.7, 12.0, 2.6, rot=rot)
                if CTX.lod <= 1:
                    for side in (-6.4, 6.4):
                        rc = r - 1.7
                        x = rc * math.cos(th) - side * math.sin(th)
                        y = rc * math.sin(th) + side * math.cos(th)
                        box(mb, balu, x, y, z - 1.5, z + 0.5, 0.8, 3.4, rot=rot, top=white)
    # ---- the bronze pillar
    zp = lo[-1][1] - 1.4 - 0.7
    if not hull:
        revolve(mb, [(6.5, zp - 0.6, bronze), (6.5, zp + 1.6, goldl), (4.2, zp + 2.6, bronze), (3.0, zp + 2.6, None)], S(48, 8, 4))
    top = 44.0
    if state == 'Construction':
        top = 4.0
    elif state == 'Damaged':
        top = 18.0
    elif state == 'Ruin':
        top = zp + 14.0
    shaft = [(2.45, zp + 2.6, bronze)]
    zz = zp + 2.6
    band_z = [zp + 20.0, zp + 44.0, zp + 62.0]
    for bz in band_z:
        if bz > top - 1:
            break
        rr = 2.45 + (1.95 - 2.45) * (bz - zp) / (44.0 - zp)
        shaft += [(rr, bz, goldl), (rr + 0.2, bz + 0.3, goldl), (rr + 0.2, bz + 1.1, goldl), (rr, bz + 1.4, bronze)]
    rr = 2.45 + (1.95 - 2.45) * (top - zp) / (44.0 - zp)
    if state == 'Intact':
        shaft += [(1.95, 44.0, goldl), (3.4, 46.5, goldl), (3.4, 47.5, bronze), (1.2, 48.5, bronze), (1.2, 50.0, bronze), (0.0, 50.0, None)]
    else:
        shaft += [(rr, top, bronze), (rr - 0.6, top, bronze), (rr - 0.6, top - 3, None)]
    fl = None
    if not hull:
        alt_s = [(p[0] * 0.93 if p[2] is bronze or p[2] is None else p[0], p[1]) for p in shaft]
        fl = (alt_s, lambda th: 0.5 + 0.5 * math.cos(24 * th))
    sj = None
    if state != 'Intact':
        sj = lambda th, t=top: t + 1.2 * math.sin(3 * th) + 0.6 * math.sin(7 * th)
    if fl:
        revolve(mb, shaft, S(96, 24, 24), alt=fl[0], morph=fl[1], jag=sj)
    else:
        revolve(mb, [(2.45, zp, bronze), (1.95, 50.0, bronze), (0.0, 50.0, None)], 6)
    if state == 'Intact':
        so['ornament'].append({'pos': [0.0, 0.0, 50.0], 'rot_z_deg': 0.0, 'slot': 'sun_disc'})
        so['fx'].append({'name': 'sky_beam', 'pos': [0.0, 0.0, 57.0], 'kind': 'light_shaft',
                         'note': 'SM_Wonder_Sunwell_Beam, additive gold-white #fff1c8, pulses when devotion is high'})
    if state == 'Construction' and not hull:
        scaffold(mb, bronze, 0, 0, zp + 2.6, 14.0, 7.0, 7.0, bay=3.5, lift=4.0, r=0.14)
        crane(mb, bronze, 40.0, 30.0, 40.0, 40.0, rot=3.6)
        crane(mb, bronze, -70.0, 60.0, 30.0, 34.0, rot=-0.9)
        for k in range(S(6, 2)):
            rubble(mb, earth, *polar(80.0, 40 * DEG + k * 45 * DEG)[:2], 8.0, 5, 1300 + k, size=(1.2, 3.0), z=-6.0)
    if state == 'Damaged' and not hull:
        # the upper pillar lies broken across the terraces
        d = 200 * DEG
        p0 = (2.0 * math.cos(d), 2.0 * math.sin(d), 16.0)
        p1 = (30.0 * math.cos(d), 30.0 * math.sin(d), -19.5)
        tube(mb, bronze, [p0, p1], 2.1, sides=S(16, 6), caps=True)
        rubble(mb, white, *polar(80.0, 50 * DEG)[:2], 18.0, S(20, 5), 1400, size=(1.2, 3.2), z=-7.0)
    if state == 'Ruin' and not hull:
        for k in range(S(7, 2)):
            th = TAU * k / 7 + 0.3
            rubble(mb, white, *polar(70.0, th)[:2], 16.0, S(10, 3), 1500 + k, size=(1.2, 3.2), z=-9.0)
        rubble(mb, bronze, 8.0, -6.0, 12.0, S(8, 2), 1600, size=(1.0, 2.6), z=zp + 0.6)
    # ---- avenues (paving with planting strips; trees as sockets)
    if not hull:
        for k, th in enumerate((FRONT, 0.0, math.pi / 2, math.pi)):
            def P(r, s, z):
                return (r * math.cos(th) - s * math.sin(th), r * math.sin(th) + s * math.cos(th), z)
            flat_quad(mb, pav, P(114.0, -6.0, 0.35), P(176.0, -6.0, 0.35), P(176.0, 6.0, 0.35), P(114.0, 6.0, 0.35))
            for side in (-1, 1):
                if state != 'Ruin':
                    box(mb, white, *P(145.0, side * 8.0, 0)[:2], -0.3, 0.75, 62.0, 4.0, rot=th, top=grass)
                lo_ = 10.0 if side > 0 else -14.0
                flat_quad(mb, pav, P(114.0, lo_, 0.3), P(176.0, lo_, 0.3), P(176.0, lo_ + 4.0, 0.3), P(114.0, lo_ + 4.0, 0.3))
                if state == 'Intact':
                    for i in range(8):
                        r = 118.0 + i * 8.0
                        so['trees'].append({'pos': [round(v, 2) for v in P(r, side * 8.0, 0.75)], 'kind': 'avenue_cypress' if i % 2 else 'avenue_broadleaf'})
        # lamps around the rim
        if CTX.lod <= 1 and state in ('Intact', 'Damaged'):
            for k in range(48):
                th = TAU * (k + 0.5) / 48
                if morph and morph(th) > 0.3:
                    continue
                x, y, _ = polar(103.6, th)
                tube(mb, bronze, [(x, y, 0.9), (x, y, 5.2)], 0.12, sides=4)
                box(mb, lamp, x, y, 5.2, 5.9, 0.5, 0.5, rot=th)
    if state == 'Intact':
        for k in range(64):
            th = TAU * (k + 0.5) / 64
            if any(adist(th, a) < 5 * DEG for a in (FRONT, 0.0, math.pi / 2, math.pi)):
                continue
            so['trees'].append({'pos': [round(v, 2) for v in polar(108.0, th, 1.45)], 'kind': 'ring_broadleaf'})
        for k in range(40):
            th = TAU * (k + 0.5) / 40
            if any(adist(th, a) < 7 * DEG for a in (FRONT, 0.0, math.pi / 2, math.pi)):
                continue
            so['trees'].append({'pos': [round(v, 2) for v in polar(61.0, th, -12.55)], 'kind': 'terrace_small'})
    so['fx'].append({'name': 'pool_glow', 'pos': [0.0, 0.0, round(zp, 2)], 'kind': 'night_marker', 'note': 'pool and rim lamps glow warm at night'})
    return mb, so


def sunwell_beam():
    mb = MB()
    sb = SB(0, 0, 400.0, 100.0)    # single-material mesh (M_Wonder_LightShaft): UVs u along, v around
    zs = [57.0, 120.0, 260.0, 500.0, 900.0, 1500.0]
    tube(mb, sb, [(0.0, 0.0, z) for z in zs], 1.6, sides=S(12, 6), r_end=4.0)
    return mb, (700.0, 1500.0)


def sunwell_ornament(culture):
    f = sbf('sunwell')
    bronze, mirror, goldl = f('Detail', 'bronze'), f('Detail', 'mirror'), f('Roof', 'gold_leaf')
    mb = MB()
    tilt = 25 * DEG          # mirror tilted back so it throws light upward
    cz = 7.2

    def disc_xf(p):          # local disc plane x-z (facing -Y), tilted about X
        x, y, z = p
        c, s = math.cos(tilt), math.sin(tilt)
        return (x, y * c - z * s, cz + y * s + z * c)

    def in_plane(r, a, d=0.0):     # point on a circle in the disc plane (x, z), offset d along the normal (-y)
        return disc_xf((r * math.cos(a), -d, r * math.sin(a)))
    # yoke from the capital
    tube(mb, bronze, [(0, 0, 0), (0, 0, 2.2)], 1.0, sides=S(12, 6))
    for sgn in (-1, 1):
        tube(mb, bronze, [(0, 0, 2.0), (sgn * 5.0, 0, 3.4), in_plane(7.6, math.pi if sgn < 0 else 0.0)], 0.45, sides=6)
    # central mirror disc (a flat lens: two faces)
    n = S(32, 12)
    sub = MB()
    revolve(sub, [(0.0, -0.35, bronze), (4.6, -0.35, bronze), (4.6, 0.35, mirror), (0.0, 0.35, None)], n)
    # rotate the revolve (axis Z) so its axis becomes -Y, then tilt
    mb.extend(sub, lambda p: disc_xf((p[0], -p[2], p[1])))
    if culture in ('A', 'B'):
        span = (0.0, TAU) if culture == 'A' else (-60 * DEG, 240 * DEG)
        m = S(48, 12)
        pts = [in_plane(7.4, span[0] + (span[1] - span[0]) * i / m) for i in range(m + 1)]
        tube(mb, goldl, pts, 0.75, sides=8)
        if culture == 'A':
            for k in range(32):
                a = TAU * k / 32
                L = 11.0 if k % 2 == 0 else 9.4
                tube(mb, goldl, [in_plane(8.0, a), in_plane(L, a)], 0.32, sides=4, r_end=0.04)
        else:
            # crescent horns
            for a in (-60 * DEG, 240 * DEG):
                tube(mb, goldl, [in_plane(7.4, a), in_plane(9.6, a + (0.25 if a < 0 else -0.25))], 0.5, sides=6, r_end=0.05)
    else:
        for i, (r, d) in enumerate(((9.0, 0.6), (7.6, 1.4), (6.2, 2.2))):
            m = S(40, 12)
            tube(mb, goldl if i % 2 == 0 else bronze, [in_plane(r, TAU * j / m, d) for j in range(m + 1)], 0.45, sides=6)
    return mb


# ============================================================================================ STAIR OF RAINS
ST_ARC = 84 * DEG       # half-angle of the terraced front


def st_warp(th, z):
    """Organic radial wobble so the terrace walls follow a mountain contour instead of perfect circles."""
    return 1.0 + 0.045 * math.sin(2 * th + 0.3) + 0.03 * math.sin(5 * th + 1.1 + z * 0.012) + 0.012 * math.sin(11 * th + z * 0.03)


def stair_terraces():
    rs = [112.0, 101.0, 90.5, 80.5, 71.0, 62.0, 53.5, 45.5, 38.0]
    hs = [12.0, 12.0, 11.5, 11.0, 10.5, 10.0, 9.5, 9.0, 8.5]
    out = []
    z = 16.0
    for r, h in zip(rs, hs):
        out.append((r, z, h))
        z = z + h + 0.2
    return out, z


def stair_profile(f, built=9, hull=False):
    hst, hv, rock, marble = f('Structure', 'honey_stone'), f('Structure', 'honey_vines'), f('Structure', 'rock'), f('Structure', 'marble')
    gt, fol = f('Roof', 'garden_terrace'), f('Roof', 'foliage')
    T, ztop = stair_terraces()
    if hull:
        return [(152.0, -2.0, rock), (150.0, 0.0, fol), (112.0, 16.0, hst), (112.0, 52.0, gt), (71.0, 52.0, hst), (71.0, 86.0, gt),
                (30.0, ztop, fol), (16.0, ztop + 4.2, marble), (0.0, ztop + 4.2, None)]
    prof = [(152.0, -2.0, rock), (150.0, 0.0, fol), (138.0, 5.0, fol), (126.0, 10.5, fol), (116.5, 14.8, gt), (T[0][0], T[0][1], hv)]
    for j, (r, z, h) in enumerate(T):
        if j >= built:
            break
        top = z + h
        rn = T[j + 1][0] if j + 1 < len(T) else 30.0
        wall_sb = hv if j % 2 == 0 else hst
        prof[-1] = (r, z, wall_sb)
        prof += [(r, top, hst), (r, top + 1.0, marble), (r - 0.8, top + 1.0, hst), (r - 0.8, top + 0.2, gt), (rn, top + 0.2, hv)]
    if built < len(T):
        r0, z0 = prof[-1][0], prof[-1][1]
        prof[-1] = (r0, z0, rock)
        prof += [(r0 * 0.6, z0 + (ztop - z0) * 0.55, rock), (18.0, ztop + 3.0, rock), (0.0, ztop + 4.0, None)]
        return prof
    prof[-1] = (30.0, ztop, fol)
    prof += [(22.0, ztop + 3.0, fol), (16.0, ztop + 4.2, marble), (16.0, ztop + 4.8, marble), (13.5, ztop + 4.8, marble),
             (13.5, ztop + 5.4, marble), (11.5, ztop + 5.4, marble), (11.5, ztop + 6.0, marble), (0.0, ztop + 6.0, None)]
    return prof


def stair_alt_z(r):
    T, ztop = stair_terraces()
    s = (126.0 - min(r, 126.0)) / 110.0
    return 10.5 + (ztop + 4.0 - 10.5) * (1 - (1 - min(s, 1.0)) ** 1.35)


def stair_alt(prof):
    """Smooth mountain flank through the same profile points (risers collapse, treads slope)."""
    out = []
    for p in prof:
        r = p[0]
        if r >= 126.0 or r <= 16.0:
            out.append((r, p[1]))
        else:
            out.append((r, stair_alt_z(r)))
    return out


def stair(state):
    f = sbf('stair')
    hst, hv, rock, marble = f('Structure', 'honey_stone'), f('Structure', 'honey_vines'), f('Structure', 'rock'), f('Structure', 'marble')
    gt, pink, lilac, fol = f('Roof', 'garden_terrace'), f('Roof', 'blossom_pink'), f('Roof', 'blossom_lilac'), f('Roof', 'foliage')
    fall, pool, latt, lant = f('Detail', 'waterfall'), f('Detail', 'pool_water'), f('Detail', 'lattice'), f('Detail', 'lantern')
    mb = MB()
    so = sockets_empty()
    hull = CTX.lod >= 3
    T, ztop = stair_terraces()
    built = 5 if state == 'Construction' else 9
    prof = stair_profile(f, built, hull)
    alt = stair_alt(prof) if not hull else None
    zsum = ztop + 6.0
    W = st_warp

    def m_base(th):
        d = adist(th, FRONT)
        edge = ST_ARC + 8 * DEG * hnoise(th, 51)
        return min(1.0, max(0.0, (d - edge) / (14 * DEG)))
    morph = m_base
    if state == 'Damaged':
        morph = lambda th: max(m_base(th), max(0.0, 1.0 - adist(th, -42 * DEG) / (12 * DEG)) ** 0.5)
    elif state == 'Ruin':
        morph = lambda th: max(m_base(th), min(1.0, 0.45 + 0.5 * max(0.0, hnoise(th, 53) + 0.2)))

    def alt_sb(i, sb):
        if sb in (hst, hv, marble):
            return rock
        if sb is gt:
            return fol
        return sb

    def sbfn(i, th, sb):
        if sb is hst and hnoise(th * 2.0, 57 + i) > 0.35:
            return hv
        return sb
    big = S(192, 16, 16) if not hull else 24
    revolve(mb, prof, big, alt=alt, morph=morph if alt else None, alt_sb=alt_sb, sbfn=sbfn if not hull else None, rwarp=W)
    if hull:
        return mb, so

    def onwall(r, th, z):
        return polar(r * W(th, z), th, z)

    # rock outcrops on the flanks and the back only
    rnd = random.Random(61)
    for k in range(S(30, 6)):
        th = FRONT + math.pi + rnd.uniform(-1.0, 1.0) * (math.pi - ST_ARC - 10 * DEG)
        s = rnd.uniform(0.1, 0.9)
        r = 126.0 - 110.0 * s
        z = stair_alt_z(r)
        blob(mb, rock, onwall(r + 2.0, th, z - 1.5), rnd.uniform(5.0, 12.0), 6100 + k, sub=1, squash=0.6, rough=0.4)

    # ---- waterfalls and pools down the centre (and a side cascade)
    wet = state in ('Intact', 'Damaged')
    cols = [(FRONT, 3.4, range(1, built))]
    if built == 9:
        cols.append((FRONT + 30 * DEG, 2.4, range(2, 6)))
    from wb_geo import atlas_v
    for ci, (th, w0, js) in enumerate(cols):
        if state == 'Damaged' and ci == 1:
            continue
        for j in js:
            r, z, h = T[j]
            wtop = z + h + 1.0
            zb = z
            w = w0 + (8 - j) * 0.3
            rw = r * W(th, zb)
            tread = (T[j - 1][0] - 0.8 - r) * W(th, zb)
            rp = min(3.0 + (8 - j) * 0.45 if ci == 0 else 2.6, (tread - 1.8) / 2.0)
            pc_r = rw + rp + 0.6
            pr_out = T[j - 1][0] * W(th, zb) - 0.8
            px, py, _ = polar(pc_r, th)
            base_z = zb - (0.0 if pc_r + rp < pr_out else min(T[j - 1][2] * 0.6, 6.0))
            if state != 'Ruin' or j % 2 == 0:
                revolve(mb, [(rp + 0.7, base_z - 0.3, marble), (rp + 0.7, zb + 0.9, marble), (rp, zb + 0.9, marble), (rp, zb + 0.55, pool if wet else marble),
                             (0.0, zb + 0.55, None)], S(24, 8, 4), xf=xform(0, (px, py, 0)))
                so['fx'].append({'name': 'mist_%d_%d' % (ci, j), 'pos': [round(px, 2), round(py, 2), round(zb + 1.0, 2)], 'kind': 'mist', 'note': 'spray where the fall lands'})
            if not wet:
                continue
            n = S(4, 2)
            rows = []
            for i in range(n + 1):
                t = i / n
                zz = wtop - (wtop - zb - 0.6) * t
                rr = r * W(th, zz) + 0.35 + 1.4 * math.sin(t * math.pi * 0.5)
                row = []
                for sgn in (-1, 1):
                    a = th + sgn * (w * 0.5) / max(rr, 1.0) * (1.0 + 0.25 * t)
                    row.append(polar(rr, a, zz))
                rows.append(row)
            Lf = 0.0
            bk = lambda q, th=th: (q[0] - 0.06 * math.cos(th), q[1] - 0.06 * math.sin(th), q[2])
            for i in range(n):
                a0_, b0_ = rows[i]
                a1_, b1_ = rows[i + 1]
                seg = math.dist(a0_, a1_)
                u0, u1 = Lf / fall.U, (Lf + seg) / fall.U
                Lf += seg
                v0, v1 = atlas_v(fall.band, 0.0), atlas_v(fall.band, 1.0)
                ia, ib, ic, id_ = mb.vert(a0_), mb.vert(b0_), mb.vert(b1_), mb.vert(a1_)
                mb.face((ia, id_, ic, ib), ((u0, v0), (u1, v0), (u1, v1), (u0, v1)), fall.slot)
                ja, jb, jc, jd = mb.vert(bk(a0_)), mb.vert(bk(b0_)), mb.vert(bk(b1_)), mb.vert(bk(a1_))
                mb.face((ja, jb, jc, jd), ((u0, v0), (u0, v1), (u1, v1), (u1, v0)), fall.slot)
            if ci == 0 and j == 5:
                so['fx'].append({'name': 'rainbow', 'pos': [round(v, 2) for v in polar(rw + 14.0, th + 0.12, z + 6.0)], 'kind': 'rainbow',
                                 'note': 'faint rainbow in the spray, low sun only'})

    # ---- blossom trees and trailing greenery on the terraces
    rnd = random.Random(71)
    q = CTX.q
    for j in range(built):
        r, z, h = T[j]
        rn = T[j + 1][0] if j + 1 < len(T) else 30.0
        top = z + h + 0.2
        rc = (r - 0.8 + rn) * 0.5
        width = (r - 0.8 - rn)
        half = ST_ARC - 2 * DEG
        n = int(2 * half * rc / 8.0 * q ** 0.85)
        for i in range(n):
            th = FRONT - half + 2 * half * (i + rnd.uniform(0.15, 0.85)) / max(n, 1)
            pick = rnd.random()
            jitter = rnd.uniform(-0.32, 0.32)
            size = rnd.uniform(3.2, 5.4)
            extra = rnd.random()
            if adist(th, FRONT) < 4.5 * DEG * (112.0 / rc) ** 0.6 or (built == 9 and adist(th, FRONT + 30 * DEG) < 3.0 * DEG * (112.0 / rc) ** 0.6 and 2 <= j <= 5):
                continue
            if morph(th) > 0.35:
                continue
            if state == 'Ruin':
                sb = fol
            elif state == 'Construction':
                sb = fol if pick < 0.75 else pink
                size *= 0.55
            else:
                sb = pink if pick < 0.38 else (lilac if pick < 0.7 else fol)
            if CTX.lod >= 1:
                size *= 1.2
            rr = rc + jitter * width
            blob(mb, sb, onwall(rr, th, top + size * 0.5), size, 7100 + j * 1000 + i, sub=2 if CTX.lod <= 1 else 1, squash=0.78,
                 rough=0.16, flat_bottom=-size * 0.45, soft=True)
            if CTX.lod == 0:
                # verifier fix: a second, smaller lump breaks the single faceted ball into a clumped crown
                # (own RNG so the terrace layout above stays exactly as built)
                lr = random.Random(9100 + j * 1000 + i)
                s3 = size * lr.uniform(0.55, 0.7)
                th3 = th + (size * lr.uniform(0.3, 0.5) / max(rc, 1.0)) * (1 if lr.random() < 0.5 else -1)
                blob(mb, sb, onwall(rr + lr.uniform(-0.8, 0.8), th3, top + size * 0.55 + s3 * 0.35), s3, 9300 + j * 1000 + i,
                     sub=2, squash=0.8, rough=0.18, flat_bottom=-s3 * 0.3, soft=True)
            if extra < 0.3 and CTX.lod == 0:
                s2 = size * 0.65
                th2 = th + (2.2 / max(rc, 1.0)) * (1 if extra < 0.2 else -1)
                blob(mb, fol if sb is not fol else pink, onwall(rr + rnd.uniform(-1.5, 1.5), th2, top + s2 * 0.5), s2, 7500 + j * 1000 + i,
                     sub=2, squash=0.85, rough=0.3, flat_bottom=-s2 * 0.45, soft=True)
        # greenery trailing over the wall top of terrace j
        if CTX.lod <= 1 and state != 'Construction':
            m = int(2 * half * r / 11.0 * q)
            for i in range(m):
                th = FRONT - half + 2 * half * (i + 0.5) / max(m, 1)
                if rnd.random() < 0.45 or morph(th) > 0.35 or adist(th, FRONT) < 5 * DEG:
                    continue
                sz = rnd.uniform(1.4, 2.4)
                blob(mb, fol if rnd.random() < 0.7 else lilac, onwall(r + 0.4, th, top + 0.4), sz, 7800 + j * 1000 + i,
                     sub=2 if CTX.lod == 0 else 1, squash=1.0, rough=0.35, stretch=(0.7, 0.7, 1.6), soft=True)

    # ---- summit pavilion
    if built == 9:
        zf = ztop + 6.0
        for k in range(12):
            th = TAU * (k + 0.5) / 12
            x, y, _ = polar(9.2, th)
            hcol = 8.0
            if state == 'Damaged' and k % 3 == 1:
                hcol = 3.0 + (k % 2)
            if state == 'Ruin':
                if k % 3:
                    continue
                hcol = 2.0 + (k % 4)
            box(mb, marble, x, y, zf, zf + 0.6, 1.5, 1.5, rot=th)
            revolve(mb, [(0.55, zf + 0.6, marble), (0.48, zf + hcol, marble), (0.0, zf + hcol, None)], S(10, 6, 2) if hcol > 7 else 6,
                    xf=xform(0, (x, y, 0)))
            if hcol > 7:
                box(mb, marble, x, y, zf + hcol, zf + hcol + 0.5, 1.4, 1.4, rot=th)
        if state == 'Intact':
            revolve(mb, [(10.3, zf + 8.5, marble), (10.3, zf + 9.8, marble), (8.1, zf + 9.8, marble), (8.1, zf + 8.5, marble), (10.3, zf + 8.5, None)],
                    S(36, 12, 4))
            so['ornament'].append({'pos': [0.0, 0.0, round(zf + 9.8, 3)], 'rot_z_deg': 0.0, 'slot': 'pavilion_canopy'})
            tube(mb, latt, [(0, 0, zf + 9.8), (0, 0, zf + 6.2)], 0.05, sides=3)
            blob(mb, lant, (0.0, 0.0, zf + 5.6), 0.7, 7900, sub=1, rough=0.05, soft=True)
            so['lights'].append({'name': 'pavilion_lantern', 'pos': [0.0, 0.0, round(zf + 5.6, 2)], 'color': '#ffc77a', 'blink': None})
        elif state == 'Damaged':
            rubble(mb, marble, 6.0, -6.0, 9.0, S(8, 2), 7950, size=(0.5, 1.4), z=zf)
        else:
            rubble(mb, marble, 0.0, 0.0, 11.0, S(10, 3), 7960, size=(0.5, 1.6), z=zf)
    # ---- construction / damage dressing
    if state == 'Construction':
        r, z, h = T[5]
        if CTX.lod <= 1:
            scaffold_ring(mb, latt, 0, 0, r * 1.06 + 1.5, z - 0.5, z + h * 0.7, n=S(20, 6), a0=FRONT - 60 * DEG, a1=FRONT + 60 * DEG)
        crane(mb, latt, *polar(66.0, FRONT + 40 * DEG)[:2], 115.0, 40.0, rot=FRONT + 2.4)
        for k in range(S(6, 2)):
            th = FRONT + (k - 2.5) * 20 * DEG
            rubble(mb, hst, *onwall(r - 4.0, th, z)[:2], 4.0, 5, 8000 + k, size=(0.8, 1.6), z=z)
    if state == 'Damaged':
        for k in range(3):
            th = -42 * DEG + (k - 1) * 7 * DEG
            rr = 105.0 - 25 * k
            rubble(mb, hst, *onwall(rr, th, 20)[:2], 12.0, S(10, 3), 8100 + k, size=(1.2, 3.4), z=stair_alt_z(rr) - 1.0)
    if state == 'Ruin':
        rnd = random.Random(81)
        for k in range(S(18, 4)):
            th = FRONT + rnd.uniform(-1, 1) * ST_ARC
            j = rnd.randrange(0, 8)
            r, z, h = T[j]
            rubble(mb, hst, *onwall(r + 4.0, th, z)[:2], 6.0, 5, 8200 + k, size=(1.0, 2.6), z=z)
    so['fx'].append({'name': 'pavilion_glow', 'pos': [0.0, 0.0, round(zsum + 5.0, 2)], 'kind': 'night_marker', 'note': 'lantern-lit pavilion; lit pools down the falls at night'})
    return mb, so


def stair_ornament(culture):
    f = sbf('stair')
    latt, marble = f('Detail', 'lattice'), f('Structure', 'marble')
    mb = MB()
    if culture == 'A':      # white lattice dome
        n = S(10, 4)
        dome = [(10.0 * math.cos(t), 6.5 * math.sin(t), latt if i < n else None) for i, t in [(i, (math.pi / 2) * i / n) for i in range(n + 1)]]
        dome[-1] = (0.0, 6.5, None)
        revolve(mb, [(10.4, 0.0, marble)] + dome, S(32, 8, 4))
        if CTX.lod == 0:
            for k in range(16):
                th = TAU * k / 16
                pts = [polar(10.2 * math.cos(t), th, 6.7 * math.sin(t)) for t in [(math.pi / 2) * i / 10 for i in range(11)]]
                tube(mb, latt, pts, 0.18, sides=4)
        revolve(mb, [(0.9, 6.4, marble), (0.5, 7.6, marble), (0.0, 9.0, None)], S(10, 6))
    elif culture == 'B':    # upswept saddle canopy (two raised points)
        n = S(8, 3)
        prof = [(11.2 - 11.2 * i / n, 0.0 + 3.0 * (i / n) ** 1.3, latt if i < n else None) for i in range(n + 1)]
        alt = [(p[0] * 1.15, p[1] + 3.5 * (1 - p[0] / 11.2) * 0 + 4.0 * (p[0] / 11.2)) for p in prof]
        revolve(mb, prof, S(48, 12, 4), alt=alt, morph=lambda th: math.cos(th) ** 2)
    else:                   # tall cone with a stone finial
        revolve(mb, [(10.6, 0.0, marble), (10.6, 0.6, latt), (0.6, 9.5, marble), (0.9, 10.0, marble), (0.0, 11.6, None)], S(32, 8, 4))
    return mb


# ============================================================================================ registry
BUILDERS = {'choir': choir, 'ear': ear, 'ladder': ladder, 'sunwell': sunwell, 'stair': stair}
ORNAMENTS = {'choir': ('CapA', 'CapB', 'CapC', choir_ornament, 'chimney_cap'),
             'ear': ('CollarA', 'CollarB', 'CollarC', ear_ornament, 'pylon_collar'),
             'ladder': ('CrownA', 'CrownB', 'CrownC', ladder_ornament, 'anchor_crown'),
             'sunwell': ('DiscA', 'DiscB', 'DiscC', sunwell_ornament, 'sun_disc'),
             'stair': ('CanopyA', 'CanopyB', 'CanopyC', stair_ornament, 'pavilion_canopy')}
