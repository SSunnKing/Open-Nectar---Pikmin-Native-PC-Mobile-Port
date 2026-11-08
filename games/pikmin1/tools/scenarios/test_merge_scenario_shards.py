"""Exercise the shard merger's artifact contract without building the game."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

MERGER = Path(__file__).with_name('merge_scenario_shards.py')


class ShardMergeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.shards = [self.root / f'scenario-shard-macOS-{i}' for i in (1, 2)]
        self.out = self.root / 'merged'
        tests = [{'name': 'generate', 'properties': [{'name': 'FIXTURES_SETUP', 'value': ['data']}]},
                 {'name': 'first'}, {'name': 'second'}]
        for i, shard in enumerate(self.shards):
            (shard / 'shard').mkdir(parents=True)
            (shard / 'shard/tests.json').write_text(json.dumps({'tests': tests}))
            self.junit(i, ['generate', ('first', 'second')[i]])
            (shard / 'scenario-logs').mkdir()
            (shard / f'scenario-logs/{i}.hash.log').write_text(f'tick {i}\n')
            (shard / 'scenario-evidence').mkdir()
            (shard / 'scenario-evidence/metadata.json').write_text('{"revision":"same"}\n')
            (shard / 'scenario-evidence/CMakeCache.txt').write_text(f'CMAKE_CACHE_PATCH_VERSION:INTERNAL={i+4}\n')
            (shard / 'scenario-evidence/CTestTestfile.cmake').write_text(f'# runner {i}\n')

    def junit(self, index, names):
        (self.shards[index] / 'shard/junit.xml').write_text(
            '<testsuite>' + ''.join(f'<testcase name="{n}" status="run"/>' for n in names) + '</testsuite>')

    def run_merge(self):
        return subprocess.run([sys.executable, str(MERGER), '--shards', '2', '--out', str(self.out),
                               *map(str, self.shards)], capture_output=True, text=True)

    def test_retains_each_shards_exact_provenance(self):
        result = self.run_merge()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for shard in self.shards:
            for source in (shard / 'scenario-evidence').iterdir():
                self.assertEqual((self.out / 'scenario-evidence' / shard.name / source.name).read_bytes(),
                                 source.read_bytes())
        self.assertEqual(len(list((self.out / 'scenario-logs').glob('*.hash.log'))), 2)

    def test_conflicting_state_hash_logs_still_fail(self):
        for i, shard in enumerate(self.shards):
            (shard / 'scenario-logs/common.hash.log').write_text(f'tick {i}\n')
        result = self.run_merge()
        self.assertEqual(result.returncode, 1)
        self.assertIn('common.hash.log: written by', result.stdout)

    def test_missing_test_still_fails(self):
        self.junit(1, ['generate'])
        result = self.run_merge()
        self.assertEqual(result.returncode, 1)
        self.assertIn('second', result.stdout)
        self.assertIn('ran in no shard', result.stdout)

    def test_duplicate_test_still_fails(self):
        self.junit(1, ['generate', 'first', 'second'])
        result = self.run_merge()
        self.assertEqual(result.returncode, 1)
        self.assertIn('first ran in more than one shard', result.stdout)

    def test_missing_shard_still_fails(self):
        (self.shards[1] / 'shard/junit.xml').unlink()
        result = self.run_merge()
        self.assertEqual(result.returncode, 1)
        self.assertIn('no shard/tests.json or shard/junit.xml', result.stdout)


if __name__ == '__main__':
    unittest.main()
