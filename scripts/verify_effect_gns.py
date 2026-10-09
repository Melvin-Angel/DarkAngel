"""Existing GNS protocol3 active-status join and dense owner/public state."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None,timeout=5400):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=timeout)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-5000:])
 return result.stdout
run('effect-gns-product',['python','scripts/verify_tag_constants.py',*(['--build'] if args.build else [])])
if args.build:run('effect-gns-build-editor',[CMAKE,'--build',ROOT/'build/m5-editor-relwithdebinfo','--target','EffectGnsTests','--parallel','2'],activate())
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor')]:run('effect-gns-'+label,['python','scripts/run_effect_gns.py','--binary',ROOT/'build'/profile/'EffectGnsTests.exe','--output',EVIDENCE,'--prefix','effect-gns-'+label])
sources=['CMakeLists.txt','tests/effect_gns_tests.cpp','scripts/run_effect_gns.py','scripts/verify_effect_gns.py'];binaries=['build/'+p+'/EffectGnsTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo')]
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Real offline GNS protocol3 join to warm native effects: dense 63 owner/1 public/1 server-private filtering, timers, destroyed-source activation attribution, periodic/expiry Health and persistent cue lifetime',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},related_evidence=['tag-constants.json','effect-replication.json'],limitations=['The provider check uses two native processes over localhost UDP, one owner connection and synthetic stationary motor publication. It does not qualify graphical remote/Jolt play or four simultaneous remote owners.','No packet loss/latency/jitter matrix or live EOS service is implied. Complete fault/clock/streaming/lifecycle/bandwidth/allocation/frame-performance gates remain required.','Checked Luau gameplay composition, reflected/typed graph/socket authoring, authored utility dodge/status/motor/equipment/combo/projectile, persistent visual/audio/one-shot cue adapters and RmlUi bars remain open. M4/M5 remain In progress; M6 has not started.'])
(EVIDENCE/'effect-gns.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('GNS active effect join native/full-editor and complete product compatibility passed')
