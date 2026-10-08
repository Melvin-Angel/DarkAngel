"""Focused typed-import, native GUI and preserved Royal movement acceptance."""
import argparse, hashlib, json, subprocess, time, uuid
from pathlib import Path
from msvc_environment import activate

ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser()
parser.add_argument('--build',action='store_true',help='Build the affected native targets before checking.')
args=parser.parse_args()
gates=[]

def run(name,command,timeout=180,env=None):
    start=time.monotonic()
    result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
    (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
    gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
    return result.stdout

EVIDENCE.mkdir(exist_ok=True)
if args.build:
    env=activate()
    run('external-import-build',[CMAKE,'--build','build/m5-editor-relwithdebinfo','--target','DarkAngelEditor','ExternalImportTests','AssetTests','--parallel','2'],600,env)
    run('external-import-native-build',[CMAKE,'--build','build/m5-relwithdebinfo','--target','AssetTool','ExternalImportTests','AssetTests','AnimationClipTests','RenderableHumanTests','EditorTests','CharacterSceneTests','--parallel','2'],600,env)
    run('external-import-no-animation-build',[CMAKE,'--build','build/m2-relwithdebinfo','--target','ExternalImportTests','AssetTests','EditorTests','--parallel','2'],600,env)

run('external-import-native',[ROOT/'build/m5-relwithdebinfo/ExternalImportTests.exe'])
run('external-import-affected',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M2.asset_pipeline|M2.editor_transactions|M5.clips|M5.renderable_human|M5.character_scene)$'])
run('external-import-no-animation',[CMAKE.parent/'ctest.exe','--test-dir','build/m2-relwithdebinfo','--output-on-failure','-R',r'^(M2.asset_pipeline|M2.editor_transactions|M5.external_import)$'])

run('external-import-royal-regression',['python','scripts/verify_character_editor.py'],300)
registry=ROOT/'.cache/royal-scene/registry.json'
cas=ROOT/'.cache/royal-scene/cas'
source=ROOT/'content/royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'
model=json.loads(Path(str(source)+'.daimport').read_text())['id']
workspace=ROOT/'.cache'/('import-editor-'+str(uuid.uuid4()))
project=workspace/'project';project.mkdir(parents=True)
editor=ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe'
base=[editor,'--registry',registry,'--cas',cas,'--model',model,'--sources',project,'--asset-cache',workspace/'cache','--hidden','--frames','5','--capture-workspace']
source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
clips=','.join(json.loads((ROOT/('content/royal_district/clips/'+name+'.glb.daimport')).read_text())['id'] for name in ('idle','omni-walk','omni-left','omni-back','omni-right','omni-run','omni-run-left','omni-run-back','omni-run-right'))
for backend in ('d3d12','vulkan'):
    name='external-import-royal-mounted-'+backend
    output=run(name,[editor,'--registry',registry,'--cas',cas,'--model',model,'--scene',ROOT/'content/royal_district/RoyalVillage.dascene','--character-clips',clips,'--sources',ROOT/'content','--asset-cache',ROOT/'.cache/editor-assets','--backend',backend,'--hidden','--frames','120','--exercise-character','--capture-workspace','--capture',EVIDENCE/(name+'.png')])
    if 'Native character editor tick=120' not in output:raise RuntimeError('Mounted project movement marker missing')
for backend in ('d3d12','vulkan'):
    name='external-import-editor-'+backend
    output=run(name,base+['--backend',backend,'--assets-workspace','--exercise-import',source,'--capture',EVIDENCE/(name+'.png')])
    if 'Native editor import dialog validated and committed without scene mutation' not in output:raise RuntimeError('Native dialog acceptance marker missing')
    run(name+'-browse',base+['--backend',backend,'--assets-workspace','--capture',EVIDENCE/(name+'-browse.png')])
run('external-import-editor-choice',base+['--backend','d3d12','--assets-workspace','--import-preview',source,'--capture',EVIDENCE/'external-import-editor-choice.png'])
if hashlib.sha256(source.read_bytes()).hexdigest()!=source_hash:raise RuntimeError('External source changed')
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    scope='Single-file typed external imports with dependency closure, static glTF key normalization, canonical skin/normalized clip and sRGB texture profiles. Native Scene/Assets layout and modal validate/commit callbacks render on D3D12/Vulkan; Royal movement and affected source/no-animation regressions. Scripted callbacks do not qualify OS file-dialog interaction or a thumbnail picker.',
    gates=gates,workspace=str(workspace),binary_sha256=hashlib.sha256(editor.read_bytes()).hexdigest(),
    source_sha256=source_hash,limitations=['Synchronous bounded single-file import; asynchronous/batch jobs remain planned.','VFX/audio/UI/standalone materials and arbitrary FBX/rig conversion unavailable.','Asset table search/folder filters; thumbnails, tags and field picker remain planned.','M4/M5 full gates and live EOS remain open; no new independent GNS claim.'])
receipt['implementation_sha256']={path:hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in ('CMakeLists.txt','engine/assets/external_import.cpp','engine/assets/asset_service.cpp','engine/assets/include/darkangel/assets.hpp','apps/editor/main.cpp','apps/editor/editor_assets.cpp','apps/editor/editor_ui.cpp','apps/editor/editor_controller.cpp','tests/external_import_tests.cpp')}
(EVIDENCE/'external-import.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Typed import and native editor focused acceptance passed')
