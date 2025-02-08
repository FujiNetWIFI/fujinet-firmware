#include "SystemCommands.h"

#include <cstring>

#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <getopt.h>

#include <soc/efuse_reg.h>

#include <memory>
#include <soc/soc.h>
#include <esp_partition.h>

#include <soc/spi_reg.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_mac.h>
#include <esp_flash.h>

#include "../ESP32Console.h"

#include "../../../include/version.h"

#include "Esp.h"

EspClass ESP;

static std::string mac2String(uint64_t mac)
{
    uint8_t *ar = (uint8_t *)&mac;
    std::string s;
    for (uint8_t i = 0; i < 6; ++i)
    {
        char buf[3];
        sprintf(buf, "%02X", ar[i]); // J-M-L: slight modification, added the 0 in the format for padding
        s += buf;
        if (i < 5)
            s += ':';
    }
    return s;
}

static const char *getFlashModeStr()
{
    auto mode = ESP.getFlashChipMode();

    switch(mode)
    {
        case FM_QIO: return "QIO";
        case FM_QOUT: return "QOUT";
        case FM_DIO: return "DIO";
        case FM_DOUT: return "DOUT";
        case FM_FAST_READ: return "FAST READ";
        case FM_SLOW_READ: return "SLOW READ";
        default: return "DOUT";
    }
}

static const char *getResetReasonStr()
{
    switch (esp_reset_reason())
    {
    case ESP_RST_BROWNOUT:
        return "Brownout reset (software or hardware)";
    case ESP_RST_DEEPSLEEP:
        return "Reset after exiting deep sleep mode";
    case ESP_RST_EXT:
        return "Reset by external pin (not applicable for ESP32)";
    case ESP_RST_INT_WDT:
        return "Reset (software or hardware) due to interrupt watchdog";
    case ESP_RST_PANIC:
        return "Software reset due to exception/panic";
    case ESP_RST_POWERON:
        return "Reset due to power-on event";
    case ESP_RST_SDIO:
        return "Reset over SDIO";
    case ESP_RST_SW:
        return "Software reset via esp_restart";
    case ESP_RST_TASK_WDT:
        return "Reset due to task watchdog";
    case ESP_RST_WDT:
        return "ESP_RST_WDT";

    case ESP_RST_UNKNOWN:
    default:
        return "Unknown";
    }
}

static int sysInfo(int argc, char **argv)
{
    esp_chip_info_t info;
    esp_chip_info(&info);

    printf("FujiNet %s\r\n", FN_VERSION_FULL);
//    printf("ESP32Console version: %s\r\n", ESP32CONSOLE_VERSION);
//    printf("Arduino Core version: %s (%x)\r\n", XTSTR(ARDUINO_ESP32_GIT_DESC), ARDUINO_ESP32_GIT_VER);
    printf("ESP-IDF v%s\r\n", ESP.getSdkVersion());

    printf("\r\n");
    printf("Chip info:\r\n");
    printf("\tModel: %s\r\n", ESP.getChipModel());
    printf("\tRevison number: %d\r\n", ESP.getChipRevision());
    printf("\tCores: %d\r\n", ESP.getChipCores());
    printf("\tClock: %lu MHz\r\n", ESP.getCpuFreqMHz());
    printf("\tFeatures:%s%s%s%s%s\r\r\n",
           info.features & CHIP_FEATURE_WIFI_BGN ? " 802.11bgn " : "",
           info.features & CHIP_FEATURE_BLE ? " BLE " : "",
           info.features & CHIP_FEATURE_BT ? " BT " : "",
           info.features & CHIP_FEATURE_EMB_FLASH ? " Embedded-Flash " : " External-Flash ",
           info.features & CHIP_FEATURE_EMB_PSRAM ? " Embedded-PSRAM" : "");

    printf("EFuse MAC: %s\r\n", mac2String(ESP.getEfuseMac()).c_str());

    printf("Flash size: %ld MB (mode: %s, speed: %ld MHz)\r\n", ESP.getFlashChipSize() / (1024 * 1024), getFlashModeStr(), ESP.getFlashChipSpeed() / (1024 * 1024));
    printf("PSRAM size: %ld MB\r\n", ESP.getPsramSize() / (1024 * 1024));

#ifndef CONFIG_APP_REPRODUCIBLE_BUILD
    printf("Compilation datetime: " __DATE__ " " __TIME__ "\r\n");
#endif

    //printf("\nReset reason: %s\r\n", getResetReasonStr());

    //printf("\r\n");
    //printf("CPU temperature: %.01f °C\r\n", ESP.temperatureRead());

    return EXIT_SUCCESS;
}

