"""Independent GLB bone-length and Unity socket-position audit after reimport."""
import argparse
import json
import math
from pathlib import Path
import struct

from convert_unity_humanoid_clip import parse_glb, punity_to_gltf, qunity_to_gltf, qmul, qinv, qrot, xyz_add, xyz_sub

ROOT = Path(__file__).resolve().parents[1]
PROBES = {'Wrist_L', 'Wrist_R', 'Ankle_L', 'Ankle_R', 'Head_M'}


def accessor(document, binary, index, width):
    entry = document['accessors'][index]
    view = document['bufferViews'][entry['bufferView']]
    return struct.unpack_from('<' + 'f'*(entry['count']*width), binary,
                              view.get('byteOffset', 0) + entry.get('byteOffset', 0))


def audit_clip(path, sample_path, rig):
    document, binary = parse_glb(path)
    sample = json.loads(Path(sample_path).read_text())
    joints = rig['joints']
    indices = {j['key']: i for i, j in enumerate(joints)}
    parents = [-1 if j['parent'] is None else indices[j['parent']] for j in joints]
    channels = {}
    max_translation, max_norm, max_length = 0., 0., 0.
    for channel in document['animations'][0]['channels']:
        key = document['nodes'][channel['target']['node']]['name']
        kind = channel['target']['path']
        if kind not in ('translation', 'rotation'):
            raise ValueError(f'Animated scale/unsupported channel in {path}')
        output = document['animations'][0]['samplers'][channel['sampler']]['output']
        data = accessor(document, binary, output, 3 if kind == 'translation' else 4)
        if not all(math.isfinite(v) for v in data):
            raise ValueError(f'Non-finite animation data in {path}')
        channels[key, kind] = data
        joint = joints[indices[key]]
        if kind == 'translation' and joint['parent']:
            rest_length = math.sqrt(sum(v*v for v in joint['translation']))
            for offset in range(0, len(data), 3):
                values = data[offset:offset+3]
                max_translation = max(max_translation, math.dist(values, joint['translation']))
                if rest_length > .001:
                    max_length = max(max_length, abs(math.sqrt(sum(v*v for v in values))/rest_length - 1))
        elif kind == 'rotation':
            max_norm = max(max_norm, max(abs(sum(v*v for v in data[i:i+4])-1) for i in range(0,len(data),4)))
    source_tracks = {t['key']: t for t in sample['tracks']}
    actor_rest_p = punity_to_gltf(sample['restActorPosition'])
    actor_rest_q = qunity_to_gltf(sample['restActorRotation'])
    source_rest = qrot(qinv(actor_rest_q), xyz_sub(punity_to_gltf(source_tracks[joints[0]['key']]['restWorldPosition']),actor_rest_p))
    offset = xyz_sub(joints[0]['translation'], source_rest)
    max_probe_error, worst_probe = 0., None
    for frame in sorted(set(range(0,sample['frameCount'],4)) | {sample['frameCount']-1}):
        wp, wq = [], []
        actor_p = punity_to_gltf(sample['actorPositions'][frame*3:frame*3+3])
        actor_q = qunity_to_gltf(sample['actorRotations'][frame*4:frame*4+4])
        for i, joint in enumerate(joints):
            t = channels[joint['key'], 'translation'][frame*3:frame*3+3]
            q = channels[joint['key'], 'rotation'][frame*4:frame*4+4]
            p = parents[i]
            wp.append(xyz_add(wp[p],qrot(wq[p],t)) if p>=0 else t)
            wq.append(qmul(wq[p],q) if p>=0 else q)
            if joint['key'] in PROBES:
                source = source_tracks[joint['key']]['worldPositions'][frame*3:frame*3+3]
                expected = xyz_add(offset,qrot(qinv(actor_q),xyz_sub(punity_to_gltf(source),actor_p)))
                error = math.dist(wp[i],expected)
                if error > max_probe_error:
                    max_probe_error, worst_probe = error, dict(bone=joint['key'],frame=frame)
    return dict(path=str(path), frames=sample['frameCount'], max_descendant_translation_error_metres=max_translation,
                max_bone_length_relative_error=max_length, max_quaternion_squared_norm_error=max_norm,
                max_unity_socket_position_error_metres=max_probe_error, worst_probe=worst_probe)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,default=ROOT/'.cache/animation-reimport')
    parser.add_argument('--progress',action='store_true',help='Audit completed clips while a bulk import is running')
    args = parser.parse_args()
    plan = json.loads((args.work/'plan.json').read_text())
    manifest_path=args.work/'import-progress.json' if args.progress else ROOT/'content/animation/unity/clips.json'
    manifest = json.loads(manifest_path.read_text())
    accepted = {c['path'] for c in manifest['clips']}
    rig = json.loads((ROOT/'content/animation/canonical_human.daskeleton').read_text())
    rows = []
    for item in plan['clips']:
        if item['output'] in accepted:
            rows.append(audit_clip(ROOT/'content'/item['output'],item['sample'],rig))
    summary = dict(clips=len(rows), sampled_frames=sum(c['frames'] for c in rows),
        max_descendant_translation_error_metres=max(c['max_descendant_translation_error_metres'] for c in rows),
        max_bone_length_relative_error=max(c['max_bone_length_relative_error'] for c in rows),
        max_quaternion_squared_norm_error=max(c['max_quaternion_squared_norm_error'] for c in rows),
        max_unity_socket_position_error_metres=max(c['max_unity_socket_position_error_metres'] for c in rows),
        worst_socket_clips=sorted(rows,key=lambda c:c['max_unity_socket_position_error_metres'],reverse=True)[:12])
    (args.work/('audit-progress.json' if args.progress else 'audit.json')).write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))
    if summary['max_descendant_translation_error_metres']>1e-6 or summary['max_quaternion_squared_norm_error']>1e-5:
        raise SystemExit('Rigid bone/quaternion audit failed')
    if summary['max_unity_socket_position_error_metres']>1e-4:
        raise SystemExit('Independent Unity pose comparison exceeds 0.1 mm')


if __name__ == '__main__':
    main()
