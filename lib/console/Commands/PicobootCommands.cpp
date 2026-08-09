#include "PicobootCommands.h"

#ifdef CONFIG_USB_PICOBOOT_HOST_ENABLED

#include <cstdio>
#include <cstdlib>

#include "../../hardware/PicobootClient.h"

namespace ESP32Console::Commands
{
    static int picobootFlash(int argc, char **argv)
    {
        if (argc < 2 || argc > 3) {
            fprintf(stderr, "usage: picoboot-flash <path-on-sd> [flash-addr-hex]\r\n"
                             "  path-on-sd: a raw .bin (not .uf2), e.g. /sd/fuji_intv.bin\r\n"
                             "  flash-addr-hex: defaults to 10000000 (RP2040 XIP flash base)\r\n"
                             "  Waits up to 10s for an RP2040 already sitting in BOOTSEL to attach.\r\n");
            return 1;
        }

        uint32_t addr = 0x10000000;
        if (argc == 3)
            addr = (uint32_t)strtoul(argv[2], NULL, 16);

        printf("picoboot-flash: waiting for RP2040 in BOOTSEL, flashing %s to 0x%08X...\r\n", argv[1], (unsigned int)addr);
        bool ok = picobootClient.flashBin(argv[1], addr, 10000);
        printf("picoboot-flash: %s\r\n", ok ? "OK" : "FAILED");
        return ok ? 0 : 1;
    }

    const ConsoleCommand getPicobootFlashCommand()
    {
        return ConsoleCommand("picoboot-flash", &picobootFlash,
                               "Reflash an RP2040 sitting in BOOTSEL over the USB host link",
                               "<path-on-sd> [flash-addr-hex]");
    }
}

#endif /* CONFIG_USB_PICOBOOT_HOST_ENABLED */
