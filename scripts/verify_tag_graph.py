"""Native tag selector contracts, frozen asset closure and authored fresh Play."""
import argparse, hashlib, json, subprocess, time
from pathlib import Path
from msvc_environment import activate

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/m5-editor-relwithdebinfo'
EVIDENCE=ROOT/'docs/implementation/evidence'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args()
gates=[]
def run(name,command,env=None):
    start=time.monotonic()
    result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=900)
    output=result.stdout+result.stderr
    (EVIDENCE/(name+'.log')).write_text(output,encoding='utf8')
    gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError(output[-4000:])
    print(name+' passed',flush=True)
    return output
if args.build:
    run('tag-graph-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','AnimationGraphTests','GraphAssetTests','NativeAuthoringTests','--parallel','2'],activate())
run('tag-graph-runtime',[BUILD/'AnimationGraphTests.exe'])
run('tag-graph-assets',[BUILD/'GraphAssetTests.exe'])
output=run('tag-graph-authoring',[BUILD/'NativeAuthoringTests.exe'])
if 'Authored tag graph:' not in output:raise RuntimeError('Tagged source/fresh Play scenario missing')
fixture=json.loads((BUILD/'authoring-fixture.json').read_text());source=Path(fixture['source']);cache=Path(fixture['cache'])
kit=json.loads((source/'royal_district/combat/player.dakit').read_text());graph=kit['locomotion_stance']
character_file=next(source.rglob('pending_shared_character.dacharacter'));character=json.loads(character_file.read_text())['asset']
model=json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id']
collision=json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset']
base=[BUILD/'DarkAngelEditor.exe','--registry',fixture['registry'],'--cas',cache/'cas','--model',model,
    '--scene',source/'royal_district/RoyalCombat.dascene','--character-kit',fixture['kit'],'--character-collision',collision,
    '--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005',
    '--sources',source,'--asset-cache',cache,'--backend','d3d12','--hidden']
for state,extra in [('unmatched',[]),('matched',['--graph-preview-tag','2'])]:
    run('tag-graph-preview-'+state,base+['--exercise-graph-result','--graph-character',character,'--authoring-asset',graph,
        '--composer-tick','20','--height','1000','--frames','5','--capture-workspace','--capture',EVIDENCE/('tag-graph-'+state+'.png'),*extra])
run('tag-graph-game',base+['--exercise-combat','--frames','120'])
files=['engine/runtime/include/darkangel/animation_graph.hpp','engine/runtime/animation_graph.cpp','engine/runtime/character_scene.cpp',
    'engine/runtime/include/darkangel/character_scene.hpp','engine/assets/graph_cooker.cpp','engine/assets/combat_kit_cooker.cpp',
    'apps/editor/native_authoring.cpp','apps/editor/native_authoring.hpp','apps/editor/editor_authoring.cpp','apps/editor/editor_ui.hpp',
    'apps/editor/graph_canvas.hpp','apps/editor/editor_composer.cpp','apps/editor/character_preview.cpp','apps/editor/character_preview.hpp','apps/editor/main.cpp',
    'tests/animation_graph_tests.cpp','tests/graph_asset_tests.cpp','tests/native_authoring_tests.cpp','scripts/verify_tag_graph.py']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    scope='Registered native tag-conditioned locomotion selectors; frozen closure, source-free runtime, history/Save/fresh Play and isolated preview',gates=gates,
    source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},
    binary_sha256={p:hashlib.sha256((BUILD/p).read_bytes()).hexdigest() for p in ['DarkAngelEditor.exe','AnimationGraphTests.exe','GraphAssetTests.exe','NativeAuthoringTests.exe']},
    limitations=['Instant matched/unmatched selection on the existing locomotion clock; no queued state transitions, reaction mapping, additive layers or mask rewrite.',
        'Live graph uses confirmed owner tags; isolated preview uses explicit manual inputs and never activates gameplay.',
        'Scripted source/preview/D3D12 evidence, not physical pointer automation.','M4/M5 In progress; EOS blocked; M6–M9 not started.'])
(EVIDENCE/'tag-graph.json').write_text(json.dumps(receipt,indent=2)+'\n')
