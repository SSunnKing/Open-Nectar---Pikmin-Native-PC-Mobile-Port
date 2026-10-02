# Open Nectar 0.8.5 - fix

A small fix release on top of 0.8.5. Saves and settings carry over.

## Fixes

- **A day is as long as the original again (#50).** The option labelled
  "original" gave 10 minutes of play; the real game runs 13 minutes 30
  seconds. The default now matches it and the menu reads "13.5 min
  (original)". Settings that still hold the old default switch over on
  their own; if you had picked 10 minutes on purpose, pick it once more.
  Hard mode keeps its 8-minute cap.
- **Final results line up on wide screens (#49).** The glow around the
  tally numbers now sits on its frames instead of off to the left.
- **The Ship Parts list no longer shows the screen behind it (#49).** Its
  black background now covers the whole screen, and the Japanese part names
  the original layout keeps off-screen no longer show at the right edge.
