# lib/device/adamnet

The Coleco ADAM device set for `BUILD_ADAM`.

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/adamnet/](../../bus/adamnet/) and implements
  `adamnet_control_send()` and `deviceStatus()`. Images come from
  [lib/media/adam/](../../media/adam/).
- `src/main.cpp` adds the printer (when enabled in config) and `platformClock`; there is no modem
  device on this platform.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_ADAM`. PC: `FUJINET_TARGET=ADAM`
lists every file.

## Notes
- `adamKeyboard` (`keyboard.cpp`) is used only behind `VIRTUAL_ADAM_DEVICES`.
