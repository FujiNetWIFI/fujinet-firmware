#ifdef BUILD_COCO

#include <queue>

#include "drivewire.h"

#include "../../include/debug.h"

#include "fuji.h"
#include "udpstream.h"
#include "modem.h"
#include "cassette.h"
#include "printer.h"
#include "drivewire/dload.h"
#include "../../lib/device/drivewire/cpm.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnDNS.h"
#include "led.h"
#include "utils.h"

#ifdef ESP_PLATFORM
#include <freertos/queue.h>
#include <freertos/task.h>
#endif

#include "../../include/pinmap.h"
#include "../../include/debug.h"

#ifdef ESP_PLATFORM
static QueueHandle_t drivewire_evt_queue = NULL;
#endif

drivewireDload dload;

// Host & client channel queues
std::queue<char> outgoingChannel[16];
std::queue<char> incomingChannel[16];

#define DEBOUNCE_THRESHOLD_US 50000ULL

#ifdef ESP_PLATFORM
static void IRAM_ATTR drivewire_isr_handler(void *arg)
{
    // Generic default interrupt handler
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(drivewire_evt_queue, &gpio_num, NULL);
}
#endif

/**
 * Static callback function for the DriveWire state machine.
 */
#ifdef ESP_PLATFORM
void onTimer(void *info)
{
    systemBus *parent = (systemBus *)info;
    parent->resetState();
}
#endif

/**
 * Start the DriveWire state machine recovery timer.
 */
void systemBus::timer_start()
{
#ifdef ESP_PLATFORM
    esp_timer_create_args_t tcfg;
    tcfg.arg = this;
    tcfg.callback = onTimer;
    tcfg.dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK;
    tcfg.name = nullptr;
    esp_timer_create(&tcfg, &stateMachineRecoveryTimerHandle);
    esp_timer_start_periodic(stateMachineRecoveryTimerHandle, timerRate * 1000);
#else
    timerActive = true;
    lastInterruptMs = fnSystem.millis();
#endif
}

/**
 * Stop the DriveWire state machine recovery timer
 */
void systemBus::timer_stop()
{
#ifdef ESP_PLATFORM
    // Delete existing timer
    if (stateMachineRecoveryTimerHandle != nullptr)
    {
        Debug_println("Deleting existing DriveWire state machine timer\n");
        esp_timer_stop(stateMachineRecoveryTimerHandle);
        esp_timer_delete(stateMachineRecoveryTimerHandle);
        stateMachineRecoveryTimerHandle = nullptr;
    }
#else
    timerActive = false;
#endif
}

// Calculate 8-bit checksum
inline uint16_t drivewire_checksum(uint8_t *buf, unsigned short len)
{
    uint16_t chk = 0;

    for (int i = 0; i < len; i++)
        chk += buf[i];

    return chk;
}

