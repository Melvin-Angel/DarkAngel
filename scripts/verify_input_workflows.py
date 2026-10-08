"""Focused input, native adapter and single-editor workspace acceptance."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate

ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args()
gates=[]
def run(name,command,timeout=180,env=None):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
    (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
    return result.stdout
if args.build:
    env=activate()
    run('input-workflow-editor-build',[CMAKE,'--build','build/m5-editor-relwithdebinfo','--target','DarkAngelEditor','InputTests','NativeInputTests','--parallel','2'],600,env)
    run('input-workflow-native-build',[CMAKE,'--build','build/m5-relwithdebinfo','--target','AssetTool','InputTests','AssetTests','EditorTests','CharacterSceneTests','MotorTests','RenderableHumanTests','ExternalImportTests','--parallel','2'],600,env)
    run('input-workflow-library-build',[CMAKE,'--build','build/m2-relwithdebinfo','--target','InputTests','AssetTests','EditorTests','ExternalImportTests','--parallel','2'],600,env)
run('input-workflow-core',[ROOT/'build/m5-editor-relwithdebinfo/InputTests.exe'])
run('input-workflow-platform',[ROOT/'build/m5-editor-relwithdebinfo/NativeInputTests.exe'])
run('input-workflow-affected',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M4.input_actions|M4.motor|M2.asset_pipeline|M2.editor_transactions|M5.character_scene|M5.renderable_human|M5.external_import)$'])
run('input-workflow-library',[CMAKE.parent/'ctest.exe','--test-dir','build/m2-relwithdebinfo','--output-on-failure','-R',r'^(M4.input_actions|M2.asset_pipeline|M2.editor_transactions|M5.external_import)$'])
run('input-workflow-royal-regression',['python','scripts/verify_character_editor.py'],300)
editor=ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe'
source=ROOT/'content';cache=ROOT/'.cache/royal-scene'
def asset(relative):return json.loads(Path(str(source/relative)+'.daimport').read_text())['id']
clips=','.join(asset('royal_district/clips/'+name+'.glb') for name in ('idle','omni-walk','omni-left','omni-back','omni-right','omni-run','omni-run-left','omni-run-back','omni-run-right'))
base=[editor,'--registry',cache/'registry.json','--cas',cache/'cas','--model',asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'),'--scene',source/'royal_district/RoyalVillage.dascene','--character-clips',clips,'--sources',source,'--asset-cache',ROOT/'.cache/editor-assets','--hidden','--capture-workspace']
for backend in ('d3d12','vulkan'):
    name='input-workflow-game-'+backend
    output=run(name,base+['--backend',backend,'--frames','120','--exercise-character','--capture',EVIDENCE/(name+'.png')])
    if 'Native character editor tick=120' not in output:raise RuntimeError('Mapped native character marker missing')
    name='input-workflow-switch-'+backend
    output=run(name,base+['--backend',backend,'--frames','120','--exercise-character','--exercise-workspaces','--capture',EVIDENCE/(name+'.png')])
    if 'Workspace transitions preserved authoring and resumed after neutral input' not in output:raise RuntimeError('Workspace switch marker missing')
for name,extra in [('scene',[]),('bindings',['--tools-workspace']),('settings',['--settings-workspace'])]:
    run('input-workflow-'+name,base+['--backend','d3d12','--frames','5',*extra,'--capture',EVIDENCE/('input-workflow-'+name+'.png')])
inputs=('CMakeLists.txt','engine/runtime/input.cpp','engine/runtime/include/darkangel/input.hpp','engine/assets/input_cooker.cpp','tools/editor/editor_document.cpp','content/input/royal_player.dainput','apps/editor/main.cpp','apps/editor/native_input.cpp','apps/editor/native_input.hpp','apps/editor/editor_ui.cpp','apps/editor/editor_workspaces.cpp')
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native semantic input/chords and frozen profile; Win32 message adapter with synthetic source; full editor Game/Scene/Assets/Tools/Settings workflows and mapped Loopback character movement. One editor product; feature-disabled targets are shared-library checks only.',gates=gates,binary_sha256=hashlib.sha256(editor.read_bytes()).hexdigest(),implementation_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},limitations=['Physical gamepad hardware/rebinding UI and camera-mode/player/NPC authoring remain open.','Combat events are intent only; ability/combat authority is not implemented.','Synthetic hidden fixtures are not real-time performance qualification.','Full M4/M5 timing/fault/streaming/authority gates remain open; no new GNS/EOS acceptance.'])
(EVIDENCE/'input-workflow.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Native input and single-editor Game workflow acceptance passed')
