"""GPU checks for HDR sky cache invalidation and conservative shadow/static batching.

Runs the real Crown scene against the direct evaluation/reference route.
Authored sources and preferences are not saved by the isolated runs.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import uuid
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('d3d12', 'vulkan'), default='d3d12')
    args = parser.parse_args()
    output = ROOT / 'artifacts' / ('sky-shadow-check-' + uuid.uuid4().hex[:10])
    output.mkdir(parents=True)
    receipt = dict(backend=args.backend, checks={})
    for case, probe in [('authored', True), ('shared_override', False)]:
        samples = {}
        for reference in [True, False]:
            folder = output / (case + ('-reference' if reference else '-optimized'))
            command = [sys.executable, ROOT / 'scripts/benchmark_royal_crown.py', '--backend', args.backend,
                       '--output', folder, '--frames', '100' if probe else '35', '--maximize', '--cases', case]
            if reference: command.append('--reference')
            if probe: command.append('--probe')
            subprocess.run(list(map(str, command)), cwd=ROOT, check=True, timeout=300)
            run = json.loads((folder / 'receipt.json').read_text())
            image = Image.open(folder / (case + '.png')).convert('RGB')
            rect = run['cases'][case]['viewport_rect']
            samples[reference] = (image.crop(rect), run['cases'][case])
        first, second = samples[True][0], samples[False][0]
        if first.size != second.size: raise RuntimeError('Reference/optimized viewport sizes differ')
        diff = ImageChops.difference(first, second)
        pixels = list(getattr(diff, 'get_flattened_data', diff.getdata)())
        beyond_four = sum(max(pixel) > 4 for pixel in pixels)
        mean = sum(sum(pixel) for pixel in pixels) / (len(pixels) * 3)
        # Half-float sky storage can round a final channel by one. Instancing
        # assigned surfaces also changes vertex arithmetic at isolated edges.
        limit = 30 if probe else int(len(pixels) * .004)
        if beyond_four > limit or mean > .5:
            raise RuntimeError(f'{case} image changed: {beyond_four} pixels >4, mean {mean}')
        if probe:
            events = samples[False][1]['render_probe']
            for event in ['seed', 'resize', 'cloud', 'seed_again']:
                if events[event][0] != 1: raise RuntimeError(f'{event} did not rebuild sky: {events[event]}')
            for event in ['steady', 'post', 'reenabled']:
                if events[event][0] != 0 or events[event][1] != 1:
                    raise RuntimeError(f'{event} failed sky reuse: {events[event]}')
            if events['disabled'][:2] != [0, 0]: raise RuntimeError('Disabled sky still builds or samples cache')
        elif samples[False][1]['pass_draws']['color'] >= samples[True][1]['pass_draws']['color']:
            raise RuntimeError('Matching native material overrides did not batch')
        receipt['checks'][case] = dict(pixels_beyond_four=beyond_four, mean_channel_error=mean,
                                      reference=samples[True][1], optimized=samples[False][1])
    receipt['passed'] = True
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2))
    print('PASS', output / 'receipt.json', flush=True)


if __name__ == '__main__':
    main()
