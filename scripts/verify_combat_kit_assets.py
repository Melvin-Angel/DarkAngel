"""Frozen CombatKit closure and existing WorldSession replacement acceptance; M4/M5 remain incomplete."""
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
        ('m5-relwithdebinfo',['CombatKitAssetTests','GraphAssetTests','AnimationGraphTests','AbilityAssetTests','AbilityTests','CombatKitTests','AttributeTests','InputTests','AssetTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','CombatKitAssetTests']),
        ('m2-relwithdebinfo',['AbilityAssetTests']),
        ('m1-relwithdebinfo',['DarkAngelHeadless','AbilityIntentTests','AbilityCorrectionTests','AbilityTests'])):
        run('combat-kit-assets-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env)
run('combat-kit-assets-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.combat_kit_assets|M5.graph_assets|M5.graph_runtime|M5.ability_assets|M5.ability_commit|M5.combat_kit|M5.attributes|M4.input_actions|M2.asset_pipeline)$'])
run('combat-kit-assets-editor',[ROOT/'build/m5-editor-relwithdebinfo/CombatKitAssetTests.exe'])
run('combat-kit-assets-no-animation',[ROOT/'build/m2-relwithdebinfo/AbilityAssetTests.exe'])
run('combat-kit-assets-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_intent|M5.ability_correction|M5.ability_commit|HeadlessSmoke)$'])

inputs=['CMakeLists.txt','engine/assets/combat_kit_cooker.cpp','engine/assets/include/darkangel/combat_kit_assets.hpp','engine/assets/graph_cooker.cpp','engine/assets/ability_cooker.cpp','engine/assets/input_cooker.cpp','engine/assets/assets_internal.hpp','engine/assets/asset_service.cpp','tests/combat_kit_asset_tests.cpp','scripts/verify_combat_kit_assets.py']
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/CombatKitAssetTests.exe','build/m2-relwithdebinfo/AbilityAssetTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Versioned canonical eight-slot CombatKit, atomic input/schema/ability/stance cooking, source-free loading and existing WorldSession active-grant replacement',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['Royal input/attack/target presentation integration and character/kit/ability authoring remain open.','Graph typed states/events/transitions/layers/actions and animated socket sweeps remain open.','No new owner prediction, observer actions/public visibility or fault/timing/performance qualification.','M4/M5 remain In progress; live EOS remains blocked.'])
(EVIDENCE/'combat-kit-assets.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Frozen CombatKit native/full-editor/no-animation and ordinary Headless checks passed')
