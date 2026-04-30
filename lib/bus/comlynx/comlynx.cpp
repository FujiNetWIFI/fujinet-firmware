#ifdef BUILD_LYNX

/**
 * Comlynx Functions
 */
#include "comlynx.h"
#include "netstream.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnDNS.h"
#include "led.h"
#include <cstring>

#define IDLE_TIME 500 // Idle tolerance in microseconds (roughly three characters at 62500 baud)

uint8_t comlynx_checksum(uint8_t *buf, unsigned short len)
{
    uint8_t checksum = 0x00;

    for (unsigned short i = 0; i < len; i++)
        checksum ^= buf[i];

    return checksum;
}

void virtualDevice::comlynx_send(uint8_t b)
{
    //Debug_printf("comlynx_send_buffer - %X\n", b);

    // Wait for idle only when in netstream mode
    if (SYSTEM_BUS.netstreamActive())
        SYSTEM_BUS.wait_for_idle();

    // Write the byte
    SYSTEM_BUS.write(b);
    SYSTEM_BUS.read();
}

void virtualDevice::comlynx_send_buffer(uint8_t *buf, unsigned short len)
{
    Debug_printf("comlynx_send_buffer - len:%d\n", len);

    // Wait for idle only when in netstream mode
    if (SYSTEM_BUS.netstreamActive())
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

/*bool virtualDevice::comlynx_recv_timeout(uint8_t *b, uint64_t dur)
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
}*/

uint16_t virtualDevice::comlynx_recv_length()
{
    unsigned short l = 0;
    l = comlynx_recv() << 8;
    l |= comlynx_recv();

    if (l > 1024)
        l = 1024;

    // Reset recv buffer
    recvbuffer_len = 0;
    recvbuf_pos = &recvbuffer[0];

    return l;
}

void virtualDevice::comlynx_send_length(uint16_t l)
{
    comlynx_send(l >> 8);
    comlynx_send(l & 0xFF);

    #ifdef DEBUG
        Debug_printf("comlynx_send_length - len: %ld\n", (long int)l);
    #endif
}

unsigned short virtualDevice::comlynx_recv_buffer(uint8_t *buf, unsigned short len)
{
    unsigned short b;

    b = SYSTEM_BUS.read(buf, len);

    // Add to receive buffer
    memcpy(recvbuffer, buf, len);
    recvbuffer_len = len;               // length of payload
    recvbuf_pos = &recvbuffer[0];       // pointer into payload

    return(b);
}

void virtualDevice::reset()
{
    Debug_printf("No Reset implemented for device %u\n", _devnum);
}

void virtualDevice::comlynx_response_ack()
{
    comlynx_send(FUJICMD_ACK);
}

void virtualDevice::comlynx_response_nack()
{
    comlynx_send(FUJICMD_NAK);
}

bool systemBus::wait_for_idle()
{
    int64_t start, current, dur;

    // SJ notes: we really don't need to do this unless we are in netstream mode
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

bool systemBus::netstreamActive() const
{
    return _streamDev != nullptr && _streamDev->netstreamActive;
}

void virtualDevice::comlynx_process()
{
    fnDebugConsole.printf("comlynx_process() not implemented yet for this device.\n");
}

void systemBus::_comlynx_process_cmd()
{
    uint8_t d;

    d = SYSTEM_BUS.read();

    for (auto devicep : _daisyChain)
    {
        if (d == devicep->_devnum)
        {
            //_activeDev = devicep;
            // handle command
            //_activeDev->sio_process(tempFrame.commanddata, tempFrame.checksum);

            #ifdef DEBUG
            Debug_println("---");
            Debug_printf("comlynx_process_cmd - dev:%X\n", d);
            #endif

            // turn on Comlynx Indicator LED
            fnLedManager.set(eLed::LED_BUS, true);
            devicep->comlynx_process();
            // turn off Comlynx Indicator LED
            fnLedManager.set(eLed::LED_BUS, false);
        }
    }

    // Find device ID and pass control to it
    /*if (_daisyChain.count(d) < 1)
    {
    }
    else if (_daisyChain[d]->device_active == true)
    {
     #ifdef DEBUG
        Debug_println("---");
        Debug_printf("comlynx_process_cmd - dev:%X\n", d);
    #endif

        // turn on Comlynx Indicator LED
        fnLedManager.set(eLed::LED_BUS, true);
        _daisyChain[d]->comlynx_process();
        // turn off Comlynx Indicator LED
        fnLedManager.set(eLed::LED_BUS, false);
    }*/

    SYSTEM_BUS.flush();
}

void systemBus::_comlynx_process_queue()
{
}

void systemBus::service()
{
    // Handle NetStream if active
    if (_streamDev != nullptr && _streamDev->netstreamActive)
        _streamDev->comlynx_handle_netstream();
    // Process anything waiting
    else if (SYSTEM_BUS.available() > 0)
        _comlynx_process_cmd();
}

void systemBus::setup()
{
    Debug_println("COMLYNX SETUP");

    // Set up NetStream device
    //_streamDev = new lynxnetstream();

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
        Debug_printf("Shutting down device %02x\n", devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id)
{
    Debug_printf("Adding device: %02X\n", device_id);

    if (device_id == FUJI_DEVICEID_FUJINET)
    {
        _fujiDev = (lynxFuji *)pDevice;
    }
    else if (device_id >= FUJI_DEVICEID_NETWORK && device_id <= FUJI_DEVICEID_NETWORK_LAST)
    {
        _netDev[device_id - FUJI_DEVICEID_NETWORK] = (lynxNetwork*)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_PRINTER)
    {
        _printerDev = (lynxPrinter *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_MIDI)
    {
        _streamDev = (lynxNetStream *)pDevice;
    }

    pDevice->_devnum = device_id;
    _daisyChain.push_front(pDevice);
}

void systemBus::remDevice(virtualDevice *pDevice)
{
    _daisyChain.remove(pDevice);
}

void systemBus::remDevice(fujiDeviceID_t device_id)
{
}

int systemBus::numDevices()
{
      int i = 0;
    //__BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    //__END_IGNORE_UNUSEDVARS
}

void systemBus::changeDeviceId(virtualDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = (fujiDeviceID_t) device_id;
    }
}

virtualDevice *systemBus::deviceById(fujiDeviceID_t device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

void systemBus::reset()
{
    for (auto devicep : _daisyChain)
        devicep->reset();
}

void systemBus::enableDevice(fujiDeviceID_t device_id)
{
}

void systemBus::disableDevice(fujiDeviceID_t device_id)
{
}

void systemBus::setStreamHost(const char *hostname, int port)
{
    // Turn off if hostname is STOP
    if (hostname != nullptr && !strcmp(hostname, "STOP"))
    {
        if (_streamDev->netstreamActive)
            _streamDev->comlynx_disable_netstream();

        return;
    }

    if (hostname != nullptr && hostname[0] != '\0')
    {
        // Try to resolve the hostname and store that so we don't have to keep looking it up
        _streamDev->netstream_host_ip = get_ip4_addr_by_name(hostname);
        //_streamDev->netstream_host_ip = IPADDR_NONE;

        if (_streamDev->netstream_host_ip == IPADDR_NONE)
        {
            Debug_printf("Failed to resolve hostname \"%s\"\n", hostname);
        }
    }
    else
    {
        _streamDev->netstream_host_ip = IPADDR_NONE;
    }

    if (port > 0 && port <= 65535)
    {
        _streamDev->netstream_port = port;
    }
    else
    {
        _streamDev->netstream_port = 5004;
        Debug_printf("netstream port not provided or invalid (%d), setting to 5004\n", port);
    }

    // Restart NetStream mode if needed
    if (_streamDev->netstreamActive) {
        _streamDev->comlynx_disable_netstream();
        _streamDev->comlynx_disable_redeye();
    }
    if (_streamDev->netstream_host_ip != IPADDR_NONE) {
        _streamDev->comlynx_enable_netstream();
        if (_streamDev->redeye_mode)
            _streamDev->comlynx_enable_redeye();
    }
}

void systemBus::setRedeyeMode(bool enable)
{
    Debug_printf("setRedeyeMode, %d\n", enable);
    _streamDev->redeye_mode = enable;
    _streamDev->redeye_logon = true;
}

void systemBus::setRedeyeGameRemap(uint32_t remap)
{
    Debug_printf("setRedeyeGameRemap, %d\n", (int) remap);

    // handle pure updstream games
    if ((remap >> 8) == 0xE1) {
        _streamDev->redeye_mode = false;           // turn off redeye
        _streamDev->redeye_logon = true;           // reset logon phase toggle
        _streamDev->redeye_game = remap;           // set game, since we can't detect it
    }

    // handle redeye game that need remapping
    if (remap != 0xFFFF) {
        _streamDev->remap_game_id = true;
        _streamDev->new_game_id = remap;
    }
    else {
        _streamDev->remap_game_id = false;
        _streamDev->new_game_id = 0xFFFF;
    }
}

void virtualDevice::transaction_continue(transState_t expectMoreData)
{    
}

void virtualDevice::transaction_complete()
{
    Debug_println("transaction_complete - sent ACK");
    comlynx_response_ack();
}

void virtualDevice::transaction_error()
{
    Debug_println("transaction_error - send NAK");
    comlynx_response_nack();
    
    // throw away any waiting bytes
    while (SYSTEM_BUS.available() > 0)
        SYSTEM_BUS.read();
}
    
success_is_true virtualDevice::transaction_get(void *data, size_t len) 
{
    size_t remaining = recvbuffer_len - (recvbuf_pos - recvbuffer);
    size_t to_copy = (len > remaining) ? remaining : len;

    memcpy(data, recvbuf_pos, to_copy);
    recvbuf_pos += to_copy;

    RETURN_SUCCESS_IF(to_copy != 0);
}

void virtualDevice::transaction_put(const void *data, size_t len, bool err)
{
    uint8_t b;

    // set response buffer
    memcpy(response, data, len);
    response_len = len;

    // send all data back to Lynx
    uint8_t ck = comlynx_checksum(response, response_len);
    comlynx_send_length(response_len);
    comlynx_send_buffer(response, response_len);
    comlynx_send(ck);

    // get ACK or NACK from Lynx, we're ignoring currently
    uint8_t r = comlynx_recv();
    #ifdef DEBUG
        if (r == FUJICMD_ACK)
            Debug_println("transaction_put - Lynx ACKed");
        else
            Debug_println("transaction put - Lynx NAKed");
    #endif

    return;
}

#endif /* BUILD_LYNX */
