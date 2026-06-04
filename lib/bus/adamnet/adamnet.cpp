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

#define IDLE_TIME 180 // Idle tolerance in microseconds

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

        b->reset();
        vTaskDelay(1/portTICK_PERIOD_MS);
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
    int64_t start = esp_timer_get_time();

    // Half-duplex bus: if the master aborts a packet (reset, framing error,
    // dropped byte) the remaining bytes never arrive. Bounding the wait keeps a
    // stalled packet from spinning the bus task forever and tripping the WDT.
    while (SYSTEM_BUS.available() <= 0)
    {
        if (esp_timer_get_time() - start > ADAMNET_RECV_TIMEOUT_US)
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

// doNotWaitForIdle skips the turnaround delay for callers that have already
// paced themselves and need to respond immediately.
void virtualDevice::adamnet_response_ack(bool doNotWaitForIdle)
{
    if (!doNotWaitForIdle)
        SYSTEM_BUS.min_turnaround();

    // Measure the deadline after the mandatory turnaround so it reflects the
    // real elapsed time, not just processing latency.
    if (esp_timer_get_time() - SYSTEM_BUS.start_time < ADAMNET_RESPONSE_DEADLINE_US)
        adamnet_send(0x90 | _devnum);
}

void virtualDevice::adamnet_response_nack(bool doNotWaitForIdle)
{
    if (!doNotWaitForIdle)
        SYSTEM_BUS.min_turnaround();

    if (esp_timer_get_time() - SYSTEM_BUS.start_time < ADAMNET_RESPONSE_DEADLINE_US)
        adamnet_send(0xC0 | _devnum);
}

void virtualDevice::adamnet_control_ready()
{
    adamnet_response_ack();
}

void systemBus::wait_for_idle()
{
    _port.discardInput();
    fnSystem.yield();
}

void systemBus::wait_turnaround(uint32_t us)
{
    // Don't drive the shared wire until at least `us` after the command. Below
    // the contention floor it avoids ORing onto a frame the master is still
    // releasing; at a real drive's longer block-read turnaround it keeps us from
    // answering before the master has masked interrupts for the transfer.
    int64_t dt = esp_timer_get_time() - start_time;
    if (dt >= 0 && dt < (int64_t)us)
        fnSystem.delay_microseconds(us - dt);
}

void systemBus::min_turnaround()
{
    wait_turnaround(ADAMNET_TURNAROUND_US);
}

void systemBus::drain_echo(size_t n)
{
    // Everything we transmit on the one-wire bus echoes back into our own RX.
    // Consume our echo so the next service pass starts on a real command byte.
    // A response larger than the RX ring overflows it during the long transmit,
    // so fall back to idle-detection draining (safe: the master blocks on it).
    if (n == 0)
        return;
    if (n > ECHO_DRAIN_MAX)
    {
        wait_for_idle();
        return;
    }

    // The echo is already in RX after flushOutput(), so take the bytes actually
    // present, capped at the count we sent. Never block waiting for a byte that
    // may have been lost to a bus glitch: doing so could swallow the master's
    // next command and desync the stream (misframing later reads as a phantom
    // command). Reading at most n also leaves a fast-following master command
    // intact.
    uint8_t scratch[ECHO_DRAIN_MAX];
    size_t got = 0;
    int64_t last = esp_timer_get_time();

    while (got < n)
    {
        size_t avail = _port.available();
        if (avail)
        {
            size_t take = n - got;
            if (take > avail)
                take = avail;
            got += _port.read(scratch, take);
            last = esp_timer_get_time();
        }
        else if (esp_timer_get_time() - last > ECHO_SETTLE_US)
        {
            break; // straggler window elapsed; don't wait for a lost echo byte
        }
    }
}

void virtualDevice::adamnet_process(uint8_t b)
{
    fnDebugConsole.printf("adamnet_process() not implemented yet for this device. Cmd received: %02x\n", b);
}

void virtualDevice::adamnet_control_status()
{
    SYSTEM_BUS.start_time=esp_timer_get_time();
   adamnet_response_status();
}

void virtualDevice::adamnet_response_status()
{
    status_response.cmd_dev = (NM_STATUS << 4) | _devnum;

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
    uint8_t b;

    b = _port.read();
    int64_t cmd_start = esp_timer_get_time();
    start_time = cmd_start;
    frame_error = false;
    _tx_count = 0;
    stall_silent = false;

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

    // A handler that blocked the bus task for a long time (e.g. a multi-second
    // WiFi scan) leaves a backlog of the master's CONTROL.RECEIVE retries piled
    // up in RX. Counting our echo off that backlog would desync, so flush to the
    // next idle gap instead and let the post-command exchange start clean.
    if (stall_silent)
    {
        // The handler intentionally gave no response (disk seek stall) and the
        // master is mid re-poll. We transmitted nothing, so there is no echo to
        // drain; just yield (don't starve the UART/other tasks). Do NOT
        // discardInput() -- it would clear the FIFO and swallow the re-poll.
        //
        // EXCEPT when the stall itself ran long because it did a blocking media
        // read (a network/TNFS block fetch is 100s of ms, vs ~ms for SD). The
        // master's CONTROL.RECEIVE re-polls piled up in RX during that read;
        // replaying that stale backlog responds at the wrong times and desyncs
        // the bus. Flush to the next idle gap and let the next exchange start
        // clean -- the block is cached now, so the master's next RECEIVE delivers.
        if (esp_timer_get_time() - cmd_start > ADAMNET_LONG_CMD_US)
            wait_for_idle();
        else
            fnSystem.yield();
    }
    else if (esp_timer_get_time() - cmd_start > ADAMNET_LONG_CMD_US)
        wait_for_idle();
    // Otherwise clear our half-duplex echo before the next service pass. When the
    // command was fully handled we know exactly how many bytes we sent, so drain
    // just our echo and leave a following master command intact. Otherwise
    // (unknown/disabled device, or an aborted packet) time-flush whatever
    // unconsumed bytes remain on the bus.
    else if (_tx_count > 0 && !frame_error)
        drain_echo(_tx_count);
    else
        wait_for_idle();
}

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

void systemBus::service()
{
    // process queue messages (disk swap)
    _adamnet_process_queue();

    // Process anything waiting.
    if (_port.available() > 0)
        _adamnet_process_cmd();
}

void systemBus::setup()
{
    Debug_println("ADAMNET SETUP");

    // Set up interrupt for RESET line
    reset_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // Set up event queue
    qAdamNetMessages = xQueueCreate(4, sizeof(adamnet_message_t));

    // Start card detect task
    xTaskCreate(adamnet_reset_intr_task, "adamnet_reset_intr_task", 2048, this, 10, NULL);
    // Enable interrupt for card detection
    fnSystem.set_pin_mode(PIN_ADAMNET_RESET, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_UP, GPIO_INTR_NEGEDGE);
    // Add the card detect handler
    gpio_isr_handler_add((gpio_num_t)PIN_ADAMNET_RESET, adamnet_reset_isr_handler, (void *)PIN_CARD_DETECT_FIX);

    // Set up UART
    _port.begin(ChannelConfig()
                .deviceID(FN_UART_BUS)
                .baud(ADAMNET_BAUDRATE)
                .inverted(true)
                // Inter-byte gap tolerance for a KNOWN-LENGTH receive (read(buf,n),
                // e.g. a device receiving a block/print/network payload). The 6801
                // master pauses up to ~220us between bytes mid-transfer when its
                // ~60Hz keyboard-scan ISR fires; at the old 180us this returned the
                // buffer truncated and the tail went stale (proven on disk writes:
                // short data SENDs == corrupted block tails). We always know how
                // many bytes we want, so this is only a stalled-transfer guard --
                // make it generous; read() still returns the instant all n arrive.
                .readTimeout(2.0)
                // discardInput()'s bus-idle detector stays tight: this IS the
                // ~160us AdamNet packet-boundary heuristic, unrelated to read(buf,n).
                .discardTimeout(0.180)
                .rxThreshold(1)
                // ISR-fed TX ring so a 1028-byte block response can't underrun
                // the FIFO when the bus task is preempted by WiFi/TNFS.
                .txBuffer(2048)
                );
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
        _fujiDev = (adamFuji *)pDevice;
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
