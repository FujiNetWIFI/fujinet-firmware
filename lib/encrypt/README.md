# lib/encrypt

Reversible obfuscation of the WiFi passphrases written to the config file (`CONFIG_FILENAME` in
[lib/config/](../config/)). It is not encryption in any cryptographic sense.

## Layout
| File | Defines |
|---|---|
| `crypt.h`, `crypt.cpp` | `Crypto` (`setkey`, `crypt`), a symmetric printable-character cipher taken from MicroEMACS, and the global `crypto` |

## How it fits
- `src/main.cpp` sets the key from the device MAC address at boot.
- `fnConfig` in [lib/config/](../config/) runs stored passphrases through `crypto.crypt()` on load
  and save. `README_wifi.md` at the repository root describes recovery when the key changes.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.
