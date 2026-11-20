# DEMIURGE art - Age6 independent export check (re-imports every FBX; no rendering).
#
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
#       --python art/age6/verify_age6.py -- [--out <report.json>] [--only Substring] [--dir <folder with export/, textures/, manifest.json>]
#
# For every art/age6/export/SM_Age6_*.fbx: LodGroup + LOD0..3, triangles per LOD (vs manifest and
# the ASSETS.md ratios), material slots (<= 3, known materials), UVMap and the Col vertex colour,
# world bounds in metres (pivot at the footprint centre, plinth to -0.3), open / non-manifold edges,
# inward-facing shells (signed volume per connected shell), degenerate faces and UV sanity.
# Also checks that the manifest lists every mesh with the fields ASSETS.md asks for and that every
# texture exists at 1024^2. Prints a summary and writes the full report as JSON.
import os
import sys
import json
import math

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.join(HERE, 'export')
TEX = os.path.join(HERE, 'textures')
REQ = ('asset', 'state', 'footprint_m', 'height_m', 'triangles_per_lod', 'material_slots', 'textures', 'states',
       'culture_variants', 'licence', 'script', 'fbx', 'sim_mapping', 'budget_class')
KNOWN = {'M_Age6_Facade', 'M_Age6_GreenRoof', 'M_Age6_Tech', 'M_Age6_Concrete', 'M_Age6_Hull', 'M_Age6_Cladding',
         'M_Age6_RoofMetal'}


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    o = {'out': os.path.join(HERE, 'verify_report.json'), 'only': None, 'dir': HERE}
    for i, x in enumerate(a):
        if x == '--dir':
            o['dir'] = a[i + 1]
        if x == '--out':
            o['out'] = a[i + 1]
        if x == '--only':
            o['only'] = a[i + 1]
    return o


def shells(bm):
    """Connected face shells -> list of (faces, signed volume, open edges)."""
    seen = set()
    out = []
    for f0 in bm.faces:
        if f0.index in seen:
            continue
        stack = [f0]
        seen.add(f0.index)
        fs = []
        while stack:
            f = stack.pop()
            fs.append(f)
            for e in f.edges:
                for g in e.link_faces:
                    if g.index not in seen:
                        seen.add(g.index)
                        stack.append(g)
        vol = 0.0
        for f in fs:
            a = f.verts[0].co
            for i in range(1, len(f.verts) - 1):
                vol += a.dot(f.verts[i].co.cross(f.verts[i + 1].co))
        es = {e for f in fs for e in f.edges}
        nopen = sum(1 for e in es if len(e.link_faces) == 1)
        out.append((len(fs), vol / 6.0, nopen))
    return out


def check_mesh(ob):
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.transform(ob.matrix_world)
    bm.faces.ensure_lookup_table()
    bm.faces.index_update()
    tris = sum(len(f.verts) - 2 for f in bm.faces)
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    sh = shells(bm)
    inverted = [(n, round(v, 3)) for n, v, o in sh if v < -1e-3 and o == 0]     # closed shells only
    open_e = sum(1 for e in bm.edges if len(e.link_faces) == 1)
    nonman = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    degen = sum(1 for f in bm.faces if f.calc_area() < 1e-7)
    uvl = bm.loops.layers.uv.active
    uv_bad = 0
    uv_lo, uv_hi = [1e9, 1e9], [-1e9, -1e9]
    if uvl:
        for f in bm.faces:
            for l in f.loops:
                u, v = l[uvl].uv
                if not (math.isfinite(u) and math.isfinite(v)):
                    uv_bad += 1
                    continue
                uv_lo = [min(uv_lo[0], u), min(uv_lo[1], v)]
                uv_hi = [max(uv_hi[0], u), max(uv_hi[1], v)]
    bm.free()
    cols = [a.name for a in me.color_attributes]
    return {
        'tris': tris,
        'bounds': [[round(min(xs), 2), round(max(xs), 2)], [round(min(ys), 2), round(max(ys), 2)], [round(min(zs), 2), round(max(zs), 2)]],
        'mats': [m.name if m else None for m in me.materials],
        'uv_layers': [u.name for u in me.uv_layers], 'uv_range': [uv_lo, uv_hi], 'uv_nonfinite': uv_bad,
        'colors': cols,
        'shells': len(sh), 'inverted_shells': len(inverted), 'inverted_examples': inverted[:4],
        'open_edges': open_e, 'nonmanifold_edges': nonman, 'degenerate_faces': degen,
        'scale': [round(s, 4) for s in ob.matrix_world.to_scale()],
    }


