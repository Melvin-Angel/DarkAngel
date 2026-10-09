"""Server motor-relative melee profile; not final animated socket/owner-prediction acceptance."""
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
        ('m5-relwithdebinfo',['MeleeTests','AbilityTests','AbilityAssetTests','PhysicsQuerySensorTests','ActionTimelineTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','MeleeTests','AbilityAssetTests']),
        ('m1-relwithdebinfo',['DarkAngelHeadless','AbilityTests'])):
        run('melee-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env)
run('melee-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.authoritative_melee|M5.ability_assets|M5.ability_commit|M5.action_timeline|M4.queries_sensors)$'])
run('melee-editor',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-editor-relwithdebinfo','--output-on-failure','-R',r'^(M5.authoritative_melee|M5.ability_assets)$'])
run('melee-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_commit|HeadlessSmoke)$'])
inputs=['CMakeLists.txt','engine/runtime/include/darkangel/ability.hpp','engine/runtime/include/darkangel/melee.hpp','engine/runtime/include/darkangel/world_session.hpp','engine/runtime/ability.cpp','engine/runtime/ability_state.hpp','engine/runtime/world_session.cpp','engine/runtime/melee_query.cpp','engine/runtime/character_motor.cpp','engine/runtime/include/darkangel/character_motor.hpp','engine/assets/ability_cooker.cpp','tests/melee_tests.cpp','tests/ability_asset_tests.cpp','scripts/verify_melee.py']
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/MeleeTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='WorldSession authored motor-relative sphere melee, bound native authoritative query, game-owned read-only evaluator, atomic Health/death and existing baseline',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['No playable Royal input/action/pose integration yet.','Initial static motor-relative sphere, not animated canonical socket sweeps or historical melee.','No new client ability wire, inclusion/prediction, action/resource replication or GNS/EOS acceptance.','Full effects/tags/defense/status/stagger/formula/credit lifetime and M4/M5 gates remain open.','Render-rate comparison is synthetic fixed-tick equality, not performance qualification.'])
(EVIDENCE/'melee.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Authoritative melee focused native/editor and ordinary Headless checks passed')
