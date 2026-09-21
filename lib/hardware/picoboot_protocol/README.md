# Vendored PICOBOOT headers

`picoboot.h` and `picoboot_constants.h` are copied **verbatim** (byte-identical) from the
Raspberry Pi Pico SDK 2.3.0:

    <pico-sdk>/src/common/boot_picoboot_headers/include/boot/

They describe the PICOBOOT USB protocol that an RP2040/RP2350/RP2354 bootrom speaks while the
chip is in BOOTSEL mode, and are used by `lib/hardware/PicobootClient.cpp` to reflash the
companion MCU over the ESP32-S3's USB host port. They carry the RP2350 additions
(`PC_REBOOT2`, `PC_GET_INFO`, `REBOOT2_FLAG_*`), so both chip families are covered.

SPDX-License-Identifier: BSD-3-Clause
Copyright (c) 2020 Raspberry Pi (Trading) Ltd.

They are vendored rather than included from `$PICO_SDK_PATH` because the ESP32 build has no
pico-sdk dependency and must build on a machine that never installs one. Re-sync them only
against a pico-sdk release, and keep them byte-identical so the diff stays reviewable:

```sh
diff -u lib/hardware/picoboot_protocol/picoboot.h \
        "$PICO_SDK_PATH/src/common/boot_picoboot_headers/include/boot/picoboot.h"
```

`PicobootClient.cpp` defines `NO_PICO_PLATFORM` and supplies the few `__packed`/`__aligned`
shims these headers expect, so nothing else from the SDK is needed.