#if defined(ESP_PLATFORM) && 1 == 0
static void drivewire_intr_task(void *arg)
{
    uint32_t gpio_num;

    systemBus *bus = (systemBus *)arg;

    while (true)
    {
        if (xQueueReceive(drivewire_evt_queue, &gpio_num, portMAX_DELAY))
        {
            esp_rom_delay_us(DEBOUNCE_THRESHOLD_US);

            if (gpio_num == PIN_CASS_MOTOR && gpio_get_level((gpio_num_t)gpio_num))
            {
                bus->motorActive = true;
            }
            else
            {
                bus->motorActive = false;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS); // avoid spinning too fast...
    }
}
#endif

// Helper functions outside the class definintions

systemBus virtualDevice::get_bus() { return DRIVEWIRE; }



void systemBus::resetState(void)
{
    dwStateMethod = &systemBus::_drivewire_process_cmd;
    timer_stop();
}

int systemBus::op_jeff(std::vector<uint8_t> *q)
{
    int result = 1;
    
    resetState();
    Debug_println("OP_JEFF\n");
    
    return result;
}

int systemBus::op_nop(std::vector<uint8_t> *q)
{
    int result = 1;
    
    resetState();

    return result;
}

int systemBus::op_reset(std::vector<uint8_t> *q)
{
    int result = 1;
    
    Debug_printv("op_reset()");

    // When a reset transaction occurs, set the mounted disk image to the CONFIG disk image.
    theFuji.boot_config = true;
    theFuji.insert_boot_device(Config.get_general_boot_mode());

    for (int i = 0; i < 16; i++)
    {
        // clear all channel queues
        fnDwCom.outgoingChannel[i].clear();
        fnDwCom.incomingChannel[i].clear();
        fnDwCom.incomingScreen[i].clear();
    }
    
    resetState();
    
    return result;
}

int readexChecksum = 0;
int readexError = 0;

int systemBus::op_readex(std::vector<uint8_t> *q)
{
    int result = 0;
    readexChecksum = 0;
    
    if (q->size() >= 5) {
        uint8_t drive_num = q->at(1);
        int lsn = q->at(2) << 16 | q->at(3) << 8 | q->at(4);
        drivewireDisk *d = nullptr;
        uint8_t sector_data[MEDIA_BLOCK_SIZE];
        uint8_t *blk_buffer = sector_data;
        uint16_t blk_size = MEDIA_BLOCK_SIZE;
        
        result = 5;
        
        Debug_printf("OP_READEX: DRIVE %3u - SECTOR %8lu\n", drive_num, lsn);
    
        if (theFuji.boot_config && drive_num == 0)
            d = theFuji.bootdisk();
        else
            d = &theFuji.get_disks(drive_num)->disk_dev;
    
        if (!d)
        {
            Debug_printv("Invalid drive #%3u", drive_num);
            readexError = 0xF6;
        }
    
        if (readexError == DISK_CTRL_STATUS_CLEAR && !d->device_active)
        {
            Debug_printv("Device not active.");
            readexError = 0xF6;
        }
    
        if (readexError == DISK_CTRL_STATUS_CLEAR)
        {
            bool use_media_buffer = true;
            d->get_media_buffer(&blk_buffer, &blk_size);
            if (blk_buffer == nullptr || blk_size == 0)
            {
                // no media buffer, use "bus buffer" with default block size
                blk_buffer = sector_data;
                blk_size = MEDIA_BLOCK_SIZE;
                use_media_buffer = false;
            }
    
            if (d->read(lsn, use_media_buffer ? nullptr : sector_data))
            {
                if (d->get_media_status() == 2)
                {
                    Debug_printf("EOF\n");
                    readexError = 211;
                }
                else
                {
                    Debug_printf("Read error\n");
                    readexError = 0xF4;
                }
            }
        }
    
        // send zeros on error
        if (readexError != DISK_CTRL_STATUS_CLEAR)
        {
            memset(blk_buffer, 0x00, blk_size);
        }
    
        readexChecksum = drivewire_checksum(blk_buffer, blk_size);

        // send sector data
        fnDwCom.write(blk_buffer, blk_size);
    
        fnDwCom.flush();

        dwStateMethod = &systemBus::op_readex_p2;    
    }
            
    return result;
}
    
int systemBus::op_readex_p2(std::vector<uint8_t> *q)
{
    int result = 0;
    
    if (q->size() >= 2) {
        // We read 2 bytes into this buffer (guest's checksum).
        // Here we're expecting the checksum from the guest.
        result = 2; 
        
        int guestChecksum = q->at(0) * 256 + q->at(1);
        if (readexChecksum != guestChecksum) {
            Debug_printf("Checksum error: expected %d, got %d\n", readexChecksum, guestChecksum);
            readexError = 243;
        }
        
        // send status
        fnDwCom.write(readexError);

        resetState();
    }
    
    return result;
}

int systemBus::op_write(std::vector<uint8_t> *q)
{
    int result = 0;
    int rc = 0;
    int expectedResult = 263;
    
    if (q->size() >= expectedResult) {
        resetState();
        result = expectedResult;
        
        int drive_num = q->at(1);
        int lsn = q->at(2) << 16 | q->at(3) << 8 | q->at(4);
        std::vector<uint8_t> sector_data(256);
        std::copy(q->begin() + 5, q->begin() + 260, sector_data.begin());
        int checksum = q->at(261)*256 + q->at(262);

        int computedChecksum = drivewire_checksum(sector_data.data(), MEDIA_BLOCK_SIZE);
        
        if (computedChecksum == checksum) {
            Debug_printf("OP_WRITE DRIVE %3u - SECTOR %8lu\n", drive_num, lsn);
            drivewireDisk *d = &theFuji.get_disks(drive_num)->disk_dev;
        
            if (!d)
            {
                Debug_printv("Invalid drive #%3u", drive_num);
                rc = 0xF6;
            }
            else if (!d->device_active)
            {
                Debug_printv("Device not active.");
                rc = 0xF6;
            }
            else if (d->write(lsn, sector_data.data()))
            {
                Debug_print("Write error\n");
                rc = 0xF5;
            }
        }
        else
        {
            rc = 243;
        }

        resetState();
    }        
        
    return result;
}

int systemBus::op_fuji(std::vector<uint8_t> *q)
{
    int result = 0;
    
    result = theFuji.process(q);
    
    if (result > 0)
    {
        Debug_printv("OP_FUJI");
        Debug_printf("result = %d\n", result);
    }
    
    return result;
}

int systemBus::op_cpm(std::vector<uint8_t> *q)
{
    int result = 1;
    
#ifdef ESP_PLATFORM
    theCPM.process();
#endif /* ESP_PLATFORM */

    resetState();
    Debug_printv("OP_CPM");    

    return result;
}

int systemBus::op_net(std::vector<uint8_t> *q)
{
    int result = 1;
    
    // Get device ID
    uint8_t device_id = (uint8_t)fnDwCom.read();

    // If device doesn't exist, create it.
    if (!_netDev.contains(device_id))
    {
        Debug_printf("Opening new network device %u\n",device_id);
        _netDev[device_id] = new drivewireNetwork();
    }

    // And pass control to it
    _netDev[device_id]->process();
    resetState();
    Debug_printf("OP_NET: %u\n", device_id);
    
    return result;
}

int systemBus::op_unhandled(std::vector<uint8_t> *q)
{
    int result = 1;
    
    resetState();
    Debug_printv("Unhandled opcode: %02x", q->at(0));
    
    return result;
}

int systemBus::op_time(std::vector<uint8_t> *q)
{
    int result = 1;
    
    time_t tt = time(nullptr);
    struct tm *now = localtime(&tt);

    now->tm_mon++;

    Debug_printf("Returning %02d/%02d/%02d %02d:%02d:%02d\n", now->tm_year, now->tm_mon, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);

    fnDwCom.write(now->tm_year);
    fnDwCom.write(now->tm_mon);
    fnDwCom.write(now->tm_mday);
    fnDwCom.write(now->tm_hour);
    fnDwCom.write(now->tm_min);
    fnDwCom.write(now->tm_sec);
    
    resetState();
    Debug_printv("OP_TIME");    
    
    return result;
}

int systemBus::op_init(std::vector<uint8_t> *q)
{
    int result = 1;
    
    resetState();
    Debug_printv("OP_INIT");
    
    return result;
}

int systemBus::op_term(std::vector<uint8_t> *q)
{
    int result = 1;
    
    resetState();
    Debug_printv("OP_TERM");
    
    return result;
}

int systemBus::op_serinit(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 2;
    
    if (q->size() >= expectedResult)
    {
        // Clear the outgoing channel.
        result = expectedResult;
        int vchan = q->at(1);
        fnDwCom.outgoingChannel[vchan].clear();
        resetState();
        Debug_printv("OP_SERINIT");
    }
        
    return result;
}

int systemBus::op_serterm(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 2;
    
    if (q->size() >= expectedResult)
    {
        // Clear the outgoing channel.
        result = expectedResult;
        int vchan = q->at(1);
        fnDwCom.outgoingChannel[vchan].clear();
        resetState();
        Debug_printv("OP_SERTERM");
    }
        
    return result;
}

int systemBus::op_dwinit(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 2;
    
    if (q->size() >= expectedResult)
    {
        result = expectedResult;
        
        // save capabilities byte
        guestCapabilityByte = q->at(1);
        uint8_t hostResponse = 0x00;
        
        if (guestCapabilityByte == 1)
        {
            // OS-9 is the only environment that uses OP_DWINIT.
            // dwio sends OP_DWINIT followed by $01.
            // If the host responds with $04, dwio starts the
            // virtual interrupt service routine to poll for input,
            // so we'll respond with that value here.
            hostResponse = 0x04;
            fnDwCom.pollingMode = true;
        }
        fnDwCom.write(hostResponse);

        resetState();
        Debug_printv("OP_DWINIT: %02x", guestCapabilityByte);
    }
    
    return result;
}

int systemBus::op_getstat(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 3;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        int lastDriveNumber = q->at(1);
        int lastGetStat = q->at(2);
        resetState();
        Debug_printv("OP_GETSTAT: 0x%02x 0x%02x", lastDriveNumber, lastGetStat);
    }
    
    return result;
}

