"""Bounded replay run for the netplay determinism harness.

Runs one fresh game process that replays a PKNI input file in deterministic,
unthrottled mode while writing a per-tick state-hash log, then exits after
--ticks ticks (PIKMIN_NETPLAY_EXIT_AFTER_TICKS).

Everything the run writes stays in --out: the game runs with that directory
as its working directory, so pikmin_settings.conf is a private copy (seeded
from --config-overrides), the memory card goes to NECTAR_SAVE_DIR=<out>/save
and the game files are reached through an <out>/assets junction (symlink off
Windows) to --assets. Only the spawned process is ever killed.

Determinism check (two replays of the same input must hash the same):

    py tools/netplay/gen_inputs.py --ticks 1800 --seed 1 --out run/in.pkni
    py tools/netplay/run_replay.py --exe bin/nectar.exe --assets <assets> \
        --replay run/in.pkni --ticks 1800 --out run/a \
        --exe-args --vs-map maps/vs/impact_arena/map.json
    py tools/netplay/run_replay.py ... --out run/b   (same arguments)
    py tools/netplay/compare_hashes.py run/a/hashes.txt run/b/hashes.txt

--record <file> also records while replaying; the file must then equal
--replay byte for byte (record/replay identity).
"""

import argparse
import hashlib
import os
import subprocess
import sys
import time
from pathlib import Path

try:
    import _winapi
except ImportError:  # not Windows: a symlink is used instead
    _winapi = None


def parse_kv(items, what):
    out = {}
    for item in items or []:
        if "=" not in item:
            raise SystemExit(f"--{what} needs key=value, got {item!r}")
        key, value = item.split("=", 1)
        out[key.strip()] = value.strip()
    return out


