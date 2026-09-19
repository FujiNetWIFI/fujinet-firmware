# AGENTS.md

The development standards for this repository are in
[`CONTRIBUTING.md`](CONTRIBUTING.md), with the detailed C++ and code-structure rules in
[`docs/cpp-style.md`](docs/cpp-style.md). **Read `CONTRIBUTING.md` before changing anything.** It is
the single source of truth; this file only points at it and repeats the few rules that cause the
most damage when missed.

- Build with `make build` or `./build.sh -b`; the target comes from `build_board` in the
  git-ignored `platformio.local.ini`. `-f` is upload-filesystem, not flash.
- `make atari-lwm` (or `./build.sh -p ATARI`) runs the host build and the only automated tests
  there are. Use it before proposing a change.
- Never hand-edit `platformio-generated.ini`, `platformio.ini`,
  `platformio-ini-files/platformio.common.ini`, `managed_components/` or `data/BUILD_*/`. They are
  generated.
- `sdkconfig.*` and `include/version.h` are listed in `.gitignore` but tracked, and builds rewrite
  `sdkconfig.<env>`. Check `git diff --stat` and never commit incidental sdkconfig churn.
- Never return a bare `bool` for success or failure — use `success_is_true` / `error_is_true` from
  `include/global_types.h` and test with `.is_success()` / `.is_error()`. Compare a `fujiError_t`
  against `FUJI_ERROR::NONE`, never `FUJI_ERROR::UNSPECIFIED`.
- Use the `u16le_t` / `u24be_t` family for wire data instead of bit shifts or `htole*()`.
- Extend the shared `fujiDevice` / `NDevice` bases by overriding a virtual. A `BUILD_*` macro in a
  preprocessor conditional under those directories fails a test.
- Keep comments short, keep one concern per pull request, and re-read the comments in your diff
  before committing to confirm the change did not make them untrue.

@CONTRIBUTING.md
