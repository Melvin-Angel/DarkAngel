"""M4 native acceptance. EOS remains blocked; this verifies the initial primitive collision profile."""
import argparse,hashlib,json,pathlib,subprocess,time
from acquire import ROOT
from msvc_environment import activate

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--skip-configure',action='store_true');args=parser.parse_args()
    env=activate();tools=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin';evidence=ROOT/'docs/implementation/evidence';rows=[]
    def run(gate,cmd,timeout=600):
        start=time.monotonic();result=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
        log=evidence/f'm4-{gate}.log';log.write_text(result.stdout+result.stderr,encoding='utf8');rows.append(dict(gate=gate,command=list(map(str,cmd)),exit_code=result.returncode,passed=result.returncode==0,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()));print(gate,result.returncode,flush=True)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-3000:])
    if not args.skip_configure:run('configure',[tools/'cmake.exe','--preset','m4-relwithdebinfo'])
    run('build',[tools/'cmake.exe','--build',ROOT/'build/m4-relwithdebinfo','--parallel','2'])
    run('ctest',[tools/'ctest.exe','--test-dir',ROOT/'build/m4-relwithdebinfo','--output-on-failure'],120)
    host=subprocess.Popen([str(ROOT/'build/m4-relwithdebinfo/MotorSessionTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    try:
        run('gns-client',[ROOT/'build/m4-relwithdebinfo/MotorSessionTests.exe','--client'],20)
        output=host.communicate(timeout=20)[0];(evidence/'m4-gns-host.log').write_text(output,encoding='utf8')
        if host.returncode:raise RuntimeError('Independent GNS host failed: '+output)
    finally:
        if host.poll() is None:host.terminate();host.wait(timeout=5)
    files=[p for base in ['engine/foundation','engine/runtime','engine/assets','tests'] for p in (ROOT/base).rglob('*') if p.is_file() and p.suffix in {'.cpp','.hpp'}]+[ROOT/'CMakeLists.txt',ROOT/'CMakePresets.json',ROOT/'vcpkg.json']
    receipt={'schema':1,'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty':True,'profile':'Windows x64 optimized /MD; Jolt 5.6.0; fixed 60 Hz; primitive static/kinematic collision; Loopback/GNS','online':'EOS live acceptance blocked on deployment/policy and two identities; M3 not fully Verified','gates':rows,'sha256':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    (evidence/'m4-acceptance.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
    return 0
if __name__=='__main__':raise SystemExit(main())
