# Independent check: which map makes the Flow walls / roof render darker than the procedural ones?
#   blender -b art/textures-pilot/scene/shrine_pilot.blend --python art/textures-pilot/check/render_ab.py --
#           --out=<dir> [--views=close8,v40] [--lights=overcast,golden] [--samples=48] [--res=480,360]
#           [--variants=before,after,after_flatN,after_procN,after_procR] [--stone=StoneB] [--thatch=ThatchB]
#           [--nset=<dir with T_Shrine_<Name>_N.png to use instead of out/>]
# Never saves the .blend. Reuses render_pilot.py's scene setup (exec'd without its main()).
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
PILOT = os.path.dirname(HERE)
src = open(os.path.join(PILOT, 'render_pilot.py'), encoding='utf-8').read()
src = src[:src.rstrip().rfind('main()')]
G = {'__file__': os.path.join(PILOT, 'render_pilot.py'), '__name__': 'render_pilot'}
exec(compile(src, 'render_pilot.py', 'exec'), G)

PROC = {'rubble': 'Rubble', 'limestone': 'Limestone', 'thatch': 'Thatch'}


def find_img_node(nt, kind):
    for n in nt.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            nm = n.image.name
            k = 'D' if ('_BC' in nm or '_D.' in nm) else 'ORM' if '_ORM' in nm else 'N' if '_N' in nm else None
            if k == kind:
                return n
    return None


def tweak(variant, nset):
    mats = [m for m in bpy.data.materials if '_flow_' in m.name]
    for m in mats:
        nt = m.node_tree
        base = m.name.split('_flow_')[0]
        setname = m.name.split('_flow_')[1].split('.')[0]
        bsdf = nt.nodes['Principled BSDF']
        if variant == 'after_flatN':
            for l in list(bsdf.inputs['Normal'].links):
                nt.links.remove(l)
        elif variant == 'after_procN':
            n = find_img_node(nt, 'N')
            n.image = bpy.data.images.load(os.path.join(G['ROOT'], 'art', 'textures', base,
                                                        'T_%s_N.png' % PROC[base]), check_existing=True)
            n.image.colorspace_settings.name = 'Non-Color'
            # the procedural set tiles at 2 m: feed it the plain UV
            for l in list(n.inputs[0].links):
                nt.links.remove(l)
        elif variant == 'after_procR':
            n = find_img_node(nt, 'ORM')
            n.image = bpy.data.images.load(os.path.join(G['ROOT'], 'art', 'textures', base,
                                                        'T_%s_ORM.png' % PROC[base]), check_existing=True)
            n.image.colorspace_settings.name = 'Non-Color'
            for l in list(n.inputs[0].links):
                nt.links.remove(l)
        elif variant == 'after_nset' and nset:
            n = find_img_node(nt, 'N')
            n.image = bpy.data.images.load(os.path.join(nset, 'T_Shrine_%s_N.png' % setname), check_existing=False)
            n.image.colorspace_settings.name = 'Non-Color'


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    f = dict(a[2:].split('=', 1) for a in argv if a.startswith('--') and '=' in a)
    out = os.path.abspath(f['out'])
    os.makedirs(out, exist_ok=True)
    views = f.get('views', 'close8,v40').split(',')
    lights = f.get('lights', 'overcast,golden').split(',')
    variants = f.get('variants', 'before,after,after_flatN,after_procN,after_procR').split(',')
    W, H = [int(x) for x in f.get('res', '480,360').split(',')]
    G['fix_paths']()
    G['show_only']()
    G['setup_ground']()
    G['setup_render'](int(f.get('samples', 48)))
    stone, thatch = f.get('stone', 'StoneB'), f.get('thatch', 'ThatchB')
    for var in variants:
        G['apply_before']()
        if var != 'before':
            G['apply_after'](stone, thatch)
            tweak(var, f.get('nset'))
        for light in lights:
            G['setup_light'](light)
            for view in views:
                G['setup_camera'](view, W, H)
                sc = bpy.context.scene
                sc.render.filepath = os.path.join(out, '%s-%s-%s.png' % (view, light, var))
                bpy.ops.render.render(write_still=True)
                print('AB_RENDER', sc.render.filepath)


main()
