"""Execute and audit vcpkg's actual full acquisition graph without building it."""
import json, pathlib, re, subprocess, time
from acquire import ROOT
from msvc_environment import activate

def main():
    started=time.monotonic()
    args=[str(ROOT/'.tools/vcpkg/vcpkg.exe'),'install','--x-wait-for-lock','--triplet','x64-windows-darkangel','--host-triplet','x64-windows-darkangel','--x-feature=m0','--x-feature=m1-tools','--x-feature=later','--x-feature=amplitude-deps','--x-feature=evaluation-flac','--only-downloads']
    try:env=activate()
    except Exception as error:
        (ROOT/'.cache/prefetch-result.json').write_text(json.dumps({'status':'blocked','exit_code':None,'reason':str(error),'command':args},indent=2)+'\n')
        print('Full native recipe resolution blocked:',error);return 1
    env['PATH']=str(ROOT/'.tools/perl/perl/bin')+';'+str(ROOT/'.tools/nasm/nasm-3.01')+';'+env['PATH']
    process=subprocess.run(args,cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=1800)
    output=process.stdout+process.stderr
    (ROOT/'.cache/vcpkg-prefetch.log').write_text(output,encoding='utf-8')
    # In download mode, port scripts intentionally halt before compilation. Those
    # CMake messages are not package builds or evidence of integration.
    failures=[line for line in output.splitlines() if re.search(r'error:|BUILD_FAILED|Failed to download|Download failed|HASH_MISMATCH',line,re.I)]
    status='passed' if process.returncode==0 and not failures else 'failed'
    result={'status':status,'exit_code':process.returncode,'seconds':round(time.monotonic()-started,3),'command':args,'downloaded_recipes':re.findall(r'Downloaded sources for (.+)',output),'failures':failures,'log':'.cache/vcpkg-prefetch.log'}
    (ROOT/'.cache/prefetch-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(status,'exit',process.returncode,'downloaded recipe records',len(result['downloaded_recipes']))
    if failures:print('\n'.join(failures))
    return int(status!='passed')

if __name__=='__main__':raise SystemExit(main())
