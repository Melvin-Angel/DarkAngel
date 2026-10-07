"""Inactive evaluation wheels only; no install, execution, server or global changes."""
import concurrent.futures, json, pathlib
from acquire import ROOT, download

def main():
    records=json.loads((ROOT/'cmake/candidate-python.lock.json').read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        rows=list(pool.map(download,records['archives']))
    (ROOT/'.cache/acquisition-candidate-python.json').write_text(json.dumps(rows,indent=2)+'\n')
    print(f'{len(rows)} inactive evaluation wheels acquired and checked; not installed')
    return 0

if __name__=='__main__':raise SystemExit(main())