int systemBus::op_setstat(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 3;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        int lastDriveNumber = q->at(1);
        int lastSetStat = q->at(2);
        resetState();
        Debug_printv("OP_SETSTAT: 0x%02x 0x%02x", lastDriveNumber, lastSetStat);
    }
    
    return result;
}

int systemBus::op_sergetstat(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 3;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        int lastChannelNumber = q->at(1);
        int lastGetStat = q->at(2);
        resetState();
        Debug_printv("OP_SERGETSTAT: 0x%02x 0x%02x", lastChannelNumber, lastGetStat);
    }
    
    return result;
}

int systemBus::op_sersetstat(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 3;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        int lastChannelNumber = q->at(1);
        int lastSetStat = q->at(2);
        
        if (lastSetStat == 0x28)
        {
            dwStateMethod = &systemBus::op_sersetstat_comstat;
        }
        else
        {
            resetState();
        }
        Debug_printv("OP_SERSETSTAT: 0x%02x 0x%02x", lastChannelNumber, lastSetStat);
    }
    
    return result;
}

int systemBus::op_sersetstat_comstat(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 26;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        resetState();
    }
    
    return result;
}

int systemBus::op_serread(std::vector<uint8_t> *q)
{
    int result = 1;
    uint8_t responseByte1 = 0x00;
    uint8_t responseByte2 = 0x00;
    bool hasData = false;
    
    // scan client channels for first that has available data    
    for (int i = 0; i < 16; i++) {
        if (fnDwCom.outgoingChannel[i].empty() == false) {
            responseByte1 = (i + 1); // virtual channel indicator
            int sizeInChannel = fnDwCom.outgoingChannel[i].size();
            if (sizeInChannel > 1)
            {
                responseByte1 |= 0x10; // multibyte response
                responseByte2 = (sizeInChannel > 16) ? 16 : sizeInChannel;
            }
            else
            {
                responseByte2 = fnDwCom.outgoingChannel[i].front();
                fnDwCom.outgoingChannel[i].erase(fnDwCom.outgoingChannel[i].begin());
            }
            fnDwCom.write(responseByte1); 
            fnDwCom.write(responseByte2);
            Debug_printv("OP_SERREAD: response1 $%02x - response2 $%02x\n", responseByte1, responseByte2);
            hasData = true;
            break;
        }
    }
    
    if (hasData == false)
    {
        responseByte1 = 0x00;
        responseByte2 = 0x00;
        fnDwCom.write(responseByte1); 
        fnDwCom.write(responseByte2);
        Debug_printv("OP_SERREAD: response1 $%02x - response2 $%02x\n", responseByte1, responseByte2);
    }
    
    resetState();    
    
    return result;
}

