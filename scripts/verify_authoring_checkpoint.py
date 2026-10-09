"""Focused shared-path integration after the Composer/Attribute editor chunks."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-4000:])
 print(name+' passed',flush=True);return r.stdout
run('authoring-checkpoint-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','NativeAuthoringTests','AssetTests','NativeSourceEditTests','--parallel','2'],activate())
for binary in ['AssetTests','NativeSourceEditTests','NativeAuthoringTests']:run('authoring-checkpoint-'+binary,[BUILD/(binary+'.exe')])
f=json.loads((BUILD/'authoring-fixture.json').read_text());s=Path(f['source']);c=Path(f['cache']);model=json.loads((s/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id'];collision=json.loads((s/'royal_district/collision/environment.dacollision').read_text())['asset']
base=[BUILD/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',c/'cas','--model',model,'--scene',s/'royal_district/RoyalCombat.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',s,'--asset-cache',c,'--backend','d3d12','--hidden']
text=run('authoring-checkpoint-fresh-play',base+['--exercise-authoring','--frames','30'])
if 'target_health=60 stamina=80 pending=0' not in text:raise RuntimeError('Authored fresh-Play result missing')
files=['engine/assets/asset_service.cpp','apps/editor/native_authoring.cpp','apps/editor/editor_authoring.cpp','apps/editor/editor_composer.cpp','apps/editor/main.cpp','scripts/verify_authoring_checkpoint.py']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Focused shared asset/source/coordinated Save and D3D12 authored fresh-Play integration after Composer/attribute chunks',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},binary_sha256={p:hashlib.sha256((BUILD/(p+'.exe')).read_bytes()).hexdigest() for p in ['DarkAngelEditor','NativeAuthoringTests','AssetTests','NativeSourceEditTests']},limitations=['One D3D12 authored edit/history/Save/fresh Play case; no GNS/Vulkan/full historical matrix.','No full performance/clock/fault/streaming/graphical multiplayer qualification. M4/M5 remain In progress; live EOS blocked.'])
(E/'authoring-checkpoint.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
