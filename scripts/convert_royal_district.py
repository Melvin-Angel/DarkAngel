"""Blender bpy: read-only Royal District static FBXs to owned native glTF sources.

Usage: blender --background --factory-startup --python-exit-code 1 --python
scripts/convert_royal_district.py -- SOURCE_ROYAL_DISTRICT OWNED_OUTPUT
Preserves UVs/pivots, normalizes FBX units and glTF Y-up, shares original atlases.
"""
import bpy
import hashlib
import json
from pathlib import Path
import shutil
import sys
from mathutils import Matrix, Vector

source_root, output_root = map(Path, sys.argv[sys.argv.index('--')+1:])
source_root, output_root = source_root.resolve(), output_root.resolve()
workspace = Path(__file__).resolve().parents[1]
if not output_root.resolve().is_relative_to(workspace) or output_root.resolve().is_relative_to(source_root.resolve()):
    raise RuntimeError('Outputs must be in the owned workspace, outside original sources')
packs = [('Mud Huts', 'mud_huts', 'mudhut trim sheet.png', 'mudhut-trim.png', .82),
         ('TempleIslandTerrain', 'terrain', '1-Foto-1.jpg', 'terrain-trim.jpg', .9)]
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
records = []
axis = Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
for source_folder, owned_folder, texture_name, owned_texture, roughness in packs:
    folder = source_root/source_folder
    inputs = sorted(folder.rglob('*.fbx'))
    if not inputs:
        raise RuntimeError('Empty requested FBX pack: '+str(folder))
    target = output_root/owned_folder
    target.mkdir(parents=True, exist_ok=True)
    texture = folder/texture_name
    texture_hash = digest(texture)
    shutil.copyfile(texture, target/owned_texture)
    for source in inputs:
        before = digest(source)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=str(source))
        meshes = sorted((obj for obj in bpy.context.scene.objects if obj.type=='MESH'), key=lambda obj:obj.name)
        if not meshes or any(obj.type=='ARMATURE' for obj in bpy.context.scene.objects):
            raise RuntimeError('Requested static profile requires meshes without an armature')
        material = bpy.data.materials.new('RoyalDistrict_Trim')
        material.use_nodes = True
        material['darkangel_key'] = 'material/trim'
        shader = material.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Roughness'].default_value = roughness
        image = bpy.data.images.load(str(texture), check_existing=False)
        node = material.node_tree.nodes.new('ShaderNodeTexImage')
        node.image = image
        material.node_tree.links.new(node.outputs['Color'], shader.inputs['Base Color'])
        points = []
        triangles = 0
        for index, obj in enumerate(meshes):
            if not obj.data.uv_layers:
                raise RuntimeError('Missing authored trim UVs: '+obj.name)
            matrix = obj.matrix_world.copy()
            obj.parent = None
            obj.matrix_world = matrix
            obj.data.materials.clear()
            obj.data.materials.append(material)
            for polygon in obj.data.polygons:
                polygon.material_index = 0
            obj.data['darkangel_key'] = 'mesh/'+str(index)
            obj.data.calc_loop_triangles()
            triangles += len(obj.data.loop_triangles)
            points.extend(axis @ obj.matrix_world @ vertex.co for vertex in obj.data.vertices)
        bpy.ops.object.select_all(action='DESELECT')
        for obj in meshes:
            obj.select_set(True)
        output = target/(source.stem+'.gltf')
        bpy.ops.export_scene.gltf(filepath=str(output), export_format='GLTF_SEPARATE',
            use_selection=True, export_animations=False, export_skins=False,
            export_morph=False, export_yup=True, export_extras=True,
            export_materials='EXPORT', export_cameras=False, export_lights=False)
        document = json.loads(output.read_text())
        if len(document.get('images', [])) != 1:
            raise RuntimeError('Expected one shared trim atlas')
        # Use an exact byte copy of the supplied atlas, avoiding image reencoding.
        generated = document['images'][0].get('uri')
        document['images'][0] = dict(uri=owned_texture)
        output.write_text(json.dumps(document, separators=(',', ':'))+'\n')
        if generated and generated != owned_texture:
            generated_path = (target/generated).resolve()
            if generated_path.is_relative_to(target.resolve()) and generated_path.is_file():
                generated_path.unlink()
        if digest(source) != before or digest(texture) != texture_hash or digest(target/owned_texture) != texture_hash:
            raise RuntimeError('Source/trim hash fence failed')
        records.append(dict(source=str(source), source_sha256=before, texture=str(texture),
            texture_sha256=texture_hash, gltf=output.relative_to(workspace).as_posix(),
            gltf_sha256=digest(output), buffers={item['uri']:digest(target/item['uri']) for item in document['buffers']},
            meshes=len(meshes), triangles=triangles,
            minimum=[min(point[axis] for point in points) for axis in range(3)],
            maximum=[max(point[axis] for point in points) for axis in range(3)],
            material_profile='Original trim UVs/base color; native PBR roughness '+str(roughness)))
        print('ROYAL_CONVERTED', source.name, 'triangles=',triangles, flush=True)
output_root.joinpath('conversion.json').write_text(json.dumps(dict(blender=bpy.app.version_string,
    profile='Read-only FBX, original shared trim atlases, retained pivots/UVs, metre/Y-up static glTF',
    files=records),indent=2)+'\n')
