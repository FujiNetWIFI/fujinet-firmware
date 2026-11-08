# lib/fnpretty

HTML-to-text renderer for the N: device: parses a document with Gumbo and lays it out as lightly
formatted plain text, word-wrapped to the client's screen width.

## Layout
| File | Defines |
|---|---|
| `fnpretty.h`, `fnpretty.cpp` | `FNPretty`: `parse` (fetch through a `NetworkProtocol` and render), `renderDocument`, `rerender`, `setQuery` (CSS selector), `setOption` (`!links=`, `!width=`, `!eol=`, `!gfx=`), `setScreenWidth`, `setLineEnding`, link collection (`linkUrl`), `attrOfFirst` (attribute of the first match of a selector), `available`, `readValue`; ASCII and ATASCII furniture |
| `fn_render_text.h`, `fn_render_text.cpp` | `fn_render_html_text(html, width, eol)`: one-shot render of an HTML fragment to ASCII text with its lines broken by `eol`, used by the query parsers' TEXT output mode |

## How it fits
- `PrettyParser` in [lib/device/NDevice/](../device/NDevice/) wraps `FNPretty` for `NET_SET_PARSER` mode 4.
- `FNXML`, `FNHTML` and `FNJSON` call `fn_render_html_text()` for `FN_QUERY_OUTPUT_TEXT`
  (see [lib/fntext/](../fntext/)).
- Text is folded to ASCII with [lib/fntext/](../fntext/); parsing is Gumbo plus gumbo-query from `components/`.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`, and linked by `fnpretty_tests` and
`nquery_output_mode_tests`.
