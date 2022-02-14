#ifdef BUILD_ADAM

/**
 * AdamNet Functions
 */
#include "adamnet.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "led.h"

static xQueueHandle reset_evt_queue = NULL;

static void IRAM_ATTR adamnet_reset_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(reset_evt_queue, &gpio_num, NULL);
}

static void adamnet_reset_intr_task(void *arg)
{
    uint32_t io_num;
    bool was_reset = false;
    bool reset_debounced = false;
    uint64_t start, current, elapsed;

    // reset_detect_status = gpio_get_level((gpio_num_t)PIN_ADAMNET_RESET);
    start = current = esp_timer_get_time();
    for (;;)
    {
        if (xQueueReceive(reset_evt_queue, &io_num, portMAX_DELAY))
        {
            start = esp_timer_get_time();
            printf("ADAMNet RESET Asserted\n");
            was_reset = true;
        }
        current = esp_timer_get_time();

        elapsed = current - start;

        if (was_reset)
        {
            if (elapsed >= ADAMNET_RESET_DEBOUNCE_PERIOD)
            {
                reset_debounced = true;
            }
        }

        if (was_reset && reset_debounced)
        {
            was_reset = false;
            // debounce period for reset completed
            reset_debounced = false;
            ;
        }
        vTaskDelay(10);
    }
}

uint8_t adamnet_checksum(uint8_t *buf, unsigned short len)
{
    uint8_t checksum = 0x00;

    for (unsigned short i = 0; i < len; i++)
        checksum ^= buf[i];

    return checksum;
}

void virtualDevice::adamnet_send(uint8_t b)
{
    // Write the byte
    fnUartSIO.write(b);
    fnUartSIO.flush();
}

void virtualDevice::adamnet_send_buffer(uint8_t *buf, unsigned short len)
{
    fnUartSIO.write(buf, len);
    fnUartSIO.flush();
}

uint8_t virtualDevice::adamnet_recv()
{
    uint8_t b;

    while (fnUartSIO.available() <= 0)
        fnSystem.yield();

    b = fnUartSIO.read();

    return b;
}

bool virtualDevice::adamnet_recv_timeout(uint8_t *b, uint64_t dur)
{
    uint64_t start, current, elapsed;
    bool timeout = true;

    start = current = esp_timer_get_time();
    elapsed = 0;

    while (fnUartSIO.available() <= 0)
    {
        current = esp_timer_get_time();
        elapsed = current - start;
        if (elapsed > dur)
            break;
    }

    if (fnUartSIO.available() > 0)
    {
        *b = (uint8_t)fnUartSIO.read();
        timeout = false;
    } // else
      //   Debug_printf("duration: %llu\n", elapsed);

    return timeout;
}

uint16_t virtualDevice::adamnet_recv_length()
{
    unsigned short s = 0;
    s = adamnet_recv() << 8;
    s |= adamnet_recv();

    return s;
}

void virtualDevice::adamnet_send_length(uint16_t l)
{
    adamnet_send(l >> 8);
    adamnet_send(l & 0xFF);
}

unsigned short virtualDevice::adamnet_recv_buffer(uint8_t *buf, unsigned short len)
{
    return fnUartSIO.readBytes(buf, len);
}

uint32_t virtualDevice::adamnet_recv_blockno()
{
    unsigned char x[4] = {0x00, 0x00, 0x00, 0x00};

    adamnet_recv_buffer(x, 4);

    return x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];
}

void virtualDevice::reset()
{
    Debug_printf("No Reset implemented for device %u\n", _devnum);
}

void virtualDevice::adamnet_response_ack()
{
    int64_t t = esp_timer_get_time() - AdamNet.start_time;

    if (t < 300)
    {
        AdamNet.wait_for_idle();
        adamnet_send(0x90 | _devnum);
    }
    else
    {
        Debug_printf("NM_ACK too long: %u μs\n",t);
    }
}

void virtualDevice::adamnet_response_nack()
{
    int64_t t = esp_timer_get_time() - AdamNet.start_time;

    if (t < 300)
    {
        AdamNet.wait_for_idle();
        adamnet_send(0xC0 | _devnum);
    }
}

void virtualDevice::adamnet_control_ready()
{
    adamnet_response_ack();
}

