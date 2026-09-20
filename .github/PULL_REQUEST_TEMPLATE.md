<!-- A prompt sheet, not a form. Delete any section you have nothing to say in, and delete these
     comments as you go. Short is the goal: see "Write a short pull request description" in
     CONTRIBUTING.md. -->

# Summary

<!-- One or two sentences: what this change does and why. If that is hard to write, the PR is
     probably more than one change — see "Scope of a change". -->

<!-- Closing keyword on its own line, one per issue, if this finishes a tracked issue:
     Fixes #NNNN        (partial work: Refs #NNNN) -->

## Changes

<!-- A handful of bullets — one per real change, not one per file. Note which platforms or buses
     are affected, whether shared code (fujiDevice, NDevice, lib/bus/, lib/config/) changed, and
     any behaviour or wire-protocol change. Skip what the diff already says plainly. -->

-

## Notes

<!-- Only what a reviewer cannot get from the diff: a design decision and the alternative you
     rejected, follow-up work, a PR that must land first, an unrelated problem found in passing
     (with its issue number), a deliberate sdkconfig change. Delete this section if there is none. -->

## Testing

<!-- The section worth spending words on. Exact commands and targets, not prose. -->

- Host build + ctest (`./build.sh -p <TARGET>`):
- Firmware build (`./build.sh -b`), board(s):
- All boards (`./build.sh -a`, required if shared `lib/` or `src/main.cpp` changed):
- Flashed on hardware — board(s) and host machine(s):
- Tests added or updated in `tests/`:

**Expected:** <!-- what a reviewer should see if they repeat the above: behaviour before vs after,
ctest names that must pass, or the on-device behaviour to look for. -->

**Not tested:** <!-- Required. Which targets were not built, whether it was never flashed, whether
any test covers this. "Builds for ATARI and COCO, not flashed, no test coverage" is a fine answer
and far better than silence. -->
