import json, pathlib
from acquire import ROOT, digest, run

lock=json.loads((ROOT/'cmake/vcpkg-tool.lock.json').read_text())
path=ROOT/'.tools/vcpkg/vcpkg.exe'
actual=digest(path)
if actual!=lock['sha256_local']:raise SystemExit(f'vcpkg executable differs: {actual}; expected {lock["sha256_local"]}')
print(run([str(path),'version']))
