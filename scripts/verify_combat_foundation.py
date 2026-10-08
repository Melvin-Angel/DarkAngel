"""Focused native combat primitive acceptance; no gameplay or network qualification."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'docs/implementation/evidence'
cmake=ROOT/'.tools/cmake/cmake-4.4.4-windows-x86_64/bin/ctest.exe'
gates=[]
for profile in ('m5-relwithdebinfo','m5-editor-relwithdebinfo'):
 result=subprocess.run([str(cmake),'--test-dir',str(ROOT/'build'/profile),'--output-on-failure','-R',r'^(M5.combat_kit|M5.attributes)$'],cwd=ROOT,capture_output=True,text=True)
 (EVIDENCE/('combat-foundation-'+profile+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
 gates.append(dict(profile=profile,exit=result.returncode))
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
inputs=('CMakeLists.txt','engine/runtime/combat_kit.cpp','engine/runtime/include/darkangel/combat_kit.hpp','engine/runtime/attributes.cpp','engine/runtime/include/darkangel/attributes.hpp','tests/combat_kit_tests.cpp','tests/attribute_tests.cpp')
receipt=dict(base_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),scope='Native kit routing/replacement and attribute primitives only; no gameplay integration',gates=gates,source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},editor_sha256=hashlib.sha256((ROOT/'build/m5-editor-relwithdebinfo/DarkAngelEditor.exe').read_bytes()).hexdigest())
(EVIDENCE/'combat-foundation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf8')
print('Combat foundation focused checks passed in native and full-editor profiles')
