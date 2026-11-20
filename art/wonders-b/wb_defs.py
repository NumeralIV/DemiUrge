# DEMIURGE art / wonders-b - shared definitions (asset table, texture atlas bands, palettes, culture variants).
#
# Every material slot of every wonder uses one 2048 x 2048 atlas made of FOUR horizontal bands
# (band k occupies UV v in [k/4, (k+1)/4]). Each band is a 2048 x 512 tile that repeats in u
# (UVs run past 0..1 in u on purpose) and is periodic inside its own height in v, so geometry
# maps one band period per strip of faces. A band covers U x V metres with U = 4 V, so texels are square.
#
# Texture maps per atlas:  _D  (sRGB base colour), _N (tangent normal, OpenGL +Y), _ORM (linear:
# R occlusion, G roughness, B metallic), _M (linear: R night emissive mask, G emissive colour select
# 0 = EmissiveA / 1 = EmissiveB, B culture accent mask).

FAMILY = 'Wonder'
SLOTS = ('Structure', 'Roof', 'Detail')

# asset key -> (task id, mesh token, display name, concept image)
ASSETS = {
    'choir':   ('wonder-4-industry',     'ChoirOfChimneys', 'The Choir of Chimneys', 'docs/concept/wonders/wonder-4-industry.png'),
    'ear':     ('wonder-5-atomic',       'EarOfTheSky',     'The Ear of the Sky',    'docs/concept/wonders/wonder-5-atomic.png'),
    'ladder':  ('wonder-6-space',        'Ladder',          'The Ladder',            'docs/concept/wonders/wonder-6-space.png'),
    'sunwell': ('wonder-sunwell',        'Sunwell',         'The Sunwell',           'docs/concept/landmarks/wonder-sunwell.png'),
    'stair':   ('wonder-stair-of-rains', 'StairOfRains',    'The Stair of Rains',    'docs/concept/landmarks/wonder-stair-of-rains.png'),
}
ORDER = ['choir', 'ear', 'ladder', 'sunwell', 'stair']
STATES = ('Intact', 'Construction', 'Damaged', 'Ruin')
CULTURES = ('A', 'B', 'C')

# ---------------------------------------------------------------------------------------------- bands
# (band name, V metres (band height); U = 4 V, generator kind, params)
BANDS = {
    'choir': {
        'Structure': [('brick', 3.0), ('brick_soot', 3.0), ('ashlar', 2.0), ('slate', 4.0)],
        'Roof':      [('dome_glass', 4.0), ('lantern_glass', 2.0), ('lead', 4.0), ('ironplate', 2.0)],
        'Detail':    [('brass', 1.0), ('castiron', 1.0), ('arch_windows', 8.0), ('setts', 4.0)],
    },
    'ear': {
        'Structure': [('concrete', 8.0), ('concrete_weathered', 8.0), ('concrete_smooth', 8.0), ('asphalt', 6.0)],
        'Roof':      [('dish_panels', 8.0), ('dish_panels_dark', 8.0), ('dish_rib', 2.0), ('grate', 2.0)],
        'Detail':    [('steel', 1.0), ('grass_berm', 8.0), ('office_windows', 8.0), ('aviation_red', 1.0)],
    },
    'ladder': {
        'Structure': [('composite', 6.0), ('glazing', 6.0), ('composite_smooth', 6.0), ('plaza_paving', 6.0)],
        'Roof':      [('garden_solar', 12.0), ('solar', 6.0), ('garden', 12.0), ('reflecting_pool', 6.0)],
        'Detail':    [('gold', 1.0), ('climber', 2.0), ('light_slit', 2.0), ('tether_core', 1.0)],
    },
    'sunwell': {
        'Structure': [('white_stone', 4.0), ('riser_gold', 1.4), ('balustrade', 1.2), ('avenue_paving', 6.0)],
        'Roof':      [('pool_water', 8.0), ('gold_leaf', 1.0), ('planter_grass', 4.0), ('earth_cut', 6.0)],
        'Detail':    [('bronze', 1.0), ('lamp', 1.0), ('beam_core', 2.0), ('mirror', 2.0)],
    },
    'stair': {
        'Structure': [('honey_stone', 4.0), ('honey_vines', 4.0), ('rock', 8.0), ('marble', 2.0)],
        'Roof':      [('garden_terrace', 8.0), ('blossom_pink', 4.0), ('blossom_lilac', 4.0), ('foliage', 4.0)],
        'Detail':    [('waterfall', 2.0), ('pool_water', 4.0), ('lattice', 2.0), ('lantern', 1.0)],
    },
}


