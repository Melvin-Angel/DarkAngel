"""Native joint-mask cook and EditorDocument acceptance, with and without Ozz."""
import hashlib
import json
from pathlib import Path
import subprocess
import time
import argparse
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--no-animation-only',action='store_true',help='Retain already successful native logs when repairing only an independent verification command');args=parser.parse_args()
evidence=ROOT/'docs/implementation/evidence';env=activate();gates=[]
def run(name,command,timeout=180):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,timeout=timeout)
    log=evidence/('m5-mask-'+name+'.log');log.write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()));print(name,result.returncode,flush=True)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
def native_snapshot():
    inputs=[ROOT/'CMakeLists.txt',ROOT/'vcpkg.json',ROOT/'cmake/sources.lock.json',ROOT/'build/m5-relwithdebinfo/CMakeCache.txt']
    for folder in ['engine','tools/editor','tests','content/animation','.cache/fixtures']:
        inputs.extend(path for path in (ROOT/folder).rglob('*') if path.is_file() and path.suffix in {'.cpp','.hpp','.damask','.daskeleton','.glb','.json'})
    return dict(inputs={path.relative_to(ROOT).as_posix():digest(path) for path in sorted(set(inputs))},binary=digest(ROOT/'build/m5-relwithdebinfo/JointMaskTests.exe'),logs={name:digest(evidence/('m5-mask-'+name+'.log')) for name in ['build','focused','ctest']})
checkpoint=ROOT/'.cache/m5-mask-native-checkpoint.json'
fixture=json.loads((ROOT/'.cache/fixtures/attack.conversion.json').read_text())
if digest(Path(fixture['source']))!=fixture['source_sha256'] or digest(ROOT/'.cache/fixtures/attack.glb')!=fixture['glb_sha256']:raise RuntimeError('Attack original/derived hash fence')
cmake=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin'
if not args.no_animation_only:
    run('build',[cmake/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'])
    run('focused',[ROOT/'build/m5-relwithdebinfo/JointMaskTests.exe'])
    run('ctest',[cmake/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'])
    checkpoint.write_text(json.dumps(native_snapshot(),indent=2)+'\n')
else:
    if not checkpoint.exists() or json.loads(checkpoint.read_text())!=native_snapshot():raise RuntimeError('Native inputs/binary/logs changed; run full verification')
    if '100% tests passed' not in (evidence/'m5-mask-ctest.log').read_text() or 'passed' not in (evidence/'m5-mask-focused.log').read_text():raise RuntimeError('No successful native logs to retain')
    for name in ['build','focused','ctest']:gates.append(dict(name=name,exit=0,retained=True,log='docs/implementation/evidence/m5-mask-'+name+'.log'))
run('no-animation-build',[cmake/'cmake.exe','--build','build/m4-relwithdebinfo','--target','AssetTests','EditorTests','CollisionAssetTests','--parallel','2'])
run('no-animation-checks',[cmake/'ctest.exe','--test-dir','build/m4-relwithdebinfo','-R','M2.asset_pipeline|M2.editor_transactions|M4.collision_assets','--output-on-failure'])
paths=['engine/runtime/joint_mask.cpp','engine/runtime/include/darkangel/joint_mask.hpp','engine/assets/joint_mask_cooker.cpp','engine/assets/asset_service.cpp','engine/assets/assets_internal.hpp','engine/assets/include/darkangel/animation_assets.hpp','tools/editor/editor_document.cpp','tests/joint_mask_tests.cpp','content/animation/m5_upper_body.damask','CMakeLists.txt','scripts/verify_joint_masks.py']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native stable-key joint masks, frozen mask/rig/archive closure, fresh/warm and failed generation retention, Ozz consumption and atomic source authoring; graph authoring/action/additive/IK remain open',online='EOS live blocked; GNS evidence unchanged',gates=gates,fixture=fixture,source_sha256={p:digest(ROOT/p) for p in paths},binary_sha256=digest(ROOT/'build/m5-relwithdebinfo/JointMaskTests.exe'))
(evidence/'m5-mask.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
