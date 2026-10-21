import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('migration', Path(__file__).resolve().parents[1] / 'scripts/migrate_composer_hits.py')
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


class MigrationTests(unittest.TestCase):
    def write(self, root, name, value):
        (root / name).write_text(json.dumps(value), encoding='utf-8')

    def fixture(self, root):
        profile = dict(block=1, offset=[0, 1, .8], radius=.5, power=20, evaluator=1, damage_type=1)
        self.write(root, 'action.daaction', dict(asset='action', blocks=[dict(id=1, kind='hit')]))
        self.write(root, 'a.daability', dict(asset='a', sources=dict(action='action.daaction'), melee=[profile]))
        return profile

    def snapshot(self, root):
        return {p.name: p.read_bytes() for p in root.iterdir() if p.is_file()}

    def test_dry_run_and_idempotence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            before = self.snapshot(root)
            self.assertFalse(migration.migrate(root, set())['applied'])
            self.assertEqual(before, self.snapshot(root))
            report = migration.migrate(root, set(), True)
            self.assertFalse(report['warnings'])
            once = self.snapshot(root)
            migration.migrate(root, set(), True)
            self.assertEqual(once, self.snapshot(root))
            self.assertEqual(json.loads((root / 'action.daaction').read_text())['blocks'][0]['hit']['power'], 20)

    def test_partial_combo_migration_preserves_effects_and_projectile(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile = self.fixture(root)
            ability = json.loads((root / 'a.daability').read_text())
            ability['combo_steps'] = [dict(action='step', source='step.daaction', melee=[profile])]
            ability['targeting'] = dict(mode='aim', range=20, projectile=dict(speed=20, gravity=0, lifetime=60))
            self.write(root, 'a.daability', ability)
            self.write(root, 'step.daaction', dict(asset='step', blocks=[dict(id=1, kind='hit')]))
            self.write(root, 'b.daability', dict(asset='b', sources=dict(action='action.daaction'), melee=[profile]))
            binding = dict(ability='a', hit_block=1, effect='effect', power=1)
            self.write(root, 'kit.dakit', dict(slots=[dict(ability='a')], sources=dict(effects={'effect': 'effect.daeffect'}), effect_bindings=[binding]))
            report = migration.migrate(root, {root / 'a.daability'}, True)
            self.assertTrue(report['warnings'])
            changed = json.loads((root / 'a.daability').read_text())
            self.assertEqual(changed['targeting'], ability['targeting'])
            self.assertEqual(json.loads((root / 'kit.dakit').read_text())['effect_bindings'], [binding])
            self.assertNotIn('effects', json.loads((root / 'step.daaction').read_text())['blocks'][0])

    def test_failed_publication_restores_all_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            before = self.snapshot(root)
            original_replace = migration.os.replace
            calls = 0
            def replace(source, destination):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise OSError('injected publication failure')
                return original_replace(source, destination)
            with patch.object(migration.os, 'replace', replace):
                with self.assertRaises(OSError):
                    migration.migrate(root, set(), True)
            self.assertEqual(before, self.snapshot(root))


if __name__ == '__main__':
    unittest.main()
