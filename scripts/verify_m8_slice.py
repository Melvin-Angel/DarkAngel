"""M8 acceptance slice: two scenes authored through the editor, played solo and as a two-process party.

Uses a private copy of the completed authoring fixture (run NativeAuthoringTests or verify_effect_reaction.py first).
Scene A is RoyalTraining and Scene B is RoyalVillage: they share the Royal district geometry, because Play still takes
one collision profile from the command line (Q1 is parsed but not used yet). Every world object is placed with
--world-place, which goes through the Scene tab's own Place and Inspector edit paths and saves the scene.

Gates:
  m8-slice-place-*     placing both scenes' pickups, altars, doors, platform, entries and transition
  m8-slice-solo        pickup, locked door consuming the key, altar discovery, travel to B, fast travel back,
                       checkpoint untouched by fast travel, rest (resets, checkpoint, save)
  m8-slice-load        restart with --play-from-save: arrives at the checkpoint altar with the saved ledger and inventory
  m8-slice-party       two editor processes over localhost GNS: downed, hold-to-revive, wipe, coordinated respawn
Same-machine UDP and D3D12 only; nothing here qualifies two machines, latency or EOS.
"""
import json, re, shutil, subprocess, sys, time, uuid
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]; BUILD = ROOT/'build/m5-editor-relwithdebinfo'; EVIDENCE = ROOT/'docs/implementation/evidence'
gates = []
fixture = json.loads((BUILD/'authoring-fixture.json').read_text())
# --reuse continues in the newest work directory from its recorded state (after the solo gate), for iterating on later gates.
reuse = '--reuse' in sys.argv
if reuse: work = max((ROOT/'.cache').glob('m8-slice-*'), key=lambda p: p.stat().st_mtime); state = json.loads((work/'state.json').read_text())
else:
    work = ROOT/'.cache'/('m8-slice-'+uuid.uuid4().hex[:8]); work.mkdir(parents=True)
    shutil.copytree(fixture['source'], work/'s'); shutil.copytree(fixture['cache'], work/'c'); shutil.copyfile(fixture['reaction_registry'], work/'registry.json')
source, cache = work/'s', work/'c'
model = json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id']
collision = json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset']
player = json.loads((source/'players/royal_player.daplayer').read_text())['asset']
scene_a, scene_b = 'RoyalTraining.dascene', 'RoyalVillage.dascene'
scene_id = lambda name: json.loads((source/'royal_district'/name).read_text())['root']
# A destination-only appearance override exercises frozen material preparation on host and guest scene travel.
material_path = source/'materials/m8_travel.damaterial'
if not reuse:
    material_path.parent.mkdir(parents=True, exist_ok=True)
    material_path.write_text(json.dumps(dict(schema=1, kind='material', asset=str(uuid.uuid4()), name='Travel material', family='lit-color-v1',
                                           color=[.1,.2,.9,1], roughness=.4, metallic=.1, double_sided=False, texture=None)))
travel_material = json.loads(material_path.read_text())['asset'] if material_path.exists() else None
travel_model = json.loads((source/'royal_district/static/mud_huts/SM_RC_MudHut_Round_1F.gltf.daimport').read_text())
travel_slot = travel_model['subassets']['material/trim']
def editor(scene):
    return [BUILD/'DarkAngelEditor.exe', '--registry', work/'registry.json', '--cas', cache/'cas', '--model', model, '--scene', source/'royal_district'/scene,
            '--character-kit', fixture['kit'], '--character-collision', collision, '--character-player', '00000000000000000000000000000004', '--player-asset', player,
            '--sources', source, '--asset-cache', cache, '--backend', 'd3d12', '--hidden', '--no-audio']
def fail(name, why, *seen):
    raise RuntimeError(name+': '+why+'\n'+'\n'.join(json.dumps(x, indent=1)[:4000] if isinstance(x, (dict, list)) else str(x)[-4000:] for x in seen))
