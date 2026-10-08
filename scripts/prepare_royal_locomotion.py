"""Own canonical Omni cardinal walk/run/idle sources through the native pipeline."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from prepare_royal_scene import ROOT, SOURCE, CACHE, TOOL

ORIGINAL = Path(r'C:\Unity Projects\AshenRootsMP\Assets\thirdparty\Animations\Opsive\OmniAnimation\Packs\CoreLocomotion\Animations\TPose')
BLENDER = r'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
clips = dict(**{'omni-walk':'WalkForward','omni-back':'WalkBackward','omni-left':'WalkStrafeLeft','omni-right':'WalkStrafeRight','omni-run':'RunForward','omni-run-back':'RunBackward','omni-run-left':'RunStrafeLeft','omni-run-right':'RunStrafeRight'})
records = {}
idle = ROOT / '.cache/fixtures/idle.glb'
idle_report = json.loads(idle.with_suffix('.conversion.json').read_text())
if digest(idle) != idle_report['glb_sha256'] or digest(Path(idle_report['source'])) != idle_report['source_sha256']:
    raise RuntimeError('Canonical idle hash fence')
destination = SOURCE / 'royal_district/clips/idle.glb'
if not destination.exists():
    shutil.copyfile(idle, destination)
    shutil.copyfile(idle.with_suffix('.conversion.json'), destination.with_suffix('.conversion.json'))
if not Path(str(destination) + '.daimport').exists():
    subprocess.run([str(TOOL),'adopt-clip',str(SOURCE),str(CACHE),'royal_district/clips/idle.glb','animation/canonical_human.daskeleton','loop'],cwd=ROOT,check=True,timeout=120)
subprocess.run([str(TOOL),'cook',str(SOURCE),str(CACHE),'royal_district/clips/idle.glb'],cwd=ROOT,check=True,timeout=120)
if digest(destination) != idle_report['glb_sha256'] or idle_report['canonical_sha256'] != digest(SOURCE / 'animation/canonical_human.daskeleton'):
    raise RuntimeError('Owned idle/canonical hash fence')
records['idle'] = idle_report

for name, stem in clips.items():
    original = ORIGINAL / (stem + '.fbx')
    destination = SOURCE / 'royal_district/clips' / (name + '.glb')
    report_path = destination.with_suffix('.conversion.json')
    before = digest(original)
    if not destination.exists():
        existing = ROOT / '.cache/fixtures' / (name + '.glb')
        if existing.exists() and existing.with_suffix('.conversion.json').exists():
            report = json.loads(existing.with_suffix('.conversion.json').read_text())
            if report['source_sha256'] != before or report['glb_sha256'] != digest(existing):
                raise RuntimeError('Existing conversion hash fence')
            shutil.copyfile(existing, destination)
            shutil.copyfile(existing.with_suffix('.conversion.json'), report_path)
        else:
            result = subprocess.run([BLENDER,'--background','--factory-startup','--python-exit-code','1','--python','scripts/convert_omni_fixture.py','--',str(original),'content/animation/canonical_human.daskeleton',str(destination)], cwd=ROOT, capture_output=True, text=True, timeout=120)
            (ROOT / 'docs/implementation/evidence' / ('royal-convert-' + name + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(result.stderr)
    report = json.loads(report_path.read_text())
    if digest(original) != before or report['source_sha256'] != before or report['glb_sha256'] != digest(destination) or report['canonical_sha256'] != digest(SOURCE / 'animation/canonical_human.daskeleton'):
        raise RuntimeError('Source/derived/canonical hash fence: ' + name)
    relative = destination.relative_to(SOURCE).as_posix()
    if not Path(str(destination) + '.daimport').exists():
        subprocess.run([str(TOOL),'adopt-clip',str(SOURCE),str(CACHE),relative,'animation/canonical_human.daskeleton','loop'],cwd=ROOT,check=True,timeout=120)
    subprocess.run([str(TOOL),'cook',str(SOURCE),str(CACHE),relative],cwd=ROOT,check=True,timeout=120)
    records[name] = report
    print(name, 'owned/canonical/cooked', flush=True)
(ROOT / 'docs/implementation/evidence/royal-locomotion-content.json').write_text(json.dumps(dict(schema=1,scope='Eight owned canonical Omni cardinal walk/run clips plus validated Blink idle; Omni Idle exceeds the initial conversion timing cap; original assets unchanged; directional graph/play visual acceptance remains separate',clips=records),indent=2)+'\n')
