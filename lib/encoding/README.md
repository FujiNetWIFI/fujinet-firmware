# lib/encoding

Base64 and message digests (MD5, SHA-1, SHA-224/256/384/512, HMAC) over mbedTLS.

## Layout
| File | Defines |
|---|---|
| `base64.h`, `base64.cpp` | `Base64` with standard and URL-safe alphabets (`encode`, `url_encode`, `decode`, `url_decode`); RFC 1341 code converted to C++ |
| `hash.h`, `hash.cpp` | `Hash`: accumulate with `add_data`, then `compute` and read `output_hex` or `output_binary`; a one-shot static `compute`; HMAC when `key` is set. Handles mbedTLS 2, 3 and 4 (PSA). Global `hasher` |

## How it fits
- `Base64Mixin` and `HashMixin` in [lib/device/fujiDevice/](../device/fujiDevice/) expose both to
  the computer; `fnPassword` in [lib/config/](../config/) hashes the web password.
- Network adapters in [lib/network-protocol/](../network-protocol/) use them for credentials and
  request signing (S3, GMAIL, IMAPS), as does HTTP authentication in
  [lib/fn_esp_http_client/](../fn_esp_http_client/).

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.
