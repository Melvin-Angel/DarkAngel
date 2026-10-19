"""Package every imported Unity clip and sample every cooked frame after reload."""
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'.cache/animation-reimport'
BUILD=ROOT/'build/m5-editor-relwithdebinfo'

def main():
    manifest=json.loads((ROOT/'content/animation/unity/clips.json').read_text())
    groups=[]
    for offset in range(0,len(manifest['clips']),64):
        clips=manifest['clips'][offset:offset+64]
        registry=WORK/'packages'/f'{offset//64:03}.json'
        registry.parent.mkdir(parents=True,exist_ok=True)
        command=[BUILD/'AssetTool.exe','package-many',ROOT/'content',WORK/'cooked',
                 clips[0]['id'],registry,*[c['id'] for c in clips[1:]]]
        subprocess.run(list(map(str,command)),cwd=ROOT,check=True,capture_output=True,text=True)
        result=subprocess.run([str(BUILD/'AnimationClipTests.exe'),'--registry',str(registry),
                               str(WORK/'cooked/cas')],cwd=ROOT,capture_output=True,text=True,timeout=900)
        groups.append(dict(registry=str(registry),clips=len(clips),exit=result.returncode,
                           output=result.stdout+result.stderr))
        (WORK/'package-validation.json').write_text(json.dumps(groups,indent=2)+'\n')
        print(result.stdout+result.stderr,flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)
    print(f"Packaged/reloaded/sampled all {len(manifest['clips'])} clips",flush=True)

if __name__=='__main__':
    main()
