"""Prepare the Royal scene through the existing native cook and document formats."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'content'
CACHE = ROOT / '.cache/royal-scene'
TOOL = ROOT / 'build/m5-relwithdebinfo/AssetTool.exe'

def run(*args):
    subprocess.run([str(TOOL), *map(str, args)], cwd=ROOT, check=True, timeout=120)

def asset(relative):
    return json.loads(Path(str(SOURCE / relative) + '.daimport').read_text())['id']

def prepare():
    static = 'royal_district/static/'
    ground = static + 'terrain/SM_RC_Terrain_Ground_32x32.gltf'
    huts = sorted((SOURCE / static / 'mud_huts').glob('*.gltf'))
    chosen = [ground, *[p.relative_to(SOURCE).as_posix() for p in huts[:2]]]
    human = 'royal_district/character/canonical-human.glb'
    if not Path(str(SOURCE / human) + '.daimport').exists():
        run('adopt-human-renderable', SOURCE, CACHE, human, 'animation/canonical_human.daskeleton')
    for relative in chosen:
        run('cook', SOURCE, CACHE, relative)
    run('cook', SOURCE, CACHE, human)
    clips = sorted((SOURCE / 'royal_district/clips').glob('*.glb'))
    for clip in clips:
        run('cook', SOURCE, CACHE, clip.relative_to(SOURCE).as_posix())
    graph_source = 'royal_district/locomotion.dagraph'
    run('cook', SOURCE, CACHE, graph_source)
    graph_id = json.loads((SOURCE / graph_source).read_text())['asset']
    kit_source = 'royal_district/combat/player.dakit'
    run('cook', SOURCE, CACHE, kit_source)
    kit_id = json.loads((SOURCE / kit_source).read_text())['asset']
    input_source = 'input/royal_player.dainput'
    run('cook', SOURCE, CACHE, input_source)
    input_id = json.loads((SOURCE / input_source).read_text())['asset']
    run('package-many', SOURCE, CACHE, asset(ground), CACHE / 'registry.json', *[asset(p) for p in chosen[1:]], asset(human), *[asset(p.relative_to(SOURCE).as_posix()) for p in clips], input_id, graph_id, kit_id)
    for relative in chosen:
        run('inspect', CACHE / 'registry.json', CACHE / 'cas', asset(relative))
    root = '492cd837-3bf6-45d7-b640-b0470c5cec82'
    entities = {}
    def place(index, name, model, x, y, z, yaw=0):
        entities[f'{index:032x}'] = dict(name=name, model=model, target=None, types={
            '1': dict(version=2, fields={'1': yaw, '2': x, '3': y, '4': z, '5': 0, '6': 0, '7': 1}),
            '2': dict(version=1, fields={'1': 100, '2': 100})})
    place(1, 'Temple ground', asset(ground), 0, 0, 0)
    place(2, huts[0].stem, asset(chosen[1]), -7, .5, -4)
    place(3, huts[1].stem, asset(chosen[2]), 7, .5, -4)
    place(4, 'Canonical player', asset(human), 0, .5, 3)
    graph = dict(schema=1, asset=root, entities=entities, mounts={})
    output = ROOT / 'content/royal_district/RoyalVillage.dascene'
    write_scene(output, dict(schema=1, root=root, placement=f'{1:032x}', sources={root: graph}))
    combat_root = '6b91b527-31be-4b15-aa9c-2c0b9970457b'
    place(5, 'Training target', asset(human), 0, .5, 4.2, 3.141592653589793)
    combat_graph = dict(schema=1, asset=combat_root, entities=entities, mounts={})
    write_scene(SOURCE / 'royal_district/RoyalCombat.dascene', dict(schema=1, root=combat_root, placement=f'{1:032x}', sources={combat_root: combat_graph}))
    print('Scene:', output, '\nPrimary model:', asset(ground), flush=True)

def write_scene(path, data):
    payload=(json.dumps(data, indent=2) + '\n').encode('utf8')
    if path.exists() and path.read_bytes().replace(b'\r\n', b'\n')==payload:
        return
    path.write_bytes(payload)

if __name__ == '__main__':
    prepare()
