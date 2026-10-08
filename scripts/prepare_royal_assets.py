"""Cook all 54 owned Royal District sources through the native AssetService CLI."""
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'content/royal_district/static';cache=ROOT/'.cache/royal-district'
tool=ROOT/'build/m5-relwithdebinfo/AssetTool.exe'
report=json.loads((source/'conversion.json').read_text())
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
records=[];logs=[]
def run(arguments):
    result=subprocess.run([str(tool)]+list(map(str,arguments)),cwd=ROOT,capture_output=True,text=True,timeout=60)
    logs.append(' '.join(map(str,arguments))+'\n'+result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return result.stdout
try:
    pending=[(ROOT/entry['gltf']).relative_to(source).as_posix() for entry in report['files'] if not Path(str(ROOT/entry['gltf'])+'.daimport').exists()]
    if pending:run(['adopt-many',source,cache]+pending)
    for entry in report['files']:
        original=Path(entry['source']);path=ROOT/entry['gltf'];relative=path.relative_to(source).as_posix()
        if digest(original)!=entry['source_sha256'] or digest(Path(entry['texture']))!=entry['texture_sha256'] or digest(path)!=entry['gltf_sha256']:raise RuntimeError('Source/derived hash fence: '+relative)
        for buffer,expected in entry['buffers'].items():
            if digest(path.parent/buffer)!=expected:raise RuntimeError('glTF buffer hash fence: '+relative)
        sidecar=Path(str(path)+'.daimport')
        if not sidecar.exists():run(['adopt',source,cache,relative])
        asset=json.loads(sidecar.read_text())['id'];run(['cook',source,cache,relative])
        warm=run(['cook',source,cache,relative])
        if 'changed=0' not in warm:raise RuntimeError('Unchanged native cook was not warm')
        registry=cache/'registries'/(path.stem+'.json');registry.parent.mkdir(parents=True,exist_ok=True)
        run(['package',source,cache,asset,registry]);inspection=run(['inspect',registry,cache/'cas',asset])
        records.append(dict(asset=asset,source=relative,registry=registry.relative_to(ROOT).as_posix(),registry_sha256=digest(registry),inspection=inspection.strip(),bounds=dict(minimum=entry['minimum'],maximum=entry['maximum']),triangles=entry['triangles']))
        print(path.stem,'cooked/warm/packaged',flush=True)
    receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='54 original-UV/trim static sources; native cold/warm cook and frozen registry inspection; playable scene remains integration work',tool_sha256=digest(tool),converter_sha256=digest(ROOT/'scripts/convert_royal_district.py'),files=records)
    (ROOT/'docs/implementation/evidence/royal-assets.json').write_text(json.dumps(receipt,indent=2)+'\n')
finally:
    (ROOT/'docs/implementation/evidence/royal-assets.log').write_text('\n'.join(logs),encoding='utf8')
