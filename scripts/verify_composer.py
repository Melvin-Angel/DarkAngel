"""Focused Composer editor checks against the existing isolated authoring fixture."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/m5-editor-relwithdebinfo'
EVIDENCE=ROOT/'docs/implementation/evidence'
parser=argparse.ArgumentParser()
parser.add_argument('--build',action='store_true')
parser.add_argument('--structure',action='store_true',help='Focused structural history/reference/publication checks')
parser.add_argument('--preview',action='store_true',help='Focused frozen pose/isolation and D3D12 scrub checks')
args=parser.parse_args()
gates=[]
def run(name,command,env=None):
 start=time.monotonic()
 result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=900)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
 gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-4000:])
 print(name+' passed',flush=True)
 return result.stdout
if args.build:run('composer-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor',*(['ComposerPreviewTests'] if args.preview else []),*(['ComposerStructureTests'] if args.structure else []),'--parallel','2'],activate())
fixture=json.loads((BUILD/'authoring-fixture.json').read_text())
source=Path(fixture['source']);cache=Path(fixture['cache'])
action=json.loads((source/'royal_district/combat/heavy.daaction').read_text())['asset']
model=json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id']
collision=json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset']
base=[BUILD/'DarkAngelEditor.exe','--registry',fixture['registry'],'--cas',cache/'cas','--model',model,'--scene',source/'royal_district/RoyalCombat.dascene','--character-kit',fixture['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',source,'--asset-cache',cache,'--backend','d3d12','--hidden','--capture-workspace']
if not args.preview and not args.structure:run('composer-lanes',base+['--ability-workspace','--authoring-asset',action,'--frames','5','--capture',EVIDENCE/'composer-lanes.png'])
if args.structure:
 run('composer-structure-native',[BUILD/'ComposerStructureTests.exe',BUILD/'authoring-fixture.json'])
 run('composer-structure-action',base+['--ability-workspace','--authoring-asset',action,'--frames','5','--height','1100','--capture',EVIDENCE/'composer-structure-action.png'])
 run('composer-structure-ability',base+['--ability-workspace','--authoring-asset',fixture['ability'],'--frames','5','--height','1100','--capture',EVIDENCE/'composer-structure-ability.png'])
if args.preview:
 run('composer-preview-native',[BUILD/'ComposerPreviewTests.exe',BUILD/'authoring-fixture.json'])
 for tick in [0,20]:
  output=run('composer-preview-'+str(tick),base+['--exercise-composer','--authoring-asset',action,'--composer-tick',str(tick),'--frames','5','--height','1100','--capture',EVIDENCE/('composer-preview-'+str(tick)+'.png')])
  if 'gameplay=stopped source=unchanged' not in output:raise RuntimeError('Isolated Composer sampling missing')
files=['apps/editor/editor_composer.cpp' ,'apps/editor/editor_authoring.cpp','apps/editor/editor_ui.hpp','scripts/verify_composer.py']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Read-only native action track lanes, markers and scrub cursor linked to selected numeric block',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},binary_sha256=hashlib.sha256((BUILD/'DarkAngelEditor.exe').read_bytes()).hexdigest(),limitations=['D3D12 rendered panel check; no physical input automation or full milestone matrix.','Character pose preview, structural block editing and dragging remain later chunks.','M4/M5 remain In progress; live EOS externally blocked.'])
if args.preview:
 receipt['scope']='Frozen compatible clip/rig sampling in isolated authoring buffers; D3D12 pose preview at tick0/20 with gameplay stopped and sources unchanged'
 receipt['source_sha256'].update({p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ['apps/editor/main.cpp','apps/editor/character_preview.cpp','apps/editor/character_preview.hpp','tests/composer_preview_tests.cpp']})
 receipt['limitations']=['One D3D12 backend and scripted capture, not physical input automation or full milestone matrix.','Preview cooks a selected clip closure independently; native Save/fresh Play still validate the complete scene/action/kit closure.','Structural action editing, full graph/layer/mask/character authoring remain open; M4/M5 In progress, live EOS blocked.']
if args.structure:
 receipt['scope']='Stable native action block create/remove, grouped history, typed hit/commit fields and failed-consumer publication retention'
 receipt['source_sha256'].update({p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ['apps/editor/native_authoring.cpp','apps/editor/native_authoring.hpp','tests/composer_structure_tests.cpp','CMakeLists.txt']})
 receipt['limitations']=['Dependent references are validated on Save; removing/retyping a referenced block requires explicit ability/kit cleanup before publication.','One D3D12 scripted panel check; no physical UI automation or full milestone matrix.','Timeline gestures, graphs/layers/masks and full designers remain open. M4/M5 In progress; live EOS blocked.']
(EVIDENCE/('composer-structure.json' if args.structure else 'composer-preview.json' if args.preview else 'composer-lanes.json')).write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
