"""Qualify local single-thread physics and replay CPU/allocation measurements."""
import hashlib,json,subprocess,time
from acquire import ROOT
from msvc_environment import activate
env=activate();tools=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin';evidence=ROOT/'docs/implementation/evidence';gates=[]
def run(name,command,timeout=900):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
    log=evidence/f'm4-budgets-{name}.log';log.write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(gate=name,command=list(map(str,command)),exit_code=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
    print(name,result.returncode,flush=True)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
run('build',[tools/'cmake.exe','--build','build/m5-relwithdebinfo','--parallel','2'])
run('focused',[ROOT/'build/m5-relwithdebinfo/MotorBudgetTests.exe'],30)
run('native-cook',[ROOT/'build/m5-relwithdebinfo/CollisionAssetTests.exe'],30)
run('ctest',[tools/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure'],120)
host=subprocess.Popen([str(ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
try:
    run('gns-client',[ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe','--client'],20)
    output=host.communicate(timeout=20)[0];(evidence/'m4-budgets-gns-host.log').write_text(output,encoding='utf8')
    if host.returncode:raise RuntimeError(output)
finally:
    if host.poll() is None:host.terminate();host.wait(timeout=5)
run('no-animation-build',[tools/'cmake.exe','--build','build/m4-relwithdebinfo','--target','AssetTests','EditorTests','CollisionAssetTests','MotorBudgetTests','--parallel','2'])
run('no-animation-regression',[tools/'ctest.exe','--test-dir','build/m4-relwithdebinfo','-R','M2.asset_pipeline|M2.editor_transactions|M4.collision_assets|M4.local_budgets','--output-on-failure'],60)
paths=['CMakeLists.txt','scripts/verify_budgets.py','tests/motor_budget_tests.cpp','engine/runtime/collision_observer.cpp','tests/motor_session_tests.cpp','engine/runtime/world_session.cpp','engine/runtime/include/darkangel/world_session.hpp','content/physics/m4_cave.dacollision','tests/collision_asset_tests.cpp','engine/runtime/character_motor.cpp','engine/runtime/collision_source.cpp','engine/runtime/include/darkangel/character_motor.hpp','engine/assets/collision_cooker.cpp','engine/runtime/include/darkangel/collision_asset.hpp']
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
import winreg,os,platform
with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as cpu:hardware=dict(cpu=winreg.QueryValueEx(cpu,'ProcessorNameString')[0].strip(),logical_processors=os.cpu_count(),os=platform.platform())
measurement=json.loads((evidence/'m4-budgets-focused.log').read_text())
receipt=dict(schema=1,hardware=hardware,measurement=measurement,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Local four-motor/prop/cave/query/history CPU and separate C++/Jolt allocation requests; not total resident memory or production/full gameplay stress; broader M4/M5 remain open',online='EOS live acceptance blocked; Loopback/GNS authorized',settings=dict(live_ticks=1200,replay_runs=64,replay_commands=30,candidate_cpu_budget_us=16667,grounded_jitter_limit_m=.002),gates=gates,source_sha256={p:digest(ROOT/p) for p in paths},binary_sha256={p:digest(ROOT/'build/m5-relwithdebinfo'/p) for p in ['MotorBudgetTests.exe','CollisionAssetTests.exe','MotorSessionTests.exe']})
(evidence/'m4-budgets.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
