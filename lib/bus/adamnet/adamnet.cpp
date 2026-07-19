#ifdef BUILD_ADAM

/**
 * AdamNet Functions
 */
#include "adamnet.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "led.h"
#include <cstring>
#include "adamFuji.h"
#include "fnConfig.h"

#include <cassert>

#ifdef ESP_PLATFORM
#include <driver/gpio.h>
#endif

#define IDLE_TIME 180 // Idle tolerance in microseconds

#ifdef ESP_PLATFORM
static QueueHandle_t reset_evt_queue = NULL;

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
    systemBus *b = (systemBus *)arg;

    // reset_detect_status = gpio_get_level((gpio_num_t)PIN_ADAMNET_RESET);
    start = current = GET_TIMESTAMP();
    for (;;)
    {
        if (xQueueReceive(reset_evt_queue, &io_num, portMAX_DELAY))
        {
            start = GET_TIMESTAMP();
            printf("ADAMNet RESET Asserted\n");
            was_reset = true;
        }
        current = GET_TIMESTAMP();

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

        b->reset();
        vTaskDelay(1/portTICK_PERIOD_MS);
    }
}

// Dedicated AdamNet bus service task.
static void adamnet_bus_task(void *arg)
{
    systemBus *b = (systemBus *)arg;
    int64_t last = GET_TIMESTAMP();
    for (;;)
    {
        int64_t now = GET_TIMESTAMP();

        if (now - last > ADAMNET_STALL_RESYNC_US && b->available())
            b->wait_for_idle();
        b->service();
        last = GET_TIMESTAMP();
        taskYIELD(); // cooperative; returns at once when no other core-1 task is ready
    }
}
#endif // ESP_PLATFORM

uint8_t adamnet_checksum(uint8_t *buf, unsigned short len)
{
    uint8_t checksum = 0x00;

    for (unsigned short i = 0; i < len; i++)
        checksum ^= buf[i];

    return checksum;
}

// Consume the trailing checksum and ACK, once the full payload has been read.
void virtualDevice::deferred_ack()
{
    adamnet_recv(); // CK
    SYSTEM_BUS.start_time = GET_TIMESTAMP();
    adamnet_response_ack();
    _ack_deferred = false;
}

void systemBus::transaction_accept(transState_t expectMoreData)
{
    assert(_transaction_state == TRANS_STATE::INVALID);

    start_time = GET_TIMESTAMP();
    if (expectMoreData == TRANS_STATE::WILL_GET)
    {
        _activeDev->_ack_deferred = true;
    }
    else
    {
        // No payload follows; the next byte is the checksum.
        _activeDev->adamnet_recv(); // Discard CK
        _activeDev->adamnet_response_ack();
    }

    _transaction_state = expectMoreData;
}

void systemBus::transaction_success()
{
    assert(_transaction_state == TRANS_STATE::NO_GET
           || _transaction_state == TRANS_STATE::DID_GET);
    _transaction_state = TRANS_STATE::INVALID;
}

void systemBus::transaction_error()
{
    if (_activeDev->_ack_deferred)
    {
        wait_for_idle();
        start_time = GET_TIMESTAMP();
        _activeDev->adamnet_response_ack();
        _activeDev->_ack_deferred = false;
    }
    _transaction_state = TRANS_STATE::INVALID;
}

success_is_true systemBus::transaction_get(void *data, size_t len)
{
    assert(_transaction_state == TRANS_STATE::WILL_GET);
    _transaction_state = TRANS_STATE::DID_GET;
    if (_activeDev->_ack_deferred)
        _activeDev->deferred_ack();
    if (_activePacket->data()->size() != len)
        RETURN_ERROR_AS_FALSE();
    std::copy(_activePacket->data()->begin(), _activePacket->data()->end(),
              static_cast<uint8_t *>(data));
    RETURN_SUCCESS_AS_TRUE();
}

void systemBus::transaction_send(const void *data, size_t len, bool err)
{
    assert(_transaction_state == TRANS_STATE::NO_GET);
    memcpy(_activeDev->response, data, len);
    _activeDev->response_len = len;
    _transaction_state = TRANS_STATE::INVALID;
}

void virtualDevice::adamnet_send(uint8_t b)
{
    // Write the byte
    SYSTEM_BUS.write(b);
    SYSTEM_BUS.flush();
}

void virtualDevice::adamnet_send_buffer(uint8_t *buf, unsigned short len)
{
    SYSTEM_BUS.write(buf, len);
    SYSTEM_BUS.flush();
}

