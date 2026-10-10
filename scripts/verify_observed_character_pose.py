"""Public actor pose sampling and prepared target rendering, without AI ingress."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';EVIDENCE=ROOT/'docs/implementation/evidence'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None):
    start=time.monotonic();r=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=900)
    output=r.stdout+r.stderr;(EVIDENCE/(name+'.log')).write_text(output,encoding='utf8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
    if r.returncode:raise RuntimeError(output[-4000:])
    print(name+' passed',flush=True);return output
if args.build:
    run('observed-pose-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','NativeAuthoringTests','ObservedCharacterPoseTests','AnimationGraphTests','CharacterCombatSceneTests','RoyalHeavySceneTests','--parallel','2'],activate())
run('observed-pose-graph-role',[BUILD/'AnimationGraphTests.exe'])
run('observed-pose-authoring',[BUILD/'NativeAuthoringTests.exe'])
run('observed-pose-public',[BUILD/'ObservedCharacterPoseTests.exe'])
run('observed-pose-combat',[BUILD/'CharacterCombatSceneTests.exe'])
run('observed-pose-heavy',[BUILD/'RoyalHeavySceneTests.exe'])
f=json.loads((BUILD/'authoring-fixture.json').read_text());source=Path(f['source']);cache=Path(f['cache'])
model=json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id'];collision=json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset']
run('observed-pose-d3d12',[BUILD/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',cache/'cas','--model',model,'--scene',source/'royal_district/RoyalCombat.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',source,'--asset-cache',cache,'--backend','d3d12','--hidden','--exercise-combat','--frames','120','--capture-workspace','--capture',EVIDENCE/'observed-pose-d3d12.png'])
files=['CMakeLists.txt','engine/runtime/include/darkangel/character_presentation.hpp','engine/runtime/character_presentation.cpp','engine/runtime/include/darkangel/character_scene.hpp','engine/runtime/character_scene.cpp','engine/runtime/include/darkangel/animation_graph.hpp','engine/runtime/animation_graph.cpp','apps/editor/main.cpp','tests/animation_graph_tests.cpp','tests/native_authoring_tests.cpp','tests/observed_character_pose_tests.cpp','scripts/verify_observed_character_pose.py']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Prepared public actor locomotion/action sampling and native target GPU pose routing',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},binary_sha256={p:hashlib.sha256((BUILD/p).read_bytes()).hexdigest() for p in ['DarkAngelEditor.exe','ObservedCharacterPoseTests.exe','NativeAuthoringTests.exe']},limitations=['Training target shares the prepared Character rig/kit presentation; no NPC definition, AI controller, navigation or independent target loadout.','Current public action clocks restore native loop-local pose without gameplay/cue replay. Exact network late-join locomotion phase and remote graphical timing/loss qualification remain open.','Public graph inputs cannot include owner/server-only tag values; private conditions see only available public state.','M4/M5 In progress; EOS blocked; M6–M9 not started.'])
(EVIDENCE/'observed-pose.json').write_text(json.dumps(receipt,indent=2)+'\n')
