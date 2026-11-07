# Ballistic motion and the 30 Hz reference

Issue #157 makes the 60/120 Hz port reproduce the original discrete arc. It does
not replace the GameCube recurrence with a continuous parabola or change launch
speeds to compensate for one type of throw.

## Disc evidence

The retained [original-disc measurements](evidence/ballistic-gc-2026-11-06.json)
come from Kyle's GPIE01 revision 1 executable in headless Dolphin 2503. The DOL
read directly from the ISO has SHA-256
`8ba130565950d37ca2563e0513348bba47b917d3c4c9b2c8525e5de24a561578`, matching the
retained DOL used to resolve the GDB driver's symbols. No disc assets or executable
bytes are included in this repository.

The driver injects controller input and a fixed cursor, and reads positions and
velocities from the original executable. Six blue-leaf throws gave sampled peaks
of +61.22 to +61.28; their residual captain velocity is printed in each retained
line. The unmodified 30 Hz port on disc assets gives +61.29 with launch speed 290.
Before the fix, the same port throw gave +68.00 at 60 Hz and +71.51 at 120 Hz.
Stand-in `throw_levels` independently gave 61.3/68.0/71.5 at its one-decimal precision.

The fall probe starts a blue leaf 200 units above the map with vertical speed 0.
The flick probe enters the real Flick state and normalizes vertical launch speed
to 125, isolating integration from the original random launch draw. The first
unforced original-disc step was 0.032989752 seconds; its flick peak 9.006573 agrees
with the fixed 30 Hz port's 9.011110. The raw traces retain this timing limitation. A second capture normalizes only
`gsys->mDeltaTime` to exactly 1/30 at each controller breakpoint, retaining the
original executable's physics instructions. Its flick peak is 9.011106, against
the port's 9.011110. Both unforced and normalized traces are retained. Across all 61 height samples,
the normalized original-disc and port 30 Hz traces differ by at most 0.000004 units
for fall and round identically to six decimals for flick. CI anchors the 30 Hz
fall/flick scenarios to these retained original-disc traces within 0.001 units.
Newly measured GameCube yellow/bud/flower throws are not claimed: those port cases
are checked against their 30 Hz reference, with original provenance in the throw
checker.

## Shared recurrence, separate owners

`PcBallisticClock` tracks consumed time within 1/30 second. It splits movement at
reference boundaries, including a configured tick that crosses one. Movement and
collision tracing still occur at the selected rate; an impulse changes the
remaining segment immediately without taking a second gravity kick.

| Owner | Original recurrence retained |
| --- | --- |
| `Creature::update` | Gravity before movement, volatile pass then persistent pass. The first pass moves the actor but discards its velocity mutation. |
| Direct `Creature::moveNew` callers, including bosses | One persistent gravity-before-movement pass. |
| `BTeki::gravitate` | Action-velocity gravity before drift, native sea clamp and bounce callback. |
| `PeveAccelerationEvent` / scripted parabolas | Drift using old velocity, then apply the reference acceleration. |

The volatile pass caches its collision-adjusted transport. A floor trace retains
a damped downward physical velocity even when its displacement is clamped: the
cached vertical transport therefore uses the actual clamped displacement.
Gravity/drag eligibility changes invalidate that transport without charging
normal gravity again. Pooled initialization, teleports and legacy-rate movement
reset reference phase. Ground contacts continue through the existing collision
code; repeated enemy sea contacts preserve elapsed phase rather than accelerating
gravity at the high tick rate.

Flower flight keeps visual spin, animation and state dispatch at the configured
cadence. Its apex/glide force terms and random acceleration draw execute from the
movement callback at every reference boundary. This deliberately changes high-rate
RNG consumption and later hashes. The30 Hz statements retain their original
ordering and values. `SeedItem`'s separate hover force is not routed into movement:
that override never calls Creature movement; real Onion sprouts use PikiHead's
shared Creature path. Scripted animation and spring motion are not characterized
as free ballistic gravity.

The extra-state divergence diagnostic includes high-rate reference phase and
cached transport, so equal visible positions with different future motion remain
observable. Its30 Hz schema is unchanged.

## Regression checks

`throw_levels` asserts the normal peak61.29 within 0.1 units at 30/60/120 Hz while
retaining its raised-height charge negative control. `check_throw_flight.py`
checks every leaf/bud/flower/yellow throw, with peak tolerance 0.05 and launch-speed
tolerance 0.1; yellow peaks 104.40 at all three rates. High-frequency ground contact
can quantize landing by one high-rate tick; its measured flight is 37/74/149 ticks.

The controlled scenarios compare full airborne heights at shared 30 Hz timestamps
and every sampled peak within 0.1 units. They cover fall, real Flick, elevated
launch, a mid-flight impulse, zero gravity, drag, volatile movement, real Teki
action-gravity and a real scripted acceleration event. Separate assertions cover
mid-interval gravity disable, a sub-tick impulse and repeated enemy ground contact.
Two further real Flying-state fixtures cover an inherited negative launch speed
and an impulse at the apex. They assert both heights and reference-boundary
velocities within 0.1. These controlled fixtures normalize the original glide-noise
value to zero using `PIKMIN_BALLISTIC_GLIDE_NOISE_ZERO=1`, while retaining the random
draw. This isolates integration from the world's rate-dependent RNG interleaving;
ordinary throw tests keep the game's noise. The knob exists only in scenario
builds and has no release-build effect. The flower force callback runs at the
**end** of each reference interval, matching ViewPiki's movement-before-state
ordering. A start-of-interval callback erases inherited downward speed and misses
an apex impulse; the two probes reject that ordering.

The clock unit also splits non-divisor 75 Hz segments. Corrupted-log tests prove
missing traces, changed heights and an interior apex cannot pass the checker.

Run the complete local suite with:

```sh
cmake --build build/pikmin1-scenarios -j8
ctest --test-dir build/pikmin1-scenarios -E _disc --output-on-failure -j6
python3 games/pikmin1/tools/scenarios/check_throw_flight.py build/pikmin1-scenarios/scenario-logs
```

The local aggregate `ballistic_cross_rate` runs after its probe dependencies in a
full CTest invocation. CI's isolated shards only run `scenario_` tests: the
aggregate comparison runs after their logs merge, beside the throw checker.

## Hash-log impact

On the original c34b4e617 baseline, all 99 saved 30 Hz hash logs stayed byte-identical;
all 77 saved 60 Hz logs changed. Gravity applies to captains and other actors as well
as thrown Pikmin, so even otherwise idle scenarios change at high rates. Flower
RNG cadence also changes. High-rate extra-state diagnostics now include the new
future-motion state. Hash logs are generated build/CI artifacts, not checked-in
golden files; they must be regenerated and compared across operating systems.

The refreshed unmodified fork base 205b7e393 was built independently from a Git
archive. All 102 saved 30 Hz logs remain byte-identical; all 80 saved 60 Hz logs change.
The [per-scenario first-difference report](evidence/ballistic-hash-impact-205b7e393.csv)
records each affected tick, hash column and traced owner. First divergence is at
tick 7 for 43 scenarios, tick 21 for 34, tick 8 for the two challenge death cases, and
tick 593 when title_boot reaches gameplay. The first changed components are
captain/enemy/object motion; world, RNG and mode do not differ at those first ticks.
Final test and cross-OS results are recorded with the fork PR. A pre-existing local macOS boot screenshot mismatch
must be distinguished from a new physics or 30 Hz regression; goldens are not
refreshed merely to hide that failure.
