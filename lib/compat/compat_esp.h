#ifndef COMPAT_ESP_H
#define COMPAT_ESP_H

#ifndef ESP_PLATFORM

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ      100
#endif

#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS      (1000 / configTICK_RATE_HZ)
#endif

#include <stdint.h>
#include <time.h>

// Microseconds from a monotonic clock, like ESP-IDF's esp_timer_get_time()
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// Reflected CRC-16/CCITT with inverted seed and result, like the ESP ROM's esp_rom_crc16_le()
static inline uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *buf, uint32_t len)
{
    crc = ~crc;
    while (len--)
    {
        crc ^= *buf++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    return ~crc;
}

#endif /* !ESP_PLATFORM */

#endif /* COMPAT_ESP_H */
