"""M8 stage 1b: a party in separate editor processes over GNS on localhost.

Run after scripts/verify_effect_reaction.py: it uses that run's copied authoring fixture and its lever and gate scenes.
Each gate starts one host editor; the host starts and owns its guest editors, except in the live admission gate, where
this script starts a guest by hand after the host's scene is running. Every process writes a JSON receipt of what it
saw, and a script given on the command line drives each player and ends the run on the host's tick, which every process
of a party shares. Same-machine UDP only: this says nothing about two machines, latency, loss or EOS.
"""
import json,shutil,subprocess,time,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';EVIDENCE=ROOT/'docs/implementation/evidence';gates=[];began=time.monotonic()
f=json.loads((BUILD/'authoring-fixture.json').read_text());source=Path(f['source']);cache=Path(f['cache'])
model=json.loads((source/'royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport').read_text())['id'];collision=json.loads((source/'royal_district/collision/environment.dacollision').read_text())['asset']
def editor(registry,scene):
    return [BUILD/'DarkAngelEditor.exe','--registry',f[registry],'--cas',cache/'cas','--model',model,'--scene',source/('royal_district/'+scene),'--character-kit',f['kit'],'--character-collision',collision,'--character-player','00000000000000000000000000000004','--sources',source,'--asset-cache',cache,'--backend','d3d12','--hidden','--no-audio']
def fail(name,why,*seen):raise RuntimeError(name+': '+why+'\n'+'\n'.join(json.dumps(x,indent=1)[:3000] if isinstance(x,dict) else str(x)[-3000:] for x in seen))
def receipt(path):
    if not path.exists():return {}
    for attempt in range(20):
        try:return json.loads(path.read_text())
        except (json.JSONDecodeError,PermissionError):time.sleep(.05)
    return {}
def clean(name):
    for old in EVIDENCE.glob(name+'*'):old.unlink()
    for old in (ROOT/'.cache/party').glob('player-*.log'):old.unlink()
def keep_guest_logs(name):
    for seat in (1,2,3):
        log=ROOT/'.cache/party'/('player-%d.log'%seat)
        if log.exists():shutil.copyfile(log,EVIDENCE/('%s-player-%d.log'%(name,seat)))
def lines(text):return [line.replace('\x1b[39m','').strip() for line in text.splitlines() if 'Party ' in line or 'DarkAngel' in line]
def host(name,registry,scene,port,players,own,guests,extra=()):
    """One host editor that owns its guests. Returns the host log, its receipt and the guests' receipts and logs."""
    clean(name);start=time.monotonic()
    command=editor(registry,scene)+['--game-workspace','--listen','127.0.0.1:%d'%port,'--players',players,'--party-report',EVIDENCE/(name+'.json'),'--capture',EVIDENCE/(name+'.png'),'--player-script',own]+(['--input-script',guests] if guests else [])+list(extra)
    result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=420)
    log=result.stdout+result.stderr;(EVIDENCE/(name+'.log')).write_text(log,encoding='utf8');keep_guest_logs(name)
    gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode:fail(name,'the host exited with %d'%result.returncode,log)
    seen=[receipt(EVIDENCE/('%s-player-%d.json'%(name,seat))) for seat in range(1,int(players))];texts=[(EVIDENCE/('%s-player-%d.log'%(name,seat))).read_text(encoding='utf8',errors='replace') if (EVIDENCE/('%s-player-%d.log'%(name,seat))).exists() else '' for seat in range(1,int(players))]
    return log,receipt(EVIDENCE/(name+'.json')),seen,texts
def best(seen):return seen.get('fullest') or seen.get('last_running') or seen
def health(seen):return min([actor['health'] for actor in seen.get('actors',[])] or [0])
def together(name,hosted,guest,players=2):
    full=best(hosted)
    if not(len(full.get('players',[]))==players and full.get('authority') is True and full.get('running')):fail(name,'the host never ran the scene with the whole party',hosted)
    if not(guest.get('running') and guest.get('playing') and guest.get('authority') is False and len(guest.get('players',[]))==players and all(player['pose'] for player in guest['players'])):fail(name,'the guest did not end its run playing, without authority, with every player presented',guest)
    if guest['owner'] not in [player['id'] for player in full['players']] or guest['owner']==full['owner']:fail(name,'the guest avatar is not a separate member of the host scene',hosted,guest)

