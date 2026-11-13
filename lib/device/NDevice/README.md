# lib/device/NDevice

The shared N: network device. A host program opens a URL on an N: unit, and `NDevice` turns the
`NET_*` commands into calls on a `NetworkProtocol` adapter, optionally through a parser that lets
an 8-bit program query one value out of a JSON, XML or HTML document.

## Layout

| File | Defines |
|---|---|
| `NDevice.h`, `NDevice.cpp` | `NDevice`, the static `dispatch_table` of `NET_*` handlers, `parse_and_instantiate_protocol()`, the `PARSER` and `PARSER_PARAM` enums |
| `NParser.h`, `NParser.cpp` | `NParser`, the strategy interface over a protocol (`read`, `write`, `available`, `seek`, `setQuery`, `parse`, `status`), and `NErrorParser`, bound after a failed open so `NET_STATUS` can report why |
| `QueryParser.h` | `QueryParser`, the base of the three query parsers: a query replaces the unread result, and `seek`/`tell` move within a copy of it |
| `JSONParser.h`, `.cpp` | `JSONParser` over `FNJSON` from [lib/fnjson/](../../fnjson/) |
| `XMLParser.h`, `.cpp` | `XMLParser` over `FNXML` from [lib/fnxml/](../../fnxml/) |
| `HTMLParser.h`, `.cpp` | `HTMLParser` over `FNHTML` from [lib/fnhtml/](../../fnhtml/) |
| `PrettyParser.h`, `.cpp` | `PrettyParser` over `FNPretty` from [lib/fnpretty/](../../fnpretty/): the whole page rendered to word-wrapped text and read back sequentially (`PARSER::PRETTY`, mode 4) |

## How it works

- `processCommand()` and `recognizesCommand()` look the command up in `dispatch_table`, which
  covers open, close, read, write, status, the parser and query commands, translation and EOL,
  seek and tell, prefix handling, the filesystem operations (rename, delete, lock, mkdir, ...) and
  the TCP server commands.
- `NET_OPEN` parses the URL with `PeoplesUrlParser` and asks
  `NetworkProtocolFactory::createProtocol()` in [lib/network-protocol/](../../network-protocol/)
  for the adapter matching the scheme. The adapter owns the receive, transmit and special buffers.
- `NET_SET_PARAMETER` sets a parser parameter: 0 = query flags, 1 = line ending byte, 2 = text
  width in columns (0 = default 40; the PRETTY screen width, or the TEXT output mode's wrap width)), 3 = PRETTY only: furniture in the bus's native graphics set, ATASCII/MouseText (0/1), 4 = PRETTY only: number each link `[n]` (0/1). Other parsers reject 3 and 4.
- `NET_SET_PARSER` swaps the `NParser` (0 none, 1 JSON, 2 HTML, 3 XML, 4 PRETTY); with no parser set, reads and writes pass straight through
  to the protocol. PRETTY queries: digits = link URL, `sel@attr` = attribute of the first match of `sel` (both staged ahead of the page text, which then resumes), anything else = CSS selector for the section to show.
- A query on the JSON, XML and HTML parsers replaces whatever is unread: the receive buffer holds only
  the new result. They keep a copy of the current result (as served, after native-charset conversion)
  and support `NET_SEEK` (`SEEK_SET`: the unread data becomes `result[offset..]`, clamped to the length)
  and `NET_TELL` (result length minus unread bytes). `network_seek(ds, 0xFFFFFFFF)` clears the unread
  data. Other whence values and the other parsers return an error for `NET_SEEK`, so a client can probe
  for the feature on older firmware.
- `NET_SET_PARSER` to PRETTY after a JSON/XML/HTML query renders that query's value instead of fetching:
  `PrettyParser` takes over the query parser and the value as it is in the document (`NParser::rawValue()`:
  no output mode, no remapping, no charset conversion; JSON uses the string value, XML has entities and
  CDATA already resolved by tinyxml2, so an RSS `description` is its HTML text). The receive buffer is
  cleared; `PARSE` then renders the value, and links, sections, `sel@attr`, width and the other params
  work as for a page, with the opened URL as base. Setting the saved parser's mode again restores it with
  its document and query state; any other mode builds a fresh parser and drops it. PRETTY with no
  query parser, or before a query, fetches from the protocol as before.
- Platform differences are the `fujidev_*` virtuals (`fujidev_read`, `fujidev_write`,
  `fujidev_status`, `fujidev_set_prefix`, `fujidev_get_prefix`, `fujidev_set_query`,
  `fujidev_set_parser`, `fujidev_seek`, `fujidev_tell`) and `dir_long_width()`.

Subclasses: `sioNetwork`, `iwmNetwork`, `drivewireNetwork`, `rs232Network`, `adamNetwork`,
`lynxNetwork` and `iecNetwork`, each in its bus directory under [lib/device/](../). The legacy
`H89Network`, `rc2014Network` and `s100spiNetwork` predate this class and do not use it.

## How it fits

- Instantiated per unit by each platform's `<bus>Fuji::setup()` (or lazily by the DriveWire bus)
  and registered on `SYSTEM_BUS` with a `NETWORK` device ID.
- Query flags shared with the parsers live in [lib/fntext/](../../fntext/).
- `tests/sioNetwork-DSTATS-test.cpp` and `tests/NQueryOutputModeTests.cpp` drive the command
  handlers on the host; the `no_build_ifdefs_in_fujidevice` ctest also covers this directory.

## Build

Compiled into every ESP target and every PC target; like `fujiDevice` it needs the active bus to
define `FUJI_COMMAND_PACKET`.
