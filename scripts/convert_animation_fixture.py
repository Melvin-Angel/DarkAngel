"""Blender bpy: read-only FBX to canonical, metre/Y-up, sampled glTF clip.

Normalize the authored tpose basis onto the same canonical hierarchy offline.
Different rest bases require --normalize-rest. Reject different hierarchy or nonuniform scale. No mapping asset
or runtime retargeting is introduced. Extra helper joints may be omitted.
Run: blender --background --factory-startup --python this.py -- FBX RIG OUT.glb
"""
import bpy,sys,pathlib,json,hashlib,struct,math
from mathutils import Matrix,Quaternion,Vector
arguments=sys.argv[sys.argv.index('--')+1:];normalize_rest='--normalize-rest' in arguments
source,canonical,output=map(pathlib.Path,[a for a in arguments if a!='--normalize-rest'])
if output.suffix.lower()!='.glb':raise RuntimeError('Output must be GLB')
workspace=pathlib.Path(__file__).resolve().parents[1]
if not output.resolve().is_relative_to(workspace):raise RuntimeError('Derived fixture output must stay inside the owned workspace')
if source.resolve()==output.resolve():raise RuntimeError('Original must stay read-only')
before=hashlib.sha256(source.read_bytes()).hexdigest()
definition=json.loads(canonical.read_text());joints=definition['joints'];keys={j['key']:i for i,j in enumerate(joints)}
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(source))
rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
if len(rigs)!=1:raise RuntimeError('Exactly one source armature required')
rig=rigs[0]
actions=[a for a in bpy.data.actions if a.name.startswith(rig.name+'|') and a.frame_range[1]-a.frame_range[0]>2]
references=[a for a in bpy.data.actions if a.name.startswith(rig.name+'|tpose|')]
if len(actions)!=1 or len(references)!=1:raise RuntimeError('Expected one animation and one authored armature tpose reference')
action=actions[0];reference_action=references[0]
def select_action(value):
    rig.animation_data.action=value
    if value.slots:rig.animation_data.action_slot=value.slots[0]
select_action(reference_action);bpy.context.scene.frame_set(round(reference_action.frame_range[0]))
reference_rig=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
axis=Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
def rigid(matrix):
    t,q,s=matrix.decompose();q.normalize()
    if min(s)<=0 or max(s)-min(s)>1e-5:raise RuntimeError('Nonuniform/reflected animated scale')
    return Matrix.Translation(t) @ q.to_matrix().to_4x4()
canonical_world=[];canonical_local=[];reference_world=[];reference_local=[];rest_offsets=[];length_offsets=[];rest_only=[]
for joint in joints:
    name=joint['key'];bone=rig.data.bones.get(name)
    if bone is None:
        if not normalize_rest or not name.startswith('jnt_eye'):raise RuntimeError('Missing required canonical joint: '+name)
        rest_only.append(name)
    elif (bone.parent.name if bone.parent else None)!=joint['parent']:raise RuntimeError('Canonical joint/hierarchy mismatch: '+name)
    q=joint['rotation'];local=Matrix.Translation(Vector(joint['translation'])) @ Quaternion((q[3],q[0],q[1],q[2])).to_matrix().to_4x4()
    world=canonical_world[keys[joint['parent']]] @ local if joint['parent'] else local
    canonical_world.append(world)
    canonical_local.append(local)
    # These FBXs use the initial animation pose as their Blender bone rest.
    # The authored tpose take is the bind reference; validate that take and bake
    # the animation's evaluated canonical transforms rather than rest deltas.
    actual=rigid(axis @ reference_rig.matrix_world @ reference_rig.pose.bones[name].matrix) if bone else reference_world[keys[joint['parent']]] @ local
    reference_world.append(actual)
    reference=reference_world[keys[joint['parent']]].inverted() @ actual if joint['parent'] else actual
    reference_local.append(reference)
    length_offsets.append(abs(reference.translation.length-local.translation.length) if joint['parent'] else 0)
    rest_offsets.append(max(abs(reference[c][r]-local[c][r]) for c in range(4) for r in range(4)))
    if not normalize_rest and rest_offsets[-1]>.0003:raise RuntimeError('Canonical rest differs; explicit offline --normalize-rest required: '+name)
extras=sorted(set(rig.data.bones.keys())-set(keys))
for name in extras:
    if any(child.name in keys for child in rig.data.bones[name].children_recursive):raise RuntimeError('Cannot omit ancestor of canonical joint')
