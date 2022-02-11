/*
    FujiNet System Manager
    These are board/system-level routines, e.g. memory info, timers, hardware versions
*/
#ifndef FNSYSTEM_H
#define FNSYSTEM_H

#include <string>

#include <driver/gpio.h>

#include "fnFS.h"


#define FILE_COPY_BUFFERSIZE 2048

#define NOP() asm volatile("nop")

#define ESP_INTR_FLAG_DEFAULT 0

class SystemManager
{
private:
    char _uptime_string[18];
    char _currenttime_string[40];
    int _hardware_version = 0; // unknown

public:
    SystemManager();
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

        bool _sntp_initialized = false;
        unsigned long _sntp_last_sync = 0;

        std::string _get_ip4_address_str(_ip4_address_type iptype);
        std::string _get_ip4_dns_str(_ip4_dns_type dnstype);

        static void _sntp_time_sync_notification(struct timeval *tv);

    public:
        std::string get_hostname();
        std::string get_ip4_address_str();
        std::string get_ip4_mask_str();
        std::string get_ip4_gateway_str();
        int get_ip4_info(uint8_t ip4address[4], uint8_t ip4mask[4], uint8_t ip4gateway[4]);
        std::string get_ip4_dns_str();
        int get_ip4_dns_info(uint8_t ip4dnsprimary[4]);

        void start_sntp_client();
        void stop_sntp_client();
        void set_sntp_lastsync();
    };
    _net Net;

    enum chipmodels
    {
        CHIP_UNKNOWN = 0,
        CHIP_ESP32
    };

    enum pull_updown_t
    {
        PULL_NONE = 0,
        PULL_UP,
        PULL_DOWN,
        PULL_BOTH
    };

#define DIGI_LOW 0x00
#define DIGI_HIGH 0x01

    void set_pin_mode(uint8_t pin, gpio_mode_t mode, pull_updown_t pull_mode = PULL_NONE, gpio_int_type_t intr_type = GPIO_INTR_DISABLE);

    int digital_read(uint8_t pin);
    void digital_write(uint8_t pin, uint8_t val);

    void reboot();
    uint32_t get_cpu_frequency();
    uint32_t get_free_heap_size();
    uint32_t get_psram_size();
    const char *get_sdk_version();
    chipmodels get_cpu_model();
    int get_cpu_rev();
    int64_t get_uptime();
    unsigned long millis();
    unsigned long micros();
    void delay_microseconds(uint32_t us);
    void delay(uint32_t ms);

    const char *get_uptime_str();
    const char *get_current_time_str();
    void update_timezone(const char *timezone);
    void update_hostname(const char *hostname);

    const char *get_fujinet_version(bool shortVersionOnly = false);

    int get_sio_voltage();
    void yield();

    size_t copy_file(FileSystem *source_fs, const char *source_filename, FileSystem *dest_fs, const char *dest_filename, size_t buffer_hint = FILE_COPY_BUFFERSIZE);
    FILE *make_tempfile(FileSystem *fs, char *result_filename);
    FILE *make_tempfile(char *result_filename);
    void delete_tempfile(FileSystem *fs, const char *filename);
    void delete_tempfile(const char *filename);

    int load_firmware(const char *filename, uint8_t **buffer);
    void debug_print_tasks();

    void check_hardware_ver();
    int get_hardware_ver() { return _hardware_version; };
    const char *get_hardware_ver_str();
};

extern SystemManager fnSystem;

#endif // FNSYSTEM_H
