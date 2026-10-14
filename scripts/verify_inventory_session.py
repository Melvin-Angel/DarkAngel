"""Native and two-process GNS inventory adapter gates; no graphical claims."""
import json
import hashlib
import pathlib
import socket
import subprocess
import time

from msvc_environment import activate

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/m5-editor-relwithdebinfo'
EVIDENCE = ROOT / 'docs/implementation/evidence'
ENV = activate()
gates = []
for name, arguments in [('native', ['InventorySessionTests.exe']),
                        ('transactions', ['InventoryTests.exe']),
                        ('session-authority', ['SessionTests.exe', 'authority'])]:
    start = time.monotonic()
    result = subprocess.run([str(BUILD / arguments[0]), *arguments[1:]], cwd=ROOT,
                            env=ENV, capture_output=True, text=True, encoding='utf-8',
                            errors='replace', timeout=30)
    (EVIDENCE / ('m8-inventory-' + name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    gates.append(dict(name=name, exit=result.returncode, seconds=round(time.monotonic() - start, 3)))
    if result.returncode:
        raise RuntimeError(name + ': ' + result.stdout + result.stderr)
    print(name + ' passed', flush=True)

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
    probe.bind(('127.0.0.1', 0))
    endpoint = '127.0.0.1:' + str(probe.getsockname()[1])
start = time.monotonic()
with (EVIDENCE / 'm8-inventory-gns-host.log').open('w', encoding='utf-8') as h, \
     (EVIDENCE / 'm8-inventory-gns-client.log').open('w', encoding='utf-8') as c:
    host = subprocess.Popen([str(BUILD / 'InventoryGnsTests.exe'), '--host', endpoint], cwd=ROOT, env=ENV, stdout=h, stderr=subprocess.STDOUT)
    client = None
    try:
        time.sleep(.5)
        client = subprocess.Popen([str(BUILD / 'InventoryGnsTests.exe'), '--client', endpoint], cwd=ROOT, env=ENV, stdout=c, stderr=subprocess.STDOUT)
        client_exit = client.wait(timeout=25)
        host_exit = host.wait(timeout=5)
    finally:
        for process in [client, host]:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
host_log = (EVIDENCE / 'm8-inventory-gns-host.log').read_text(encoding='utf-8')
client_log = (EVIDENCE / 'm8-inventory-gns-client.log').read_text(encoding='utf-8')
gates.append(dict(name='gns-two-process', host_exit=host_exit, client_exit=client_exit,
                  seconds=round(time.monotonic() - start, 3)))
if host_exit or client_exit or 'Inventory GNS host passed:' not in host_log or 'Inventory GNS client passed:' not in client_log:
    raise RuntimeError('GNS verification failed: ' + host_log + client_log)
sources = ['CMakeLists.txt', 'apps/editor/main.cpp', 'engine/runtime/world_session.cpp',
           'engine/runtime/include/darkangel/world_session.hpp',
           'engine/runtime/owner_mailbox.hpp', 'engine/runtime/owner_mailbox.cpp',
           'engine/runtime/include/darkangel/party_inventory.hpp', 'engine/runtime/party_inventory.cpp',
           'engine/runtime/include/darkangel/inventory_session.hpp', 'engine/runtime/inventory_session.cpp',
           'tests/inventory_session_tests.cpp', 'tests/inventory_gns_tests.cpp',
           'scripts/verify_inventory_session.py']
hashes = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources}
(EVIDENCE / 'm8-inventory-session.json').write_text(json.dumps(dict(gates=gates, source_hashes=hashes,
    limits=['Native store/gameplay adapter; editor party and SceneAuthority wiring pending.',
            'Mask slot ownership only; gameplay loadout publication pending.',
            'Same-machine UDP GNS only, no two-machine/EOS or latency/loss qualification.',
            'No runtime inventory document, typed use effects, pickups or durable slot writes.']), indent=2) + '\n', encoding='utf-8')
print('All four inventory adapter gates passed', flush=True)
