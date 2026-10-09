"""Owner pending ability/resource/action replay and atomic motor preparation."""
import argparse, hashlib, json, subprocess, time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args()
gates=[]
def run(name,command,env=None,timeout=900):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
    (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
run('ability-prediction-royal',['python','scripts/verify_royal_combat.py',*(['--build'] if args.build else [])],timeout=2400)
run('ability-prediction-editor',[ROOT/'build/m5-editor-relwithdebinfo/AbilityPredictionTests.exe'])
env=activate();start=time.monotonic();host=subprocess.Popen([str(ROOT/'build/m5-relwithdebinfo/AbilityIntentGnsTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
try:
    run('ability-prediction-gns-client',[ROOT/'build/m5-relwithdebinfo/AbilityIntentGnsTests.exe','--client'],env,30)
    output=host.communicate(timeout=30)[0];(EVIDENCE/'ability-prediction-gns-host.log').write_text(output,encoding='utf8');gates.append(dict(name='ability-prediction-gns-host',exit=host.returncode,seconds=round(time.monotonic()-start,3)))
    if host.returncode:raise RuntimeError(output)
finally:
    if host.poll() is None:host.terminate();host.wait(timeout=5)
inputs=['CMakeLists.txt','engine/runtime/include/darkangel/ability_prediction.hpp','engine/runtime/ability_prediction.cpp','engine/runtime/include/darkangel/ability.hpp','engine/runtime/ability.cpp','engine/runtime/ability_state.hpp','engine/runtime/ability_wire.cpp','engine/runtime/include/darkangel/combat_kit.hpp','engine/runtime/character_motor.cpp','engine/runtime/include/darkangel/character_motor.hpp','engine/runtime/character_scene.cpp','engine/runtime/include/darkangel/character_scene.hpp','apps/editor/main.cpp','tests/ability_prediction_tests.cpp','tests/ability_motor_prediction_tests.cpp','tests/character_combat_scene_tests.cpp','scripts/verify_ability_prediction.py','scripts/verify_royal_combat.py']
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/AbilityPredictionTests.exe','build/m5-relwithdebinfo/AbilityMotorPredictionTests.exe','build/m1-relwithdebinfo/AbilityPredictionTests.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native owner pending commitment view, exact operation inclusion/receipt rejection/dependency invalidation, held-state correction schema2, regenerated root policy and atomic disposable Jolt motor replay; Royal full-editor and Headless integration',gates=gates,royal_evidence='royal-combat.json',source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['Operation dependencies are local prediction metadata; authored server combo transitions remain open.','Prediction emits no damage or cosmetic cues; effects/tags, deferred reservations and game-owned Luau composition remain open.','Missing exact inclusion, changed execution tick, grant/generation/lifecycle discontinuity and 30-tick history overflow require explicit resync.','GNS refresh verifies schema2 commitment/correction compatibility, not delayed Jolt prediction over GNS or full network faults/performance.','Observer public state and active-action late join are checked separately in ability-observer.json; M4/M5 remain In progress; EOS live service remains blocked.'])
(EVIDENCE/'ability-prediction.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Owner ability prediction native/Jolt/full-editor/Royal/Headless and GNS compatibility checks passed')
