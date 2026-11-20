# DEMIURGE art - Age4 asset registry: which generator makes which mesh, slots, budgets, states, sim mapping.
import a4_industry as I
import a4_terrace

STATES = ['Intact', 'Construction', 'Damaged', 'Ruin']
BRICK = ['M_Age4_Brick', 'M_Age4_Slate', 'M_Age4_Trim']

ORNAMENT_INFO = {
    'description': 'Culture ornament meshes snap to SOCKET_Ornament_* sockets (chimney tops, gable apexes, the station spire).',
    'by_culture': {'A': 'SM_Age4_Ornament_A (domed clay chimney cap)', 'B': 'SM_Age4_Ornament_B (upswept timber gable horns)',
                   'C': 'SM_Age4_Ornament_C (conical stone finial)', 'Base': None},
    'material': 'M_Age4_Trim (single Detail slot) or its culture instance',
}


def _terrace(v):
    p = a4_terrace.VARIANTS[v]
    return {
        'mesh': 'SM_Age4_Terrace_%s' % v, 'gen': (lambda lod, v=v: a4_terrace.terrace(v, lod)), 'slots': BRICK,
        'states': STATES, 'class': 'house', 'budget': [2000, 6000], 'seed': p['seed'], 'lod1_grid': 'S',
        'meta': {'sim': {'building': 'house', 'age': 'Industry', 'tier': 'terrace row (%d dwellings, %d storeys)' % (p['units'], p['storeys']),
                         'towns_kit_id': 'Rowhouse', 'kit_variant': 'ABCD'.index(v)},
                 'dwellings': p['units'], 'storeys': p['storeys'],
                 'notes': 'Plot = front steps + main block + rear outshots + walled yards with privies. Rows butt end to end along X '
                          '(the end walls are party walls); back-to-back rows meet at the yard walls.'},
    }