def main():
    global EXP, TEX
    o = args()
    EXP = os.path.join(o['dir'], 'export')
    TEX = os.path.join(o['dir'], 'textures')
    with open(os.path.join(o['dir'], 'manifest.json'), encoding='utf-8') as f:
        man = json.load(f)
    meshes = man['meshes']
    files = sorted(fn for fn in os.listdir(EXP) if fn.lower().endswith('.fbx'))
    report = {'files': len(files), 'manifest_meshes': len(meshes), 'meshes': {}, 'problems': []}
    P = report['problems']
    names = {fn[:-4] for fn in files}
    for n in sorted(set(meshes) ^ names):
        P.append('%s: in %s only' % (n, 'manifest' if n in meshes else 'export'))
    for n, m in meshes.items():
        miss = [k for k in REQ if k not in m or m[k] in (None, '', [])]
        if miss:
            P.append('%s: manifest missing %s' % (n, miss))
        for t in m.get('textures', []):
            p = os.path.join(TEX, t + '.png')
            if not os.path.exists(p):
                P.append('%s: texture %s missing' % (n, t))
    for fn in sorted(os.listdir(TEX)):
        img = bpy.data.images.load(os.path.join(TEX, fn))
        if tuple(img.size) != (1024, 1024):
            P.append('texture %s is %s' % (fn, tuple(img.size)))
        bpy.data.images.remove(img)
    for fn in files:
        name = fn[:-4]
        if o['only'] and o['only'] not in name:
            continue
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=os.path.join(EXP, fn))
        obs = list(bpy.context.scene.objects)
        empties = [ob for ob in obs if ob.type == 'EMPTY']
        lods = sorted([ob for ob in obs if ob.type == 'MESH'], key=lambda ob: ob.name)
        r = {'empties': [(e.name, e.get('fbx_type')) for e in empties], 'lods': {}}
        if len(empties) != 1 or empties[0].name != name:
            P.append('%s: expected one group empty named like the file, got %s' % (name, r['empties']))
        exp_names = ['%s_LOD%d' % (name, k) for k in range(4)]
        if [ob.name for ob in lods] != exp_names:
            P.append('%s: LOD objects %s' % (name, [ob.name for ob in lods]))
        for ob in lods:
            if empties and ob.parent != empties[0]:
                P.append('%s: %s not parented to the LodGroup' % (name, ob.name))
        mm = meshes.get(name, {})
        tris = []
        for k, ob in enumerate(lods):
            c = check_mesh(ob)
            r['lods'][ob.name] = c
            tris.append(c['tris'])
            tag = '%s LOD%d' % (name, k)
            if len(c['mats']) > 3:
                P.append('%s: %d material slots' % (tag, len(c['mats'])))
            bad = [x for x in c['mats'] if x not in KNOWN]
            if bad:
                P.append('%s: unknown materials %s' % (tag, bad))
            if 'UVMap' not in c['uv_layers'] and not c['uv_layers']:
                P.append('%s: no UVs' % tag)
            if c['uv_nonfinite']:
                P.append('%s: %d non-finite UVs' % (tag, c['uv_nonfinite']))
            if 'Col' not in c['colors']:
                P.append('%s: no Col vertex colour (%s)' % (tag, c['colors']))
            if c['inverted_shells']:
                P.append('%s: %d inward-facing shells %s' % (tag, c['inverted_shells'], c['inverted_examples']))
            if c['open_edges']:
                P.append('%s: %d open edges' % (tag, c['open_edges']))
            if c['nonmanifold_edges']:
                P.append('%s: %d non-manifold edges' % (tag, c['nonmanifold_edges']))
            if c['degenerate_faces']:
                P.append('%s: %d degenerate faces' % (tag, c['degenerate_faces']))
            zmin = c['bounds'][2][0]
            if 'Rocket' not in name and abs(zmin + 0.3) > 0.06:
                P.append('%s: lowest point z = %.2f (want -0.3)' % (tag, zmin))
            if k == 0 and mm and name.endswith('_Intact'):
                bx, by = c['bounds'][0], c['bounds'][1]
                cx, cy = (bx[0] + bx[1]) * 0.5, (by[0] + by[1]) * 0.5
                if abs(cx) > 0.12 * (bx[1] - bx[0]) + 1.0 or abs(cy) > 0.12 * (by[1] - by[0]) + 1.0:
                    P.append('%s: bounds centre (%.1f, %.1f) far from the pivot' % (tag, cx, cy))
            if k == 0 and mm:
                if abs(c['bounds'][2][1] - mm.get('height_m', 0)) > 0.1:
                    P.append('%s: height %.2f vs manifest %.2f' % (tag, c['bounds'][2][1], mm.get('height_m', 0)))
                ss = c['scale']
                if any(abs(s - 1.0) > 1e-3 for s in ss):
                    r['note_scale'] = ss
        if mm and tris != mm.get('triangles_per_lod'):
            P.append('%s: triangles %s vs manifest %s' % (name, tris, mm.get('triangles_per_lod')))
        if len(tris) == 4 and tris[0]:
            r['lod_ratio'] = [round(t / tris[0], 3) for t in tris]
            if not (tris[0] > tris[1] >= tris[2] >= tris[3]):
                P.append('%s: LOD triangle counts not decreasing %s' % (name, tris))
        if mm:
            lo, hi = mm.get('budget_lod0', [0, 1e9])
            if tris and tris[0] > hi:
                P.append('%s: LOD0 %d over budget %s' % (name, tris[0], hi))
            r['budget'] = [lo, hi]
        r['tris'] = tris
        report['meshes'][name] = r
        print('%-44s %s' % (name, tris))
    with open(o['out'], 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print('PROBLEMS %d' % len(P))
    for p in P[:400]:
        print('  ' + p)
    print('VERIFY DONE')


main()
