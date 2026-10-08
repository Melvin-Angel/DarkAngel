"""Directional catalogue/runtime and owned locomotion integration acceptance."""
import hashlib
import json
from pathlib import Path
import subprocess
import time
from msvc_environment import activate

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / 'docs/implementation/evidence'
CMAKE = ROOT / '.tools/cmake/cmake-4.4.4-windows-x86_64/bin'
gates = []
def run(name, command, timeout=180):
    start = time.monotonic()
    result = subprocess.run(list(map(str, command)), cwd=ROOT, env=activate(), capture_output=True, text=True, encoding='utf8', errors='replace', timeout=timeout)
    path = EVIDENCE / ('royal-directional-' + name + '.log')
    path.write_text(result.stdout + result.stderr, encoding='utf8')
    gates.append(dict(name=name, exit=result.returncode, seconds=round(time.monotonic()-start,3), log=path.relative_to(ROOT).as_posix()))
    print(name, result.returncode, flush=True)
    if result.returncode:
        raise RuntimeError((result.stdout + result.stderr)[-4000:])

run('content', ['python', 'scripts/prepare_royal_locomotion.py'])
run('build', [CMAKE/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'], 900)
run('graph', [ROOT/'build/m5-relwithdebinfo/AnimationGraphTests.exe'])
run('clips', [ROOT/'build/m5-relwithdebinfo/AnimationClipTests.exe','--royal'])
run('ctest', [CMAKE/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'])
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
files = ['engine/runtime/animation_graph.cpp','engine/runtime/include/darkangel/animation_graph.hpp','tests/animation_graph_tests.cpp','tests/animation_clip_tests.cpp','scripts/prepare_royal_locomotion.py']
receipt = dict(schema=1, base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    scope='Bounded selector catalogue, nine-point directional selection, simultaneous-layer compile rejection, playback rates and existing replay/render-rate gates; eight canonical Omni cardinal walk/run sources plus Blink idle. Interactive movement/action binding remains separate.',
    limits=dict(nodes=32,depth=16,points=32,triangles=64,catalogue_capacity=32,simultaneous_layers=4,playback_rate=[0,4]),
    gates=gates, source_sha256={path:digest(ROOT/path) for path in files}, online='EOS remains blocked; existing GNS receipts unchanged')
(EVIDENCE/'royal-directional.json').write_text(json.dumps(receipt,indent=2)+'\n')
