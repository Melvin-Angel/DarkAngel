"""Owned fixed-tick ability intents/receipts on existing Loopback and GNS paths."""
import argparse, hashlib, json, subprocess, time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args()
gates=[]
def run(name,command,env=None,timeout=900):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
    (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
env=activate()
if args.build:
    for profile,targets in (
        ('m5-relwithdebinfo',['AbilityIntentTests','AbilityIntentGnsTests','AbilityCorrectionTests','AbilityTests','SessionTests','MotorSessionTests','MeleeTests','AbilityAssetTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','AbilityIntentTests']),
        ('m1-relwithdebinfo',['DarkAngelHeadless','AbilityIntentTests','AbilityCorrectionTests','AbilityTests'])):
        run('ability-intent-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env)
run('ability-intent-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_intent|M5.ability_correction|M5.ability_commit|M5.ability_assets|M5.authoritative_melee|M3.authority|M3.loopback|M4.session)$'])
run('ability-intent-editor',[ROOT/'build/m5-editor-relwithdebinfo/AbilityIntentTests.exe'])
run('ability-intent-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_intent|M5.ability_correction|M5.ability_commit|HeadlessSmoke)$'])
start=time.monotonic();host=subprocess.Popen([str(ROOT/'build/m5-relwithdebinfo/AbilityIntentGnsTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
try:
    run('ability-intent-gns-client',[ROOT/'build/m5-relwithdebinfo/AbilityIntentGnsTests.exe','--client'],env,30)
    output=host.communicate(timeout=30)[0];(EVIDENCE/'ability-intent-gns-host.log').write_text(output,encoding='utf8');gates.append(dict(name='ability-intent-gns-host',exit=host.returncode,seconds=round(time.monotonic()-start,3)))
    if host.returncode:raise RuntimeError(output)
finally:
    if host.poll() is None:host.terminate();host.wait(timeout=5)
inputs=['CMakeLists.txt','engine/runtime/include/darkangel/ability.hpp','engine/runtime/include/darkangel/world_session.hpp','engine/runtime/ability.cpp','engine/runtime/ability_state.hpp','engine/runtime/ability_wire.cpp','engine/runtime/world_session.cpp','tests/ability_intent_tests.cpp','tests/ability_intent_gns_tests.cpp','scripts/verify_ability_intent.py']
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/AbilityIntentTests.exe','build/m5-relwithdebinfo/AbilityIntentGnsTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Protocol-3 owned semantic intents, authoritative fixed-tick gestures/expiry, canonical native commitment, correlated receipts/inclusion and teardown; separate GNS cost/correction process fixture',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['No pending owner prediction/resource view or combo dependency replay yet.','Royal GUI ability/action/root/socket integration and native graph/kit authoring remain open.','GNS fixture validates protocol/cost/Health/correction with published synthetic motor states; it is not a Jolt melee or realtime performance test.','Full input-loss/lease/fault/clock and gameplay qualification remain open; EOS live service blocked.'])
(EVIDENCE/'ability-intent.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Owned ability intent native/editor/Headless and separate GNS processes passed')
