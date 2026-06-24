// PC shim for FreeRTOS <freertos/queue.h> (ADAM PC build only).
#ifndef PC_RTOS_QUEUE_H
#define PC_RTOS_QUEUE_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
void          vQueueDelete(QueueHandle_t q);

BaseType_t    xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks_to_wait);
BaseType_t    xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *higher_prio_woken);
BaseType_t    xQueueReceive(QueueHandle_t q, void *buffer, TickType_t ticks_to_wait);

UBaseType_t   uxQueueMessagesWaiting(QueueHandle_t q);
UBaseType_t   uxQueueSpacesAvailable(QueueHandle_t q);

#ifdef __cplusplus
}
#endif

#endif // PC_RTOS_QUEUE_H
