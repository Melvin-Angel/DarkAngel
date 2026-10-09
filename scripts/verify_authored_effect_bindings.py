"""Focused checks for frozen kit-authored ability/hit/effect composition."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None,timeout=1800):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-4000:])
 return result.stdout
if args.build:
 env=activate();run('authored-effect-bindings-build-native',[CMAKE,'--build',ROOT/'build/m5-relwithdebinfo','--target','RoyalHeavySceneTests','CharacterCombatSceneTests','CombatKitAssetTests','CombatEffectTests','AssetTool','--parallel','2'],env)
 run('authored-effect-bindings-build-editor',[CMAKE,'--build',ROOT/'build/m5-editor-relwithdebinfo','--target','DarkAngelEditor','--parallel','2'],env)
run('authored-effect-bindings-native',[CMAKE.parent/'ctest.exe','--test-dir',ROOT/'build/m5-relwithdebinfo','--output-on-failure','-R',r'^M5\.(royal_heavy_scene|character_combat_scene|combat_kit_assets|combat_effects)$'])
run('authored-effect-bindings-prepare',['python','scripts/prepare_royal_scene.py'])
from prepare_royal_scene import SOURCE,CACHE,asset
kit=json.loads((SOURCE/'royal_district/combat/player.dakit').read_text())['asset'];collision=json.loads((SOURCE/'royal_district/collision/environment.dacollision').read_text())['asset']
base=[ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','--registry',CACHE/'registry.json','--cas',CACHE/'cas','--model',asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'),'--scene',SOURCE/'royal_district/RoyalCombat.dascene','--character-kit',kit,'--character-collision',collision,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005','--backend','d3d12','--hidden']
for frames,health in ((30,65),(210,55)):
 output=run('authored-effect-bindings-game-'+str(frames),base+['--frames',str(frames),'--exercise-heavy'])
 if f'Native heavy editor tick={frames} target_health={health} stamina=70 pending=0' not in output:raise RuntimeError('Data-bound heavy marker missing')
run('authored-effect-bindings-light',base+['--frames','120','--exercise-combat'])
sources=['engine/runtime/include/darkangel/combat_kit.hpp','engine/runtime/combat_kit.cpp','engine/runtime/include/darkangel/melee.hpp','engine/runtime/world_session.cpp','engine/assets/combat_kit_cooker.cpp','games/AshenRoots/royal_combat.cpp','content/royal_district/combat/player.dakit','tests/combat_kit_asset_tests.cpp','tests/royal_heavy_scene_tests.cpp','tests/character_combat_scene_tests.cpp','scripts/verify_authored_effect_bindings.py'];binaries=['build/m5-relwithdebinfo/'+p+'.exe' for p in ('RoyalHeavySceneTests','CharacterCombatSceneTests','CombatKitAssetTests','CombatEffectTests')]+['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Kit-authored ability/hit/effect/magnitude binding, strict frozen references, native authoritative identity and game-owned generic composition',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['Focused affected native and primary D3D12 Game checks only; the complete historical 74-test/multi-profile/Vulkan/GNS chain was not rerun for this increment. Prior complete evidence belongs to checkpoint74717c7.','Bindings currently belong to the CombatKit composition and melee hit blocks, target surviving hit actors, and use authored fixed magnitudes. Self/on-commit/area/projectile triggers and richer capture policies remain open.','Royal registers game execution evaluator2 as Health damage; modifier-only effects need no evaluator. New custom execution formulas still require a registered game evaluator.','Native Ability/Effect/Composer designers and validated Save/cook/fresh-Play UI are the next priorities. M4/M5 remain In progress; M6 has not started.'])
(EVIDENCE/'authored-effect-bindings.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Authored effect bindings focused native and primary Game checks passed')
