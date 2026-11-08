#!/usr/bin/env python3
"""Merges one OS's scenario shards (disc-free-scenarios.yml) into one result directory.

The scenarios job runs each OS's stand-in scenarios in SHARDS jobs (ctest -I k,,SHARDS). Each
shard uploads a directory with

    shard/tests.json            ctest --show-only=json-v1 of the whole scenario set (no -I)
    shard/junit.xml             ctest --output-junit of the tests this shard ran
    scenario-logs/...           its scenario logs (state-hash, throw_release, throw_flight)
    scenario-evidence/...       record_rng_provenance.py's output
    Testing/Temporary/LastTest.log

    merge_scenario_shards.py --shards N --out DIR SHARD_DIR [SHARD_DIR ...]

It fails (exit 1) unless
  - there are exactly N shard directories, each with tests.json and junit.xml,
  - every shard lists the same scenario set,
  - every test of that set ran in exactly one shard (a FIXTURES_SETUP test, such as
    scenario_standin_generate, runs in every shard that needs it and is allowed more than once),
  - no test ran that is not in the set, and
  - two shards never wrote different bytes to the same scenario log name.
Raw build provenance is retained separately in scenario-evidence/<shard-name>/; runner
tool versions and paths can differ between shards. Nothing is normalised or discarded.
DIR then holds scenario-logs/, scenario-evidence/ and Testing/Temporary/LastTest_shard<k>.log,
the layout ci_k_summary.sh, check_throw_flight.py and compare_hash_logs.py read from a build
directory. With GITHUB_STEP_SUMMARY set, a coverage line is appended to it.
"""
import argparse
import filecmp
import json
import os
import re
import shutil
import sys
import xml.etree.ElementTree as ET


def load_set(path):
    """(all test names, FIXTURES_SETUP test names) from a ctest json-v1 listing."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    names, setup = set(), set()
    for test in data.get("tests", []):
        names.add(test["name"])
        if any(p.get("name") == "FIXTURES_SETUP" for p in test.get("properties", [])):
            setup.add(test["name"])
    return names, setup


def load_ran(path):
    """Names of the testcases in a ctest JUnit file, and those not run (status notrun)."""
    try:
        cases = [(c.get("name"), c.get("status")) for c in ET.parse(path).getroot().iter("testcase")]
    except ET.ParseError:
        # A test's captured output can hold bytes that are not valid XML; the testcase
        # elements CTest writes are still one per test, name first.
        with open(path, encoding="utf-8", errors="replace") as f:
            cases = re.findall(r'<testcase name="([^"]*)"[^>]*?status="([^"]*)"', f.read())
    ran = [name for name, _ in cases]
    notrun = [name for name, status in cases if status in ("notrun", "disabled")]
    return ran, notrun


def copy_tree(src, dst, owner, problems, owners):
    for root, _dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        for name in files:
            s = os.path.join(root, name)
            key = os.path.normpath(os.path.join(rel, name))
            d = os.path.join(dst, key)
            if os.path.exists(d):
                if not filecmp.cmp(s, d, shallow=False):
                    problems.append(f"{key}: written by {owners[key]} and {owner} with different bytes")
                continue
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d)
            owners[key] = owner


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--shards", type=int, required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--label", default="")
    parser.add_argument("shard_dirs", nargs="*")
    args = parser.parse_args()

    problems = []
    dirs = sorted(args.shard_dirs)
    if len(dirs) != args.shards:
        problems.append(f"{len(dirs)} shard result(s), expected {args.shards}: {dirs}")

    expected, setup = None, set()
    ran_in = {}  # test name -> [shard names]
    per_shard = []
    for d in dirs:
        shard = os.path.basename(os.path.normpath(d))
        tests_json = os.path.join(d, "shard", "tests.json")
        junit = os.path.join(d, "shard", "junit.xml")
        if not os.path.isfile(tests_json) or not os.path.isfile(junit):
            problems.append(f"{shard}: no shard/tests.json or shard/junit.xml (the shard did not run its tests)")
            continue
        names, fixture_setup = load_set(tests_json)
        if expected is None:
            expected, setup = names, fixture_setup
        elif names != expected:
            problems.append(f"{shard}: lists a different scenario set ({len(names)} tests, not {len(expected)})")
        ran, notrun = load_ran(junit)
        per_shard.append((shard, len(ran), notrun))
        for name in ran:
            ran_in.setdefault(name, []).append(shard)

    if expected is not None:
        missing = sorted(expected - ran_in.keys())
        extra = sorted(ran_in.keys() - expected)
        twice = sorted(n for n, s in ran_in.items() if len(s) > 1 and n not in setup)
        if missing:
            problems.append(f"{len(missing)} test(s) ran in no shard: {', '.join(missing)}")
        if extra:
            problems.append(f"{len(extra)} test(s) ran that are not in the set: {', '.join(extra)}")
        for n in twice:
            problems.append(f"{n} ran in more than one shard: {', '.join(ran_in[n])}")

    out = args.out
    owners = {}
    for i, d in enumerate(dirs, 1):
        shard = os.path.basename(os.path.normpath(d))
        logs_src = os.path.join(d, "scenario-logs")
        if os.path.isdir(logs_src):
            copy_tree(logs_src, os.path.join(out, "scenario-logs"), shard, problems, owners)
        evidence = os.path.join(d, "scenario-evidence")
        if os.path.isdir(evidence):
            shutil.copytree(evidence, os.path.join(out, "scenario-evidence", shard))
        last = os.path.join(d, "Testing", "Temporary", "LastTest.log")
        if os.path.isfile(last):
            dst = os.path.join(out, "Testing", "Temporary", f"LastTest_shard{i}.log")
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(last, dst)

    logs = os.path.join(out, "scenario-logs")
    hash_logs = len([n for n in os.listdir(logs) if n.endswith(".hash.log")]) if os.path.isdir(logs) else 0
    label = f" {args.label}" if args.label else ""
    for shard, count, notrun in per_shard:
        extra = f", not run: {', '.join(notrun)}" if notrun else ""
        print(f"{shard}: {count} test(s){extra}")
    total = len(expected) if expected is not None else 0
    summary = (f"Scenario shards{label}: {len(per_shard)}/{args.shards} shard(s), "
               f"{len(ran_in)} of {total} test(s) ran, {hash_logs} state-hash log(s)")
    print(summary)
    for p in problems:
        print(f"FAIL {p}")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write(f"{'FAIL' if problems else 'ok'}: {summary}\n\n")
            for p in problems:
                f.write(f"- {p}\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
