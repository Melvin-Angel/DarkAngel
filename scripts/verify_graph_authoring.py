"""Focused native graph history, frozen consumers and fresh Play."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3000:])
 print(name+' passed',flush=True);return r.stdout
run('graph-authoring-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','NativeAuthoringTests','--parallel','2'],activate())
run('graph-authoring-native',[BUILD/'NativeAuthoringTests.exe'])
f=json.loads((BUILD/'authoring-fixture.json').read_text(encoding='utf-8'));s=Path(f['source']);c=Path(f['cache'])
model=json.loads((s/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text(encoding='utf-8'))['id'];collision=json.loads((s/'royal_district/collision/environment.dacollision').read_text(encoding='utf-8'))['asset']
base=[BUILD/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',c/'cas','--model',model,'--scene',s/'royal_district/RoyalCombat.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',s,'--asset-cache',c,'--backend','d3d12','--hidden']
output=run('graph-authoring-fresh-play',base+['--exercise-graph-authoring','--frames','40'])
if 'Graph point edit -> Undo -> Redo -> frozen Save -> fresh native Player Play passed' not in output:raise RuntimeError('Authored Player combat result missing')
kit=json.loads((s/'royal_district/combat/player.dakit').read_text(encoding='utf-8'));graph=kit['locomotion_stance']
run('graph-authoring-panel',base+['--animation-workspace','--authoring-asset',graph,'--frames','5','--height','1000','--capture-workspace','--capture',E/'graph-authoring-panel.png'])
files=['engine/assets/actor_cooker.cpp','engine/assets/include/darkangel/actor_assets.hpp','engine/runtime/character_scene.cpp','engine/runtime/include/darkangel/character_scene.hpp','engine/assets/asset_service.cpp','tests/native_authoring_tests.cpp','apps/editor/native_authoring.cpp','apps/editor/native_authoring.hpp','apps/editor/character_preview.cpp','apps/editor/editor_authoring.cpp','apps/editor/editor_workspaces.cpp','apps/editor/editor_ui.cpp','apps/editor/editor_ui.hpp','apps/editor/editor_controller.hpp','apps/editor/reference_picker.hpp','apps/editor/main.cpp','scripts/open_royal_scene.ps1','scripts/verify_player_defaults.py']
(E/'graph-authoring.json').write_text(json.dumps(dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Authored Existing native locomotion blend graph source history, consumer closure and fresh native Player Play',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},limitations=['One scripted D3D12 creation/navigation/history/fresh Play and rendered panels; no physical pointer automation.','Initial existing-node typed forms only; node topology creation, graph-state preview, tag conditions, reactions and NPC configuration remain planned.','M4/M5 In progress, NPC M6 not started, live EOS blocked.']),indent=2)+'\n',encoding='utf-8')
