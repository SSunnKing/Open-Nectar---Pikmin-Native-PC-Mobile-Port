# Upstream RNG coverage (#250)

This coverage branch merges upstream a39f2e11f641b1a717fab0cc44218a8ba5650264 into fork harness cbf41fca170ad8e5a6323fbc899e38206e30c8f1. Eight conflict arms were resolved to upstream #224's left-to-right expressions. The harness base had later right-to-left changes with disassembly claims; those claims are not adjudicated by this coverage work. The original integration branch is unchanged.

The result includes the fork scenario runner, stand-in writers, deterministic-float implementation and other integration changes. It is a controlled integration build, not a pure upstream gameplay build. The expression checks separately compile the merged upstream expressions in a synthetic context.

`.github/workflows/disc-free-scenarios.yml` builds that same branch revision on macOS Clang, Linux GCC and Windows MinGW-w64. Existing per-tick state-hash comparisons cover every generated stand-in scenario across OSes. The new throw timing comparison requires exactly one valid calibration and a successful scenario verdict at both 30 and 60 Hz, on all three OSes with native JAudio both off and on. It fails on missing logs, unequal K, failed scenarios, or missing session-seed evidence. Timing is compared per rate; 30 Hz and 60 Hz are not required to have the same K.

Each scenario-hash artifact now contains throw-release stdout and build provenance as well as hashes. `record_rng_provenance.py` records the exact revision, upstream/harness bases, compiler, input/seed implementation source hashes (LF-normalized for cross-OS comparison, plus raw working-tree hashes), CMake cache and CTest command definitions. Session seed is pinned to zero by the scenario runner; reseed lines in retained logs show the resulting day/stage seed. The scripted inputs are in the hashed `scenario_throw_release.cpp`.

Local comparator verification:

```
python3 -m unittest discover -s games/pikmin1/tools/scenarios -p 'test_compare_throw_timing.py' -v
```

The workflow's timing and state comparisons are gates; do not read a printed K summary alone as a pass. Actual cross-platform results are established only by a completed run of this branch, linked in #250. Enemy RNG expressions are covered by the focused upstream checks; no claim is made that gameplay scenarios reach every enemy behavior or cosmetic site, or that other helper-hidden draws are absent.
