"""Native complete modular skin pipeline acceptance, without claiming GPU readiness."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];env=activate();evidence=ROOT/'docs/implementation/evidence';gates=[]
def run(name,command,timeout=180):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,timeout=timeout);log=evidence/('royal-human-'+name+'.log');log.write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()));print(name,result.returncode,flush=True)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest();fixture=json.loads((ROOT/'content/royal_district/character/canonical-human.conversion.json').read_text())
if digest(Path(fixture['source']))!=fixture['source_sha256'] or digest(Path(fixture['texture']))!=fixture['texture_sha256'] or digest(ROOT/'content/royal_district/character/canonical-human.glb')!=fixture['glb_sha256']:raise RuntimeError('Human original/atlas/derived hash fence')
cmake=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin'
run('build',[cmake/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'])
run('focused',[ROOT/'build/m5-relwithdebinfo/RenderableHumanTests.exe'])
run('ctest',[cmake/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'])
run('no-animation-build',[cmake/'cmake.exe','--build','build/m4-relwithdebinfo','--target','AssetTests','EditorTests','CollisionAssetTests','--parallel','2'])
run('no-animation-checks',[cmake/'ctest.exe','--test-dir','build/m4-relwithdebinfo','-R','M2.asset_pipeline|M2.editor_transactions|M4.collision_assets','--output-on-failure'])
paths=['CMakeLists.txt','scripts/convert_modular_human.py','scripts/verify_renderable_human.py','engine/assets/animation_cooker.cpp','engine/assets/asset_service.cpp','engine/assets/assets_internal.hpp','engine/assets/gltf_cooker.cpp','engine/assets/include/darkangel/assets.hpp','engine/assets/include/darkangel/animation_assets.hpp','tests/renderable_human_tests.cpp','tools/cooker/main.cpp']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Opt-in human-gltf-v2 renderable frozen binary skin, original 14-part assembly/atlas, canonical bind validation, runtime loading and failed generation retention; GPU/visual/player/combat integration remains open',online='EOS blocked; existing independent GNS receipt unchanged',gates=gates,fixture=fixture,source_sha256={path:digest(ROOT/path) for path in paths},binary_sha256=digest(ROOT/'build/m5-relwithdebinfo/RenderableHumanTests.exe'))
(evidence/'royal-human.json').write_text(json.dumps(receipt,indent=2)+'\n')
