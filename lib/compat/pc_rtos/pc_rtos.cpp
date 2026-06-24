// PC implementation of the FreeRTOS / esp_timer subset used by the ADAM build.
// Queues are bounded thread-safe FIFOs; tasks are detached std::threads.

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// esp_timer
// ---------------------------------------------------------------------------
extern "C" int64_t esp_timer_get_time(void)
{
    static const auto t0 = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - t0).count();
}

extern "C" void fn_pc_task_yield(void)
{
    std::this_thread::yield();
}

// ---------------------------------------------------------------------------
// Queues
// ---------------------------------------------------------------------------
namespace {
struct PcQueue
{
    std::mutex mtx;
    std::condition_variable cv_not_empty;
    std::condition_variable cv_not_full;
    std::deque<std::vector<uint8_t>> items;
    size_t capacity;
    size_t item_size;
};
} // namespace

extern "C" QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    PcQueue *q = new PcQueue();
    q->capacity = length;
    q->item_size = item_size;
    return (QueueHandle_t)q;
}

extern "C" void vQueueDelete(QueueHandle_t handle)
{
    delete (PcQueue *)handle;
}

static BaseType_t pc_queue_send(QueueHandle_t handle, const void *item, TickType_t ticks_to_wait)
{
    if (handle == nullptr)
        return pdFALSE;
    PcQueue *q = (PcQueue *)handle;
    std::unique_lock<std::mutex> lk(q->mtx);

    if (q->items.size() >= q->capacity)
    {
        if (ticks_to_wait == 0)
            return pdFALSE;
        if (ticks_to_wait == portMAX_DELAY)
            q->cv_not_full.wait(lk, [&] { return q->items.size() < q->capacity; });
        else if (!q->cv_not_full.wait_for(lk, std::chrono::milliseconds(ticks_to_wait * portTICK_PERIOD_MS),
                                          [&] { return q->items.size() < q->capacity; }))
            return pdFALSE;
    }

    std::vector<uint8_t> buf(q->item_size);
    std::memcpy(buf.data(), item, q->item_size);
    q->items.push_back(std::move(buf));
    q->cv_not_empty.notify_one();
    return pdTRUE;
}

extern "C" BaseType_t xQueueSend(QueueHandle_t handle, const void *item, TickType_t ticks_to_wait)
{
    return pc_queue_send(handle, item, ticks_to_wait);
}

extern "C" BaseType_t xQueueSendFromISR(QueueHandle_t handle, const void *item, BaseType_t *woken)
{
    if (woken)
        *woken = pdFALSE;
    return pc_queue_send(handle, item, 0);
}

extern "C" BaseType_t xQueueReceive(QueueHandle_t handle, void *buffer, TickType_t ticks_to_wait)
{
    if (handle == nullptr)
        return pdFALSE;
    PcQueue *q = (PcQueue *)handle;
    std::unique_lock<std::mutex> lk(q->mtx);

    if (q->items.empty())
    {
        if (ticks_to_wait == 0)
            return pdFALSE;
        if (ticks_to_wait == portMAX_DELAY)
            q->cv_not_empty.wait(lk, [&] { return !q->items.empty(); });
        else if (!q->cv_not_empty.wait_for(lk, std::chrono::milliseconds(ticks_to_wait * portTICK_PERIOD_MS),
                                           [&] { return !q->items.empty(); }))
            return pdFALSE;
    }

    std::memcpy(buffer, q->items.front().data(), q->item_size);
    q->items.pop_front();
    q->cv_not_full.notify_one();
    return pdTRUE;
}

extern "C" UBaseType_t uxQueueMessagesWaiting(QueueHandle_t handle)
{
    if (handle == nullptr)
        return 0;
    PcQueue *q = (PcQueue *)handle;
    std::lock_guard<std::mutex> lk(q->mtx);
    return (UBaseType_t)q->items.size();
}

extern "C" UBaseType_t uxQueueSpacesAvailable(QueueHandle_t handle)
{
    if (handle == nullptr)
        return 0;
    PcQueue *q = (PcQueue *)handle;
    std::lock_guard<std::mutex> lk(q->mtx);
    return (UBaseType_t)(q->capacity - q->items.size());
}

// ---------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------
static BaseType_t pc_task_create(TaskFunction_t fn, void *arg, TaskHandle_t *out_handle)
{
    std::thread t([fn, arg] { fn(arg); });
    t.detach();
    if (out_handle)
        *out_handle = nullptr; // we don't support deletion of detached tasks
    return pdPASS;
}

extern "C" BaseType_t xTaskCreate(TaskFunction_t fn, const char *, uint32_t, void *arg,
                                  UBaseType_t, TaskHandle_t *out_handle)
{
    return pc_task_create(fn, arg, out_handle);
}

extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *, uint32_t, void *arg,
                                              UBaseType_t, TaskHandle_t *out_handle, BaseType_t)
{
    return pc_task_create(fn, arg, out_handle);
}

extern "C" void vTaskDelete(TaskHandle_t)
{
    // Detached threads run for the process lifetime; nothing to delete.
}

extern "C" void vTaskDelay(TickType_t ticks_to_delay)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks_to_delay * portTICK_PERIOD_MS));
}
