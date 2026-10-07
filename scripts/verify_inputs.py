import json, pathlib, re, subprocess, time
from acquire import ROOT, digest, run

def main():
    evidence=[]
    def check(name,fn):
        start=time.monotonic()
        try:detail=fn();status='passed'
        except Exception as e:detail=str(e);status='failed'
        evidence.append({'check':name,'status':status,'seconds':round(time.monotonic()-start,3),'detail':detail});print(name,status,str(detail)[:250],flush=True)
    def archives():
        count=0
        for lock in ['tools.lock.json','port-assets.lock.json']:
            for item in json.loads((ROOT/'cmake'/lock).read_text())['archives']:
                algorithm='sha512' if 'sha512' in item else 'sha256'
                actual=digest(ROOT/'.cache/downloads'/item['archive'],algorithm)
                if actual!=item[algorithm]:raise RuntimeError(item['name']+' checksum mismatch')
                count+=1
        return f'{count} source/tool archives matched immutable checksums'
    def sources():
        pins=json.loads((ROOT/'cmake/sources.lock.json').read_text());count=0
        for item in pins['sources']:
            path=ROOT/item['destination']
            if run(['git','-C',str(path),'rev-parse','HEAD'])!=item['commit']:raise RuntimeError(item['name']+' HEAD mismatch')
            if run(['git','-C',str(path),'status','--porcelain']):raise RuntimeError(item['name']+' dirty tree')
            actual=run(['git','-C',str(path),'submodule','status','--recursive']).splitlines()
            pairs={line.split()[1]:line.strip().split()[0] for line in actual}
            if pairs!=item.get('submodule_pins',{}):raise RuntimeError(item['name']+' submodule pins differ')
            count+=1
        if run(['git','-C',str(ROOT/'.tools/vcpkg'),'rev-parse','HEAD'])!=pins['vcpkg_commit']:raise RuntimeError('vcpkg baseline checkout mismatch')
        return f'{count} sources and vcpkg match pins; required submodules present'
    check('archive-checksums',archives);check('source-pins',sources)
    def eos():
        config=json.loads((ROOT/'local.config.json').read_text());sdk=pathlib.Path(config['eos_sdk_root']);pins=json.loads((ROOT/'cmake/eos.lock.json').read_text())
        for name,sha in pins['files'].items():
            if digest(sdk/name)!=sha:raise RuntimeError('EOS input differs: '+name)
        return 'Authorized EOS Windows C SDK 1.19.2.1 inputs match local SHA256 pins; no online integration tested'
    check('eos-sdk-inventory',eos)
    cmake=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
    check('preset-schema',lambda:run([str(cmake),'--list-presets=all'],cwd=ROOT))
    node=ROOT/'.tools/node/node-v24.21.0-win-x64/node.exe'
    check('npm-tooling',lambda:run([str(node),'scripts/verify_tooling.mjs'],cwd=ROOT))
    for name,exe,arg,expected in [('flatc',ROOT/'.tools/flatc/flatc.exe','--version','25.12.19'),('ninja',ROOT/'.tools/ninja/ninja.exe','--version','1.13.2'),('node',node,'--version','v24.21.0')]:
        def version(exe=exe,arg=arg,expected=expected):
            result=run([str(exe),arg]);
            if expected not in result:raise RuntimeError('Tool version mismatch '+result)
            return result
        check(name+'-version',version)
    def shader_probe():
        directory=ROOT/'.cache/dxc-probe';directory.mkdir(exist_ok=True)
        source=directory/'probe.hlsl';source.write_text('float4 main(float4 p:POSITION):SV_Position { return p; }\n')
        dxc=str(ROOT/'.tools/dxc/bin/x64/dxc.exe')
        run([dxc,'-T','vs_6_0','-E','main','-Fo',str(directory/'probe.dxil'),str(source)])
        run([dxc,'-spirv','-fspv-target-env=vulkan1.2','-T','vs_6_0','-E','main','-Fo',str(directory/'probe.spv'),str(source)])
        return {'DXIL':digest(directory/'probe.dxil'),'SPIR-V':digest(directory/'probe.spv'),'scope':'Offline shader compiler smoke only; no renderer/device/Vulkan runtime validation'}
    check('dxc-dxil-spirv',shader_probe)
    def schemas():
        schemas=ROOT/'third_party/amplitude/schemas';output=ROOT/'.cache/amplitude-schemas';output.mkdir(exist_ok=True)
        count=0
        for schema in sorted(schemas.glob('*.fbs')):
            run([str(ROOT/'.tools/flatc/flatc.exe'),'-b','--schema','-I',str(schemas),'-o',str(output),str(schema)])
            count+=1
        if not count:raise RuntimeError('No actual Amplitude schemas found')
        return f'{count} actual pinned Amplitude schemas compiled with flatc 25.12.19; SDK library compilation remains pending'
    check('amplitude-schema-compiler',schemas)
    def docking():
        text=(ROOT/'third_party/imgui-docking/imgui.h').read_text()
        if 'IMGUI_HAS_DOCK' not in text or '1.92.1' not in text:raise RuntimeError('Matching docking ImGui not present')
        return 'Official ImGui 1.92.1 docking; DiligentTools provider substitution prepared, adapter compile pending'
    check('imgui-docking',docking)
    (ROOT/'.cache/independent-checks.json').write_text(json.dumps(evidence,indent=2)+'\n')
    return int(any(e['status']=='failed' for e in evidence))

if __name__=='__main__':raise SystemExit(main())
