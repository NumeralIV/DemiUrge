# DEMIURGE art - Age2 export checks (headless):
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age2/check_age2.py
# Re-imports every FBX in art/age2/export and checks it against manifest.json: LodGroup node with
# _LOD0.._LODn children, triangle counts, pivot at the footprint centre on the ground, foundation
# below Z=0, LOD3 present over 10 m, LOD0 budget per kind, material slots <= 3, UV and colour layers.
# Verifier additions (2026-11-19): the same 3 slots (Structure, Roof, Detail) on every LOD, and on intact
# buildings and wall pieces LOD1 and LOD2 keep at least 80 % of the LOD0 top and height (catches a decimation that
# removed a part's body), and intact LOD0 carries faces on Detail (and Roof for houses and civic buildings).
# Prints one line per mesh and a summary; exit code 1 on any failure.
import os
import sys
import json
import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.join(HERE, 'export')
BUDGET = {'house': (2000, 6000), 'civic': (8000, 20000), 'wall': (0, 8000), 'prop': (50, 1000)}


def main():
    mf = json.load(open(os.path.join(HERE, 'manifest.json'), encoding='utf-8'))
    fails, warns = [], []
    for name, m in sorted(mf['meshes'].items()):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        path = os.path.join(HERE, '..', '..', m['fbx'])
        bpy.ops.import_scene.fbx(filepath=path)
        grp = bpy.data.objects.get(name)
        lods = sorted([o for o in bpy.data.objects if o.type == 'MESH'], key=lambda o: o.name)
        errs = []
        if grp is None:
            errs.append('no group node')
        if len(lods) != len(m['triangles']):
            errs.append('LOD count %d != %d' % (len(lods), len(m['triangles'])))
        tris = []
        want = list(m['materials'].values())
        h0 = None
        for o in lods:
            me = o.data
            me.calc_loop_triangles()
            tris.append(len(me.loop_triangles))
            if len(me.materials) > 3:
                errs.append('%s has %d slots' % (o.name, len(me.materials)))
            # verification additions: fixed slot order on every LOD, and no LOD that lost its body
            got = [mt.name if mt else None for mt in me.materials]
            if got != want:
                errs.append('%s slots %s != %s' % (o.name, got, want))
            if me.vertices and m['state'] == 'Intact' and m['kind'] != 'prop':
                zz = [(o.matrix_world @ v.co).z for v in me.vertices]
                h = (max(zz), max(zz) - min(zz))
                if h0 is None:
                    h0 = h
                elif o.name[-1] in '12' and (h[0] < 0.8 * h0[0] or h[1] < 0.8 * h0[1]):
                    errs.append('%s top %.1f m / height %.1f m < 80%% of LOD0 (%.1f / %.1f)' % (o.name, h[0], h[1], h0[0], h0[1]))
            if not me.uv_layers:
                errs.append('%s no UVs' % o.name)
            if not me.color_attributes:
                errs.append('%s no colour attribute' % o.name)
        if lods and m['state'] == 'Intact':
            used = {p.material_index for p in lods[0].data.polygons}
            need = {2} | ({1} if m['kind'] in ('house', 'civic') else set())
            if not need <= used:
                errs.append('LOD0 has no faces on slot(s) %s' % sorted(need - used))
            if sorted(used) != [['Structure', 'Roof', 'Detail'].index(x) for x in m.get('slots_with_faces', [])]:
                errs.append('used slots %s != manifest slots_with_faces %s' % (sorted(used), m.get('slots_with_faces')))
        if tris != m['triangles']:
            errs.append('tris %s != manifest %s' % (tris, m['triangles']))
        if lods:
            o = lods[0]
            ws = [o.matrix_world @ v.co for v in o.data.vertices]
            xs = [v.x for v in ws]
            ys = [v.y for v in ws]
            zs = [v.z for v in ws]
            cx = (min(xs) + max(xs)) / 2
            cy = (min(ys) + max(ys)) / 2
            span = max(max(xs) - min(xs), max(ys) - min(ys))
            if m['kind'] != 'prop' and min(zs) > -0.29:
                errs.append('foundation only reaches %.2f' % min(zs))
            if abs(cx) > 0.2 * span + 0.5 or abs(cy) > 0.2 * span + 0.5:
                warns.append('%s pivot offset from bounds centre (%.1f, %.1f) - dressing outside the footprint' % (name, cx, cy))
            if span > 10.0 and len(lods) < 4:
                errs.append('no LOD3 although %.1f m' % span)
        lo, hi = BUDGET[m['kind']]
        if m['state'] == 'Intact' and not (lo <= m['triangles'][0] <= hi):
            warns.append('%s LOD0 %d outside %s budget %d-%d' % (name, m['triangles'][0], m['kind'], lo, hi))
        status = 'OK  ' if not errs else 'FAIL'
        print('%s %-36s lods %d tris %s %s' % (status, name, len(lods), tris, '; '.join(errs)))
        if errs:
            fails.append(name)
    print('---')
    for w in warns:
        print('WARN', w)
    print('checked %d meshes, %d failed' % (len(mf['meshes']), len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    rc = main()
    sys.stdout.flush()
    os._exit(rc)
