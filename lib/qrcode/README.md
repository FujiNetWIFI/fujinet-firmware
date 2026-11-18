# lib/qrcode

QR code generation for the computer, rendered in whichever form the host can display.

## How it fits
- Driven by `QRMixin` in [lib/device/fujiDevice/](../device/fujiDevice/), which maps the
  `FUJI_QRCODE_*` commands onto `qrManager`.

## Build
ESP: globbed into every target (`qrcode.c` is C). PC: every `FUJINET_TARGET`.

## Notes
- `qrcode.h` and `qrcode.c` are vendored unmodified from Richard Moore's QRCode library, derived
  from Project Nayuki (MIT).
