"""Focused reference layout and existing native navigation smoke check."""
import json,subprocess,hashlib,sys
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]; B=ROOT/'build/m5-editor-relwithdebinfo'; E=ROOT/'docs/implementation/evidence'
PREFIX=sys.argv[1] if len(sys.argv)>1 else "binding-navigation"
EXERCISES={"pending-reference-selection":"--exercise-pending-selection","kit-copy-recovery":"--exercise-kit-copy-undo","binding-links-view":"--exercise-binding-links","reference-draft-state":"--exercise-native-navigation","player-kit-copy":"--exercise-player-kit-copy"}
gates=[]
def run(name,cmd,env=None):
 name=name.replace('binding-navigation',PREFIX)
 r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append({'name':name,'exit':r.returncode})
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3000:])
 print(name+' passed',flush=True)
run('binding-navigation-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',B,'--target','DarkAngelEditor',*(['NativeAuthoringTests'] if PREFIX=='player-kit-copy' else []),'--parallel','2'],activate())
if PREFIX=='player-kit-copy':run('binding-navigation-native',[B/'NativeAuthoringTests.exe'])
f=json.loads((B/'authoring-fixture.json').read_text());s=Path(f['source']);c=Path(f['cache'])
scene=s/'royal_district/RoyalCombat.dascene';before=hashlib.sha256(scene.read_bytes()).hexdigest()
model=json.loads((s/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id'];collision=json.loads((s/'royal_district/collision/environment.dacollision').read_text())['asset']
run('binding-navigation-d3d12',[B/'DarkAngelEditor.exe','--registry',f['registry'],'--cas',c/'cas','--model',model,'--scene',scene,'--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',s,'--asset-cache',c,'--backend','d3d12','--hidden','--ability-workspace','--authoring-asset',f['ability'],*([EXERCISES[PREFIX]] if PREFIX in EXERCISES else []),'--frames','5','--height','1400','--capture-workspace','--capture',E/(PREFIX+'.png')])
assert hashlib.sha256(scene.read_bytes()).hexdigest()==before
(E/(PREFIX+'.json')).write_text(json.dumps({'gates':gates,'scope':PREFIX,'base_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_sha256':{str(p):hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in map(Path,['apps/editor/editor_authoring.cpp','apps/editor/editor_ui.hpp','apps/editor/main.cpp','apps/editor/native_authoring.cpp','apps/editor/native_authoring.hpp','tests/native_authoring_tests.cpp'])},'limitations':['Normal form draw and designated prefix route/history exercises; no physical pointer navigation claim.','No cook/publication/runtime change; M4/M5 In progress.']},indent=2)+'\n')
