#!/usr/bin/env python3
"""Builds boot-manifest.txt from PIKMIN_LOG_FILE_OPENS logs.

Run each scenario on disc data with PIKMIN_LOG_FILE_OPENS=<log> (see #64), then:

    make_boot_manifest.py --version VERSION_GPIE01_01 --flags "<cmake flags>" \
        boot_smoke=a.log,b.log throw_release=c.log day30_clock=d.log > boot-manifest.txt

Output: a header, then one sorted line per unique game-relative path:
"<path>\t<OK|FAILED>\t<scenarios that opened it>". A path that opened OK in any
run is OK; no sizes or contents.
"""
import argparse
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--flags", required=True)
    ap.add_argument("runs", nargs="+", help="<scenario>=<log>[,<log>...]")
    args = ap.parse_args()

    files = {}  # path -> [ok, set(scenarios)]
    for run in args.runs:
        name, _, logs = run.partition("=")
        for log in logs.split(","):
            with open(log, encoding="utf-8") as f:
                for line in f:
                    path, _, flag = line.rstrip("\n").partition("\t")
                    if not path:
                        continue
                    entry = files.setdefault(path, [False, set()])
                    entry[0] = entry[0] or flag == "OK"
                    entry[1].add(name)

    out = sys.stdout
    out.write(f"# Boot manifest: files DVDOpen reads on the scenario boot path\n")
    out.write(f"# version: {args.version}\n# build: {args.flags}\n")
    out.write("# columns: path, OK|FAILED (OK if any run opened it), scenarios that opened it\n")
    for path in sorted(files):
        ok, scenarios = files[path]
        out.write(f"{path}\t{'OK' if ok else 'FAILED'}\t{','.join(sorted(scenarios))}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
