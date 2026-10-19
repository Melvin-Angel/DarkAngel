"""Import catalogue deduplication preserves authored native identities."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
import import_unity_animations as importer


class ImportCatalogueTests(unittest.TestCase):
    def fixture(self,root):
        content=root/'content';work=root/'work'
        item=dict(path='Assets/thirdparty/Animations/Opsive/Walk.fbx',name='Walk',length=1.,loop=True)
        old=dict(item,existing=True,output='royal_district/clips/omni-walk.glb')
        new=dict(item,existing=False,output='animation/unity/opsive/walk-copy.glb')
        target=content/new['output'];target.parent.mkdir(parents=True)
        target.write_bytes(b'owned imported clip')
        importer.write(Path(str(target)+'.daimport'),dict(id='new-id'))
        importer.write(target.with_suffix('.conversion.json'),dict(schema=2))
        existing=content/old['output'];existing.parent.mkdir(parents=True);existing.write_bytes(b'authored existing clip')
        importer.write(work/'plan.json',dict(clips=[old,new],skipped=[],skipped_counts={}))
        importer.write(content/'animation/unity/clips.json',dict(clips=[
            dict(path=old['output'],id='old-id'),
            dict(path=new['output'],id='new-id',glb_sha256=hashlib.sha256(target.read_bytes()).hexdigest())]))
        return content,SimpleNamespace(work=work),target,existing

    def test_duplicate_copy_removed_and_authored_identity_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            content,args,target,existing=self.fixture(Path(directory))
            with patch.object(importer,'CONTENT',content):importer.deduplicate_existing(args)
            self.assertFalse(target.exists())
            self.assertEqual(existing.read_bytes(),b'authored existing clip')
            manifest=json.loads((content/'animation/unity/clips.json').read_text())
            self.assertEqual([c['id'] for c in manifest['clips']],['old-id'])
            self.assertEqual(len(manifest['skipped']),1)

    def test_edited_duplicate_is_preserved_and_catalogue_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            content,args,target,existing=self.fixture(Path(directory))
            catalogue=content/'animation/unity/clips.json';before=catalogue.read_bytes()
            target.write_bytes(b'external edit')
            with patch.object(importer,'CONTENT',content):
                with self.assertRaisesRegex(RuntimeError,'edited after import'):importer.deduplicate_existing(args)
            self.assertEqual(target.read_bytes(),b'external edit')
            self.assertEqual(catalogue.read_bytes(),before)
            self.assertTrue(existing.exists())

if __name__=='__main__':unittest.main()
