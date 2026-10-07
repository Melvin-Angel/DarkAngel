"""Native M2 acceptance with optional local FBX conversion and cooked-only viewports.

The source FBX is read only. Generated fixtures and catalog live under .cache.
"""
import argparse, hashlib, json, pathlib, subprocess, time
from acquire import ROOT
from msvc_environment import activate

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--fbx',type=pathlib.Path)
    parser.add_argument('--blender',type=pathlib.Path,default=pathlib.Path(r'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe'))
    args=parser.parse_args()
    env=activate(); evidence=ROOT/'docs/implementation/evidence';evidence.mkdir(parents=True,exist_ok=True)
    rows=[]; profile='m2-relwithdebinfo';fixture=ROOT/'.cache/fixtures/horned-mask.glb'
    def run(name,cmd,timeout=900):
        start=time.monotonic()
        try:
            proc=subprocess.run([str(x) for x in cmd],cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=timeout)
            code=proc.returncode;output=proc.stdout+proc.stderr
        except subprocess.TimeoutExpired as error:
            code=1;output=str(error)
        log=evidence/f'm2-{name}.log';log.write_text(output,encoding='utf8')
        rows.append(dict(gate=name,command=[str(x) for x in cmd],exit_code=code,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
        (evidence/'m2.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8')
        print(name,code,output[-2200:],flush=True)
        if code:raise RuntimeError(f'M2 {name} failed; see {log}')
        return output
    if args.fbx:
        source=args.fbx.resolve();before=hashlib.sha256(source.read_bytes()).hexdigest()
        run('conversion',[args.blender,'--background','--factory-startup','--python',ROOT/'scripts/convert_fbx_fixture.py','--',source,fixture],120)
        if before!=hashlib.sha256(source.read_bytes()).hexdigest():raise RuntimeError('Source FBX changed')
    cmake=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
    run('configure',[cmake,'--preset',profile])
    run('build',[cmake,'--build','--preset',profile],1800)
    run('ctest',[cmake.with_name('ctest.exe'),'--preset',profile])
    if fixture.exists():
        node=ROOT/'.tools/node/node-v24.21.0-win-x64/node.exe'
        if node.exists():run('gltf-validator',[node,ROOT/'tools/asset-validation/validate.mjs',fixture],30)
        tool=ROOT/'build'/profile/'AssetTool.exe';source=fixture.parent;cache=ROOT/'.cache/assets-real'
        sidecar=fixture.with_suffix('.glb.daimport')
        if not sidecar.exists():run('adopt',[tool,'adopt',source,cache,fixture.name])
        asset=json.loads(sidecar.read_text())['id'];registry=source/'mask.registry.json'
        run('cook',[tool,'cook',source,cache,fixture.name],60)
        run('package',[tool,'package',source,cache,asset,registry],30)
        run('inspect',[tool,'inspect',registry,cache/'cas',asset],30)
        for backend in ['d3d12','vulkan']:
            run(backend+'-viewport',[ROOT/'build'/profile/'DarkAngelEditor.exe','--registry',registry,'--cas',cache/'cas','--model',asset,'--backend',backend,'--frames','8','--hidden','--capture',evidence/f'm2-{backend}-mask.png'],60)
    else:print('Local prop/viewports skipped: supply --fbx for the real asset gate.',flush=True)
    return 0

if __name__=='__main__':raise SystemExit(main())
