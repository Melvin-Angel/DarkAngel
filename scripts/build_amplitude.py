"""Optional bounded M0 static SDK recipe check; not part of headless linkage."""
import argparse, json, pathlib, subprocess, sys, time
from acquire import ROOT
from msvc_environment import activate
from prepare_amplitude import main as prepare

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--import-only',action='store_true');args=parser.parse_args()
    env=activate();env['PATH']=str(ROOT/'.tools/flatc')+';'+str(pathlib.Path(sys.executable).parent)+';'+env['PATH']
    env['XMAKE_GLOBALDIR']=str(ROOT/'.cache/xmake-global');env['XMAKE_COLORTERM']='nocolor'
    sdk=ROOT/'.cache/amplitude-sdk';rows=[]
    def step(name,command,cwd=ROOT):
        start=time.monotonic();p=subprocess.run(command,cwd=cwd,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=1800)
        log=ROOT/'.cache/evidence'/('amplitude-'+name+'.log');log.parent.mkdir(parents=True,exist_ok=True);log.write_text(p.stdout+p.stderr,encoding='utf-8')
        rows.append({'gate':name,'command':command,'exit_code':p.returncode,'seconds':round(time.monotonic()-start,3),'log':log.relative_to(ROOT).as_posix()})
        (ROOT/'.cache/amplitude-verification.json').write_text(json.dumps(rows,indent=2)+'\n')
        print(name,'exit',p.returncode,(p.stdout+p.stderr)[-800:],flush=True)
        if p.returncode:raise RuntimeError('Amplitude '+name+' failed; evidence preserved')
    if not args.import_only:
        # Install only the exact SDK dependencies, not the whole later graph.
        step('dependencies',[str(ROOT/'.tools/vcpkg/vcpkg.exe'),'install','--triplet','x64-windows-darkangel','--host-triplet','x64-windows-darkangel','--x-no-default-features','--x-feature=amplitude-deps'])
        prepare();source=ROOT/json.loads((ROOT/'.cache/amplitude-recipe-location.json').read_text())['source_directory']
        xmake=str(ROOT/'.tools/xmake/xmake/xmake.exe')
        step('configure',[xmake,'f','-p','windows','-a','x64','-m','release','-k','static','--as_package=n','--build_samples=n','--build_tools=n','--unit_tests=n','--build_assets=n','-y'],source)
        step('build',[xmake,'build','-v','-j','2','Amplitude::Static'],source)
        step('install',[xmake,'install','-o',str(sdk),'Amplitude::Static'],source)
    cmake=str(ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe');build=ROOT/'.cache/amplitude-import-build'
    step('import-configure',[cmake,'-S',str(ROOT/'scripts/amplitude_probe'),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DDAE_ROOT='+str(ROOT),'-DAM_SDK_PATH='+str(sdk),'-DCMAKE_MAKE_PROGRAM='+str(ROOT/'.tools/ninja/ninja.exe')])
    step('import-build',[cmake,'--build',str(build),'--parallel','2','--verbose'])
    step('import-run',[str(build/'AmplitudeImportProbe.exe')])
    return 0

if __name__=='__main__':raise SystemExit(main())
