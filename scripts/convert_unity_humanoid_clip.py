#!/usr/bin/env python3
"""Bake Unity Avatar rotations onto the canonical rig while preserving bone lengths.

Only the root receives positional animation. Descendants keep their canonical
local bind translations and unit scale. Per-bone world-position offsets do not
describe a rigid skeleton and must never become local translation animation.
"""
import argparse, hashlib, json, math, struct
from pathlib import Path

def parse_glb(path):
    data = Path(path).read_bytes()
    if data[:4] != b'glTF': raise ValueError('Expected GLB')
    version, length = struct.unpack_from('<II', data, 4)
    if version != 2 or length != len(data): raise ValueError('Unsupported GLB header')
    offset, document, binary = 12, None, None
    while offset < len(data):
        size, kind = struct.unpack_from('<II', data, offset); offset += 8
        chunk = data[offset:offset+size]; offset += size
        if kind == 0x4E4F534A: document = json.loads(chunk.decode('utf-8'))
        elif kind == 0x004E4942: binary = chunk
    if document is None or binary is None: raise ValueError('GLB needs JSON and BIN chunks')
    return document, binary

def emit_glb(document, binary, path):
    document['buffers'][0]['byteLength'] = len(binary)
    j = json.dumps(document, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
    j += b' ' * ((-len(j)) % 4)
    binary += b'\0' * ((-len(binary)) % 4)
    total = 12 + 8 + len(j) + 8 + len(binary)
    out = bytearray(struct.pack('<4sII', b'glTF', 2, total))
    out += struct.pack('<II', len(j), 0x4E4F534A) + j
    out += struct.pack('<II', len(binary), 0x004E4942) + binary
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    # Windows asset readers can retain a mapped source briefly after cooking.
    # Publish a complete file rather than truncating that mapped generation.
    target=Path(path);temporary=target.with_suffix(target.suffix+'.tmp')
    temporary.write_bytes(out);temporary.replace(target)

def qmul(a,b):
    ax,ay,az,aw=a; bx,by,bz,bw=b
    return [aw*bx+ax*bw+ay*bz-az*by, aw*by+ay*bw+az*bx-ax*bz,
            aw*bz+az*bw+ax*by-ay*bx, aw*bw-ax*bx-ay*by-az*bz]

def qinv(q): return [-q[0],-q[1],-q[2],q[3]]
def qnorm(q):
    d=math.sqrt(sum(x*x for x in q))
    if not math.isfinite(d) or d < 1e-10: raise ValueError('Invalid quaternion')
    return [x/d for x in q]
def qrot(q,v):
    x,y,z=q[:3]; w=q[3]
    # v + 2*w*(q.xyz cross v) + 2*(q.xyz cross (q.xyz cross v))
    a=[y*v[2]-z*v[1], z*v[0]-x*v[2], x*v[1]-y*v[0]]
    b=[y*a[2]-z*a[1], z*a[0]-x*a[2], x*a[1]-y*a[0]]
    return [v[i]+2*w*a[i]+2*b[i] for i in range(3)]
def qunity_to_gltf(q): return qnorm([q[0],-q[1],-q[2],q[3]])
def punity_to_gltf(p): return [-p[0],p[1],p[2]]
def xyz_add(a,b): return [a[i]+b[i] for i in range(3)]
def xyz_sub(a,b): return [a[i]-b[i] for i in range(3)]

def convert(sample_json, output_glb, skeleton=Path('content/animation/canonical_human.daskeleton'), template=Path('content/animation/blink/punch-right.glb')):
    sample_json,output_glb,skeleton,template=map(Path,(sample_json,output_glb,skeleton,template))
    sample=json.loads(sample_json.read_text(encoding='utf-8'))
    rig=json.loads(skeleton.read_text(encoding='utf-8'))
    doc,binary=parse_glb(template)
    if len(doc.get('nodes',[]))!=len(rig['joints']) or doc.get('meshes') or doc.get('skins'):
        raise ValueError('Clip template does not match the canonical meshless skeleton profile')
    key_to_index={joint['key']:i for i,joint in enumerate(rig['joints'])}
    node_to_index={node['name']:i for i,node in enumerate(doc['nodes'])}
    if set(key_to_index)!=set(node_to_index): raise ValueError('Template joint names differ from canonical skeleton')
    if sample['sampleRate']!=60 or not 2<=sample['frameCount']<=601: raise ValueError('Unity samples must be a 60 Hz clip with 2 to 601 frames')
    source_tracks={track['key']:track for track in sample['tracks']}
    count=sample['frameCount']; joints=rig['joints']; n=len(joints)
    explicit_motion='actorPositions' in sample
    actor_rest_p=punity_to_gltf(sample.get('restActorPosition',[0,0,0]))
    actor_rest_q=qunity_to_gltf(sample.get('restActorRotation',[0,0,0,1]))
    actor_p=[punity_to_gltf(sample['actorPositions'][i*3:i*3+3]) for i in range(count)] if explicit_motion else [[0,0,0] for _ in range(count)]
    actor_q=[qunity_to_gltf(sample['actorRotations'][i*4:i*4+4]) for i in range(count)] if explicit_motion else [[0,0,0,1] for _ in range(count)]
    parent=[]; rest_p=[]; rest_q=[]
    for j in joints:
        parent.append(-1 if j['parent'] is None else key_to_index[j['parent']])
        rest_p.append([float(x) for x in j['translation']]); rest_q.append(qnorm([float(x) for x in j['rotation']]))
    rest_wp=[[0.,0.,0.] for _ in range(n)]; rest_wq=[[0.,0.,0.,1.] for _ in range(n)]
    for i in range(n):
        p=parent[i]
        if p<0: rest_wp[i],rest_wq[i]=rest_p[i],rest_q[i]
        else:
            rest_wq[i]=qnorm(qmul(rest_wq[p],rest_q[i]))
            rest_wp[i]=xyz_add(rest_wp[p],qrot(rest_wq[p],rest_p[i]))
    track_can={}
    for key,track in source_tracks.items():
        if key not in key_to_index: continue
        i=key_to_index[key]
        srp=punity_to_gltf(track['restWorldPosition']); srq=qunity_to_gltf(track['restWorldRotation'])
        if explicit_motion:
            srp=qrot(qinv(actor_rest_q),xyz_sub(srp,actor_rest_p));srq=qnorm(qmul(qinv(actor_rest_q),srq))
        poses=[]
        for frame in range(count):
            p=frame*3; q=frame*4
            sp=punity_to_gltf(track['worldPositions'][p:p+3]); sq=qunity_to_gltf(track['worldRotations'][q:q+4])
            if explicit_motion:
                sp=qrot(qinv(actor_q[frame]),xyz_sub(sp,actor_p[frame]));sq=qnorm(qmul(qinv(actor_q[frame]),sq))
            dq=qnorm(qmul(sq,qinv(srq)))
            wp=xyz_add(rest_wp[i],xyz_sub(sp,srp))
            wq=qnorm(qmul(dq,rest_wq[i]))
            poses.append((wp,wq))
        track_can[i]=poses
    if len(track_can)<40: raise ValueError(f'Only {len(track_can)} canonical joints mapped from Unity')
    times=[i/60.0 for i in range(count)]
    values_p=[]; values_q=[]
    for frame in range(count):
        wp=[[0.,0.,0.] for _ in range(n)]; wq=[[0.,0.,0.,1.] for _ in range(n)]
        for i in range(n):
            p=parent[i]
            if p<0: wp[i],wq[i]=rest_wp[i],rest_wq[i]
            else:
                wq[i]=qnorm(qmul(wq[p],rest_q[i]));wp[i]=xyz_add(wp[p],qrot(wq[p],rest_p[i]))
            if i in track_can: wp[i],wq[i]=track_can[i][frame]
        lp=[[0.,0.,0.] for _ in range(n)];lq=[[0.,0.,0.,1.] for _ in range(n)]
        for i in range(n):
            p=parent[i]
            if p<0: lp[i],lq[i]=wp[i],wq[i]
            else:
                inv=qinv(wq[p]); lq[i]=qnorm(qmul(inv,wq[i]));lp[i]=list(rest_p[i])
            if values_q and sum(a*b for a,b in zip(values_q[-1][i],lq[i]))<0:
                lq[i]=[-v for v in lq[i]]
            if not all(math.isfinite(v) and abs(v)<=100 for v in lp[i]):
                raise ValueError('Non-finite or out-of-bounds root translation')
        values_p.append(lp); values_q.append(lq)
    # Append a regular 60 Hz time grid and complete linear translation/rotation channels for all 81 joints.
    pad=lambda b: b+b'\0'*((-len(b))%4)
    binary=b''; views=[];accessors=[];doc['bufferViews']=views;doc['accessors']=accessors;samplers=[];channels=[]
    def accessor(raw,typ,components,minimum=None,maximum=None):
        nonlocal binary
        binary=pad(binary);offset=len(binary);binary+=raw
        vi=len(views);views.append({'buffer':0,'byteOffset':offset,'byteLength':len(raw)})
        out={'bufferView':vi,'componentType':5126,'count':count,'type':typ}
        if minimum is not None:out['min']=minimum
        if maximum is not None:out['max']=maximum
        ai=len(accessors);accessors.append(out);return ai
    time_bytes=struct.pack('<'+'f'*count,*times); time_accessor=accessor(time_bytes,'SCALAR',1,[0.],[times[-1]])
    for name,node_index in sorted(((name,index) for name,index in node_to_index.items()),key=lambda kv:kv[1]):
        joint=key_to_index[name]
        pos_flat=[v for frame in values_p for v in frame[joint]]
        rot_flat=[v for frame in values_q for v in frame[joint]]
        pa=accessor(struct.pack('<'+'f'*len(pos_flat),*pos_flat),'VEC3',3)
        qa=accessor(struct.pack('<'+'f'*len(rot_flat),*rot_flat),'VEC4',4)
        for path,out in [('translation',pa),('rotation',qa)]:
            si=len(samplers);samplers.append({'input':time_accessor,'output':out,'interpolation':'LINEAR'})
            channels.append({'sampler':si,'target':{'node':node_index,'path':path}})
    doc['animations']=[{'name':sample['name'],'samplers':samplers,'channels':channels}]
    if explicit_motion:
        roots=[];previous=None;first_yaw=0
        for frame in range(count):
            forward=qrot(actor_q[frame],[0,0,1]);yaw=math.atan2(forward[0],forward[2])
            if previous is not None:yaw=previous+math.remainder(yaw-previous,2*math.pi)
            else:first_yaw=yaw
            previous=yaw;delta=xyz_sub(actor_p[frame],actor_p[0]);c,s=math.cos(first_yaw),math.sin(first_yaw)
            roots.append([c*delta[0]-s*delta[2],delta[1],s*delta[0]+c*delta[2],yaw-first_yaw])
        doc['extras']['darkangel_motion_root']=roots
    else:doc['extras'].pop('darkangel_motion_root',None)
    doc['asset']['generator']='DarkAngel Unity Avatar retarget v2 (rigid bone lengths)'
    emit_glb(doc,binary,output_glb)
    report=dict(schema=2,source=sample.get('sourcePath'),source_guid=sample.get('sourceGuid'),
        source_local_id=sample.get('sourceLocalId'),source_clip=sample['name'],ticks=count-1,joints=n,
        canonical_sha256=hashlib.sha256(skeleton.read_bytes()).hexdigest(),
        glb_sha256=hashlib.sha256(output_glb.read_bytes()).hexdigest(),
        translation_policy='root-only; canonical local bind translation for every descendant',
        max_descendant_translation_error_metres=0,animated_scale=False,
        motion_root='explicit Unity Animator trajectory; hips remain in skeletal pose' if explicit_motion else 'legacy hips extraction')
    output_glb.with_suffix('.conversion.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('sample_json',type=Path);ap.add_argument('output_glb',type=Path)
    ap.add_argument('--skeleton',type=Path,default=Path('content/animation/canonical_human.daskeleton'))
    ap.add_argument('--template',type=Path,default=Path('content/animation/blink/punch-right.glb'))
    args=ap.parse_args()
    print(json.dumps(convert(args.sample_json,args.output_glb,args.skeleton,args.template)))

if __name__=='__main__':main()
