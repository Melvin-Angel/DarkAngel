"""Apply exact recorded adapter patches to pinned Diligent sources, idempotently."""
import hashlib,json,pathlib,os
root=pathlib.Path(__file__).resolve().parents[1]
# Remove the obsolete development alias; recorded header patches use one spelling.
alias=root/'third_party/DiligentCore';target=root/'third_party/diligent-core'
if alias.exists():
    if os.path.normcase(os.path.realpath(alias))!=os.path.normcase(str(target.resolve())):raise RuntimeError('DiligentCore alias points to unexpected source')
    os.rmdir(alias) # Removes only this verified junction, never its target.
for patch in json.loads((root/'cmake/diligent-patches.json').read_text()):
    path=root/patch['path'];digest=hashlib.sha256(path.read_bytes()).hexdigest()
    if digest==patch['patched_sha256']:continue
    if digest!=patch['original_sha256']:raise RuntimeError('Unexpected upstream content: '+patch['path'])
    text=patch.get('replacement')
    if text is None:
        text=path.read_text(encoding='utf-8')
        for before,after in patch['replacements'].items():text=text.replace(before,after)
    path.write_bytes(text.encode())
    if hashlib.sha256(path.read_bytes()).hexdigest()!=patch['patched_sha256']:raise RuntimeError('Patch validation failed')
    print('Applied recorded patch:',patch['path'])
