/*
    FujiNet System Manager
    These are board/system-level routines, e.g. memory info, timers, hardware versions
*/
#ifndef FNSYSTEM_H
#define FNSYSTEM_H

#include <cstdint>
#include <string>

#include <driver/adc.h>
#include "esp_adc_cal.h"

// This is normally defineid in Arduino.h
typedef uint8_t byte;

class SystemManager
{
private:
    char _uptime_string[14];

public:

    class _net
    {
    private:
        enum _ip4_address_type
        {
            IP4_PRIMARY = 0,
            IP4_PRIMARYMASK,
            IP4_GATEWAY
        };

        enum _ip4_dns_type
        {
            IP4_DNS_PRIMARY = 0
        };

        static std::string _get_ip4_address_str(_ip4_address_type iptype);
        static std::string _get_ip4_dns_str(_ip4_dns_type dnstype);

    public:
        static std::string get_hostname();
        static std::string get_ip4_address_str();
        static std::string get_ip4_mask_str();
        static std::string get_ip4_gateway_str();
        static int get_ip4_info(uint8_t ip4address[4], uint8_t ip4mask[4], uint8_t ip4gateway[4]);
        static std::string get_ip4_dns_str();
        static int get_ip4_dns_info(uint8_t ip4dnsprimary[4]);
    };
    _net Net;

    enum chipmodels
    {
        CHIP_UNKNOWN = 0,
        CHIP_ESP32
    };

#define PINMODE_INPUT 0x01
#define PINMODE_OUTPUT 0x02
#define PINMODE_PULLDOWN 0x10
#define PINMODE_PULLUP 0x20

#define DIGI_LOW 0x00
#define DIGI_HIGH 0x01

    static void set_pin_mode(uint8_t pin, uint8_t mode);
    static int digital_read(uint8_t pin);
    static void digital_write(uint8_t pin, uint8_t val);

    static void reboot();
    static uint32_t get_free_heap_size();
    static const char * get_sdk_version();
    static chipmodels get_cpu_model();
    static int get_cpu_rev();
    static int64_t get_uptime();
    static unsigned long millis();
    static unsigned long micros();
    static void delay_microseconds(uint32_t us);
    static void delay(uint32_t ms);
    const char * get_uptime_str();
    static const char * get_fujinet_version();
    static int get_sio_voltage();
    static void yield();
};


extern SystemManager fnSystem;

#endif // FNSYSTEM_H
