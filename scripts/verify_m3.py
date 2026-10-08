"""M3 independent acceptance; online service verification requires real inputs.

This receipt never equates SDK initialization with EOS online qualification.
"""
import argparse,json,pathlib,subprocess,time
from acquire import ROOT
from msvc_environment import activate
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--skip-configure',action='store_true');args=parser.parse_args();env=activate();bin=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin';evidence=ROOT/'docs/implementation/evidence';rows=[]
    def run(gate,cmd,timeout=1800,expected=0):
        start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout);log=evidence/f'm3-{gate}.log';log.write_text(r.stdout+r.stderr,encoding='utf8');ok=(r.returncode==0) if expected==0 else r.returncode!=0;rows.append(dict(gate=gate,command=list(map(str,cmd)),exit_code=r.returncode,expected_success=expected==0,passed=ok,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()));(evidence/'m3-independent.json').write_text(json.dumps({'schema':1,'online_gate':'Unverified; requires deployment/public-client policy and two authorized identities','gates':rows},indent=2)+'\n');print(gate,'passed' if ok else (r.stdout+r.stderr)[-2400:],flush=True)
        if not ok:raise RuntimeError(f'M3 gate failed: {gate}')
        return r.stdout+r.stderr
    if not args.skip_configure:run('configure',[bin/'cmake.exe','--preset','m3-relwithdebinfo'])
    run('build',[bin/'cmake.exe','--build',ROOT/'build/m3-relwithdebinfo','--parallel','2'])
    run('ctest',[bin/'ctest.exe','--test-dir',ROOT/'build/m3-relwithdebinfo','--output-on-failure'],120)
    host=subprocess.Popen([str(ROOT/'build/m3-relwithdebinfo/GnsTests.exe'),'--host'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    try:
        run('gns-client-process',[ROOT/'build/m3-relwithdebinfo/GnsTests.exe','--client'],20)
        output=host.communicate(timeout=20)[0];(evidence/'m3-gns-host-process.log').write_text(output,encoding='utf8')
        if host.returncode:raise RuntimeError('GNS independent host process failed: '+output)
    finally:
        if host.poll() is None:host.terminate();host.wait(timeout=5)
    run('mcp',[ROOT/'.tools/node/node-v24.21.0-win-x64/node.exe',ROOT/'tools/agent-bridge/verify.mjs',ROOT/'build/m3-relwithdebinfo/AgentHost.exe'],30)
    run('shipping-configure',[bin/'cmake.exe','--preset','m1-release','-DDAE_SHIPPING=ON','-DBUILD_TESTING=OFF','-DCMAKE_INSTALL_PREFIX='+str(ROOT/'stage/m3-shipping')])
    run('shipping-build',[bin/'cmake.exe','--build',ROOT/'build/m1-release','--parallel','2'])
    targets=run('shipping-targets',[bin/'cmake.exe','--build',ROOT/'build/m1-release','--target','help'])
    if any(name in targets for name in ['AgentHost','DarkAngelAgent','EditorCli','DarkAngelEditorService']):raise RuntimeError('Shipping retained an authoring target')
    run('shipping-run',[ROOT/'build/m1-release/DarkAngelHeadless.exe'],10)
    run('shipping-invalid',[bin/'cmake.exe','--preset','m1-release','-B',ROOT/'build/m3-invalid-shipping','-DDAE_SHIPPING=ON','-DDAE_AUTHORING=ON','-DDAE_AGENT_ENDPOINTS=ON','-DBUILD_TESTING=OFF'],60,expected=1)
    return 0
if __name__=='__main__':raise SystemExit(main())
