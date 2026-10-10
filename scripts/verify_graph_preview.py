"""Focused isolated native graph/Character measured-state preview."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3000:])
 print(name+' passed',flush=True);return r.stdout
run('graph-preview-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','ComposerPreviewTests','--parallel','2'],activate())
run('graph-preview-native',[BUILD/'ComposerPreviewTests.exe',BUILD/'authoring-fixture.json','--character'])
f=json.loads((BUILD/'authoring-fixture.json').read_text(encoding='utf-8'));s=Path(f['source']);c=Path(f['cache'])
model=json.loads((s/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text(encoding='utf-8'))['id'];collision=json.loads((s/'royal_district/collision/environment.dacollision').read_text(encoding='utf-8'))['asset']
base=[BUILD/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',c/'cas','--model',model,'--scene',s/'royal_district/RoyalCombat.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',s,'--asset-cache',c,'--backend','d3d12','--hidden']
character=json.loads((s/'characters/gui_character.dacharacter').read_text(encoding='utf-8'))['asset'];graph=json.loads((s/'royal_district/combat/player.dakit').read_text(encoding='utf-8'))['locomotion_stance']
for tick in [0,20]:
 output=run('graph-preview-'+str(tick),base+['--exercise-graph-preview','--graph-character',character,'--authoring-asset',graph,'--composer-tick',str(tick),'--frames','5','--height','1000','--capture-workspace','--capture',E/('graph-preview-'+str(tick)+'.png')])
 if 'joints=81 gameplay=stopped source=unchanged' not in output:raise RuntimeError('Isolated graph preview result missing')
files=['apps/editor/character_preview.hpp','apps/editor/editor_composer.cpp','tests/composer_preview_tests.cpp','apps/editor/editor_controller.cpp','CMakeLists.txt','engine/assets/asset_service.cpp','tests/actor_asset_tests.cpp','apps/editor/native_authoring.cpp','apps/editor/native_authoring.hpp','apps/editor/character_preview.cpp','apps/editor/editor_authoring.cpp','apps/editor/editor_ui.cpp','apps/editor/editor_ui.hpp','apps/editor/editor_controller.hpp','apps/editor/reference_picker.hpp','apps/editor/main.cpp','scripts/open_royal_scene.ps1','scripts/verify_graph_preview.py']
(E/'graph-preview.json').write_text(json.dumps(dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Isolated authored graph/Character measured-state preview with exact native graph fixed ticks and independent GPU resources',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},limitations=['Native exact graph poses, bounded repeated scrub and rejected-candidate tests plus D3D12 graph captures at ticks0/20; no physical controls or performance qualification.','Constant measured-state simulation only; tag conditions, transitions, reactions and animation authority changes remain planned.','M4/M5 In progress, NPC M6 not started, live EOS blocked.']),indent=2)+'\n',encoding='utf-8')
