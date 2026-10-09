"""Frozen native graph closure and Royal viewport acceptance; M4/M5 remain incomplete."""
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
        ('m5-relwithdebinfo',['AssetTool','GraphAssetTests','AnimationGraphTests','AnimationClipTests','AbilityAssetTests','AssetTests','CharacterSceneTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','AssetTool','GraphAssetTests']),
        ('m2-relwithdebinfo',['AbilityAssetTests']),
        ('m1-relwithdebinfo',['DarkAngelHeadless'])):
        run('graph-assets-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env)
run('graph-assets-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.graph_assets|M5.graph_runtime|M5.clips|M5.omni_clips|M5.ability_assets|M5.character_scene|M2.asset_pipeline)$'])
run('graph-assets-editor',[ROOT/'build/m5-editor-relwithdebinfo/GraphAssetTests.exe'])
run('graph-assets-no-animation',[ROOT/'build/m2-relwithdebinfo/AbilityAssetTests.exe'])
run('graph-assets-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^HeadlessSmoke$'])
run('graph-assets-royal-prepare',['python','scripts/prepare_royal_scene.py'])
from prepare_royal_scene import SOURCE,CACHE,asset
base=[ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','--registry',CACHE/'registry.json','--cas',CACHE/'cas','--model',asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'),'--scene',SOURCE/'royal_district/RoyalVillage.dascene','--character-graph','3177471d-c721-425f-958b-6b03b78c8bcd']
for backend in ('d3d12','vulkan'):
    run('graph-assets-royal-'+backend,base+['--backend',backend,'--frames','120','--hidden','--exercise-character','--capture',EVIDENCE/('graph-assets-royal-'+backend+'.png')])

inputs=['CMakeLists.txt','engine/assets/graph_cooker.cpp','engine/assets/include/darkangel/graph_assets.hpp','engine/assets/assets_internal.hpp','engine/assets/asset_service.cpp','apps/editor/character_preview.cpp','apps/editor/main.cpp','content/royal_district/locomotion.dagraph','tests/graph_asset_tests.cpp','scripts/prepare_royal_scene.py','scripts/open_royal_scene.ps1','scripts/verify_graph_assets.py']
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/GraphAssetTests.exe','build/m2-relwithdebinfo/AbilityAssetTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Versioned native locomotion graph, frozen clip/rig closure and compiled generation fencing; primary Royal editor consumes cooked graph through existing motor-achieved pose path',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['Initial graph profile is clip/1D/2D normalized locomotion, not typed states/events/transitions or layer/action authoring.','CombatKit assets, reflected character/ability authoring and playable Royal attack remain open.','Synthetic 120-tick hidden graphics checks are not timing/fault/streaming/network performance qualification.','M4/M5 remain In progress; live EOS remains blocked.'])
(EVIDENCE/'graph-assets.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Frozen graph native/full-editor/no-animation/Headless and Royal D3D12/Vulkan checks passed')
