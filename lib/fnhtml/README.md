# lib/fnhtml

HTML query wrapper for the N: device: parses a fetched document with Gumbo and answers a CSS
selector query with the matching text or attribute.

## Layout
| File | Defines |
|---|---|
| `fnhtml.h`, `fnhtml.cpp` | `FNHTML`: `setProtocol`, `setReadQuery`, `parse`, `status`, `readValueLen`, `readValue`, `available`, `setLineEnding`; the same shape as `FNJSON` and `FNXML` |

## How it fits
- Wrapped by `HTMLParser` in [lib/device/NDevice/](../device/NDevice/), which is what the
  `NET_SET_PARSER` command selects; reads the document through a `NetworkProtocol` from
  [lib/network-protocol/](../network-protocol/).
- Parsing is Gumbo plus gumbo-query (`CDocument`, `Selection`) from `components/gumbo` and
  `components/gumbo-query`; result text goes through [lib/fntext/](../fntext/) for entity decoding
  and ASCII folding.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`; the gumbo sources are compiled from
`components/` on both sides.
