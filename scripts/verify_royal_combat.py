"""Playable Royal native combat composition checks; M4/M5 remain incomplete."""
import argparse, hashlib, json, subprocess, time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args()
gates=[]
def run(name,command,env=None):
    start=time.monotonic()
    result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=900)
    (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
    gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
if args.build:
    env=activate()
    for profile,targets in (
        ('m5-relwithdebinfo',['AssetTool','CharacterCombatSceneTests','CharacterSceneTests','ActionClipBindingTests','ActionTimelineTests','AbilityAssetTests','CombatKitAssetTests','GraphAssetTests','AbilityTests','AbilityCorrectionTests','AbilityIntentTests','AbilityPredictionTests','AbilityObserverTests','AbilityMotorPredictionTests','MeleeTests','MotorActorHistoryTests','AbilityIntentGnsTests']),
        ('m5-editor-relwithdebinfo',['DarkAngelEditor','CharacterCombatSceneTests','AbilityPredictionTests','AbilityObserverTests']),
        ('m2-relwithdebinfo',['AbilityAssetTests','AssetTool']),
        ('m1-relwithdebinfo',['DarkAngelHeadless','AbilityIntentTests','AbilityCorrectionTests','AbilityTests','AbilityPredictionTests','AbilityObserverTests'])):
        run('royal-combat-build-'+profile,[CMAKE,'--build','build/'+profile,'--target',*targets,'--parallel','2'],env)
run('royal-combat-native',[CMAKE.parent/'ctest.exe','--test-dir','build/m5-relwithdebinfo','--output-on-failure','-R',r'^(M5.character_combat_scene|M5.character_scene|M5.action_clip_binding|M5.action_timeline|M5.ability_assets|M5.combat_kit_assets|M5.graph_assets|M5.ability_commit|M5.ability_correction|M5.ability_intent|M5.ability_prediction|M5.ability_observer|M5.ability_motor_prediction|M5.authoritative_melee|M4.actor_history)$'])
run('royal-combat-editor',[ROOT/'build/m5-editor-relwithdebinfo/CharacterCombatSceneTests.exe'])
run('royal-combat-no-animation',[ROOT/'build/m2-relwithdebinfo/AbilityAssetTests.exe'])
run('royal-combat-headless',[CMAKE.parent/'ctest.exe','--test-dir','build/m1-relwithdebinfo','--output-on-failure','-R',r'^(M5.ability_intent|M5.ability_correction|M5.ability_commit|M5.ability_prediction|M5.ability_observer|HeadlessSmoke)$'])

run('royal-combat-prepare',['python','scripts/prepare_royal_scene.py'])
from prepare_royal_scene import SOURCE,CACHE,asset
kit=json.loads((SOURCE/'royal_district/combat/player.dakit').read_text())['asset']
base=[ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','--registry',CACHE/'registry.json','--cas',CACHE/'cas','--model',asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'),'--scene',SOURCE/'royal_district/RoyalCombat.dascene','--character-kit',kit,'--character-player','00000000000000000000000000000004','--combat-target','00000000000000000000000000000005']
for backend in ('d3d12','vulkan'):
    run('royal-combat-game-'+backend,base+['--backend',backend,'--frames','120','--hidden','--exercise-combat','--capture-workspace','--capture',EVIDENCE/('royal-combat-game-'+backend+'.png')])
    run('royal-combat-pose-'+backend,base+['--backend',backend,'--frames','30','--hidden','--exercise-combat','--capture',EVIDENCE/('royal-combat-pose-'+backend+'.png')])
    log=(EVIDENCE/('royal-combat-game-'+backend+'.log')).read_text()
    if 'Native combat editor tick=120 target_health=50 stamina=80 pending=0' not in log:raise RuntimeError('Native combat editor marker missing')
# Preserve the original locomotion scene and Game movement path as well.
legacy=[ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','--registry',CACHE/'registry.json','--cas',CACHE/'cas','--model',asset('royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf'),'--scene',SOURCE/'royal_district/RoyalVillage.dascene','--character-graph','3177471d-c721-425f-958b-6b03b78c8bcd']
run('royal-combat-locomotion',legacy+['--backend','d3d12','--frames','120','--hidden','--exercise-character'])
inputs=['CMakeLists.txt','engine/runtime/ability_prediction.cpp','engine/runtime/include/darkangel/ability_prediction.hpp','engine/runtime/ability.cpp','engine/runtime/ability_wire.cpp','engine/runtime/character_motor.cpp','engine/runtime/character_scene.cpp','engine/runtime/include/darkangel/character_scene.hpp','apps/editor/main.cpp','apps/editor/character_preview.cpp','apps/editor/character_preview.hpp','tests/character_combat_scene_tests.cpp','scripts/prepare_royal_scene.py','scripts/open_royal_scene.ps1','scripts/verify_royal_combat.py','content/royal_district/RoyalCombat.dascene',*['content/royal_district/combat/'+name for name in ('base.daattributes','light.daaction','light.daability','player.dakit')]]
binaries=['build/m5-editor-relwithdebinfo/DarkAngelEditor.exe','build/m5-relwithdebinfo/CharacterCombatSceneTests.exe','build/m2-relwithdebinfo/AbilityAssetTests.exe','build/m1-relwithdebinfo/DarkAngelHeadless.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Primary Royal Game workflow with frozen kit/action/clip, serialized protocol3 input/correction/ACK, native owner pending cost/action/root prediction, authoritative motor-relative melee target damage and Health/Stamina metrics',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['Pending owner cost/action/root prediction and dependent operation invalidation pass; real authored combo transitions, tags/reservations and predicted cosmetic cues remain open.','Full character/kit/ability and typed graph/layer/mask/Action Composer authoring and animated socket sweeps remain open.','Public action/resources, bounded interpolation and active-action late join pass focused native tests; public effects/tags/cues and full fault/timing/performance qualification remain open.','Training target is a stationary native actor with an application-owned power evaluator; NPC death animation/despawn and final formulas remain game-owned work.','M4/M5 remain In progress; live EOS remains blocked.'])
(EVIDENCE/'royal-combat.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Royal combat native/full-editor/Game D3D12/Vulkan and ordinary Headless checks passed')
