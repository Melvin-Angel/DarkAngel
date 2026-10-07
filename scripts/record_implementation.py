"""Record current source/test/provider evidence without changing dependency pins."""
import hashlib,json,pathlib,subprocess
from acquire import ROOT

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    evidence=ROOT/'docs/implementation/evidence'
    def record(name,value):(evidence/name).write_text(json.dumps(value,indent=2)+'\n',encoding='utf8')
    profiles=['m1-debug']
    for profile in profiles:
        rows=json.loads((evidence/(profile+'.json')).read_text())
        if len(rows)!=3 or any(row['exit_code'] for row in rows):raise RuntimeError('Incomplete profile receipt: '+profile)
        if '100% tests passed out of 22' not in (evidence/(profile+'-ctest.log')).read_text():raise RuntimeError('Unexpected M1 test count')
    m2=json.loads((evidence/'m2.json').read_text())
    if any(row['exit_code'] for row in m2) or not {'ctest','d3d12-viewport','vulkan-viewport'}.issubset({row['gate'] for row in m2}):raise RuntimeError('Incomplete M2 receipt')
    if '100% tests passed out of 26' not in (evidence/'m2-ctest.log').read_text():raise RuntimeError('Unexpected M2 test count')
    base=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    paths=[]
    for directory in ['engine','apps','tests','scripts','tools/editor','tools/cooker','games/AshenRoots/scripts','games/AshenRoots/shaders']:
        paths.extend(path for path in (ROOT/directory).rglob('*') if path.is_file() and path.suffix in ['.cpp','.hpp','.py','.luau','.hlsl'])
    paths.extend(ROOT/path for path in ['CMakeLists.txt','CMakePresets.json','vcpkg.json','cmake/later-recipes.cmake','cmake/diligent-patches.json','cmake/m1-dependencies.lock.json'])
    hashes={path.relative_to(ROOT).as_posix():digest(path) for path in sorted(set(paths))}
    receipt=dict(schema=1,base_revision=base,working_tree='uncommitted implementation',profiles=profiles,ctest_per_profile='22/22 Debug; optimized M1 coverage in m2-relwithdebinfo 26/26',historical_profiles=['m1-relwithdebinfo','m1-release'],source_sha256=hashes)
    record('m1-source-provenance.json',receipt)
    conversion=json.loads((ROOT/'.cache/fixtures/horned-mask.conversion.json').read_text())
    if digest(pathlib.Path(conversion['source']))!=conversion['source_sha256']:raise RuntimeError('Original FBX changed')
    conversion['sidecar']=json.loads((ROOT/'.cache/fixtures/horned-mask.glb.daimport').read_text())
    record('m2-real-asset-provenance.json',conversion)
    receipt=dict(schema=1,base_revision=base,working_tree='uncommitted implementation',profile='m2-relwithdebinfo',ctest='26/26 passed',viewports=['d3d12','vulkan'],source_sha256=hashes)
    receipt['evidence_sha256']={path.name:digest(path) for path in sorted(evidence.glob('m2-*')) if path.suffix in ['.png','.log']}
    record('m2-source-provenance.json',receipt)
    inventory=json.loads((ROOT/'docs/implementation/dependencies.json').read_text())
    providers={'abseil','cgltf','directxmath','directxtex','flecs','libpng','luau','meshoptimizer','sqlite3','zlib'}
    selected=[{key:value for key,value in row.items() if key not in ['status','native_build','integration','active']} for row in inventory['ports'] if row['name'] in providers]
    record('m2-provider-integration.json',dict(schema=1,baseline=inventory['vcpkg_checkout_and_baseline'],pins=selected,native_build='m2-relwithdebinfo verified',integration='bounded asset/editor slice; see M2_REPORT.md',installed_status_sha256=digest(ROOT/'build/m2-relwithdebinfo/vcpkg_installed/vcpkg/status'),adapter_patches_sha256=digest(ROOT/'cmake/diligent-patches.json')))
    m3=json.loads((evidence/'m3-loopback.json').read_text())
    if any(row['exit_code'] for row in m3):raise RuntimeError('M3 follow-up failed')
    record('m3-source-provenance.json',dict(schema=1,base_revision=base,working_tree='uncommitted implementation',profile='m2-relwithdebinfo',case='M3.loopback passed including endpoint cleanup',source_sha256={name:value for name,value in hashes.items() if 'transport' in name or 'session_tests' in name}))
    print('Recorded current M1/M2/M3 source, provider and unchanged FBX provenance.')
if __name__=='__main__':main()
