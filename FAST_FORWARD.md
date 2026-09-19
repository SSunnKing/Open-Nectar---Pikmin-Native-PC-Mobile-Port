# Experimental local fast-forward

In **30 FPS (stable)** mode, hold **F10** for 2x simulation speed and release it
for normal speed. Opening settings or losing focus cancels the hold; press F10
again to resume. Fast-forward is disabled in the experimental 60/120 FPS modes.
It is a temporary local control, not a saved setting or an Archipelago item.

The day clock, delta-time-driven movement, physics, and animation advance with
the simulation. This does not extend the day or alter relative Pikmin stats.
Audio continues on its existing sample/wall clock at normal playback speed.
Raw wall-clock gameplay timers need further audit; this is not a claim of exact
emulator-equivalent acceleration for every cutscene or enemy.

## Timing changes

The scheduler now accepts a bounded 1x–2x rate. It accumulates scaled wall time
but retains the original small simulation delta. The main loop consumes every
scheduled tick, up to the existing catch-up limit, and supplies that fixed delta
to gameplay after updating its wall-clock diagnostics. This also corrects the
previous normal-speed consumer that treated a multiple-tick schedule as a boolean
and used elapsed wall time instead of the scheduled delta.

Input is polled once per consumed tick. If input or the tick changes speed/frame
mode, the sampled tick is delivered before resetting the schedule. This avoids
discarding its button edges. Pauses long enough to hit the scheduler suspension
threshold create no catch-up debt; OS, networking and audio clocks are not globally modified.

Presentation's software deadline scales with the hold. Each update still renders
a frame: 2x at 30 Hz therefore needs approximately 60 rendered frames per second.
There is no render-skipping or update/render separation in this prototype; a
machine that cannot keep up will not sustain 2x. Catch-up remains bounded rather
than allowing unlimited queued work. The port already disables driver swap-interval
waiting and applies its own presentation limiter.

The simulation speed check uses the **current simulation clamp**, not the prior
presentation clamp. This matters when a section changes frame mode while F10 is
held: a stale presentation interval must not enable 2x in a 60/120 Hz simulation.

## Validation and reproduction

The scheduler test covers unchanged 1x rates across varied presentation rates,
jitter, bounded stalls, speed-change debt reset, suspension, and 2x elapsed-time
ratios. Its accelerated test consumes all requested fixed ticks, rather than
checking only the scheduler's counters.

```sh
ctest --test-dir build-speedup --output-on-failure -R 'pc_frame_scheduler_test|pc_pad_axis_test|pc_menu_repeat_test|pc_render_phase_test'
```

`tools/fast_forward_smoke.cpp` is an isolated integration App for linking in place
of `pc_main.cpp` against the completed native build. It initializes SDL/the game
system, uses the real System loop and presentation limiter, injects F10 press and
release events, and measures 1x/2x/1x over sixty tick intervals each. It also tests
the settings-open/close interface, focus-loss event cancellation and 60/120 clamp
rejection with a held key. It uses original local base assets for system fonts/audio
initialization but creates no campaign or game scene. Run it in a fresh private
directory with those assets, a dummy audio driver and a bounded process timeout.
The test window must have focus for the accelerated interval.

Recorded Windows validation (2026-09-12): private Release/JAudio full build passed,
as did all four CTest targets above. The real-loop smoke passed with measured
simulation/wall ratios **0.995594, 1.998271, 0.986176** for normal, held F10, and
released F10. Settings cancellation, held-key rejection in 60/120 modes, and the
focus-loss event check also passed. The runner explicitly focused the test window
using the Windows foreground-window API; preceding unfocused runs correctly stayed
at 1x and failed the test's acceleration expectation. No production focus bypass
was added. Evidence remains in private `output/speedup-runtime-05/result.json` in
the surrounding randomizer workspace; fixture provenance is beside
`output/speedup-smoke-build-04/fixture.exe`.

The initial default legacy-audio build failed to link an existing
`Jac_NoteDemoSkipped` reference. Validation used `-DPIKMIN_NATIVE_JAUDIO=ON`, as
used by the fork's active native build. That unrelated legacy-audio link problem
was not changed in this timing patch.

This source does not add an automatic gameplay test or alter production input
to inject events. Normal movement/combat, real settings-menu interaction, saves,
cutscenes, hardware-under-load behavior and subjective audio acceptance still need
playtesting before removing the experimental label.

## Upstream scope

This work is independent of randomizer balance and P2 actor work. The native
implementation began from fork commit `e704b268`; upstream `main` was inspected
at `f88c7810`. The scheduler baseline is shared with upstream. Keep timing review
and gameplay validation separate from enemy-fixture acceptance because fixed-delta
consumption changes the normal-speed timing contract too.

The packaged randomizer build also received a successful user gameplay playtest
on 2026-09-12. The upstream adaptation excludes fork-only cross-game and P2 hooks;
this report does not imply exhaustive campaign or cutscene coverage.

On the upstream adaptation, all four CTest targets pass. A full Windows MinGW
16.2 Release/JAudio build is blocked by existing legacy OpenGL header errors
(`include/GL/glext.h`: undefined GLint/GLsizei). Compiling untouched upstream
`f88c7810` System.cpp with the same flags reproduces these errors. The initial
WIN32 legacy-renderer macro collision was bypassed locally with `-UWIN32`;
no unrelated build fixes are included. Full-build and real-loop results above
refer to the fork build, not a successfully linked upstream executable.
