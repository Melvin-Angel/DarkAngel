"""Exercise live Game render sizes while preserving the panel and Scene sizing."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('d3d12', 'vulkan'), default='d3d12')
    args = parser.parse_args()
    output = ROOT / 'artifacts' / ('game-resolutions-' + uuid.uuid4().hex[:10])
    subprocess.run([sys.executable, str(ROOT / 'scripts/benchmark_royal_crown.py'),
                    '--output', str(output), '--backend', args.backend, '--frames', '100',
                    '--resolution-probe', '--width', '1600', '--height', '1000', '--cases', 'authored'],
                   cwd=ROOT, check=True)
    receipt_path = output / 'receipt.json'
    receipt = json.loads(receipt_path.read_text())
    samples = {v['frame']: v for v in receipt['cases']['authored']['resolution_probe']}
    for frame, size in [(15,[1280,720]),(25,[1366,768]),(35,[1600,900]),
                        (45,[1920,1080]),(65,[1280,720]),(85,[1280,720])]:
        assert samples[frame]['target'] == size, samples[frame]
    panel = samples[15]['panel']
    assert all(samples[f]['panel'] == panel for f in (25,35,45,55)), samples
    assert samples[65]['panel'] != panel, 'Panel resize was not exercised'
    for frame in (55,75):
        assert samples[frame]['target'] == list(map(int, samples[frame]['panel'][2:])), samples[frame]
    assert not samples[75]['game'] and samples[85]['game'], 'Scene/Game restoration failed'
    assert json.loads((output / 'preferences.json').read_text())['game_resolution'] == 1
    receipt['resolution_checks_passed'] = True
    receipt_path.write_text(json.dumps(receipt, indent=2))
    print('PASS', receipt_path, flush=True)

if __name__ == '__main__':
    main()
