#include <Arduino.h> // Lets us get the Arduino framework version

#include <string.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <driver/gpio.h>

#include "../../include/version.h"

#include "fnSystem.h"

#include "debug.h"

// Global object to manage System
SystemManager fnSystem;

// Temprary (?) replacement for Arduino's pinMode()
// Handles only common cases
// PINMODE_INPUT or PINMODE_OUTPUT
// can be ORed with PINMODE_PULLUP or PINMODE_PULLDOWN
void SystemManager::set_pin_mode(uint8_t pin, uint8_t mode)
{
    gpio_config_t io_conf;

    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;


    // set pin mode
    if(mode & PINMODE_INPUT) 
    {
        io_conf.mode = GPIO_MODE_INPUT;

    } else if (mode & PINMODE_OUTPUT)
    {
        io_conf.mode = GPIO_MODE_OUTPUT;
    }
    else
    {
        // Make sure we have either PINMODE_INPUT or PINMODE_OUTPUT
        // Don't continue if we get something unexpected        
#ifdef DEBUG
        Debug_println("set_pin_mode mode isn't INPUT or OUTPUT");
#endif
        abort();
    }

    //set pull-up/down mode (only one or the other)
    io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_DISABLE;
    if(mode & PINMODE_PULLDOWN)
    {
        io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_ENABLE;
    } else if (mode & PINMODE_PULLUP)
    {
        io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_ENABLE;
    }

    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = (1ULL << pin);

    //configure GPIO with the given settings
    gpio_config(&io_conf);    

}

// from esp32-hal-misc.
// Set DIGI_LOW or DIGI_HIGH
void IRAM_ATTR SystemManager::digital_write(uint8_t pin, uint8_t val)
{
    if(val) {
        if(pin < 32) {
            GPIO.out_w1ts = ((uint32_t)1 << pin);
        } else if(pin < 34) {
            GPIO.out1_w1ts.val = ((uint32_t)1 << (pin - 32));
        }
    } else {
        if(pin < 32) {
            GPIO.out_w1tc = ((uint32_t)1 << pin);
        } else if(pin < 34) {
            GPIO.out1_w1tc.val = ((uint32_t)1 << (pin - 32));
        }
    }
}

// from esp32-hal-misc.
// Returns DIGI_LOW or DIGI_HIGH
int IRAM_ATTR SystemManager::digital_read(uint8_t pin)
{
    if(pin < 32) {
        return (GPIO.in >> pin) & 0x1;
    } else if(pin < 40) {
        return (GPIO.in1.val >> (pin - 32)) & 0x1;
    }
    return 0;
}

// from esp32-hal-misc.
void SystemManager::delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

// from esp32-hal-misc.c
unsigned long IRAM_ATTR SystemManager::millis()
{
     return (unsigned long) (esp_timer_get_time() / 1000ULL);
}

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
    // Configure ADC1_CH7
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_11db);

    // Calculate ADC characteristics
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    int samples = 10;
    uint32_t avgV = 0;
    uint32_t vcc = 0;

    for (int i = 0; i < samples; i++)
    {
        esp_adc_cal_get_voltage(ADC_CHANNEL_7, &adc_chars, &vcc);
        avgV += vcc;
        //delayMicroseconds(5);
    }

    avgV /= samples;

    if (avgV < 501)
        return 0;
    else
        return (avgV * 5900/3900); // SIOvoltage = Vadc*(R1+R2)/R2 (R1=2000, R2=3900)
}

