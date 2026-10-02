# lib/fnjson

JSON query wrapper for the N: device: parses a fetched document with cJSON and answers a
JSON Pointer query with the value as text.

## Layout
| File | Defines |
|---|---|
| `fnjson.h`, `fnjson.cpp` | `FNJSON`: `setProtocol`, `setReadQuery`, `parse`, `status`, `readValueLen`, `readValue`, `available`, `setLineEnding`; `JSONQueryFlags_t` aliases the shared query flags |

## How it fits
- Wrapped by `JSONParser` in [lib/device/NDevice/](../device/NDevice/); reads the document through
  a `NetworkProtocol` from [lib/network-protocol/](../network-protocol/).
- Queries are resolved with `cJSON_Utils` pointers. cJSON comes from the ESP-IDF `json`
  component on ESP and from `components_pc/cJSON` on PC.
- Query flags and text sanitising are shared with the XML and HTML wrappers through
  [lib/fntext/](../fntext/).

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.

## Notes
The legacy H89 and RC2014 network devices include this header directly instead of through
`NDevice`.
