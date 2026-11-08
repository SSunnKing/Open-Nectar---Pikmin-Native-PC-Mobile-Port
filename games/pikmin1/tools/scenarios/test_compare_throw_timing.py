import json
import pathlib
import tempfile
import unittest
from compare_throw_timing import compare


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.sources = {name: pathlib.Path(self.temp.name)/name/'scenario-logs' for name in ('macOS','Linux','Windows')}
        for directory in self.sources.values():
            directory.mkdir(parents=True)
            evidence = directory.parent/'scenario-evidence'
            evidence.mkdir()
            (evidence/'metadata.json').write_text(json.dumps({'revision':'same','upstream_revision':'upstream',
                'harness_base_revision':'harness','session_seed':0,'rates':[30,60],'input_source_sha256':{'input':'hash'}}))
            for rate, k in ((30,15),(60,18)):
                (directory/f'scenario_throw_release_{rate}.log').write_text(
                    f'[netplay-det] reseed day=0 stage=0 sessionSeed=0 hash=0x123 tick=0\n'
                    f'[scenario] throw_release calibration: K = {k} ticks\n[scenario] PASS throw_release at tick 200 (intro 0 ticks)\n')

    def path(self):
        return self.sources['Windows']/'scenario_throw_release_60.log'

    def test_equal(self):
        self.assertEqual(compare(self.sources)['Linux'], {'30':15,'60':18})

    def test_changed_timing(self):
        self.path().write_text(self.path().read_text().replace('18 ticks','19 ticks'))
        with self.assertRaisesRegex(ValueError,'differs'):
            compare(self.sources)

    def test_missing_log(self):
        self.path().unlink()
        with self.assertRaises(OSError):
            compare(self.sources)

    def test_bad_logs(self):
        original = self.path().read_text()
        for text in ('', original.replace('[scenario] PASS','[scenario] FAIL'),
                     original.replace('sessionSeed=0','sessionSeed=1'),
                     original.replace('sessionSeed=0','sessionSeed=01'),
                     original.replace('PASS throw_release','PASS boot_smoke'),
                     original+original, original.replace('18 ticks','0 ticks')):
            with self.subTest(text=text):
                self.path().write_text(text)
                with self.assertRaises(ValueError):
                    compare(self.sources)

    def test_wrong_revision(self):
        path = self.sources['Windows'].parent/'scenario-evidence'/'metadata.json'
        data = json.loads(path.read_text())
        data['revision'] = 'different'
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError,'provenance differs'):
            compare(self.sources)

    def nest_evidence(self, name, shards=('shard-a', 'shard-b')):
        evidence = self.sources[name].parent/'scenario-evidence'
        raw = (evidence/'metadata.json').read_text()
        (evidence/'metadata.json').unlink()
        for index, shard in enumerate(shards):
            directory = evidence/shard
            directory.mkdir()
            (directory/'metadata.json').write_text(raw)
            (directory/'CMakeCache.txt').write_text(f'CMAKE_CACHE_PATCH_VERSION:INTERNAL={index}\n')

    def test_nested_shard_provenance(self):
        # Merged shard artifacts nest provenance; unsharded JAudio artifacts keep one file.
        self.nest_evidence('macOS')
        self.nest_evidence('Linux')
        caches = sorted((self.sources['macOS'].parent/'scenario-evidence').glob('*/CMakeCache.txt'))
        self.assertEqual(len(caches), 2)
        self.assertNotEqual(caches[0].read_bytes(), caches[1].read_bytes())
        self.assertTrue((self.sources['Windows'].parent/'scenario-evidence'/'metadata.json').is_file())
        self.assertEqual(compare(self.sources)['Linux'], {'30':15,'60':18})

    def test_nested_revision_conflict(self):
        self.nest_evidence('macOS')
        path = self.sources['macOS'].parent/'scenario-evidence'/'shard-b'/'metadata.json'
        data = json.loads(path.read_text())
        data['revision'] = 'different'
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError,'provenance differs'):
            compare(self.sources)

    def test_nested_missing_metadata(self):
        self.nest_evidence('macOS', shards=('shard-a',))
        (self.sources['macOS'].parent/'scenario-evidence'/'shard-a'/'metadata.json').unlink()
        with self.assertRaises(OSError):
            compare(self.sources)

    def test_single_source(self):
        with self.assertRaisesRegex(ValueError,'two distinct'):
            compare({'macOS': self.sources['macOS']})


if __name__ == '__main__':
    unittest.main()
