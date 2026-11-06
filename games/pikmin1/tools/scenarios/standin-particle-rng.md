# Particle draws on the cosmetic RNG stream

`standin-k-investigation.md` found that particle effects drew from the sim RNG
stream. Effect content therefore changed squad and enemy behaviour in
deterministic mode, and a tree with different `.pcr` data gave a different
throw_release K. This change moves those draws to the cosmetic stream.

## What moved

- `include/zen/Math.h`: new `zen::CosmeticRand` and `zen::CosmeticRandShift`
  (PC only). They are `Rand` and `RandShift` on `pc_cosmetic_randf`.
- `src/plugPikiYamashita/particleGenerator.cpp`: all 29 `Rand` / `RandShift`
  draws (emission count, lifetime, spread, velocity, size, rotation, jitter,
  drag, alpha, child offset). A file-local macro under `PIKI_PC_PORT` maps
  them to the cosmetic helpers, so the decomp lines stay as they were.
- `include/zen/particle.h`: the 2 draws in `pmGetArbitUnitVec`.
- `src/plugPikiKando/goalItem.cpp`: the leg that the Onion puff effect
  (`EFF_Onyon_Puff`) is placed on. Only the effect uses that index.

`zen::Rand` itself is unchanged, because gameplay code shares it.

With deterministic mode off, both streams are `rand()` in the same call order,
so normal play draws the same values as before.

## What did not move, and why

- Particle callbacks (`TAIeffectAttack`, `TAItank`, `TAIhibaA`, King, Spider,
  Snake, Mizu, Kogane, Pom, `UtEffect`, `NaviWhistle`, `GoalItem`) draw no
  random numbers. The attack callbacks read the emitter position, the frame
  counter and the finish flag, which do not depend on particle draws. `TAItank`
  and `TAIhibaA` write particle velocities from the hit point; they do not read
  particles for damage.
- `effectMgr.cpp`, `effectMgr2D.cpp`, `simpleParticle.cpp`,
  `bBoardColourAnim.cpp`, `ptclGenPack.cpp`, `particleManager.cpp`,
  `particleMdlManager.cpp`, `uteffect.cpp` and `src/plugPikiOgawa` have no
  `Rand` or `getRand` draws.
- `viewPiki.cpp:314` gates a smoke effect on `gsys->getRand`, but only when
  `AIPerf::optLevel == 0` (the default is 2), so the draw does not happen.
  It stays on the sim stream.

## throw_release K

RelWithDebInfo, macOS, `nectar --scenario throw_release`, 2 runs per cell,
both runs equal. All runs pass.

| Tree | Before 30 | Before 60 | After 30 | After 60 |
|---|---|---|---|---|
| Disc (Forest of Hope) | 13 | 14 | 15 | 18 |
| Stand-ins (sandbox) | 12 | 23 | 12 | 23 |
| Disc + stand-in `effects.bun` | 8 | 14 | 15 | 18 |

"Before" is `c8284cf7`. The stand-in K does not change, because the inert
stand-in effects emit no particles.

## Overlay proof

Method as in `standin-overlay-experiment.md`: a clone of the install's
`assets`, with the loose `effects/pcr/*.pcr` deleted and the stand-in
`effects/pcr/effects.bun` in place of the disc one. The control is an
unchanged clone. Both run on the default stage (`stages/stage1.ini`) with
`--state-hash-log`.

- Before: the overlay's sim RNG state differs from the control from tick 6,
  and K is 8 instead of 13 at 30 Hz.
- After: the overlay and control hash logs are identical (600 ticks at 60 Hz,
  300 ticks at 30 Hz), and K is the same (15 / 18).

Effect content no longer reaches the simulation. With `c8284cf7` the overlay
also passes at 60 Hz before this change (K 14), so the 4 of 4 failure in the
#72 note does not reproduce on this base.

The hash log is flushed every 300 ticks, and the scenario exit loses the last
partial block, so the logs end at tick 300 or 600, before the scenario ends.

## Determinism and old recordings

Two `day30_clock` runs at 60 Hz on the disc give identical hash logs
(3900 ticks), before and after the change.

Deterministic-mode state hashes and input replays recorded before this change
do not reproduce after it. Against the old binary, the `rng` column diverges
from tick 6 (the first tick with particles), then `teki` (tick 52 in
day30_clock, tick 98 in throw_release), `piki` (tick 213) and `navi` (tick
596). Ticks 1 to 5 are identical.
