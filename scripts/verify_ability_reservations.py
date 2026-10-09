"""Deferred typed commit markers, native claims and owner replay."""
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
run('ability-reservations-product',['python','scripts/verify_effect_replication.py',*(['--build'] if args.build else [])])
if args.build:
 env=activate()
 for profile in ('m5-editor-relwithdebinfo','m1-relwithdebinfo'):run('ability-reservations-build-'+profile,[CMAKE,'--build',ROOT/'build'/profile,'--target','AbilityReservationTests','--parallel','2'],env)
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m1-relwithdebinfo','headless')]:run('ability-reservations-'+label,[ROOT/'build'/profile/'AbilityReservationTests.exe'])
sources=['CMakeLists.txt','engine/runtime/action.cpp','engine/runtime/include/darkangel/action.hpp','engine/runtime/ability.cpp','engine/runtime/ability_state.hpp','engine/runtime/include/darkangel/ability.hpp','engine/runtime/ability_prediction.cpp','engine/runtime/ability_wire.cpp','engine/runtime/world_session.cpp','engine/runtime/include/darkangel/world_session.hpp','engine/assets/ability_cooker.cpp','tests/ability_reservation_tests.cpp','tests/ability_asset_tests.cpp','tests/ability_tag_tests.cpp','scripts/verify_ability_reservations.py']
binaries=['build/'+p+'/AbilityReservationTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m1-relwithdebinfo')]
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Frozen typed commit marker, one-execution resource/cooldown/slot claims, native marker revalidation/consumption/lifecycle release, correlated commitment trace and owner schema5 replay',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['effect-replication.json','action-tags.json','effect-assets.json','royal-combat.json'],limitations=['The initial policy has one active execution per owner and one selected marker in a single-loop action; concurrent multi-spec casts, per-step/channel/refund policies and targeting reservations are not implied.','Start receipts confirm activation/claims. Native commitment updates correlate owner/activation/start operation/block; they are authority-side traces. Owner correction carries active start operation and pending resource/cooldown claims, never an additional client gameplay authority.','Owner prediction uses the same prepared claim/marker subset and corrected baseline; external effect-dependent attribute recomputation and periodic gameplay remain unpredicted.','Primary Royal preserves its light attack; authored charge/dodge/status/motor/combo/projectile, reflected/Luau/graph/socket, full clock/fault/streaming/performance and graphical RmlUi gates remain open. M4/M5 stay In progress; M6 not started.'])
(EVIDENCE/'ability-reservations.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Deferred native claims/markers, owner replay, native/editor/Headless and complete product compatibility passed')
