# lib/clipboard

Shared clipboard between the attached computer and the web interface: one current snippet plus a
short history, readable and writable from either side.

## Layout
| File | Defines |
|---|---|
| `clipboardManager.h`, `clipboardManager.cpp` | `ClipboardSnippet` (text, binary flag, source, timestamp), `ClipboardManager`, and the global `fnClipboard` |

## How it fits
- The computer reaches it through the `CLIPBOARD:` adapter in [lib/network-protocol/](../network-protocol/);
  the browser reaches it through both web servers in [lib/http/](../http/).
- Text is stored with LF line endings. The protocol adapter translates for the computer and the web
  UI normalises what the browser posts; snippets flagged binary are never translated. A snippet is
  capped at `CLIPBOARD_MAX_SIZE`.

## Build
ESP: globbed by `src/CMakeLists.txt` into every target. PC: every `FUJINET_TARGET`.
