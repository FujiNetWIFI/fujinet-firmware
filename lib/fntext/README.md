# lib/fntext

Text handling shared by the N: device query wrappers: the query-parameter byte and the sanitizing
that turns web text into something an 8-bit host can print.

## How it fits
- `NDevice` validates the query parameter with `fn_query_param_is_valid`; [lib/fnjson/](../fnjson/),
  [lib/fnxml/](../fnxml/) and [lib/fnhtml/](../fnhtml/) interpret the same byte and post-process
  their results with the sanitizer.
- No parser or protocol dependency, so `tests/FnSanitizeTests.cpp` links it on its own.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`, plus the `fn_sanitize_tests` and
`nquery_output_mode_tests` ctests.

## Notes
Written without exceptions, `std::regex` or large tables so it fits the ESP firmware constraints.
