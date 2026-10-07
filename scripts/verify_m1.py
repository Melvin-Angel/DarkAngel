"""Bounded M1 configure/build/behavior checks with retained evidence."""
import argparse, json, pathlib, subprocess, time
from acquire import ROOT
from msvc_environment import activate

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--profile',choices=['m1-debug','m1-relwithdebinfo','m1-release'],default='m1-debug')
    args=parser.parse_args()
    env=activate()
    cmake=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
    ctest=cmake.with_name('ctest.exe')
    evidence=ROOT/'docs/implementation/evidence'
    evidence.mkdir(parents=True,exist_ok=True)
    rows=[]
    for name,cmd in [('configure',[str(cmake),'--preset',args.profile]),
                     ('build',[str(cmake),'--build','--preset',args.profile]),
                     ('ctest',[str(ctest),'--preset',args.profile])]:
        start=time.monotonic()
        try:
            proc=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
            code=proc.returncode; output=proc.stdout+proc.stderr
        except subprocess.TimeoutExpired as e:
            code=1; output=str(e)
        log=evidence/f'{args.profile}-{name}.log'; log.write_text(output,encoding='utf-8')
        rows.append(dict(gate=name,command=cmd,exit_code=code,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
        print(name,code,output[-2200:] if code else "passed",flush=True)
        if code:break
    (evidence/f'{args.profile}.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
    return int(rows[-1]['exit_code']!=0)
if __name__=='__main__':raise SystemExit(main())
