# Stand-in overlay experiment (#72)

Question: does a scratch tree made from a disc install, with only the 20 stand-in
layouts then written (`screen/eng_blo/*.blo`; the generator writes 22 on 2026-10-28) and the 331 `.pcr` entries replaced, boot to the
stage and run the scenarios?

Method: copy the install's `dataDir` to a scratch directory, overwrite every
`screen/eng_blo/*.blo` the generator writes, delete the loose `effects/pcr/*.pcr`,
add the stand-in `effects/pcr/effects.bun`. Run
`PIKMIN_SCENARIO_DATA_DIR=<copy> PIKMIN_SCENARIO_STAGE=stages/stage1.ini nectar --scenario <name> --scenario-tick-rate <30|60>`
from a scratch working directory. The control is an unmodified copy of the same
install. RelWithDebInfo build, macOS, hidden window.

| Scenario | Rate | Overlay tree | Control (real install) |
| --- | --- | --- | --- |
| boot_smoke | 30 | exit 0, PASS | not run |
| boot_smoke | 60 | exit 0, PASS | not run |
| throw_release | 30 | exit 0, PASS | exit 0, PASS |
| throw_release | 60 | exit 1, FAIL at tick 712 (4 of 4 runs) | exit 0, PASS (4 of 4 runs) |
| day30_clock | 30 | exit 0, PASS | not run |
| day30_clock | 60 | exit 0, PASS | not run |

No `FAILED to open` outside the three `objects/ufoparts/mnaka1/naka1{a,f,s}.dck`
paths, which the install lacks too and the allowlist covers. No abort or crash.
The stand-in layouts survive the HUD flows of all three scenarios.

## The throw_release failure at 60

Splitting the overlay: layouts only passes; `.pcr` only fails the same way. So the
stand-in layouts are not the cause. With the stand-in `.pcr` the simulation
diverges from the real one (at 30 the calibration constant K is 8 instead of 13,
and the thrown distances differ), so the 60 tick case 1 never sees a Hanged Pikmin
in the window the scenario expects. The mechanism is not traced: the inert
particle payload (`kPcrDisableEmission`) is the only difference, so something in
the stage 1 simulation depends on effect emission. The scenario is written for
the sandbox stage, where the stand-in tree passes at both rates (ctest
`scenario_throw_release_60`); treat this as "the stand-in `.pcr` does not
reproduce the real effects' effect on the simulation", not as a writer bug.
Unverified: whether the divergence is RNG consumption, effect-driven collision,
or timing.

## Container menu

`con01.blo` now carries `p264` (a window, four corners `p2b_64.bti`, which is in
the stand-in `screen.bun`) and picture panes `ws8c`, `p2c4`, `ws8u`, `ws8l` (no
texture; `P2DPicture::setTexture` does nothing with zero textures), so
`DrawContainer::start` (`drawContainer.cpp:186-195`) finds every pane it
dereferences. The writer test pins the type of each and that the window is the
only one in the stand-in set. The `container_menu` scenario now opens and closes the menu on the
stand-in tree at 30 and 60 (`verification-log.md`, item 2).
