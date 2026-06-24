// PC shim for FreeRTOS <freertos/FreeRTOS.h> (ADAM PC build only).
#ifndef PC_RTOS_FREERTOS_H
#define PC_RTOS_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

// ADAM code uses esp_timer_get_time() alongside FreeRTOS; expose it here.
#include <esp_timer.h>

typedef int          BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t     TickType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0

#define portMAX_DELAY ((TickType_t)0xffffffffUL)

#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 1000
#endif
#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS ((TickType_t)(1000 / configTICK_RATE_HZ))
#endif
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms) / portTICK_PERIOD_MS)
#endif

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// Critical-section spinlock: a no-op stub on PC.
typedef struct { int dummy; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }

#ifdef __cplusplus
extern "C" {
#endif
void fn_pc_task_yield(void);
#ifdef __cplusplus
}
#endif
#define taskYIELD() fn_pc_task_yield()

// Expose task/queue handle types via the umbrella header, as ESP-IDF does.
#include "task.h"
#include "queue.h"

#endif // PC_RTOS_FREERTOS_H
