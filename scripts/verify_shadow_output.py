"""Render Royal with/without shadows in disposable scenes; never edit authored content."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]

def main():
    output = ROOT / 'artifacts' / ('shadow-output-' + uuid.uuid4().hex[:10])
    output.mkdir(parents=True)
    source = ROOT / 'content'
    original = source / 'royal_district/RoyalCombat.dascene'
    protected = [p for p in source.rglob('*') if p.is_file()]
    before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    manifest = json.loads(original.read_text())
    records = output / 'test.records'
    records.mkdir()
    # Only copy records referenced by the live scene, not old user history.
    for asset in manifest['assets'].values():
        for digest in asset['entities'].values():
            (records / (digest + '.json')).write_bytes((original.parent / manifest['records'] / (digest + '.json')).read_bytes())
    manifest['records'] = 'test.records'
    on = output / 'on.dascene'
    on.write_text(json.dumps(manifest))
    for asset in manifest['assets'].values():
        for entity, digest in list(asset['entities'].items()):
            data = json.loads((records / (digest + '.json')).read_text())
            settings = data.get('optional', {}).get('rendering')
            if settings:
                assert settings['shadows']['enabled'], 'Royal baseline has shadows disabled'
                settings['shadows']['enabled'] = False
                encoded = json.dumps(data, separators=(',', ':'), sort_keys=True).encode()
                replacement = hashlib.sha256(encoded).hexdigest()
                (records / (replacement + '.json')).write_bytes(encoded)
                asset['entities'][entity] = replacement
    off = output / 'off.dascene'
    off.write_text(json.dumps(manifest))
    def asset(path, key='asset'):
        return json.loads((source / path).read_text())[key]
    editor = ROOT / 'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe'
    base = [editor, '--registry', ROOT / '.cache/royal-scene/registry.json', '--cas', ROOT / '.cache/royal-scene/cas',
            '--model', asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport', 'id'),
            '--sources', source, '--asset-cache', ROOT / '.cache/editor-assets', '--character-kit',
            asset('royal_district/combat/player.dakit'), '--character-player', '00000000000000000000000000000004',
            '--hidden', '--no-audio', '--exercise-camera-look', '--frames', '30', '--width', '2560', '--height', '1600']
    receipt = {'binary_sha256': hashlib.sha256(editor.read_bytes()).hexdigest(), 'comparisons': []}
    try:
        for backend in ('d3d12', 'vulkan'):
            images = []
            for name, scene in [('on', on), ('off', off)]:
                preferences = output / 'preferences.json'
                preferences.write_text(json.dumps(dict(version=1, layout='', layout_dpi=1, ui_scale=.75,
                    console_info=True, console_warning=True, console_error=True)))
                env = dict(os.environ, DAE_EDITOR_PREFERENCES=str(preferences))
                capture = output / f'{backend}-{name}.png'
                result = subprocess.run(list(map(str, [*base, '--backend', backend, '--scene', scene, '--capture', capture])),
                    cwd=ROOT, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=600)
                log = result.stdout + result.stderr
                (output / f'{backend}-{name}.log').write_text(log, encoding='utf-8')
                assert result.returncode == 0, log[-4000:]
                rect = re.search(r'viewport_rect=(\d+),(\d+),(\d+),(\d+)', log)
                assert rect, log[-4000:]
                images.append(Image.open(capture).convert('RGB').crop(tuple(map(int, rect.groups()))))
                print(f'{backend} shadows {name} rendered', flush=True)
            lit, unshadowed = images
            # Restrict to floor below the character waist, excluding UI/nameplates/sky.
            w,h = lit.size
            region = (int(w*.25), int(h*.6), int(w*.75), int(h*.92))
            a,b = lit.crop(region),unshadowed.crop(region)
            def darkened_pixels(a, b):
                x,y = a.tobytes(),b.tobytes()
                return sum(sum(y[i:i+3])-sum(x[i:i+3])>60 for i in range(0,len(x),3))
            darkened = darkened_pixels(a,b)
            foot_region = (int(w*.475),int(h*.77),int(w*.485),int(h*.80))
            contact = darkened_pixels(lit.crop(foot_region), unshadowed.crop(foot_region))
            clear_region = (int(w*.85),int(h*.58),int(w*.95),int(h*.68))
            clear_a,clear_b = lit.crop(clear_region),unshadowed.crop(clear_region)
            acne_fraction = darkened_pixels(clear_a,clear_b)/(clear_a.width*clear_a.height)
            ImageChops.difference(lit,unshadowed).save(output / f'{backend}-shadow-difference.png')
            receipt['comparisons'].append({'backend': backend, 'darkened_floor_pixels': darkened,
                'darkened_foot_contact_pixels': contact, 'clear_floor_shadow_fraction': acne_fraction})
            assert darkened > 150, f'No visible floor shadows: {darkened}'
            assert contact > 10, f'Shadow detached from feet: {contact}'
            assert acne_fraction < .02, f'Shadow-map stripes on clear floor: {acne_fraction:.2%}'
        receipt['passed'] = True
    finally:
        receipt['protected_unchanged'] = all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in before.items())
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2))
        print(output / 'receipt.json', flush=True)
    assert receipt['protected_unchanged'], 'Authored content changed'

if __name__ == '__main__':
    main()
