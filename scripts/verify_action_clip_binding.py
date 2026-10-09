"""Frozen Action Composer clip/rig and timeline-local root ownership checks; M4/M5 remain incomplete."""
import argparse, hashlib, json, subprocess, time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args()
gates=[]
def run(name,command,env=None):
    start=time.monotonic()
    result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=900)
    (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
    gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
if args.build:
    env=activate()
    for profile,targets in (
        ('m5-relwithdebinfo',['ActionClipBindingTests','ActionTimelineTests','AbilityAssetTests','CombatKitAssetTests','GraphAssetTests','AnimationGraphTests','AnimationClipTests','CharacterSceneTests','AbilityTests','AbilityCorrectionTests','AbilityIntentTests','MeleeTests','MotorTests','MotorFaultTests','MotorSessionTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','ActionClipBindingTests','CombatKitAssetTests']),
        ('m2-relwithdebinfo',['AbilityAssetTests','AssetTool']),
        ('m1-relwithdebinfo',['DarkAngelHeadless','AbilityIntentTests','AbilityCorrectionTests','AbilityTests'])):
        run('action-clip-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env)
run('action-clip-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.action_clip_binding|M5.action_timeline|M5.ability_assets|M5.combat_kit_assets|M5.graph_assets|M5.graph_runtime|M5.clips|M5.omni_clips|M5.character_scene|M5.ability_commit|M5.ability_correction|M5.ability_intent|M5.authoritative_melee|M4.motor|M4.faults|M4.session)$'])
run('action-clip-editor',[ROOT/'build/m5-editor-relwithdebinfo/ActionClipBindingTests.exe'])
run('action-clip-no-animation',[ROOT/'build/m2-relwithdebinfo/AbilityAssetTests.exe'])
run('action-clip-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_intent|M5.ability_correction|M5.ability_commit|HeadlessSmoke)$'])

# Feature-disabled cooking fails explicitly before creating a partial action head.
fixture=ROOT/'build/m2-relwithdebinfo/action-clip-disabled';fixture.mkdir(exist_ok=True)
data=json.loads((ROOT/'content/animation/m5_attack.daaction').read_text());data.update(schema=2,motion=dict(clip='260f716a-1883-42bc-8ea7-29d53913b79d',source='attack.glb',policy='motor'))
(fixture/'attack.daaction').write_text(json.dumps(data))
start=time.monotonic();result=subprocess.run([str(ROOT/'build/m2-relwithdebinfo/AssetTool.exe'),'cook',str(fixture),str(fixture/'cache'),'attack.daaction'],cwd=ROOT,capture_output=True,text=True,timeout=30)
(EVIDENCE/'action-clip-disabled.log').write_text(result.stdout+result.stderr)
if result.returncode==0 or 'Action clip binding cook requires explicit animation tools build' not in result.stderr:raise RuntimeError('Feature-disabled action cook did not reject explicitly')
gates.append(dict(name='action-clip-disabled',exit=0,expected_rejection=result.returncode,seconds=round(time.monotonic()-start,3)))
inputs=['CMakeLists.txt','engine/assets/action_cooker.cpp','engine/assets/ability_cooker.cpp','engine/assets/asset_service.cpp','engine/runtime/action.cpp','engine/runtime/include/darkangel/action.hpp','tests/action_clip_binding_tests.cpp','tests/combat_kit_asset_tests.cpp','scripts/verify_action_clip_binding.py']
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/ActionClipBindingTests.exe','build/m2-relwithdebinfo/AbilityAssetTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native action schema2 clip/rig and root-policy closure, source-free generation-fenced loading through ability/kit, pure timeline root requests consumed by existing Jolt motor, synthetic 30/60/144 traces',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['Royal input/attack/target presentation integration and character/kit/ability authoring remain open.','Upper-body masking/additive pose, typed graph states/events and animated socket sweeps remain open.','No new owner prediction, observer actions/public visibility or fault/timing/performance qualification.','M4/M5 remain In progress; live EOS remains blocked.'])
(EVIDENCE/'action-clip.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Frozen action clip native/full-editor/no-animation and ordinary Headless checks passed')
