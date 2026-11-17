# lib/fnxml

XML query wrapper for the N: device: parses a fetched document with tinyxml2 and answers an
XPath-subset query.

## How it fits
- Wrapped by `XMLParser` in [lib/device/NDevice/](../device/NDevice/); reads the document through
  a `NetworkProtocol` from [lib/network-protocol/](../network-protocol/).
- Parsing is [lib/tinyxml2/](../tinyxml2/); query flags and sanitizing come from
  [lib/fntext/](../fntext/).

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`, plus the `fnxml_query_tests` ctest
(`tests/FnxmlQueryTests.cpp`).

## Notes
- `fnxml_query.*` is kept out of the protocol so it can be tested without one.
