"""Authored effect reaction: native binding, frozen closure, checked public flinch and fresh Play."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';EVIDENCE=ROOT/'docs/implementation/evidence'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None):
    start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=3600)
    (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-4000:])
    print(name+' passed',flush=True);return result.stdout+result.stderr
if args.build:
    run('effect-reaction-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','DarkAngelEditor','NativeAuthoringTests','ReactionPresentationTests','ObservedCharacterPoseTests','EffectAssetTests','CombatKitAssetTests','EffectReplicationTests','CombatEffectTests','CameraTests','CharacterCombatSceneTests','RoyalHeavySceneTests','HudTests','MotorTests','NavigationTests','BehaviorTreeTests','--parallel','2'],activate())
# NativeAuthoringTests writes the disposable copied fixture the later gates read.
for name,binary in [('effect-reaction-authoring','NativeAuthoringTests'),('effect-reaction-presentation','ReactionPresentationTests'),('effect-reaction-public-pose','ObservedCharacterPoseTests'),('effect-reaction-effect-assets','EffectAssetTests'),('effect-reaction-kit-assets','CombatKitAssetTests'),('effect-reaction-replication','EffectReplicationTests'),('effect-reaction-combat-effects','CombatEffectTests'),('camera-rig','CameraTests'),('scripted-attacker','CharacterCombatSceneTests'),('royal-heavy-scene','RoyalHeavySceneTests'),('runtime-hud','HudTests'),('motor-warp','MotorTests'),('navigation','NavigationTests'),('behavior-tree','BehaviorTreeTests')]:run(name,[BUILD/(binary+'.exe')])
f=json.loads((BUILD/'authoring-fixture.json').read_text());source=Path(f['source']);cache=Path(f['cache']);model=json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id'];collision=json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset']
common=[BUILD/'DarkAngelEditor.exe','--registry',f['reaction_registry'],'--cas',cache/'cas','--model',model,'--scene',source/'royal_district/RoyalTraining.dascene','--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--sources',source,'--asset-cache',cache,'--backend','d3d12','--hidden','--capture-workspace']
run('effect-reaction-form',common+['--ability-workspace','--authoring-asset',f['reaction_effect'],'--height','1500','--frames','5','--capture',EVIDENCE/'effect-reaction-form.png'])
run('effect-reaction-d3d12',common+['--exercise-reaction','--frames','30','--capture',EVIDENCE/'effect-reaction-d3d12.png'])
death=[f['death_registry'] if x==f['reaction_registry'] else x for x in common]
run('kit-death-d3d12',death+['--exercise-death','--frames','210','--capture',EVIDENCE/'kit-death-d3d12.png'])
# Scenes for the NPC gates. The training scene (common) spawns one passive Training Dummy. The duel scene is the same
# layout with the owned Royal Guard at that spawn point; the owned combat scene spawns two Royal Guards further away.
duel_source=json.loads((source/'royal_district/RoyalTraining.dascene').read_text());duel_root=duel_source['root']
for entity in duel_source['sources'][duel_root]['entities'].values():
    if 'spawn' in entity:entity['spawn']['npc']=f['npc']
(source/'royal_district/RoyalDuel.dascene').write_text(json.dumps(duel_source,indent=2)+chr(10))
def scene(arguments,name):
    out=[x for x in arguments];out[out.index('--scene')+1]=source/('royal_district/'+name);return out
# A spawned Royal Guard attacks through its behaviour tree: the player is hit, flinches and dies on its own confirmed pose.
def marker(log,key,prefix='Editor attacked '):
    line=[l for l in log.splitlines() if l.startswith(prefix)][-1];value=dict(part.split('=') for part in line.split()[2:])[key];return value if key.startswith('hud') or key in('behavior','decision') else float(value)
log=run('attacked-flinch-d3d12',scene(death,'RoyalDuel.dascene')+['--exercise-attacked','--frames','40','--capture',EVIDENCE/'attacked-flinch-d3d12.png'])
if not(marker(log,'reaction_tick')>0 and marker(log,'player_health')==75 and marker(log,'death_tick')<0 and marker(log,'hud_health')=='75/100'):raise RuntimeError('Editor Play did not show the player flinch: '+log[-400:])
log=run('attacked-death-d3d12',scene(death,'RoyalDuel.dascene')+['--exercise-attacked','--frames','420','--capture',EVIDENCE/'attacked-death-d3d12.png'])
if not(marker(log,'death_tick')>0 and marker(log,'player_health')==0 and marker(log,'hud_health')=='0/100'):raise RuntimeError('Editor Play did not show the player death: '+log[-400:])
# The Royal kit's default Light binding: the guard's Light hit staggers the player with the owned Flinch.
log=run('attacked-light-d3d12',scene(common,'RoyalDuel.dascene')+['--exercise-attacked','--frames','40','--capture',EVIDENCE/'attacked-light-d3d12.png'])
if not(marker(log,'reaction_tick')>0 and marker(log,'player_health')==75 and marker(log,'hud_health')=='75/100'):raise RuntimeError('Editor Play did not show the default Light flinch: '+log[-400:])
dodge=[f['dodge_registry'] if x==f['reaction_registry'] else x for x in common]
run('dodge-d3d12',dodge+['--exercise-dodge','--frames','30','--capture',EVIDENCE/'dodge-d3d12.png'])
# Crosshair aim: hold the ranged modifier (aim camera, crosshair) and release a Bow Shot at the target.
log=run('ranged-d3d12',common+['--exercise-ranged','--frames','60','--capture',EVIDENCE/'ranged-d3d12.png'])
if 'camera=Aim' not in log or 'pending=0 hud_crosshair=1' not in log:raise RuntimeError('Editor Play did not land the aimed ranged ability: '+log[-400:])
# Travelling projectile: the same shot at a dummy 14 m away, captured while the arrow is in the air.
range_source=json.loads((source/'royal_district/RoyalTraining.dascene').read_text());range_root=range_source['root']
for entity in range_source['sources'][range_root]['entities'].values():
    if 'spawn' in entity:entity['types']['1']['fields']['4']=17.0
(source/'royal_district/RoyalRange.dascene').write_text(json.dumps(range_source,indent=2)+chr(10))
log=run('projectile-flight-d3d12',[source/'royal_district/RoyalRange.dascene' if str(x).endswith('RoyalTraining.dascene') else x for x in common]+['--exercise-ranged','--frames','48','--capture',EVIDENCE/'projectile-flight-d3d12.png'])
if 'projectile=1 in_flight=1' not in log or 'target_health=100' not in log:raise RuntimeError('Editor Play did not show a projectile in flight: '+log[-400:])
# Melee soft lock: strafe aside, push toward the target and press Light; the swing turns and closes on it.
log=run('soft-lock-d3d12',common+['--exercise-soft-lock','--frames','30','--capture',EVIDENCE/'soft-lock-d3d12.png'])
if 'predicted_error=0 pending=0 hud_lock=1' not in log or 'Editor soft lock tick=30' not in log:raise RuntimeError('Editor Play did not soft-lock the Light attack: '+log[-400:])
worn=[x for x in common];worn[worn.index('--registry')+1]=f['mask_registry'];k=worn.index('--character-kit');worn[k:k+2]=['--character-clips','player:'+f['mask_player'],'--player-asset',f['mask_player']]
run('mask-worn-d3d12',worn+['--exercise-upper-body','--frames','30','--capture',EVIDENCE/'mask-worn-d3d12.png'])
run('mask-placement-d3d12',worn+['--exercise-upper-body','--frames','30','--mask-tune','0.04,0.11,0,0,-90,0,0.01','--capture',EVIDENCE/'mask-placement-d3d12.png'])
log=run('mask-switch-d3d12',worn+['--exercise-mask-switch','--frames','30','--capture',EVIDENCE/'mask-switch-d3d12.png'])
if 'Editor mask switch tick=30 loadout=2 mask=test_air worn=0 pending=0' not in log:raise RuntimeError('Editor Play did not switch masks: '+log[-400:])
run('camera-free-look-d3d12',worn+['--exercise-camera-look','--frames','30','--capture',EVIDENCE/'camera-free-look-d3d12.png'])
run('camera-aim-d3d12',worn+['--exercise-aim','--frames','30','--capture',EVIDENCE/'camera-aim-d3d12.png'])
form=[x for x in worn];run('camera-preview-d3d12',form+['--player-workspace','--authoring-asset',f['mask_player'],'--exercise-camera-preview','--height','1500','--frames','6','--capture',EVIDENCE/'camera-preview-d3d12.png'])
# M6 track: the owned Royal Guard spawned from a scene spawn point with its own attributes, running its authored behaviour tree.
guarded=[f['npc_registry'] if x==f['reaction_registry'] else x for x in common]
log=run('npc-target-d3d12',scene(guarded,'RoyalDuel.dascene')+['--exercise-attacked','--frames','40','--capture',EVIDENCE/'npc-target-d3d12.png'])
if not(marker(log,'player_health')==75 and marker(log,'actors')==1 and marker(log,'hud_target')=='60/60' and marker(log,'hud_target_name')=='Royal_Guard' and marker(log,'nav_polygons')>0 and marker(log,'behavior')=='Guard_Melee' and ' decision=' in log):raise RuntimeError('Editor Play did not spawn the Royal Guard from its spawn point: '+log[-400:])
# Multi-entity combat: two Royal Guards facing the player from 6 and 7 m (a scene derived here, not the user's combat scene) see it, walk the navigation mesh to it and attack.
guards_source=json.loads((source/'royal_district/RoyalTraining.dascene').read_text());guards_entities=guards_source['sources'][guards_source['root']]['entities']
for key in [key for key,entity in guards_entities.items() if 'spawn' in entity]:
    first=guards_entities[key];first['name']='Royal Guard spawn';first['spawn']['npc']=f['npc'];first['types']['1']['fields'].update({'2':-1.5,'4':9})
    second=json.loads(json.dumps(first));second['name']='Royal Guard spawn 2';second['types']['1']['fields'].update({'2':2,'4':10});guards_entities['00000000000000000000000000000006']=second
(source/'royal_district/RoyalGuards.dascene').write_text(json.dumps(guards_source,indent=2)+chr(10))
log=run('npc-spawn-points-d3d12',scene(guarded,'RoyalGuards.dascene')+['--exercise-attacked','--frames','260','--capture',EVIDENCE/'npc-spawn-points-d3d12.png'])
if not(marker(log,'player_health')<100 and marker(log,'actors')>=2 and marker(log,'nav_polygons')>0 and marker(log,'behavior')=='Guard_Melee'):raise RuntimeError('Editor Play did not run two spawned Royal Guards: '+log[-400:])
# NPC ranged attack: a Royal Archer spawned 12 m away looses arrows at the player through its behaviour tree.
archer_source=json.loads((source/'royal_district/RoyalTraining.dascene').read_text())
for entity in archer_source['sources'][archer_source['root']]['entities'].values():
    if 'spawn' in entity:entity['spawn']['npc']=f['archer'];entity['types']['1']['fields']['4']=15.0
(source/'royal_district/RoyalArcher.dascene').write_text(json.dumps(archer_source,indent=2)+chr(10))
log=run('npc-archer-d3d12',scene(guarded,'RoyalArcher.dascene')+['--exercise-attacked','--frames','110','--capture',EVIDENCE/'npc-archer-d3d12.png'])
if not(marker(log,'player_health')==80 and marker(log,'actors')==1 and marker(log,'behavior')=='Archer_Ranged' and marker(log,'hud_target_name')=='Royal_Archer'):raise RuntimeError('Editor Play did not run the Royal Archer: '+log[-400:])
# The same scene in the Scene tab: both spawn points preview their NPC's Character.
log=run('spawn-points-scene-d3d12',scene(guarded,'RoyalGuards.dascene')+['--select-spawn','--frames','5','--capture',EVIDENCE/'spawn-points-scene-d3d12.png'])
if not marker(log,'drawn','Editor senses_scene ')==1:raise RuntimeError('The Scene tab did not draw the selected spawn point senses: '+log[-400:])
# NPC senses: a Royal Guard 6 m to the player's side, facing the way the player will walk, has the player outside its cone and
# its distance radius. It stays unaware for a second, notices the player once it has walked into the cone, then comes and attacks.
senses_source=json.loads((source/'royal_district/RoyalTraining.dascene').read_text())
for entity in senses_source['sources'][senses_source['root']]['entities'].values():
    if 'spawn' in entity:entity['spawn']['npc']=f['npc'];entity['types']['1']['fields'].update({'1':0.0,'2':6.0,'4':3.0})
(source/'royal_district/RoyalSenses.dascene').write_text(json.dumps(senses_source,indent=2)+chr(10))
log=run('npc-senses-d3d12',scene(guarded,'RoyalSenses.dascene')+['--exercise-senses','--frames','330','--capture',EVIDENCE/'npc-senses-d3d12.png'])
if not(marker(log,'noticed_tick','Editor senses ')>60 and marker(log,'noticed_z','Editor senses ')>4.5 and marker(log,'sensors','Editor senses ')==2 and marker(log,'senses_drawn','Editor senses ')==1):raise RuntimeError('Editor Play did not show a guard noticing the player in its cone: '+log[-400:])
# Tools > Navigation: bake the Royal scene collision on demand; Play reuses the bake.
log=run('navigation-tool-d3d12',common+['--exercise-navigation-bake','--frames','5','--capture',EVIDENCE/'navigation-tool-d3d12.png'])
if 'play_reuses_bake=1 on_demand_for_other_collision=1' not in log:raise RuntimeError('Navigation tool did not bake: '+log[-400:])
# Level design: the Place panel's catalogue and its place, delete, undo and redo commands on the training scene.
log=run('place-panel-d3d12',common+['--exercise-place','--frames','5','--capture',EVIDENCE/'place-panel-d3d12.png'])
if 'Place panel items=' not in log or 'delete_undo_redo=1' not in log or 'on_collision=1' not in log:raise RuntimeError('Place panel did not place and delete scene objects: '+log[-400:])
# Encounters: the player walks into a trigger volume, its world event starts a wave encounter, and Royal Guards enter the
# running session at random points inside the encounter's volume. The same scene in the Scene tab draws the volumes and
# places an encounter, a second volume and a trigger volume through the Place panel's command.
encounter_source=json.loads((source/'royal_district/RoyalTraining.dascene').read_text());encounter_entities=encounter_source['sources'][encounter_source['root']]['entities']
for key in [key for key,entity in encounter_entities.items() if 'spawn' in entity]:
    template=encounter_entities.pop(key)
def gameplay_entity(name,x,z,**component):
    entity=dict(name=name,target=None,types=json.loads(json.dumps(template['types'])),**component);entity['types']['1']['fields'].update({'1':0.0,'2':x,'4':z});return entity
encounter_entities['00000000000000000000000000000007']=gameplay_entity('Gate trigger',0.0,6.0,trigger={'event':'gate.entered','half':[3.0,2.0,1.0],'once':True})
encounter_entities['00000000000000000000000000000008']=gameplay_entity('Gate fight',0.0,14.0,encounter={'trigger':{'event':'gate.entered'},'mode':'waves','waves':[{'npc':f['npc'],'count':2},{'npc':f['npc'],'count':1}],'placement':'random','emits':{'started':'gate.fight_started','completed':'gate.cleared'},'tag':'gate.fight_active'})
encounter_entities['00000000000000000000000000000009']=gameplay_entity('Gate fight volume',0.0,14.0,volume={'encounter':'00000000000000000000000000000008','half':[4.0,2.0,3.0]})
(source/'royal_district/RoyalEncounter.dascene').write_text(json.dumps(encounter_source,indent=2)+chr(10))
log=run('encounter-scene-d3d12',scene(guarded,'RoyalEncounter.dascene')+['--select-encounter','--frames','6','--capture',EVIDENCE/'encounter-scene-d3d12.png'])
if 'Editor encounter_place encounters=2 volumes=3 triggers=2 undo_redo=1' not in log or marker(log,'volumes_drawn','Editor encounter_scene ')<4:raise RuntimeError('The Scene tab did not draw or place encounter volumes: '+log[-400:])
log=run('encounter-d3d12',scene(guarded,'RoyalEncounter.dascene')+['--exercise-encounter','--frames','200','--capture',EVIDENCE/'encounter-d3d12.png'])
if not('Editor encounter fired=1 spawned=2 ' in log and 'events=play.started,gate.entered,gate.fight_started' in log):raise RuntimeError('Editor Play did not start the encounter from its trigger volume: '+log[-400:])
# The Place panel and outliner driven with real mouse and key events: drag into the viewport, double-click, Delete key, right-click Delete.
log=run('place-mouse-d3d12',common+['--exercise-place-mouse','--frames','44','--capture',EVIDENCE/'place-mouse-d3d12.png'])
if 'Editor mouse drag=1' not in log or 'double_click=1 delete_key=1 context_delete=1' not in log or 'viewport_select=1' not in log:raise RuntimeError('The Place panel mouse paths did not work: '+log[-400:])
# Mask Editor: one window for a mask definition, opened over the Player tab and over the NPC tab.
masks=sorted(source.glob('masks/*.dafacemask'));mask_id=json.loads(masks[0].read_text())['asset']
masked_npc=next((json.loads(p.read_text())['asset'] for p in sorted(source.glob('npcs/*.danpc')) if any(json.loads(p.read_text()).get('loadout',{}).get('masks',[]))),f['npc'])
log=run('mask-editor-player-d3d12',form+['--player-workspace','--authoring-asset',f['mask_player'],'--mask-editor',mask_id,'--height','1500','--frames','5','--capture',EVIDENCE/'mask-editor-player-d3d12.png'])
if 'Mask Editor open mask=' not in log or 'workspace=player selection_kept=1 dirty=0' not in log:raise RuntimeError('Mask Editor did not open over the Player tab: '+log[-400:])
log=run('mask-editor-npc-d3d12',form+['--npc-workspace','--authoring-asset',masked_npc,'--mask-editor',mask_id,'--height','1500','--frames','5','--capture',EVIDENCE/'mask-editor-npc-d3d12.png'])
if 'Mask Editor open mask=' not in log or 'workspace=npc selection_kept=1 dirty=0' not in log:raise RuntimeError('Mask Editor did not open over the NPC tab: '+log[-400:])
# A real click into the Mask Editor's name field and a typed character edit the mask draft, and Undo restores it.
log=run('mask-editor-field-d3d12',form+['--player-workspace','--authoring-asset',f['mask_player'],'--mask-editor',mask_id,'--exercise-mask-field','--height','1500','--frames','12','--capture',EVIDENCE/'mask-editor-field-d3d12.png'])
if 'Editor mask_field typed=1 undo=1' not in log:raise RuntimeError('Typing into the Mask Editor did not edit the mask: '+log[-400:])
# File > Save All with the mouse writes a gameplay draft made in a workspace, with the scene.
log=run('save-all-d3d12',form+['--player-workspace','--authoring-asset',f['mask_player'],'--mask-editor',mask_id,'--exercise-save-all','--height','1500','--frames','16','--capture',EVIDENCE/'save-all-d3d12.png'])
if 'Editor save_all saved=1 restored=1' not in log:raise RuntimeError('File > Save All did not save the gameplay draft: '+log[-400:])
# M6 track: the Guard Melee behaviour opened in the NPC workspace (tree outline with structure controls).
run('behavior-form-d3d12',common+['--npc-workspace','--authoring-asset',f['behavior'],'--height','1500','--frames','5','--capture',EVIDENCE/'behavior-form-d3d12.png'])
# M6 track: the owned Royal Guard NPC definition opened in the NPC workspace.
run('npc-form-d3d12',common+['--npc-workspace','--authoring-asset',f['npc'],'--height','1500','--frames','5','--capture',EVIDENCE/'npc-form-d3d12.png'])
files=['apps/editor/character_preview.cpp','engine/runtime/melee_query.cpp','engine/runtime/world_session.cpp','engine/runtime/include/darkangel/world_session.hpp','engine/assets/ability_cooker.cpp','engine/runtime/assembly.cpp','engine/runtime/include/darkangel/assembly.hpp','apps/editor/editor_ui.cpp','content/npcs/training_dummy.danpc','content/royal_district/RoyalCombat.dascene','content/royal_district/RoyalTraining.dascene','engine/assets/actor_cooker.cpp','content/npcs/royal_guard.danpc','apps/editor/editor_workspaces.cpp','apps/editor/editor_authoring.cpp','apps/editor/editor_ui.hpp','apps/editor/main.cpp','apps/editor/native_authoring.cpp','apps/editor/native_authoring.hpp','engine/assets/effect_cooker.cpp','engine/assets/actor_cooker.cpp','engine/assets/asset_service.cpp','engine/assets/include/darkangel/actor_assets.hpp','apps/editor/character_preview.hpp','engine/assets/combat_kit_cooker.cpp','engine/runtime/action.cpp','engine/runtime/camera.cpp','engine/runtime/include/darkangel/camera.hpp','tests/camera_tests.cpp','engine/runtime/ability.cpp','engine/runtime/ability_prediction.cpp','engine/runtime/combat_kit.cpp','engine/assets/ability_cooker.cpp','engine/runtime/include/darkangel/ability.hpp','engine/runtime/include/darkangel/combat_kit.hpp','engine/runtime/effects.cpp','engine/runtime/character_presentation.cpp','engine/runtime/character_scene.cpp','engine/runtime/include/darkangel/effects.hpp','engine/runtime/include/darkangel/character_presentation.hpp','engine/runtime/include/darkangel/character_scene.hpp','tests/native_authoring_tests.cpp','tests/character_combat_scene_tests.cpp','tests/royal_heavy_scene_tests.cpp','tests/hud_tests.cpp','engine/runtime/hud.cpp','engine/runtime/include/darkangel/hud.hpp','apps/editor/hud_overlay.cpp','games/AshenRoots/royal_hud.cpp','games/AshenRoots/ui/hud.rml','engine/runtime/effects.cpp','content/royal_district/combat/player.dakit','content/royal_district/combat/flinch.daeffect','content/royal_district/combat/stun.daeffect','content/royal_district/combat/stun.daaction','tests/reaction_presentation_tests.cpp','engine/runtime/targeting.cpp','engine/runtime/navigation.cpp','engine/runtime/behavior.cpp','engine/runtime/behavior_tree.cpp','engine/runtime/include/darkangel/behavior.hpp','engine/runtime/include/darkangel/behavior_tree.hpp','tests/behavior_tree_tests.cpp','content/npcs/guard_melee.dabehavior','engine/runtime/include/darkangel/navigation.hpp','tests/navigation_tests.cpp','engine/runtime/melee_query.cpp','content/royal_district/combat/bow.daability','content/royal_district/combat/bow.daaction','engine/runtime/include/darkangel/actor_controller.hpp','apps/editor/editor_controller.hpp','engine/runtime/include/darkangel/targeting.hpp','engine/runtime/motor_state.cpp','engine/runtime/character_motor.cpp','engine/runtime/include/darkangel/character_motor.hpp','engine/runtime/include/darkangel/ability_prediction.hpp','tests/motor_tests.cpp','content/royal_district/combat/light.daability','scripts/verify_effect_reaction.py']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Multi-entity combat (an actor list on stable identities, per-actor controllers, factions, nearest soft lock) and NPC spawn points (scene objects that reference an NPC definition, derived actor identities, Play preparation, two Royal Guards from spawn points); melee soft lock from the player targeting module (authored on the Ability, proposed by the targeting module, resolved by the motor on authoritative, predicted and replayed ticks, HUD indicator); runtime Health HUD through RmlUi with a native view model, drawn in the editor Game viewport; stagger as gameplay (effect movement lock predicted by the owner, looping stun reaction, death movement lock, Flinch bound to the Royal Light hit); scripted training attacker on the combat target (owner damage, flinch, interruption, dodge invulnerability and death in fresh Play); Player virtual camera rig (free-look and aim) with blending; in-Play mask switching; mask assets with four Player slots, active-mask kit and worn face-socket visual; dodge as an authored ability on the kit utility ingress with dash and facing lock; kit death timeline with terminal precedence and held final pose; optional native effect reaction binding, frozen action/clip closure, stateless checked public reaction selection shared by owner and observed poses, authoring commands/forms and fresh authoritative Play',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},binary_sha256={p:hashlib.sha256((BUILD/p).read_bytes()).hexdigest() for p in ['DarkAngelEditor.exe','NativeAuthoringTests.exe','ReactionPresentationTests.exe']},limitations=['One visible full-body reaction per actor, root policy none; no reaction queue, blend ramps, additive or upper-body reactions.','Reaction timelines carry cue blocks only and no cue is dispatched by presentation in this slice.','The fixture reuses the existing attack clip as its flinch; no flinch clip or default Royal reaction content is shipped.','All actors in a scene share the player rig and skin; a spawn point never respawns; hits do not check faction, so NPCs can hit each other; nothing regenerates Stamina yet.','Scripted native commands and D3D12 form/rendering evidence, not physical pointer input, remote graphical timing/loss or multiplayer qualification.','M4/M5 In progress; EOS blocked; M6-M9 not started; publication crash-recovery limits unchanged.'])
(EVIDENCE/'effect-reaction.json').write_text(json.dumps(receipt,indent=2)+'\n')
