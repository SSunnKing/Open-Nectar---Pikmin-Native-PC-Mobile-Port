# RNG expression regression coverage (#250)

From the repository root:

```
python3 games/pikmin1/tools/rng-sequence/check.py --cxx clang++ --output /tmp/rng-results
```

Use `g++` for Linux GCC or Windows MSYS2 MinGW-w64. Python 3.9+ and a C++17 compiler are required. No SDL, OpenGL, Nintendo assets or scenario runner is needed. CI uses all three platforms. Each run compiles at `-O0` and `-O2` with FP contraction disabled and retains compiler version, revision, source hashes, seeds, flags, generated source, build logs and runtime results.

The harness extracts the actual `PIKI_PC_PORT` expression arms at all eight #224 sites. Anchors must be unique; missing anchors, changed preprocessor structure, compilation errors and unexpected runtime exits fail the run. Synthetic receivers supply just the state those expressions use. An independent expected-value oracle checks coordinates, direction/sign, attack decision, panel motion parameters and each draw's stream/range/count using four seeded input sequences. Attack weights are 1, 51 and 101. Both shooting-star gate outcomes are covered.

For each site, the runner swaps its first two draw declarations and requires the compiled program to exit with that site's specific runtime assertion. A compile error or unrelated failure cannot satisfy the negative control. This detects a changed assignment of draws even where the ranges are identical, or the attack product remains mathematically symmetric.

## Scope and audit

These are expression-level tests, not whole-object/gameplay tests. The synthetic `gsys`, `zen::Rand` and cosmetic adapters expose side effects; they do not validate the real RNG implementation (covered separately by `pc_sim_rng_test`), downstream vector normalization, rendering, or enemy state machines. Rates are irrelevant to these expressions; 30/60 Hz timing and state hashes belong to the fork gameplay runner.

The shooting-star `PC_WM_STAR_X` macro is read from production source and compiled as part of the harness; its RNG call is therefore included. `zen::Rand` delegates to `gsys->getRand` in `include/zen/Math.h`; these simulation calls retain the simulation adapter. The three existing cosmetic sites retain their cosmetic adapter. This bounded audit does not establish the absence of draws hidden in other helpers, other macros, or Pikmin 2.

A source refactor may require updating the extraction boundaries and oracle after review. Do not silently skip a missing site. Cross-platform agreement here does not establish original GameCube evaluation order or complete simulation determinism.
