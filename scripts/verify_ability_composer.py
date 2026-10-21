"""Native ability/composer checks and a real isolated Ability workspace capture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/m5-editor-relwithdebinfo'
CMAKE = ROOT / '.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
CTEST = CMAKE.with_name('ctest.exe')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--full-suite', action='store_true')
    parser.add_argument('--skip-capture', action='store_true')
    parser.add_argument('--capture-only', action='store_true')
    parser.add_argument('--ability', default='royal_district/combat/earth/earth_light.daability')
    parser.add_argument('--tick', type=float, default=60.)
    parser.add_argument('--backend', choices=['d3d12', 'vulkan'], default='d3d12')
    args = parser.parse_args()
    output = ROOT / 'artifacts' / ('ability-composer-' + uuid.uuid4().hex[:10])
    output.mkdir(parents=True)
    receipt = {'backend': args.backend, 'gates': [], 'capture': None}
    protected = [*ROOT.glob('content/royal_district/combat/**/*.daability'),
                 *ROOT.glob('content/royal_district/combat/**/*.daaction'),
                 *ROOT.glob('content/royal_district/combat/*.dakit'),
                 ROOT / 'content/royal_district/RoyalCombat.dascene']
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}

    def run(name, command, env=None, timeout=900):
        start = time.monotonic()
        try:
            result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                    capture_output=True, text=True, encoding='utf-8',
                                    errors='replace', timeout=timeout)
            code, log = result.returncode, result.stdout + result.stderr
        except subprocess.TimeoutExpired as error:
            code, log = -1, str(error)
        (output / (name + '.log')).write_text(log, encoding='utf-8')
        receipt['gates'].append({'name': name, 'exit': code, 'seconds': round(time.monotonic() - start, 3)})
        print(f'{name}: {"passed" if code == 0 else "FAILED"}', flush=True)
        return code == 0

    if args.build:
        from msvc_environment import activate
        command = [CMAKE, '--build', BUILD]
        if not args.full_suite:
            command += ['--target', 'DarkAngelEditor', 'ActionTimelineTests', 'AbilityTests', 'MeleeTests',
                        'CuePlayerTests', 'ComposerStructureTests', 'NativeAuthoringTests',
                        'NativeAuthoringCommandsTests', 'AbilityAssetTests', 'CombatKitAssetTests']
        if not run('build', command + ['--parallel', '2'], activate(), 3600):
            (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
            return 1
    if not args.capture_only:
        run('native-tests', [CTEST, '--test-dir', BUILD, '--output-on-failure', '-j', '1', '-R',
            r'^(M5\.(action_timeline|ability_commit|authoritative_melee|ability_assets|combat_kit_assets|composer_structure|composer_gameplay_assets|native_authoring_commands)|M7\.cue_player)$'], timeout=1800)
        if args.full_suite:
            run('full-suite', [CTEST, '--test-dir', BUILD, '--output-on-failure', '-j', '1', '--timeout', '300'], timeout=7200)
        for name in ['earth/earth_light.daability', 'earth/earth_spear.daability', 'earth.dakit', 'player.dakit']:
            run('cook-' + Path(name).stem, [BUILD / 'AssetTool.exe', 'cook', ROOT / 'content',
                ROOT / '.cache/ability-composer-verification', 'royal_district/combat/' + name])
    if not args.skip_capture:
        source = ROOT / 'content'
        asset = lambda path, key='asset': json.loads((source / path).read_text(encoding='utf-8'))[key]
        preferences = output / 'preferences.json'
        preferences.write_text(json.dumps(dict(version=1, layout='', layout_dpi=1, ui_scale=.75,
            console_info=True, console_warning=True, console_error=True)), encoding='utf-8')
        env = os.environ.copy()
        env['DAE_EDITOR_PREFERENCES'] = str(preferences)
        image = output / 'ability-workspace.png'
        run('ability-workspace', [BUILD / 'DarkAngelEditor.exe', '--registry', ROOT / '.cache/royal-scene/registry.json',
            '--cas', ROOT / '.cache/royal-scene/cas', '--model',
            asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport', 'id'),
            '--scene', source / 'royal_district/RoyalCombat.dascene', '--sources', source,
            '--asset-cache', ROOT / '.cache/editor-assets', '--character-kit', asset('royal_district/combat/player.dakit'),
            '--character-player', '00000000000000000000000000000004', '--backend', args.backend,
            '--hidden', '--no-audio', '--width', '2560', '--height', '1600', '--ability-workspace',
            '--authoring-asset', asset(args.ability), '--exercise-composer',
            '--composer-tick', str(args.tick), '--frames', '5', '--capture-workspace', '--capture', image], env)
        if image.exists():
            receipt['capture'] = str(image.relative_to(ROOT))
    after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    receipt['sources_unchanged'] = before == after
    receipt['binary_sha256'] = hashlib.sha256((BUILD / 'DarkAngelEditor.exe').read_bytes()).hexdigest()
    receipt['qualification'] = 'Native tests, native cooking, isolated GPU workspace capture; no physical pointer automation or multiplayer qualification.'
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(output / 'receipt.json', flush=True)
    return 0 if before == after and all(gate['exit'] == 0 for gate in receipt['gates']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
