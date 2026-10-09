"""Run the native GNS active-effect host/client with bounded child lifetimes."""
import argparse,subprocess
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--binary',required=True);parser.add_argument('--output',required=True);parser.add_argument('--prefix',default='effect-gns');args=parser.parse_args();output=Path(args.output);output.mkdir(parents=True,exist_ok=True)
host=subprocess.Popen([args.binary,'--host'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8',errors='replace')
try:
 client=subprocess.run([args.binary,'--client'],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=30)
 (output/(args.prefix+'-client.log')).write_text(client.stdout+client.stderr,encoding='utf8')
 if client.returncode:raise RuntimeError(client.stdout+client.stderr)
 text=host.communicate(timeout=30)[0];(output/(args.prefix+'-host.log')).write_text(text,encoding='utf8')
 if host.returncode:raise RuntimeError(text)
 print(text.strip());print(client.stdout.strip())
finally:
 if host.poll() is None:host.terminate()
 text=host.communicate(timeout=5)[0];(output/(args.prefix+'-host.log')).write_text(text,encoding='utf8')
