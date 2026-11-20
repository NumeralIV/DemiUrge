# DEMIURGE ships - checks on the exports (run headless, no render):
#   blender -b --factory-startup --python art/ships/check_ships.py
# For every FBX: re-import it, confirm the LodGroup node and its four LOD children, sizes in metres after the
# unit round trip, triangle counts against the manifest, UV V inside 0..1, pivot (waterline at Z = 0, centred),
# and how much of the LOD0 surface faces outward (normals). Prints one line per mesh and a summary.
import json
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ship_tex  # noqa: E402
THIN = {'sail', 'sail_stripe', 'sail_torn', 'flag', 'rope'}     # deliberately two-sided or open thin parts

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.join(HERE, 'export')


def node_attr_types(path):
    """Return the NodeAttribute subclasses written in the file (parse_fbx from Blender's own add-on)."""
    from io_scene_fbx import parse_fbx
    root, _ = parse_fbx.parse(path)
    out = []
    for e in root.elems:
        if e.id == b'Objects':
            for o in e.elems:
                if o.id == b'NodeAttribute':
                    out.append(o.props[2].decode() if len(o.props) > 2 else '?')
    return out


def front_facing(o, bb):
    """Cast rays at the mesh from outside (a dome of directions above the waterline, plus low grazing ones);
    the first face each ray hits must face the ray (normal toward the viewer). Returns the front-facing share."""
    import math
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()
    me = o.data
    tree = BVHTree.FromPolygons([v.co for v in me.vertices], [tuple(p.vertices) for p in me.polygons])
    uv = me.uv_layers.active.data

    def band(pi):
        p = me.polygons[pi]
        v = uv[p.loop_indices[0]].uv[1] * ship_tex.N
        slot = p.material_index
        for k, (s, y0, h) in ship_tex.BANDS.items():
            if s == slot and y0 - 0.5 <= v <= y0 + h + 0.5:
                return k
        return '?'
    M = o.matrix_world
    Mi = M.inverted()
    ctr = Vector([(bb[0][i] + bb[1][i]) * 0.5 for i in range(3)])
    ext = Vector([bb[1][i] - bb[0][i] for i in range(3)])
    R = ext.length * 1.5
    hits = good = 0
    for ei, el in enumerate((-5, 10, 25, 45, 70, 88)):
        for k in range(36):
            az = math.radians(k * 10 + ei * 3)
            d = Vector((math.cos(az) * math.cos(math.radians(el)), math.sin(az) * math.cos(math.radians(el)), math.sin(math.radians(el))))
            for ox in (-0.3, 0.0, 0.3):
                for oz in (-0.2, 0.1, 0.35):
                    side = d.cross(Vector((0, 0, 1)))
                    if side.length < 1e-3:
                        side = Vector((1, 0, 0))
                    side.normalize()
                    target = ctr + Vector((side.x * ox * ext.x, side.y * ox * ext.y, oz * ext.z))
                    origin = target + d * R
                    lo = Mi @ origin
                    ld = (Mi.to_3x3() @ (-d)).normalized()
                    loc, nrm, idx, dist = tree.ray_cast(lo, ld)
                    if loc is None:
                        continue
                    if band(idx) in THIN:
                        continue        # rays slipping into the 3 cm gap of a two-sided sail are not errors
                    hits += 1
                    if nrm.dot(ld) < 0:
                        good += 1
    return good / max(hits, 1)


def main():
    man = json.load(open(os.path.join(HERE, 'manifest.json'), encoding='utf-8'))['meshes']
    bad = 0
    for fn in sorted(os.listdir(EXP)):
        if not fn.endswith('.fbx'):
            continue
        name = fn[:-4]
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=os.path.join(EXP, fn))
        attrs = node_attr_types(os.path.join(EXP, fn))
        roots = [o for o in bpy.data.objects if o.parent is None]
        lods = sorted([o for o in bpy.data.objects if o.type == 'MESH'], key=lambda o: o.name)
        dg = bpy.context.evaluated_depsgraph_get()
        tris, vbad, outward = [], 0, 0.0
        bb = None
        for o in lods:
            me = o.data
            tris.append(sum(len(p.vertices) - 2 for p in me.polygons))
            uv = me.uv_layers.active
            vbad += sum(1 for d in uv.data if not (-1e-4 <= d.uv[1] <= 1.0001))
            if o.name.endswith('LOD0'):
                M = o.matrix_world
                pts = [M @ v.co for v in me.vertices]
                bb = [[min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]]
                outward = front_facing(o, bb)
        m = man.get(name, {})
        exp = [m.get('triangles', {}).get('LOD%d' % i) for i in range(4)]
        ok = (len(lods) == 4 and 'LodGroup' in attrs and tris == exp and vbad == 0 and len(roots) == 1 and outward > 0.99)
        bad += 0 if ok else 1
        print('%-34s %s attrs=%s lods=%d tris=%s uvV_out=%d size=%.1fx%.1fx%.1f zmin=%.2f centre=(%.2f,%.2f) front_facing=%.1f%%' % (
            name, 'OK ' if ok else 'BAD', ','.join(sorted(set(attrs))), len(lods), tris, vbad,
            bb[1][0] - bb[0][0], bb[1][1] - bb[0][1], bb[1][2], bb[0][2], 0.5 * (bb[0][0] + bb[1][0]),
            0.5 * (bb[0][1] + bb[1][1]), outward * 100))
    print('checked; %d problem file(s)' % bad)


main()
