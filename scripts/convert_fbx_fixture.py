"""Run with Blender --background --factory-startup --python ... -- SOURCE OUTPUT.
Reads a user-selected FBX and writes a local GLB fixture; never writes the source.
"""
import bpy, sys, pathlib, json, hashlib

source,output=map(pathlib.Path,sys.argv[sys.argv.index('--')+1:])
source=source.resolve();output=output.resolve();output.parent.mkdir(parents=True,exist_ok=True)
before=hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(source))
meshes=[obj for obj in bpy.context.scene.objects if obj.type=='MESH']
if len(meshes)!=1 or any(obj.type=='ARMATURE' for obj in bpy.context.scene.objects):
    raise RuntimeError('This fixture converter requires one static prop mesh and no armatures')
# Explicit test material: a generated checker makes texture/material integration observable.
image=bpy.data.images.new('DarkAngel_TestChecker',width=16,height=16,alpha=True)
pixels=[]
for y in range(16):
    for x in range(16):pixels.extend((0.55,0.18,0.04,1.0) if ((x//4+y//4)%2) else (0.08,0.23,0.42,1.0))
image.pixels=pixels;image.pack()
material=bpy.data.materials.new('DarkAngel_TestMaterial');material.use_nodes=True
nodes=material.node_tree.nodes;texture=nodes.new('ShaderNodeTexImage');texture.image=image
shader=nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=.8
material.node_tree.links.new(texture.outputs['Color'],shader.inputs['Base Color'])
material['darkangel_key']='material/checker'
for i,obj in enumerate(sorted(meshes,key=lambda obj:obj.name)):
    obj.data['darkangel_key']='mesh/prop' # explicit immutable author key, unrelated to array order/name
    obj.data.materials.clear();obj.data.materials.append(material)
    if not obj.data.uv_layers:
        bpy.context.view_layer.objects.active=obj;obj.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project();bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.export_scene.gltf(filepath=str(output),export_format='GLB',export_animations=False,
    export_yup=True,export_extras=True,export_materials='EXPORT',export_cameras=False,export_lights=False)
after=hashlib.sha256(source.read_bytes()).hexdigest()
if before!=after:raise RuntimeError('Source changed during conversion')
report={'blender':bpy.app.version_string,'source':str(source),'source_sha256':before,
    'output_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'objects':len(meshes),
    'triangles_before_export':sum(len(obj.data.polygons) for obj in meshes),
    'material':'Generated checker test material, original source untouched',
    'axes':'glTF exporter Y-up/right-handed, FBX units handled by importer'}
output.with_suffix('.conversion.json').write_text(json.dumps(report,indent=2)+'\n')
print('DARKANGEL_CONVERSION',json.dumps(report))
