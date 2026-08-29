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
    b->adamnet_handle_bus_task();
}

void systemBus::adamnet_handle_bus_task(void)
{
    int64_t last = GET_TIMESTAMP();
    for (;;)
    {
        int64_t now = GET_TIMESTAMP();

        if (now - last > ADAMNET_STALL_RESYNC_US && _port->available())
            wait_for_idle();
        service();
        last = GET_TIMESTAMP();
        taskYIELD(); // cooperative; returns at once when no other core-1 task is ready
    }
}
#endif // ESP_PLATFORM

void systemBus::transaction_accept(transState_t expectMoreData)
{
    assert(_transaction_state == TRANS_STATE::INVALID);
    _transaction_state = expectMoreData;
}

void systemBus::transaction_success()
{
    assert(_transaction_state == TRANS_STATE::NO_GET
           || _transaction_state == TRANS_STATE::DID_GET);
    // Data was requested but didn't go through transaction_send
    if (_activePacket->type() == APT::MN_RECEIVE)
        busPhase.didIgnore();
    else if (busPhase.needAck())
        sendAckPacket();
    _transaction_state = TRANS_STATE::INVALID;
}

void systemBus::transaction_error()
{
    if (busPhase.needAck())
    {
        wait_for_idle();
        _start_time = GET_TIMESTAMP();
        sendNakPacket();
    }
    _transaction_state = TRANS_STATE::INVALID;
}

success_is_true systemBus::transaction_get(void *data, size_t len)
{
    assert(_transaction_state == TRANS_STATE::WILL_GET);
    _transaction_state = TRANS_STATE::DID_GET;
    len = std::min(len, _activePacket->data()->size());
    std::copy(_activePacket->data()->begin(), _activePacket->data()->begin() + len,
              static_cast<uint8_t *>(data));
    RETURN_SUCCESS_AS_TRUE();
}

void systemBus::transaction_send(const void *data, size_t len, bool err)
{
    assert(_transaction_state == TRANS_STATE::NO_GET);
    const uint8_t *ptr = static_cast<const uint8_t*>(data);
    FujiAdamPacket packet(_activeDev->id(), APT::NM_SEND, ByteBuffer(ptr, ptr + len));
    _transaction_reply_encoded = packet.serialize();

    // FIXME - won't this always ack? Is the if needed?
    if (busPhase.needAck())
        sendAckPacket();

    _transaction_state = TRANS_STATE::INVALID;
}

bool systemBus::sendReplyPacket(bool ack, const void *data, size_t length)
{
    adamPacketType_t ptype;


    switch (_activePacket->type())
    {
    case APT::MN_STATUS:
        ptype = APT::NM_STATUS;
        break;

    case APT::MN_SEND:
    case APT::MN_RECEIVE:
    case APT::MN_READY:
        ptype = ack ? APT::NM_ACK : APT::NM_NAK;
        break;

    case APT::MN_CLR:
        ptype = APT::NM_SEND;
        break;

    default:
        abort();
    }

    std::optional<FujiAdamPacket> packet;
    if (ack && data)
    {
        const uint8_t *ptr = static_cast<const uint8_t*>(data);
        packet.emplace(_activeDev->id(), ptype, ByteBuffer(ptr, ptr + length));
    }
    else
        packet.emplace(_activeDev->id(), ptype);

    return writeBusPacket(*packet);
}

void systemBus::sendResponsePacket(void)
{
    if (!_transaction_reply_encoded.has_value())
        return;

#ifdef ESP_PLATFORM
    // Real bus only: answer only inside the window of opportunity
    if (GET_TIMESTAMP() - _start_time >= ADAMNET_RESPONSE_DEADLINE_US)
    {
        busPhase.didIgnore();
        return;
    }
#endif /* ESP_PLATFORM */

    _port->write(_transaction_reply_encoded->data(), _transaction_reply_encoded->size());
    _port->flushOutput();
    busPhase.sentData();
    _transaction_reply_encoded.reset();
}

bool systemBus::writeBusPacket(const FujiAdamPacket &packet)
{
    ByteBuffer encoded = packet.serialize();
#ifdef ESP_PLATFORM
    // Real bus only: answer only inside the window of opportunity
    if (GET_TIMESTAMP() - _start_time >= ADAMNET_RESPONSE_DEADLINE_US)
        return false;
#endif /* ESP_PLATFORM */

    _port->write(encoded.data(), encoded.size());
    _port->flushOutput();
#ifdef DEBUG_RAW_PACKET
    Debug_printv("Sent %d:\n%s", encoded.size(),
                 util_hexdump(encoded.data(), encoded.size()).c_str());
#endif // DEBUG_RAW_PACKET
    return true;
}

void virtualDevice::reset()
{
    Debug_printf("No Reset implemented for device %u\n", _devnum);
}

void virtualDevice::adamnet_control_ready()
{
    SYSTEM_BUS.sendAckPacket();
}

void virtualDevice::adamnet_control_receive()
{
    SYSTEM_BUS.sendAckPacket();
}