static int restart(int argc, char **argv)
{
    printf("Restarting...");
    ESP.restart();
    return EXIT_SUCCESS;
}

static int meminfo(int argc, char **argv)
{
    uint32_t free = ESP.getFreeHeap() / 1024;
    uint32_t total = ESP.getHeapSize() / 1024;
    uint32_t used = total - free;
    uint32_t min = ESP.getMinFreeHeap() / 1024;
    uint32_t total_free = esp_get_free_heap_size() / 1024;

    printf("Internal Heap: %lu KB free, %lu KB used, (%lu KB total)\r\n", free, used, total);
    printf("Minimum free heap size during uptime was: %lu KB\r\n", min);
    printf("Overall Free Memory: %lu KB\r\n\r\n", total_free);

    total = ESP.getPsramSize() / 1024;
    free = ESP.getFreePsram() / 1024;
    used = total - free;    
    printf("PSRAM: %lu KB free, %lu KB used, (%lu KB total)\r\n", free, used, total);
    return EXIT_SUCCESS;
}

static int taskinfo(int argc, char **argv)
{
    printf( "Task Name\tStatus\tPrio\tHWM\tTask\tAffinity\r\r\n");
    char stats_buffer[1024];
    vTaskList(stats_buffer);
    printf("%s\r\r\n", stats_buffer);
    return EXIT_SUCCESS;
}

static int date(int argc, char **argv)
{
    bool set_time = false;
    char *target = nullptr;

    int c;
    opterr = 0;

    // Set timezone from env variable
    tzset();

    while ((c = getopt(argc, argv, "s")) != -1)
        switch (c)
        {
        case 's':
            set_time = true;
            break;
        case '?':
            printf("Unknown option: %c\r\n", optopt);
            return 1;
        case ':':
            printf("Missing arg for %c\r\n", optopt);
            return 1;
        }

    if (optind < argc)
    {
        target = argv[optind];
    }

    if (set_time)
    {
        if (!target)
        {
            fprintf(stderr, "Set option requires an datetime as argument in format '%%Y-%%m-%%d %%H:%%M:%%S' (e.g. 'date -s \"2022-07-13 22:47:00\"'\r\n");
            return 1;
        }

        tm t;

        if (!strptime(target, "%Y-%m-%d %H:%M:%S", &t))
        {
            fprintf(stderr, "Set option requires an datetime as argument in format '%%Y-%%m-%%d %%H:%%M:%%S' (e.g. 'date -s \"2022-07-13 22:47:00\"'\r\n");
            return 1;
        }

        timeval tv = {
            .tv_sec = mktime(&t),
            .tv_usec = 0};

        if (settimeofday(&tv, nullptr))
        {
            fprintf(stderr, "Could not set system time: %s", strerror(errno));
            return 1;
        }

        time_t tmp = time(nullptr);

        constexpr int buffer_size = 100;
        char buffer[buffer_size];
        strftime(buffer, buffer_size, "%a %b %e %H:%M:%S %Z %Y", localtime(&tmp));
        printf("Time set: %s\r\n", buffer);

        return 0;
    }

    // If no target was supplied put a default one (similar to coreutils date)
    if (!target)
    {
        target = (char*) "+%a %b %e %H:%M:%S %Z %Y";
    }

    // Ensure the format string is correct
    if (target[0] != '+')
    {
        fprintf(stderr, "Format string must start with an +!\r\n");
        return 1;
    }

    // Ignore + by moving pointer one step forward
    target++;

    constexpr int buffer_size = 100;
    char buffer[buffer_size];
    time_t t = time(nullptr);
    strftime(buffer, buffer_size, target, localtime(&t));
    printf("%s\r\n", buffer);
    return 0;

    return EXIT_SUCCESS;
}

namespace ESP32Console::Commands
{
    const ConsoleCommand getRestartCommand()
    {
        return ConsoleCommand("restart", &restart, "Restart / Reboot the system");
    }

    const ConsoleCommand getSysInfoCommand()
    {
        return ConsoleCommand("sysinfo", &sysInfo, "Shows informations about the system like chip model and ESP-IDF version");
    }

    const ConsoleCommand getMemInfoCommand()
    {
        return ConsoleCommand("meminfo", &meminfo, "Shows information about heap usage");
    }

    const ConsoleCommand getTaskInfoCommand()
    {
        return ConsoleCommand("ps", &taskinfo, "Shows information about running tasks");
    }

    const ConsoleCommand getDateCommand()
    {
        return ConsoleCommand("date", &date, "Shows and modify the system time");
    }
}