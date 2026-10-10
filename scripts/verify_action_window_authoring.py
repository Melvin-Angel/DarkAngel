"""Focused action-window source authoring, native lifecycle and D3D12 forms."""
import argparse, hashlib, json, subprocess, time
from pathlib import Path
from msvc_environment import activate

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/m5-editor-relwithdebinfo'
EVIDENCE = ROOT / 'docs/implementation/evidence'
parser = argparse.ArgumentParser()
parser.add_argument('--build', action='store_true')
args = parser.parse_args()
gates = []

def run(name, command, env=None):
    start = time.monotonic()
    result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                            capture_output=True, text=True, encoding='utf8',
                            errors='replace', timeout=900)
    output = result.stdout + result.stderr
    (EVIDENCE / (name + '.log')).write_text(output, encoding='utf8')
    gates.append(dict(name=name, exit=result.returncode,
                      seconds=round(time.monotonic()-start, 3)))
    if result.returncode:
        raise RuntimeError(output[-4000:])
    print(name + ' passed', flush=True)
    return output

if args.build:
    run('action-window-authoring-build', [ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe',
        '--build', BUILD, '--target', 'DarkAngelEditor', 'NativeAuthoringTests',
        'ActionTagTests', '--parallel', '2'], activate())
output = run('action-window-authoring-native', [BUILD/'NativeAuthoringTests.exe'])
if 'Authored action-window tags:' not in output:
    raise RuntimeError('Authored source/fresh Play lifecycle marker missing')
run('action-window-authoring-lifecycle', [BUILD/'ActionTagTests.exe'])
fixture = json.loads((BUILD/'authoring-fixture.json').read_text())
source, cache = Path(fixture['source']), Path(fixture['cache'])
model = json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id']
collision = json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset']
run('action-window-authoring-panel', [BUILD/'DarkAngelEditor.exe',
    '--registry', fixture['registry'], '--cas', cache/'cas', '--model', model,
    '--scene', source/'royal_district/RoyalCombat.dascene', '--character-kit', fixture['kit'],
    '--character-collision', collision, '--character-player', '00000000000000000000000000000004',
    '--combat-target', '00000000000000000000000000000005', '--sources', source,
    '--asset-cache', cache, '--backend', 'd3d12', '--hidden', '--ability-workspace',
    '--authoring-asset', fixture['ability'], '--height', '1600', '--frames', '5',
    '--capture-workspace', '--capture', EVIDENCE/'action-window-authoring-panel.png'])
files = ['apps/editor/editor_authoring.cpp', 'tests/native_authoring_tests.cpp',
         'scripts/verify_action_window_authoring.py']
receipt = dict(schema=1, base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
    scope='Native Ability action-window tag forms, source history/validation, frozen Save and fresh WorldSession lifecycle',
    gates=gates, source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},
    binary_sha256={p:hashlib.sha256((BUILD/p).read_bytes()).hexdigest() for p in ['DarkAngelEditor.exe','NativeAuthoringTests.exe','ActionTagTests.exe']},
    limitations=['Scripted source editing and D3D12 form rendering; no physical pointer automation.',
                 'Existing invulnerability/movement-lock windows and registered owner/public tags only; no tag registry edits or utility ingress.',
                 'M4/M5 In progress; EOS blocked; M6–M9 not started.'])
(EVIDENCE/'action-window-authoring.json').write_text(json.dumps(receipt,indent=2)+'\n')
