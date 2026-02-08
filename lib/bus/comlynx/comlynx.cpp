#ifdef BUILD_LYNX

/**
 * Comlynx Functions
 */
#include "comlynx.h"
#include "udpstream.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnDNS.h"
#include "led.h"
#include <cstring>

#define IDLE_TIME 500 // Idle tolerance in microseconds (roughly three characters at 62500 baud)

//systemBus SYSTEM_BUS.;

static QueueHandle_t reset_evt_queue = NULL;

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
    Debug_printf("comlynx_send_buffer: %X\n", b);

    // Wait for idle only when in UDPStream mode
    if (SYSTEM_BUS._udpDev->udpstreamActive)
        SYSTEM_BUS.wait_for_idle();

    // Write the byte
    SYSTEM_BUS.write(b);
    SYSTEM_BUS.flush();
    SYSTEM_BUS.read();
}

void virtualDevice::comlynx_send_buffer(uint8_t *buf, unsigned short len)
{
    //Debug_printf("comlynx_send_buffer: len:%d %0X %0X %0X %0X %0X %0X\n", len, buf[0], buf[1], buf[2], buf[3], buf[4], buf[len-1]);
    Debug_printf("comlynx_send_buffer: len:%d\n", len);

    // Wait for idle only when in UDPStream mode
    if (SYSTEM_BUS._udpDev->udpstreamActive)
        SYSTEM_BUS.wait_for_idle();

    SYSTEM_BUS.write(buf, len);
    SYSTEM_BUS.read(buf, len);
}

bool virtualDevice::comlynx_recv_ck()
{
    uint8_t recv_ck, ck;


    while (SYSTEM_BUS.available() <= 0)
        fnSystem.yield();

    // get checksum
    recv_ck = SYSTEM_BUS.read();

    ck = comlynx_checksum(recvbuffer, recvbuffer_len);

    // debugging checksum values
    //Debug_printf("comlynx_recv_ck, recv:%02X calc:%02X\n", recv_ck, ck);

    // reset receive buffer
    recvbuffer_len = 0;

    if (recv_ck == ck)
        return true;
    else
        return false;
}

uint8_t virtualDevice::comlynx_recv()
{
    uint8_t b;

    while (SYSTEM_BUS.available() <= 0)
        fnSystem.yield();

    b = SYSTEM_BUS.read();

    // Add to receive buffer
    recvbuffer[recvbuffer_len] = b;
    recvbuffer_len++;

    //Debug_printf("comlynx_recv: %x\n", b);
    return b;
}

bool virtualDevice::comlynx_recv_timeout(uint8_t *b, uint64_t dur)
{
    uint64_t start, current, elapsed;
    bool timeout = true;

    start = current = esp_timer_get_time();
    elapsed = 0;

    while (SYSTEM_BUS.available() <= 0)
    {
        current = esp_timer_get_time();
        elapsed = current - start;
        if (elapsed > dur)
            break;
    }

    if (SYSTEM_BUS.available() > 0)
    {
        *b = (uint8_t)SYSTEM_BUS.read();
        timeout = false;
    } // else
      //   Debug_printf("duration: %llu\n", elapsed);

    return timeout;
}

uint16_t virtualDevice::comlynx_recv_length()
{
    unsigned short l = 0;
    l = comlynx_recv() << 8;
    l |= comlynx_recv();

    if (l > 1024)
        l = 1024;

    // Reset recv buffer, but maybe we want checksum over the length too? -SJ
    recvbuffer_len = 0;

    return l;
}

void virtualDevice::comlynx_send_length(uint16_t l)
{
    comlynx_send(l >> 8);
    comlynx_send(l & 0xFF);
}

unsigned short virtualDevice::comlynx_recv_buffer(uint8_t *buf, unsigned short len)
{
    unsigned short b;

    b = SYSTEM_BUS.read(buf, len);

    // Add to receive buffer
    //memcpy(&recvbuffer[recvbuffer_len], buf, len);
    recvbuffer_len = len;               // length of payload
    recvbuf_pos = &recvbuffer[0];       // pointer into payload

    return(b);
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
    //comlynx_send(0x90 | _devnum);
    comlynx_send(FUJICMD_ACK);
}

void virtualDevice::comlynx_response_nack()
{
    //comlynx_send(0xC0 | _devnum);
    comlynx_send(FUJICMD_NAK);
}

/*void virtualDevice::comlynx_control_ready()
{
    comlynx_response_ack();
}*/

bool systemBus::wait_for_idle()
{
    int64_t start, current, dur;

    // SJ notes: we really don't need to do this unless we are in UDPStream mode
    // Likely we want to just wait until the bus is "idle" for about 3 character times
    // which is about 0.5 ms at 62500 baud 8N1
    //
    // Check that the bus is truly idle for the whole duration, and then we can start sending?

    start = esp_timer_get_time();

    do {
        current = esp_timer_get_time();
        dur = current - start;

        // Did we get any data in the FIFO while waiting?
        if (SYSTEM_BUS.available() > 0)
            return false;

    } while (dur < IDLE_TIME);

    // Must have been idle at least IDLE_TIME to get here
    return true;

    //fnSystem.yield();         // not sure if we need to do this, from old function - SJ
}

