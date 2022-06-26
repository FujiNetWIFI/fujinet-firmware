#ifdef BUILD_LYNX

/**
 * Comlynx Functions
 */
#include "comlynx.h"
#include "udpstream.h"
#include "8bithub.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnDNS.h"
#include "led.h"
#include <cstring>

#define IDLE_TIME 300 // Idle tolerance in microseconds

static xQueueHandle reset_evt_queue = NULL;

static void IRAM_ATTR comlynx_reset_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(reset_evt_queue, &gpio_num, NULL);
}

static void comlynx_reset_intr_task(void *arg)
{
    uint32_t io_num;
    bool was_reset = false;
    bool reset_debounced = false;
    uint64_t start, current, elapsed;
    systemBus *b = (systemBus *)arg;

    // reset_detect_status = gpio_get_level((gpio_num_t)PIN_COMLYNX_RESET);
    start = current = esp_timer_get_time();
    for (;;)
    {
        if (xQueueReceive(reset_evt_queue, &io_num, portMAX_DELAY))
        {
            start = esp_timer_get_time();
            printf("Comlynx RESET Asserted\n");
            was_reset = true;
        }
        current = esp_timer_get_time();

        elapsed = current - start;

        if (was_reset)
        {
            if (elapsed >= COMLYNX_RESET_DEBOUNCE_PERIOD)
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

        b->reset();
        vTaskDelay(1);
    }
}

uint8_t comlynx_checksum(uint8_t *buf, unsigned short len)
{
    uint8_t checksum = 0x00;

    for (unsigned short i = 0; i < len; i++)
        checksum ^= buf[i];

    return checksum;
}

void virtualDevice::comlynx_send(uint8_t b)
{
    // Write the byte
    ComLynx.wait_for_idle();
    fnUartSIO.write(b);
    fnUartSIO.flush();
    fnUartSIO.read();
}

void virtualDevice::comlynx_send_buffer(uint8_t *buf, unsigned short len)
{
    ComLynx.wait_for_idle();
    fnUartSIO.write(buf,len);
    fnUartSIO.readBytes(buf,len);
}

uint8_t virtualDevice::comlynx_recv()
{
    uint8_t b;

    while (fnUartSIO.available() <= 0)
        fnSystem.yield();

    b = fnUartSIO.read();

    return b;
}

bool virtualDevice::comlynx_recv_timeout(uint8_t *b, uint64_t dur)
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

uint16_t virtualDevice::comlynx_recv_length()
{
    unsigned short s = 0;
    s = comlynx_recv() << 8;
    s |= comlynx_recv();

    return s;
}

void virtualDevice::comlynx_send_length(uint16_t l)
{
    ComLynx.wait_for_idle();
    comlynx_send(l >> 8);
    comlynx_send(l & 0xFF);
}

unsigned short virtualDevice::comlynx_recv_buffer(uint8_t *buf, unsigned short len)
{
    return fnUartSIO.readBytes(buf, len);
}

uint32_t virtualDevice::comlynx_recv_blockno()
{
    unsigned char x[4] = {0x00, 0x00, 0x00, 0x00};

    comlynx_recv_buffer(x, 4);

    return x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];
}

void virtualDevice::reset()
{
    Debug_printf("No Reset implemented for device %u\n", _devnum);
}

void virtualDevice::comlynx_response_ack()
{
    comlynx_send(0x90 | _devnum);
}

void virtualDevice::comlynx_response_nack()
{
    comlynx_send(0xC0 | _devnum);
}

void virtualDevice::comlynx_control_ready()
{
    comlynx_response_ack();
}

void systemBus::wait_for_idle()
{
    bool isIdle = false;
    int64_t start, current, dur;
    int trashCount = 0;

    do
    {
        // Wait for serial line to quiet down.
        while (fnUartSIO.available() > 0)
        {
            fnUartSIO.read();
            trashCount++;
        }

        if (trashCount > 0)
            Debug_printf("wait_for_idle() dropped %d bytes\n", trashCount);

        start = current = esp_timer_get_time();

        while ((fnUartSIO.available() <= 0) && (isIdle == false))
        {
            current = esp_timer_get_time();
            dur = current - start;
            if (dur > IDLE_TIME)
                isIdle = true;
        }
    } while (isIdle == false);
    fnSystem.yield();
}

void virtualDevice::comlynx_process(uint8_t b)
{
    fnUartDebug.printf("comlynx_process() not implemented yet for this device. Cmd received: %02x\n", b);
}

void virtualDevice::comlynx_control_status()
{
    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_status();
}

void virtualDevice::comlynx_response_status()
{
    status_response[0] |= _devnum;

    status_response[5] = comlynx_checksum(&status_response[1], 4);
    comlynx_send_buffer(status_response, sizeof(status_response));
}