def band_index(asset, slot, name):
    for i, (n, _v) in enumerate(BANDS[asset][slot]):
        if n == name:
            return i
    raise KeyError((asset, slot, name))


def band_dims(asset, slot, name):
    for n, v in BANDS[asset][slot]:
        if n == name:
            return 4.0 * v, v
    raise KeyError((asset, slot, name))


# ---------------------------------------------------------------------------------------------- materials
# per-slot material parameters (what the UE master material instance needs): emissive colours, day/night
# emissive strength (multiplies M.R), base roughness scale.
MATPARAMS = {
    'choir': {
        'Structure': dict(EmissiveA='#ffc766', EmissiveB='#ff9a40', EmissiveDay=0.0, EmissiveNight=0.0),
        'Roof':      dict(EmissiveA='#ffcf88', EmissiveB='#ffc766', EmissiveDay=0.45, EmissiveNight=9.0),
        'Detail':    dict(EmissiveA='#ffc766', EmissiveB='#ffb050', EmissiveDay=0.5, EmissiveNight=7.0),
    },
    'ear': {
        'Structure': dict(EmissiveA='#ffd9a0', EmissiveB='#ff3a2a', EmissiveDay=0.0, EmissiveNight=0.0),
        'Roof':      dict(EmissiveA='#ffd9a0', EmissiveB='#ff3a2a', EmissiveDay=0.0, EmissiveNight=0.0),
        'Detail':    dict(EmissiveA='#ffd9a0', EmissiveB='#ff3a2a', EmissiveDay=2.0, EmissiveNight=25.0,
                          EmissiveBBlink='1 s on / 1 s off, phase by pylon socket (0, 0.667, 1.334 s: one third of the 2 s period each, as in the lights list)'),
    },
    'ladder': {
        'Structure': dict(EmissiveA='#fff0d1', EmissiveB='#e8f0ff', EmissiveDay=0.0, EmissiveNight=6.0),
        'Roof':      dict(EmissiveA='#fff0d1', EmissiveB='#e8f0ff', EmissiveDay=0.0, EmissiveNight=0.0),
        'Detail':    dict(EmissiveA='#fffaf0', EmissiveB='#e8f0ff', EmissiveDay=12.0, EmissiveNight=14.0),
    },
    'sunwell': {
        'Structure': dict(EmissiveA='#ffcf80', EmissiveB='#ffe8b0', EmissiveDay=0.0, EmissiveNight=3.0),
        'Roof':      dict(EmissiveA='#ffcf80', EmissiveB='#7fd0e0', EmissiveDay=0.0, EmissiveNight=1.5),
        'Detail':    dict(EmissiveA='#fff1c8', EmissiveB='#ffcf80', EmissiveDay=6.0, EmissiveNight=10.0),
    },
    'stair': {
        'Structure': dict(EmissiveA='#ffc77a', EmissiveB='#7fe0e0', EmissiveDay=0.0, EmissiveNight=2.0),
        'Roof':      dict(EmissiveA='#ffc77a', EmissiveB='#7fe0e0', EmissiveDay=0.0, EmissiveNight=0.0),
        'Detail':    dict(EmissiveA='#ffc77a', EmissiveB='#7fe0e0', EmissiveDay=0.25, EmissiveNight=5.0),
    },
}

# culture material instances: BaseTint multiplies the whole albedo, AccentTint multiplies where M.B = 1,
# RoughnessScale multiplies ORM.G. Hex colours are sRGB multipliers.
CULTURE_MI = {
    'A': dict(label='Cliff-Terrace (warm rose-ochre)', BaseTint='#fff1e6', AccentTint='#f0b890', RoughnessScale=1.0),
    'B': dict(label='Lagoon-Weaver (silver and verdigris)', BaseTint='#eef3f2', AccentTint='#86c0ae', RoughnessScale=0.9),
    'C': dict(label='Ring-Builder (cool slate and pewter)', BaseTint='#e9edf3', AccentTint='#98a2b4', RoughnessScale=1.1),
}

LICENCE = 'original'