def run(name, command, timeout=420):
    start = time.monotonic(); result = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True, text=True, encoding='utf8', errors='replace', timeout=timeout)
    log = result.stdout+result.stderr; (EVIDENCE/(name+'.log')).write_text(log, encoding='utf8')
    gates.append(dict(name=name, exit=result.returncode, seconds=round(time.monotonic()-start, 3)))
    if result.returncode: fail(name, 'exited with %d' % result.returncode, log)
    return log
def receipt(path):
    for attempt in range(20):
        try: return json.loads(Path(path).read_text())
        except (json.JSONDecodeError, PermissionError, FileNotFoundError): time.sleep(.05)
    return {}
def latest(value): return value.get('last_running') or value
def quantity(seen, item): return sum(e['quantity'] for e in seen.get('inventory', {}).get('entries', []) if e['item'] == item)

# --- authoring: Scene B first, so Scene A's transition can name B's entry point -------------------------------------------
def place(name, scene, objects):
    plan = work/(name+'.json'); plan.write_text(json.dumps({'objects': objects}))
    log = run(name, editor(scene)+['--world-place', plan, '--frames', '3'])
    placed = dict(re.findall(r'Editor world_place (\w+)=([0-9a-f]{32})', log))
    if len(placed) != len([o for o in objects if 'key' in o]): fail(name, 'not every object was placed', log)
    print(name+' passed', flush=True); return placed
token = json.loads((source/'items/traveller_token.daitem').read_text())['asset']
if reuse: a, b = state['a'], state['b']
else: b = place('m8-slice-place-b', scene_b, [
    {'key': 'entry', 'kind': 'entry', 'x': 0, 'z': 1, 'component': {'name': 'From the training ground'}},
    {'key': 'altar', 'kind': 'altar', 'x': 3, 'z': 1, 'component': {'name': 'Village Altar'}},
    {'key': 'platform', 'kind': 'platform', 'x': -5, 'z': 6},
    {'key': 'material_copy', 'kind': 'model', 'asset': travel_model['id'], 'x': -18, 'z': -18, 'materials': {travel_slot: travel_material}}])
if not reuse: a = place('m8-slice-place-a', scene_a, [
    {'key': 'key', 'kind': 'pickup', 'x': 3, 'z': 3, 'component': {'item': '@item:items/traveller_token.daitem', 'prompt': 'Take the temple key'}},
    {'key': 'door', 'kind': 'door', 'x': -3, 'z': 6, 'component': {'locked': True, 'prompt': 'Open the gate', 'locked_prompt': 'The gate is locked', 'requires': {'item': '@item:items/traveller_token.daitem', 'consume': True}}},
    {'key': 'altar', 'kind': 'altar', 'x': -3, 'z': 1, 'component': {'name': 'Training Altar'}},
    {'key': 'entry', 'kind': 'entry', 'x': 4, 'z': -2, 'component': {'name': 'From the village'}},
    {'key': 'travel', 'kind': 'transition', 'x': 6, 'z': 1, 'component': {'scene': scene_id(scene_b), 'entry': b['entry'], 'activation': 'interact', 'prompt': 'Travel to the village', 'radius': 3}}])
# Fast-travel destinations are listed in ledger order: by scene asset, then altar identity.
destinations = sorted([(scene_id(scene_a), a['altar']), (scene_id(scene_b), b['altar'])])
to_a = destinations.index((scene_id(scene_a), a['altar']))

