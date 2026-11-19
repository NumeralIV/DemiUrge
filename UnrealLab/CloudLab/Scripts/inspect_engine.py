import unreal
out = []
for p in ['/Engine/EngineSky/VolumetricClouds/VT_PerlinWorley_Balanced', '/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseShape128',
          '/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseShape64', '/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseErosion32',
          '/Engine/EngineSky/VolumetricClouds/T_Volume_PerlinWorley_Balanced', '/Engine/EngineSky/VolumetricClouds/T_NoiseShape',
          '/Engine/EngineSky/VolumetricClouds/T_NoiseErosion', '/Engine/EngineSky/VolumetricClouds/CloudWeatherTexture',
          '/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud']:
    a = unreal.load_asset(p)
    if a is None:
        out.append(p + ' : None'); continue
    s = p + ' : ' + a.get_class().get_name()
    try:
        s += ' %dx%d' % (a.blueprint_get_size_x(), a.blueprint_get_size_y())
    except Exception as e:
        pass
    for prop in ['srgb', 'compression_settings', 'address_x', 'filter', 'source_texture', 'tile2d']:
        try: s += ' %s=%s' % (prop, a.get_editor_property(prop))
        except Exception: pass
    out.append(s)
m = unreal.load_asset('/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud')
for prop in ['material_domain', 'blend_mode', 'shading_model', 'used_with_volumetric_cloud' ]:
    try: out.append('mat %s=%s' % (prop, m.get_editor_property(prop)))
    except Exception as e: out.append('mat %s ERR %s' % (prop, e))
MEL = unreal.MaterialEditingLibrary
for mp in [unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_EMISSIVE_COLOR, unreal.MaterialProperty.MP_SUBSURFACE_COLOR, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION, unreal.MaterialProperty.MP_OPACITY]:
    n = MEL.get_material_property_input_node(m, mp)
    out.append('%s <- %s' % (mp, n.get_class().get_name() if n else None))
try:
    exprs = MEL.get_num_material_expressions(m)
    out.append('num expr %d' % exprs)
except Exception as e: out.append(str(e))
open(unreal.Paths.project_saved_dir() + '/inspect.txt', 'w').write('\n'.join(out))