int systemBus::op_serreadm(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 3;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        uint8_t vchan = q->at(1);
        uint8_t count = q->at(2);
    
        Debug_printv("OP_SERREADM: vchan $%02x - count $%02x\n", vchan, count);

        // scan client channels for first that has available data    
        if (fnDwCom.outgoingChannel[vchan].empty() == false) {
            if (fnDwCom.outgoingChannel[vchan].size() < count) count = fnDwCom.outgoingChannel[vchan].size();
            for (int i = 0; i < count; i++) {
                int response = fnDwCom.outgoingChannel[vchan].front();
                fnDwCom.outgoingChannel[vchan].erase(fnDwCom.outgoingChannel[vchan].begin());
                fnDwCom.write(response);
                Debug_printv("Data to client -> $%02x", response);
            }
        }

        resetState();    
    }
    
    return result;
}

int systemBus::op_serwrite(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 3;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        uint8_t vchan = q->at(1);
        uint8_t byte = q->at(2);
    
        fnDwCom.incomingChannel[vchan].push_back(byte);
        resetState();
        Debug_printv("OP_SERWRITE: vchan $%02x - byte $%02x\n", vchan, byte);
    }
    
    return result;
}

int systemBus::op_serwritem(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 3;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        uint8_t vchan = q->at(1);
        uint8_t count = q->at(2);
    
        for (int i = 0; i < count; i++) {
            int byte = q->at(i+3);
            fnDwCom.incomingChannel[vchan].push_back(byte);
        }
        
        resetState();
        Debug_printv("OP_SERWRITEM: vchan $%02x\n", vchan);
    }
    
    return result;
}

