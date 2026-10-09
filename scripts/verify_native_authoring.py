"""Focused first authoring-loop checks; one full editor profile and D3D12."""
import argparse,hashlib,json,subprocess,time,shutil
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe';BUILD=ROOT/'build/m5-editor-relwithdebinfo'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');parser.add_argument('--history',action='store_true',help='Focused history/coordinated Save scope; no repeated GNS check');args=parser.parse_args();gates=[]
def run(name,command,env=None,timeout=900):
 if args.history:name=name.replace('native-authoring','authoring-history');command=[str(p).replace('native-authoring-', 'authoring-history-') if str(p).endswith('.png') else p for p in command]
 start=time.monotonic();r=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout);(EVIDENCE/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-6000:])
 print(name+' passed',flush=True);return r.stdout
if args.build:
 run('native-authoring-build',[CMAKE,'--build',BUILD,'--target','DarkAngelEditor','NativeAuthoringTests',*(['AssetTests'] if args.history else ['AuthoredEffectGnsTests']),'NativeSourceEditTests','--parallel','2'],activate())
 run('native-authoring-no-animation-build',[CMAKE,'--build',ROOT/'build/m2-relwithdebinfo','--target','NativeSourceEditTests','--parallel','2'],activate())
if args.history:run('native-authoring-asset-regression',[BUILD/'AssetTests.exe'])
run('native-authoring-source-check',[BUILD/'NativeSourceEditTests.exe'])
run('native-authoring-workflow',[BUILD/'NativeAuthoringTests.exe'])
fixture=json.loads((BUILD/'authoring-fixture.json').read_text());source=Path(fixture['source']);cache=Path(fixture['cache']);kit=fixture['kit'];registry=Path(fixture['registry'])
base=[BUILD/'DarkAngelEditor.exe','--registry',registry,'--cas',cache/'cas','--model',json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id'],'--scene',source/'royal_district/RoyalCombat.dascene','--character-kit',kit,'--character-collision',json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset'],'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',source,'--asset-cache',cache,'--backend','d3d12','--hidden','--capture-workspace']
output=run('native-authoring-game-active',base+['--exercise-authoring','--frames','30','--capture',EVIDENCE/'native-authoring-game-active.png'])
if args.history and 'edit -> Undo -> Redo -> coordinated Save -> fresh Play' not in output:raise RuntimeError('History/Save GUI demonstration missing')
if 'target_health=60 stamina=80 pending=0' not in output:raise RuntimeError('Edited GUI gameplay result missing')
output=run('native-authoring-game-expired',base+['--exercise-authored-replay','--frames','150','--capture',EVIDENCE/'native-authoring-game-expired.png'])
if 'target_health=39 stamina=80 pending=0' not in output:raise RuntimeError('Fresh replay/expiry result missing')
run('native-authoring-light',base+['--exercise-combat','--frames','120'])
run('native-authoring-panel',base+['--ability-workspace','--frames','5','--capture',EVIDENCE/'native-authoring-panel.png'])
run('native-authoring-effect-panel',base+['--ability-workspace','--authoring-asset',fixture['effect'],'--frames','5','--capture',EVIDENCE/'native-authoring-effect-panel.png'])
action=json.loads((source/'royal_district/combat/heavy.daaction').read_text())['asset']
run('native-authoring-action-panel',base+['--ability-workspace','--authoring-asset',action,'--frames','5','--capture',EVIDENCE/'native-authoring-action-panel.png'])
if not args.history:
 common=[str(registry),str(cache/'cas'),kit];binary=str(BUILD/'AuthoredEffectGnsTests.exe');host=subprocess.Popen([binary,'--host',*common],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8',errors='replace')
 try:
  run('native-authoring-gns-client',[binary,'--client',*common],timeout=35);text=host.communicate(timeout=35)[0];(EVIDENCE/'native-authoring-gns-host.log').write_text(text,encoding='utf8');gates.append(dict(name='native-authoring-gns-host',exit=host.returncode))
  if host.returncode:raise RuntimeError(text)
 finally:
  if host.poll() is None:host.terminate()
  text=host.communicate(timeout=5)[0]
  (EVIDENCE/'native-authoring-gns-host.log').write_text(text,encoding='utf8')
if args.build:run('native-authoring-no-animation',[ROOT/'build/m2-relwithdebinfo/NativeSourceEditTests.exe'])
sources=['tests/asset_tests.cpp','CMakeLists.txt','engine/assets/asset_service.cpp','engine/assets/include/darkangel/assets.hpp','apps/editor/native_authoring.hpp','apps/editor/native_authoring.cpp','apps/editor/editor_authoring.cpp','apps/editor/editor_ui.hpp','apps/editor/editor_ui.cpp','apps/editor/editor_controller.hpp','apps/editor/editor_controller.cpp','apps/editor/main.cpp','tests/native_authoring_tests.cpp','tests/authored_effect_gns_tests.cpp','tests/native_source_edit_tests.cpp','scripts/verify_native_authoring.py','scripts/open_royal_scene.ps1'];binaries=['DarkAngelEditor','NativeAuthoringTests','NativeSourceEditTests']+(['AssetTests'] if args.history else ['AuthoredEffectGnsTests'])
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native Ability/Effect/action forms, compatible pickers, create/duplicate/edit/assign/bind/save and complete scene package/fresh CharacterPreviewResources/isolated Play; edited status owner/public over existing GNS',gates=gates,fixture=fixture,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((BUILD/(p+'.exe')).read_bytes()).hexdigest() for p in binaries},limitations=['Save prepares a coordinated native changed-source/dependent kit/scene candidate. Normal failures restore prior sources and catalog heads; multi-file source updates plus SQLite are not crash-atomic.','Create uses a compatible native template and retains action/schema references. Full visual Composer/rig-preview/graph authoring and rich thumbnail picker remain open.','One D3D12 backend; scripted native editor commands and rendered panels, not physical mouse/gamepad automation.','GNS check loads the same edited frozen kit/effect and proves owner/public status timers and damage on separate localhost processes. It does not qualify remote graphical/Jolt combat or the full multiplayer fault matrix.','M4/M5 In progress; M6 not started; live EOS externally blocked.'])
if args.history:
 receipt['scope']='Revisioned grouped native draft Undo/Redo; private changed-source/dependent kit/scene candidate; protected source publication with one catalog transaction; failure recovery/stale-writer conflict; D3D12 fresh isolated Play'
 receipt['limitations']=['Windows file sharing excludes source writers during commit and SQLite heads commit together, but source files plus catalog are not crash-atomic or a cross-process multi-file reader snapshot. Recovery originals/journal are retained on recovery I/O failure or process crash; manual reconciliation is required.', 'Preparation snapshots at most 4096 files/256 MiB and conservatively rejects unrelated external changes. Immutable orphan CAS blobs may remain after failed preparation.', 'Create persists a fresh native source; creation/deletion is outside draft Undo. Reload clears session gameplay history while retaining other drafts. Shared actions must be duplicated before independent timing edits.', 'One D3D12 backend; scripted editor commands and rendered panels, not physical mouse/gamepad automation. Runtime/protocol/physics unchanged; historical GNS check not repeated.', 'Visual Composer/character preview and full M4/M5 qualification remain open; M4/M5 In progress, M6 not started, live EOS externally blocked.']
(EVIDENCE/('native-authoring-history.json' if args.history else 'native-authoring.json')).write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Authoring history and coordinated Save passed' if args.history else 'First native authoring loop passed',flush=True)