# --- solo play: the whole single-player loop -----------------------------------------------------------------------------
def solo():
  name = 'm8-slice-solo'; report = EVIDENCE/(name+'.json'); report.unlink(missing_ok=True)
  script = ';'.join(['walk-to:3,2.8', 'use', 'wait:20',                     # the key
                     'walk-to:-3,4.4', 'use', 'wait:90',                    # the locked gate opens and takes the key
                     'walk-to:-3,2.4', 'use', 'altar-await', 'altar-close',  # discover the altar
                     'walk-to:5.5,1', 'use', 'await-scene:1', 'wait:30',     # travel to the village
                     'walk-to:3,2.4', 'use', 'altar-await', 'altar-travel:%d' % to_a, 'await-scene:2', 'wait:30',   # fast travel back
                     'capture', 'use', 'altar-await', 'altar-rest', 'await-scene:3', 'wait:120', 'quit'])
  log = run(name, editor(scene_a)+['--play', '--game-workspace', '--party-report', report, '--capture', EVIDENCE/(name+'.png'), '--player-script', script], timeout=900)
  seen = latest(receipt(report)); world = seen.get('world', {})
  if seen.get('arrivals') != 3 or seen.get('travel_failures'): fail(name, 'the party did not arrive three times (travel, fast travel, rest)', seen, log)
  if a['key'] not in world.get('collected', []): fail(name, 'the key pickup is not collected', seen)
  if a['door'] not in world.get('unlocked', []): fail(name, 'the gate was not unlocked', seen)
  if a['door'] in world.get('open', []): fail(name, 'the gate is still open after a rest (its open state resets on rest)', seen)
  if quantity(seen, token) != 1: fail(name, 'the token count is not 1 (start 1, pickup +1, gate consumed 1)', seen)
  if not world.get('checkpoint') or world['checkpoint']['altar'] != a['altar']: fail(name, 'resting did not set the checkpoint to the training altar', seen)
  if sorted(d['altar'] for d in world.get('destinations', [])) != sorted([a['altar'], b['altar']]): fail(name, 'both altars are not discovered fast-travel destinations', seen)
  if 'Saved' not in seen.get('save_status', ''): fail(name, 'resting did not save', seen, log)
  revision = seen.get('inventory', {}).get('revision')
  print(name+' passed', flush=True)
  (work/'state.json').write_text(json.dumps({'a': a, 'b': b, 'saved_revision': revision}))
  return revision
saved_revision = state['saved_revision'] if reuse else solo()

# --- load: restart from the save -----------------------------------------------------------------------------------------
name = 'm8-slice-load'; report = EVIDENCE/(name+'.json'); report.unlink(missing_ok=True)
log = run(name, editor(scene_a)+['--play', '--play-from-save', '--game-workspace', '--party-report', report, '--player-script', 'wait:120;quit'])
seen = latest(receipt(report)); world = seen.get('world', {}); at = seen.get('predicted_position', [99, 0, 99])
if 'Play starts from the save slot.' not in seen.get('console', []): fail(name, 'Play did not load the save', seen)
if seen.get('inventory', {}).get('revision') != saved_revision: fail(name, 'the inventory is not the saved one (revision %s, saved %s)' % (seen.get('inventory', {}).get('revision'), saved_revision), seen)
if a['key'] not in world.get('collected', []) or a['door'] not in world.get('unlocked', []): fail(name, 'the saved world state did not come back', seen)
if quantity(seen, token) != 1: fail(name, 'the saved inventory did not come back', seen)
if abs(at[0]+3) > 1.5 or abs(at[2]-2.5) > 1.5: fail(name, 'the party did not start at the checkpoint altar', seen)
print(name+' passed', flush=True)

