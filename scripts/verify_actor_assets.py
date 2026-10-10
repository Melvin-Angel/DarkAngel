"""Focused typed Character/Player assets and shared AssetService boundary checks."""
import hashlib,json,subprocess,time
from pathlib import Path
from msvc_environment import activate
ROOT=Path(__file__).resolve().parents[1];BUILD=ROOT/'build/m5-editor-relwithdebinfo';E=ROOT/'docs/implementation/evidence';gates=[]
def run(name,cmd,env=None):
 start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900)
 (E/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');gates.append(dict(name=name,exit=r.returncode,seconds=round(time.monotonic()-start,3)))
 if r.returncode:raise RuntimeError((r.stdout+r.stderr)[-3000:])
 print(name+' passed',flush=True)
run('actor-assets-build',[ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build',BUILD,'--target','ActorAssetTests','AssetTests','NativeSourceEditTests','--parallel','2'],activate())
run('actor-assets-native',[BUILD/'ActorAssetTests.exe',BUILD/'authoring-fixture.json'])
for name in ['AssetTests','NativeSourceEditTests']:run('actor-assets-'+name,[BUILD/(name+'.exe')])
files=['engine/assets/actor_cooker.cpp','engine/assets/include/darkangel/actor_assets.hpp','engine/assets/asset_service.cpp','engine/assets/assets_internal.hpp','tests/actor_asset_tests.cpp','CMakeLists.txt','scripts/verify_actor_assets.py']
(E/'actor-assets.json').write_text(json.dumps(dict(schema=1,base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Typed presentation Character and common Player kit references through native frozen asset closure',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files},limitations=['Native asset slice only; workflow creation, actor instantiation and authored Player fresh Play integration are next.','Initial Player loadout references existing kit; independent attribute/input/movement/camera/mask/equipment configuration remains planned.','Canonical renderable human skin profile only; NPC schemas and complete M4/M5 qualification remain open.']),indent=2)+'\n',encoding='utf-8')
