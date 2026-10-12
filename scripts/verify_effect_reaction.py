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
# M7: presentation cues in Play. Visual cues play as effects; audio has no service yet, so a recording sink shows what it would be told. The owner's
# Light starts the predictable swing cue (audio) once and it has faded by tick 30; the Heavy hit's Burn starts its aura cue (visual), still live.
def cues(log):
    line=[l for l in log.splitlines() if l.startswith('Editor cues ')][-1];return dict(part.split('=') for part in line.split()[2:])
seen=cues(run('cues-swing-d3d12',common+['--exercise-combat','--exercise-cues','--frames','30','--capture',EVIDENCE/'cues-swing-d3d12.png']))
# The swing sound and the weapon trail start on the predicted press; the Light's hit applies Flinch, whose cue plays the hit spark and the hit sound and lays a decal.
# A hidden run has no audio device: the service runs on its silent driver and the lifecycle is the same.
if not(int(seen['assets'])>=3 and [key for key in seen['audio'].split(',') if key!='Footstep']==['Attack.Start','Status.Flinch'] and seen['visual']=='Attack.Start,Status.Flinch' and seen['vfx_missing']=='0' and int(seen['vfx_loaded'])>=1 and int(seen['trails'])>=1 and int(seen['trail_points'])>=2 and int(seen['decals'])>=1 and int(seen['primitive_quads'])>0 and seen['primitive_missing']=='0'):raise RuntimeError('Editor Play did not present the swing and hit cues: '+str(seen))
if not(seen['audio_service']=='1' and seen['audio_device']=='0' and int(seen['audio_loaded'])>=2 and int(seen['audio_played'])>=2 and seen['audio_missing']=='0'):raise RuntimeError('Editor Play did not play the swing and hit sounds on the silent driver: '+str(seen))
# Footsteps: the player runs forward for two seconds and is heard doing it, a step every stride of ground covered, from its predicted position.
# A rejected activation in a live session: authority refuses the press (fault injection). The swing sound and the weapon trail start on the
# predicted press, as real effect and sound, and are cut short when the refusal arrives; no hit follows, so no hit cue, spark or decal.
seen=cues(run('cues-rejected-d3d12',common+['--exercise-combat','--exercise-cues','--exercise-reject','--frames','30','--capture',EVIDENCE/'cues-rejected-d3d12.png']))
if not(seen['refused']=='1' and [key for key in seen['audio'].split(',') if key!='Footstep']==['Attack.Start'] and seen['visual']=='Attack.Start' and seen['live']=='0' and int(seen['audio_stops'])>=1 and seen['trails']=='0' and seen['decals']=='0' and seen['audio_played']=='1' and seen['audio_missing']=='0' and seen['primitive_missing']=='0'):raise RuntimeError('A refused press did not start and then cut its real sound and trail: '+str(seen))
seen=cues(run('cues-footsteps-d3d12',common+['--exercise-character','--exercise-cues','--frames','120','--capture',EVIDENCE/'cues-footsteps-d3d12.png']))
if not(set(seen['audio'].split(','))=={'Footstep'} and 8<=int(seen['audio_played'])<=30 and seen['audio_missing']=='0' and seen['visual']=='none'):raise RuntimeError('Editor Play did not play the player footsteps: '+str(seen))
seen=cues(run('cues-burn-d3d12',common+['--exercise-reaction','--exercise-cues','--frames','30','--capture',EVIDENCE/'cues-burn-d3d12.png']))
if not('Status.Burn' in seen['visual'].split(',') and int(seen['visual_live'])>=1 and int(seen['closure'])>=2):raise RuntimeError('Editor Play did not present the Burn aura cue: '+str(seen))
# The aura is a real effect: cooked from its Effekseer source, loaded, playing and drawn through the renderer, with nothing skipped.
if not(int(seen['vfx_loaded'])>=1 and int(seen['vfx_playing'])>=1 and int(seen['vfx_particles'])>0 and int(seen['vfx_quads'])>0 and int(seen['vfx_draws'])>=1 and seen['vfx_unsupported']=='0' and seen['vfx_missing']=='0'):raise RuntimeError('Editor Play did not draw the Burn aura effect: '+str(seen))
# M7: the VFX tab. The selected effect is cooked through the Effekseer tool and plays in the preview; a change of its source file on disk
# (what saving it in Effekseer does) cooks it again without anyone asking.
def vfx_tab(log):
    line=[l for l in log.splitlines() if l.startswith('Editor vfx_tab ') and 'error' not in l.split()[2]][-1];return dict(part.split('=') for part in line.split()[2:])
