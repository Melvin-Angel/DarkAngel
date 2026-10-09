"""Bounded owner/public effect state and persistent cue reconstruction."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None,timeout=3600):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
 return result.stdout
run('effect-replication-product',['python','scripts/verify_action_tags.py',*(['--build'] if args.build else [])])
if args.build:
 env=activate()
 for profile in ('m5-editor-relwithdebinfo','m1-relwithdebinfo'):run('effect-replication-build-'+profile,[CMAKE,'--build',ROOT/'build'/profile,'--target','EffectReplicationTests','--parallel','2'],env)
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m1-relwithdebinfo','headless')]:run('effect-replication-'+label,[ROOT/'build'/profile/'EffectReplicationTests.exe'])
sources=['CMakeLists.txt','engine/runtime/world_session.cpp','engine/runtime/include/darkangel/world_session.hpp','engine/runtime/effects.cpp','engine/runtime/include/darkangel/effects.hpp','engine/runtime/effect_replication.cpp','engine/runtime/effect_wire.hpp','engine/runtime/include/darkangel/effect_replication.hpp','engine/assets/effect_cooker.cpp','tests/effect_replication_tests.cpp','tests/effect_asset_tests.cpp','scripts/verify_effect_replication.py']
binaries=['build/'+p+'/EffectReplicationTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m1-relwithdebinfo')]
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Existing protocol3 bounded owner/public effect component, authored visibility and persistent cue identities, current timers/durable presentation attribution, frozen read-only preparation and late join',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['action-tags.json','effect-assets.json','effect-budget.json','royal-combat.json'],limitations=['Effect state is a separate timestamped presentation component, not an additional predicted gameplay authority or atomic owner resource correction. It never executes damage, modifiers or historical applications.','Only durable source session/network/activation attribution is replicated; captured power/source statistics stay native and private. Owner/public effect visibility is explicit, default owner.','Persistent cue state and bounded Begin/Update/End changes are prepared; visual/audio adapters, one-shot cue confirmation and primary Royal status gameplay remain required.','Fault checks cover dense bounded fragmentation, duplication/reordering, missing-fragment expiry/recovery and four-actor fairness; full RTT/loss/burst/provider/clock/bandwidth/frame-performance qualification remains open.','Reservations, reflected/typed graph/socket authoring, generated constants/checked Luau, authored dodge/status/motor/combo/projectile and graphical runtime bars remain open. M4/M5 stay In progress; M6 not started.'])
(EVIDENCE/'effect-replication.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Owner/public effect state, persistent cue reconstruction, native/editor/Headless and full product compatibility passed')
