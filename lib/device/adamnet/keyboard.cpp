#ifdef BUILD_ADAM
#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "../device/adamnet/keyboard.h"
#include "driver/timer.h"
#include "hal/timer_ll.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/periph_ctrl.h"
#include "soc/soc.h"
#include "soc/periph_defs.h"
#include "esp_log.h"
#include "usb_host.h"

struct USBMessage
{
    uint8_t src;
    uint8_t len;
    uint8_t data[0x8];
};

static xQueueHandle usb_mess_Que = NULL;
TaskHandle_t kbTask;

void IRAM_ATTR timer_group0_isr(void *para)
{
    timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
    usb_process();
    timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);
}

void timer_task(void *pvParameter)
{
    timer_config_t config = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .divider = TIMER_DIVIDER
    };

    setDelay(4);
    usb_mess_Que = xQueueCreate(10, sizeof(struct USBMessage));
    initStates(PIN_USB_DP, PIN_USB_DM, -1, -1, -1, -1, -1, -1);

    timer_idx_t timer_idx = TIMER_0;
    double timer_interval_sec = TIMER_INTERVAL0_SEC;

    timer_init(TIMER_GROUP_0, timer_idx, &config);
    timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x00000000ULL);
    timer_set_alarm_value(TIMER_GROUP_0, timer_idx, timer_interval_sec * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, timer_idx);
    timer_isr_register(TIMER_GROUP_0, timer_idx, timer_group0_isr, (void *)timer_idx, ESP_INTR_FLAG_IRAM, NULL);
    timer_start(TIMER_GROUP_0, timer_idx);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    while (1)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    };
}

// ctor
adamKeyboard::adamKeyboard()
{
    server = new fnTcpServer(1234, 1); // Run a TCP server on port 1234.
    server->begin(1234);
    // xTaskCreatePinnedToCore(&timer_task,"KBTask",4096,NULL,10,&kbTask,1);
}

// dtor
adamKeyboard::~adamKeyboard()
{
    // vTaskDelete(kbTask);
    server->stop();
    delete server;
}

void adamKeyboard::adamnet_control_status()
{
    uint8_t r[6] = {0x81, 0x01, 0x00, 0x00, 0x00, 0x01};
    AdamNet.wait_for_idle();
    adamnet_send_buffer(r, sizeof(r));
}

void adamKeyboard::adamnet_control_receive()
{
    if (!client.connected() && server->hasClient())
    {
        AdamNet.wait_for_idle();
        adamnet_send(0xC1); // NAK
        client = server->available();
    }
    else if (!client.connected())
    {
        AdamNet.wait_for_idle();
        adamnet_send(0xC1); // NAK
    }
    else if (!kpQueue.empty())
    {
        AdamNet.wait_for_idle();
        adamnet_send(0x91); // ACK
    }
    else if (client.available() > 0)
    {
        AdamNet.wait_for_idle();
        adamnet_send(0x91); // ACK
        kpQueue.push(client.read());
    }
    else
    {
        AdamNet.wait_for_idle();
        adamnet_send(0xC1); // NAK
    }
}

void adamKeyboard::adamnet_control_clr()
{
    uint8_t r[5] = {0xB1, 0x00, 0x01, 0x00, 0x00};

    r[3] = r[4] = kpQueue.front();
    adamnet_send_buffer(r, sizeof(r));
    kpQueue.pop();
}

void adamKeyboard::adamnet_control_ready()
{
    AdamNet.wait_for_idle();
    adamnet_send(0x91); // Ack
}

void adamKeyboard::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_RECEIVE:
        adamnet_control_receive();
        break;
    case MN_CLR:
        adamnet_control_clr();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
}

void adamKeyboard::shutdown()
{
}
#endif /* BUILD_ADAM */