int systemBus::op_print(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 2;
    
    if (q->size() >= expectedResult) {
        uint8_t byte = q->at(1);
        _printerdev->write(byte);
        
        resetState();
        Debug_printv("OP_PRINT: byte $%02x\n", byte);
    }
    
    return result;
}

int systemBus::op_printflush(std::vector<uint8_t> *q)
{
    int result = 1;
        
    resetState();
    Debug_printv("OP_PRINTFLUSH\n");

    return result;
}

int systemBus::op_fastwrite_serial(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 2;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        int vchan = q->at(0) & 0x7F;
        uint8_t c = q->at(1);
        fnDwCom.incomingChannel[vchan].push_back(c);
    
        resetState();
        Debug_printv("OP_FASTWRITE (serial): vchan = $%02x, byte $%02x\n", vchan, c);
    }
    
    return result;
}

int systemBus::op_fastwrite_screen(std::vector<uint8_t> *q)
{
    int result = 0;
    int expectedResult = 2;
    
    if (q->size() >= expectedResult) {
        result = expectedResult;
        int vchan = q->at(0) & 0x7F;
        uint8_t c = q->at(1);
        fnDwCom.incomingChannel[vchan].push_back(c);
        
        resetState();
        Debug_printv("OP_FASTWRITE (serial): vchan = $%02x, byte $%02x\n", vchan, c);
    }
    
    return result;
}

// Read and process a command frame from DRIVEWIRE
int systemBus::_drivewire_process_cmd(std::vector<uint8_t> *q)
{
    int result = 0;
    
    timer_start();
    
    uint8_t c = q->at(0);
    
    fnLedManager.set(eLed::LED_BUS, true);

    if (c >= 0x80 && c <= 0x8E)
    {
        // handle FASTWRITE serial
        dwStateMethod = &systemBus::op_fastwrite_serial;
    }
    else if (c >= 0x91 && c <= 0x9E)
    {
        // handle FASTWRITE virtual screen
        dwStateMethod = &systemBus::op_fastwrite_screen;
    }
    else
    {
        switch (c)
        {
        case OP_JEFF:
            dwStateMethod = &systemBus::op_jeff;
		    break;
	    case OP_NOP:
            dwStateMethod = &systemBus::op_nop;
            break;
        case OP_RESET1:
        case OP_RESET2:
        case OP_RESET3:
            dwStateMethod = &systemBus::op_reset;
            break;
        case OP_READEX:
            dwStateMethod = &systemBus::op_readex;
            break;
        case OP_WRITE:
            dwStateMethod = &systemBus::op_write;
            break;
        case OP_TIME:
            dwStateMethod = &systemBus::op_time;
            break;
        case OP_INIT:
            dwStateMethod = &systemBus::op_init;
            break;
        case OP_SERINIT:
            dwStateMethod = &systemBus::op_serinit;
            break;
        case OP_DWINIT:
            dwStateMethod = &systemBus::op_dwinit;
            break;
        case OP_SERREAD:
            dwStateMethod = &systemBus::op_serread;
            break;
        case OP_SERREADM:
            dwStateMethod = &systemBus::op_serreadm;
            break;
        case OP_SERWRITE:
            dwStateMethod = &systemBus::op_serwrite;
            break;
        case OP_SERWRITEM:
            dwStateMethod = &systemBus::op_serwritem;
            break;
        case OP_PRINT:
            dwStateMethod = &systemBus::op_print;
            break;
        case OP_PRINTFLUSH:
            dwStateMethod = &systemBus::op_printflush;
            break;
        case OP_GETSTAT:
            dwStateMethod = &systemBus::op_getstat;
            break;
        case OP_SETSTAT:
            dwStateMethod = &systemBus::op_setstat;
            break;
        case OP_SERGETSTAT:
            dwStateMethod = &systemBus::op_sergetstat;
            break;
        case OP_SERSETSTAT:
            dwStateMethod = &systemBus::op_sersetstat;
            break;
        case OP_TERM:
            dwStateMethod = &systemBus::op_term;
            break;
        case OP_SERTERM:
            dwStateMethod = &systemBus::op_serterm;
            break;
        case OP_FUJI:
            q->erase(q->begin()); // lob off OP_FUJI so parsing in fuji.cpp can go smoothly
            dwStateMethod = &systemBus::op_fuji;
            break;
        case OP_NET:
            dwStateMethod = &systemBus::op_net;
            break;
        case OP_CPM:
            dwStateMethod = &systemBus::op_cpm;
            break;
        default:
            dwStateMethod = &systemBus::op_unhandled;
            break;
        }
    }
    
    result = (this->*dwStateMethod)(q);

    fnLedManager.set(eLed::LED_BUS, false);
    
    return result;
}