seen=vfx_tab(run('vfx-tab-d3d12',common+['--vfx-workspace','--vfx-select','vfx/placeholder/burn_aura.efkproj','--exercise-vfx-edit','--frames','45','--capture',EVIDENCE/'vfx-tab-d3d12.png']))
if not(int(seen['effects'])>=2 and seen['selected']=='vfx/placeholder/burn_aura.efkproj' and seen['cooked']=='1' and seen['cooks']=='2' and seen['textures']=='1' and seen['playing']=='1' and int(seen['quads'])>0 and seen['unsupported']=='0' and seen['refused']=='0'):raise RuntimeError('The VFX tab did not preview the effect and cook it again after its source changed: '+str(seen))
# The cue form: a new cue is staged, edited and saved through the draft path, and its cooked product names its effect and sound by identity.
log=run('cue-form-d3d12',common+['--vfx-workspace','--vfx-select','vfx/placeholder/hit_spark.efkproj','--exercise-cue-form','--frames','20','--capture',EVIDENCE/'cue-form-d3d12.png'])
line=[l for l in log.splitlines() if l.startswith('Editor cue_form ')][-1];seen=dict(part.split('=') for part in line.split()[2:])
if not(int(seen['cues'])>=5 and seen['path'].startswith('vfx/cues/gate_') and seen['saved']=='1' and seen['tail']=='20' and seen['visual_resolved']=='1' and seen['audio_resolved']=='1'):raise RuntimeError('The cue form did not create, save and cook a cue: '+str(seen))
# The Audio tab: a sound definition auditioned, a mixer volume changed in its draft and heard at once, a whole profile auditioned.
def audio(log):
    line=[l for l in log.splitlines() if l.startswith('Editor audio ')][-1];return dict(part.split('=') for part in line.split()[2:])
def asset_id(path):return json.loads((source/path).read_text())['asset']
seen=audio(run('audio-tab-d3d12',common+['--audio-workspace','--exercise-audio','--frames','30','--capture',EVIDENCE/'audio-tab-d3d12.png']))
if not(seen['workspace']=='1' and int(seen['waves'])>=6 and int(seen['definitions'])>=3 and int(seen['profiles'])>=2 and seen['mixers']=='1' and seen['driver']=='null' and int(seen['loaded'])>=2 and abs(float(seen['bus_music'])-0.3)<1e-3 and seen['director']=='1' and seen['profile']==asset_id('audio/profiles/royal_calm.daaudioprofile') and seen['music']==asset_id('audio/music_calm.dasound') and seen['ambient']=='1' and seen['missing']=='0'):raise RuntimeError('The Audio tab did not audition a sound and a profile and apply a mixer change: '+str(seen))
# An agent changes actual effect and sound content, not wrapper parameters: the hit spark's own node (how many sparks it emits) in the
# Effekseer project, and the hit sound's samples in its WAV. Both sources cook again, and the next Play draws and plays the changed products.
def generations(seen,field,name):
    return dict(item.rsplit(':',1) for item in seen[field].split(';'))[name]
