"""Qualify atomic WorldSession collision streams and network-fed prediction."""
import hashlib,json,subprocess,time
from acquire import ROOT
from msvc_environment import activate
env=activate();tools=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin';evidence=ROOT/'docs/implementation/evidence';gates=[]
def run(name,command,timeout=900):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
    log=evidence/f'm4-stream-{name}.log';log.write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(gate=name,command=list(map(str,command)),exit_code=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
    print(name,result.returncode,flush=True)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
run('build',[tools/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'])
run('focused',[ROOT/'build/m5-relwithdebinfo/CollisionStreamTests.exe'],30)
run('native-cook',[ROOT/'build/m5-relwithdebinfo/CollisionAssetTests.exe'],30)
run('ctest',[tools/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'],120)
host=subprocess.Popen([str(ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
try:
    run('gns-client',[ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe','--client'],20)
    output=host.communicate(timeout=20)[0];(evidence/'m4-stream-gns-host.log').write_text(output,encoding='utf8')
    if host.returncode:raise RuntimeError(output)
finally:
    if host.poll() is None:host.terminate();host.wait(timeout=5)
run('no-animation-build',[tools/'cmake.exe','--build','build/m4-relwithdebinfo','--target','AssetTests','EditorTests','CollisionAssetTests','CollisionStreamTests','--parallel','2'])
run('no-animation-regression',[tools/'ctest.exe','--test-dir','build/m4-relwithdebinfo','-R','M2.asset_pipeline|M2.editor_transactions|M4.collision_assets|M4.collision_stream','--output-on-failure'],60)
paths=['CMakeLists.txt','scripts/verify_collision_stream.py','tests/collision_stream_tests.cpp','tests/motor_session_tests.cpp','engine/runtime/world_session.cpp','engine/runtime/include/darkangel/world_session.hpp','content/physics/m4_cave.dacollision','tests/collision_asset_tests.cpp','engine/runtime/character_motor.cpp','engine/runtime/collision_source.cpp','engine/runtime/include/darkangel/character_motor.hpp','engine/assets/collision_cooker.cpp','engine/runtime/include/darkangel/collision_asset.hpp']
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Atomic WorldSession actor/prop/geometry collision frames, network-fed isolated owner reconciliation, bounded proxy updates, loss/reorder/duplicate and current-frame late join; clock/observer/stream barriers/budgets and M5 remain open',online='EOS live acceptance blocked; Loopback/GNS authorized',settings=dict(packet_bytes=1000,chunk_items=4,chunks=17,history_frames=31,replay_ticks=30,body_inventory=64,actors=4,meshes=16),gates=gates,source_sha256={p:digest(ROOT/p) for p in paths},binary_sha256={p:digest(ROOT/'build/m5-relwithdebinfo'/p) for p in ['CollisionStreamTests.exe','CollisionAssetTests.exe','MotorSessionTests.exe']})
(evidence/'m4-stream.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