# Movement, combat and a guest leaving. The guest walks up to the training dummy and hits it; the host stands. The host's
# capture is taken while both are in; then the guest quits and the host plays on alone.
name='party-combat-d3d12'
log,hosted,(guest,),(guest_log,)=host(name,'reaction_registry','RoyalTraining.dascene',26100,'2','await-tick:200;capture;await-tick:330;quit','walk-to:0,2.5;attack;await-tick:240;quit')
together(name,hosted,guest)
if 'Party host prepared epoch=1 players=2' not in log or 'Party host resumed epoch=1 players=2' not in log or 'Party guest prepared epoch=1' not in guest_log:fail(name,'the readiness barrier was not passed by both processes',lines(log),lines(guest_log))
mine=[player for player in guest['players'] if player['id']==guest['owner']][0];theirs=[player for player in best(hosted)['players'] if player['id']==guest['owner']][0]
if not(guest['script_steps']==4 and mine['position'][2]>2.0 and theirs['position'][2]>2.0):fail(name,'the guest did not move in both processes',hosted,guest)
if not(health(guest)<100 and health(best(hosted))<100 and health(guest)==health(best(hosted)) and guest['highest_operation']>=1):fail(name,'the guest\'s attack did not damage the dummy in both processes',hosted,guest)
if guest['rebases']>4 or best(hosted)['rebases']>4:fail(name,'prediction was rebuilt as a matter of routine',hosted,guest)
if not(hosted.get('running') and len(hosted['players'])==1 and hosted['tick']>=330 and hosted['epoch']==1 and any('Guest' in line for line in hosted['console'])):fail(name,'the host did not play on alone after the guest left',hosted)
print(name+' passed',flush=True)

# Interaction: the guest walks round the host to the lever and uses it; authority starts the encounter that listens for
# its event, and the guards it spawns reach both processes.
name='party-interaction-d3d12'
log,hosted,(guest,),(guest_log,)=host(name,'npc_registry','RoyalLever.dascene',26132,'2','await-tick:270;capture;await-tick:370;quit','walk-to:1.5,2;walk-to:1.2,3.8;use;await-tick:300;quit')
together(name,hosted,guest)
if guest.get('interaction')!='Accepted':fail(name,'authority did not accept the guest\'s use of the lever',hosted,guest)
if not(len(guest['actors'])>=2 and len(best(hosted)['actors'])>=2):fail(name,'the encounter the lever starts did not bring its guards into both processes',hosted,guest)
print(name+' passed',flush=True)

# Content mismatch: the guest's prepared content hash is made wrong. It is refused before it reaches the scene and the
# host plays alone.
name='party-mismatch-d3d12'
log,hosted,(guest,),(guest_log,)=host(name,'reaction_registry','RoyalTraining.dascene',26164,'2','await-tick:150;quit','',['--party-mismatch'])
if 'Party host dropped seat=1 reason=Scene content or epoch mismatch' not in log or 'Party guest failed: Scene content or epoch mismatch' not in guest_log:fail(name,'the mismatched guest was not refused by name',lines(log),lines(guest_log))
if guest.get('running') or guest.get('last_running') or guest.get('players'):fail(name,'the mismatched guest entered the scene',guest)
if not(hosted.get('running') and len(hosted['players'])==1 and hosted['tick']>=150 and hosted['authority'] is True):fail(name,'the host did not play alone after refusing the guest',hosted)
print(name+' passed',flush=True)

# Travel. Two scenes on the training layout, each its own scene asset, each with an entry point and a way to the other
# that is used on purpose, placed and saved by the editor's own commands. The host takes the party to the temple, the
# guest takes it back. Guests are held while the host prepares each destination, prepare it themselves and acknowledge.
made=json.loads((source/'royal_district/RoyalGate.dascene').read_text())
def travel_scene(file,lever):
    root=str(uuid.uuid4());copy=json.loads(json.dumps(made));old=copy['root'];copy['root']=root;copy['sources'][root]=copy['sources'].pop(old);copy['sources'][root]['asset']=root
    if not lever:copy['sources'][root]['entities'].pop('0000000000000000000000000000000a',None)
    for stale in source.glob('royal_district/'+file+'*'):
        shutil.rmtree(stale) if stale.is_dir() else stale.unlink()
    (source/('royal_district/'+file)).write_text(json.dumps(copy,indent=2)+chr(10));return root
