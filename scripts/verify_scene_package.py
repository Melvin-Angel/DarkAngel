"""Meaningful native multi-root publication and shared-generation rejection checks."""
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3
import subprocess
import tempfile
from contextlib import closing
from prepare_royal_scene import ROOT, SOURCE, CACHE, TOOL, prepare

prepare()
registry = CACHE / 'registry.json'
manifest = json.loads(registry.read_text())
primary, second = manifest['roots'][:2]
logs = []

def execute(args, succeeds=True):
    result = subprocess.run([str(TOOL), *map(str, args)], cwd=ROOT, capture_output=True, text=True, timeout=120)
    logs.append(' '.join(map(str, args)) + '\n' + result.stdout + result.stderr)
    if (result.returncode == 0) != succeeds:
        raise RuntimeError(result.stdout + result.stderr)
    return result

with tempfile.TemporaryDirectory(prefix='scene-package-', dir=CACHE) as temporary:
    isolated = Path(temporary)
    shutil.copytree(CACHE / 'cas', isolated / 'cas')
    with closing(sqlite3.connect(CACHE / 'catalog.sqlite')) as original, closing(sqlite3.connect(isolated / 'catalog.sqlite')) as copy:
        original.backup(copy)
    published = isolated / 'registry.json'
    execute(['package-many', SOURCE, isolated, primary, published, second])
    baseline = published.read_bytes()
    execute(['package-many', SOURCE, isolated, primary, published, primary], False)
    execute(['package-many', SOURCE, isolated, primary, published, '77777777-7777-4777-8777-777777777777'], False)
    if published.read_bytes() != baseline:
        raise RuntimeError('Invalid root selection replaced published package')
    # Malformed isolated catalog: one shared UUID has two valid-CAS generations.
    with closing(sqlite3.connect(isolated / 'catalog.sqlite')) as database:
        products = database.execute("SELECT root,id,kind,hash,extension FROM products WHERE kind='model' AND root=id").fetchall()
        first, other = products[:2]
        database.execute('INSERT INTO products(root,id,kind,hash,extension) VALUES(?,?,?,?,?)', (other[0], first[1], 'conflicting-kind', first[3], first[4]))
        database.commit()
    rejected = execute(['package-many', SOURCE, isolated, primary, published, *manifest['roots'][1:]], False)
    if 'Conflicting shared cooked product generations' not in rejected.stderr or published.read_bytes() != baseline:
        raise RuntimeError('Conflicting shared generation did not reject atomically')

result = subprocess.run([str(ROOT / 'build/m5-relwithdebinfo/AssetTests.exe')], cwd=ROOT, capture_output=True, text=True, timeout=60)
logs.append(result.stdout + result.stderr)
if result.returncode:
    raise RuntimeError('Native asset regression failed')
evidence = ROOT / 'docs/implementation/evidence'
(evidence / 'royal-scene-package.log').write_text('\n'.join(logs))
(evidence / 'royal-scene-package.json').write_text(json.dumps(dict(schema=1, base_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(), roots=manifest['roots'], products=len(manifest['assets']), registry_sha256=hashlib.sha256(registry.read_bytes()).hexdigest(), checks=['native multi-root load', 'legacy root rejection', 'missing/duplicate root retains package', 'conflicting shared generation retains package', 'native asset regression'], scope='Native package only; editor/GPU/gameplay acceptance remains separate'), indent=2) + '\n')
print('Native multi-root package and atomic failure checks passed')
