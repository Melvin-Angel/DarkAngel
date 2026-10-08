"""Scoped offline Omni CoreLocomotion TPose FBX conversion; no runtime mapping.

Blender arguments: -- INPUT.fbx canonical.daskeleton OUTPUT.glb
The first TPose frame is a reference, never part of playback. Preserve canonical
bone lengths, fill absent face/item joints at rest and bake source world rotation
deltas onto the canonical rest basis. Visual motion acceptance remains separate.
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

source, canonical, output = map(Path, sys.argv[sys.argv.index('--') + 1:])
workspace = Path(__file__).resolve().parents[1]
if not output.resolve().is_relative_to(workspace) or output.suffix.lower() != '.glb':
    raise RuntimeError('Derived GLB must stay inside the owned workspace')
if source.parent.name != 'TPose' or source.parent.parent.parent.name != 'CoreLocomotion':
    raise RuntimeError('Only Omni CoreLocomotion/Animations/TPose profile is supported')
before = hashlib.sha256(source.read_bytes()).hexdigest()
definition = json.loads(canonical.read_text())
joints = definition['joints']
keys = {joint['key']: index for index, joint in enumerate(joints)}
mapping = dict(Root_M='Hips', Spine1_M='Spine', Spine2_M='Spine1', Chest_M='Spine3',
               Neck_M='Neck', Head_M='Head', HeadEnd_M='Head_End')
expected = dict(Hips=None, Spine='Hips', Spine1='Spine', Spine2='Spine1',
                Spine3='Spine2', Neck='Spine3', Head='Neck', Head_End='Head')
for side, prefix in [('L', 'Left'), ('R', 'Right')]:
    names = [('Hip', 'Thigh', 'Hips'), ('Knee', 'Shin', prefix + 'Thigh'),
             ('Ankle', 'Foot', prefix + 'Shin'), ('Toes', 'Toe', prefix + 'Foot'),
             ('ToesEnd', 'Toe_End', prefix + 'Toe'), ('Scapula', 'Shoulder', 'Spine3'),
             ('Shoulder', 'Arm', prefix + 'Shoulder'), ('Elbow', 'Forearm', prefix + 'Arm'),
             ('Wrist', 'Hand', prefix + 'Forearm')]
    for target, name, parent in names:
        mapping[target + '_' + side] = prefix + name
        expected[prefix + name] = parent
    for finger, number in [('Thumb', 1), ('Index', 2), ('Middle', 3), ('Ring', 4), ('Pinky', 5)]:
        for segment in range(1, 5):
            name = prefix + 'Finger' + str(number) + '_' + ('3_End' if segment == 4 else str(segment))
            mapping[finger + 'Finger' + str(segment) + '_' + side] = name
            expected[name] = prefix + 'Hand' if segment == 1 else prefix + 'Finger' + str(number) + '_' + str(segment - 1)
rest_only = {'Eye_L', 'EyeEnd_L', 'Eye_R', 'EyeEnd_R', 'Jaw_M', 'JawEnd_M',
             'jnt_eyeBrow_L', 'jnt_eyeBrow_LEnd_L', 'jnt_eyeBrow_R', 'jnt_eyeBrowEnd_R',
             'jnt_eyelid_L', 'jnt_eyelid_LEnd_L', 'jnt_eyelid_R', 'jnt_eyelidEnd_R',
             'jointItemL', 'jointItemR'}
if set(keys) != set(mapping) | rest_only:
    raise RuntimeError('Unsupported canonical rig profile')
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(source))
rigs = [obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE']
if len(rigs) != 1:
    raise RuntimeError('Exactly one source armature required')
rig = rigs[0]
parents = {bone.name: bone.parent.name if bone.parent else None for bone in rig.data.bones}
if parents != expected:
    raise RuntimeError('Omni source joint/hierarchy profile changed')
actions = list(bpy.data.actions)
if len(actions) != 1 or not rig.animation_data or rig.animation_data.action != actions[0]:
    raise RuntimeError('Exactly one active source action required')
action = actions[0]
reference, end = action.frame_range
start = reference + 1
fps = bpy.context.scene.render.fps / bpy.context.scene.render.fps_base
if fps != 30 or not 1 < end - start <= 300:
    raise RuntimeError('Unsupported source timing')
axis = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))

def poses(frame):
    bpy.context.scene.frame_set(math.floor(frame), subframe=frame % 1)
    evaluated = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    result = {}
    for name in expected:
        t, q, s = (axis @ evaluated.matrix_world @ evaluated.pose.bones[name].matrix).decompose()
        if not all(math.isfinite(value) for value in list(t) + list(q) + list(s)) or min(s) <= 0 or max(s) - min(s) > 1e-5:
            raise RuntimeError('Unsupported source pose/scale')
        q.normalize()
        result[name] = (t.copy(), q.copy())
    return result

bind = poses(reference)
left, right, hips = [bind[name][0] for name in ['LeftHand', 'RightHand', 'Hips']]
if left.x - right.x < 1 or min(left.y, right.y) - hips.y < .3 or abs(left.z - right.z) > .1:
    raise RuntimeError('First frame is not the supported authored T-pose reference')
locals_rest, worlds_rest = [], []
for joint in joints:
    q = joint['rotation']
    local = Matrix.Translation(Vector(joint['translation'])) @ Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4()
    locals_rest.append(local)
    worlds_rest.append(worlds_rest[keys[joint['parent']]] @ local if joint['parent'] else local)

def convert(source_pose):
    worlds, locals_ = [], []
    for index, joint in enumerate(joints):
        parent = keys[joint['parent']] if joint['parent'] else None
        if joint['key'] in rest_only:
            local = locals_rest[index].copy()
            world = worlds[parent] @ local
        else:
            name = mapping[joint['key']]
            rotation = source_pose[name][1] @ bind[name][1].inverted() @ worlds_rest[index].to_quaternion()
            rotation.normalize()
            translation = worlds[parent] @ Vector(joint['translation']) if parent is not None else worlds_rest[index].translation + source_pose[name][0] - bind[name][0]
            world = Matrix.Translation(translation) @ rotation.to_matrix().to_4x4()
            local = worlds[parent].inverted() @ world if parent is not None else world
        worlds.append(world)
        locals_.append(local)
    return locals_

reference_error = max(abs(actual[col][row] - rest[col][row]) for actual, rest in zip(convert(bind), locals_rest) for col in range(4) for row in range(4))
if reference_error > 1e-5:
    raise RuntimeError('Offline reference does not reproduce canonical rest')
ticks = round((end - start) * 60 / fps)
initial_root = convert(poses(start))[0]
initial_delta = initial_root.to_quaternion() @ worlds_rest[0].to_quaternion().inverted()
if initial_delta.y * initial_delta.y + initial_delta.w * initial_delta.w <= 1e-5:
    raise RuntimeError('Initial root yaw is singular')
initial_yaw = 2 * math.atan2(initial_delta.y, initial_delta.w)
origin = initial_root.translation.copy()
tracks = [dict(translation=[], rotation=[]) for joint in joints]
for tick in range(ticks + 1):
    local_pose = convert(poses(start + (end - start) * tick / ticks))
    # Omni's directional grid is defined in its authored reference axes, while
    # the native cooker expresses motion relative to the initial root yaw.
    # Bake the inverse basis change into translations so the final cooked path
    # retains that grid even when the first gait pose twists the pelvis.
    delta = local_pose[0].translation - origin
    c, s = math.cos(initial_yaw), math.sin(initial_yaw)
    local_pose[0].translation = origin + Vector((c * delta.x + s * delta.z, delta.y, -s * delta.x + c * delta.z))
    for index, local in enumerate(local_pose):
        t, q, s = local.decompose()
        q.normalize()
        values = [q.x, q.y, q.z, q.w]
        previous = tracks[index]['rotation']
        if previous and sum(a * b for a, b in zip(previous[-1], values)) < 0:
            values = [-value for value in values]
        if max(abs(t[n] - joints[index]['translation'][n]) for n in range(3)) > 1e-5 and index:
            raise RuntimeError('Canonical bone lengths changed')
        tracks[index]['translation'].append(list(t))
        tracks[index]['rotation'].append(values)
binary, views, accessors = bytearray(), [], []

def accessor(values, width):
    flat = [n for value in values for n in value] if width > 1 else values
    offset = len(binary)
    binary.extend(struct.pack('<' + 'f' * len(flat), *flat))
    views.append(dict(buffer=0, byteOffset=offset, byteLength=len(flat) * 4))
    item = dict(bufferView=len(views) - 1, componentType=5126, count=len(values), type={1: 'SCALAR', 3: 'VEC3', 4: 'VEC4'}[width])
    if width == 1:
        item.update(min=[min(values)], max=[max(values)])
    accessors.append(item)
    return len(accessors) - 1

times = accessor([tick / 60 for tick in range(ticks + 1)], 1)
nodes, samplers, channels = [], [], []
for index, joint in enumerate(joints):
    node = dict(name=joint['key'], translation=joint['translation'], rotation=joint['rotation'], scale=[1, 1, 1])
    children = [n for n, child in enumerate(joints) if child['parent'] == joint['key']]
    if children:
        node['children'] = children
    nodes.append(node)
    for path, width in [('translation', 3), ('rotation', 4)]:
        samplers.append(dict(input=times, output=accessor(tracks[index][path], width), interpolation='LINEAR'))
        channels.append(dict(sampler=len(samplers) - 1, target=dict(node=index, path=path)))
gltf = dict(asset=dict(version='2.0', generator='DarkAngel scoped offline Omni conversion'), scene=0,
            scenes=[dict(nodes=[0])], nodes=nodes, buffers=[dict(byteLength=len(binary))],
            bufferViews=views, accessors=accessors, animations=[dict(name=source.stem, samplers=samplers, channels=channels)],
            extras=dict(darkangel_axes='right-handed-y-up-metres', darkangel_canonical=definition['asset']))
document = json.dumps(gltf, separators=(',', ':')).encode()
document += b' ' * (-len(document) % 4)
binary += b'\0' * (-len(binary) % 4)
blob = struct.pack('<III', 0x46546c67, 2, 28 + len(document) + len(binary)) + struct.pack('<II', len(document), 0x4e4f534a) + document + struct.pack('<II', len(binary), 0x004e4942) + binary
if hashlib.sha256(source.read_bytes()).hexdigest() != before:
    raise RuntimeError('Original FBX changed')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_bytes(blob)
report = dict(source=str(source), source_sha256=before, canonical_sha256=hashlib.sha256(canonical.read_bytes()).hexdigest(),
              glb_sha256=hashlib.sha256(blob).hexdigest(), blender=bpy.app.version_string,
              source_frames=[start, end], source_fps=fps, reference_frame=reference, ticks=ticks,
              joints=len(joints), fixed_offline_profile=mapping, rest_only=sorted(rest_only),
              collapsed_source_joint='Spine2', reference_error=reference_error,
              normalized_initial_yaw=initial_yaw,
              scope='Offline fixture only; visual quality, foot sliding and gameplay graph/root integration not qualified')
output.with_suffix('.conversion.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
print('OMNI_CONVERTED', output, 'ticks=', ticks, 'reference_error=', reference_error)
