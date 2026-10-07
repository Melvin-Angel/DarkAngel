"""Collect actual acquired license/notice files; inventory does not certify redistribution."""
import hashlib, json, pathlib, re, tarfile, zipfile
from acquire import ROOT, digest

def main():
    destination=ROOT/'third_party/notices';destination.mkdir(parents=True,exist_ok=True);rows=[]
    def is_notice(name):
        base=pathlib.PurePosixPath(name).name.lower()
        return bool(re.match(r'^(licen[cs]e|copying|copyright|notice)([._-].*|$)',base))
    def save(provider,name,data):
        if len(data)>512*1024:return
        if b'\x00' in data[:200]:return
        sha=hashlib.sha256(data).hexdigest();relative=provider+'/'+sha[:12]+'-'+re.sub(r'[^a-zA-Z0-9._-]','_',pathlib.PurePosixPath(name).name)+'.txt'
        target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        rows.append({'provider':provider,'source_file':name,'sha256':sha,'notice_file':'third_party/notices/'+relative})
    for lock in ['port-assets.lock.json','tools.lock.json','candidate-python.lock.json']:
        for item in json.loads((ROOT/'cmake'/lock).read_text(encoding='utf-8'))['archives']:
            archive=ROOT/'.cache/downloads'/item['archive'];provider=item['name'].split(':')[0]
            if archive.name.endswith(('.zip','.whl')):
                with zipfile.ZipFile(archive) as z:
                    for name in z.namelist():
                        if is_notice(name) and not name.endswith('/'):save(provider,name,z.read(name))
            elif archive.name.endswith(('.tar.gz','.tar.xz','.tgz')):
                with tarfile.open(archive) as t:
                    for member in t:
                        if member.isfile() and is_notice(member.name):save(provider,member.name,t.extractfile(member).read())
    sources=json.loads((ROOT/'cmake/sources.lock.json').read_text(encoding='utf-8'))['sources']
    for item in sources:
        source=ROOT/item['destination']
        for file in source.rglob('*'):
            if file.is_file() and '.git' not in file.parts and is_notice(file.name):save(item['name'],file.relative_to(source).as_posix(),file.read_bytes())
    for folder in ['tools/agent-bridge','tools/asset-validation']:
        source=ROOT/folder/'node_modules'
        for file in source.rglob('*'):
            if file.is_file() and is_notice(file.name):save('npm',file.relative_to(source).as_posix(),file.read_bytes())
    # Vendor SDK notices supplied by the user are retained as inventory references;
    # the entire proprietary SDK is not included in source or package.
    config=json.loads((ROOT/'local.config.json').read_text(encoding='utf-8'));sdk=pathlib.Path(config['eos_sdk_root'])
    vendor=[{'file':p.relative_to(sdk.parent).as_posix(),'sha256':digest(p)} for p in (sdk.parent/'ThirdPartyNotices').rglob('*') if p.is_file()]
    (ROOT/'docs/implementation/notice-inventory.json').write_text(json.dumps({'schema':1,'scope':'Actual acquired sources/tools/candidates, not a shipping license certification','notices':rows,'eos_notice_references':vendor},indent=2)+'\n',encoding='utf-8')
    print(f'{len(rows)} notice records retained from acquired inputs; EOS notice inventory {len(vendor)} files')

if __name__=='__main__':main()
