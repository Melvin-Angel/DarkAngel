"""Capture the current native editor and exercise its real scoped MCP endpoint."""
import json, subprocess, time
from acquire import ROOT

def main():
    evidence=ROOT/'docs/implementation/evidence';rows=[]
    editor=ROOT/'build/m2-relwithdebinfo/DarkAngelEditor.exe'
    asset=json.loads((ROOT/'.cache/fixtures/horned-mask.glb.daimport').read_text())['id']
    def run(gate,command):
        start=time.monotonic();result=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace',timeout=60)
        log=evidence/f'm3-editor-{gate}.log';log.write_text(result.stdout+result.stderr,encoding='utf8')
        rows.append(dict(gate=gate,command=list(map(str,command)),exit_code=result.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
        (evidence/'m3-editor-captures.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8')
        if result.returncode:raise RuntimeError(f'Native editor {gate} failed; see {log}')
        print(gate,'passed',flush=True)
    for gate,backend,width,height in [('d3d12','d3d12',1280,800),('vulkan','vulkan',1280,800),('d3d12-narrow','d3d12',960,720)]:
        run(gate,[editor,'--registry',ROOT/'.cache/fixtures/mask.registry.json','--cas',ROOT/'.cache/assets-real/cas','--model',asset,'--backend',backend,'--width',width,'--height',height,'--frames','8','--hidden','--capture',evidence/f'm3-editor-{gate}.png']+(['--exercise'] if gate=='d3d12' else []))
    run('mcp',[ROOT/'.tools/node/node-v24.21.0-win-x64/node.exe',ROOT/'tools/agent-bridge/verify.mjs',editor,'--editor'])
    return 0

if __name__=='__main__':raise SystemExit(main())
