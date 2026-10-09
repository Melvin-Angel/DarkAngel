"""Global periodic effect capacity and measured native-only workload."""
import argparse,hashlib,json,re,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[];measurements={}
def run(name,command,env=None,timeout=3600):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
 return result.stdout
run('effect-budget-product',['python','scripts/verify_tagged_ability_assets.py',*(['--build'] if args.build else [])])
if args.build:
 env=activate()
 for profile in ('m5-editor-relwithdebinfo','m1-relwithdebinfo'):run('effect-budget-build-'+profile,[CMAKE,'--build',ROOT/'build'/profile,'--target','EffectBudgetTests','--parallel','2'],env)
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m1-relwithdebinfo','headless')]:
 output=run('effect-budget-'+label,[ROOT/'build'/profile/'EffectBudgetTests.exe']);matches=dict(re.findall(r'(actors|active|ticks|outcomes|peak_due|mean_ms|p99_ms|max_ms|ordinary_cpp_new_calls)=([0-9.eE+-]+)',output));measurements[label]={key:float(value) if key.endswith('_ms') else int(value) for key,value in matches.items()}
sources=['CMakeLists.txt','engine/runtime/world_session.cpp','tests/effect_budget_tests.cpp','scripts/verify_effect_budget.py'];binaries=['build/'+p+'/EffectBudgetTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m1-relwithdebinfo')]
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Worst simultaneous periodic outcome reservation before effect publication, atomic rejection/removal/refresh and measured native 4-actor/128-effect workload',gates=gates,measurements=measurements,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['tagged-ability-assets.json','ability-tags.json','royal-combat.json'],limitations=['Timing measures native ability/effect advancement plus outcome drain only; no Jolt, graph/pose, renderer, networking or full frame workload.','Allocation counter measures ordinary C++ new/new[] calls only, excluding C/Flecs/aligned allocations and setup. Counts are evidence, not an allocation qualification.','Periodic reservation conservatively bounds the worst simultaneous active batch at 128 across all owners; it does not infer future schedule staggering.','Full M4/M5 timing/fault/streaming/bandwidth/allocation/performance and other remaining gates stay open; M6 not started.'])
(EVIDENCE/'effect-budget.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Global periodic capacity, measured native workload and full product compatibility checks passed')
