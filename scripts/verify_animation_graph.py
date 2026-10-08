"""Scoped native initial graph acceptance; authoring/action slots remain open."""
import hashlib
import json
from pathlib import Path
import subprocess
import time

from msvc_environment import activate

ROOT=Path(__file__).resolve().parents[1]
evidence=ROOT/'docs/implementation/evidence'
env=activate()
gates=[]
def run(name,command,timeout=120):
    start=time.monotonic()
    result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,timeout=timeout)
    log=evidence/('m5-graph-'+name+'.log');log.write_text(result.stdout+result.stderr,encoding='utf8')
    gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
    print(name,result.returncode,flush=True)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
fixtures={}
for name in ['omni-walk','omni-run','omni-left','omni-right']:
    output=ROOT/('.cache/fixtures/'+name+'.glb');report=json.loads(output.with_suffix('.conversion.json').read_text())
    if digest(Path(report['source']))!=report['source_sha256'] or digest(output)!=report['glb_sha256'] or digest(ROOT/'content/animation/canonical_human.daskeleton')!=report['canonical_sha256']:raise RuntimeError('Fixture hash fence: '+name)
    fixtures[name]=report
cmake=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin'
run('build',[cmake/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'])
run('focused',[ROOT/'build/m5-relwithdebinfo/AnimationGraphTests.exe'])
run('ctest',[cmake/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'])
paths=['engine/runtime/animation_graph.cpp','engine/runtime/include/darkangel/animation_graph.hpp','engine/runtime/animation_pose.cpp','engine/runtime/include/darkangel/animation.hpp','tests/animation_graph_tests.cpp','scripts/verify_animation_graph.py','CMakeLists.txt']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native typed clip/1D/explicit 2D graph, per-instance normalized phase, frozen topology/dependency generation and bounded replay; graph assets/editor/markers/transitions/action/additive/IK remain open',online='EOS live acceptance blocked; existing GNS evidence unchanged',gates=gates,fixtures=fixtures,source_sha256={p:digest(ROOT/p) for p in paths},binary_sha256=digest(ROOT/'build/m5-relwithdebinfo/AnimationGraphTests.exe'))
(evidence/'m5-graph.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
