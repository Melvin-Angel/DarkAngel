"""Atomic owner ability/motor correction in the existing WorldSession protocol."""
import argparse, hashlib, json, subprocess, time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args()
gates=[]
def run(name,command,env=None):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=900)
    (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
if args.build:
    env=activate()
    for profile,targets in (
        ('m5-relwithdebinfo',['AbilityCorrectionTests','AbilityTests','SessionTests','MotorSessionTests','MeleeTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','AbilityCorrectionTests']),
        ('m1-relwithdebinfo',['DarkAngelHeadless','AbilityCorrectionTests','AbilityTests'])):
        run('ability-correction-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env)
run('ability-correction-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_correction|M5.ability_commit|M5.authoritative_melee|M3.authority|M3.loopback|M4.session)$'])
run('ability-correction-editor',[ROOT/'build/m5-editor-relwithdebinfo/AbilityCorrectionTests.exe'])
run('ability-correction-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_correction|M5.ability_commit|HeadlessSmoke)$'])
inputs=['CMakeLists.txt','engine/runtime/session_wire.hpp','engine/runtime/ability_wire.hpp','engine/runtime/ability_wire.cpp','engine/runtime/include/darkangel/ability.hpp','engine/runtime/include/darkangel/world_session.hpp','engine/runtime/ability.cpp','engine/runtime/ability_state.hpp','engine/runtime/world_session.cpp','tests/ability_correction_tests.cpp','scripts/verify_ability_correction.py']
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/AbilityCorrectionTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Protocol 3 owner-only coherent ability/motor correction, exact terminal operation inclusion and native-prepared ACK/retirement; existing session envelope/adapters',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['Client ability intents/terminal receipt delivery and pending owner prediction still remain open.','Observers still receive existing public Health/movement, not new action/tag state.','No playable Royal attack or new GNS/EOS acceptance.','Profile 1/2 remain compatible with their prior message classes; new owner correction messages require protocol 3.','No final performance/bandwidth or coupled fault qualification.'])
(EVIDENCE/'ability-correction.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Atomic ability correction native/editor/ordinary Headless checks passed')
