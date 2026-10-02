# lib/qrcode

QR code generation for the computer, rendered in whichever form the host can display.

## Layout
| File | Defines |
|---|---|
| `qrcode.h`, `qrcode.c` | the encoder, vendored unmodified from Richard Moore's QRCode library, derived from Project Nayuki (MIT) |
| `qrmanager.h`, `qrmanager.cpp` | `QRManager`: `encode` with a version and `qr_ecc_t` level, then `to_binary`, `to_bitmap`, `to_ansi`, `to_svg`, `to_atascii` or `to_petscii`; the global `qrManager` |

## How it fits
- Driven by `QRMixin` in [lib/device/fujiDevice/](../device/fujiDevice/), which maps the
  `FUJI_QRCODE_*` commands onto `qrManager`.

## Build
ESP: globbed into every target (`qrcode.c` is C). PC: every `FUJINET_TARGET`.
