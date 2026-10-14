"""M8 items and projected RmlUi gates. Requires the completed authoring fixture.

Uses disposable item sources and the existing same-machine GNS party scenarios.
Screen captures and receipts do not qualify physical pointer input or two machines.
"""
import json, math, runpy, subprocess, time, uuid
from pathlib import Path
from msvc_environment import activate
ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/m5-editor-relwithdebinfo'
EVIDENCE = ROOT / 'docs/implementation/evidence'
gates = []
def run(name, command):
    start = time.monotonic()
    result = subprocess.run(list(map(str, command)), cwd=ROOT, env=activate(), capture_output=True,
                            text=True, encoding='utf8', errors='replace', timeout=420)
    (EVIDENCE / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf8')
    gates.append(dict(name=name, exit=result.returncode, seconds=round(time.monotonic()-start, 3)))
    if result.returncode:
        raise RuntimeError(name + ': ' + (result.stdout+result.stderr)[-2500:])
    print(name + ' passed', flush=True)
for name, binary in [('items-assets', 'ItemAssetTests'), ('world-ui-native', 'HudTests'), ('items-inventory', 'InventoryTests')]:
    run(name, [BUILD / (binary + '.exe')])
fixture = json.loads((BUILD / 'item-fixture.json').read_text())
source, cache = Path(fixture['source']), Path(fixture['cache'])
common = [BUILD / 'DarkAngelEditor.exe', '--registry', fixture['registry'], '--cas', cache / 'cas',
          '--model', fixture['model'], '--sources', source, '--asset-cache', cache, '--backend', 'd3d12',
          '--hidden', '--no-audio', '--items-workspace', '--capture-workspace', '--frames', '6', '--width', '1480', '--height', '960']
for kind in ['consumable', 'mask']:
    command = common + ['--authoring-asset', fixture[kind], '--capture', EVIDENCE / ('items-'+kind+'-d3d12.png')]
    if kind == 'mask':
        definition = json.loads((source / ('items/'+fixture[kind]+'.daitem')).read_text())['properties']['mask']
        command += ['--mask-editor', definition]
    run('items-'+kind+'-d3d12', command)

# The verifier asserts real movement/combat, interaction, mismatch, travel and admission,
# and preserves per-process captures and receipts. Reuse it rather than duplicating its gameplay gates.
start = time.monotonic()
party = runpy.run_path(str(ROOT / 'scripts/verify_network_party.py'), run_name='__main__')
gates.append(dict(name='party-regression', exit=0, seconds=round(time.monotonic()-start, 3)))
def markers(value):
    result = []
    if isinstance(value, dict):
        result.extend(value.get('world_ui', []))
        for key, child in value.items():
            if key != 'world_ui': result.extend(markers(child))
    elif isinstance(value, list):
        for child in value: result.extend(markers(child))
    return result
for role, filename, expected in [('host','party-combat-d3d12.json','Player 2 (Client)'),
                                  ('guest','party-combat-d3d12-player-1.json','Player 1 (Host)')]:
    observed = markers(json.loads((EVIDENCE / filename).read_text()))
    if not any(marker['label'] == expected for marker in observed):
        raise RuntimeError(role + ' did not present the other player banner: ' + repr(observed))
    if not any(marker['health_bar'] and 0 < marker['percent'] < 100 for marker in observed):
        raise RuntimeError(role + ' did not present the damaged NPC health bar: ' + repr(observed))
    for marker in observed:
        if not all(math.isfinite(marker[field]) for field in ['x','y','width','percent']):
            raise RuntimeError('Non-finite world UI receipt')
    gates.append(dict(name='world-ui-'+role+'-d3d12', exit=0))
cold = ROOT / '.cache' / ('world-ui-cold-' + uuid.uuid4().hex)
log, hosted, (guest,), _ = party['host']('party-world-ui-cold-d3d12', 'reaction_registry', 'RoyalTraining.dascene', 26420, '2', 'await-tick:190;capture;await-tick:330;quit', 'walk-to:1.5,0;walk-to:1.5,3.5;attack;await-tick:220;quit', ['--asset-cache', cold])
party['together']('party-world-ui-cold-d3d12', hosted, guest)
if not any(marker['label'] == 'Player 1 (Host)' for marker in markers(guest)):
    raise RuntimeError('Cold-cache guest did not present the host banner')
gates.append(dict(name='party-world-ui-cold-d3d12', exit=0))
(EVIDENCE / 'm8-items-world-ui.json').write_text(json.dumps(dict(gates=gates,
    limits=['Projected screen-space UI, no depth occlusion.', 'D3D12 and same-machine GNS only.',
            'Item native authoring commands/form captures; physical pointer workflow not qualified.',
            'Inventory host/wire, runtime inventory menu and pickups remain Stage 2 work.']), indent=2)+'\n')
print('All item/world UI gates passed', flush=True)