void virtualDevice::comlynx_process()
{
    fnDebugConsole.printf("comlynx_process() not implemented yet for this device.\n");
}

/*void virtualDevice::comlynx_control_status()
{
    //SYSTEM_BUS.start_time = esp_timer_get_time();
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
*/
// void virtualDevice::comlynx_status()
//{
//     fnUartDebug.printf("comlynx_status() not implemented yet for this device.\n");
// }

void systemBus::_comlynx_process_cmd()
{
    uint8_t d;

    d = SYSTEM_BUS.read();

    // Find device ID and pass control to it
    if (_daisyChain.count(d) < 1)
    {
    }
    else if (_daisyChain[d]->device_active == true)
    {
    /*#ifdef DEBUG
        if ((b & 0xF0) == (MN_ACK<<4))
            Debug_println("Lynx sent ACK");
        else {
                Debug_println("---");
            Debug_printf("comlynx_process_cmd: dev:%X cmd:%X\n", d, (b & 0xF0)>>4);
        }
    #endif*/

    #ifdef DEBUG
        Debug_println("---");
        Debug_printf("comlynx_process_cmd - dev:%Xn", d);
    #endif

        // turn on Comlynx Indicator LED
        fnLedManager.set(eLed::LED_BUS, true);
        _daisyChain[d]->comlynx_process();
        // turn off Comlynx Indicator LED
        fnLedManager.set(eLed::LED_BUS, false);
    }

    SYSTEM_BUS.flush();
}

void systemBus::_comlynx_process_queue()
{
}

void systemBus::service()
{
    // Handle UDP Stream if active
    if (_udpDev != nullptr && _udpDev->udpstreamActive)
        _udpDev->comlynx_handle_udpstream();
    // Process anything waiting
    else if (SYSTEM_BUS.available() > 0)
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

    // Set up UART
    _port.begin(ChannelConfig()
                .deviceID(FN_UART_BUS)
                .baud(COMLYNX_BAUDRATE)
                .parity(UART_PARITY_ODD)
                );
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

void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id)
{
    Debug_printf("Adding device: %02X\n", device_id);
    pDevice->_devnum = device_id;
    _daisyChain[device_id] = pDevice;

    switch (device_id)
    {
    case FUJI_DEVICEID_PRINTER:
        _printerDev = (lynxPrinter *)pDevice;
        break;
    case FUJI_DEVICEID_FUJINET:
        _fujiDev = (lynxFuji *)pDevice;
        break;
    default:
        break;
    }
}

bool systemBus::deviceExists(fujiDeviceID_t device_id)
{
    return _daisyChain.find(device_id) != _daisyChain.end();
}

bool systemBus::deviceEnabled(fujiDeviceID_t device_id)
{
    if (deviceExists(device_id))
        return _daisyChain[device_id]->device_active;
    else
        return false;
}

void systemBus::remDevice(virtualDevice *pDevice)
{
}

void systemBus::remDevice(fujiDeviceID_t device_id)
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

void systemBus::changeDeviceId(virtualDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep.second == p)
            devicep.second->_devnum = (fujiDeviceID_t) device_id;
    }
}

virtualDevice *systemBus::deviceById(fujiDeviceID_t device_id)
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

void systemBus::enableDevice(fujiDeviceID_t device_id)
{
    Debug_printf("Enabling Comlynx Device %d\n", device_id);

    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = true;
}

void systemBus::disableDevice(fujiDeviceID_t device_id)
{
    Debug_printf("Disabling Comlynx Device %d\n", device_id);

    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = false;
}

void systemBus::setUDPHost(const char *hostname, int port)
{
    // Turn off if hostname is STOP
    if (hostname != nullptr && !strcmp(hostname, "STOP"))
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
    if (_udpDev->udpstreamActive) {
        _udpDev->comlynx_disable_udpstream();
        _udpDev->comlynx_disable_redeye();
    }
    if (_udpDev->udpstream_host_ip != IPADDR_NONE) {
        _udpDev->comlynx_enable_udpstream();
        if (_udpDev->redeye_mode)
            _udpDev->comlynx_enable_redeye();
    }
}


void systemBus::setRedeyeMode(bool enable)
{
    Debug_printf("setRedeyeMode, %d\n", enable);
    _udpDev->redeye_mode = enable;
    _udpDev->redeye_logon = true;
}


void systemBus::setRedeyeGameRemap(uint32_t remap)
{
    Debug_printf("setRedeyeGameRemap, %d\n", remap);

    // handle pure updstream games
    if ((remap >> 8) == 0xE1) {
        _udpDev->redeye_mode = false;           // turn off redeye
        _udpDev->redeye_logon = true;           // reset logon phase toggle
        _udpDev->redeye_game = remap;           // set game, since we can't detect it
    }

    // handle redeye game that need remapping
    if (remap != 0xFFFF) {
        _udpDev->remap_game_id = true;
        _udpDev->new_game_id = remap;
    }
    else {
        _udpDev->remap_game_id = false;
        _udpDev->new_game_id = 0xFFFF;
    }
}


#endif /* BUILD_LYNX */
