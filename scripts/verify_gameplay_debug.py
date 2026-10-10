"""Focused Game state/provenance diagnostics and one active D3D12 capture."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3500:])
 print(name+' passed',flush=True);return r.stdout
run('gameplay-debug-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','NativeAuthoringTests','GameplayDebugTests','--parallel','2'],activate())
run('gameplay-debug-native',[BUILD/'NativeAuthoringTests.exe'])
run('gameplay-debug-sources',[BUILD/'GameplayDebugTests.exe'])
f=json.loads((BUILD/'authoring-fixture.json').read_text(encoding='utf-8'));s=Path(f['source']);c=Path(f['cache'])
model=json.loads((s/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text(encoding='utf-8'))['id'];collision=json.loads((s/'royal_district/collision/environment.dacollision').read_text(encoding='utf-8'))['asset']
output=run('gameplay-debug-panel',[BUILD/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',c/'cas','--model',model,'--scene',s/'royal_district/RoyalCombat.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',s,'--asset-cache',c,'--backend','d3d12','--hidden','--exercise-game-debug','--frames','30','--height','1600','--capture-workspace','--capture',E/'gameplay-debug-panel.png'])
if 'Game inspector effect=' not in output or 'target_health=60 stamina=80 pending=0' not in output:raise RuntimeError('Native Game source/combat result missing')
files=['apps/editor/gameplay_debug.hpp','apps/editor/editor_workspaces.cpp','apps/editor/editor_ui.hpp','apps/editor/main.cpp','tests/gameplay_debug_tests.cpp','CMakeLists.txt','scripts/verify_gameplay_debug.py']
(E/'gameplay-debug.json').write_text(json.dumps(dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Read-only Game confirmed/predicted attributes, action/outcomes and available source-aware tag/effect attribution',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},limitations=['Native source attribution tests and one D3D12 active heavy/status capture; no physical input or full multiplayer qualification.','Aggregate snapshots omit full contributor internals; unavailable values/sources are explicit rather than inferred.','M4/M5 In progress; live EOS blocked.']),indent=2)+'\n',encoding='utf-8')