select_action(action)
start,end=action.frame_range;fps=bpy.context.scene.render.fps/bpy.context.scene.render.fps_base
ticks=round((end-start)*60/fps)
if not 1<=ticks<=600:raise RuntimeError('Clip tick budget')
duration=ticks/60;tracks=[dict(translation=[],rotation=[]) for _ in joints]
for tick in range(ticks+1):
    frame=start+(end-start)*tick/ticks;bpy.context.scene.frame_set(math.floor(frame),subframe=frame%1)
    evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    worlds=[]
    for i,j in enumerate(joints):
        worlds.append(worlds[keys[j['parent']]] @ reference_local[i] if j['key'] in rest_only else rigid(axis @ evaluated.matrix_world @ evaluated.pose.bones[j['key']].matrix))
    for i,joint in enumerate(joints):
        local=worlds[keys[joint['parent']]].inverted() @ worlds[i] if joint['parent'] else worlds[i]
        if normalize_rest:local=canonical_local[i] @ reference_local[i].inverted() @ local
        t,q,s=local.decompose();q.normalize()
        previous=tracks[i]['rotation']
        values=[q.x,q.y,q.z,q.w]
        if previous and sum(a*b for a,b in zip(previous[-1],values))<0:values=[-n for n in values]
        tracks[i]['translation'].append(list(t));tracks[i]['rotation'].append(values)
binary=bytearray();views=[];accessors=[]
def accessor(values,width):
    offset=len(binary);flat=[n for value in values for n in value] if width>1 else values
    binary.extend(struct.pack('<'+'f'*len(flat),*flat));views.append(dict(buffer=0,byteOffset=offset,byteLength=len(flat)*4))
    item=dict(bufferView=len(views)-1,componentType=5126,count=len(values),type={1:'SCALAR',3:'VEC3',4:'VEC4'}[width])
    if width==1:item.update(min=[min(values)],max=[max(values)])
    accessors.append(item);return len(accessors)-1
times=accessor([tick/60 for tick in range(ticks+1)],1);samplers=[];channels=[];nodes=[]
for i,joint in enumerate(joints):
    node=dict(name=joint['key'],translation=joint['translation'],rotation=joint['rotation'],scale=[1,1,1]);children=[n for n,j in enumerate(joints) if j['parent']==joint['key']]
    if children:node['children']=children
    nodes.append(node)
    for path,width in [('translation',3),('rotation',4)]:
        samplers.append(dict(input=times,output=accessor(tracks[i][path],width),interpolation='LINEAR'))
        channels.append(dict(sampler=len(samplers)-1,target=dict(node=i,path=path)))
gltf=dict(asset=dict(version='2.0',generator='DarkAngel bpy canonical clip conversion'),scene=0,scenes=[dict(nodes=[0])],nodes=nodes,buffers=[dict(byteLength=len(binary))],bufferViews=views,accessors=accessors,animations=[dict(name=source.stem,samplers=samplers,channels=channels)],extras=dict(darkangel_axes='right-handed-y-up-metres',darkangel_canonical=definition['asset']))
document=json.dumps(gltf,separators=(',',':')).encode();document+=b' '*((-len(document))%4);binary+=b'\0'*((-len(binary))%4)
blob=struct.pack('<III',0x46546c67,2,12+8+len(document)+8+len(binary))+struct.pack('<II',len(document),0x4e4f534a)+document+struct.pack('<II',len(binary),0x004e4942)+binary
if hashlib.sha256(source.read_bytes()).hexdigest()!=before:raise RuntimeError('Original FBX changed')
output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(blob)
report=dict(blender=bpy.app.version_string,source=str(source),source_sha256=before,canonical_sha256=hashlib.sha256(canonical.read_bytes()).hexdigest(),glb_sha256=hashlib.sha256(blob).hexdigest(),action=action.name,reference=reference_action.name,source_fps=fps,source_frames=[start,end],ticks=ticks,joints=len(joints),omitted_helpers=extras,rest_only_face_joints=rest_only,normalized_rest=normalize_rest,max_rest_basis_matrix_difference=max(rest_offsets),max_rest_length_difference_metres=max(length_offsets),profile='Mapped canonical hierarchy validated; explicit offline authored-tpose rest normalization and missing brow/eyelid rest fill; rigid metre/Y-up transforms sampled at 60 Hz; no runtime retarget; visual motion quality still requires acceptance')
output.with_suffix('.conversion.json').write_text(json.dumps(report,indent=2)+'\n');print('CLIP_CONVERTED',json.dumps(report))
