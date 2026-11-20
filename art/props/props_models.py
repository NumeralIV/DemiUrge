# DEMIURGE props - the asset registry and every generator.
#
# registry() maps the asset name (mesh = SM_Prop_<name>) to a spec: build(name, collection, bake) -> (lods, extra),
# material roles in slot order, category, description, ages, cultures, placement hints. Generators return a Part
# whose faces carry a role ('Structure', 'Roof', 'Detail'); SLOTS maps each role to its master material.
import math
import random

import bpy

import props_lib as L
import props_rocks as R
import props_kit as K

AGES = ['Stone', 'Bronze', 'Iron', 'Sail', 'Industry', 'Atomic', 'Space']
TEX_DIR = None


def make(gen, slots, lod_mode='decimate', ratios=(0.4, 0.12), weld=0.0):
    """Wrap a Part generator gen(lod, rng) into a build function.

    'native': every LOD is gen(lod). 'decimate' (verifier fix, 2026-11-19): decimating LOD0 straight to 12 % folded
    whole boxes, stones, posts and the hay rick into flat duplicate-face sheets (some LOD2s lost their stones, the
    rick or the whole post). Now the candidates for LOD k are gen(k) as built (it drops small parts and segments),
    gen(k) collapse-decimated towards LOD0 x ratio, and LOD k-1 decimated towards it; a decimation only counts if it
    keeps the silhouette (_safe_decimate), and the lightest candidate that is not heavier than LOD k-1 wins.
    Every LOD keeps the LOD0 material slots."""
    def build(name, coll, bake):
        full = 'SM_Prop_' + name
        lod0 = L.build_object(full + '_LOD0', gen(0, L.rng_for(name)), slots, coll)
        roles = lod0['roles'].split(',')
        t0 = L.tri_count(lod0)
        lods = [lod0]
        for i, r in enumerate(ratios, start=1):
            nm = '%s_LOD%d' % (full, i)
            src = L.build_object(nm + '_src', gen(i, L.rng_for(name)), slots, coll, roles=roles)
            target = t0 * r
            cands = [src]
            if lod_mode != 'native':
                for base in (src, lods[-1]):
                    if L.tri_count(base) > target * 1.25:
                        o = _safe_decimate(base, nm + '_dec', target, coll)
                        if o is not None:
                            cands.append(o)
            prev_t = L.tri_count(lods[-1])
            ok = [c for c in cands if L.tri_count(c) <= prev_t]
            if not ok:   # nothing lighter keeps the shape: repeat the previous LOD
                cp = lods[-1].copy()
                cp.data = lods[-1].data.copy()
                coll.objects.link(cp)
                cands.append(cp)
                ok = [cp]
            best = min(ok, key=L.tri_count)
            for c in cands:
                if c is not best:
                    me = c.data
                    bpy.data.objects.remove(c)
                    bpy.data.meshes.remove(me)
            best.name = best.data.name = nm
            lods.append(best)
        return lods, {}
    return build


def _safe_decimate(src, name, target, coll):
    """Collapse-decimate src towards target triangles, but only accept a result that keeps the silhouette: no
    more than 12 % of its faces folded into duplicate sheets, every bounding-box side at least 90 % of the source
    and at least 90 % of its surface area (a cone, a pole or an obelisk folded into slivers keeps its bounding box). Tries 1x, 1.5x and 2.2x the target; returns None if none holds (thin props, posts and fingerposts
    otherwise vanish entirely at LOD2)."""
    st = L.tri_count(src)
    dims = src.dimensions.copy()
    area = sum(p.area for p in src.data.polygons)
    for f in (1.0, 1.5, 2.2):
        ratio = target * f / st
        if ratio >= 0.9:
            return None
        o = L.decimated_copy(src, name, ratio, coll)
        t = L.tri_count(o)
        ok = t > 0 and o['cleaned'] <= 0.12 * (t + o['cleaned']) and             all(o.dimensions[k] >= dims[k] * 0.9 for k in range(3)) and             sum(p.area for p in o.data.polygons) >= 0.9 * area
        if ok:
            return o
        me = o.data
        bpy.data.objects.remove(o)
        bpy.data.meshes.remove(me)
    return None


def rock_build(spec, seed):
    def build(name, coll, bake):
        lods, info = R.build_rock(name, spec, coll, L.TEX_DIR, seed, bake)
        b = 'T_Prop_' + name.replace('_', '')
        return lods, {'textures': [b + '_N', b + '_ORM'],
                      'manifest': {'baked_maps': {'normal': b + '_N', 'orm': b + '_ORM', 'uv': 'UVUnique'},
                                   'uv': 'UV0 UVUnique = 0..1 non-overlapping (baked N / ORM, tangent basis), '
                                         'UV1 UVMap = tiling rock detail (2 m tiles)',
                                   'sculpt_tris': info['high_tris']}}
    return build


