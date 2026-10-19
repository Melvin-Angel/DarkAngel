"""Regression for the world-position transfer that stretched retargeted limbs."""
import copy
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from convert_unity_humanoid_clip import convert, parse_glb, qmul, qrot, xyz_add


def values(document, binary, channel):
    accessor = document['accessors'][document['animations'][0]['samplers'][channel['sampler']]['output']]
    view = document['bufferViews'][accessor['bufferView']]
    width = {'VEC3': 3, 'VEC4': 4}[accessor['type']]
    return struct.unpack_from('<' + 'f'*(width*accessor['count']), binary,
                              view.get('byteOffset', 0) + accessor.get('byteOffset', 0))


class RetargetTests(unittest.TestCase):
    def setUp(self):
        self.rig = json.loads((ROOT / 'content/animation/canonical_human.daskeleton').read_text())
        world = {}
        tracks = []
        delta = [0, math.sin(.2), 0, math.cos(.2)]
        for joint in self.rig['joints']:
            p, q = joint['translation'], joint['rotation']
            if joint['parent']:
                pp, pq = world[joint['parent']]
                p, q = xyz_add(pp, qrot(pq, p)), qmul(pq, q)
            world[joint['key']] = p, q
            unity_p = [-p[0], p[1], p[2]]
            unity_q = [q[0], -q[1], -q[2], q[3]]
            moved_q = qmul(delta, q)
            moved_q = [moved_q[0], -moved_q[1], -moved_q[2], moved_q[3]]
            # Position noise/proportion changes must not stretch any descendant.
            moved_p = [-p[0]*1.7, p[1]*.6, p[2]*2]
            if not joint['parent']:
                moved_p = [unity_p[0]-.25, unity_p[1]+.1, unity_p[2]-.2]
            tracks.append(dict(key=joint['key'], restWorldPosition=unity_p, restWorldRotation=unity_q,
                               worldPositions=unity_p+moved_p, worldRotations=unity_q+moved_q))
        self.sample = dict(name='proportion regression', sampleRate=60, frameCount=2, tracks=tracks)

    def bake(self, sample):
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder)/'sample.json', Path(folder)/'clip.glb'
            source.write_text(json.dumps(sample))
            convert(source, target, ROOT/'content/animation/canonical_human.daskeleton',
                    ROOT/'content/animation/blink/punch-right.glb')
            return parse_glb(target)

    def test_root_motion_survives_but_every_descendant_keeps_bind_length(self):
        document, binary = self.bake(self.sample)
        for channel in document['animations'][0]['channels']:
            if channel['target']['path'] != 'translation':
                continue
            joint = self.rig['joints'][channel['target']['node']]
            data = values(document, binary, channel)
            for frame in range(2):
                expected = list(joint['translation'])
                if not joint['parent'] and frame:
                    expected = [expected[0]+.25, expected[1]+.1, expected[2]-.2]
                for actual, wanted in zip(data[frame*3:frame*3+3], expected):
                    self.assertAlmostEqual(actual, wanted, places=6, msg=joint['key'])

    def test_shared_world_rotation_changes_only_root_local_rotation(self):
        document, binary = self.bake(self.sample)
        for channel in document['animations'][0]['channels']:
            if channel['target']['path'] != 'rotation':
                continue
            joint = self.rig['joints'][channel['target']['node']]
            data = values(document, binary, channel)
            dot = abs(sum(a*b for a, b in zip(data[4:], joint['rotation'])))
            if joint['parent']:
                self.assertAlmostEqual(dot, 1, places=5, msg=joint['key'])
            else:
                self.assertLess(dot, .999)

    def test_equivalent_quaternion_signs_do_not_create_interpolation_flips(self):
        sample = copy.deepcopy(self.sample)
        for track in sample['tracks']:
            q = track['restWorldRotation']
            track['worldRotations'] = q + [-v for v in q]
        document, binary = self.bake(sample)
        for channel in document['animations'][0]['channels']:
            if channel['target']['path'] == 'rotation':
                data = values(document, binary, channel)
                self.assertGreater(sum(a*b for a, b in zip(data[:4], data[4:])), .9999)

    def test_actor_trajectory_is_separate_from_rigid_skeletal_pose(self):
        sample = copy.deepcopy(self.sample)
        actor_q = [0, math.sin(.2), 0, math.cos(.2)]
        actor_p = [.25, .1, -.2]
        sample.update(restActorPosition=[0,0,0], restActorRotation=[0,0,0,1],
                      actorPositions=[0,0,0,-.25,.1,-.2],
                      actorRotations=[0,0,0,1,0,-math.sin(.2),0,math.cos(.2)])
        for track in sample['tracks']:
            p = track['restWorldPosition']
            world = xyz_add(actor_p, qrot(actor_q, [-p[0],p[1],p[2]]))
            track['worldPositions'][3:] = [-world[0],world[1],world[2]]
        document, binary = self.bake(sample)
        for actual, expected in zip(document['extras']['darkangel_motion_root'][1], actor_p+[.4]):
            self.assertAlmostEqual(actual, expected, places=6)
        for channel in document['animations'][0]['channels']:
            joint = self.rig['joints'][channel['target']['node']]
            data = values(document, binary, channel)
            if channel['target']['path'] == 'translation':
                for actual, expected in zip(data[3:],joint['translation']):
                    self.assertAlmostEqual(actual,expected,places=6)
            else:
                self.assertAlmostEqual(abs(sum(a*b for a,b in zip(data[4:],joint['rotation']))),1,places=5)


if __name__ == '__main__':
    unittest.main()
