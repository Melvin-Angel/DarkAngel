"""Native Royal village rendering, skin-reference and editor regression gates."""
import hashlib
import json
from pathlib import Path
import subprocess
import time
from PIL import Image, ImageChops
from prepare_royal_scene import ROOT, SOURCE, CACHE, asset, prepare

prepare()
EVIDENCE = ROOT / 'docs/implementation/evidence'
EDITOR = ROOT / 'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe'
CLIP = asset('royal_district/clips/attack.glb')
GROUND = asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf')
gates = []

def run(name, args):
    start = time.monotonic()
    result = subprocess.run(list(map(str, args)), cwd=ROOT, capture_output=True, text=True, encoding='utf8', errors='replace', timeout=120)
    (EVIDENCE / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf8')
    gates.append(dict(name=name, exit=result.returncode, seconds=round(time.monotonic()-start, 3)))
    if result.returncode:
        raise RuntimeError((result.stdout + result.stderr)[-4000:])

run('royal-document', [EDITOR.with_name('EditorCli.exe'), 'validate', SOURCE / 'royal_district/RoyalVillage.dascene'])
comparisons = []
for backend in ['d3d12', 'vulkan']:
    for pose in ['rest', 'attack']:
        paths = []
        for reference in [False, True]:
            name = 'royal-' + backend + '-' + pose + ('-reference' if reference else '-gpu')
            capture = EVIDENCE / (name + '.png')
            command = [EDITOR, '--registry', CACHE / 'registry.json', '--cas', CACHE / 'cas', '--model', GROUND,
                       '--scene', SOURCE / 'royal_district/RoyalVillage.dascene', '--backend', backend,
                       '--frames', '8', '--hidden', '--camera-close', '--capture', capture]
            if pose == 'attack':
                command += ['--pose-clip', CLIP, '--pose-tick', '18']
            if reference:
                command += ['--skin-reference']
            run(name, command)
            paths.append(capture)
        # Exact fixed native layout, image-only viewport; excludes all changing UI.
        first, second = [Image.open(path).convert('RGB').crop((247, 200, 969, 596)) for path in paths]
        differences = list(ImageChops.difference(first, second).getdata())
        maximum = max(max(pixel) for pixel in differences)
        beyond_two = sum(max(pixel) > 2 for pixel in differences)
        if maximum > 8 or beyond_two > 20:
            raise RuntimeError('GPU skin differs from canonical CPU reference')
        comparisons.append(dict(backend=backend, pose=pose, maximum_channel_error=maximum, pixels_beyond_two=beyond_two))
for backend in ['d3d12', 'vulkan']:
    rest = Image.open(EVIDENCE / ('royal-' + backend + '-rest-gpu.png')).convert('RGB').crop((247, 200, 969, 596))
    attack = Image.open(EVIDENCE / ('royal-' + backend + '-attack-gpu.png')).convert('RGB').crop((247, 200, 969, 596))
    changed = sum(max(pixel) > 8 for pixel in ImageChops.difference(rest, attack).getdata())
    if changed < 500:
        raise RuntimeError('Deformed attack did not exercise visible GPU skinning')
run('royal-editor-regression', [EDITOR, '--registry', CACHE / 'registry.json', '--cas', CACHE / 'cas', '--model', GROUND,
    '--scene', SOURCE / 'royal_district/RoyalVillage.dascene', '--backend', 'd3d12', '--frames', '8', '--hidden', '--exercise'])
receipt = dict(schema=1, base_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
    scope='Four-object native scene; D3D12/Vulkan GPU skin versus CPU rest/deformed attack reference; isolated Play/Step, undo/redo, save/open, resource reload and failed shader preservation. Interactive locomotion/combat remains integration work.',
    binary_sha256=hashlib.sha256(EDITOR.read_bytes()).hexdigest(), comparisons=comparisons, gates=gates,
    vulkan_validation='Khronos validation layer unavailable; Diligent checks enabled', online='EOS remains blocked')
(EVIDENCE / 'royal-renderer.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('Royal native scene/GPU/editor checks passed:', comparisons)