// Look to see if we have any waiting messages and process them accordingly
void systemBus::_drivewire_process_queue()
{
}

/*
 Primary DRIVEWIRE serivce loop:
 * If MOTOR line asserted, hand DRIVEWIRE processing over to the TAPE device
 * If CMD line asserted, try reading CMD frame and sending it to appropriate device
 * If CMD line not asserted but MODEM is active, give it a chance to read incoming data
 * Throw out stray input on DRIVEWIRE if neither of the above two are true
 * Give NETWORK devices an opportunity to signal available data
 */
void systemBus::service()
{
#ifdef ESP_PLATFORM
    // Handle cassette play if MOTOR pin active.
    if (_cassetteDev)
    {
        if (motorActive)
        {
            _cassetteDev->play();
            return;
        }
    }
#endif

    // check and assert interrupts if needed for any open
    // network device.
    if (!_netDev.empty())
    {
        for (auto it=_netDev.begin(); it != _netDev.end(); ++it)
        {
            it->second->poll_interrupt();
        }
    }

    // TODO: read from serial port...
//    if (fnDwCom.available())
//        _drivewire_process_cmd();
//    fnDwCom.poll(1);

    int gotNewData = 0;
    
    while (fnDwCom.available()) {
        int byte = fnDwCom.read();
        serialBuffer.push_back(byte);
        gotNewData = 1;
    }
    
    int showDump = 1;
    
#ifdef ESP_PLATFORM
#else
    uint64_t ms = fnSystem.millis();
#if 0
    if (ms % 100 == 0) {
        Debug_printv("MS = %ld, lastInterruptMs = %ld, TIMER = %ld\n", ms, lastInterruptMs, (timerActive == true && ms - lastInterruptMs >= timerRate));
    }
#endif
    if (timerActive == true && ms - lastInterruptMs >= timerRate)
    {
        Debug_printv("State Reset Timer INVOKED!\n");
        resetState();
    }
#endif

    if (gotNewData == 1 && showDump == 1 && serialBuffer.size() > 0)
    {
        for (int i = 0; i < serialBuffer.size(); i++)
        {
            Debug_printf("$%02x ", serialBuffer.at(i));
        }
        
        Debug_printf("=====================================\n");    
    }
    
    if (gotNewData == 1)
    {
        int bytesConsumed = 0;
        
        do {
            bytesConsumed = (this->*dwStateMethod)(&serialBuffer);
            
            if (bytesConsumed > 0 && serialBuffer.size() >= bytesConsumed) {
                // chop off consumed bytes
                serialBuffer.erase(serialBuffer.begin(), serialBuffer.begin() + bytesConsumed);
            }
        } while (bytesConsumed > 0 && serialBuffer.size() > 0);
    }
        
    // dload.dload_process();
}

