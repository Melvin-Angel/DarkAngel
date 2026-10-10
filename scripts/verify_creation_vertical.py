"""Focused staged authoring -> private validation -> coordinated Save -> fresh Play."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3000:])
 print(name+' passed',flush=True);return r.stdout
run('creation-vertical-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','NativeAuthoringTests','--parallel','2'],activate())
run('creation-vertical-native',[BUILD/'NativeAuthoringTests.exe'])
f=json.loads((BUILD/'authoring-fixture.json').read_text(encoding='utf-8'));s=Path(f['source']);c=Path(f['cache'])
model=json.loads((s/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id'];collision=json.loads((s/'royal_district/collision/environment.dacollision').read_text())['asset']
output=run('creation-vertical-d3d12',[BUILD/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',c/'cas','--model',model,'--scene',s/'royal_district/RoyalCombat.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',s,'--asset-cache',c,'--backend','d3d12','--hidden','--exercise-creation-vertical','--frames','40','--height','1000','--capture-workspace','--capture',E/'creation-vertical.png'])
if 'Created native vertical: six sources, private validation, coordinated Save, fresh scene/Player/Kit/Composer resources, deferred cost12 damage17 status source4/activation1, health83 stamina88 pending0' not in output:raise RuntimeError('Created vertical combat receipt missing')
files=['apps/editor/main.cpp','apps/editor/native_authoring.cpp','apps/editor/native_authoring.hpp','apps/editor/editor_controller.cpp','apps/editor/editor_authoring.cpp','apps/editor/editor_workspaces.cpp','apps/editor/character_preview.cpp','apps/editor/effect_preview.hpp','engine/assets/asset_service.cpp','tests/native_authoring_tests.cpp','scripts/verify_creation_vertical.py']
(E/'creation-vertical.json').write_text(json.dumps(dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Six new native authoring sources, deferred Composer commit, ordinary effect binding, coordinated publication and complete fresh Player Play',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},limitations=['Scripted native API/one D3D12 integration; no physical pointer automation, Vulkan/GNS or multiplayer fault qualification.','Private target statistic value is not inferred from public replication; ordinary effect definition, application and attribution are checked.','Published creations cannot be deleted by Undo; publication retains documented crash-recovery limitations.','M4/M5 In progress; live EOS blocked; M6-M9 not started.']),indent=2)+'\n',encoding='utf-8')
