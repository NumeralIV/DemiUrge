# DEMIURGE tree families: the one entry point. Regenerates every tree asset from code, deterministically.
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/trees/build_trees.py -- [options]
#
# options:
#   --stages bark,tiles,trees,atlas   (default: all, in that order; trees needs the atlas PNG of the tiles stage)
#   --species broadleaf,conifer,...   (default: all five)
#   --variants 0,1                    (default: all)
#
# Writes (all under art/trees/):
#   textures/T_Bark_<Species>.png, T_Bark_<Species>_N.png, T_TreeLeafAtlas.png, T_TreeImpostors.png
#   export/<Species>/SM_Tree_<Species>_<v>.fbx (LOD0) and SM_Tree_<Species>_<v>_LOD1..3.fbx
#   source/<species>.blend (all variants, all LODs, bake materials)
#   export/manifest.json (sizes, triangle counts, impostor cells, screen sizes, import rules)
import os
import sys
import time
import json

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import importlib
import treelib.imaging, treelib.bark, treelib.render, treelib.tiles, treelib.mesh, treelib.species, treelib.export   # noqa
for m in (treelib.imaging, treelib.bark, treelib.render, treelib.tiles, treelib.mesh, treelib.species, treelib.export):
    importlib.reload(m)
from treelib import bark, tiles, export


def log(msg):
    print('TREES ' + msg, flush=True)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts = {'stages': 'bark,tiles,trees,atlas', 'species': ','.join(export.SPECIES), 'variants': ''}
    for i, a in enumerate(argv):
        if a.startswith('--') and i + 1 < len(argv):
            opts[a[2:]] = argv[i + 1]
    stages = opts['stages'].split(',')
    species = [s for s in opts['species'].split(',') if s]
    variants = [int(v) for v in opts['variants'].split(',') if v != '']
    tex_dir = os.path.join(HERE, 'textures')
    t0 = time.time()
    if 'bark' in stages:
        bark.write_all(tex_dir, log)
        log('bark done %.1fs' % (time.time() - t0))
    if 'tiles' in stages:
        path, cover = tiles.bake(tex_dir, log)
        log('tiles done %.1fs -> %s' % (time.time() - t0, path))
    if 'trees' in stages:
        export.build_species(HERE, species, variants, log)
        log('trees done %.1fs' % (time.time() - t0))
    if 'atlas' in stages:
        export.pack_impostors(HERE, log)
        log('impostor atlas done %.1fs' % (time.time() - t0))
    log('ALL DONE %.1fs' % (time.time() - t0))


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    print('TREES FAILED', flush=True)
    sys.exit(1)
