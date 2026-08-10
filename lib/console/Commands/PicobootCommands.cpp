#include "PicobootCommands.h"

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <cstdio>
#include <cstdlib>

#include "../../hardware/PicobootClient.h"

namespace ESP32Console::Commands
{
    // The RP2040 image is no longer a file argument -- it's the fuji_intv.bin
    // linked into this ESP32-S3 binary at build time (build_pico_intv.py +
    // src/CMakeLists.txt), so there's nothing left to pick a path for. This
    // does the same thing autoFlashTask does automatically on BOOTSEL
    // attach (PicobootClient.cpp); useful mainly to retry after a failed
    // auto-flash without a full reboot cycle.
    static int picobootFlash(int argc, char **argv)
    {
        if (argc > 2) {
            fprintf(stderr, "usage: picoboot-flash [flash-addr-hex]\r\n"
                             "  flash-addr-hex: defaults to 10000000 (RP2040 XIP flash base)\r\n"
                             "  Flashes the fuji_intv.bin embedded in this firmware. Waits up to\r\n"
                             "  10s for an RP2040 already sitting in BOOTSEL to attach.\r\n");
            return 1;
        }

        uint32_t addr = 0x10000000;
        if (argc == 2)
            addr = (uint32_t)strtoul(argv[1], NULL, 16);

        printf("picoboot-flash: waiting for RP2040 in BOOTSEL, flashing embedded image to 0x%08X...\r\n", (unsigned int)addr);
        bool ok = picobootClient.flashEmbedded(addr, 10000);
        printf("picoboot-flash: %s\r\n", ok ? "OK" : "FAILED");
        return ok ? 0 : 1;
    }

    const ConsoleCommand getPicobootFlashCommand()
    {
        return ConsoleCommand("picoboot-flash", &picobootFlash,
                               "Reflash an RP2040 sitting in BOOTSEL with this firmware's embedded image",
                               "[flash-addr-hex]");
    }
}

#endif /* CONFIG_USB_PICOBOOT_HOST_ENABLED */
