"""Inventory, export, retarget and cook Unity Humanoid clips through native assets.

Run: python scripts/import_unity_animations.py all --project "C:/Unity Projects/AshenRootsMP"
Individual stages (inventory, plan, export, import) resume from .cache/animation-reimport.
Unity originals stay read-only; only a temporary Editor exporter is installed.
Existing clip UUIDs/loop flags and authored playback durations are preserved.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import uuid

from convert_unity_humanoid_clip import convert, parse_glb

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / 'content'
SKELETON = CONTENT / 'animation/canonical_human.daskeleton'
TOOL = ROOT / 'build/m5-editor-relwithdebinfo/AssetTool.exe'
NAMESPACE = uuid.UUID('e651b0cb-d16e-473d-9dc7-1917fac7d0de')


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def slug(value):
    return re.sub(r'[^a-z0-9]+', '-', value.lower()).strip('-')[:100] or 'clip'


def source_relative(source, project):
    source = source.replace('\\', '/')
    if '/Assets/' in source:
        return 'Assets/' + source.split('/Assets/', 1)[1]
    return source


def run_unity(args, method, extra):
    installed = args.project / 'Assets/Editor/DarkAngelAnimationExport_Temporary.cs'
    if installed.exists():
        raise RuntimeError(f'Temporary exporter already exists; inspect it before retrying: {installed}')
    shutil.copyfile(ROOT/'scripts/unity/DarkAngelAnimationExport.cs', installed)
    try:
        command = [args.unity, 'run', str(args.project), '--timeout', '3600', '--',
                   '-executeMethod', 'DarkAngelAnimationExport.' + method, '-nographics',
                   '-logFile', str(args.work/(method + '.log')), *extra]
        print('Unity batch:', method, flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
    finally:
        # These exact paths are owned temporary tooling, never source assets.
        installed.unlink(missing_ok=True)
        Path(str(installed) + '.meta').unlink(missing_ok=True)


def make_plan(args):
    inventory = json.loads(args.inventory.read_text())
    clips = inventory['clips']
    by_source = defaultdict(list)
    for clip in clips:
        if clip['humanoid'] and not re.search(r't[-_ ]?pose|bindpose|restpose', clip['name'], re.I):
            by_source[clip['path'].lower()].append(clip)
    existing = {}
    for report in CONTENT.rglob('*.conversion.json'):
        target = report.with_name(report.name.replace('.conversion.json', '.glb'))
        if not target.exists() or not Path(str(target)+'.daimport').exists():
            continue
        data = json.loads(report.read_text())
        if not data.get('source'):
            continue
        matches = by_source[source_relative(data['source'], args.project).lower()]
        if len(matches) > 1:
            action = data.get('source_clip') or data.get('action') or ''
            exact = [c for c in matches if c['name'] == action or c['name'] in action]
            matches = exact if len(exact) == 1 else matches
        if len(matches) == 1:
            existing[target] = matches[0]
    # The original Earth import had no provenance sidecar. Resolve its exact
    # clip subassets through the Unity composer references rather than names.
    for composer, target in [
        ('Earth/AC_H2H_PowerPunch01_Forward.asset', 'earth_overhand_punch'),
        ('Earth/AC_H2H_LeftCutKick_Forward.asset', 'earth_spinning_kick'),
        ('EarthSpear_Composer.asset', 'earth_spear_throw')]:
        text = (args.project/'Assets/Game/Combat/AnimationComposers'/composer).read_text()
        match = re.search(r'AnimationClip: \{fileID: (-?\d+), guid: ([0-9a-f]+)', text)
        local, guid = int(match[1]), match[2]
        clip = next(c for c in clips if c['guid'] == guid and c['localId'] == local)
        existing[CONTENT/'animation/earth'/f'{target}.glb'] = clip
    records, excluded, identities, variants, source_hashes = [], [], set(), set(), {}

    def variant_key(clip):
        parts=clip['path'].split('/')
        pack=slug(parts[3] if len(parts)>3 and parts[1]=='thirdparty' else 'game')
        return (pack,clip['name'].lower(),round(clip['length'],4),clip['loop'])

    def add(clip, target, present):
        identity = f"{clip['guid']}:{clip['localId']}"
        if present:
            document, _ = parse_glb(target)
            ticks = document['accessors'][document['animations'][0]['samplers'][0]['input']]['count']-1
        else:
            ticks = 0
        key = hashlib.sha256((identity + str(target)).encode()).hexdigest()[:20]
        record = dict(clip, output=target.relative_to(CONTENT).as_posix(), existing=present,
                      outputTicks=ticks, sample=str(args.work/'samples'/(key+'.json')))
        if clip['path'] not in source_hashes:
            source_hashes[clip['path']] = hashlib.sha256((args.project/clip['path']).read_bytes()).hexdigest()
        record['source_sha256'] = source_hashes[clip['path']]
        records.append(record)
        identities.add(identity)
        variants.add(variant_key(clip))

    for target, clip in sorted(existing.items()):
        add(clip, target, True)
    for clip in sorted(clips, key=lambda c: ('/TPose/' not in c['path'], c['path'], c['localId'])):
        reason = None
        if f"{clip['guid']}:{clip['localId']}" in identities:
            continue
        if not clip['humanoid']:
            reason = 'not Humanoid (prop/UI/generic rig); no safe Avatar mapping'
        elif re.search(r't[-_ ]?pose|bindpose|restpose', clip['name'], re.I):
            reason = 'bind/reference pose'
        elif clip['length'] < 1/60 or clip['length'] > 10:
            reason = 'outside native clip duration bound (1/60 to 10 seconds)'
        parts = clip['path'].split('/')
        pack = slug(parts[3] if len(parts)>3 and parts[1]=='thirdparty' else 'game')
        variant = variant_key(clip)
        if not reason and variant in variants:
            reason = 'same named/duration take already selected from this pack'
        if reason:
            excluded.append(dict(path=clip['path'], name=clip['name'], reason=reason))
            continue
        variants.add(variant)
        suffix = hashlib.sha256(f"{clip['guid']}:{clip['localId']}".encode()).hexdigest()[:10]
        target = CONTENT/'animation/unity'/pack/(slug(clip['name'])+'-'+suffix+'.glb')
        add(clip, target, False)
    plan = dict(schema=1, unity_project=str(args.project), inventory_count=len(clips), clips=records,
                skipped=excluded, skipped_counts=dict(Counter(c['reason'] for c in excluded)))
    write(args.work/'plan.json', plan)
    write(args.work/'selection.json', dict(clips=[dict(guid=c['guid'], localId=c['localId'],
        output=c['sample'], sampleRate=60, playRate=1, outputTicks=c['outputTicks']) for c in records]))
    print(f"Selected {len(records)} clips: {sum(c['existing'] for c in records)} reimports, {sum(not c['existing'] for c in records)} new", flush=True)
    print(json.dumps(plan['skipped_counts']), flush=True)


def import_clips(args):
    plan = json.loads((args.work/'plan.json').read_text())
    records, failures = [], []
    for index, item in enumerate(plan['clips']):
        target = CONTENT/item['output']
        sidecar = Path(str(target)+'.daimport')
        conversion = target.with_suffix('.conversion.json')
        originals = {p: p.read_bytes() if p.exists() else None for p in (target, sidecar, conversion)}
        for path, data in originals.items():
            if data is not None:
                backup = args.work/'originals'/path.relative_to(CONTENT)
                if not backup.exists():
                    backup.parent.mkdir(parents=True, exist_ok=True)
                    backup.write_bytes(data)
        try:
            if hashlib.sha256((args.project/item['path']).read_bytes()).hexdigest() != item['source_sha256']:
                raise RuntimeError('Unity original changed since inventory')
            report = convert(item['sample'], target, SKELETON, CONTENT/'animation/blink/punch-right.glb')
            report.update(source=str(args.project/item['path']), source_asset_path=item['path'],
                          source_sha256=item['source_sha256'], action=item['name'])
            write(conversion, report)
            if not sidecar.exists():
                identity = f"unity:{item['guid']}:{item['localId']}"
                stable_id=lambda key: str(uuid.UUID(bytes=uuid.uuid5(NAMESPACE,key).bytes,version=4))
                write(sidecar, dict(schema=1, id=stable_id(identity), importer='clip-gltf-v1',
                    canonical_source='animation/canonical_human.daskeleton', loop=item['loop'],
                    tags=[], subassets=dict(runtime=stable_id(identity+':runtime'))))
            result = subprocess.run([str(TOOL), 'cook', str(CONTENT), str(args.work/'cooked'), item['output']],
                                    cwd=ROOT, capture_output=True, text=True, timeout=180)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            records.append(dict(path=item['output'], id=json.loads(sidecar.read_text())['id'],
                source=item['path'], source_guid=item['guid'], source_local_id=item['localId'],
                clip=item['name'], ticks=report['ticks'], loop=json.loads(sidecar.read_text())['loop'],
                reimported=item['existing'], glb_sha256=report['glb_sha256']))
        except Exception as error:
            for path, data in originals.items():
                if data is None:
                    path.unlink(missing_ok=True)
                else:
                    temporary=path.with_suffix(path.suffix+'.tmp')
                    temporary.write_bytes(data);temporary.replace(path)
            failures.append(dict(path=item['output'], reason=str(error)))
        if (index+1)%20 == 0 or index+1 == len(plan['clips']):
            print(f"Converted/cooked {index+1}/{len(plan['clips'])}, failures={len(failures)}", flush=True)
            write(args.work/'import-progress.json', dict(clips=records, failed=failures))
    write(CONTENT/'animation/unity/clips.json', dict(schema=1, unity_project=str(args.project),
        retarget='Unity canonical Avatar; world rotations; canonical descendant bind translations; root motion retained',
        clips=records, failed=failures, skipped=plan['skipped'], skipped_counts=plan['skipped_counts']))
    print(f"Imported {len(records)} clips; {len(failures)} failures", flush=True)
    if failures:
        print(json.dumps(failures[:8], indent=2), flush=True)
        raise SystemExit(1)


def deduplicate_existing(args):
    """Remove unused copies of takes already represented by existing native IDs."""
    plan_path=args.work/'plan.json'
    catalogue=CONTENT/'animation/unity/clips.json'
    plan=json.loads(plan_path.read_text());manifest=json.loads(catalogue.read_text())
    records={c['path']:c for c in manifest['clips']}
    seen=set();duplicates=[]
    for item in plan['clips']:
        parts=item['path'].split('/')
        pack=slug(parts[3] if len(parts)>3 and parts[1]=='thirdparty' else 'game')
        key=(pack,item['name'].lower(),round(item['length'],4),item['loop'])
        if key in seen and not item['existing'] and item['output'] in records:
            duplicates.append(item)
        seen.add(key)
    owned=(CONTENT/'animation/unity').resolve()
    for item in duplicates:
        target=(CONTENT/item['output']).resolve();record=records[item['output']]
        if not target.is_relative_to(owned):raise RuntimeError('Duplicate outside owned animation import folder')
        if hashlib.sha256(target.read_bytes()).hexdigest()!=record['glb_sha256']:
            raise RuntimeError('Duplicate was edited after import; preserve it')
        if json.loads(Path(str(target)+'.daimport').read_text())['id']!=record['id']:
            raise RuntimeError('Duplicate identity changed after import; preserve it')
    removed={c['output'] for c in duplicates}
    for item in duplicates:
        target=CONTENT/item['output']
        for path in (target,Path(str(target)+'.daimport'),target.with_suffix('.conversion.json')):
            path.unlink()
        plan['skipped'].append(dict(path=item['path'],name=item['name'],reason='same named/duration take already selected from this pack'))
    plan['clips']=[c for c in plan['clips'] if c['output'] not in removed]
    plan['skipped_counts']=dict(Counter(c['reason'] for c in plan['skipped']))
    manifest['clips']=[c for c in manifest['clips'] if c['path'] not in removed]
    manifest['skipped']=plan['skipped'];manifest['skipped_counts']=plan['skipped_counts']
    write(plan_path,plan);write(catalogue,manifest)
    write(args.work/'deduplication.json',dict(removed=[c['output'] for c in duplicates]))
    print(f"Removed {len(duplicates)} duplicate copies; kept {len(manifest['clips'])} native clips")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['all', 'inventory', 'plan', 'export', 'import', 'deduplicate'])
    parser.add_argument('--project', type=Path, default=Path('C:/Unity Projects/AshenRootsMP'))
    parser.add_argument('--work', type=Path, default=ROOT/'.cache/animation-reimport')
    parser.add_argument('--inventory', type=Path)
    parser.add_argument('--unity', default='C:/Users/bengt/AppData/Local/Unity/bin/unity.exe')
    args = parser.parse_args()
    args.project, args.work = args.project.resolve(), args.work.resolve()
    args.inventory = args.inventory or args.work/'unity-inventory.json'
    args.work.mkdir(parents=True, exist_ok=True)
    if args.stage in ('all', 'inventory'):
        run_unity(args, 'InventoryClips', ['-darkAngelInventory', str(args.inventory)])
    if args.stage in ('all', 'plan'):
        make_plan(args)
    if args.stage in ('all', 'export'):
        run_unity(args, 'ExportClips', ['-darkAngelSelection', str(args.work/'selection.json'),
            '-darkAngelSkeleton', str(SKELETON), '-darkAngelReceipt', str(args.work/'unity-export-receipt.json')])
        receipt=json.loads((args.work/'unity-export-receipt.json').read_text())
        if receipt['failed']:
            raise RuntimeError(f"Unity failed to export {len(receipt['failed'])} clips; inspect the export receipt")
    if args.stage in ('all', 'import'):
        import_clips(args)
    if args.stage=='deduplicate':
        deduplicate_existing(args)


if __name__ == '__main__':
    main()
