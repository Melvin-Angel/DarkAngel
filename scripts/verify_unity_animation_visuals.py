"""Capture corrected Unity poses and Earth gameplay in the authored Royal scene."""
import argparse
import json
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'content'
BUILD=ROOT/'build/m5-editor-relwithdebinfo'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend',choices=['d3d12','vulkan'],default='d3d12')
    parser.add_argument('--gameplay-only',action='store_true')
    args=parser.parse_args()
    cache=ROOT/'.cache/animation-reimport/visual'
    output=cache/args.backend
    output.mkdir(parents=True,exist_ok=True)
    def run(name,command):
        for attempt in range(4):
            result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,
                                  encoding='utf-8',errors='replace',timeout=900)
            if not result.returncode or 'Source has no sidecar' not in result.stderr or attempt==3:
                break
            # A concurrent bulk import can briefly expose a new model before
            # its sidecar. Retrying does not modify sources or adopt anything.
            time.sleep(.5)
        (output/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        if result.returncode:
            raise RuntimeError(name+': '+(result.stdout+result.stderr)[-3000:])
        print(name+' passed',flush=True)
    def identity(path):
        file=SOURCE/path
        data=json.loads(Path(str(file)+'.daimport').read_text()) if file.suffix in ('.glb','.gltf') else json.loads(file.read_text())
        return data.get('id',data.get('asset'))
    ground='royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'
    huts=sorted((SOURCE/'royal_district/static/mud_huts').glob('*.gltf'))[:2]
    kit='royal_district/combat/earth.dakit'
    character='characters/royal_human.dacharacter'
    roots=[kit,ground,'royal_district/character/canonical-human.glb',character,
           *[p.relative_to(SOURCE).as_posix() for p in huts]]
    for index,path in enumerate(roots):
        run('cook-'+str(index),[BUILD/'AssetTool.exe','cook',SOURCE,cache,path])
    run('package',[BUILD/'AssetTool.exe','package-many',SOURCE,cache,identity(kit),cache/'registry.json',
                   *[identity(p) for p in roots[1:]]])
    base=[BUILD/'DarkAngelEditor.exe','--registry',cache/'registry.json','--cas',cache/'cas',
          '--model',identity(ground),'--scene',SOURCE/'royal_district/RoyalVillage.dascene',
          '--character-kit',identity(kit),'--character-player','00000000000000000000000000000004',
          '--sources',SOURCE,'--asset-cache',cache,'--backend',args.backend,'--hidden',
          '--width','1920','--height','1080','--capture-workspace']
    if not args.gameplay_only:
        for clip,ticks in [('earth_overhand_punch',[30,55]),('earth_spinning_kick',[18,31]),('earth_spear_throw',[20,62])]:
            for tick in ticks:
                name=clip+'-'+str(tick)
                run(name,base+['--exercise-character-preview','--character-preview-clip',
                    identity('animation/earth/'+clip+'.glb'),'--authoring-asset',identity(character),
                    '--composer-tick',str(tick),'--frames','5','--capture',output/(name+'.png')])
    for name,script in [('punch','attack:8;wait:35;capture'),
                        ('kick','attack:8;wait:58;attack:8;wait:25;capture'),
                        ('spear-ground','attack:14;wait:12;capture'),
                        ('spear-hand','attack:14;wait:55;capture'),
                        ('spear-release','attack:14;wait:72;capture')]:
        run(name,base+['--exercise-character','--player-script',script,'--frames','160',
                      '--capture',output/(name+'.png')])

if __name__=='__main__':
    main()
