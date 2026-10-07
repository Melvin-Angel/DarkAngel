"""Acquire only immutable records; never resolve latest versions during bootstrap."""
import argparse, concurrent.futures, hashlib, json, os, pathlib, shutil, subprocess, time, urllib.request, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def digest(path, algorithm='sha256'):
    h = hashlib.new(algorithm)
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()

def run(args, cwd=None):
    p = subprocess.run(args, cwd=cwd, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=900)
    if p.returncode: raise RuntimeError(f'{args}: exit {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p.stdout.strip()

def download(item):
    cache = ROOT / '.cache/downloads' / item['archive']
    cache.parent.mkdir(parents=True, exist_ok=True)
    algorithm = 'sha512' if 'sha512' in item else 'sha256'
    expected = item[algorithm]
    if not cache.exists():
        partial = cache.with_suffix(cache.suffix + '.partial')
        request = urllib.request.Request(item['url'], headers={'User-Agent':'DarkAngel-M0'})
        with urllib.request.urlopen(request, timeout=120) as src, partial.open('wb') as dest:
            while block := src.read(1024 * 1024): dest.write(block)
        if digest(partial, algorithm) != expected:
            raise RuntimeError(f'Checksum mismatch for {partial}; preserved for investigation')
        partial.replace(cache)
    actual = digest(cache, algorithm)
    if actual != expected: raise RuntimeError(f'{cache}: expected {expected}, actual {actual}; cache preserved')
    if item.get('vcpkg_cache'):
        mirror = ROOT / item['vcpkg_cache']
        mirror.parent.mkdir(parents=True, exist_ok=True)
        if mirror.exists() and digest(mirror, algorithm) != expected:
            raise RuntimeError(f'{mirror}: existing vcpkg cache differs; preserved')
        if not mirror.exists(): shutil.copyfile(cache, mirror)
    if item.get('destination'):
        dest = ROOT / item['destination']
        marker = dest / '.darkangel-artifact.json'
        if dest.exists() and not marker.exists():
            raise RuntimeError(f'{dest}: existing unverified extraction; preserved, use a separate destination')
        if marker.exists():
            prior = json.loads(marker.read_text())
            if prior['archive_hash'] != actual: raise RuntimeError(f'{dest}: extraction belongs to a different artifact')
            for name, sha in prior['files'].items():
                p = dest / name
                if not p.is_file() or digest(p) != sha: raise RuntimeError(f'{p}: extracted file differs from recorded hash')
        else:
            dest.mkdir(parents=True)
            with zipfile.ZipFile(cache) as archive:
                for name in archive.namelist():
                    if not (dest / name).resolve().is_relative_to(dest.resolve()): raise RuntimeError('Archive path escapes destination')
                archive.extractall(dest)
            marker.write_text(json.dumps({'archive_hash':actual,'files':{p.relative_to(dest).as_posix():digest(p) for p in dest.rglob('*') if p.is_file()}},indent=2))
    return {'name':item['name'],'status':'acquired','archive':cache.relative_to(ROOT).as_posix(),'sha256_local':digest(cache),'verification':algorithm+' matched pinned record'}

def checkout(item):
    dest = ROOT / item['destination']
    if not dest.exists():
        dest.mkdir(parents=True)
        run(['git','init',str(dest)])
        run(['git','-C',str(dest),'remote','add','origin',item['url']])
        run(['git','-C',str(dest),'fetch','--depth=1','origin',item['commit']])
        run(['git','-C',str(dest),'checkout','--detach',item['commit']])
    origin = run(['git','-C',str(dest),'remote','get-url','origin'])
    head = run(['git','-C',str(dest),'rev-parse','HEAD'])
    if origin != item['url'] or head != item['commit']: raise RuntimeError(f'{dest}: origin {origin}, HEAD {head}; expected {item["url"]} {item["commit"]}')
    dirty = run(['git','-C',str(dest),'status','--porcelain'])
    if dirty: raise RuntimeError(f'{dest}: dirty dependency preserved: {dirty}')
    if (dest / '.gitmodules').exists():
        run(['git','-C',str(dest),'submodule','update','--init','--recursive','--jobs','2'])
    attributes = [dest / '.gitattributes', *dest.glob('**/.gitattributes')]
    if any(p.is_file() and 'filter=lfs' in p.read_text(errors='replace') for p in attributes):
        run(['git','-C',str(dest),'lfs','pull'])
        run(['git','-C',str(dest),'lfs','fsck'])
    submodules = run(['git','-C',str(dest),'submodule','status','--recursive'])
    if any(line.startswith(('+','-','U')) for line in submodules.splitlines()): raise RuntimeError('Submodule mismatch: '+submodules)
    return {'name':item['name'],'status':'acquired','commit':head,'submodules':submodules.splitlines(),'active':item.get('active',True)}

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('mode',choices=['tools','sources','port-assets','vcpkg']); args=parser.parse_args()
    if args.mode=='vcpkg':
        records=json.loads((ROOT/'cmake/sources.lock.json').read_text())
        print(json.dumps(checkout({'name':'vcpkg','url':'https://github.com/microsoft/vcpkg.git','commit':records['vcpkg_commit'],'destination':'.tools/vcpkg'})))
        # Hydrate the older xsimd port objects outside vcpkg's subprocess environment.
        for file in ['portfile.cmake','usage','vcpkg.json']:
            run(['git','-C',str(ROOT/'.tools/vcpkg'),'show','4a2b1da7795e5dc452044da506646fbbb49a1b27:'+file])
        return 0
    lock={'tools':'tools.lock.json','sources':'sources.lock.json','port-assets':'port-assets.lock.json'}[args.mode]
    records=json.loads((ROOT/'cmake'/lock).read_text())
    items=records['sources' if args.mode=='sources' else 'archives']
    output=[]
    def one(item):
        start=time.monotonic()
        try: result=checkout(item) if args.mode=='sources' else download(item)
        except Exception as e: result={'name':item['name'],'status':'failed','error':str(e)}
        result['seconds']=round(time.monotonic()-start,3)
        print(result['name'],result['status'],result.get('error','')[:300],flush=True)
        return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool: output=list(pool.map(one,items))
    path=ROOT/'.cache'/('acquisition-'+args.mode+'.json');path.parent.mkdir(exist_ok=True)
    path.write_text(json.dumps(output,indent=2)+'\n')
    return int(any(i['status']=='failed' for i in output))

if __name__=='__main__': raise SystemExit(main())