before=cues(run('content-before-d3d12',common+['--exercise-combat','--exercise-cues','--frames','30','--capture',EVIDENCE/'content-before-d3d12.png']))
spark=source/'vfx/placeholder/hit_spark.efkproj';spark_text=spark.read_text(encoding='utf8');hit=source/'audio/placeholder/hit.wav';hit_bytes=hit.read_bytes()
if spark_text.count('<MaxGeneration><Value>14</Value></MaxGeneration>')!=1:raise RuntimeError('The hit spark source is not the one this gate edits')
import math,struct
spark.write_text(spark_text.replace('<MaxGeneration><Value>14</Value></MaxGeneration>','<MaxGeneration><Value>40</Value></MaxGeneration>'),encoding='utf8')
samples=bytes().join(struct.pack('<h',int(20000*math.sin(2*math.pi*330*i/48000)*math.exp(-i/4000))) for i in range(9600));hit.write_bytes(bytes('RIFF','ascii')+struct.pack('<I',36+len(samples))+bytes('WAVEfmt ','ascii')+struct.pack('<IHHIIHH',16,1,1,48000,96000,2,16)+bytes('data','ascii')+struct.pack('<I',len(samples))+samples)
try:
    after=cues(run('content-after-d3d12',common+['--exercise-combat','--exercise-cues','--frames','30','--capture',EVIDENCE/'content-after-d3d12.png']))
finally:
    spark.write_text(spark_text,encoding='utf8');hit.write_bytes(hit_bytes)
