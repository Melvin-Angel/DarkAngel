"""Activate the pinned Microsoft toolchain inside one child environment."""
import json, os, pathlib, subprocess
from acquire import ROOT

def activate():
    env=os.environ.copy()
    vswhere=pathlib.Path(env.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    probe=subprocess.run([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],capture_output=True,text=True,timeout=30)
    instance=probe.stdout.strip()
    if probe.returncode or not instance:raise RuntimeError('vswhere found no complete MSVC installation')
    setup=pathlib.Path(instance)/'VC/Auxiliary/Build/vcvars64.bat'
    if '"' in str(setup) or '%' in str(setup):raise RuntimeError('Unsupported shell metacharacter in compiler installation path')
    pins=json.loads((ROOT/'cmake/native-toolchain.lock.json').read_text(encoding='utf-8'))
    script=ROOT/'.cache/scoped-msvc.cmd';script.parent.mkdir(exist_ok=True)
    keys=['PATH','INCLUDE','LIB','LIBPATH','VCToolsInstallDir','VCToolsVersion','WindowsSDKVersion','WindowsSdkDir','WindowsLibPath','UCRTVersion','UniversalCRTSdkDir']
    script.write_text('@echo off\ncall "'+str(setup)+'" '+pins['windows_sdk']+' -vcvars_ver='+pins['msvc_toolset']+' >nul\nif errorlevel 1 exit /b 1\n'+'\n'.join('set '+key for key in keys)+'\n',encoding='utf-8')
    result=subprocess.run(['cmd','/d','/c',str(script)],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60)
    if result.returncode:raise RuntimeError('Pinned vcvars activation failed: '+result.stderr[:1000])
    allowed={key.upper() for key in keys}
    for line in result.stdout.splitlines():
        if '=' in line:
            key,value=line.split('=',1)
            if key.upper() in allowed:env[key.upper()]=value
    if env.get('VCTOOLSVERSION','').strip()!=pins['msvc_toolset']:raise RuntimeError('Selected MSVC toolset differs from pin')
    if env.get('WINDOWSSDKVERSION','').strip('\\ /')!=pins['windows_sdk']:raise RuntimeError('Selected Windows SDK differs from pin')
    tools=[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin',ROOT/'.tools/ninja',ROOT/'.tools/perl/perl/bin',ROOT/'.tools/nasm/nasm-3.01']
    env['PATH']=';'.join(str(path) for path in tools)+';'+env.get('PATH','')
    env['VCPKG_MAX_CONCURRENCY']='2'
    return env

if __name__=='__main__':
    env=activate()
    print(json.dumps({key:env.get(key) for key in ['VCTOOLSVERSION','WINDOWSSDKVERSION']},indent=2))
