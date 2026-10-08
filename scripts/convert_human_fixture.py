"""Offline read-only FBX inspection/export; run with Blender background mode."""
import bpy,sys,pathlib,json,hashlib,struct
source,output=map(pathlib.Path,sys.argv[sys.argv.index('--')+1:]);output.parent.mkdir(parents=True,exist_ok=True)
before=hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(source))
rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
if len(rigs)!=1:raise RuntimeError('Canonical human source requires exactly one armature')
rig=rigs[0];meshes=[o for o in bpy.context.scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers)]
print('HUMAN_SOURCE',json.dumps({'rig':rig.name,'bones':len(rig.data.bones),'meshes':[(o.name,len(o.data.vertices)) for o in meshes]}))
# Select the largest original skin only for the bounded native pipeline fixture.
mesh=max(meshes,key=lambda o:len(o.data.vertices));bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);mesh.select_set(True);mesh.data['darkangel_key']='mesh/canonical-human'
for index,material in enumerate(mesh.data.materials):material['darkangel_key']='material/human-'+str(index)
bpy.context.view_layer.objects.active=rig
bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
bpy.ops.export_scene.gltf(filepath=str(output),export_format='GLB',use_selection=True,export_animations=False,export_yup=True,export_extras=True,export_materials='EXPORT',export_cameras=False,export_lights=False,export_def_bones=False)
if hashlib.sha256(source.read_bytes()).hexdigest()!=before:raise RuntimeError('Read-only original FBX changed')
data=output.read_bytes();length,kind=struct.unpack_from('<II',data,12);gltf=json.loads(data[20:20+length]);nodes=gltf['nodes'];skin=gltf['skins'][0];parents={c:i for i,n in enumerate(nodes) for c in n.get('children',[])}
# Normalize rest transforms into rigid metre-space matrices; fold the armature
# object's unit/axis transform into the single root rather than adding a bone.
from mathutils import Matrix
axis=Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
world={}
for bone in rig.data.bones:
    matrix=axis @ rig.matrix_world @ bone.matrix_local
    translation,rotation,scale=matrix.decompose()
    if max(scale)-min(scale)>1e-5:raise RuntimeError('Nonuniform canonical bone scale')
    world[bone.name]=Matrix.Translation(translation) @ rotation.to_matrix().to_4x4()
joints=[]
def visit(bone):
    local=world[bone.parent.name].inverted() @ world[bone.name] if bone.parent else world[bone.name]
    translation,rotation,scale=local.decompose();rotation.normalize()
    if rotation.w<0:rotation.negate()
    joints.append({'key':bone.name,'parent':bone.parent.name if bone.parent else None,'translation':list(translation),'rotation':[rotation.x,rotation.y,rotation.z,rotation.w],'scale':[1,1,1]})
    for child in sorted(bone.children,key=lambda bone:bone.name):visit(child)
for bone in sorted((b for b in rig.data.bones if b.parent is None),key=lambda bone:bone.name):visit(bone)
canonical={'schema':1,'asset':'d9f069df-ff5c-4a02-aeea-f14829fcc705','runtime':'c6a32cb0-a818-4cf2-8c61-7d6c491eb01b','kind':'skeleton','axes':'right-handed-y-up-metres','human':True,'joints':joints,'sockets':{'weapon_right':'jointItemR','weapon_left':'jointItemL','head':'Head_M','foot_right':'Ankle_R','foot_left':'Ankle_L'}}
# Derived source stays beside the ignored fixture until reviewed/copied into content.
output.with_suffix('.daskeleton').write_text(json.dumps(canonical,indent=2)+'\n')
report={'blender':bpy.app.version_string,'source':str(source),'source_sha256':before,'glb_sha256':hashlib.sha256(data).hexdigest(),'bone_count':len(rig.data.bones),'mesh':mesh.name,'skin_joints':len(skin['joints']),'joints':[{'node':i,'name':nodes[i]['name'],'parent':parents.get(i)} for i in skin['joints']],'nodes':len(nodes)}
output.with_suffix('.conversion.json').write_text(json.dumps(report,indent=2)+'\n');print('HUMAN_EXPORTED',json.dumps({k:v for k,v in report.items() if k!='joints'}))
