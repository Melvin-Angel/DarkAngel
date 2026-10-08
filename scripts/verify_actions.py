"""Qualify native action timeline clocks, crossed windows and replay identities."""
import hashlib,json,subprocess,time
from acquire import ROOT
from msvc_environment import activate
env=activate();tools=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin';evidence=ROOT/'docs/implementation/evidence';gates=[]
def run(name,command,timeout=900):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
    log=evidence/f'm5-actions-{name}.log';log.write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(gate=name,command=list(map(str,command)),exit_code=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
    print(name,result.returncode,flush=True)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
run('build',[tools/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'])
run('focused',[ROOT/'build/m5-relwithdebinfo/ActionTimelineTests.exe'],30)
run('native-cook',[ROOT/'build/m5-relwithdebinfo/CollisionAssetTests.exe'],30)
run('ctest',[tools/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'],120)
host=subprocess.Popen([str(ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
try:
    run('gns-client',[ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe','--client'],20)
    output=host.communicate(timeout=20)[0];(evidence/'m5-actions-gns-host.log').write_text(output,encoding='utf8')
    if host.returncode:raise RuntimeError(output)
finally:
    if host.poll() is None:host.terminate();host.wait(timeout=5)
run('no-animation-build',[tools/'cmake.exe','--build','build/m4-relwithdebinfo','--target','AssetTests','EditorTests','CollisionAssetTests','ActionTimelineTests','--parallel','2'])
run('no-animation-regression',[tools/'ctest.exe','--test-dir','build/m4-relwithdebinfo','-R','M2.asset_pipeline|M2.editor_transactions|M4.collision_assets|M5.action_timeline','--output-on-failure'],60)
paths=['CMakeLists.txt','scripts/verify_actions.py','tests/action_timeline_tests.cpp','content/animation/m5_attack.daaction','engine/runtime/action.cpp','engine/runtime/include/darkangel/action.hpp','engine/assets/action_cooker.cpp','tools/editor/editor_document.cpp','tests/collision_asset_tests.cpp','engine/runtime/character_motor.cpp','engine/runtime/collision_source.cpp','engine/runtime/include/darkangel/character_motor.hpp','engine/assets/collision_cooker.cpp','engine/assets/asset_service.cpp','engine/assets/assets_internal.hpp','engine/assets/include/darkangel/assets.hpp']
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native action timeline foundation, integer rate/hitstop/loops/cancel, crossed intervals, cue identities and native CAS/EditorDocument; graph, root/socket/ability/authority integration remain open',online='EOS live acceptance blocked; Loopback/GNS authorized',settings=dict(units_per_tick=1024,rate_max=4096,blocks=32,loops=8,duration_ticks=600,event_budget=512,interval_budget=256,cue_ledger_budget=256),gates=gates,source_sha256={p:digest(ROOT/p) for p in paths},binary_sha256={p:digest(ROOT/'build/m5-relwithdebinfo'/p) for p in ['ActionTimelineTests.exe','CollisionAssetTests.exe','MotorSessionTests.exe']})
(evidence/'m5-actions.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
