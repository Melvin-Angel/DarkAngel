"""Record current independent M3/editor evidence without claiming EOS live success."""
import hashlib,json,pathlib,re,subprocess
from acquire import ROOT

def main():
    evidence=ROOT/'docs/implementation/evidence'
    independent=json.loads((evidence/'m3-independent.json').read_text())
    if not independent['gates'] or any(not row['passed'] for row in independent['gates']):raise RuntimeError('M3 independent gates incomplete')
    for name in ['m2.json','m3-editor-captures.json']:
        rows=json.loads((evidence/name).read_text())
        if not rows or any(row['exit_code'] for row in rows):raise RuntimeError('Editor gates incomplete')
    def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    def hashes(paths):return {path.relative_to(ROOT).as_posix():digest(path) for path in sorted(set(paths))}
    paths=[]
    for directory in ['engine','apps','tests','scripts','tools/editor','tools/cooker','tools/agent-bridge','cmake']:
        paths.extend(path for path in (ROOT/directory).rglob('*') if path.is_file() and 'node_modules' not in path.parts and path.suffix in ['.cpp','.hpp','.py','.ts','.mjs','.json','.cmake'])
    paths.extend(ROOT/name for name in ['CMakeLists.txt','CMakePresets.json','vcpkg.json'])
    counts={name:int(re.search(r'100% tests passed out of (\d+)',(evidence/name).read_text()).group(1)) for name in ['m3-ctest.log','m2-ctest.log']}
    binaries=[ROOT/'build'/profile/name for profile,name in [('m3-relwithdebinfo','GnsTests.exe'),('m3-relwithdebinfo','EosTests.exe'),('m3-relwithdebinfo','OnlineProbe.exe'),('m3-relwithdebinfo','AgentHost.exe'),('m2-relwithdebinfo','DarkAngelEditor.exe'),('m1-release','DarkAngelHeadless.exe')]]
    record=dict(schema=1,base_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),working_tree='Sources hashed before local checkpoint; exact hashes below',profiles=['m3-relwithdebinfo','m2-relwithdebinfo','m1-release Shipping'],ctest_passed=counts,online_gate=independent['online_gate'],mcp_compatibility_profile='2025-11-25',source_sha256=hashes(paths),binary_sha256=hashes(binaries))
    record['evidence_sha256']=hashes(path for path in evidence.glob('m3-*') if path.suffix in ['.log','.png','.json'] and path.name!='m3-source-provenance.json')
    (evidence/'m3-source-provenance.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf8')
    # The M2 report retains its historical initial acceptance; its current receipt
    # was refreshed by verify_m2.py, so refresh the corresponding source hashes.
    m2=json.loads((evidence/'m2-source-provenance.json').read_text())
    m2.update(base_revision=record['base_revision'],working_tree=record['working_tree'],ctest=f"{counts['m2-ctest.log']}/{counts['m2-ctest.log']} passed",source_sha256=record['source_sha256'])
    m2['evidence_sha256']={path.name:digest(path) for path in sorted(evidence.glob('m2-*')) if path.suffix in ['.png','.log']}
    (evidence/'m2-source-provenance.json').write_text(json.dumps(m2,indent=2)+'\n',encoding='utf8')
    print('Recorded current source/binary/editor evidence; EOS live gate remains unverified.')
    return 0

if __name__=='__main__':raise SystemExit(main())