changed=dict(effect=(generations(before,'effect_generations','vfx/placeholder/hit_spark.efkproj'),generations(after,'effect_generations','vfx/placeholder/hit_spark.efkproj')),sound=(generations(before,'sound_generations','audio/placeholder/hit.wav'),generations(after,'sound_generations','audio/placeholder/hit.wav')),aura=(generations(before,'effect_generations','vfx/placeholder/burn_aura.efkproj'),generations(after,'effect_generations','vfx/placeholder/burn_aura.efkproj')),particles=(int(before['vfx_particles']),int(after['vfx_particles'])))
if not(changed['effect'][0]!=changed['effect'][1] and changed['sound'][0]!=changed['sound'][1] and changed['aura'][0]==changed['aura'][1] and changed['particles'][1]>changed['particles'][0] and after['vfx_missing']=='0' and after['audio_missing']=='0' and int(after['audio_played'])>=2 and after['vfx_unsupported']=='0'):raise RuntimeError('Edited effect and sound content did not reach Play: '+str(changed)+' '+str(after))
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
# The baked navigation mesh drawn over the Scene viewport.
log=run('navigation-overlay-d3d12',common+['--exercise-navigation-overlay','--frames','5','--capture',EVIDENCE/'navigation-overlay-d3d12.png'])
if 'Editor navigation_overlay polygons=' not in log:raise RuntimeError('Navigation overlay was not drawn: '+log[-400:])
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
if not('Editor encounter fired=1 spawned=2 ' in log and 'events=play.started,gate.entered,gate.fight_started' in log and ' hud_target=' in log and ' readouts=' in log):raise RuntimeError('Editor Play did not start the encounter from its trigger volume: '+log[-400:])
# Play: the scene's audio profile starts with it, the encounter's is layered while the fight runs, and a mixer volume changed during Play is applied.
audio_source=json.loads(json.dumps(encounter_source));audio_entities=audio_source['sources'][audio_source['root']]['entities']
audio_entities['00000000000000000000000000000008']['encounter']['audio_profile']=asset_id('audio/profiles/royal_battle.daaudioprofile')
audio_entities['0000000000000000000000000000000a']=gameplay_entity('Scene audio',0.0,0.0,audio={'profile':asset_id('audio/profiles/royal_calm.daaudioprofile')})
(source/'royal_district/RoyalEncounterAudio.dascene').write_text(json.dumps(audio_source,indent=2)+chr(10))
# Without the trigger volume the fight never starts, so only the scene's profile plays.
quiet_source=json.loads(json.dumps(audio_source));del quiet_source['sources'][quiet_source['root']]['entities']['00000000000000000000000000000007'];(source/'royal_district/RoyalQuietAudio.dascene').write_text(json.dumps(quiet_source,indent=2)+chr(10))
seen=audio(run('audio-play-d3d12',scene(guarded,'RoyalQuietAudio.dascene')+['--exercise-character','--exercise-audio','--frames','40','--capture',EVIDENCE/'audio-play-d3d12.png']))
if not(seen['director']=='1' and seen['profile']==asset_id('audio/profiles/royal_calm.daaudioprofile') and seen['music']==asset_id('audio/music_calm.dasound') and seen['ambient']=='1' and seen['layers']=='0' and seen['missing']=='0' and abs(float(seen['bus_ambient'])-1.0)<1e-3):raise RuntimeError('Play did not start the scene audio profile: '+str(seen))
log=run('audio-encounter-d3d12',scene(guarded,'RoyalEncounterAudio.dascene')+['--exercise-encounter','--exercise-audio','--frames','200','--capture',EVIDENCE/'audio-encounter-d3d12.png']);seen=audio(log)
if not('Editor encounter fired=1 spawned=2 ' in log and seen['profile']==asset_id('audio/profiles/royal_battle.daaudioprofile') and seen['music']==asset_id('audio/music_battle.dasound') and seen['layers']=='1' and seen['ambient']=='1' and seen['missing']=='0' and abs(float(seen['bus_ambient'])-10**(-6/20))<1e-3 and abs(float(seen['bus_npc'])-0.25)<1e-3):raise RuntimeError('The encounter did not layer its audio profile, or the mixer change during Play was not applied: '+str(seen))
# Interaction: the same scene with a lever instead of the trigger volume. The player walks to it, the HUD offers it, the interact input
# asks the server, and the encounter that listens for its event starts.
lever_source=json.loads(json.dumps(encounter_source));lever_entities=lever_source['sources'][lever_source['root']]['entities'];del lever_entities['00000000000000000000000000000007']
lever_entities['0000000000000000000000000000000a']=gameplay_entity('Gate lever',0.0,5.0,interactable={'prompt':'Pull the lever','event':'lever.pulled','radius':2.0,'once':True})
lever_entities['00000000000000000000000000000008']['encounter']['trigger']={'event':'lever.pulled'}
(source/'royal_district/RoyalLever.dascene').write_text(json.dumps(lever_source,indent=2)+chr(10))
log=run('interaction-d3d12',scene(guarded,'RoyalLever.dascene')+['--exercise-interaction','--frames','200','--capture',EVIDENCE/'interaction-d3d12.png'])
if not('Editor interaction asked=1 result=Accepted used=1 ' in log and 'lever.pulled' in log):raise RuntimeError('Editor Play did not use the lever through the server: '+log[-400:])
# Splines and scatter: the training scene with a four-point spline that scatters one of the scene's own hut models along it.
spline_source=json.loads((source/'royal_district/RoyalTraining.dascene').read_text());spline_entities=spline_source['sources'][spline_source['root']]['entities']
hut=[entity['model'] for entity in spline_entities.values() if entity.get('model') and 'MudHut' in entity.get('name','')][0]
spline_entities['0000000000000000000000000000000b']=gameplay_entity('Hut row',-12.0,-11.0,spline={'closed':False,'scatter':{'model':hut,'spacing':7.0,'jitter':0.0,'seed':3,'align':True}})
for index,(x,z) in enumerate([(-12.0,-11.0),(-4.0,-13.0),(4.0,-13.0),(12.0,-11.0)]):spline_entities['000000000000000000000000000000%02x'%(0x10+index)]=gameplay_entity('Hut row point',x,z,spline_point={'spline':'0000000000000000000000000000000b','index':index})
(source/'royal_district/RoyalSpline.dascene').write_text(json.dumps(spline_source,indent=2)+chr(10))
log=run('spline-d3d12',scene(common,'RoyalSpline.dascene')+['--exercise-spline','--frames','5','--capture',EVIDENCE/'spline-d3d12.png'])
if not('Editor spline splines=1 points=4 ' in log and ' scatter=4' in log):raise RuntimeError('Editor Scene did not draw the spline and its scatter: '+log[-400:])
# Background cooking: a source copied into the project (not through Import) is listed as uncooked and the sweep cooks it.
log=run('cook-sweep-d3d12',common+['--assets-workspace','--exercise-cook-sweep','--frames','5','--capture',EVIDENCE/'cook-sweep-d3d12.png'])
if 'Editor cook_sweep waiting_before=' not in log:raise RuntimeError('The background cook sweep did not cook a copied source: '+log[-400:])
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