void systemBus::wait_for_idle()
{
    bool isIdle = false;
    int64_t start, current, dur;

    do
    {
        // Wait for serial line to quiet down.
        while (fnUartSIO.available() > 0)
            fnUartSIO.read();

        start = current = esp_timer_get_time();

        while ((fnUartSIO.available() <= 0) && (isIdle == false))
        {
            current = esp_timer_get_time();
            dur = current - start;
            if (dur > 150)
                isIdle = true;
        }
    } while (isIdle == false);
    fnSystem.yield();
}

void virtualDevice::adamnet_process(uint8_t b)
{
    fnUartDebug.printf("adamnet_process() not implemented yet for this device. Cmd received: %02x\n", b);
}

void virtualDevice::adamnet_control_status()
{
    AdamNet.start_time=esp_timer_get_time();
    adamnet_response_status();
}

void virtualDevice::adamnet_response_status()
{
    status_response[0] |= _devnum;

    status_response[5] = adamnet_checksum(&status_response[1], 4);
    adamnet_send_buffer(status_response, sizeof(status_response));
    // uart_tx_chars(2,(const char *)status_response,sizeof(status_response));
}

void virtualDevice::adamnet_idle()
{
    // Not implemented in base class
}

//void virtualDevice::adamnet_status()
//{
//    fnUartDebug.printf("adamnet_status() not implemented yet for this device.\n");
//}

void systemBus::_adamnet_process_cmd()
{
    uint8_t b;

    b = fnUartSIO.read();
    start_time = esp_timer_get_time();

    uint8_t d = b & 0x0F;

    // Find device ID and pass control to it
    if (_daisyChain.count(d) < 1)
    {
    }
    else if (_daisyChain[d]->device_active == true)
    {
        // turn on AdamNet Indicator LED
        fnLedManager.set(eLed::LED_BUS, true);
        _daisyChain[d]->adamnet_process(b);
        // turn off AdamNet Indicator LED
        fnLedManager.set(eLed::LED_BUS, false);
    }

    wait_for_idle(); // to avoid failing edge case where device is connected but disabled.
    fnUartSIO.flush_input();
}

void systemBus::_adamnet_process_queue()
{
}

void systemBus::service()
{
    // Process anything waiting.
    if (fnUartSIO.available() > 0)
        _adamnet_process_cmd();
}

void systemBus::setup()
{
    Debug_println("ADAMNET SETUP");

    // Set up interrupt for RESET line
    reset_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // Start card detect task
    xTaskCreate(adamnet_reset_intr_task, "adamnet_reset_intr_task", 2048, NULL, 10, NULL);
    // Enable interrupt for card detection
    fnSystem.set_pin_mode(PIN_ADAMNET_RESET, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_UP, GPIO_INTR_NEGEDGE);
    // Add the card detect handler
    gpio_isr_handler_add((gpio_num_t)PIN_ADAMNET_RESET, adamnet_reset_isr_handler, (void *)PIN_CARD_DETECT_FIX);

    // Set up UART
    fnUartSIO.begin(ADAMNET_BAUD);
}

void systemBus::shutdown()
{
    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n", devicep.second->id());
        devicep.second->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

void systemBus::addDevice(virtualDevice *pDevice, uint8_t device_id)
{
    Debug_printf("Adding device: %02X\n", device_id);
    pDevice->_devnum = device_id;
    _daisyChain[device_id] = pDevice;

    switch (device_id)
    {
    case 0x02:
        _printerDev = (adamPrinter *)pDevice;
        break;
    case 0x0f:
        _fujiDev = (adamFuji *)pDevice;
        break;
    }
}

bool systemBus::deviceExists(uint8_t device_id)
{
    return _daisyChain.find(device_id) != _daisyChain.end();
}

void systemBus::remDevice(virtualDevice *pDevice)
{
}

void systemBus::remDevice(uint8_t device_id)
{
    if (deviceExists(device_id))
    {
        _daisyChain.erase(device_id);
    }
}

int systemBus::numDevices()
{
    return _daisyChain.size();
}

void systemBus::changeDeviceId(virtualDevice *p, uint8_t device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep.second == p)
            devicep.second->_devnum = device_id;
    }
}

virtualDevice *systemBus::deviceById(uint8_t device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep.second->_devnum == device_id)
            return devicep.second;
    }
    return nullptr;
}

void systemBus::reset()
{
    for (auto devicep : _daisyChain)
        devicep.second->reset();
}

void systemBus::enableDevice(uint8_t device_id)
{
    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = true;
}

void systemBus::disableDevice(uint8_t device_id)
{
    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = false;
}

systemBus AdamNet;
#endif /* BUILD_ADAM */