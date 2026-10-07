"""Read-only tool/environment detection. Absolute paths stay in ignored output."""
import json, os, pathlib, platform, shutil, subprocess
from acquire import ROOT, digest

def command(args):
    try:
        p=subprocess.run(args, capture_output=True, text=True, encoding='utf-8',errors='replace',timeout=30)
        return {'exit_code':p.returncode,'output':(p.stdout+p.stderr).strip()[:2500]}
    except (OSError,subprocess.TimeoutExpired) as e: return {'exit_code':None,'error':str(e)}

def main():
    report={'os':platform.platform(),'architecture':platform.machine(),'free_disk_bytes':shutil.disk_usage(ROOT).free,'tools':{},'blockers':[]}
    names={'git':['git','--version'],'git-lfs':['git','lfs','version'],'python':[os.sys.executable,'--version'],'powershell':['pwsh','-NoProfile','-Command','$PSVersionTable.PSVersion.ToString()'],'cmake':[str(ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'),'--version'],'ninja':[str(ROOT/'.tools/ninja/ninja.exe'),'--version'],'node':[str(ROOT/'.tools/node/node-v24.21.0-win-x64/node.exe'),'--version'],'npm':[str(ROOT/'.tools/node/node-v24.21.0-win-x64/node.exe'),str(ROOT/'.tools/node/node-v24.21.0-win-x64/node_modules/npm/bin/npm-cli.js'),'--version'],'flatc':[str(ROOT/'.tools/flatc/flatc.exe'),'--version'],'dxc':[str(ROOT/'.tools/dxc/bin/x64/dxc.exe'),'--version'],'xmake':[str(ROOT/'.tools/xmake/xmake/xmake.exe'),'--version'],'msvc':['cl'],'vcpkg':[str(ROOT/'.tools/vcpkg/vcpkg.exe'),'version']}
    for name,args in names.items():
        report['tools'][name]=command(args)
        path=shutil.which(args[0])
        if path and pathlib.Path(path).is_file():report['tools'][name]['executable_sha256']=digest(pathlib.Path(path))
    vswhere=pathlib.Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    vs=command([str(vswhere),'-all','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-format','json'])
    report['visual_studio']=vs
    if vs.get('exit_code') != 0 or vs.get('output') in ('[]',''):
        report['blockers'].append('Visual Studio 2026 Build Tools, MSVC x64/x86 v14.51.36231 and Windows 11 SDK 10.0.26100: no complete C++ installation detected by vswhere. Run the signed installer interactively; vendor terms and elevation require the user.')
    if report['architecture'] not in ('AMD64','x86_64') or platform.system()!='Windows':report['blockers'].append('Windows x64 baseline unavailable on this host')
    local=ROOT/'local.config.json'
    if local.exists():
        sdk=pathlib.Path(json.loads(local.read_text()).get('eos_sdk_root',''))
        report['eos']={}
        for file in ['Include/eos_sdk.h','Include/eos_version.h','Lib/EOSSDK-Win64-Shipping.lib','Bin/EOSSDK-Win64-Shipping.dll']:
            path=sdk/file;report['eos'][file]={'present':path.is_file(),'sha256':digest(path) if path.is_file() else None}
    else:report['blockers'].append('Set ignored local.config.json eos_sdk_root to the authorized EOS SDK directory for the later EOS integration')
    (ROOT/'.cache/environment.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'os':report['os'],'architecture':report['architecture'],'free_disk_bytes':report['free_disk_bytes'],'blockers':report['blockers']},indent=2))
    return 0

if __name__=='__main__':raise SystemExit(main())
