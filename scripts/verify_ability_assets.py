"""Frozen Ability/Attribute asset acceptance, not playable combat or M5 completion."""
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
        ('m5-relwithdebinfo',['AbilityAssetTests','AbilityTests','ActionTimelineTests','AssetTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','AbilityAssetTests']),
        ('m2-relwithdebinfo',['AbilityAssetTests']),
        ('m1-relwithdebinfo',['DarkAngelHeadless','AbilityTests'])):
        run('ability-assets-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env)
run('ability-assets-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_assets|M5.ability_commit|M5.action_timeline|M2.asset_pipeline)$'])
run('ability-assets-editor',[ROOT/'build/m5-editor-relwithdebinfo/AbilityAssetTests.exe'])
run('ability-assets-no-animation',[ROOT/'build/m2-relwithdebinfo/AbilityAssetTests.exe'])
run('ability-assets-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_commit|HeadlessSmoke)$'])
inputs=['CMakeLists.txt','engine/assets/ability_cooker.cpp','engine/assets/include/darkangel/ability_assets.hpp','engine/assets/assets_internal.hpp','engine/assets/asset_service.cpp','engine/runtime/ability.cpp','engine/runtime/include/darkangel/ability.hpp','tests/ability_asset_tests.cpp','scripts/verify_ability_assets.py']
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/AbilityAssetTests.exe','build/m2-relwithdebinfo/AbilityAssetTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Versioned Ability/Attribute source, frozen action/schema closure, source-free loader and existing WorldSession commitment',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['CombatKit/AnimationGraph assets, reflected authoring UI and stance switching remain open.','No new hit/damage, effect/tag, prediction/operation wire or resource/action snapshot implementation.','No playable Royal attack, graphics qualification or GNS/EOS acceptance refreshed.','Attribute visibility metadata is retained but does not change wire visibility.'])
(EVIDENCE/'ability-assets.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Ability assets native/full-editor/no-animation and ordinary Headless checks passed')
