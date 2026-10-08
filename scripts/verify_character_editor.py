"""Focused native viewport movement and preserved editor behavior checks."""
import hashlib,json,subprocess,time
from pathlib import Path
from prepare_royal_scene import ROOT,CACHE,SOURCE,asset,prepare
prepare()
evidence=ROOT/'docs/implementation/evidence'
editor=ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe'
names=['idle','omni-walk','omni-left','omni-back','omni-right','omni-run','omni-run-left','omni-run-back','omni-run-right']
clips=','.join(asset('royal_district/clips/'+name+'.glb') for name in names)
base=[editor,'--registry',CACHE/'registry.json','--cas',CACHE/'cas','--model',asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'),'--scene',SOURCE/'royal_district/RoyalVillage.dascene','--character-clips',clips]
gates=[]
for backend in ['d3d12','vulkan']:
    name='character-editor-'+backend;start=time.monotonic()
    result=subprocess.run(list(map(str,base+['--backend',backend,'--frames','120','--hidden','--exercise-character','--capture',evidence/(name+'.png')])),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=120)
    (evidence/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
    gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode or 'Native character editor tick=120' not in result.stdout:raise RuntimeError((result.stdout+result.stderr)[-4000:])
# Start the native session for Step/isolation, then check transaction/save/reload paths.
result=subprocess.run(list(map(str,base+['--backend','d3d12','--frames','8','--hidden','--exercise'])),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=120)
(evidence/'character-editor-regression.log').write_text(result.stdout+result.stderr,encoding='utf8')
gates.append(dict(name='editor-regression',exit=result.returncode))
if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-4000:])
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native editor viewport drives WorldSession/motor/achieved-movement graph and GPU pose for120 fixed ticks on D3D12/Vulkan; native Play/Step/undo/redo/save/open/reload preservation. Standing locomotion and generated solid box proxies; precise mesh collision, actual action/ability integration and broader milestone gates remain open.',gates=gates,binary_sha256=hashlib.sha256(editor.read_bytes()).hexdigest(),online='EOS blocked; no new independent GNS scene claim')
(evidence/'character-editor.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('Native character viewport and editor checks passed')
