"""Repeatable, read-only Royal Crown frame benchmark with feature ablations.

Frame times include GPU backpressure/presentation; section timings are CPU wall
time, not GPU timestamps. Hidden runs disable vsync. No authored files are saved.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--backend', choices=('d3d12', 'vulkan'), default='d3d12')
    parser.add_argument('--frames', type=int, default=130)
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--ui-scale', type=float, default=.75, help='Editor UI scale; use 1 to reproduce the opening benchmark layout')
    parser.add_argument('--maximize', action='store_true', help='Maximize the Scene viewport')
    parser.add_argument('--reference', action='store_true', help='Direct sky, broad shadow box and original batching for pixel/performance comparison')
    parser.add_argument('--probe', action='store_true', help='Exercise live sky edits, exposure, resize and disable/reenable; not a steady-state benchmark')
    parser.add_argument('--resolution-probe', action='store_true', help='Exercise Game render presets and Scene restoration in an isolated hidden run')
    parser.add_argument('--game-resolution', type=int, choices=range(5), default=None,
                        help='Use Game view with preset 0 viewport, 1 720p, 2 768p, 3 900p, 4 1080p')
    parser.add_argument('--camera-from', nargs=3, type=float, default=[38, 25, 42])
    parser.add_argument('--camera-target', nargs=3, type=float, default=[0, 8, -100])
    parser.add_argument('--cases', nargs='+', default=['authored', 'no_fxaa', 'no_outline', 'no_shadows', 'shadow_2048'])
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    editor = ROOT / 'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe'
    scene_path = ROOT / 'content/royal_crown/RoyalCrownTemple.dascene'
    original = scene_path.read_bytes()
    scene = json.loads(original)
    receipt = dict(backend=args.backend, binary_sha256=hashlib.sha256(editor.read_bytes()).hexdigest(),
                   scene_sha256=hashlib.sha256(original).hexdigest(), width=args.width, height=args.height,
                   camera_from=args.camera_from, camera_target=args.camera_target, maximized=args.maximize,
                   reference=args.reference,
                   probe=args.probe,
                   game_resolution=args.game_resolution,
                   ui_scale=args.ui_scale,
                   timing='steady-state frame wall time including GPU backpressure; CPU sections are not GPU time', cases={})
    for name in args.cases:
        variant = copy.deepcopy(scene)
        settings = variant['sources'][variant['root']]['entities']['00000000000000000000000000000001']['optional']['rendering']
        if name == 'no_fxaa': settings['post']['fxaa'] = False
        elif name == 'no_outline': settings['post']['outline']['enabled'] = False
        elif name == 'no_bloom': settings['post']['bloom'] = False
        elif name == 'no_clouds': settings['sky']['cloud_amount'] = 0
        elif name == 'no_shadows': settings['shadows']['enabled'] = False
        elif name == 'shadow_2048': settings['shadows']['resolution'] = 2048
        elif name == 'balanced': settings['shadows'].update(cascade_count=2, resolution=2048, distance=180)
        elif name == 'cpu_reference': settings['gpu_clustered'] = False
        elif name == 'shared_override':
            metadata = json.loads((ROOT / 'content/royal_crown/models/rc_terrain_ground_32x32.glb.daimport').read_text())
            material = json.loads((ROOT / 'content/royal_district/materials/RoyalCharacterToon.damaterial').read_text())['asset']
            slots = {value: material for key, value in metadata['subassets'].items() if key.startswith('material/')}
            for entity in variant['sources'][variant['root']]['entities'].values():
                if entity.get('model') == metadata['id']: entity['materials'] = slots.copy()
        elif name != 'authored': raise ValueError(name)
        variant_path = output / (name + '.dascene')
        variant_path.write_text(json.dumps(variant), encoding='utf-8')
        report = output / (name + '.json')
        config = output / (name + '-config.json')
        # End before the scale exercise's authoring operations. finish() writes
        # the existing steady-state receipt without placing anything or Play.
        config.write_text(json.dumps(dict(report=str(report), authoring_frames=args.frames + 1000,
                                          maximize_viewport=args.maximize,
                                          render_probe=args.probe,
                                          resolution_probe=args.resolution_probe,
                                          used_model='c0774767-2fff-44ad-afc9-418a61a93e30',
                                          new_model='c0774767-2fff-44ad-afc9-418a61a93e30')))
        prefs = output / 'preferences.json'
        prefs.write_text(json.dumps(dict(version=1, layout='', layout_dpi=1, ui_scale=args.ui_scale,
                                        game_resolution=args.game_resolution or 0,
                                        console_info=True,console_warning=True,console_error=True)))
        env = os.environ.copy()
        env['DAE_EDITOR_PREFERENCES'] = str(prefs)
        command = [editor, '--registry', ROOT / '.cache/royal-crown-temple/registry.json',
                   '--cas', ROOT / '.cache/royal-crown-temple/cas', '--model', 'c0774767-2fff-44ad-afc9-418a61a93e30',
                   '--scene', variant_path, '--sources', ROOT / 'content', '--asset-cache', ROOT / '.cache/editor-assets',
                   '--backend', args.backend, '--hidden', '--no-audio', '--frames', str(args.frames),
                   '--width', str(args.width), '--height', str(args.height), '--camera-from', *map(str, args.camera_from),
                   '--camera-target', *map(str, args.camera_target), '--exercise-scale', config, '--capture', output / (name + '.png')]
        if args.reference: command.append('--render-reference')
        if args.game_resolution is not None: command.append('--game-workspace')
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=300)
        log = result.stdout + result.stderr
        (output / (name + '.log')).write_text(log, encoding='utf-8')
        if result.returncode or not report.exists(): raise RuntimeError(log[-3000:])
        data = json.loads(report.read_text())
        rect = re.search(r'viewport_rect=(\d+),(\d+),(\d+),(\d+)', log)
        receipt['cases'][name] = dict(frames=data['authoring_frames'], draws=data['authoring_draws'],
                                      cpu_sections_ms=data['authoring_sections_ms'],
                                      render_cpu_ms=data.get('authoring_render_cpu_ms'),
                                      pass_draws=data.get('authoring_pass_draws'),
                                      optimizations=data.get('optimizations_per_frame'),
                                      render_probe=data.get('render_probe'),
                                      resolution_probe=data.get('resolution_probe'),
                                      viewport_rect=list(map(int, rect.groups())) if rect else None)
        print(name, json.dumps(receipt['cases'][name]), flush=True)
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2))
    if scene_path.read_bytes() != original: raise RuntimeError('Authored scene changed')


if __name__ == '__main__':
    main()
