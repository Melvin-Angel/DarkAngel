"""Read-only complete canonical human FBX to renderable native GLB source."""
import bpy,json,hashlib,sys
from pathlib import Path
source,texture,output=map(Path,sys.argv[sys.argv.index('--')+1:])
workspace=Path(__file__).resolve().parents[1];output=output.resolve()
if not output.is_relative_to(workspace) or output.suffix!='.glb':raise RuntimeError('Derived human output must be an owned GLB')
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
before=digest(source);texture_before=digest(texture)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(source))
rigs=[obj for obj in bpy.context.scene.objects if obj.type=='ARMATURE']
if len(rigs)!=1 or len(rigs[0].data.bones)!=81:raise RuntimeError('Expected original canonical 81-bone human rig')
rig=rigs[0];meshes=sorted((obj for obj in bpy.context.scene.objects if obj.type=='MESH'),key=lambda obj:obj.name)
expected={'Body_Arms','Body_Chest','Body_Feet','Body_Hands','Body_Head','Body_Legs','Body_Neck','Underwear','Eyes','Eyebrows','Eyelashes','Hair7','Beard6','Moustache4'}
if {obj.name for obj in meshes}!=expected:raise RuntimeError('Selected canonical modular assembly changed')
material=bpy.data.materials.new('CanonicalHuman_Atlas');material.use_nodes=True;material['darkangel_key']='material/canonical-human'
node=material.node_tree.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(str(texture),check_existing=False);node.image.pack();shader=material.node_tree.nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=.8;material.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
for obj in meshes:
    if not obj.data.uv_layers or not any(mod.type=='ARMATURE' and mod.object==rig for mod in obj.modifiers):raise RuntimeError('Unbound human part or missing authored UVs')
    obj.select_set(True);obj.data['darkangel_key']='mesh/human/'+obj.name;obj.data.materials.clear();obj.data.materials.append(material)
    for polygon in obj.data.polygons:polygon.material_index=0
bpy.context.view_layer.objects.active=rig;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
output.parent.mkdir(parents=True,exist_ok=True)
bpy.ops.export_scene.gltf(filepath=str(output),export_format='GLB',use_selection=True,export_animations=False,export_yup=True,export_extras=True,export_materials='EXPORT',export_cameras=False,export_lights=False,export_def_bones=False)
if digest(source)!=before or digest(texture)!=texture_before:raise RuntimeError('Original human/texture changed')
report=dict(source=str(source),source_sha256=before,texture=str(texture),texture_sha256=texture_before,glb_sha256=digest(output),blender=bpy.app.version_string,bones=81,parts=sorted(expected),scope='Complete original modular assembly and supplied atlas; no runtime retargeting; visual/GPU acceptance remains required')
output.with_suffix('.conversion.json').write_text(json.dumps(report,indent=2)+'\n');print('MODULAR_HUMAN_EXPORTED',len(meshes),'parts')
