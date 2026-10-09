"""Focused Composer editor checks against the existing isolated authoring fixture."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/m5-editor-relwithdebinfo'
EVIDENCE=ROOT/'docs/implementation/evidence'
parser=argparse.ArgumentParser()
parser.add_argument('--build',action='store_true')
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
if args.build:run('composer-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','--parallel','2'],activate())
fixture=json.loads((BUILD/'authoring-fixture.json').read_text())
source=Path(fixture['source']);cache=Path(fixture['cache'])
action=json.loads((source/'royal_district/combat/heavy.daaction').read_text())['asset']
model=json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id']
collision=json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset']
base=[BUILD/'DarkAngelEditor.exe','--registry',fixture['registry'],'--cas',cache/'cas','--model',model,'--scene',source/'royal_district/RoyalCombat.dascene','--character-kit',fixture['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--sources',source,'--asset-cache',cache,'--backend','d3d12','--hidden','--capture-workspace']
run('composer-lanes',base+['--ability-workspace','--authoring-asset',action,'--frames','5','--capture',EVIDENCE/'composer-lanes.png'])
files=['apps/editor/editor_composer.cpp','apps/editor/editor_authoring.cpp','apps/editor/editor_ui.hpp','scripts/verify_composer.py']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Read-only native action track lanes, markers and scrub cursor linked to selected numeric block',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},binary_sha256=hashlib.sha256((BUILD/'DarkAngelEditor.exe').read_bytes()).hexdigest(),limitations=['D3D12 rendered panel check; no physical input automation or full milestone matrix.','Character pose preview, structural block editing and dragging remain later chunks.','M4/M5 remain In progress; live EOS externally blocked.'])
(EVIDENCE/'composer-lanes.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
