# DEMIURGE art - Age6 verification renders (independent check, 2026-11-19). Run through the slot tool:
#
#   python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b \
#       art/age6/source/age6.blend --python art/age6/verify_render_age6.py
#
# Reuses the preview rig in preview_age6.py (same sun, sky, ground, haze, 1.8 m figure) and adds:
#   shots/asset-age6-verify-lods.png    LOD0..LOD3 forced side by side (towers A/E, hall, pad, block, track)
#   shots/asset-age6-verify-scale.png   the 1.8 m figure at a tower base and the station entrance, 25 m away
import os
import sys
import math

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv = [a for a in sys.argv] + (['--'] if '--' not in sys.argv else []) + ['--shots', 'none']
G = {'__file__': os.path.join(HERE, 'preview_age6.py'), '__name__': 'preview_age6'}
exec(compile(open(os.path.join(HERE, 'preview_age6.py'), encoding='utf-8').read(), 'preview_age6.py', 'exec'), G)

sun, cam, fig = G['RIG']
place, clear_placed, render, S = G['place'], G['clear_placed'], G['render'], G['S']
SHOTS = G['SHOTS']


def force_lod(obs, k):
    for lod, ob in obs:
        ob.hide_render = lod != k


def render_forced(path, pos, target, sun_az, rows):
    G['aim'](cam, pos, target)
    G['sun_from'](sun, sun_az)
    for obs, k in rows:
        force_lod(obs, k)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('wrote', path)


def lods_sheet():
    clear_placed()
    rows = []
    # rows back to front (+Y is away from the camera); columns LOD0..LOD3 left to right
    items = [('ArcologyTower_E', 0, 90), ('ArcologyTower_A', 0, 70), ('GreenBlock_C', 0, 60),
             ('MaglevTrack_Straight', 0, 50), ('LaunchPad', 0, 160), ('AssemblyHall', 0, 150)]
    y = 0.0
    for name, rot, pitch in items:
        for k in range(4):
            obs = place(S(name), -375 + 250 * k, y, rot)
            rows.append((obs, k))
        y -= pitch
    a = math.radians(185)
    e = math.radians(34)
    c = Vector((0, y * 0.5, 0))
    d = 1350.0
    pos = c + Vector((math.sin(a) * d * math.cos(e), math.cos(a) * d * math.cos(e), d * math.sin(e)))
    render_forced(os.path.join(SHOTS, 'asset-age6-verify-lods.png'), pos, c + Vector((0, 0, 20)), 200 - 115, rows)


def scale_shot():
    clear_placed()
    place(S('ArcologyTower_A'), 0, 0, 0)
    place(S('MaglevStation_A'), 75, 10, 0)
    fig.location = (2, -15.6, 0)
    a = math.radians(205)
    pos = Vector((math.sin(a) * 26 + 6, math.cos(a) * 26 - 10, 4.0))
    render(os.path.join(SHOTS, 'asset-age6-verify-scale.png'), cam, sun, pos, Vector((8, -6, 6.0)), 205 - 115)


lods_sheet()
scale_shot()
print('VERIFY RENDER DONE')
