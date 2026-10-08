"""Reproduce scoped Omni FBX conversion/native cook acceptance with hash fences."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

from msvc_environment import activate

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--convert', action='store_true')
args = parser.parse_args()
source_root = Path(r'C:\Unity Projects\AshenRootsMP\Assets\thirdparty\Animations\Opsive\OmniAnimation\Packs\CoreLocomotion\Animations')
blender = r'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe'
evidence = ROOT / 'docs/implementation/evidence'
env = activate()
gates = []

def run(name, command, expected=0, timeout=120):
    start = time.monotonic()
    result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)
    log = evidence / ('m5-omni-' + name + '.log')
    log.write_text(result.stdout + result.stderr, encoding='utf8')
    gates.append(dict(name=name, exit=result.returncode, expected=expected, seconds=round(time.monotonic()-start, 3), log=log.relative_to(ROOT).as_posix()))
    print(name, result.returncode, flush=True)
    if result.returncode != expected:
        raise RuntimeError((result.stdout + result.stderr)[-3000:])

digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
fixtures = {}
for name, source in [('omni-walk', 'WalkForward'), ('omni-run', 'RunForward'), ('omni-left', 'WalkStrafeLeft'), ('omni-right', 'WalkStrafeRight')]:
    original = source_root / 'TPose' / (source + '.fbx')
    before = digest(original)
    output = ROOT / ('.cache/fixtures/' + name + '.glb')
    if args.convert:
        run('convert-' + name, [blender, '--background', '--factory-startup', '--python-exit-code', '1', '--python', 'scripts/convert_omni_fixture.py', '--', original, 'content/animation/canonical_human.daskeleton', output])
    report = json.loads(output.with_suffix('.conversion.json').read_text())
    if digest(original) != before or report['source_sha256'] != before or report['glb_sha256'] != digest(output) or report['canonical_sha256'] != digest(ROOT / 'content/animation/canonical_human.daskeleton'):
        raise RuntimeError('Original/derived/canonical hash fence: ' + name)
    fixtures[name] = report
run('original-profile-reject', [blender, '--background', '--factory-startup', '--python-exit-code', '1', '--python', 'scripts/convert_omni_fixture.py', '--', source_root/'Original/WalkForward.fbx', 'content/animation/canonical_human.daskeleton', '.cache/fixtures/omni-invalid.glb'], expected=1)
if (ROOT / '.cache/fixtures/omni-invalid.glb').exists():
    raise RuntimeError('Rejected profile published output')
cmake = ROOT / '.tools/cmake/cmake-4.4.4-windows-x86_64/bin'
run('build', [cmake/'cmake.exe', '--build', 'build/m5-relwithdebinfo', '--parallel', '2'])
run('focused', [ROOT/'build/m5-relwithdebinfo/AnimationClipTests.exe', '--omni'])
run('ctest', [cmake/'ctest.exe', '--test-dir', 'build/m5-relwithdebinfo', '--output-on-failure'])
paths = ['scripts/convert_omni_fixture.py', 'scripts/inspect_animation_fbx.py', 'scripts/verify_omni_clips.py', 'tests/animation_clip_tests.cpp', 'CMakeLists.txt', 'content/animation/canonical_human.daskeleton']
receipt = dict(schema=1, base_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
               scope='Four offline Omni canonical locomotion clips; native cook/sampling and initial-yaw root direction normalization. Visual/graph/authority gates remain open.',
               error_profile=dict(blink_socket_tip_metres=.0005, omni_socket_tip_metres=.00075, masked_blend_metres=.0005),
               online='Live EOS blocked; no new online claim', fixtures=fixtures, gates=gates,
               source_sha256={path: digest(ROOT/path) for path in paths},
               binary_sha256=digest(ROOT/'build/m5-relwithdebinfo/AnimationClipTests.exe'))
(evidence/'m5-omni.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf8')
