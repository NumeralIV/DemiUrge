# TreeLab diagnostic: prints the UV channel ranges and build settings of imported tree meshes (checks the vertex contract).
import unreal
for path in ['/Game/Trees/Palm/SM_Tree_Palm_0', '/Game/Trees/Broadleaf/SM_Tree_Broadleaf_0']:
    m = unreal.load_asset(path)
    ses = unreal.get_default_object(unreal.StaticMeshEditorSubsystem)
    for lod in (0, 1):
        bs = ses.get_lod_build_settings(m, lod)
        unreal.log('UVCHK %s lod%d gen_lightmap=%s dst=%s src=%s fullprec=%s nuv=%d lmidx=%d' % (path, lod, bs.get_editor_property('generate_lightmap_u_vs'),
                   bs.get_editor_property('dst_lightmap_index'), bs.get_editor_property('src_lightmap_index'), bs.get_editor_property('use_full_precision_u_vs'),
                   unreal.EditorStaticMeshLibrary.get_num_uv_channels(m, lod), m.get_editor_property('light_map_coordinate_index')))
        md = m.get_static_mesh_description(lod)
        n = md.get_vertex_instance_count()
        for ch in range(4):
            lo = [1e9, 1e9]; hi = [-1e9, -1e9]
            for i in range(0, n, max(1, n // 400)):
                try:
                    uv = md.get_vertex_instance_uv(unreal.VertexInstanceID(i), ch)
                except Exception as e:
                    unreal.log('UVCHK err %s' % e); break
                lo = [min(lo[0], uv.x), min(lo[1], uv.y)]; hi = [max(hi[0], uv.x), max(hi[1], uv.y)]
            unreal.log('UVCHK   ch%d min %s max %s' % (ch, lo, hi))
