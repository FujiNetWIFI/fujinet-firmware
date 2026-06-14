// PC shim for ESP-IDF/FreeRTOS <freertos/FreeRTOS.h>, used only by the ADAM PC
// build. Provides the small subset of FreeRTOS types/macros the AdamNet bus and
// devices reference. See pc_rtos.cpp for the queue/task implementations.
#ifndef PC_RTOS_FREERTOS_H
#define PC_RTOS_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

// esp_timer_get_time() is used pervasively alongside FreeRTOS in the ADAM code;
// make it available to anything that includes FreeRTOS.
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

// Critical-section spinlock (a network device declares one as a member). No real
// critical sections are entered on PC; this just satisfies the declaration.
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

// ESP-IDF code commonly relies on the FreeRTOS umbrella header making the task
// and queue handle types available. Pull them in here (guards prevent recursion).
#include "task.h"
#include "queue.h"

#endif // PC_RTOS_FREERTOS_H
