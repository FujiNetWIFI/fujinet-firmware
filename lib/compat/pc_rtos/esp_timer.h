// PC shim for ESP-IDF <esp_timer.h> (ADAM PC build only): a monotonic us clock.
#ifndef PC_RTOS_ESP_TIMER_H
#define PC_RTOS_ESP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque timer handle (declared but unused on PC).
typedef void *esp_timer_handle_t;

// Microseconds since program start (monotonic).
int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif

#endif // PC_RTOS_ESP_TIMER_H
