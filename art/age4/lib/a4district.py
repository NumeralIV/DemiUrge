# DEMIURGE art - Age4 district preview: the kit laid out as a small industrial town (terrace grid, mills,
# warehouses on a river, station, rail bridge, colliery) to compare against docs/concept/settlements/age-4-industry.png
# at the game's 300 m and 1 km camera distances. Preview only: nothing here is exported.
import math
import os
import random

import bpy

import a4mat

HERE = os.path.dirname(os.path.abspath(__file__))
BLEND = os.path.normpath(os.path.join(HERE, '..', 'blend'))


def load_mesh(key, state='Intact', lod=0):
    name = 'SM_Age4_%s_%s_LOD%d' % (key, state, lod)
    if name in bpy.data.meshes:
        return bpy.data.meshes[name]
    with bpy.data.libraries.load(os.path.join(BLEND, key + '.blend'), link=False) as (src, dst):
        dst.meshes = [n for n in src.meshes if n == name]
    return bpy.data.meshes[name]


def place(key, x, y, rot=0.0, z=0.0, state='Intact', mat_override=None, lod=0):
    me = load_mesh(key, state, lod)
    if mat_override:
        me = me.copy()
        for i, m in enumerate(me.materials):
            if m and m.name.startswith(mat_override[0]):
                me.materials[i] = mat_override[1]
    ob = bpy.data.objects.new('%s_%d' % (key, len(bpy.data.objects)), me)
    ob.location = (x, y, z)
    ob.rotation_euler = (0, 0, math.radians(rot))
    bpy.context.scene.collection.objects.link(ob)
    return ob


def water(x0, x1, y0, y1, z):
    me = bpy.data.meshes.new('A4Water')
    me.from_pydata([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], [], [(0, 1, 2, 3)])
    m = bpy.data.materials.new('A4WaterMat')
    m.use_nodes = True
    bs = m.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (0.025, 0.04, 0.045, 1)
    bs.inputs['Roughness'].default_value = 0.06
    me.materials.append(m)
    ob = bpy.data.objects.new('A4Water', me)
    bpy.context.scene.collection.objects.link(ob)


def paved(x0, x1, y0, y1, z=0.02, col=(0.09, 0.085, 0.08)):
    me = bpy.data.meshes.new('A4Paved')
    me.from_pydata([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], [], [(0, 1, 2, 3)])
    m = bpy.data.materials.get('A4PavedMat') or bpy.data.materials.new('A4PavedMat')
    m.use_nodes = True
    bs = m.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (*col, 1)
    bs.inputs['Roughness'].default_value = 0.9
    me.materials.append(m)
    ob = bpy.data.objects.new('A4Paved', me)
    bpy.context.scene.collection.objects.link(ob)


TERRACE_LEN = {'A': 20.0, 'B': 26.0, 'C': 28.8, 'D': 22.4}
PLOT_D = {'A': 14.3, 'B': 14.5, 'C': 14.9, 'D': 15.3}


def layout():
    rng = random.Random(4)
    # terrace grid: back-to-back pairs of rows separated by streets
    y = -230.0
    for band in range(6):
        for facing in (0, 1):
            x = -260.0
            while x < 40.0:
                v = rng.choice('AABBCD')
                L = TERRACE_LEN[v]
                yy = y + (0 if facing == 0 else PLOT_D[v] + 0.2)
                place('Terrace_%s' % v, x + L / 2, yy, 180.0 if facing else 0.0)
                x += L
                if rng.random() < 0.18:
                    x += 9.0    # cross street
        y += 2 * 15.0 + 11.0
    paved(-270, 50, -250, 25, 0.01)
    # mills and works (east)
    place('Factory_A', 140.0, -150.0)
    place('Factory_B', 130.0, -55.0, 0)
    place('Stack_A', 85.0, -95.0)
    coal = a4mat.make_material('M_Age4_Gravel', 'Coal')
    ore = a4mat.make_material('M_Age4_Gravel', 'Ore')
    place('Spoil_B', 185.0, -90.0, 20, mat_override=('M_Age4_Gravel', coal))
    place('Spoil_B', 200.0, -70.0, 70, mat_override=('M_Age4_Gravel', ore))
    place('Spoil_B', 172.0, -68.0, 140, mat_override=('M_Age4_Gravel', coal))
    paved(60, 230, -200, -10, 0.012, (0.07, 0.065, 0.06))
    # station on the north side of the terraces, tracks running east to the bridge approach
    place('Station_A', -150.0, 75.0)
    place('Warehouse_B', -40.0, 65.0)
    # quay with dock warehouses along the river
    for x in (-60.0, -25.0, 10.0, 45.0):
        place('Warehouse_A', x, 128.0, 180.0)
    place('Warehouse_A', 80.0, 128.0, 180.0)
    paved(-90, 110, 105, 152, 0.015, (0.11, 0.10, 0.09))
    # river and the rail bridge (along Y), lifted so the deck clears the water
    water(-2000, 2000, 152, 240, 0.05)
    deck = 13.0
    for k, yc in enumerate((174.0, 217.0)):
        place('Bridge_A', 180.0, yc, 90.0, z=deck)
    place('Bridge_B', 180.0, 134.5, 90.0, z=deck)
    place('Bridge_B', 180.0, 98.5, 90.0, z=deck)
    place('Bridge_B', 180.0, 256.5, 90.0, z=deck)
    # colliery on the far bank
    place('MineHead_A', -140.0, 300.0, 0)
    place('Spoil_A', -60.0, 310.0, 30)
    place('Spoil_A', -10.0, 345.0, 75)


def render_district(shots, rig, look, aim_sun, render):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    cam, sun = rig(scene)
    layout()
    for (name, dist, tgt) in (('300m', 300.0, (-40.0, 10.0, 0.0)), ('1km', 1000.0, (-30.0, 40.0, 0.0))):
        look(cam, tgt, 25, 40, dist, 50.0)
        aim_sun(sun, 25, el=20.0, rel=-120.0)
        render(scene, os.path.join(shots, 'asset-age4-district-%s.png' % name))
    look(cam, (120.0, -110.0, 10.0), 35, 32, 220.0, 50.0)
    aim_sun(sun, 35, el=20.0, rel=-100.0)
    render(scene, os.path.join(shots, 'asset-age4-district-mills.png'))
