"""Focused native reference selection and one D3D12 authoring surface check."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3500:])
 print(name+' passed',flush=True);return r.stdout
run('effect-preview-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','AttributeAuthoringTests','--parallel','2'],activate())
run('effect-preview-native',[BUILD/'AttributeAuthoringTests.exe',BUILD/'authoring-fixture.json'])
f=json.loads((BUILD/'authoring-fixture.json').read_text(encoding='utf-8'));s=Path(f['source']);c=Path(f['cache'])
preview=json.loads((BUILD/'attribute-authoring-fixture.json').read_text(encoding='utf-8'))
model=json.loads((s/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text(encoding='utf-8'))['id'];collision=json.loads((s/'royal_district/collision/environment.dacollision').read_text(encoding='utf-8'))['asset']
output=run('effect-preview-panel',[BUILD/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',c/'cas','--model',model,'--scene',s/'royal_district/RoyalCombat.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',preview['source'],'--asset-cache',preview['cache'],'--backend','d3d12','--hidden','--ability-workspace','--authoring-asset',preview['effect'],'--exercise-effect-preview','--frames','5','--height','1000','--capture-workspace','--capture',E/'effect-preview-panel.png'])
if 'Effect preview modifier/tag apply, removal and expiry passed; gameplay=stopped source=unchanged' not in output:raise RuntimeError('Isolated preview lifecycle result missing')
files=['apps/editor/effect_preview.hpp','apps/editor/editor_authoring.cpp','apps/editor/editor_ui.hpp','apps/editor/main.cpp','tests/attribute_authoring_tests.cpp','scripts/verify_effect_preview.py']
(E/'effect-preview.json').write_text(json.dumps(dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Isolated ordinary modifier/status draft preview with no source/head publication',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},limitations=['Native unsaved modifier/source/head tests and scripted D3D12 Apply/remove/expiry; no physical button input automation.','Preview supports evaluator-free effects only and does not model actor death, multiplayer or custom evaluators; Game remains their test environment.','M4/M5 In progress; live EOS blocked.']),indent=2)+'\n',encoding='utf-8')
