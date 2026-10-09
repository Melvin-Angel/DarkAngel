"""Native prepared tag code generation and Royal/strict Luau consumers."""
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
run('tag-constants-product',['python','scripts/verify_royal_heavy.py',*(['--build'] if args.build else [])])
if args.build:
 for profile in ('m5-editor-relwithdebinfo','m2-relwithdebinfo'):run('tag-constants-build-'+profile,[CMAKE,'--build',ROOT/'build'/profile,'--target','TagCodegenTests','AssetTool','--parallel','2'],activate())
for profile,label in [('m5-relwithdebinfo','native'),('m5-editor-relwithdebinfo','editor'),('m2-relwithdebinfo','no-animation')]:run('tag-constants-'+label,[ROOT/'build'/profile/'TagCodegenTests.exe'])
identity=json.loads((ROOT/'content/royal_district/combat/state.datags').read_text())['asset'];profile=ROOT/'build/m5-relwithdebinfo';directory=run('tag-constants-cli',[profile/'AssetTool.exe','tag-constants',profile/'generated/royal-tags/registry.json',profile/'generated/royal-tags/cache/cas',identity,profile/'tag-constants-cli']).strip();export=Path(directory)
sources=['CMakeLists.txt','engine/assets/tag_codegen.cpp','engine/assets/include/darkangel/tag_codegen.hpp','tools/cooker/main.cpp','scripts/generate_royal_tags.py','scripts/verify_tag_constants.py','tests/tag_codegen_tests.cpp','tests/royal_heavy_scene_tests.cpp','games/AshenRoots/royal_combat.cpp','content/royal_district/combat/state.datags']
binaries=['build/'+p+'/TagCodegenTests.exe' for p in ('m5-relwithdebinfo','m5-editor-relwithdebinfo','m2-relwithdebinfo')]+['build/m5-relwithdebinfo/AssetTool.exe','build/m5-editor-relwithdebinfo/DarkAngelEditor.exe']
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Existing prepared tag registry exports native TagId constants and strict/frozen Luau IDs, persistent keys and generation checks; atomic immutable publication and compiled Royal consumer',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},generated_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (export/'tags.hpp',export/'tags.luau')},related_evidence=['royal-heavy.json'],limitations=['Generated Luau TagId is a number alias plus an explicit registry/generation check; this does not provide a new native ability/effect scripting facade. Checked gameplay composition still remains.','Generated outputs are derived build artifacts. The .datags source and persistent UUID keys remain authoritative; changed registries require compatible rebuilt consumers.','Reflected designers, utility dodge/status/motor/combo/projectile, typed graph/socket authoring, cue visual/audio/one-shot adapters and full clock/fault/streaming/performance/RmlUi gates remain required. M4/M5 remain In progress; M6 has not started; live EOS is externally blocked.'])
(EVIDENCE/'tag-constants.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Generated tags native/full-editor/feature-disabled, strict Luau and full Royal compatibility passed')