void virtualDevice::comlynx_control_clr()
{
    if (response_len == 0)
    {
        comlynx_response_nack();
    }
    else
    {
        comlynx_send(0xB0 | _devnum);
        comlynx_send_length(response_len);
        comlynx_send_buffer(response, response_len);
        comlynx_send(comlynx_checksum(response, response_len));
        memset(response, 0, sizeof(response));
        response_len = 0;
    }
}

void virtualDevice::comlynx_idle()
{
    // Not implemented in base class
}

// void virtualDevice::comlynx_status()
//{
//     fnUartDebug.printf("comlynx_status() not implemented yet for this device.\n");
// }

void systemBus::_comlynx_process_cmd()
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
        // turn on Comlynx Indicator LED
        fnLedManager.set(eLed::LED_BUS, true);
        _daisyChain[d]->comlynx_process(b);
        // turn off Comlynx Indicator LED
        fnLedManager.set(eLed::LED_BUS, false);
    }

    //wait_for_idle(); // to avoid failing edge case where device is connected but disabled.
    fnUartSIO.flush_input();
}

void systemBus::_comlynx_process_queue()
{
}

void systemBus::service()
{
    // Handle UDP Stream if active
    if (_udpDev != nullptr && _udpDev->udpstreamActive)
        _udpDev->comlynx_handle_udpstream();
    // Handle 8 Bit Hub Mode if active
    //else if (_hubDev != nullptr && _hubDev->hubActive)
    //    _hubDev->comlynx_handle_8bithub();
    // Process anything waiting
    else if (fnUartSIO.available() > 0)
        _comlynx_process_cmd();
}

void systemBus::setup()
{
    Debug_println("COMLYNX SETUP");

    // Set up interrupt for RESET line
    reset_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // Start card detect task
    xTaskCreate(comlynx_reset_intr_task, "comlynx_reset_intr_task", 2048, this, 10, NULL);
    // Enable interrupt for card detection
    fnSystem.set_pin_mode(PIN_COMLYNX_RESET, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_UP, GPIO_INTR_NEGEDGE);
    // Add the card detect handler
    gpio_isr_handler_add((gpio_num_t)PIN_COMLYNX_RESET, comlynx_reset_isr_handler, (void *)PIN_CARD_DETECT_FIX);

    // Set up UDP device
    _udpDev = new lynxUDPStream();

    // Set up 8bit Hub device
    _hubDev = new lynx8bithub();
    
    // Set up UART
    fnUartSIO.begin(COMLYNX_BAUD);
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
        _printerDev = (lynxPrinter *)pDevice;
        break;
    case 0x0f:
        _fujiDev = (lynxFuji *)pDevice;
        break;
    }
}

bool systemBus::deviceExists(uint8_t device_id)
{
    return _daisyChain.find(device_id) != _daisyChain.end();
}

bool systemBus::deviceEnabled(uint8_t device_id)
{
    if (deviceExists(device_id))
        return _daisyChain[device_id]->device_active;
    else
        return false;
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
    Debug_printf("Enabling Comlynx Device %d\n", device_id);

    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = true;
}

void systemBus::disableDevice(uint8_t device_id)
{
    Debug_printf("Disabling Comlynx Device %d\n", device_id);

    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = false;
}

void systemBus::setUDPHost(const char *hostname, int port)
{
    // Turn off if hostname is STOP
    if (!strcmp(hostname, "STOP"))
    {
        if (_udpDev->udpstreamActive)
            _udpDev->comlynx_disable_udpstream();

        return;
    }

    if (hostname != nullptr && hostname[0] != '\0')
    {
        // Try to resolve the hostname and store that so we don't have to keep looking it up
        _udpDev->udpstream_host_ip = get_ip4_addr_by_name(hostname);
        //_udpDev->udpstream_host_ip = IPADDR_NONE;

        if (_udpDev->udpstream_host_ip == IPADDR_NONE)
        {
            Debug_printf("Failed to resolve hostname \"%s\"\n", hostname);
        }
    }
    else
    {
        _udpDev->udpstream_host_ip = IPADDR_NONE;
    }

    if (port > 0 && port <= 65535)
    {
        _udpDev->udpstream_port = port;
    }
    else
    {
        _udpDev->udpstream_port = 5004;
        Debug_printf("UDPStream port not provided or invalid (%d), setting to 5004\n", port);
    }

    // Restart UDP Stream mode if needed
    if (_udpDev->udpstreamActive)
        _udpDev->comlynx_disable_udpstream();
    if (_udpDev->udpstream_host_ip != IPADDR_NONE)
        _udpDev->comlynx_enable_udpstream();
}

void systemBus::set8bithub(bool enabled)
{
    if (enabled)
        _hubDev->comlynx_8bithub_enable();
    else
        _hubDev->comlynx_8bithub_disable();
}

systemBus ComLynx;
#endif /* BUILD_LYNX */