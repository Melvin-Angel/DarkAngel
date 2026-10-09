"""Native AbilityState-owned effect/tag foundation and product regression gates."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None,timeout=2400):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
run('owned-effects-product',['python','scripts/verify_royal_collision.py',*(['--build'] if args.build else [])])
if args.build:
 env=activate()
 for profile in ('m5-editor-relwithdebinfo','m1-relwithdebinfo'):
  run('owned-effects-build-'+profile,[CMAKE,'--build',ROOT/'build'/profile,'--target','EffectTests','--parallel','2'],env)
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m1-relwithdebinfo','headless')]:run('owned-effects-'+label,[ROOT/'build'/profile/'EffectTests.exe'])
sources=['CMakeLists.txt','engine/runtime/tags.cpp','engine/runtime/effects.cpp','engine/runtime/ability_effects.cpp','engine/runtime/ability.cpp','engine/runtime/ability_state.hpp','engine/runtime/world_session.cpp','engine/runtime/attributes.cpp',*['engine/runtime/include/darkangel/'+n for n in ('tags.hpp','effects.hpp','attributes.hpp','world_session.hpp','melee.hpp')],'tests/effect_tests.cpp','scripts/verify_owned_effects.py']
binaries=['build/'+p+'/EffectTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m1-relwithdebinfo')]
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native frozen dictionary and per-token tags, AbilityState-owned timed effects, native game evaluators, Burn/slow/invulnerability/stagger/equipment, source/cleanse/death cleanup, captured credit and atomic preparation',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['royal-collision.json','ability-observer.json','royal-combat.json'],limitations=['Effects and dictionaries use checked native construction only; versioned source/cook/CAS/editor authoring and Luau composition remain open.','Effects/tags/remaining lifetime/credit/cues are not on owner or observer wire; current attribute/Health outcomes use existing correction/baseline visibility. Prediction does not replay effect expiry or periodic execution.','Slow validates native statistic composition; character motor and authored dodge/combo/status presentation are not yet driven by these effects.','Native stagger interrupts active ownership but ongoing ability requirements/reservations and authored cancel/combo gates remain open.','Full clock/fault/streaming/performance and M4/M5 gates remain open; M6 not started.'])
(EVIDENCE/'owned-effects.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Native owned effects/tags, full product/regression and Headless checks passed')