gate_root=travel_scene('RoyalPartyGate.dascene',True);temple_root=travel_scene('RoyalPartyTemple.dascene',False)
def place(name,scene,destination,entry=None):
    start=time.monotonic();command=editor('npc_registry',scene)+['--capture-workspace','--exercise-travel-place','--travel-scene',destination,'--frames','6','--capture',EVIDENCE/(name+'.png')]+(['--travel-entry',entry] if entry else [])
    result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=420);log=result.stdout+result.stderr;(EVIDENCE/(name+'.log')).write_text(log,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
    if result.returncode or 'entries=1 transitions=1' not in log:fail(name,'the editor did not place and save an entry point and a transition',log)
    return [part.split('=')[1] for part in [l for l in log.splitlines() if 'Editor travel_place ' in l][-1].split() if part.startswith('entry=')][0]
temple_entry=place('party-travel-temple-d3d12','RoyalPartyTemple.dascene',gate_root);gate_entry=place('party-travel-gate-d3d12','RoyalPartyGate.dascene',temple_root,temple_entry)
name='party-travel-d3d12'
log,hosted,(guest,),(guest_log,)=host(name,'npc_registry','RoyalPartyGate.dascene',26196,'2','await-scene:1;walk-to:2,7;use;await-scene:2;await-tick:150;capture;await-scene:3;await-tick:220;quit','await-scene:2;await-tick:170;walk-to:0.8,8;use;await-scene:3;await-tick:120;quit')
together(name,hosted,guest)
if not(hosted['arrivals']==3 and hosted['failures']==0 and hosted['scene']==gate_root and hosted['epoch']==3 and hosted['tick']>=220):fail(name,'the host did not take the party to the temple and come back with it',hosted,lines(log))
if not(best(hosted)['epoch']>=2 and guest['arrivals']==3 and guest['epoch']==3 and guest['script_steps']==7):fail(name,'the guest did not follow the party both ways',hosted,guest,lines(guest_log))
for epoch in (1,2,3):
    if 'Party host resumed epoch=%d players=2'%epoch not in log or 'Party guest prepared epoch=%d'%epoch not in guest_log:fail(name,'scene epoch %d was not prepared and resumed by the whole party'%epoch,lines(log),lines(guest_log))
if guest_log.count('Party guest held=1')<2:fail(name,'the guest was not held while the host prepared travel',lines(guest_log))
print(name+' passed',flush=True)

# Live admission. The host starts alone and plays. A guest started by hand joins the running scene: its avatar enters
# beside the host at the tick the scene has reached, it fights, and leaves. A second guest then takes the same seat and
# finds the dummy as the first left it.
name='party-live-d3d12';clean(name);start=time.monotonic();port=26228
command=editor('reaction_registry','RoyalTraining.dascene')+['--game-workspace','--listen','127.0.0.1:%d'%port,'--players','1','--party-report',EVIDENCE/(name+'.json'),'--capture',EVIDENCE/(name+'.png'),'--player-script','await-players:2;wait:500;capture;await-players:1;await-players:2;await-players:1;wait:200;quit']
with (EVIDENCE/(name+'.log')).open('w',encoding='utf8') as out:
    running=subprocess.Popen(list(map(str,command)),cwd=ROOT,stdout=out,stderr=subprocess.STDOUT)
    try:
        def wait(condition,what,seconds=120):
            until=time.monotonic()+seconds
            while time.monotonic()<until:
                seen=receipt(EVIDENCE/(name+'.json'))
                if running.poll() is not None:fail(name,'the host ended while waiting for '+what,seen)
                if condition(seen):return seen
                time.sleep(.25)
            fail(name,'timed out waiting for '+what,receipt(EVIDENCE/(name+'.json')))
        alone=wait(lambda seen:seen.get('running') and seen.get('tick',0)>=120 and len(seen.get('players',[]))==1,'the host to play alone')
        def join(seat_name,script):
            joined=editor('reaction_registry','RoyalTraining.dascene')+['--game-workspace','--join','127.0.0.1:%d'%port,'--player','1','--party-report',EVIDENCE/(seat_name+'.json'),'--capture',EVIDENCE/(seat_name+'.png'),'--player-script',script]
            result=subprocess.run(list(map(str,joined)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=240);text=result.stdout+result.stderr;(EVIDENCE/(seat_name+'.log')).write_text(text,encoding='utf8')
            if result.returncode:fail(name,'a guest exited with %d'%result.returncode,text)
            return receipt(EVIDENCE/(seat_name+'.json')),text
        first,first_log=join(name+'-first','walk-to:0,2.5;attack;wait:700;quit')
        between=wait(lambda seen:seen.get('running') and len(seen.get('players',[]))==1 and seen.get('tick',0)>first.get('tick',0),'the host to play on after the first guest left')
        second,second_log=join(name+'-second','walk-to:1,2.2;wait:500;quit')
        running.wait(timeout=120)
    finally:
        if running.poll() is None:running.kill()
log=(EVIDENCE/(name+'.log')).read_text(encoding='utf8',errors='replace');hosted=receipt(EVIDENCE/(name+'.json'));gates.append(dict(name=name,exit=running.returncode,seconds=round(time.monotonic()-start,3)))
if running.returncode:fail(name,'the host exited with %d'%running.returncode,log)
for guest,guest_log in ((first,first_log),(second,second_log)):
    together(name,hosted,guest)
    if not(guest['epoch']==1 and guest['tick']>alone['tick'] and 'Party guest prepared epoch=1' in guest_log):fail(name,'a guest did not join the scene that was already running',guest,lines(guest_log))
if log.count('Party host admitting seat=1 epoch=1')!=2 or 'Party host resumed epoch=1 players=1' not in log:fail(name,'the host did not admit each guest to the scene it started alone',lines(log))
if not(health(first)<100 and health(second)==health(first) and health(hosted)==health(first)):fail(name,'the second guest did not find the dummy as the first left it',hosted,first,second)
if not(hosted['epoch']==1 and hosted['arrivals']==1 and hosted['tick']>between['tick']>first['tick'] and len(hosted['players'])==1 and hosted['running']):fail(name,'the host scene did not run on through both admissions',hosted)
print(name+' passed',flush=True)
(EVIDENCE/'m8-1b-network-gates.json').write_text(json.dumps(dict(gates=gates,seconds=round(time.monotonic()-began,1),transport='GameNetworkingSockets UDP, 127.0.0.1, separate editor processes'),indent=2)+chr(10))
print('All %d party network gates passed in %.1f seconds'%(len(gates),time.monotonic()-began),flush=True)
