"""Focused owned-source editing/rollback checks, without the historical chain."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];EVIDENCE=ROOT/'docs/implementation/evidence';CMAKE=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
parser=argparse.ArgumentParser();parser.add_argument('--build',action='store_true');args=parser.parse_args();gates=[]
def run(name,command,env=None):
 start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',env=env,timeout=600)
 (EVIDENCE/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8');gates.append(dict(name=name,exit=result.returncode,seconds=round(time.monotonic()-start,3)))
 if result.returncode:raise RuntimeError((result.stdout+result.stderr)[-4000:])
for profile,label in [('m5-relwithdebinfo','native'),('m2-relwithdebinfo','no-animation')]:
 if args.build:run('native-source-edit-build-'+label,[CMAKE,'--build',ROOT/'build'/profile,'--target','NativeSourceEditTests','--parallel','2'],activate())
 run('native-source-edit-'+label,[ROOT/'build'/profile/'NativeSourceEditTests.exe'])
sources=['CMakeLists.txt','engine/assets/asset_service.cpp','engine/assets/include/darkangel/assets.hpp','tests/native_source_edit_tests.cpp','scripts/verify_native_source_edits.py'];binaries=['build/'+p+'/NativeSourceEditTests.exe' for p in ('m5-relwithdebinfo','m2-relwithdebinfo')]
receipt=dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Owned native source edit command, optimistic source hash, preserved UUID/type/schema, atomic file replacement, failed cook rollback and immutable previous product lease',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources},binary_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in binaries},limitations=['Focused native/animation-disabled effect-source transaction checks only. Other supported native formats share the existing cook validators; the full historical integration/backend/provider chain is not rerun.','Single existing source only; no multi-file create/clone/kit transaction or undo history yet. A draft source is installed while synchronous cook runs and restored on failure; concurrent external edits detected during failure are preserved. This is not cross-process source/catalog atomicity.','The edited root is cooked; dependent kit/complete scene package rebuild, fresh CharacterPreviewResources and isolated Play remain required before new authored gameplay is visible.','Native Ability/Effect/Composer designer UI is still missing. M4/M5 remain In progress; M6 has not started.'])
(EVIDENCE/'native-source-edit.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8');print('Native source-edit focused native and animation-disabled checks passed')