// Setup DRIVEWIRE bus
void systemBus::setup()
{
#ifdef ESP_PLATFORM
    // Create a queue to handle parallel event from ISR
    drivewire_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Start task
    // xTaskCreate(drivewire_intr_task, "drivewire_intr_task", 2048, NULL, 10, NULL);
    // xTaskCreatePinnedToCore(drivewire_intr_task, "drivewire_intr_task", 4096, this, 10, NULL, 0);

    // Setup interrupt for cassette motor pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_CASS_MOTOR), // bit mask of the pins that you want to set
        .mode = GPIO_MODE_INPUT,                  // set as input mode
        .pull_up_en = GPIO_PULLUP_DISABLE,        // disable pull-up mode
        .pull_down_en = GPIO_PULLDOWN_ENABLE,     // enable pull-down mode
        .intr_type = GPIO_INTR_POSEDGE            // interrupt on positive edge
    };

    _cassetteDev = new drivewireCassette();

    // configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_isr_handler_add((gpio_num_t)PIN_CASS_MOTOR, drivewire_isr_handler, (void *)PIN_CASS_MOTOR);

    // Configure CD pin.
    fnSystem.set_pin_mode(PIN_CD, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    fnSystem.digital_write(PIN_CD, DIGI_HIGH);

    // Start in DRIVEWIRE mode
    // Set the initial buad rate based on which ROM image is selected by the A14/A15 dip switch on Rev000 or newer.
    // If using an older Rev0 or Rev00 board, you will need to pull PIN_EPROM_A14 (IO36) up to 3.3V or 5V via a 10K
    // resistor to have it default to the previous default of 57600 baud otherwise they will both read as low and you
    // will get 38400 baud.

    fnSystem.set_pin_mode(PIN_EPROM_A14, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE);
    fnSystem.set_pin_mode(PIN_EPROM_A15, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE);

    #ifdef FORCE_UART_BAUD
        Debug_printv("FORCE_UART_BAUD set to %u",FORCE_UART_BAUD);
        _drivewireBaud = FORCE_UART_BAUD;
    #else
    if (fnSystem.digital_read(PIN_EPROM_A14) == DIGI_LOW && fnSystem.digital_read(PIN_EPROM_A15) == DIGI_LOW)
    {
        _drivewireBaud = 38400; //Coco1 ROM Image
        Debug_printv("A14 Low, A15 Low, 38400 baud");
    }
    else if (fnSystem.digital_read(PIN_EPROM_A14) == DIGI_HIGH && fnSystem.digital_read(PIN_EPROM_A15) == DIGI_LOW)
    {
        _drivewireBaud = 57600; //Coco2 ROM Image
        Debug_printv("A14 High, A15 Low, 57600 baud");
    }
    else if  (fnSystem.digital_read(PIN_EPROM_A14) == DIGI_LOW && fnSystem.digital_read(PIN_EPROM_A15) == DIGI_HIGH)
    {
        _drivewireBaud = 115200; //Coco3 ROM Image
        Debug_printv("A14 Low, A15 High, 115200 baud");
    }
    else
    {
        _drivewireBaud = 57600; //Default or no switch
        Debug_printv("A14 and A15 High, defaulting to 57600 baud");
    }

    #endif /* FORCE_UART_BAUD */
#else
    // FujiNet-PC specific
    fnDwCom.set_serial_port(Config.get_serial_port().c_str()); // UART
    _drivewireBaud = Config.get_serial_port_baud();
#endif
    fnDwCom.set_becker_host(Config.get_boip_host().c_str(), Config.get_boip_port()); // Becker
    fnDwCom.set_drivewire_mode(Config.get_boip_enabled() ? DwCom::dw_mode::BECKER : DwCom::dw_mode::SERIAL);

    resetState();
    
    fnDwCom.begin(_drivewireBaud);
    fnDwCom.flush_input();
    Debug_printv("DRIVEWIRE MODE");
}

// Give devices an opportunity to clean up before a reboot
void systemBus::shutdown()
{
    shuttingDown = true;

    // TODO: implement device shutdown for all sub-busses

    for (std::map<uint8_t, drivewireNetwork *>::iterator it = _netDev.begin();
         it != _netDev.end();
         ++it)
    {
        Debug_printf("Shutting down network device ID: %u\n",it->first);

        if (it->second != nullptr)
            delete it->second;
    }

    Debug_printf("Clearing Network Device array.\n");
    _netDev.clear();

    Debug_printf("All devices shut down.\n");
}

void systemBus::toggleBaudrate()
{
}

int systemBus::getBaudrate()
{
    return _drivewireBaud;
}

void systemBus::setBaudrate(int baud)
{
    if (_drivewireBaud == baud)
    {
        Debug_printf("Baudrate already at %d - nothing to do\n", baud);
        return;
    }

    Debug_printf("Changing baudrate from %d to %d\n", _drivewireBaud, baud);
    _drivewireBaud = baud;
    //_modemDev->get_uart()->set_baudrate(baud); // TODO COME BACK HERE.
}

systemBus DRIVEWIRE; // Global DRIVEWIRE object
#endif               /* BUILD_COCO */