# =========================================================================== registry
def registry():
    reg = {}
    seeds = {'Rock_A': 11, 'Rock_B': 23, 'Rock_C': 37, 'Rock_D': 41, 'Rock_E': 53, 'Rock_F': 67, 'Rock_G': 71,
             'Rock_H': 83, 'Cliff_A': 101, 'Cliff_B': 113, 'Cliff_C': 127, 'Cliff_D': 131}
    for n, spec in R.ROCKS.items():
        cliff = n.startswith('Cliff')
        reg[n] = dict(build=rock_build(spec, seeds[n]), roles=['Structure'],
                      category='cliff' if cliff else 'rock', desc=spec['kind'],
                      cultures='any (biome tint by material instance: Granite, Sandstone, Basalt, Limestone)',
                      placement=('terrain dressing on steep slopes, coasts and canyon walls; rotate freely about Z, '
                                 'scale 0.7-1.4, sink up to 2 m' if cliff else
                                 'scatter on slopes, riverbanks, field edges and moorland; any Z rotation, '
                                 'scale 0.6-1.5, tilt up to 10 degrees'),
                      **({'budget_note': 'terrain-scale (10-22 m): budgeted like a house (~2k) rather than a '
                                         'small prop; LOD3 hull for > 10 m'} if cliff else {}))
    add_kit(reg)
    return reg


def _st(fn, **kw):
    return lambda lod, rng: fn(lod, rng, **kw)


