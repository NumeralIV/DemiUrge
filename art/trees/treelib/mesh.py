# DEMIURGE tree families: a small mesh builder and its conversion to a Blender mesh object.
#
# Vertex layout (the contract with Unreal, see docs/lab/trees.md "Vertex data"):
#   position   metres, Z up, trunk base at the origin (ground level = z 0)
#   normal     explicit (custom split normals; leaf cards carry a shared "volumetric" normal)
#   UV0        texture: bark in tile units, leaves/impostors in atlas space (Blender convention: v up)
#   UV1        x = trunk sway (metres per unit wind), y = leaf flutter amplitude (metres)
#   UV2        x = branch bend (metres per unit wind), y = branch phase 0..1 (constant per main limb)
#   UV3        (0, 0) guard channel: keeps the C4 nature material's billboard path (UV3.y >= 0.5) off
#   Col        BYTE colour, sRGB tint in RGB, ambient occlusion in A (materials do pow(rgb, 2.2), lerp(0.28, 1, A))
# Material sections: 0 bark, 1 leaves, 2 impostor.
import math
import numpy as np
from mathutils import Vector

SEC_BARK, SEC_LEAF, SEC_IMP = 0, 1, 2


class MB:
    def __init__(self):
        self.P = []
        self.N = []
        self.UV0 = []
        self.UV1 = []
        self.UV2 = []
        self.C = []
        self.T = []
        self.S = []

    def v(self, p, n, uv0, col, uv1=(0.0, 0.0), uv2=(0.0, 0.0)):
        self.P.append((p[0], p[1], p[2]))
        self.N.append((n[0], n[1], n[2]))
        self.UV0.append((uv0[0], uv0[1]))
        self.UV1.append((uv1[0], uv1[1]))
        self.UV2.append((uv2[0], uv2[1]))
        self.C.append((col[0], col[1], col[2], col[3]))
        return len(self.P) - 1

    def tri(self, s, a, b, c, want=None):
        if want is not None:
            pa, pb, pc = Vector(self.P[a]), Vector(self.P[b]), Vector(self.P[c])
            if (pb - pa).cross(pc - pa).dot(Vector(want)) < 0:
                b, c = c, b
        self.T.append((a, b, c))
        self.S.append(s)

    def quad(self, s, a, b, c, d, want=None):
        self.tri(s, a, b, c, want)
        self.tri(s, a, c, d, want)

    def ntris(self, sec=None):
        if sec is None:
            return len(self.T)
        return sum(1 for s in self.S if s == sec)

    def guard(self, sec):
        """A 2 mm triangle buried 0.3 m under the trunk base so every LOD carries every material section
        (Unreal keeps the slot order stable and never shifts materials on an empty section)."""
        b = (0.0, 0.0, -0.3)
        i = self.v(b, (0, 0, 1), (0.5, 0.5), (1, 1, 1, 1))
        j = self.v((0.002, 0.0, -0.3), (0, 0, 1), (0.5, 0.5), (1, 1, 1, 1))
        k = self.v((0.0, 0.002, -0.3), (0, 0, 1), (0.5, 0.5), (1, 1, 1, 1))
        self.tri(sec, i, j, k, (0, 0, 1))

    def bounds(self):
        P = np.array(self.P, np.float32)
        return P.min(0), P.max(0)


def to_blender(mb, name, materials):
    """Creates a Blender mesh object from a builder. materials: list of bpy materials in section order."""
    import bpy
    P = np.array(mb.P, np.float32)
    N = np.array(mb.N, np.float32)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-8)
    T = np.array(mb.T, np.int32)
    nv, nt = len(P), len(T)
    me = bpy.data.meshes.new(name)
    me.vertices.add(nv)
    me.vertices.foreach_set('co', P.ravel())
    me.loops.add(nt * 3)
    me.loops.foreach_set('vertex_index', T.ravel())
    me.polygons.add(nt)
    me.polygons.foreach_set('loop_start', np.arange(0, nt * 3, 3, dtype=np.int32))
    me.polygons.foreach_set('material_index', np.array(mb.S, np.int32))
    me.polygons.foreach_set('use_smooth', np.ones(nt, dtype=bool))
    me.update(calc_edges=True)
    loops_v = T.ravel()
    for nm, arr in (('UV0', mb.UV0), ('UV1', mb.UV1), ('UV2', mb.UV2), ('UV3', None)):
        lay = me.uv_layers.new(name=nm)
        if arr is None:
            data = np.zeros((nt * 3, 2), np.float32)
        else:
            data = np.array(arr, np.float32)[loops_v]
        if nm != 'UV0':
            # Unreal's FBX importer flips V (v -> 1 - v) on EVERY UV channel, not only the texture one. The data
            # channels are stored pre-flipped so Unreal receives the values of the contract (docs/lab/trees.md §4).
            data = data.copy()
            data[:, 1] = 1.0 - data[:, 1]
        lay.data.foreach_set('uv', data.ravel())
    ca = me.color_attributes.new('Col', 'BYTE_COLOR', 'CORNER')
    C = np.array(mb.C, np.float32)[loops_v]
    ca.data.foreach_set('color_srgb', np.clip(C, 0, 1).ravel())
    me.color_attributes.active_color = ca
    me.color_attributes.render_color_index = 0
    me.normals_split_custom_set_from_vertices([tuple(n) for n in N])
    for m in materials:
        me.materials.append(m)
    ob = bpy.data.objects.new(name, me)
    return ob
