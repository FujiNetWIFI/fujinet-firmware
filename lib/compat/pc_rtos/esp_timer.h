// PC shim for ESP-IDF <esp_timer.h>, used only by the ADAM PC build.
// The AdamNet bus and devices time everything with esp_timer_get_time(); on the
// ESP this comes from ESP-IDF, on PC we provide a monotonic microsecond clock.
//
// This directory (lib/compat/pc_rtos) is added to the include path ONLY for the
// ADAM PC target, so it never shadows the real ESP-IDF headers.
#ifndef PC_RTOS_ESP_TIMER_H
#define PC_RTOS_ESP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque timer handle (a network device declares one but never starts it on PC).
typedef void *esp_timer_handle_t;

// Microseconds since program start (monotonic).
int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif

#endif // PC_RTOS_ESP_TIMER_H