uint8_t virtualDevice::adamnet_recv()
{
    uint8_t b;
    int64_t start = GET_TIMESTAMP();

    while (SYSTEM_BUS.available() <= 0)
    {
        if (GET_TIMESTAMP() - start > ADAMNET_RECV_TIMEOUT_US)
        {
            SYSTEM_BUS.frame_error = true;
            return 0;
        }
        fnSystem.yield();
    }

    b = SYSTEM_BUS.read();

    return b;
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
    return SYSTEM_BUS.read(buf, len);
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

void virtualDevice::adamnet_response_ack(bool doNotWaitForIdle)
{
    if (!doNotWaitForIdle)
        SYSTEM_BUS.min_turnaround();

#ifdef ESP_PLATFORM
    // Real bus only: don't answer past the master's window. BoIP just waits.
    if (GET_TIMESTAMP() - SYSTEM_BUS.start_time >= ADAMNET_RESPONSE_DEADLINE_US)
        return;
#endif
    adamnet_send(0x90 | _devnum);
}

void virtualDevice::adamnet_response_nack(bool doNotWaitForIdle)
{
    if (!doNotWaitForIdle)
        SYSTEM_BUS.min_turnaround();

#ifdef ESP_PLATFORM
    // Real bus only: don't answer past the master's window. BoIP just waits.
    if (GET_TIMESTAMP() - SYSTEM_BUS.start_time >= ADAMNET_RESPONSE_DEADLINE_US)
        return;
#endif
    adamnet_send(0xC0 | _devnum);
}

void virtualDevice::adamnet_control_ready()
{
    adamnet_response_ack();
}

void systemBus::wait_for_idle()
{
    _port->discardInput();
    fnSystem.yield();
}

void systemBus::wait_turnaround(uint32_t us)
{
#ifdef ESP_PLATFORM
    // Hold off the shared one-wire bus until `us` after the command.
    int64_t dt = GET_TIMESTAMP() - start_time;
    if (dt >= 0 && dt < (int64_t)us)
        fnSystem.delay_microseconds(us - dt);
#else
    // BoIP has no shared wire, and usleep() can't honor sub-ms holds anyway.
    (void)us;
#endif
}

void systemBus::min_turnaround()
{
    wait_turnaround(ADAMNET_TURNAROUND_US);
}

void systemBus::drain_echo(size_t n)
{
    // Because: Everything we transmit on the one-wire bus echoes back into our own RX.
    if (n == 0)
        return;
    if (n > ECHO_DRAIN_MAX)
    {
        wait_for_idle();
        return;
    }

    uint8_t scratch[ECHO_DRAIN_MAX];
    size_t got = 0;
    int64_t last = GET_TIMESTAMP();

    while (got < n)
    {
        size_t avail = _port->available();
        if (avail)
        {
            size_t take = n - got;
            if (take > avail)
                take = avail;
            got += _port->read(scratch, take);
            last = GET_TIMESTAMP();
        }
        else if (GET_TIMESTAMP() - last > ECHO_SETTLE_US)
        {
            break; // straggler window elapsed; don't wait for a lost echo byte
        }
    }
}

void virtualDevice::adamnet_process(const FujiAdamPacket &packet)
{
    Debug_printf("adamnet_process() not implemented yet for this device: 0x%02x  type: 0x%02x\n", packet.device(), packet.type());
}

void virtualDevice::adamnet_control_status()
{
    SYSTEM_BUS.start_time=GET_TIMESTAMP();
   adamnet_response_status();
}

void virtualDevice::adamnet_response_status()
{
    status_response.cmd_dev = (static_cast<uint8_t>(APT::NM_STATUS) << 4) | _devnum;

    status_response.checksum = adamnet_checksum((uint8_t *) &status_response.length, 4);

    SYSTEM_BUS.min_turnaround();
    adamnet_send_buffer((uint8_t *) &status_response, sizeof(status_response));
}

void virtualDevice::adamnet_control_clr()
{
    if (response_len == 0)
    {
        adamnet_response_nack();
    }
    else
    {
        adamnet_send(0xB0 | _devnum);
        adamnet_send_length(response_len);
        adamnet_send_buffer(response, response_len);
        adamnet_send(adamnet_checksum(response, response_len));
        memset(response, 0, sizeof(response));
        response_len = 0;
    }
}

void virtualDevice::adamnet_idle()
{
    // Not implemented in base class
}

//void virtualDevice::adamnet_status()
//{
//    fnDebugConsole.printf("adamnet_status() not implemented yet for this device.\n");
//}

void systemBus::_adamnet_process_cmd()
{
    uint8_t dest = _port->read();
    int64_t cmd_start = GET_TIMESTAMP();
    start_time = cmd_start;
    frame_error = false;
    _tx_count = 0;
    stall_silent = false;

    auto tmpPacket = FujiAdamPacket(dest);

    // Find device ID and pass control to it
    if (_daisyChain.count(tmpPacket.device()) < 1)
    {
    }
    else if (_daisyChain[tmpPacket.device()]->device_active == true)
    {
        // turn on AdamNet Indicator LED
        fnLedManager.set(eLed::LED_BUS, true);
        _activeDev = _daisyChain[tmpPacket.device()];
        _activePacket = &tmpPacket;
        _activeDev->adamnet_process(tmpPacket);
        // turn off AdamNet Indicator LED
        fnLedManager.set(eLed::LED_BUS, false);
    }

    if (stall_silent)
    {
        if (GET_TIMESTAMP() - cmd_start > ADAMNET_LONG_CMD_US)
            wait_for_idle();
        else
            fnSystem.yield();
    }
    else if (GET_TIMESTAMP() - cmd_start > ADAMNET_LONG_CMD_US)
        wait_for_idle();
    else if (_tx_count > 0 && !frame_error)
        drain_echo(_tx_count);
    else
        wait_for_idle();
}

#ifdef ESP_PLATFORM
void systemBus::_adamnet_process_queue()
{
    adamnet_message_t msg;
    if (xQueueReceive(qAdamNetMessages, &msg, 0) == pdTRUE)
    {
        switch (msg.message_id)
        {
        case ADAMNETMSG_DISKSWAP:
            if (_fujiDev != nullptr)
                _fujiDev->fujicmd_image_rotate();
            break;
        }
    }
}
#endif /* ESP_PLATFORM */

void systemBus::service()
{
#ifdef ESP_PLATFORM
    // process queue messages (disk swap)
    _adamnet_process_queue();
#endif /* ESP_PLATFORM */

    // Process anything waiting.
    if (_port->available() > 0)
        _adamnet_process_cmd();
#ifndef ESP_PLATFORM
    else
        // Idle: block briefly instead of spinning the PC main loop at 100% CPU.
        _netadam.poll(1);
#endif
}

void systemBus::setup()
{
    Debug_println("ADAMNET SETUP");

#ifdef ESP_PLATFORM
    // Set up event queue (disk swap messages)
    qAdamNetMessages = xQueueCreate(4, sizeof(adamnet_message_t));

    // Set up interrupt for RESET line
    reset_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Start card detect task
    xTaskCreate(adamnet_reset_intr_task, "adamnet_reset_intr_task", 2048, this, 10, NULL);
    // Enable interrupt for card detection
    fnSystem.set_pin_mode(PIN_ADAMNET_RESET, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_UP, GPIO_INTR_NEGEDGE);
    // Add the card detect handler
    gpio_isr_handler_add((gpio_num_t)PIN_ADAMNET_RESET, adamnet_reset_isr_handler, (void *)PIN_CARD_DETECT_FIX);

    // Set up UART
    _serial.begin(ChannelConfig()
                .deviceID(FN_UART_BUS)
                .baud(ADAMNET_BAUDRATE)
                .inverted(true)
                .readTimeout(2.0)
                .discardTimeout(0.180)
                .rxThreshold(1)
                .txBuffer(2048)
                );
    _port = &_serial;
#else
    // PC build: carry AdamNet over a TCP socket (Bus over IP) when enabled,
    // otherwise fall back to a real serial port.
    if (Config.get_boip_enabled())
    {
        _netadam.begin(BoIPConfig()
                       .hostName(Config.get_boip_host())
                       .portNum(Config.get_boip_port())
                       .client()
                       .localEcho(true)
                       .nonBlocking()  // recv path can't absorb a blocking poll; idle is throttled by poll(1) in service()
                       .noDelay()      // disable Nagle: tiny request/response packets
                       .readTimeout(1000)
                       .discardTimeout(0)
                       );
        _port = &_netadam;
    }
    else
    {
        _serial.begin(ChannelConfig()
                    .baud(ADAMNET_BAUDRATE)
                    .readTimeout(2.0)
                    .discardTimeout(0.180)
                    );
        _port = &_serial;
    }
#endif
}

void systemBus::start_bus_task()
{
#ifdef ESP_PLATFORM
    xTaskCreatePinnedToCore(adamnet_bus_task, "adamnet_bus", ADAMNET_BUS_TASK_STACK,
                            this, ADAMNET_BUS_TASK_PRIORITY, NULL, ADAMNET_BUS_TASK_CORE);

    gpio_set_drive_capability((gpio_num_t)PIN_UART2_TX, GPIO_DRIVE_CAP_3);
    Debug_printf("AdamNet TX (GPIO%d) drive strength set to MAX\n", PIN_UART2_TX);
#endif
    // On PC the bus is serviced from the main loop (see src/main.cpp).
}

void systemBus::shutdown()
{
    shuttingDown = true;

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
        _fujiDev = dynamic_cast<adamFuji*>(pDevice);
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
    for (auto it = _daisyChain.begin(); it != _daisyChain.end(); ++it)
    {
        if (it->second == p)
        {
            _daisyChain.erase(it);
            break;
        }
    }
    p->_devnum = device_id;
    _daisyChain[device_id] = p;
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
    Debug_printf("Enabling AdamNet Device %d\n",device_id);

    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = true;
}

void systemBus::disableDevice(uint8_t device_id)
{
    Debug_printf("Disabling AdamNet Device %d\n",device_id);

    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = false;
}
#endif /* BUILD_ADAM */
