"""Move unambiguous legacy hit profiles into their owning action blocks.

Dry-run by default. Shared actions with differing profiles are reported and left
untouched. No projectile preparation or kit effect data is removed.
"""
import argparse
import json
import os
import tempfile
from pathlib import Path


def migrate(root, selected, apply=False):
    baselines = {}
    def load(path):
        raw = path.read_bytes()
        baselines.setdefault(str(path), raw)
        return json.loads(raw.decode('utf-8'))
    abilities = [(p, load(p)) for p in sorted(root.rglob('*.daability'))]
    claims = {}
    for path, ability in abilities:
        slots = [(ability['sources']['action'], ability)] + [(step['source'], step) for step in ability.get('combo_steps', [])]
        for action, slot in slots:
            for profile in slot.get('melee', []):
                claims.setdefault((action, profile['block']), []).append((path, profile))
    changes, warnings, actions = {}, [], {}
    for path, ability in abilities:
        if selected and path not in selected:
            continue
        slots = [(ability['sources']['action'], ability)] + [(step['source'], step) for step in ability.get('combo_steps', [])]
        for relative, slot in slots:
            remaining = []
            for profile in slot.get('melee', []):
                key = (relative, profile['block'])
                canonical = {k: v for k, v in profile.items() if k != 'block'}
                canonical.setdefault('socket', '')
                variants = []
                for _, claim in claims[key]:
                    value = {k: v for k, v in claim.items() if k != 'block'}
                    value.setdefault('socket', '')
                    variants.append(value)
                if any(value != canonical for value in variants) or (selected and any(owner not in selected for owner, _ in claims[key])):
                    warnings.append(f'{path}: shared action {relative}, block {profile["block"]}: migrate all identical consumers together or make independent composers')
                    remaining.append(profile)
                    continue
                action_path = root / relative
                action = actions.setdefault(action_path, load(action_path))
                block = next((b for b in action['blocks'] if b['id'] == profile['block'] and b['kind'] == 'hit'), None)
                payload = {'schema': 1, 'shape': 'sphere', **canonical}
                if block is None or ('hit' in block and block['hit'] != payload):
                    warnings.append(f'{path}: missing or conflicting hit block {profile["block"]}')
                    remaining.append(profile)
                    continue
                block['hit'] = payload
                changes[str(action_path)] = action
            slot['melee'] = remaining
        changes[str(path)] = ability
    # A single base release can move its complete projectile definition without
    # changing targeting permission or its preparation/release windows.
    for path, ability in abilities:
        if selected and path not in selected:
            continue
        shot = ability.get('targeting', {}).get('projectile')
        if shot is None:
            continue
        if ability.get('melee'):
            warnings.append(f'{path}: migrate all base hit profiles before moving projectile motion')
            continue
        relative = ability['sources']['action']
        shared = [other for other, value in abilities if other != path and (value['sources']['action'] == relative or any(s['source'] == relative for s in value.get('combo_steps', [])))]
        action_path = root / relative
        action = actions.setdefault(action_path, load(action_path))
        hits = [b for b in action['blocks'] if 'hit' in b]
        release = shot.get('preparation', {}).get('release_block')
        candidates = [b for b in hits if release is None or b['id'] == release]
        if shared or len(candidates) != 1 or len(hits) != 1:
            warnings.append(f'{path}: ambiguous/shared projectile release; legacy motion preserved')
            continue
        block = candidates[0]
        payload = {'schema': 1, 'range': ability['targeting']['range'], **shot}
        if 'visual_model' in payload:
            payload['model_source'] = ability['sources']['projectile_model']
        if 'projectile' in block and block['projectile'] != payload:
            warnings.append(f'{path}: conflicting composer projectile; legacy motion preserved')
            continue
        block['kind'] = 'projectile'
        block['projectile'] = payload
        del ability['targeting']['projectile']
        ability['sources'].pop('projectile_model', None)
        changes[str(path)] = ability
        changes[str(action_path)] = action
    # Migrate kit bindings only when every kit using the selected ability agrees.
    # The old binding addresses a block in every combo action with that identity.
    kits = [(p, load(p)) for p in sorted(root.rglob('*.dakit'))]
    for path, ability in abilities:
        if selected and path not in selected:
            continue
        identity = ability['asset']
        users = [(p, k) for p, k in kits if any(s.get('ability') == identity for s in k.get('slots', [])) or k.get('dodge', {}).get('ability') == identity]
        bindings = [b for _, kit in users for b in kit.get('effect_bindings', []) if b['ability'] == identity]
        for block_id in sorted({b['hit_block'] for b in bindings}):
            slot_data = [ability] + ability.get('combo_steps', [])
            if any(any(p['block'] == block_id for p in slot.get('melee', [])) for slot in slot_data):
                warnings.append(f'{path}: block {block_id} still has legacy combo profiles; kit effects preserved')
                continue
            variants = []
            for _, kit in users:
                variants.append(sorted([(b['effect'], b['power']) for b in kit.get('effect_bindings', []) if b['ability'] == identity and b['hit_block'] == block_id]))
            if any(v != variants[0] for v in variants):
                warnings.append(f'{path}: kits disagree on effects for block {block_id}; explicit independent composers required')
                continue
            slots = [ability['sources']['action']] + [s['source'] for s in ability.get('combo_steps', [])]
            targets = []
            for relative in slots:
                action_path = root / relative
                action = actions.setdefault(action_path, load(action_path))
                block = next((b for b in action['blocks'] if b['id'] == block_id and 'hit' in b), None)
                if block is not None:
                    targets.append((action_path, action, block))
            if not targets:
                warnings.append(f'{path}: effect block {block_id} needs a migrated hit payload')
                continue
            target_paths = {p.relative_to(root).as_posix() for p, _, _ in targets}
            if any(other != path and (value['sources']['action'] in target_paths or any(s['source'] in target_paths for s in value.get('combo_steps', []))) for other, value in abilities):
                warnings.append(f'{path}: shared composer effects require explicit independent ownership')
                continue
            entries = []
            for effect, power in variants[0]:
                locators = {kit.get('sources', {}).get('effects', {}).get(effect) for _, kit in users}
                if len(locators) != 1 or None in locators:
                    warnings.append(f'{path}: ambiguous source for effect {effect}')
                    break
                entries.append({'asset': effect, 'power': power, 'source': next(iter(locators))})
            else:
                payload = {'schema': 1, 'entries': entries}
                if any('effects' in block and block['effects'] != payload for _, _, block in targets):
                    warnings.append(f'{path}: conflicting composer effects at block {block_id}')
                    continue
                for action_path, action, block in targets:
                    block['effects'] = payload
                    changes[str(action_path)] = action
                for kit_path, kit in users:
                    kit['effect_bindings'] = [b for b in kit.get('effect_bindings', []) if not (b['ability'] == identity and b['hit_block'] == block_id)]
                    changes[str(kit_path)] = kit
    if apply:
        staged, published = {}, []
        try:
            for path, value in changes.items():
                if Path(path).read_bytes() != baselines[path]:
                    raise RuntimeError(f'Source changed during migration: {path}')
                descriptor, temporary = tempfile.mkstemp(prefix='.composer-migrate-', dir=Path(path).parent)
                with os.fdopen(descriptor, 'wb') as output:
                    output.write((json.dumps(value, indent=2) + '\n').encode('utf-8'))
                staged[path] = temporary
            for path, temporary in staged.items():
                if Path(path).read_bytes() != baselines[path]:
                    raise RuntimeError(f'Source changed before publication: {path}')
                os.replace(temporary, path)
                published.append(path)
        except BaseException:
            for path in reversed(published):
                Path(path).write_bytes(baselines[path])
            raise
        finally:
            for temporary in staged.values():
                Path(temporary).unlink(missing_ok=True)
    typed = []
    for path, ability in abilities:
        if selected and path not in selected:
            continue
        for relative in [ability['sources']['action']] + [s['source'] for s in ability.get('combo_steps', [])]:
            action = actions.get(root / relative) or load(root / relative)
            typed.append({'composer': relative, 'hit_blocks': sum('hit' in b for b in action['blocks']),
                          'effect_references': sum(len(b.get('effects', {}).get('entries', [])) for b in action['blocks']),
                          'projectiles': sum('projectile' in b for b in action['blocks'])})
    return {'applied': apply, 'files': sorted(changes), 'warnings': warnings,
            'typed_composers': typed,
            'preserved': 'Ability permissions, combo identity, preparation and effect behavior preserved. Unambiguous profiles, kit effects and projectile motion/model/preparation moved to typed blocks.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('content'))
    parser.add_argument('--ability', action='append', default=[])
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = migrate(args.root, {args.root / name for name in args.ability}, args.apply)
    text = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.write_text(text, encoding='utf-8')
    print(text)
