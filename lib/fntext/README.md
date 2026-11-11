# lib/fntext

Text handling shared by the N: device query wrappers: the query-parameter byte and the sanitising
that turns web text into something an 8-bit host can print.

## Layout
| File | Defines |
|---|---|
| `fn_query_flags.h` | `fnQueryFlags_t`: the remap bits (`FN_QUERY_REMAP_CHARS`, `FN_QUERY_REMAP_ATASCII_INTERNATIONAL`, `FN_QUERY_DELETE_SGML_TAGS`) and the output mode in bits 4-5 (`FN_QUERY_OUTPUT_VERBATIM` 0x00, `FN_QUERY_OUTPUT_ASCII` 0x10, `FN_QUERY_OUTPUT_TEXT` 0x20; 0x30 is reserved); `fn_query_param_is_valid()` |
| `fn_sanitize.h`, `fn_sanitize.cpp` | `fn_decode_entities` (numeric and a curated named-entity table), `fn_utf8_to_ascii`, `fn_sanitize_ascii`, `fn_remap_atari_ascii` and `fn_remap_atari_international` (the Atari remap tables) |

## Output modes
| Mode | Value | A queried value comes back as |
|---|---|---|
| VERBATIM | 0x00 | the document's own bytes (plus the optional Atari remap) |
| ASCII | 0x10 | entity-decoded, transliterated printable ASCII, no line breaks |
| TEXT | 0x20 | the value rendered as HTML into word-wrapped plain ASCII, lines joined by the parser's EOL (the bus's `nativeEOL()` or a `NET_SET_EOL` override, `$9B` on the Atari); wrap width is `NET_SET_PARAMETER` param 2 (0 = 40 columns). Rendered by `fn_render_html_text()` in [lib/fnpretty/](../fnpretty/); links are not rendered. |

The mode is chosen by the query parameter byte (`NET_SET_PARAMETER` param 0, or the flags byte
a bus sends with `NET_QUERY`). The parser appends its usual line ending after the value in every mode.

## How it fits
- `NDevice` validates the query parameter with `fn_query_param_is_valid`; [lib/fnjson/](../fnjson/),
  [lib/fnxml/](../fnxml/) and [lib/fnhtml/](../fnhtml/) interpret the same byte and post-process
  their results with the sanitiser.
- No parser or protocol dependency (the TEXT renderer lives in `lib/fnpretty` for that reason), so `tests/FnSanitizeTests.cpp` links it on its own.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`, plus the `fn_sanitize_tests` and
`nquery_output_mode_tests` ctests.

## Notes
Written without exceptions, `std::regex` or large tables so it fits the ESP firmware constraints.
