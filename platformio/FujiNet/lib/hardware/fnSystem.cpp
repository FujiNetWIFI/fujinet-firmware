#include <Arduino.h> // Lets us get the Arduino framework version

#include <string.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "../../include/version.h"

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

const char * SystemManager::get_uptime_str()
{
    int64_t ms = esp_timer_get_time();

    long ml = ms / 1000;
    long s = ml / 1000;
    int m = s / 60;
    int h = m / 60;

    if(h > 0)
        snprintf(_uptime_string, sizeof(_uptime_string), "%.2d:%.2d:%.2ld.%.3ld", h, m%60, s%60, ml%1000);
    else
        snprintf(_uptime_string, sizeof(_uptime_string), "%.2d:%.2ld.%.3ld", m, s%60, ml%1000);

    return _uptime_string;
}

const char * SystemManager::get_sdk_version()
{
#ifdef ARDUINO
    static char _version[60];
	int major = ARDUINO / 10000;
	int minor = (ARDUINO % 10000) / 100;
	int patch = ARDUINO % 100;
    snprintf(_version, sizeof(_version), "%s; Arduino %d.%.d.%.d", esp_get_idf_version(), major, minor, patch );
    return _version;
#else    
    return esp_get_idf_version();
#endif    
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