void systemBus::wait_for_idle()
{
    _port->discardInput();
    fnSystem.yield();
}

void systemBus::sendStatusPacket(const AdamNetStatus &status)
{
#ifdef ESP_PLATFORM
    // Real bus only: answer only inside the master's status
    // window. This will prevent Adam from recognizing that device is
    // online, it will not retry.
    if (GET_TIMESTAMP() - _start_time >= ADAMNET_RESPONSE_DEADLINE_US)
    {
        busPhase.didIgnore();
        return;
    }
#endif

    if (sendReplyPacket(true, &status, sizeof(status)))
        busPhase.sentStatus();
    else
        busPhase.didIgnore();
}

void systemBus::sendAckPacket()
{
    if (sendReplyPacket(true))
        busPhase.didAck();
    else
        busPhase.didIgnore();
}

void systemBus::sendNakPacket()
{
    if (sendReplyPacket(false))
        busPhase.didNak();
    else
        busPhase.didIgnore();
}

void virtualDevice::adamnet_idle()
{
    // Not implemented in base class
}

void systemBus::_adamnet_process_cmd()
{
    int64_t cmd_start = GET_TIMESTAMP();
    _start_time = cmd_start;
    frame_error = false;
    _stall_silent = false;

    auto tmpPacket = FujiAdamPacket(_port->read());
    auto it = _daisyChain.find(tmpPacket.device());
    if (it != _daisyChain.end() && it->second->device_active == true)
    {
        busPhase.begin(tmpPacket);

        // turn on AdamNet Indicator LED
        fnLedManager.set(eLed::LED_BUS, true);

        _activeDev = it->second;
        _activePacket = &tmpPacket;

        if (tmpPacket.type() == APT::MN_SEND)
        {
            u16be_t len, rlen;
            ByteBuffer buffer;
            uint8_t ck;

            _port->read(&len, sizeof(len));
            buffer.resize(len);
            rlen = _port->read(buffer.data(), buffer.size());
            if (rlen != len)
                buffer.resize(rlen);
            ck = _port->read();
            busPhase.gotPayload();

            // Reset window of opportunity after reading last byte of payload;
            _start_time = GET_TIMESTAMP();

            if (tmpPacket.setPayload(buffer, ck).is_error())
            {
                // At this stage the Adam only wants to know if we received the packet ok.
                sendNakPacket();
                goto done;
            }

            sendAckPacket();
        }

        _adamnet_dispatch(tmpPacket);

    done:
        // turn off AdamNet Indicator LED
        fnLedManager.set(eLed::LED_BUS, false);

#ifdef DEBUG_FINAL_PACKET_STATE
        Debug_printf("packet complete device=0x%x type=0x%x phase=%d\n",
                     tmpPacket.device(), tmpPacket.type(), busPhase.phase());
#endif

        busPhase.finish();
    }

    if (_stall_silent && (GET_TIMESTAMP() - cmd_start <= ADAMNET_LONG_CMD_US))
        fnSystem.yield();
    else
        wait_for_idle();
}

// Handle the five stages of AdamNet bus protocol
void systemBus::_adamnet_dispatch(const FujiAdamPacket &packet)
{
#ifdef DEBUG_DISPATCH
    Debug_printf("Dispatching device: %s (0x%02x)  type: %s (0x%02x)\n",
                 AdamNetPhase::device_to_string(packet.device()), packet.device(),
                 AdamNetPhase::type_to_string(packet.type()), packet.type());
    if ((packet.device() == FUJI_DEVICEID::FUJINET || packet.device() == FUJI_DEVICEID::NETWORK)
        && packet.type() == APT::MN_SEND)
        Debug_printf("Fuji command: 0x%02x\n", packet.command());
#endif // DEBUG_DISPATCH

    switch (packet.type())
    {
    case APT::MN_STATUS:
        // Get device capablities/check if it is alive
        sendStatusPacket(_activeDev->deviceStatus());
        break;

    case APT::MN_CLR:
        // Fetch the data from a previous read()/readBlock() request
        sendResponsePacket();
        break;

    case APT::MN_RECEIVE:
        // read() or readBlock()
        if (_transaction_reply_encoded.has_value())
            sendAckPacket();
        else
            _activeDev->adamnet_control_receive();
        break;

    case APT::MN_SEND:
        // write() or writeBlock()
        _activeDev->adamnet_control_send(packet);
        break;

    case APT::MN_READY:
        // Check if device is ready for a command
        _activeDev->adamnet_control_ready();
        break;

    case APT::MN_RESET:
        _activeDev->reset();
        break;

    default:
        break;
    }
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
                  .halfDuplex(true)
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

void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id)
{
    Debug_printf("Adding device: %02X\n", device_id);
    pDevice->_devnum = device_id;
    _daisyChain[device_id] = pDevice;

    switch (device_id)
    {
    case FUJI_DEVICEID::FUJINET:
        _fujiDev = dynamic_cast<adamFuji*>(pDevice);
        break;
    default:
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

void systemBus::changeDeviceId(virtualDevice *p, fujiDeviceID_t device_id)
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
