"""Writes the placeholder sounds of the Royal combat content: a swing and a hit, as short mono WAV files. They are
plain stand-ins made from noise and a sine, generated so that they can be regenerated and are easy to tell from
recorded sounds. Replace them by importing real sounds over the same files."""
import json, math, random, struct, uuid
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1] / 'content/audio/placeholder'
RATE = 48000

def wave(path, samples):
    data = b''.join(struct.pack('<h', max(-32767, min(32767, int(value * 32767)))) for value in samples)
    path.write_bytes(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE * 2, 2, 16) + b'data' + struct.pack('<I', len(data)) + data)
    sidecar = Path(str(path) + '.daimport')
    if not sidecar.exists():
        sidecar.write_text(json.dumps({'schema': 1, 'id': str(uuid.uuid4()), 'importer': 'sound-wav-v1', 'tags': [], 'subassets': {}}, indent=2) + '\n', newline='\n')

def swing(seconds=.28):
    # A whoosh: noise through a low-pass whose cutoff sweeps up and back, under a rise-and-fall envelope.
    noise = random.Random(7); held = 0.0; out = []
    for index in range(int(RATE * seconds)):
        phase = index / (RATE * seconds); envelope = math.sin(math.pi * phase) ** 2
        cutoff = .04 + .3 * math.sin(math.pi * phase)
        held += cutoff * (noise.uniform(-1, 1) - held)
        out.append(held * envelope * .9)
    return out

def hit(seconds=.16):
    # A thud: a sine that drops in pitch, with a short burst of noise at the start, decaying fast.
    noise = random.Random(11); angle = 0.0; out = []
    for index in range(int(RATE * seconds)):
        time = index / RATE; decay = math.exp(-time * 28)
        angle += 2 * math.pi * (70 + 150 * math.exp(-time * 40)) / RATE
        out.append((math.sin(angle) * .8 + noise.uniform(-1, 1) * .5 * math.exp(-time * 120)) * decay)
    return out

ROOT.mkdir(parents=True, exist_ok=True)
wave(ROOT / 'swing.wav', swing())
wave(ROOT / 'hit.wav', hit())
print('wrote', ROOT)
