"""Qualify normalized real FBX clips without replacing earlier M4/M5 receipts."""
import argparse,hashlib,json,pathlib,subprocess,time
from acquire import ROOT
from msvc_environment import activate
parser=argparse.ArgumentParser();parser.add_argument('--convert',action='store_true');args=parser.parse_args()
env=activate();tools=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin';evidence=ROOT/'docs/implementation/evidence';gates=[]
inputs={'idle':'Movement/Idle.fbx','run':'Movement/RunForward.fbx','attack':'Combat/MeleeAttack_OneHanded.fbx','dodge':'Movement/RollForward.fbx'}
fbx_root=pathlib.Path(r'C:\Unity Projects\AshenRootsMP\Assets\thirdparty\3D\Blink\Art\Animations\Animations_Starter_Pack')
def run(name,command,timeout=900):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
    log=evidence/f'm5-blend-{name}.log';log.write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(gate=name,command=list(map(str,command)),exit_code=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
    print(name,result.returncode,flush=True)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
fixtures={}
for name,path in inputs.items():
    original=fbx_root/path;before=digest(original)
    if args.convert:
        run('convert-'+name,[r'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe','--background','--factory-startup','--python-exit-code','1','--python','scripts/convert_animation_fixture.py','--',original,'content/animation/canonical_human.daskeleton',f'.cache/fixtures/{name}.glb','--normalize-rest'],60)
    report=json.loads((ROOT/f'.cache/fixtures/{name}.conversion.json').read_text());fixtures[name]=report
    if before!=report['source_sha256'] or digest(original)!=before or digest(ROOT/f'.cache/fixtures/{name}.glb')!=report['glb_sha256']:raise RuntimeError('Original/derived clip hash fence: '+name)
run('build',[tools/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'])
run('focused',[ROOT/'build/m5-relwithdebinfo/AnimationClipTests.exe'],30)
run('ctest',[tools/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'],120)
host=subprocess.Popen([str(ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
try:
    run('gns-client',[ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe','--client'],20)
    output=host.communicate(timeout=20)[0];(evidence/'m5-blend-gns-host.log').write_text(output,encoding='utf8')
    if host.returncode:raise RuntimeError(output)
finally:
    if host.poll() is None:host.terminate();host.wait(timeout=5)
run('no-animation-build',[tools/'cmake.exe','--build','build/m4-relwithdebinfo','--target','AssetTests','EditorTests','CollisionAssetTests','--parallel','2'])
run('no-animation-regression',[tools/'ctest.exe','--test-dir','build/m4-relwithdebinfo','-R','M2.asset_pipeline|M2.editor_transactions|M4.collision_assets','--output-on-failure'],60)
paths=['CMakeLists.txt','scripts/verify_pose_blend.py','scripts/convert_animation_fixture.py','tests/animation_clip_tests.cpp','engine/runtime/animation_source.cpp','engine/runtime/animation_pose.cpp','engine/runtime/include/darkangel/animation.hpp','engine/assets/animation_cooker.cpp','engine/assets/asset_service.cpp','engine/assets/assets_internal.hpp','engine/assets/include/darkangel/animation_assets.hpp','engine/assets/include/darkangel/assets.hpp','tools/cooker/main.cpp']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Regular/masked private Ozz pose composition, per-instance reusable contexts/buffers and independent glTF socket/tip reference under 0.5 mm; graph/action/additive/GPU/combat remain open',online='EOS live acceptance blocked; Loopback/GNS authorized',gates=gates,fixtures=fixtures,source_sha256={p:digest(ROOT/p) for p in paths},binary_sha256={p:digest(ROOT/'build/m5-relwithdebinfo'/p) for p in ['AnimationClipTests.exe','AssetTool.exe','MotorSessionTests.exe']})
(evidence/'m5-blend.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
