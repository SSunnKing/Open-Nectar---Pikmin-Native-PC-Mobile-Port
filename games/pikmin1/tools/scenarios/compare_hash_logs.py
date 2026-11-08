#!/usr/bin/env python3
"""Compares the per-tick state-hash logs of the stand-in scenarios across operating systems.

CI (ci.yml, job scenario-hash-compare) calls it with one directory per OS, each
holding that OS's build/pikmin1/scenario-logs/*.hash.log (the files
run_standin_scenario.cmake writes with --state-hash-log):

    compare_hash_logs.py [--label TEXT] [--common-only] NAME=DIR [NAME=DIR ...]

--common-only compares only the scenarios every directory has (for sets that
cover different scenarios, such as the native-JAudio-off and -on builds).
It fails (exit 1) when
  - a directory is missing, or holds no .hash.log,
  - a scenario has a log on some OS and none on another,
  - a log is empty, or
  - two OSes wrote different bytes for the same scenario.
For each scenario that differs it prints the first differing line (the tick and
which of the hash columns differ) and every OS's value. Nothing is normalised:
the files must be byte-identical, line ends included (the game writes the log
in binary mode, so it is LF on every OS).

Line format (pc_port/netplay/pc_state_hash.h): <tick> <total> <navi> <piki>
<teki> <item> <world> <rng> <mode>, each hash 16 hex digits.

With GITHUB_STEP_SUMMARY set, a table of the result is appended to it.
"""
import argparse
import os
import sys

COLUMNS = ["tick", "total", "navi", "piki", "teki", "item", "world", "rng", "mode"]


def read_lines(path):
    """The file split at LF, exactly as written: a CR stays in its line, and a
    file that ends in LF ends in one empty element. An empty file gives []."""
    with open(path, "rb") as f:
        data = f.read().decode("utf-8", errors="replace")
    return data.split("\n") if data else []


def collect(directory):
    """scenario name -> path of its .hash.log; None when the directory is missing."""
    if not os.path.isdir(directory):
        return None
    logs = {}
    for root, _dirs, files in os.walk(directory):
        for name in files:
            if name.endswith(".hash.log"):
                logs[name[: -len(".hash.log")]] = os.path.join(root, name)
    return logs


def differing_columns(lines):
    """Column names whose value is not the same in every line of `lines`."""
    fields = [line.split() for line in lines]
    names = []
    for i, column in enumerate(COLUMNS):
        values = {f[i] if i < len(f) else None for f in fields}
        if len(values) > 1:
            names.append(column)
    return names


def first_difference(per_os):
    """(1-based line number, {os: line or None}) of the first line that is not the same everywhere."""
    longest = max(len(lines) for lines in per_os.values())
    for i in range(longest):
        row = {name: (lines[i] if i < len(lines) else None) for name, lines in per_os.items()}
        if len(set(row.values())) > 1:
            return i + 1, row
    return None, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--label", default="state-hash logs", help="what is being compared, for the headings")
    parser.add_argument("--common-only", action="store_true",
                        help="compare only the scenarios every source has; the others are not a problem "
                             "(for sources that cover different scenario sets)")
    parser.add_argument("sources", nargs="+", metavar="NAME=DIR")
    args = parser.parse_args()

    sources = {}
    for item in args.sources:
        name, sep, directory = item.partition("=")
        if not sep or not name or not directory:
            parser.error(f"expected NAME=DIR, got {item!r}")
        sources[name] = directory

    problems = []
    logs = {}
    for name, directory in sources.items():
        found = collect(directory)
        if not found:
            problems.append(f"{name}: no *.hash.log under {directory} "
                            f"({'directory missing' if found is None else 'directory empty'})")
            continue
        logs[name] = found

    if not logs:
        scenarios = []
    elif args.common_only:
        scenarios = sorted(set.intersection(*[set(found) for found in logs.values()]))
        if not scenarios:
            problems.append("no scenario has a log in every source")
    else:
        scenarios = sorted(set().union(*[set(found) for found in logs.values()]))
    rows = []
    for scenario in scenarios:
        have = {name: found[scenario] for name, found in logs.items() if scenario in found}
        # An OS with no logs at all is already a problem of its own, above.
        missing = [name for name in logs if name not in have]
        if missing:
            problems.append(f"{scenario}: no log from {', '.join(missing)}")
            rows.append((scenario, "MISSING", f"no log from {', '.join(missing)}"))
            continue
        per_os = {name: read_lines(path) for name, path in have.items()}
        empty = [name for name, lines in per_os.items() if not lines]
        if empty:
            problems.append(f"{scenario}: empty log from {', '.join(empty)}")
            rows.append((scenario, "EMPTY", f"empty log from {', '.join(empty)}"))
            continue
        number, row = first_difference(per_os)
        if number is None:
            count = len(next(iter(per_os.values()))) - 1
            rows.append((scenario, "same", f"{count} lines"))
            continue
        present = [line for line in row.values() if line is not None]
        values = [line.rstrip("\r") for line in present]
        if len(set(values)) == 1 and len(present) == len(row):
            # The same hashes, written with different line ends.
            crlf = sorted(name for name, line in row.items() if line.endswith("\r"))
            detail = (f"line {number}: same hashes, different line ends "
                      f"({', '.join(crlf)} wrote CR before the LF)")
        else:
            columns = differing_columns(values) if len(values) > 1 else []
            tick = (values[0].split() or ["?"])[0] if values else "?"
            shown = ", ".join(c for c in columns if c != "tick") or "line count"
            detail = f"first difference at line {number} (tick {tick}), columns: {shown}"
        lengths = {name: len(lines) for name, lines in per_os.items()}
        if len(set(lengths.values())) > 1:
            detail += "; line counts " + ", ".join(f"{n}={c}" for n, c in sorted(lengths.items()))
        problems.append(f"{scenario}: {detail}")
        rows.append((scenario, "DIFFERENT", detail))
        print(f"DIFFERENT {scenario}: {detail}")
        for name in sorted(row):
            print(f"    {name:8} {row[name] if row[name] is not None else '(log ends before this line)'}")

    same = sum(1 for _s, state, _d in rows if state == "same")
    print(f"{args.label}: {len(scenarios)} scenarios on {', '.join(sources)}; "
          f"{same} identical, {len(scenarios) - same} not")
    for scenario, state, detail in rows:
        if state == "same":
            print(f"  same      {scenario}: {detail}")

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(f"### Cross-OS {args.label} ({', '.join(sources)})\n\n")
            f.write(f"{same} of {len(scenarios)} scenarios identical on every OS.\n\n")
            if problems:
                f.write("| Scenario | Result | Detail |\n|---|---|---|\n")
                for scenario, state, detail in rows:
                    if state != "same":
                        f.write(f"| `{scenario}` | {state} | {detail} |\n")
                for problem in problems:
                    if not any(problem.startswith(f"{s}:") for s, _st, _d in rows):
                        f.write(f"| | PROBLEM | {problem} |\n")
                f.write("\n")

    if problems:
        print("\nFAILED:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
