"""Verify the initial local WorldSession/motor/graph scene coordinator."""
import hashlib
import json
from pathlib import Path
import subprocess
import time
from msvc_environment import activate

ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin'
gates=[]
def run(name, command, timeout=180):
    start=time.monotonic()
    result=subprocess.run(list(map(str,command)),cwd=ROOT,env=activate(),capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
    log=EVIDENCE/('character-scene-'+name+'.log');log.write_text(result.stdout+result.stderr,encoding='utf8')
    gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
    print(name,result.returncode,flush=True)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-4000:])
run('build',[CMAKE/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'],900)
run('focused',[ROOT/'build/m5-relwithdebinfo/CharacterSceneTests.exe'])
run('ctest',[CMAKE/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'])
files=['engine/runtime/character_scene.cpp','engine/runtime/include/darkangel/character_scene.hpp','tests/character_scene_tests.cpp','CMakeLists.txt']
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    scope='Initial local listen-host coordinator through existing WorldSession wire2; static collision/upright unscaled live human/standing locomotion. Prepared collision ACK, owner correction, bounded topology baseline replacement, achieved-movement graph and fixed render-rate poses. Royal editor controls, combat, external-provider scene launch, posture and streaming remain separate.',
    gates=gates,source_sha256={path:digest(ROOT/path) for path in files},binary_sha256=digest(ROOT/'build/m5-relwithdebinfo/CharacterSceneTests.exe'),online='EOS remains blocked; previous independent GNS receipts unchanged')
(EVIDENCE/'character-scene.json').write_text(json.dumps(receipt,indent=2)+'\n')
