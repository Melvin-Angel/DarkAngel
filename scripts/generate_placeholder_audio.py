"""Writes the placeholder sounds of the Royal combat content: a swing, a hit and a step as short mono WAV files, two
music loops and a wind loop, and the audio assets that use the loops (sound definitions, the project mixer, a calm and
a battle profile). Existing asset files keep their identities: only missing ones are written. They are
plain stand-ins made from noise and a sine, generated so that they can be regenerated and are easy to tell from
recorded sounds. Replace them by importing real sounds over the same files."""
import json, math, random, struct, uuid
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1] / 'content/audio/placeholder'
RATE = 48000

def wave(path, samples, rate=RATE):
    data = b''.join(struct.pack('<h', max(-32767, min(32767, int(value * 32767)))) for value in samples)
    path.write_bytes(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16) + b'data' + struct.pack('<I', len(data)) + data)
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

def step(seconds=.09):
    # A footfall: a very short low thump with a little grit.
    noise = random.Random(13); angle = 0.0; out = []
    for index in range(int(RATE * seconds)):
        time = index / RATE; decay = math.exp(-time * 60)
        angle += 2 * math.pi * (55 + 60 * math.exp(-time * 80)) / RATE
        out.append((math.sin(angle) * .5 + noise.uniform(-1, 1) * .25 * math.exp(-time * 200)) * decay)
    return out

ROOT.mkdir(parents=True, exist_ok=True)
wave(ROOT / 'swing.wav', swing())
wave(ROOT / 'hit.wav', hit())
wave(ROOT / 'step.wav', step())

LOOP_RATE = 24000

def tone_loop(seconds, notes, pulse=0.0):
    # A pad: a few sines whose periods divide the loop length, so the end meets the start; an optional slow pulse.
    count = int(LOOP_RATE * seconds); out = []
    for index in range(count):
        time = index / LOOP_RATE; value = 0.0
        for cycles, level in notes:
            value += math.sin(2 * math.pi * cycles * time / seconds) * level
        if pulse:
            value *= .55 + .45 * math.sin(2 * math.pi * pulse * time / seconds) ** 2
        out.append(value)
    return out

def wind_loop(seconds=3.0):
    # Soft filtered noise whose last quarter second is blended into its first, so it loops without a click.
    noise = random.Random(17); held = 0.0; count = int(LOOP_RATE * seconds); raw = []
    for index in range(count):
        swell = .6 + .4 * math.sin(2 * math.pi * index / count)
        held += .02 * (noise.uniform(-1, 1) - held)
        raw.append(held * swell * 2.2)
    blend = LOOP_RATE // 4
    for index in range(blend):
        mix = index / blend
        raw[index] = raw[index] * mix + raw[count - blend + index] * (1 - mix)
    return raw[:count - blend]

wave(ROOT / 'music_calm.wav', tone_loop(4.0, [(440, .16), (660, .12), (880, .06)], pulse=2), LOOP_RATE)
wave(ROOT / 'music_battle.wav', tone_loop(2.0, [(220, .22), (330, .16), (441, .1)], pulse=8), LOOP_RATE)
wave(ROOT / 'wind.wav', wind_loop(), LOOP_RATE)

AUDIO = ROOT.parent
BUSES = ['master', 'music', 'ambient', 'player', 'npc', 'ui']

def identity(path):
    return json.loads(Path(str(path) + '.daimport').read_text())['id']

def native(path, value):
    # Written once; an existing file is the project's and keeps its identity and its edits.
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, indent=2) + chr(10), newline=chr(10))
    return json.loads(path.read_text())['asset']

def definition(name, title, file, bus, gain):
    return native(AUDIO / (name + '.dasound'), {'schema': 1, 'kind': 'sound', 'asset': str(uuid.uuid4()), 'name': title, 'wave': identity(ROOT / file), 'bus': bus, 'gain': gain, 'pitch': 1.0, 'loop': True, 'positioned': False, 'range': 40.0, 'effect': {'kind': 'none', 'parameters': {}}, 'sources': {'wave': 'audio/placeholder/' + file}})

calm = definition('music_calm', 'Calm music (placeholder)', 'music_calm.wav', 'music', 0.6)
battle = definition('music_battle', 'Battle music (placeholder)', 'music_battle.wav', 'music', 0.7)
wind = definition('wind', 'Wind (placeholder)', 'wind.wav', 'ambient', 0.5)
native(AUDIO / 'project.damix', {'schema': 1, 'kind': 'mix', 'asset': str(uuid.uuid4()), 'buses': {bus: {'volume': 1.0, 'mute': False} for bus in BUSES}, 'limit_each_sound': False, 'threshold_db': -1.0, 'ducking': []})

def profile(name, title, music, ambient, offsets):
    sounds = {music: 'audio/' + {calm: 'music_calm', battle: 'music_battle'}[music] + '.dasound'}
    for sound in ambient:
        sounds[sound] = 'audio/wind.dasound'
    native(AUDIO / 'profiles' / (name + '.daaudioprofile'), {'schema': 1, 'kind': 'audio_profile', 'asset': str(uuid.uuid4()), 'name': title, 'music': music, 'music_volume': 1.0, 'ambient': [{'sound': sound, 'volume': 1.0} for sound in ambient], 'bus_offsets_db': {bus: offsets.get(bus, 0.0) for bus in BUSES}, 'crossfade_seconds': 1.5, 'sources': {'sounds': sounds}})

profile('royal_calm', 'Royal district, calm', calm, [wind], {})
profile('royal_battle', 'Royal district, battle', battle, [], {'ambient': -6.0})
print('wrote', AUDIO)