def link_assets(target, link):
    if link.exists():
        return
    if _winapi is not None:
        _winapi.CreateJunction(str(target), str(link))
    else:
        os.symlink(str(target), str(link), target_is_directory=True)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", type=Path, required=True, help="game executable")
    p.add_argument("--assets", type=Path, required=True,
                   help="extracted game files (the directory holding dataDir/)")
    p.add_argument("--replay", type=Path, required=True, help="PKNI input file")
    p.add_argument("--ticks", type=int, required=True)
    p.add_argument("--out", type=Path, required=True, help="private run directory")
    p.add_argument("--config-overrides", nargs="*", default=[], metavar="key=value",
                   help="lines for the private pikmin_settings.conf")
    p.add_argument("--record", type=Path, default=None,
                   help="also record while replaying (must then equal --replay)")
    p.add_argument("--seed", type=int, default=0, help="PIKMIN_NETPLAY_SEED")
    p.add_argument("--preroll-rand", type=int, default=0)
    p.add_argument("--throttled", action="store_true",
                   help="run at the normal tick rate instead of as fast as possible")
    p.add_argument("--timeout", type=float, default=600)
    p.add_argument("--env", nargs="*", default=[], metavar="K=V",
                   help="extra environment, e.g. PIKMIN_NETPLAY_TEST_CAMERA_WOBBLE=20")
    p.add_argument("--exe-args", nargs=argparse.REMAINDER, default=[],
                   help="arguments passed on to the game (must come last)")
    a = p.parse_args(argv)

    run = a.out.resolve()
    run.mkdir(parents=True, exist_ok=True)
    assets = a.assets.resolve()
    if not (assets / "dataDir").is_dir():
        raise SystemExit(f"run_replay: {assets} has no dataDir/")
    link_assets(assets, run / "assets")

    overrides = parse_kv(a.config_overrides, "config-overrides")
    if overrides:
        with open(run / "pikmin_settings.conf", "w") as f:
            for key, value in sorted(overrides.items()):
                f.write(f"{key} = {value}\n")

    save_dir = run / "save"
    save_dir.mkdir(exist_ok=True)
    hash_log = run / "hashes.txt"
    stdout_log = run / "native.log"

    env = dict(os.environ)
    # A netplay switch left in the caller's environment must not leak in.
    for key in [k for k in env if k.upper().startswith(("PIKMIN_NETPLAY_", "PIKMIN_INPUT_", "PIKMIN_STATE_HASH_"))]:
        del env[key]
    env.update(
        SDL_AUDIODRIVER="dummy",
        PIKMIN_NETPLAY_DETERMINISTIC="1",
        PIKMIN_NETPLAY_SEED=str(a.seed),
        PIKMIN_STATE_HASH_LOG=str(hash_log),
        PIKMIN_INPUT_REPLAY=str(a.replay.resolve()),
        PIKMIN_NETPLAY_EXIT_AFTER_TICKS=str(a.ticks),
        NECTAR_SAVE_DIR=str(save_dir),
    )
    if not a.throttled:
        env["PIKMIN_NETPLAY_UNTHROTTLED"] = "1"
    if a.record is not None:
        env["PIKMIN_INPUT_RECORD"] = str(a.record.resolve())
    if a.preroll_rand:
        env["PIKMIN_NETPLAY_TEST_PREROLL_RAND"] = str(a.preroll_rand)
    env.update(parse_kv(a.env, "env"))

    cmd = [str(a.exe.resolve())] + list(a.exe_args)
    startup = None
    if sys.platform == "win32":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0

    start = time.time()
    with open(stdout_log, "w") as child_out:
        proc = subprocess.Popen(cmd, cwd=str(run), env=env, startupinfo=startup,
                                stdout=child_out, stderr=subprocess.STDOUT)
        try:
            rc = proc.wait(timeout=a.timeout)
        except subprocess.TimeoutExpired:
            proc.kill()  # our own child only
            try:
                rc = proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                rc = 124
            print(f"run_replay: timeout after {a.timeout}s, killed pid {proc.pid}")
    secs = time.time() - start

    nlines = 0
    if hash_log.exists():
        with open(hash_log, "r", errors="replace") as f:
            nlines = sum(1 for line in f if line.strip())
    exe_path = a.exe.resolve()
    print(f"run_replay: exit={rc} ticks={nlines}/{a.ticks} time={secs:.1f}s "
          f"tps={(nlines / secs) if secs > 0 else 0.0:.1f}")
    print(f"run_replay: exe={exe_path} sha256={hashlib.sha256(exe_path.read_bytes()).hexdigest()}")
    print(f"STDOUT_LOG={stdout_log}")
    print(f"HASH_LOG={hash_log}")

    # Only a run that provably replayed counts: the load line must be in the
    # log and the hash log must hold exactly --ticks lines. Otherwise a silent
    # fallback to live input would look like a pass.
    try:
        text = stdout_log.read_text(errors="replace")
    except OSError:
        text = ""
    ok = rc == 0
    if "[netplay] input replay:" not in text:
        print("run_replay: FAIL: replay did not load (no '[netplay] input replay:' line)")
        ok = False
    if nlines != a.ticks:
        print(f"run_replay: FAIL: {nlines} hash lines, expected {a.ticks}")
        ok = False
    if a.record is not None:
        # The record covers exactly --ticks ticks; compare it with that much
        # of the replay (header 10 bytes, then 56 bytes per tick for v2, 64
        # for v3 or 104 for v4; a v1 replay records as v2, so it never matches).
        recorded = a.record.read_bytes() if a.record.exists() else b""
        source = a.replay.read_bytes()
        rec = int.from_bytes(source[8:10], "little") if len(source) >= 10 else 56
        expected = source[: 10 + rec * a.ticks]
        same = recorded == expected
        print(f"run_replay: record {'matches' if same else 'DIFFERS from'} the replay "
              f"({len(recorded)} vs {len(expected)} bytes)")
        ok = ok and same
    return 0 if ok else (rc if rc else 1)


if __name__ == "__main__":
    raise SystemExit(main())
