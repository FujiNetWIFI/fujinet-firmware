# tests

Host-side unit tests and policy checks for the FujiNet-PC build. ctest runs them at the end of
`./build.sh -p <TARGET>` and in the Build FujiNet-PC workflow.

## How it fits
- Added by the root `CMakeLists.txt` after `fujinet_pc.cmake`, only when `FUJINET_TARGET` is set.
- doctest comes from [components_pc/doctest/](../components_pc/doctest/); the test file that owns
  `main()` defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`.

## Build
PC only. `make atari-lwm` (or `./build.sh -p ATARI`) configures, builds and runs `ctest -V`; a
failing test aborts the whole invocation. The firmware workflows compile and never run tests.

## Notes
To add a test, add an executable and an `add_test` to `CMakeLists.txt`, and keep the unit under
test free of hardware and FujiNet globals so it links alone.

`corpus_manifest.txt` records the provenance of the StuffIt sample archives; nothing reads it.
