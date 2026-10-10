"""Focused coordinated native creation/publication boundary checks."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3000:])
 print(name+' passed',flush=True)
run('native-creation-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','NativeSourceEditTests','--parallel','2'],activate())
run('native-creation-tests',[BUILD/'NativeSourceEditTests.exe'])
files=['engine/assets/asset_service.cpp','engine/assets/include/darkangel/assets.hpp','tests/native_source_edit_tests.cpp','CMakeLists.txt','scripts/verify_native_creation.py']
(E/'native-creation.json').write_text(json.dumps(dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Coordinated new-source private preparation, exclusive creation, mixed catalog/source rollback/retry and frozen generation preservation',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},limitations=['Native AssetService preparation/publication fixture only; editor pending-creation/history integration follows.','Source/CAS/catalog coordination remains non-crash-atomic and is not a cross-process multi-file snapshot; journals require manual recovery.','No renderer, physical editor interaction or broader M4/M5/multiplayer qualification claim.']),indent=2)+'\n',encoding='utf-8')
