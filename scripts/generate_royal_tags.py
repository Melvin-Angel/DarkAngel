"""Build-time Royal constants through the native source/cook/package decoder."""
import argparse,json,subprocess
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--asset-tool',required=True);parser.add_argument('--destination',required=True);args=parser.parse_args()
root=Path(__file__).resolve().parents[1];output=Path(args.destination).resolve();cache=output/'cache';source=root/'content';relative='royal_district/combat/state.datags';identity=json.loads((source/relative).read_text())['asset']
def run(*arguments):
 result=subprocess.run([args.asset_tool,*map(str,arguments)],cwd=root,capture_output=True,text=True,encoding='utf8',errors='replace')
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
 return result.stdout.strip()
run('cook',source,cache,relative);run('package',source,cache,identity,output/'registry.json');export=Path(run('tag-constants',output/'registry.json',cache/'cas',identity,output/'exports'))
header=(export/'tags.hpp').read_bytes()+('\nnamespace ashen_roots { namespace royal_tags = darkangel::generated_tags::registry_'+identity.replace('-','')+'; }\n').encode('ascii')
def publish(path,bytes):
 path.parent.mkdir(parents=True,exist_ok=True)
 if not path.exists() or path.read_bytes()!=bytes:
  temporary=path.with_suffix(path.suffix+'.tmp');temporary.write_bytes(bytes);temporary.replace(path)
publish(output/'ashen_roots/royal_tags.hpp',header);publish(output/'royal_tags.luau',(export/'tags.luau').read_bytes())
print('Royal generated tag constants prepared:',export)
