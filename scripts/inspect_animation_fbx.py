"""Read-only Blender bpy audit of an external animation against the canonical rig.

Usage: blender --background --factory-startup --python-exit-code 1 --python
scripts/inspect_animation_fbx.py -- INPUT.fbx canonical.daskeleton REPORT.json
This inventories source data; it does not retarget or qualify native playback.
"""
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy

source, canonical, output = map(Path, sys.argv[sys.argv.index('--') + 1:])
workspace = Path(__file__).resolve().parents[1]
if not output.resolve().is_relative_to(workspace) or output.suffix != '.json':
    raise RuntimeError('Audit output must be JSON inside the owned workspace')
before = hashlib.sha256(source.read_bytes()).hexdigest()
definition = json.loads(canonical.read_text())
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(source))
rigs = [obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE']
report = dict(source=str(source), source_sha256=before,
              blender=bpy.app.version_string,
              fps=bpy.context.scene.render.fps / bpy.context.scene.render.fps_base,
              qualification='Source audit only; no native cook, playback or motion-quality acceptance',
              rigs=[], actions=[])
for rig in rigs:
    bones = {bone.name: bone.parent.name if bone.parent else None for bone in rig.data.bones}
    expected = {joint['key']: joint['parent'] for joint in definition['joints']}
    report['rigs'].append(dict(name=rig.name, bones=bones,
        missing_canonical=sorted(set(expected) - set(bones)),
        extra_source=sorted(set(bones) - set(expected)),
        hierarchy_mismatch=[name for name in expected if name in bones and bones[name] != expected[name]]))
for action in bpy.data.actions:
    report['actions'].append(dict(name=action.name, frames=list(action.frame_range),
                                  slots=[slot.identifier for slot in action.slots]))
report['source_pose_samples'] = []
for rig in rigs:
    if not rig.animation_data or not rig.animation_data.action:
        continue
    start, end = rig.animation_data.action.frame_range
    for frame in (start, min(start + 1, end), end):
        bpy.context.scene.frame_set(math.floor(frame), subframe=frame % 1)
        evaluated = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
        sample = dict(rig=rig.name, frame=frame, space='Blender world Z-up metres', bones={})
        for bone in evaluated.pose.bones:
            matrix = evaluated.matrix_world @ bone.matrix
            t, q, scale = matrix.decompose()
            values = list(t) + list(q) + list(scale)
            if not all(math.isfinite(value) for value in values):
                raise RuntimeError('Non-finite imported source pose')
            sample['bones'][bone.name] = dict(position=list(t), rotation_wxyz=list(q), scale=list(scale))
        report['source_pose_samples'].append(sample)
if hashlib.sha256(source.read_bytes()).hexdigest() != before:
    raise RuntimeError('Source hash changed during read-only inspection')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
print(json.dumps(dict(report=str(output), rigs=len(rigs), actions=report['actions'],
                     missing=[rig['missing_canonical'] for rig in report['rigs']])))
