# lib/fntext

Text handling shared by the N: device query wrappers: the query-parameter byte and the sanitising
that turns web text into something an 8-bit host can print.

## Layout
| File | Defines |
|---|---|
| `fn_query_flags.h` | `fnQueryFlags_t`: the remap bits (`FN_QUERY_REMAP_CHARS`, `FN_QUERY_REMAP_ATASCII_INTERNATIONAL`, `FN_QUERY_DELETE_SGML_TAGS`) and the output mode (`FN_QUERY_OUTPUT_VERBATIM`, `FN_QUERY_OUTPUT_ASCII`); `fn_query_param_is_valid()` |
| `fn_sanitize.h`, `fn_sanitize.cpp` | `fn_decode_entities` (numeric and a curated named-entity table), `fn_utf8_to_ascii`, `fn_sanitize_ascii` |

## How it fits
- `NDevice` validates the query parameter with `fn_query_param_is_valid`; [lib/fnjson/](../fnjson/),
  [lib/fnxml/](../fnxml/) and [lib/fnhtml/](../fnhtml/) interpret the same byte and post-process
  their results with the sanitiser.
- No parser or protocol dependency, so `tests/FnSanitizeTests.cpp` links it on its own.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`, plus the `fn_sanitize_tests` and
`nquery_output_mode_tests` ctests.

## Notes
Written without exceptions, `std::regex` or large tables so it fits the ESP firmware constraints.
