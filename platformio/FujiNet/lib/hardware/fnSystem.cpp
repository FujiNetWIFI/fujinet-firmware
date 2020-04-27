#include <Arduino.h> // Lets us get the Arduino framework version

#include <string.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "../../include/version.h"

#include "fnSystem.h"

#include "debug.h"

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

int SystemManager::get_sio_voltage()
{
    // Configure ADC1 CH7
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC1_CHANNEL_7,ADC_ATTEN_11db);

    // Calculate ADC characteristics
    esp_adc_cal_characteristics_t characteristics;
    esp_adc_cal_get_characteristics(1100, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, &characteristics);

    int i, samples = 10;
    uint32_t avgV = 0;

    for (i = 0; i < samples; i++)
        avgV += adc1_to_voltage(ADC1_CHANNEL_7, &characteristics);

    avgV /= samples;

    if ((avgV <= 0) || (avgV < 501)) // ignore spurious readings
        return 0;
    else
        return (avgV*5900/3900); // SIOvoltage = Vadc*(R1+R2)/R2 (R1=2000, R2=3900)
}

#ifdef NOT_DEPRECATED_ESP_ADC_FN
// The above function uses deprecated esp-idf adc functions, but it works for now.
// The following version of the same function works for a short time then causes
// the ESP to crash. The esp-idf functions used below are not deprecated. Leaving
// this here for future testing. Perhaps it's an upstream bug that needs fixed, or
// I'm doing something wrong?
int SystemManager::get_sio_voltage()
{
    // Configure ADC1 CH7
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC1_CHANNEL_7,ADC_ATTEN_11db);

    // Calculate ADC characteristics
    static esp_adc_cal_characteristics_t *adc_chars;
    adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, adc_chars);

    int i, samples = 10;
    uint32_t avgV = 0;
    uint32_t vcc = 0;

    for (i = 0; i < samples; i++)
    {
        esp_adc_cal_get_voltage(ADC_CHANNEL_7, adc_chars, &vcc);
        avgV += vcc;
        //delayMicroseconds(5);
    }

    avgV /= samples;

    if ((avgV <= 0) || (avgV < 501)) // ignore spurious readings
        return 0;
    else
        return (avgV*5900/3900); // SIOvoltage = Vadc*(R1+R2)/R2 (R1=2000, R2=3900)
}
#endif
