#!/usr/bin/env python3
"""Require successful throw-release logs and equal calibration K per tick rate.

Provenance is metadata.json beside the logs, or one metadata.json per shard under
scenario-evidence/<shard-name>/. The compared fields must agree. Per-shard build
files such as CMakeCache.txt are not read and are not required to match.
"""
import argparse
import json
import pathlib
import re


def collect(directory):
    values = {}
    for rate in (30, 60):
        path = pathlib.Path(directory) / f'scenario_throw_release_{rate}.log'
        text = path.read_text()
        matches = re.findall(r'^\[scenario\] throw_release calibration: K = (\d+) ticks\r?$', text, re.M)
        if len(matches) != 1 or int(matches[0]) < 3:
            raise ValueError(f'{path}: expected exactly one valid calibration')
        if not re.search(r'^\[scenario\] PASS throw_release at tick \d+\b', text, re.M) or re.search(r'^\[scenario\] FAIL\b', text, re.M):
            raise ValueError(f'{path}: missing PASS or reported FAIL')
        if not re.search(r'\bsessionSeed=0(?:\s|$)', text) or re.search(r'\bsessionSeed=(?!0(?:\s|$))\d+', text):
            raise ValueError(f'{path}: missing fixed session seed evidence')
        values[str(rate)] = int(matches[0])
    return values


def provenance(directory):
    """metadata.json for one source, including every shard when provenance is nested."""
    evidence = pathlib.Path(directory).parent/'scenario-evidence'
    direct = evidence/'metadata.json'
    if direct.is_file():
        return [json.loads(direct.read_text())]
    shards = sorted(path for path in evidence.iterdir() if path.is_dir()) if evidence.is_dir() else []
    if not shards:
        raise FileNotFoundError(direct)
    return [json.loads((shard/'metadata.json').read_text()) for shard in shards]


def compare(sources):
    if len(sources) < 2:
        raise ValueError('at least two distinct sources are required')
    documents = [item for directory in sources.values() for item in provenance(directory)]
    for key in ('revision', 'upstream_revision', 'harness_base_revision', 'session_seed', 'rates', 'input_source_sha256'):
        encoded = [json.dumps(item[key], sort_keys=True) for item in documents]
        if len(set(encoded)) != 1:
            raise ValueError(f'provenance differs: {key}')
    if documents[0]['session_seed'] != 0:
        raise ValueError('unexpected session seed')
    values = {name: collect(directory) for name, directory in sources.items()}
    for rate in ('30', '60'):
        if len({v[rate] for v in values.values()}) != 1:
            raise ValueError(f'{rate} Hz calibration differs: {values}')
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', nargs='+', metavar='NAME=DIR')
    args = parser.parse_args()
    sources = {}
    for source in args.sources:
        name, sep, directory = source.partition('=')
        if not sep or not name or not directory or name in sources:
            parser.error(f'invalid or duplicate source: {source}')
        sources[name] = directory
    try:
        print(json.dumps(compare(sources), indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'FAIL: {error}\n')


if __name__ == '__main__':
    main()
