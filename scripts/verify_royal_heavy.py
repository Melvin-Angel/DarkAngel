"""Atomic native on-hit effects and authored Royal heavy/Burn composition."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None,timeout=5400):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
 return result.stdout
run('royal-heavy-product',['python','scripts/verify_ability_reservations.py',*(['--build'] if args.build else [])])
if args.build:
 env=activate()
 run('royal-heavy-build-editor',[CMAKE,'--build',ROOT/'build/m5-editor-relwithdebinfo','--target','RoyalHeavySceneTests','CombatEffectTests','--parallel','2'],env)
 run('royal-heavy-build-headless',[CMAKE,'--build',ROOT/'build/m1-relwithdebinfo','--target','CombatEffectTests','--parallel','2'],env)
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m1-relwithdebinfo','headless')]:run('royal-heavy-effects-'+label,[ROOT/'build'/profile/'CombatEffectTests.exe'])
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor')]:run('royal-heavy-scene-'+label,[ROOT/'build'/profile/'RoyalHeavySceneTests.exe'])
from prepare_royal_scene import SOURCE,CACHE,asset
kit=json.loads((SOURCE/'royal_district/combat/player.dakit').read_text())['asset'];collision=json.loads((SOURCE/'royal_district/collision/environment.dacollision').read_text())['asset']
base=[ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','--registry',CACHE/'registry.json','--cas',CACHE/'cas','--model',asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'),'--scene',SOURCE/'royal_district/RoyalCombat.dascene','--character-kit',kit,'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005']
for backend in ('d3d12','vulkan'):
 for frames,health in ((30,65),(210,55)):
  output=run('royal-heavy-game-'+backend+'-'+str(frames),base+['--backend',backend,'--frames',str(frames),'--hidden','--exercise-heavy','--capture-workspace','--capture',EVIDENCE/('royal-heavy-game-'+backend+'-'+str(frames)+'.png')])
  if f'Native heavy editor tick={frames} target_health={health} stamina=70 pending=0' not in output:raise RuntimeError('Heavy editor evidence marker missing')
sources=['CMakeLists.txt','engine/runtime/world_session.cpp','engine/runtime/include/darkangel/world_session.hpp','engine/runtime/include/darkangel/melee.hpp','engine/runtime/ability_state.hpp','engine/runtime/combat_kit.cpp','engine/runtime/include/darkangel/combat_kit.hpp','engine/assets/combat_kit_cooker.cpp','engine/assets/include/darkangel/combat_kit_assets.hpp','engine/runtime/character_scene.cpp','engine/runtime/include/darkangel/character_scene.hpp','apps/editor/main.cpp','apps/editor/character_preview.cpp','games/AshenRoots/royal_combat.cpp','games/AshenRoots/include/ashen_roots/royal_combat.hpp','tests/combat_effect_tests.cpp','tests/royal_heavy_scene_tests.cpp','tests/combat_kit_asset_tests.cpp','tests/character_combat_scene_tests.cpp','scripts/verify_royal_heavy.py',*['content/royal_district/combat/'+name for name in ('player.dakit','heavy.daaction','heavy.daability','burn.daeffect','state.datags')]]
binaries=['build/'+p+'/CombatEffectTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m1-relwithdebinfo')]+['build/'+p+'/RoyalHeavySceneTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo')]+['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Atomic typed on-hit effects and frozen Royal heavy ability with native deferred marker, authoritative Jolt hit/Burn, game-owned formulas, public status/cues and primary Game metrics',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['ability-reservations.json','effect-replication.json','royal-combat.json'],limitations=['Heavy uses the existing frozen attack clip with a fixed eight-tick windup; scalable charge/release attacks, combo and projectile rules are not implemented.','The read-only persistent Burn state appears in the existing ImGui Game metrics; visual/audio flame adapters, one-shot confirmation and RmlUi bars remain open.','Effects and damage remain native authority; owner prediction only covers the prepared action/resource/claim subset. Effect frames are separately timestamped.','M4/M5 remain In progress. Authored dodge, motor Slow/Stagger policy, equipment, reflected designers/typed graphs/sockets, generated tags/checked Luau, full fault/clock/streaming/performance gates remain open; M6 not started.'])
(EVIDENCE/'royal-heavy.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Royal heavy/Burn native, full-editor, Headless and D3D12/Vulkan checks passed')
