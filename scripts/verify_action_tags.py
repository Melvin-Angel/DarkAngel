"""Typed action-owned tags and reversible owner correction provenance."""
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
run('action-tags-product',['python','scripts/verify_effect_budget.py',*(['--build'] if args.build else [])])
if args.build:
 env=activate()
 for profile in ('m5-editor-relwithdebinfo','m1-relwithdebinfo'):run('action-tags-build-'+profile,[CMAKE,'--build',ROOT/'build'/profile,'--target','ActionTagTests','--parallel','2'],env)
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m1-relwithdebinfo','headless')]:run('action-tags-'+label,[ROOT/'build'/profile/'ActionTagTests.exe'])
sources=['CMakeLists.txt','engine/runtime/ability.cpp','engine/runtime/ability_state.hpp','engine/runtime/ability_wire.cpp','engine/runtime/ability_prediction.cpp','engine/runtime/tags.cpp','engine/runtime/effects.cpp','engine/runtime/include/darkangel/ability.hpp','engine/runtime/include/darkangel/tags.hpp','engine/runtime/include/darkangel/effects.hpp','engine/assets/ability_cooker.cpp','tests/action_tag_tests.cpp','tests/melee_tests.cpp','tests/ability_tag_tests.cpp','tests/ability_asset_tests.cpp','scripts/verify_action_tags.py']
binaries=['build/'+p+'/ActionTagTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m1-relwithdebinfo')]
sources.append('engine/runtime/world_session.cpp')
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Frozen .daability typed interval-tag bindings, action-owned contributor cleanup, owner schema4 provenance and reversible pending replay preserving external overlap',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['effect-budget.json','tagged-ability-assets.json','ability-tags.json','royal-combat.json'],limitations=['Action tags describe the active half-open interval clock; crossed hit/root intervals retain their existing separate bounded semantics.','Owner prediction reverses action contributions but holds confirmed external tags and attributes until correction; effect instances/timers/credit and effect-dependent attribute recomputation are not yet predicted.','Server-only action bindings are rejected by the current owner prediction subset. Public observers restore only current public presence and never execute historical damage/cues.','Primary Royal still uses its original light attack; authored dodge/status/motor/combo/projectile, persistent cues, reservations, reflected/Luau/graph/socket and full clock/fault/streaming/performance gates remain open. M4/M5 stay In progress; M6 not started.'])
(EVIDENCE/'action-tags.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Action-owned tags, reversible owner provenance, native/editor/Headless and complete product compatibility passed')
