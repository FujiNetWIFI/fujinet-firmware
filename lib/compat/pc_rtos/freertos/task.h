// PC shim for FreeRTOS <freertos/task.h> (ADAM PC build only).
// Tasks are implemented as detached std::threads in pc_rtos.cpp.
#ifndef PC_RTOS_TASK_H
#define PC_RTOS_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_depth,
                       void *arg, UBaseType_t priority, TaskHandle_t *out_handle);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_depth,
                                   void *arg, UBaseType_t priority, TaskHandle_t *out_handle,
                                   BaseType_t core_id);

void vTaskDelete(TaskHandle_t handle);
void vTaskDelay(TickType_t ticks_to_delay);

#ifdef __cplusplus
}
#endif

#endif // PC_RTOS_TASK_H
