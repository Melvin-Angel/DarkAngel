"""Verify the first M5 rig/skin/Ozz increment; full combat/UI gates remain open."""
import argparse,hashlib,json,pathlib,subprocess,time
from acquire import ROOT
from msvc_environment import activate

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--skip-configure',action='store_true');args=parser.parse_args();env=activate();tools=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin';evidence=ROOT/'docs/implementation/evidence';gates=[]
    def run(gate,cmd,timeout=900):
        start=time.monotonic();result=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout);log=evidence/f'm5-{gate}.log';log.write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(gate=gate,command=list(map(str,cmd)),exit_code=result.returncode,passed=result.returncode==0,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()));print(gate,result.returncode,flush=True)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-2400:])
    if not args.skip_configure:run('configure',[tools/'cmake.exe','--preset','m5-relwithdebinfo'])
    run('build',[tools/'cmake.exe','--build',ROOT/'build/m5-relwithdebinfo','--parallel','2'])
    run('rig',[ROOT/'build/m5-relwithdebinfo/AnimationTests.exe'],30)
    run('ctest',[tools/'ctest.exe','--test-dir',ROOT/'build/m5-relwithdebinfo','--output-on-failure'],120)
    # Preserve M4 receipts; independently recheck GNS against the integrated build.
    host=subprocess.Popen([str(ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    try:
        run('gns-client',[ROOT/'build/m5-relwithdebinfo/MotorSessionTests.exe','--client'],20)
        output=host.communicate(timeout=20)[0];(evidence/'m5-gns-host.log').write_text(output,encoding='utf8')
        if host.returncode:raise RuntimeError('Integrated GNS host failed: '+output)
    finally:
        if host.poll() is None:host.terminate();host.wait(timeout=5)
    # Ensure a build without Ozz still compiles affected native asset/editor code.
    run('no-animation-build',[tools/'cmake.exe','--build',ROOT/'build/m4-relwithdebinfo','--target','AssetTests','EditorTests','CollisionAssetTests','--parallel','2'])
    run('no-animation-regression',[tools/'ctest.exe','--test-dir',ROOT/'build/m4-relwithdebinfo','-R','M2.asset_pipeline|M2.editor_transactions|M4.collision_assets','--output-on-failure'],60)
    conversion=json.loads((ROOT/'.cache/fixtures/canonical-human.conversion.json').read_text());original=pathlib.Path(conversion['source']);digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    if digest(original)!=conversion['source_sha256']:raise RuntimeError('Read-only human FBX hash changed')
    paths=[p for directory in ['engine','tests','tools/editor','tools/cooker','content/animation'] for p in (ROOT/directory).rglob('*') if p.is_file() and p.suffix in ['.cpp','.hpp','.daskeleton']]+[ROOT/name for name in ['CMakeLists.txt','CMakePresets.json','vcpkg.json','scripts/convert_human_fixture.py']]
    receipt={'schema':1,'base_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'scope':'First M5 canonical-human rig, skin binding and Ozz rest-pose increment; complete animation/combat/UI acceptance remains open','ozz':'744eb9d99f606eda849acb0b1204f7a3dc20bca1','online':'M3 EOS live acceptance remains blocked; Loopback/GNS explicitly authorized','gates':gates,'fixture':conversion,'source_sha256':{p.relative_to(ROOT).as_posix():digest(p) for p in paths},'binary_sha256':{name:digest(ROOT/'build/m5-relwithdebinfo'/name) for name in ['AnimationTests.exe','MotorSessionTests.exe','AssetTool.exe']}}
    (evidence/'m5-initial.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
    return 0
if __name__=='__main__':raise SystemExit(main())
