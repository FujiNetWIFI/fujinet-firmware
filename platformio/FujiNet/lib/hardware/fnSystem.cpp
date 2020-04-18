#include <string.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "fnSystem.h"

// Global object to manage System
SystemManager fnSystem;

void SystemManager::reboot()
{
    esp_restart();
}

/* Size of available heap. Size of largest contiguous block may be smaller.
*/
uint32_t SystemManager::get_free_heap_size()
{
    return esp_get_free_heap_size();
}

/* Microseconds since system boot-up
*/
int64_t SystemManager::get_uptime()
{
    return esp_timer_get_time();
}

const char * SystemManager::get_sdk_version()
{
    return esp_get_idf_version();
}

const char * SystemManager::get_fujinet_version()
{
    return FUJINET_VERSION;
}
int SystemManager::get_cpu_rev()
{
    esp_chip_info_t chipinfo;
    esp_chip_info(&chipinfo);
    return chipinfo.revision;
}

SystemManager::chipmodels SystemManager::get_cpu_model()
{
    esp_chip_info_t chipinfo;
    esp_chip_info(&chipinfo);

    switch(chipinfo.model)
    {
    case esp_chip_model_t::CHIP_ESP32:
        return chipmodels::CHIP_ESP32;
        break;
    default:
        return chipmodels::CHIP_UNKNOWN;
        break;
    }
}