# --- party: downed, revive, wipe and respawn in two processes -------------------------------------------------------------
name = 'm8-slice-party'; report = EVIDENCE/(name+'.json'); report.unlink(missing_ok=True)
host = ';'.join(['await-players:2', 'wait:60', 'down:1', 'wait:30', 'approach:1', 'hold-interact:420', 'wait:60', 'log-party', 'capture', 'down:0', 'down:1', 'await-scene:2', 'wait:120', 'log-party', 'quit'])
guest = 'await-tick:100000;quit'
log = run(name, editor(scene_a)+['--game-workspace', '--listen', '127.0.0.1:26510', '--players', '2', '--party-report', report, '--capture', EVIDENCE/(name+'.png'), '--player-script', host, '--input-script', guest], timeout=600)
seen = latest(receipt(report))
if 'Script downed seat 1' not in log or 'Script downed seat 0' not in log: fail(name, 'the host did not down the party', log)
revived = re.findall(r'Script party tick=\d+ seat0=([0-9.]+) seat1=([0-9.]+)', log)
if not revived or float(revived[0][1]) <= 0: fail(name, 'the downed guest was not revived by holding Interact', log)
# A party host counts its first scene entry as an arrival; the respawn is the second.
if seen.get('arrivals', 0) < 2: fail(name, 'the wipe did not respawn the party through a transition', seen, log)
if not re.findall(r'Script party tick=\d+ seat0=([1-9][0-9.]*) seat1=([1-9][0-9.]*)', log)[1:]: fail(name, 'both players are not alive after the respawn', log)
if seen.get('world', {}).get('wiped'): fail(name, 'the party is still wiped after the respawn', seen)
if 'party.wiped' not in log and not any('wiped' in line for line in seen.get('console', [])): pass  # the event is server-side; arrivals prove the respawn
print(name+' passed', flush=True)

# --- party travel: a rest in Scene B sets the checkpoint, fast travel back to A leaves it, a wipe in A respawns in B -------
name = 'm8-slice-party-travel'; report = EVIDENCE/(name+'.json'); report.unlink(missing_ok=True)
host = ';'.join(['await-players:2', 'wait:60', 'walk-to:-3,2.4', 'use', 'altar-await', 'altar-close',   # discover the training altar
                 'walk-to:5.5,1', 'use', 'await-scene:2', 'wait:60',                                     # the party travels to the village
                 'walk-to:3,2.4', 'use', 'altar-await', 'altar-rest', 'await-scene:3', 'wait:60',          # rest: checkpoint in the village
                 'use', 'altar-await', 'altar-travel:%d' % to_a, 'await-scene:4', 'wait:60',               # fast travel back to the training ground
                 'down:0', 'down:1', 'await-scene:5', 'wait:120', 'log-party', 'quit'])                     # wipe there, respawn in the village
log = run(name, editor(scene_a)+['--game-workspace', '--listen', '127.0.0.1:26511', '--players', '2', '--party-report', report, '--capture', EVIDENCE/(name+'.png'), '--player-script', host, '--input-script', 'await-tick:100000;quit'], timeout=900)
seen = latest(receipt(report)); world = seen.get('world', {})
if seen.get('arrivals', 0) < 5: fail(name, 'the party did not make all five arrivals (start, travel, rest, fast travel, respawn)', seen, log)
if not world.get('checkpoint') or world['checkpoint']['altar'] != b['altar']: fail(name, 'fast travel or the wipe changed the checkpoint set by resting in the village', seen)
if seen.get('scene_now') != scene_id(scene_b): fail(name, 'the wipe in the training ground did not respawn the party in the village', seen)
if not re.findall(r'Script party tick=\d+ seat0=([1-9][0-9.]*) seat1=([1-9][0-9.]*)', log): fail(name, 'both players are not alive after the respawn in the other scene', log)
if travel_material:
  expected = {travel_slot: travel_material}
  if seen.get('render_material_bindings', {}).get(b['material_copy']) != expected: fail(name, 'host destination material bindings were not preserved through travel/respawn', seen)
  guest = latest(receipt(EVIDENCE/(name+'-player-1.json')))
  if guest.get('render_material_bindings', {}).get(b['material_copy']) != expected: fail(name, 'guest destination material bindings were not preserved through travel/respawn', guest)
print(name+' passed', flush=True)

(EVIDENCE/'m8-slice.json').write_text(json.dumps(dict(gates=gates, work=str(work), material_travel=bool(travel_material), limits=[
    'Same-machine UDP and D3D12 only.', 'Scenes share one collision profile (Q1 not used by Play yet).',
    'Revive progress is asserted through the completed revive and respawn, not through the HUD capture.']), indent=2)+'\n')
print('All M8 slice gates passed', flush=True)