def registry():
    reg = {}
    for v in 'ABCD':
        reg['Terrace_%s' % v] = _terrace(v)
    reg['Factory_A'] = {
        'mesh': 'SM_Age4_Factory_A', 'gen': (lambda lod: I.factory('A', lod)), 'slots': BRICK, 'states': STATES,
        'class': 'civic', 'budget': [8000, 20000], 'seed': 41,
        'meta': {'sim': {'building': 'b.factory', 'age': 'Industry', 'tier': 'large mill', 'towns_kit_id': 'Factory', 'kit_variant': 2},
                 'notes': 'Two-storey sawtooth mill, engine house, office, three stacks (the tallest elements, 46-58 m), water tower. '
                          'SOCKET_Smoke_* on every stack for the smoke FX.'}}
    reg['Factory_B'] = {
        'mesh': 'SM_Age4_Factory_B', 'gen': (lambda lod: I.factory('B', lod)), 'slots': BRICK, 'states': STATES,
        'class': 'civic', 'budget': [8000, 20000], 'seed': 43,
        'meta': {'sim': {'building': 'b.factory', 'age': 'Industry', 'tier': 'small works', 'towns_kit_id': 'Factory', 'kit_variant': 0},
                 'notes': 'Single-storey sawtooth shed, boiler house, one 44 m stack.'}}
    reg['Stack_A'] = {
        'mesh': 'SM_Age4_Stack_A', 'gen': I.stack, 'slots': BRICK, 'states': STATES, 'class': 'prop-tall', 'budget': [300, 1500],
        'seed': 31,
        'meta': {'sim': {'building': 'factory chimney (stand-alone)', 'age': 'Industry', 'towns_kit_id': 'Stack', 'kit_variant': 1},
                 'notes': '52 m tapered round brick chimney on a square plinth; SOCKET_Smoke_0 at the top.'}}
    reg['Warehouse_A'] = {
        'mesh': 'SM_Age4_Warehouse_A', 'gen': (lambda lod: I.warehouse('A', lod)), 'slots': BRICK, 'states': STATES,
        'class': 'civic', 'budget': [4000, 14000], 'seed': 51,
        'meta': {'sim': {'building': 'warehouse / dock store', 'age': 'Industry', 'towns_kit_id': 'Warehouse', 'kit_variant': 1},
                 'notes': 'Five-storey dock warehouse, double-pile roof, loading doors stacked under a projecting cathead with hoist.'}}
    reg['Warehouse_B'] = {
        'mesh': 'SM_Age4_Warehouse_B', 'gen': (lambda lod: I.warehouse('B', lod)), 'slots': BRICK, 'states': STATES,
        'class': 'civic', 'budget': [4000, 14000], 'seed': 53,
        'meta': {'sim': {'building': 'goods shed', 'age': 'Industry', 'towns_kit_id': 'Warehouse', 'kit_variant': 2},
                 'notes': 'Single-storey goods shed with rooflights, raised loading platform and canopy, siding track (rail top at Z = 0).'}}
    reg['Station_A'] = {
        'mesh': 'SM_Age4_Station_A', 'gen': I.station, 'slots': BRICK, 'states': STATES, 'class': 'civic', 'budget': [8000, 20000],
        'seed': 61,
        'meta': {'sim': {'building': 'rail station', 'age': 'Industry', 'tier': 'through station', 'towns_kit_id': None},
                 'notes': 'Glass-and-iron barrel-vault train shed (32 m span) over four tracks and three platforms, front building with a '
                          'clock tower and end pavilions. Tracks run along X and leave the footprint at both ends (rail top Z = 0, '
                          'centre lines at local Y = 0.75, 9.25, 12.75, 21.25 before centring; see sockets-free track ends in the .blend).'}}
    reg['MineHead_A'] = {
        'mesh': 'SM_Age4_MineHead_A', 'gen': I.minehead, 'slots': BRICK, 'states': STATES, 'class': 'civic', 'budget': [8000, 20000],
        'seed': 71, 'hooks': {'Ruin': I._minehead_ruin},
        'meta': {'sim': {'building': 'b.mine', 'age': 'Industry', 'tier': 'deep shaft colliery', 'towns_kit_id': 'MineHead', 'kit_variant': 1},
                 'notes': 'Steel lattice headframe with twin sheave wheels and back-stays, heapstead, winding engine house, boiler house and '
                          '34 m stack, ore bin. Pair with SM_Age4_Spoil_A.'}}
    for v, (dim, notes) in {'A': ('44 x 34 m, 15 m high', 'colliery tip with tipping track'), 'B': ('14 x 11 m, 4 m high', 'yard heap')}.items():
        reg['Spoil_%s' % v] = {
            'mesh': 'SM_Age4_Spoil_%s' % v, 'gen': (lambda lod, v=v: I.spoil(v, lod)),
            'slots': ['M_Age4_Gravel', 'M_Age4_Gravel', 'M_Age4_Trim'], 'states': ['Intact'], 'class': 'prop' if v == 'B' else 'landmark-prop',
            'budget': [50, 1000] if v == 'B' else [300, 3000], 'seed': 81,
            'meta': {'sim': {'building': 'spoil / coal / ore heap', 'age': 'Industry', 'towns_kit_id': 'Spoil'},
                     'material_variants': {'spoil': 'MI_Age4_Gravel_<Culture>', 'coal': 'MI_Age4_Gravel_Coal', 'ore': 'MI_Age4_Gravel_Ore'},
                     'notes': '%s %s. Roof slot unused (same material as Structure).' % (dim, notes)}}
    reg['Bridge_A'] = {
        'mesh': 'SM_Age4_Bridge_A', 'gen': I.truss_bridge, 'slots': ['M_Age4_Stone', 'M_Age4_Gravel', 'M_Age4_Trim'],
        'states': STATES, 'class': 'civic', 'budget': [3000, 15000], 'seed': 91,
        'hooks': {'custom_Construction': I._bridge_a_construction, 'custom_Ruin': I._bridge_a_ruin},
        'meta': {'sim': {'building': 'rail bridge segment', 'age': 'Industry', 'towns_kit_id': None},
                 'pivot': 'deck centre at rail level; piers reach 16 m below the rails',
                 'modular': 'segment pitch 43 m along X (40 m Pratt through-truss + a half pier at each end); chain copies at X + 43 m',
                 'notes': 'Double-track iron through-truss; Ruin = span broken off one pier and tilted into the river.'}}
    reg['Bridge_B'] = {
        'mesh': 'SM_Age4_Bridge_B', 'gen': I.viaduct, 'slots': ['M_Age4_Brick', 'M_Age4_Gravel', 'M_Age4_Trim'],
        'states': STATES, 'class': 'civic', 'budget': [2000, 12000], 'seed': 93,
        'hooks': {'custom_Construction': I._viaduct_construction, 'custom_Ruin': I._viaduct_ruin},
        'meta': {'sim': {'building': 'rail viaduct segment', 'age': 'Industry', 'towns_kit_id': None},
                 'pivot': 'deck centre at rail level; piers reach 16 m below the rails',
                 'modular': 'segment pitch 36 m along X (three 9.6 m brick arches, half piers at the ends)',
                 'notes': 'Brick arch viaduct for the approaches of the truss bridge or valley crossings.'}}
    for c in 'ABC':
        reg['Ornament_%s' % c] = {
            'mesh': 'SM_Age4_Ornament_%s' % c, 'gen': (lambda lod, c=c: I.ornament(c, lod)),
            'slots': BRICK, 'states': ['Intact'], 'class': 'prop', 'budget': [50, 1000], 'seed': 1,
            'meta': {'sim': {'building': 'culture ornament', 'culture': c},
                     'notes': ORNAMENT_INFO['by_culture'][c] + '; snaps to SOCKET_Ornament_* (pivot at its base).'}}
    return reg
