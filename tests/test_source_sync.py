"""Verify snapshot hashes, isolation of history, and rejected unsafe archives."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts/linux'))
import receive_source


def archive(files, *, wrong_hash=False):
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}
    digest = hashlib.sha256(''.join(f'{value}  {name}\n' for name, value in hashes.items()).encode()).hexdigest()
    manifest = {'snapshot': '20261111T120000Z-1234abcd', 'files': hashes,
                'source_manifest_sha256': 'wrong' if wrong_hash else digest,
                'git_commit': 'test-commit', 'git_dirty': True}
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode='w') as tar:
        for name, data in {**files, 'SOURCE.json': json.dumps(manifest).encode()}.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    stream.seek(0)
    return stream


@unittest.skipUnless(sys.platform.startswith('linux'), 'Linux receiver')
class SyncTests(unittest.TestCase):
    def test_verified_snapshot_preserves_history_and_records_build_origin(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / 'project'
            history = project / 'evidence' / 'old.bin'
            history.parent.mkdir(parents=True)
            history.write_bytes(b'historical evidence\x00\xff')
            legacy = project / 'scripts' / 'old.sh'
            legacy.parent.mkdir()
            legacy.write_bytes(b'legacy script')
            receipt = receive_source.receive(archive({'scripts/test.py': b'print(1)\n'}), project)
            self.assertEqual(receipt['files_verified'], 1)
            self.assertEqual(history.read_bytes(), b'historical evidence\x00\xff')
            self.assertEqual(legacy.read_bytes(), b'legacy script')
            source = Path(receipt['source_dir'])
            build = Path(receipt['build_dir'])
            self.assertEqual((source / 'SOURCE.json').read_bytes(), (build / 'SOURCE.json').read_bytes())
            self.assertEqual((project / 'current').resolve(), source)
            with self.assertRaises(FileExistsError):
                receive_source.receive(archive({'scripts/test.py': b'print(1)\n'}), project)

    def test_reject_hash_mismatch_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / 'project'
            with self.assertRaisesRegex(ValueError, 'SHA256'):
                receive_source.receive(archive({'README.md': b'hello'}, wrong_hash=True), project)
            self.assertFalse(project.exists())

    def test_reject_paths_into_history_or_outside_snapshot(self):
        for name in ('../escape', '/tmp/escape', 'evidence/old.bin', '.local/key', 'src/../../escape'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                with self.assertRaisesRegex(ValueError, 'unsafe source'):
                    receive_source.receive(archive({name: b'bad'}), Path(temp) / 'project')


if __name__ == '__main__':
    unittest.main()
