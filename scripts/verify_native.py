"""Supported Windows M0 workflow, with individual blocked gates and recorded evidence."""
import argparse, json, os, pathlib, subprocess, time, zipfile
from acquire import ROOT
from msvc_environment import activate

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--profile',choices=['m0-debug','m0-relwithdebinfo','m0-release'],default='m0-relwithdebinfo');parser.add_argument('--fresh',action='store_true',help='Preserve existing cache as evidence and reset only CMake configuration after a failed compiler detection');args=parser.parse_args()
    profile=args.profile; rows=[]; env=os.environ.copy()
    cmake=str(ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe')
    ctest=str(pathlib.Path(cmake).with_name('ctest.exe'));cpack=str(pathlib.Path(cmake).with_name('cpack.exe'))
    env['PATH']=str(pathlib.Path(cmake).parent)+';'+str(ROOT/'.tools/ninja')+';'+env.get('PATH','')
    env['VCPKG_MAX_CONCURRENCY']='2'
    def step(name,cmd,cwd=ROOT):
        start=time.monotonic()
        try:
            proc=subprocess.run(cmd,cwd=cwd,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=1800)
            output=proc.stdout+proc.stderr;code=proc.returncode
        except (OSError,subprocess.TimeoutExpired) as e:output=str(e);code=1
        log=ROOT/'.cache/evidence'/f'{profile}-{name}.log';log.parent.mkdir(parents=True,exist_ok=True);log.write_text(output,encoding='utf-8')
        rows.append({'gate':name,'command':cmd,'exit_code':code,'seconds':round(time.monotonic()-start,3),'log':log.relative_to(ROOT).as_posix(),'status':'passed' if code==0 else 'failed'})
        print(name,'exit',code,output[-600:],flush=True)
        return code==0
    try:
        env=activate()
        fixture=ROOT/'.cache/compiler-identity.c';fixture.write_text('int darkangel_compiler_identity(void) { return 0; }\n')
        compiler=pathlib.Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
        step('compiler-version',[str(compiler),'/nologo','/Bv','/MD','/c',str(fixture),'/Fo'+str(ROOT/'.cache/compiler-identity.obj')])
    except Exception as error:
        rows.append({'gate':'compiler-environment','status':'blocked','exit_code':1,'reason':str(error)})
    if args.fresh:
        cache=ROOT/'build'/profile/'CMakeCache.txt'
        if cache.exists():
            preserved=ROOT/'.cache/evidence'/f'{profile}-before-fresh-CMakeCache.txt'
            preserved.parent.mkdir(parents=True,exist_ok=True)
            __import__('shutil').copyfile(cache,preserved)
    success=step('configure',[cmake,*(['--fresh'] if args.fresh else []),'--preset',profile])
    if not success:
        for gate in ['build','ctest','no-change-build','incremental-build','restored-build','stage','package','relocated-launch']:
            rows.append({'gate':gate,'status':'blocked','exit_code':None,'reason':'Native configure failed; no build or package is claimed'})
    else:
        success=step('build',[cmake,'--build','--preset',profile,'--verbose'])
        if success:success=step('ctest',[ctest,'--preset',profile])
        if success:success=step('no-change-build',[cmake,'--build','--preset',profile,'--verbose'])
        if success:
            source=ROOT/'engine/runtime/bootstrap.cpp';original=source.read_bytes()
            try:
                source.write_bytes(original+b'\n// M0 controlled incremental rebuild observation.\n')
                success=step('incremental-build',[cmake,'--build','--preset',profile,'--verbose'])
            finally:source.write_bytes(original)
            if success:success=step('restored-build',[cmake,'--build','--preset',profile,'--verbose'])
        if success and profile!='m0-debug':
            success=step('stage',[cmake,'--install',str(ROOT/'build'/profile),'--component','HeadlessShell'])
            if success:success=step('package',[cpack,'--preset',profile])
            if success:
                configuration={'m0-relwithdebinfo':'RelWithDebInfo','m0-release':'Release'}[profile]
                archives=list((ROOT/'stage/packages'/configuration).glob('*HeadlessShell*.zip'))
                if len(archives)!=1:raise RuntimeError('Expected exactly one current HeadlessShell ZIP')
                dest=ROOT/'stage'/('relocation-'+profile+'-'+str(time.time_ns()))
                with zipfile.ZipFile(archives[0]) as z:z.extractall(dest)
                inventory=json.loads((dest/'share/DarkAngel/runtime-inventory.json').read_text())
                actual=sorted(p.relative_to(dest).as_posix() for p in dest.rglob('*') if p.is_file())
                if actual!=sorted(inventory['files']):raise RuntimeError(f'Runtime allowlist differs: {actual}')
                prior_path=env['PATH'];env['PATH']=os.environ.get('SystemRoot','C:/Windows')+'/System32'
                try:success=step('relocated-launch',[str(dest/'bin/DarkAngelHeadless.exe')],cwd=pathlib.Path(os.environ.get('TEMP',str(ROOT/'.cache'))))
                finally:env['PATH']=prior_path
        elif profile=='m0-debug':rows.append({'gate':'package','status':'excluded','reason':'Debug CRT is not distributable'})
    (ROOT/'.cache'/f'native-{profile}.json').write_text(json.dumps(rows,indent=2)+'\n')
    return int(not success)

if __name__=='__main__':raise SystemExit(main())
