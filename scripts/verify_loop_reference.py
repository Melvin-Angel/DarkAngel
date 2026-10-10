"""Focused native reference selection and one D3D12 authoring surface check."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3500:])
 print(name+' passed',flush=True)
run('loop-reference-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','NativeAuthoringTests','--parallel','2'],activate())
run('loop-reference-native',[BUILD/'NativeAuthoringTests.exe'])
f=json.loads((BUILD/'authoring-fixture.json').read_text(encoding='utf-8'));s=Path(f['source']);c=Path(f['cache'])
model=json.loads((s/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text(encoding='utf-8'))['id'];collision=json.loads((s/'royal_district/collision/environment.dacollision').read_text(encoding='utf-8'))['asset']
run('loop-reference-panel',[BUILD/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',c/'cas','--model',model,'--scene',s/'royal_district/RoyalCombat.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',s,'--asset-cache',c,'--backend','d3d12','--hidden','--animation-workspace','--authoring-asset',json.loads((s/'royal_district/combat/player.dakit').read_text(encoding='utf-8'))['locomotion_stance'],'--frames','5','--height','1000','--capture-workspace','--capture',E/'loop-reference-panel.png'])
files=['apps/editor/reference_picker.hpp','apps/editor/editor_authoring.cpp','apps/editor/editor_ui.hpp','tests/native_authoring_tests.cpp','scripts/verify_loop_reference.py']
(E/'loop-reference.json').write_text(json.dumps(dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Contextual looping-clip filtering in shared native catalog picker, retaining action/Character non-looping selection',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},limitations=['Native selection/compatibility checks and rendered D3D12 authoring panel; no physical popup typing or pointer automation.','Native cook/fresh Play remains final compatibility gate; no new runtime schema or generated tags.','M4/M5 In progress; live EOS blocked.']),indent=2)+'\n',encoding='utf-8')
