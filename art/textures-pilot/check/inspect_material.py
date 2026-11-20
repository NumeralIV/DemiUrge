# Independent check helper: dump the shrine pilot materials' node trees (headless, read-only, never saves).
#   blender -b art/textures-pilot/scene/shrine_pilot.blend --python art/textures-pilot/check/inspect_material.py
import bpy

for name in ('rubble', 'limestone', 'thatch', 'fringe', 'rubble_flow_StoneB', 'thatch_flow_ThatchB'):
    m = bpy.data.materials.get(name)
    if not m:
        print('MAT', name, 'missing')
        continue
    print('MAT', name)
    for n in m.node_tree.nodes:
        extra = ''
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            extra = ' image=%s cs=%s path=%s' % (n.image.name, n.image.colorspace_settings.name, n.image.filepath)
        if n.bl_idname == 'ShaderNodeNormalMap':
            extra = ' strength=%s' % n.inputs['Strength'].default_value
        if n.bl_idname == 'ShaderNodeMapping':
            extra = ' scale=%s' % (tuple(n.inputs['Scale'].default_value),)
        print('  NODE', n.bl_idname, n.name, extra)
    for l in m.node_tree.links:
        print('  LINK', l.from_node.name, l.from_socket.name, '->', l.to_node.name, l.to_socket.name)
