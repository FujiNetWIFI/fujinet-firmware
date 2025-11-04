/*
    FujiNet System Manager
    These are board/system-level routines, e.g. memory info, timers, hardware versions
*/
#ifndef FNSYSTEM_H
#define FNSYSTEM_H

#include <string>
#include <cstdint>

#ifdef ESP_PLATFORM
#include <driver/gpio.h>
#include <esp_timer.h>
#else
#include <signal.h>
#endif

#include "../FileSystem/fnFS.h"

// from sysexits.h
// #define EX_TEMPFAIL     75      /* temp failure; user is invited to retry */
// exit code should be monitored by parent process and FN restarted if ended with 75
#define EXIT_AND_RESTART 75

#define FILE_COPY_BUFFERSIZE 2048

#define NOP() asm volatile("nop")

#define ESP_INTR_FLAG_DEFAULT 0

class SystemManager
{
private:
    char _uptime_string[24];
    char _currenttime_string[40];
    int _hardware_version = 0; // unknown
    bool a2hasbuffer = false;
    bool a2no3state = false;
    bool ledstrip_found = false;
#ifdef ESP_PLATFORM
    gpio_num_t safe_reset_gpio = GPIO_NUM_NC;
#else
    char _uname_string[128];
    uint64_t _reboot_at = 0;
    int _reboot_code = EXIT_AND_RESTART;
    volatile sig_atomic_t _shutdown_requests = 0;
#endif

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

#ifdef ESP_PLATFORM
    void set_pin_mode(uint8_t pin, gpio_mode_t mode, pull_updown_t pull_mode = PULL_NONE, gpio_int_type_t intr_type = GPIO_INTR_DISABLE);
#endif

    int digital_read(uint8_t pin);
    void digital_write(uint8_t pin, uint8_t val);

#ifdef ESP_PLATFORM
    void reboot();
#else
    void reboot(uint32_t delay_ms = 0, bool reboot=true);
    bool check_deferred_reboot();
    int request_for_shutdown();
    int check_for_shutdown();
#endif
    uint32_t get_cpu_frequency();
    uint32_t get_free_heap_size();
    uint32_t get_psram_size();
    const char *get_sdk_version();
    chipmodels get_cpu_model();
    int get_cpu_rev();
#ifdef ESP_PLATFORM
    int64_t get_uptime();
    unsigned long millis();
    unsigned long micros();
#else
    uint64_t get_uptime();
    uint64_t millis();
    uint64_t micros();
#endif
    void delay_microseconds(uint32_t us);
    void delay(uint32_t ms);

    const char *get_uptime_str();
    const char *get_current_time_str();
    void update_timezone(const char *timezone);
    void update_hostname(const char *hostname);
    void update_firmware();

    const char *get_fujinet_version(bool shortVersionOnly = false);

#ifndef ESP_PLATFORM
    const char *get_uname();
#endif

    int get_sio_voltage();
    void yield();

    size_t copy_file(FileSystem *source_fs, const char *source_filename, FileSystem *dest_fs, const char *dest_filename, size_t buffer_hint = FILE_COPY_BUFFERSIZE);
    FILE *make_tempfile(FileSystem *fs, char *result_filename);
    FILE *make_tempfile(char *result_filename);
    void delete_tempfile(FileSystem *fs, const char *filename);
    void delete_tempfile(const char *filename);

    int load_firmware(const char *filename, uint8_t *buffer);
    void debug_print_tasks();

    void check_hardware_ver();
    int get_hardware_ver() { return _hardware_version; };
    const char *get_hardware_ver_str();
    const char *get_target_platform_str();

    bool hasbuffer() { return a2hasbuffer; };
    bool spishared() { return !a2hasbuffer; };
    bool no3state() { return a2no3state; };
    bool ledstrip() { return ledstrip_found; };
    bool has_button_c();
#ifdef ESP_PLATFORM
    gpio_num_t get_safe_reset_gpio() { return safe_reset_gpio; };
#endif
};

extern SystemManager fnSystem;

#endif // FNSYSTEM_H
