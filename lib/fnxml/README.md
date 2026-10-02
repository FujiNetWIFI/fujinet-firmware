# lib/fnxml

XML query wrapper for the N: device: parses a fetched document with tinyxml2 and answers an
XPath-subset query.

## Layout
| File | Defines |
|---|---|
| `fnxml.h`, `fnxml.cpp` | `FNXML`: `setProtocol`, `setReadQuery`, `parse`, `status`, `readValueLen`, `readValue`, `available`, `setLineEnding`; repeating a query steps to the next match |
| `fnxml_query.h`, `fnxml_query.cpp` | `fnxml_resolve_query`, the XPath subset (`/` steps, `//` descendants, `*`, `[n]`, `[@attr]`, `[@attr="v"]`, `[text()="v"]`, trailing `@attr`) kept separate so it can be tested without a protocol |

## How it fits
- Wrapped by `XMLParser` in [lib/device/NDevice/](../device/NDevice/); reads the document through
  a `NetworkProtocol` from [lib/network-protocol/](../network-protocol/).
- Parsing is [lib/tinyxml2/](../tinyxml2/); query flags and sanitising come from
  [lib/fntext/](../fntext/).

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`, plus the `fnxml_query_tests` ctest
(`tests/FnxmlQueryTests.cpp`).
