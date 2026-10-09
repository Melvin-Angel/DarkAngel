"""Frozen native EffectDefinition/TagRegistry through existing AssetService/CAS."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None,timeout=3000):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
run('effect-assets-product',['python','scripts/verify_owned_effects.py',*(['--build'] if args.build else [])])
if args.build:
 env=activate()
 for profile in ('m5-editor-relwithdebinfo','m2-relwithdebinfo'):
  run('effect-assets-build-'+profile,[CMAKE,'--build',ROOT/'build'/profile,'--target','EffectAssetTests','--parallel','2'],env)
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m2-relwithdebinfo','no-animation')]:run('effect-assets-'+label,[ROOT/'build'/profile/'EffectAssetTests.exe'])
sources=['CMakeLists.txt','engine/assets/effect_cooker.cpp','engine/assets/include/darkangel/effect_assets.hpp','engine/assets/ability_cooker.cpp','engine/assets/include/darkangel/ability_assets.hpp','engine/assets/assets_internal.hpp','engine/assets/asset_service.cpp','engine/runtime/effects.cpp','engine/runtime/include/darkangel/effects.hpp','engine/runtime/tags.cpp','engine/runtime/ability_effects.cpp','tests/effect_asset_tests.cpp','tests/effect_tests.cpp','scripts/verify_effect_assets.py']
binaries=['build/'+p+'/EffectAssetTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m2-relwithdebinfo')]
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native .daeffect/.datags schema validation, frozen attribute/tag closure, registered source/cook/CAS metadata, stable persistent tag keys, mixed-generation rejection and native effect authority',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['owned-effects.json','royal-collision.json','ability-observer.json','royal-combat.json'],limitations=['Reflected Effect/Tag authoring, generated C++/Luau constants and checked Luau composition remain open.','Native source/CAS construction does not imply Royal application/presentation or motor-speed integration; native sample policies are covered by fixtures.','Effect/tag/lifetime/credit/cue replication and effect-aware prediction remain open.','Reservations, authored combo/projectile, typed graph/socket/authoring and full clock/fault/streaming/performance gates remain required. M4/M5 stay In progress; M6 not started.'])
(EVIDENCE/'effect-assets.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Frozen native effect/tag source/CAS, source-free authority and product compatibility checks passed')
