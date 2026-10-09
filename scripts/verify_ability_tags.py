"""Generation-fenced owner/public tags and native activation/prediction gates."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None,timeout=3300):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
run('ability-tags-product',['python','scripts/verify_effect_assets.py',*(['--build'] if args.build else [])])
if args.build:
 env=activate()
 for profile in ('m5-editor-relwithdebinfo','m1-relwithdebinfo'):
  run('ability-tags-build-'+profile,[CMAKE,'--build',ROOT/'build'/profile,'--target','AbilityTagTests','--parallel','2'],env)
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m1-relwithdebinfo','headless')]:run('ability-tags-'+label,[ROOT/'build'/profile/'AbilityTagTests.exe'])
sources=['CMakeLists.txt','engine/runtime/tags.cpp','engine/runtime/include/darkangel/tags.hpp','engine/runtime/ability.cpp','engine/runtime/ability_wire.cpp','engine/runtime/ability_prediction.cpp','engine/runtime/ability_observer.cpp','engine/runtime/world_session.cpp','engine/runtime/ability_effects.cpp','engine/runtime/include/darkangel/ability.hpp','engine/runtime/include/darkangel/ability_prediction.hpp','engine/runtime/include/darkangel/ability_observer.hpp','engine/runtime/include/darkangel/effects.hpp','engine/assets/effect_cooker.cpp','tests/ability_tag_tests.cpp','scripts/verify_ability_tags.py']
binaries=['build/'+p+'/AbilityTagTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m1-relwithdebinfo')]
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Protocol-3 owner schema4/public schema2 tag aggregates with frozen registry identity, visibility/generation preparation, native tag activation requirements and confirmed-tag owner pending replay',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['effect-assets.json','owned-effects.json','royal-collision.json','ability-observer.json','royal-combat.json'],limitations=['Activation requirements and interval tag bindings freeze through Ability/CombatKit source closures; reflected authoring and Royal tag gameplay remain open.','Effect instances/lifetimes/credit/cues are not yet replicated; tag aggregates describe current authoritative state. Prediction holds confirmed tags until a newer bundle and does not execute expiry/periodic damage.','Server-only tag requirements or server-only descendants are explicitly unsupported by the owner prediction subset.','Reservations, authored combo/projectile, graph/socket/authoring/Luau and full clock/fault/streaming/performance remain required. M4/M5 stay In progress; M6 not started.'])
(EVIDENCE/'ability-tags.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Owner/public tag preparation, native requirements/prediction and full product/Headless checks passed')