def add_kit(reg):
    WOOD = {'Structure': 'M_Prop_Wood'}

    def put(name, gen, slots, category, desc, ages, lod='native', **kw):
        reg[name] = dict(build=make(gen, slots, lod), category=category, desc=desc,
                         ages=[AGES[a] for a in ages], **kw)

    # ---- fences and walls: 4 m tiles along local X, pivot at the tile centre
    fen = dict(tile_length_m=4.0, placement='tile along field and lane edges at 4.0 m pitch along local X; '
               'segments share their end posts; rotate 180 degrees about Z at random to break repetition')
    for nm, fn, slots, ages, cul, desc, lod in (
            ('Fence_Wattle', K.fence_wattle, WOOD, [0, 1, 2], 'any', 'Woven wattle hurdle (stakes and rods)', 'native'),
            ('Fence_Rail', K.fence_rail, WOOD, [2, 3, 4], 'any', 'Post-and-rail timber fence', 'decimate'),
            ('Fence_DryStone', K.fence_drystone, {'Structure': 'M_Prop_RockTile'}, [1, 2, 3, 4, 5, 6],
             'any; signature of RingBuilder', 'Dry-stone wall with on-edge coping', 'decimate'),
            ('Fence_Iron', K.fence_iron, {'Structure': 'M_Prop_Metal'}, [4, 5, 6], 'any',
             'Cast-iron railing with spear tips (MI PaintedGreen or Iron)', 'native')):
        put(nm, fn, slots, 'fence', desc, ages, lod, cultures=cul, group=nm, state='Intact', **fen)
        put(nm + '_Damaged', _st(fn, damaged=True), slots, 'fence', desc + ', damaged', ages, lod,
            cultures=cul, group=nm, state='Damaged', states_of=nm, **fen)
    hed = dict(tile_length_m=4.0, placement='tile along field edges at 4.0 m pitch (cards overhang 0.2 m each end); '
               'mix A and B about 4:1; MI Summer / Autumn / Dry by season and biome')
    put('Hedge_A', _st(K.hedge, tall=False), {'Structure': 'M_Prop_Foliage'}, 'hedgerow',
        'Hedgerow segment, 1.7 m', [1, 2, 3, 4, 5, 6], cultures='any', **hed)
    put('Hedge_B', _st(K.hedge, tall=True), {'Structure': 'M_Prop_Foliage', 'Roof': 'M_Prop_Wood'}, 'hedgerow',
        'Hedgerow segment with a hedgerow tree, 4 m', [1, 2, 3, 4, 5, 6], cultures='any', **hed)
    # ---- crops and hay
    crop = dict(tile_length_m=4.0, placement='tile 4 x 4 m over a field polygon, rows along local X; clip at the '
                'field edge; MI Green (spring) / Ripe (summer) / Barley')
    put('Crop_Grain', K.crop_grain, {'Structure': 'M_Prop_Crop'}, 'crop', 'Grain rows (wheat / barley), 4 x 4 m tile',
        [1, 2, 3, 4, 5, 6], **crop)
    put('Crop_Veg', K.crop_veg, {'Structure': 'M_Prop_Foliage'}, 'crop',
        'Vegetable rows (cabbage-like heads), 4 x 4 m tile, MI Cabbage', [1, 2, 3, 4, 5, 6], **crop)
    put('Crop_Vine', K.crop_vine, {'Structure': 'M_Prop_Wood', 'Roof': 'M_Prop_Foliage'}, 'crop',
        'Vineyard row on posts and wires, 4 m (rows 2 m apart)', [2, 3, 4, 5, 6], tile_length_m=4.0,
        placement='rows at 2 m spacing along local X; Sail age onward on warm slopes')
    put('Hay_Stack', K.hay_stack, {'Structure': 'M_Prop_Straw', 'Detail': 'M_Prop_Wood'}, 'hay',
        'Conical hay rick with pole and tie ropes', [0, 1, 2, 3], 'decimate', placement='farmyards and field corners, 1-4 together')
    put('Hay_Bales', K.hay_bales, {'Structure': 'M_Prop_Straw'}, 'hay', 'Three round bales', [4, 5, 6], 'decimate',
        placement='scatter on harvested fields in loose lines')
    put('Hay_Stook', K.hay_stook, {'Structure': 'M_Prop_Straw'}, 'hay', 'Stook of eight sheaves', [1, 2, 3, 4], 'decimate',
        placement='rows of stooks 4-6 m apart on harvested grain fields')
    # ---- storage, carts, market
    put('Barrel', K.barrel, {'Structure': 'M_Prop_Wood', 'Detail': 'M_Prop_Metal'}, 'storage', 'Coopered barrel',
        [2, 3, 4], 'decimate', placement='quays, market squares, inn yards')
    put('Barrel_Stack', K.barrel_stack, {'Structure': 'M_Prop_Wood', 'Detail': 'M_Prop_Metal'}, 'storage',
        'Three barrels stacked on their sides', [2, 3, 4], 'decimate', placement='quays and warehouses')
    put('Crate_Stack', K.crate_stack, WOOD, 'storage', 'Stack of five wooden crates', [2, 3, 4, 5], 'decimate',
        placement='quays, warehouses, market backs')
    put('Cart_Hand', K.cart_hand, {'Structure': 'M_Prop_Wood', 'Detail': 'M_Prop_Metal'}, 'cart',
        'Two-wheeled handcart, shafts resting on the ground', [1, 2, 3, 4], 'decimate', placement='lanes, farmyards, markets')
    put('Cart_Wagon', K.cart_wagon, {'Structure': 'M_Prop_Wood', 'Roof': 'M_Prop_Cloth', 'Detail': 'M_Prop_Metal'},
        'cart', 'Four-wheeled covered wagon (vehicle budget)', [2, 3, 4], 'decimate', cultures='any (cover MI per culture)',
        placement='roads, caravan camps, market edges',
        budget_note='a vehicle: the 1-4k triangle vehicle budget applies')
    for st in ('Intact', 'Damaged', 'Burnt'):
        nm = 'Market_Stall' + ('' if st == 'Intact' else '_' + st)
        put(nm, _st(K.market_stall, state=st), {'Structure': 'M_Prop_Wood', 'Roof': 'M_Prop_Cloth',
                                                 'Detail': 'M_Prop_Stone'},
            'market', 'Market stall with awning and goods' + ('' if st == 'Intact' else ' (%s)' % st.lower()),
            [1, 2, 3, 4], 'decimate', cultures='any (awning MI per culture: Valley, CliffTerrace, LagoonWeaver, RingBuilder)',
            group='Market_Stall', state=st, states_of=None if st == 'Intact' else 'Market_Stall',
            placement='rows around a market square, awning front (-Y) to the square')
    put('Goods_Sacks', K.goods_sacks, {'Structure': 'M_Prop_Cloth'}, 'goods', 'Pile of grain sacks (MI Burlap)',
        [1, 2, 3, 4, 5], 'decimate', placement='markets, quays, granaries, mills')
    put('Goods_Pots', K.goods_pots, {'Structure': 'M_Prop_Stone'}, 'goods', 'Amphorae and jars (MI Terracotta)',
        [1, 2, 3, 4], 'decimate', placement='markets and quays (Bronze to Sail)')
    put('Goods_Baskets', K.goods_baskets, {'Structure': 'M_Prop_Straw', 'Detail': 'M_Prop_Stone'}, 'goods',
        'Three wicker baskets of produce', [0, 1, 2, 3, 4], 'decimate', placement='markets, farm gates')
    # ---- signposts
    put('Sign_Cairn', K.sign_cairn, {'Structure': 'M_Prop_RockTile'}, 'sign', 'Waymark cairn', [0, 1],
        'decimate', placement='path forks and passes')
    put('Sign_Fingerpost', K.sign_fingerpost, WOOD, 'sign', 'Timber fingerpost (blank boards)', [1, 2, 3, 4],
        'decimate', placement='road junctions and town gates')
    put('Sign_Milestone', K.sign_milestone, {'Structure': 'M_Prop_Stone'}, 'sign', 'Stone milestone', [2, 3, 4],
        'decimate', placement='roadside, one per km')
    put('Sign_Street', K.sign_street, {'Structure': 'M_Prop_Steel'}, 'sign', 'Street sign pole (blank plates, MI PaintedGrey)',
        [4, 5, 6], 'decimate', placement='street corners')
    # ---- lamps, one per age; Detail = M_Prop_Glow (emissive), MI per light colour
    lamps = (
        (0, K.lamp_age0, {'Structure': 'M_Prop_Wood', 'Roof': 'M_Prop_Straw', 'Detail': 'M_Prop_Glow'}, 'Fire',
         'Pitch torch on a stake'),
        (1, K.lamp_age1, {'Structure': 'M_Prop_Wood', 'Roof': 'M_Prop_Stone', 'Detail': 'M_Prop_Glow'}, 'Fire',
         'Clay fire bowl on a lashed tripod'),
        (2, K.lamp_age2, {'Structure': 'M_Prop_Wood', 'Roof': 'M_Prop_Metal', 'Detail': 'M_Prop_Glow'}, 'Fire',
         'Iron cresset on a timber post'),
        (3, K.lamp_age3, {'Structure': 'M_Prop_Wood', 'Roof': 'M_Prop_Metal', 'Detail': 'M_Prop_Glow'}, 'Oil',
         'Oil lantern on a bracket post'),
        (4, K.lamp_age4, {'Structure': 'M_Prop_Metal', 'Detail': 'M_Prop_Glow'}, 'Gas',
         'Cast-iron gas lamp (MI PaintedGreen)'),
        (5, K.lamp_age5, {'Structure': 'M_Prop_Steel', 'Detail': 'M_Prop_Glow'}, 'Sodium',
         'Steel street light with sodium cobra head, 9 m (MI Galvanised)'),
        (6, K.lamp_age6, {'Structure': 'M_Prop_Steel', 'Detail': 'M_Prop_Glow'}, 'LED',
         'Curved white pole with an LED ring, 7 m (MI PaintedWhite)'))
    for age, fn, slots, glow, desc in lamps:
        put('Lamp_Age%d' % age, fn, slots, 'lamp', desc, [age], 'decimate',
            emissive={'slot': 'Detail', 'material': 'M_Prop_Glow', 'instance': glow,
                      'note': 'emissive at night only (scale by the night factor); the Detail slot is the light'},
            placement='along streets and plazas every 12-25 m (torches and braziers at doors and the hearth)')
    # ---- statues with Intact / Damaged / Ruin states
    statues = (
        ('Statue_Totem', K.statue_totem, WOOD, [0, 1], 'Carved timber totem with wing boards'),
        ('Statue_Idol', K.statue_idol, {'Structure': 'M_Prop_Stone'}, [1, 2], 'Seated stone idol on a plinth'),
        ('Statue_Figure', K.statue_figure, {'Structure': 'M_Prop_Stone'}, [2, 3, 4],
         'Robed figure with arms raised to the sky, on a stepped plinth (MI Marble)'),
        ('Statue_Obelisk', K.statue_obelisk, {'Structure': 'M_Prop_Stone'}, [3, 4, 5], 'Obelisk on a plinth, 9 m'),
        ('Statue_Monument', K.statue_monument, {'Structure': 'M_Prop_Steel', 'Roof': 'M_Prop_Stone'}, [5, 6],
         'Brushed-steel arc monument on a plaza disc (Stone MI Concrete)'))
    for nm, fn, slots, ages, desc in statues:
        for st in ('Intact', 'Damaged', 'Ruin'):
            full = nm + ('' if st == 'Intact' else '_' + st)
            put(full, _st(fn, state=st), slots, 'statue', desc + ('' if st == 'Intact' else ' (%s)' % st.lower()),
                ages, 'decimate', cultures='any (stone MI per culture)', group=nm, state=st,
                states_of=None if st == 'Intact' else nm,
                placement='plaza centres, temple approaches, crossroads; front faces -Y')
