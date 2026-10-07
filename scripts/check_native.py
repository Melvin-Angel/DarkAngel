"""Focused pinned-toolchain checks; keeps logs/receipt for each checkpoint."""
import argparse,json,subprocess,time
from acquire import ROOT
from msvc_environment import activate

def main():
    p=argparse.ArgumentParser();p.add_argument('--profile',default='m1-debug');p.add_argument('--targets',nargs='+',default=['SessionTests']);p.add_argument('--tests',default='^M3\\.');p.add_argument('--receipt',default='m3-authority');a=p.parse_args()
    env=activate();bin=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin';evidence=ROOT/'docs/implementation/evidence';rows=[]
    steps=[('build',[bin/'cmake.exe','--build',ROOT/'build'/a.profile,'--target',*a.targets]),('test',[bin/'ctest.exe','--test-dir',ROOT/'build'/a.profile,'-R',a.tests,'--output-on-failure'])]
    for name,cmd in steps:
        start=time.monotonic();r=subprocess.run(list(map(str,cmd)),env=env,cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=1200)
        log=evidence/f'{a.receipt}-{name}.log';log.write_text(r.stdout+r.stderr,encoding='utf8');rows.append(dict(gate=name,command=list(map(str,cmd)),exit_code=r.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
        (evidence/f'{a.receipt}.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8');print(name,r.returncode,(r.stdout+r.stderr)[-2400:] if r.returncode else 'passed',flush=True)
        if r.returncode:return r.returncode
    return 0
if __name__=='__main__':raise SystemExit(main())
