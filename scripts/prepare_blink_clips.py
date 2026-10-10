"""Convert the canonical-rig Blink starter clips (read-only Unity sources) into owned native clip sources.

Each clip is baked offline onto the canonical human rig by convert_animation_fixture.py,
adopted through the existing AssetService clip importer and cooked. Originals are never
modified; existing derived clips are verified by hash and left alone.
Run: python scripts/prepare_blink_clips.py
"""
import hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];SOURCE=ROOT/'content';CACHE=ROOT/'.cache/blink-clips'
TOOL=ROOT/'build/m5-editor-relwithdebinfo/AssetTool.exe'
BLENDER=r'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe'
ORIGINAL=Path(r'C:\Unity Projects\AshenRootsMP\Assets\thirdparty\3D\Blink\Art\Animations\Animations_Starter_Pack')
FOLDER='animation/blink'
# name: (source, loops). Not yet convertible with the strict canonical converter and deliberately omitted:
# SpellCast, RollBackward and Sprint lack the canonical jointItemL; Jumps holds several takes.
CLIPS={
 'get-hit':('Combat/GetHit.fbx',False),'stunned':('Combat/StunnedLoop.fbx',True),'death':('Combat/Death.fbx',False),
 'blocking':('Combat/BlockingLoop.fbx',True),'idle-combat':('Combat/IdleCombat.fbx',True),
 'punch-left':('Combat/PunchLeft.fbx',False),'punch-right':('Combat/PunchRight.fbx',False),
 'casting':('Combat/CastingLoop.fbx',True),'bow-shot':('Combat/BowShot.fbx',False),'buff':('Combat/Buff.fbx',False),
 'roll-forward':('Movement/RollForward.fbx',False),'roll-left':('Movement/RollLeft.fbx',False),'roll-right':('Movement/RollRight.fbx',False),
 'falling':('Movement/FallingLoop.fbx',True),'jump-running':('Movement/JumpWhileRunning.fbx',False),
}
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
(SOURCE/FOLDER).mkdir(parents=True,exist_ok=True);summary={};failed={}
for name,(relative,loop) in CLIPS.items():
    original=ORIGINAL/relative;output=SOURCE/FOLDER/(name+'.glb');report_path=output.with_suffix('.conversion.json');before=digest(original)
    try:
        if not output.exists():
            result=subprocess.run([BLENDER,'--background','--factory-startup','--python-exit-code','1','--python',str(ROOT/'scripts/convert_animation_fixture.py'),'--',str(original),str(SOURCE/'animation/canonical_human.daskeleton'),str(output),'--normalize-rest'],cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=300)
            if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-600:])
        report=json.loads(report_path.read_text())
        if report['source_sha256']!=before or digest(original)!=before or report['glb_sha256']!=digest(output):raise RuntimeError('original/derived hash fence')
        if not Path(str(output)+'.daimport').exists():
            subprocess.run([str(TOOL),'adopt-clip',str(SOURCE),str(CACHE),f'{FOLDER}/{name}.glb','animation/canonical_human.daskeleton',*(['loop'] if loop else [])],cwd=ROOT,check=True,capture_output=True,timeout=120)
        subprocess.run([str(TOOL),'cook',str(SOURCE),str(CACHE),f'{FOLDER}/{name}.glb'],cwd=ROOT,check=True,capture_output=True,timeout=120)
        summary[name]=dict(source=relative,loop=loop,ticks=report['ticks'],id=json.loads(Path(str(output)+'.daimport').read_text())['id'])
        print(name,'ticks',report['ticks'],'loop' if loop else 'once',flush=True)
    except Exception as error:
        failed[name]=str(error)[-400:];print(name,'FAILED',failed[name],flush=True)
(SOURCE/FOLDER/'clips.json').write_text(json.dumps(dict(pack='Blink Animations Starter Pack (canonical rig)',clips=summary,failed=failed,omitted={'spell-cast':'missing canonical jointItemL','roll-backward':'missing canonical jointItemL','sprint':'missing canonical jointItemL','jump':'multiple takes in one FBX'}),indent=2)+'\n')
if failed:raise SystemExit(1)
