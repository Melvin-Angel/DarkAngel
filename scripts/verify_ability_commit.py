"""Checked server-side immediate ability commit acceptance, not full M5."""
import argparse,hashlib,json,subprocess,sys,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
args=argparse.ArgumentParser();args.add_argument('--build',action='store_true');args=args.parse_args()
gates=[]
def run(name,command,env=None,timeout=300):
    start=time.monotonic();r=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
    (EVIDENCE/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
    if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-5000:])
if args.build:
    env=activate()
    for profile,targets in (
        ('m5-relwithdebinfo',['AbilityTests','AttributeTests','CombatKitTests','SessionTests','MotorSessionTests','ActionTimelineTests','CharacterSceneTests','FoundationTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','AbilityTests']),
        ('m1-relwithdebinfo',['DarkAngelHeadless','AbilityTests'])):
        run('ability-commit-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env,600)
run('ability-commit-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_commit|M5.attributes|M5.combat_kit|M5.action_timeline|M5.character_scene|M3.authority|M3.loopback|M4.session|M1.identity|M1.scene|M1.phases|M1.limits)$'])
run('ability-commit-editor',[ROOT/'build/m5-editor-relwithdebinfo/AbilityTests.exe'])
run('ability-commit-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_commit|HeadlessSmoke)$'])
inputs=('CMakeLists.txt','engine/runtime/ability.cpp','engine/runtime/ability_state.hpp','engine/runtime/include/darkangel/ability.hpp','engine/runtime/world_session.cpp','engine/runtime/include/darkangel/world_session.hpp','engine/foundation/world.cpp','engine/foundation/include/darkangel/world.hpp','engine/runtime/include/darkangel/attributes.hpp','tests/ability_tests.cpp')
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Server-only WorldSession immediate ability grants/costs/cooldowns/action lifecycle and existing Health baseline; no ability RPC, damage evaluator or prediction',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ('build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/AbilityTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe')},limitations=['No playable Royal attack or Character/Ability authoring UI.','No damage evaluator, effects/tags, deferred reservations or multi-slot composer arbitration.','No ability wire requests, resource/action replication, predicted operations or GNS/EOS acceptance.','Synthetic 30/60/144 render schedules compare fixed tick traces, not performance.'])
(EVIDENCE/'ability-commit.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Ability commit focused native, full-editor and SDK-free Headless checks passed')
