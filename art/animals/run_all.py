"""DEMIURGE art/animals/run_all.py - rebuild the whole wildlife family (plain Python 3, no Blender import).

    python art/animals/run_all.py                 build every asset, render every preview, write manifest.json
    python art/animals/run_all.py build [keys]    only build (comma-separated keys, default all)
    python art/animals/run_all.py preview [keys] [views]
    python art/animals/run_all.py manifest

Every Blender run goes through tools/render_slot.py (at most 3 heavy jobs machine-wide), several at once.
"""
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BLENDER = 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
SLOT = os.path.join(ROOT, 'tools', 'render_slot.py')
KEYS = ['grazer-wildebeest', 'grazer-antelope', 'grazer-bison', 'deer-stag', 'deer-doe', 'bigcat-lioness', 'bigcat-lion',
        'bird-starling', 'bird-egret', 'whale-humpback', 'fish-sardine']
VIEWS = 'close,40m,300m,1km,anim'


def blender(script, args, tag):
    cmd = [sys.executable, SLOT, '--', BLENDER, '-b', '--python-exit-code', '1', '--python', os.path.join(HERE, script), '--'] + args
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors='replace')
    lines = [l for l in p.stdout.splitlines() + p.stderr.splitlines() if l.startswith('[') or 'Error' in l or 'Traceback' in l]
    print('== %s (exit %d)\n%s' % (tag, p.returncode, '\n'.join(lines[-12:])), flush=True)
    return p.returncode


def build(keys, jobs=4):
    with ThreadPoolExecutor(jobs) as ex:
        return list(ex.map(lambda k: blender('build_animals.py', ['--asset', k], 'build ' + k), keys))


def preview(keys, views=VIEWS, jobs=4):
    with ThreadPoolExecutor(jobs) as ex:
        return list(ex.map(lambda k: blender('preview_animals.py', ['--asset', k, '--views', views], 'preview ' + k), keys))


def manifest():
    src = os.path.join(HERE, 'source')
    meshes = {}
    tri = {}
    for k in KEYS:
        p = os.path.join(src, k + '.json')
        if not os.path.exists(p):
            continue
        info = json.load(open(p))
        name = info.pop('mesh')
        info.pop('build_seconds', None)
        info['states'] = []
        info['culture_variants'] = []
        info['licence'] = 'original'
        info['script'] = 'art/animals/build_animals.py (species in art/animals/species_animals.py)'
        info['previews'] = ['shots/asset-animals-%s-%s.png' % (k, v) for v in VIEWS.split(',')]
        meshes[name] = info
        tri[name] = info['triangles_per_lod']
    doc = {
        'family': 'Animal',
        'folder': 'art/animals',
        'units': 'metres, Z up, front -Y (Unreal +X after the standard FBX import); FBX written with FBX_SCALE_ALL',
        'pivot': 'walkers: centre of the footprint at ground level; birds and fish: body centre; whale: sea surface (body axis 1.2 m below)',
        'lod_screen_sizes': [1.0, 0.35, 0.12, 0.04],
        'material_slots': {'Structure': 'coat (one slot per animal; D/N/ORM baked per asset)'},
        'master_material': 'M_Animal_Coat (to author in Unreal): D * tint, ORM (R AO, G roughness, B metallic), N tangent OpenGL (+Y, flip green on import), optional sheen; MI per asset and biome',
        'game_material_hooks': {
            'vertex_colour': 'coat colour (sRGB) sampled from D, so M_NatureCreature (vertex colour x tint) works unchanged',
            'uv1_x': 'AnimWeights: grazer/deer/cat = head weight (grazing bob), bird = wing weight (flap), fish/whale = tail weight (beat), as DmNatureCritters.h expects',
            'note': 'the critter stand-ins face +X (grazer) / +Y (bird, fish) in Unreal; these assets face +X after import, so birds and fish need a 90 degree yaw in the critter factory',
        },
        'culture_variants': 'none: wildlife carries no culture colour (docs/concept/units/index.md); biome coat variants are material-instance tints listed per mesh',
        'states': 'none: animals are not damageable buildings',
        'licence': 'original (procedural, made by script; no external inputs)',
        'meshes': meshes,
        'triangle_totals': {'lod0': sum(v[0] for v in tri.values()), 'all_lods': sum(sum(v) for v in tri.values())},
    }
    with open(os.path.join(HERE, 'manifest.json'), 'w') as f:
        json.dump(doc, f, indent=1)
    print('manifest: %d meshes, LOD0 total %d tris' % (len(meshes), doc['triangle_totals']['lod0']))
    return doc


def main():
    a = sys.argv[1:]
    cmd = a[0] if a else 'all'
    keys = a[1].split(',') if len(a) > 1 and a[1] != 'all' else KEYS
    if cmd in ('build', 'all'):
        build(keys)
    if cmd in ('preview', 'all'):
        preview(keys, a[2] if len(a) > 2 else VIEWS)
        if cmd == 'all':
            blender('preview_animals.py', ['--asset', 'lineup'], 'lineup')
    if cmd in ('manifest', 'all', 'build'):
        manifest()


if __name__ == '__main__':
    main()
