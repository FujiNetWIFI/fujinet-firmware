#ifdef BUILD_COCO

#include <queue>

#include "drivewire.h"

#include "../../include/debug.h"

#include "fujiDevice.h"
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

#ifdef ESP_PLATFORM
#define DW_UART_DEVICE FN_UART_BUS
#else /* !ESP_PLATFORM */
#define DW_UART_DEVICE Config.get_serial_port()
#endif /* ESP_PLATFORM */

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

// Calculate 8-bit checksum
inline uint16_t drivewire_checksum(uint8_t *buf, unsigned short len)
{
    uint16_t chk = 0;

    for (int i = 0; i < len; i++)
        chk += buf[i];

    return chk;
}

#ifdef ENABLE_DRIVEWIRE_INTR_TASK
#ifdef ESP_PLATFORM
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
#endif /* ESP_PLATFORM */
#endif /* ENABLE_DRIVEWIRE_INTR_TASK */

// Helper functions outside the class defintions

void systemBus::op_jeff()
{
    _port->print("FUJINET");
    Debug_println("Jeff's op");
}

void systemBus::op_nop()
{
}

void systemBus::op_reset()
{
    Debug_printv("op_reset()");

    // When a reset transaction occurs, set the mounted disk image to the CONFIG disk image.
    theFuji.boot_config = true;
    theFuji.insert_boot_device(Config.get_general_boot_mode());
    if (pNamedObjFp != NULL)
    {
        fclose(pNamedObjFp);
        pNamedObjFp = NULL;
    }
}

void systemBus::op_readex()
{
    drivewireDisk *d = nullptr;
    uint16_t c1 = 0, c2 = 0;

    uint8_t *blk_buffer = sector_data;
    uint16_t blk_size = MEDIA_BLOCK_SIZE;

    uint8_t rc = DISK_CTRL_STATUS_CLEAR;

    drive_num = _port->read();

    lsn = _port->read() << 16;
    lsn |= _port->read() << 8;
    lsn |= _port->read();

    Debug_printf("OP_READ: DRIVE %3u - SECTOR %8lu\n", drive_num, lsn);

    // named object support for dragon
    if (strlen((const char*)szNamedMount))
    {
        Debug_printf("op_readex: boot from named object %s\r\n", szNamedMount);
        Debug_printf("OP_READEX: DRIVE %3u - SECTOR %8lu\n", drive_num, lsn);

        if (NULL==pNamedObjFp)
            pNamedObjFp = fopen((const char*)szNamedMount, "r");
        if (NULL==pNamedObjFp)
        {
            Debug_printf("op_readex: boot from named object %s\r\n", "/AUTOLOAD.DWL");
            pNamedObjFp = fopen((const char*)"/AUTOLOAD.DWL", "r");
        }

        if (pNamedObjFp)
        {
            size_t bytesrd;
            Debug_printf("op_readex: open successful %s\r\n", szNamedMount);

            memset(blk_buffer,0,MEDIA_BLOCK_SIZE);
            bytesrd = fread(blk_buffer,1,MEDIA_BLOCK_SIZE,pNamedObjFp);
            if (bytesrd!=MEDIA_BLOCK_SIZE || feof(pNamedObjFp))
            {
                Debug_printf("op_readex: closed named object %s\r\n",szNamedMount);
                fclose(pNamedObjFp);
                pNamedObjFp = NULL;
                // serve up named object only one time
                szNamedMount[0]=(uint8_t)0;
            }
        }
        else
        {
            Debug_printf("op_readex: open failed %s\r\n", szNamedMount);
            _port->write(0xF4);
            return;
        }
    }
    else
    {
        if (true==bDragon && drive_num>=5) drive_num = drive_num-5;
    Debug_printf("OP_READ: DRIVE %3u - SECTOR %8lu\n", drive_num, lsn);

        if (theFuji.boot_config && drive_num == 0)
            d = theFuji.bootdisk();
        else
            d = &theFuji.get_disks(drive_num)->disk_dev;

        if (!d)
        {
            Debug_printv("Invalid drive #%3u", drive_num);
            rc = 0xF6;
        }

        if (rc == DISK_CTRL_STATUS_CLEAR && !d->device_active && !bDragon)
        {
            Debug_printv("Device not active.");
            rc = 0xF6;
        }

        if (rc == DISK_CTRL_STATUS_CLEAR)
        {
            bool use_media_buffer = true;
            d->get_media_buffer(&blk_buffer, &blk_size);
            if (blk_buffer == nullptr || blk_size == 0)
            {
                Debug_printv("No media buffer, using bus buffer.");
                // no media buffer, use "bus buffer" with default block size
                blk_buffer = sector_data;
                blk_size = MEDIA_BLOCK_SIZE;
                use_media_buffer = false;
            }

            if (bDragon)
            {
                Debug_printf("dragon read\n");
                if (d->read(lsn, sector_data))
                {
                    Debug_printf("Read error\n");
                    _port->write(0xF4);
                    _port->flushOutput();
                    _port->discardInput();
                    return;
                }
            }
            else
            {
                Debug_printf("non-dragon read\n");
                if (d->read(lsn, use_media_buffer ? nullptr : sector_data))
                {
                    if (d->get_media_status() == 2)
                    {
                        Debug_printf("EOF\n");
                        rc = 211;
                    }
                    else
                    {
                        Debug_printf("Read error\n");
                        rc = 0xF4;
                    }
                }
            }
        }
        // send zeros on error
        if (rc != DISK_CTRL_STATUS_CLEAR)
            memset(blk_buffer, 0x00, blk_size);
    }
    // send sector data
    _port->write(blk_buffer, blk_size);

    // receive checksum
    c1 = (_port->read()) << 8;
    c1 |= _port->read();

    // test checksum
    if (rc == DISK_CTRL_STATUS_CLEAR)
    {
        c2 = drivewire_checksum(blk_buffer, blk_size);

        if (c1 != c2)
        {
            Debug_printf("Checksum error: expected %d, got %d\n", c2, c1);
            rc = 243;
        }
    }

    // finally, send the transaction status
    _port->write(rc);
    _port->flushOutput();
}


void systemBus::op_write()
{
    drivewireDisk *d = nullptr;
    uint16_t c1 = 0;

    drive_num = _port->read();

    lsn = _port->read() << 16;
    lsn |= _port->read() << 8;
    lsn |= _port->read();

    size_t s = _port->read(sector_data, MEDIA_BLOCK_SIZE);

    if (s != MEDIA_BLOCK_SIZE)
    {
        Debug_printv("Insufficient # of bytes for write, total recvd: %u", s);
        _port->discardInput();
        return;
    }

    // Todo handle checksum.
    c1 = _port->read();
    c1 |= _port->read() << 8;

    drivewire_checksum(sector_data, MEDIA_BLOCK_SIZE);

    // if (c1 != c2)
    // {
    //     Debug_printf("Checksum error\n");
    //     _port->write(243);
    //     return;
    // }

    Debug_printf("OP_WRITE DRIVE %3u - SECTOR %8lu\n", drive_num, lsn);

    d = &theFuji.get_disks(drive_num)->disk_dev;

    if (!d)
    {
        Debug_printv("Invalid drive #%3u", drive_num);
        _port->write(0xF6);
        return;
    }

    if (!d->device_active)
    {
        Debug_printv("Device not active.");
        _port->write(0xF6);
        return;
    }

    if (d->write(lsn, sector_data))
    {
        Debug_print("Write error\n");
        _port->write(0xF5);
        return;
    }

    _port->write(0x00); // success
}

void systemBus::op_fuji()
{
    theFuji.process();
}

void systemBus::op_cpm()
{
#ifdef ESP_PLATFORM
    theCPM.process();
#endif /* ESP_PLATFORM */
}

void systemBus::op_net()
{
    // Get device ID
    uint8_t device_id = (uint8_t)_port->read();

    // If device doesn't exist, create it.
    if (!_netDev.contains(device_id))
    {
        Debug_printf("Opening new network device %u\n",device_id);
        _netDev[device_id] = new drivewireNetwork();
    }

    // And pass control to it
    Debug_printf("OP_NET: %u\n",device_id);
    _netDev[device_id]->process();
}

void systemBus::op_unhandled(uint8_t c)
{
    Debug_printv("Unhandled opcode: %02x", c);

    while (_port->available())
        Debug_printf("%02x ", _port->read());

    _port->discardInput();
}

void systemBus::op_time()
{
    time_t tt = time(nullptr);
    struct tm *now = localtime(&tt);

    now->tm_mon++;

    Debug_printf("Returning %02d/%02d/%02d %02d:%02d:%02d\n", now->tm_year, now->tm_mon, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);

    _port->write(now->tm_year);
    _port->write(now->tm_mon);
    _port->write(now->tm_mday);
    _port->write(now->tm_hour);
    _port->write(now->tm_min);
    _port->write(now->tm_sec);
}

void systemBus::op_init()
{
    Debug_printv("OP_INIT");
}

void systemBus::op_serinit()
{
    Debug_printv("OP_SERINIT");
    _port->read();
}

void systemBus::op_serterm()
{
    Debug_printv("OP_SERTERM");
    _port->read();
}

void systemBus::op_dwinit()
{
    Debug_printv("OP_DWINIT - Sending feature byte 0x%02x", DWINIT_FEATURES);
#define OS9 1
#ifdef OS9
    _port->write(0x04);
#else
    _port->write(DWINIT_FEATURES);
#endif
}

void systemBus::op_getstat()
{
    Debug_printv("OP_GETSTAT: 0x%02x 0x%02x", _port->read(),_port->read());
}

void systemBus::op_setstat()
{
    Debug_printv("OP_SETSTAT: 0x%02x 0x%02x", _port->read(),_port->read());
}

void systemBus::op_sergetstat()
{
    unsigned char vchan = _port->read();
    unsigned char code = _port->read();
    Debug_printv("OP_SERGETSTAT: 0x%02x 0x%02x", vchan, code);
}

void systemBus::op_sersetstat()
{
    unsigned char vchan = _port->read();
    unsigned char code = _port->read();
    Debug_printv("OP_SERSETSTAT: 0x%02x 0x%02x", vchan, code);
    if (code == 0x28) {
        for (int i = 0; i < 26; i++) {
            _port->read();
        }
    }
}

void systemBus::op_serread()
{
    unsigned char vchan = 0;
    unsigned char response = 0x00;

    // scan client channels for first that has available data
    for (int i = 0; i < 16; i++) {
        if (outgoingChannel[i].empty() == false) {
            response = outgoingChannel[i].front();
            outgoingChannel[i].pop();
            vchan = i;
            break;
        }
    }

    _port->write(vchan);
    _port->write(response);

    Debug_printv("OP_SERREAD: vchan $%02x - response $%02x\n", vchan, response);
}

void systemBus::op_serreadm()
{
    unsigned char vchan = _port->read();
    unsigned char count = _port->read();

    // scan client channels for first that has available data
    for (vchan = 0; vchan < 16; vchan++) {
        if (outgoingChannel[vchan].empty() == false) {
            if (outgoingChannel[vchan].size() < count) count = outgoingChannel[vchan].size();
            for (int i = 0; i < count; i++) {
                int response = outgoingChannel[vchan].front();
                outgoingChannel[vchan].pop();
                _port->write(response);
                Debug_printv("OP_SERREADM: vchan $%02x - response $%02x\n", vchan, response);
            }
            break;
        }
    }
}

void systemBus::op_serwrite()
{
    unsigned char vchan = _port->read();
    unsigned char byte = _port->read();
    incomingChannel[vchan].push(byte);
    Debug_printv("OP_SERWRITE: vchan $%02x - byte $%02x\n", vchan, byte);
}

void systemBus::op_serwritem()
{
    unsigned char vchan, count;

    vchan = _port->read();
    _port->read(); // discard
    count = _port->read();

    for (int i = 0; i < count; i++) {
        int byte = _port->read();
        incomingChannel[vchan].push(byte);
        Debug_printv("OP_SERWRITE: vchan $%02x - byte $%02x\n", vchan, byte);
    }
}

void systemBus::op_print()
{
    _printerdev->write(_port->read());
}

void systemBus::op_namedobj_mnt()
{
    uint8_t namelen;
    uint16_t i;

    namelen = _port->read();
    memset(szNamedMount,0,sizeof(szNamedMount));
    for (i=0; i<namelen;i++)
        szNamedMount[i]= _port->read();

    _port->write(0x01);
    Debug_printf("op_namedobj_mnt: %s\r\n", szNamedMount);

    // treat this event as a reset...
    op_reset();
}

// Read and process a command frame from DRIVEWIRE
void systemBus::_drivewire_process_cmd()
{
    int c = _port->read();
    if (c < 0)
    {
        Debug_println("Failed to read cmd!");
        return;
    }

    fnLedManager.set(eLed::LED_BUS, true);

    if (c >= 0x80 && c <= 0x8F) {
        // handle FASTWRITE here
        int vchan = c & 0xF;
        int byte = _port->read();
        incomingChannel[vchan].push(byte);
    } else {
        switch (c)
        {
        case OP_JEFF:
            op_jeff();
            break;
        case OP_NOP:
            op_nop();
            break;
        case OP_RESET1:
        case OP_RESET2:
        case OP_RESET3:
            op_reset();
            break;
        case OP_READEX:
            op_readex();
            break;
        case OP_WRITE:
            op_write();
            break;
        case OP_TIME:
            op_time();
            break;
        case OP_INIT:
            op_init();
            break;
        case OP_SERINIT:
            op_serinit();
            break;
        case OP_DWINIT:
            op_dwinit();
            break;
        case OP_SERREAD:
            op_serread();
            break;
        case OP_SERREADM:
            op_serreadm();
            break;
        case OP_SERWRITE:
            op_serwrite();
            break;
        case OP_SERWRITEM:
            op_serwritem();
            break;
        case OP_PRINT:
            op_print();
            break;
        case OP_NAMEOBJ_MNT:
            op_namedobj_mnt();
            break;
        case OP_PRINTFLUSH:
            // Not needed.
            break;
        case OP_GETSTAT:
            op_getstat();
            break;
        case OP_SETSTAT:
            op_setstat();
            break;
        case OP_SERGETSTAT:
            op_sergetstat();
            break;
        case OP_SERSETSTAT:
            op_sersetstat();
            break;
        case OP_TERM:
            Debug_printf("OP_TERM!\n");
            break;
        case OP_SERTERM:
            op_serterm();
            break;
        case OP_FUJI:
            op_fuji();
            break;
        case OP_NET:
            op_net();
            break;
        case OP_CPM:
            op_cpm();
            break;
        default:
            op_unhandled(c);
            break;
        }
    }

    fnLedManager.set(eLed::LED_BUS, false);
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

    if (_port->available())
        _drivewire_process_cmd();

    // dload.dload_process();
}

#ifdef ESP_PLATFORM
void systemBus::configureGPIO()
{
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

    return;
}

int systemBus::readBaudSwitch()
{
    if (fnSystem.digital_read(PIN_EPROM_A14) == DIGI_LOW
        && fnSystem.digital_read(PIN_EPROM_A15) == DIGI_LOW)
    {
        Debug_printv("A14 Low, A15 Low, 38400 baud");
        return 38400; //Coco1 ROM Image
    }

    if (fnSystem.digital_read(PIN_EPROM_A14) == DIGI_HIGH
             && fnSystem.digital_read(PIN_EPROM_A15) == DIGI_LOW)
    {
        Debug_printv("A14 High, A15 Low, 57600 baud");
        return 57600; //Coco2 ROM Image
    }

    if  (fnSystem.digital_read(PIN_EPROM_A14) == DIGI_LOW
              && fnSystem.digital_read(PIN_EPROM_A15) == DIGI_HIGH)
    {
        Debug_printv("A14 Low, A15 High, 115200 baud");
        return 115200; //Coco3 ROM Image
    }

    Debug_printv("A14 and A15 High, defaulting to 57600 baud");
    return 57600; //Default or no switch
}
#endif /* ESP_PLATFORM */

// Setup DRIVEWIRE bus
void systemBus::setup()
{
#ifdef ESP_PLATFORM
    // Create a queue to handle parallel event from ISR
    drivewire_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    bDragon = false;

#ifdef CONFIG_IDF_TARGET_ESP32S3
    // Configure UART to RP2040
  #ifdef FORCE_UART_BAUD
    Debug_printv("FORCE_UART_BAUD set to %u", FORCE_UART_BAUD);
    _drivewireBaud = FORCE_UART_BAUD;
  #else /* !FORCE_UART_BAUD */
    _drivewireBaud = 115200;
  #endif /* FORCE_UART_BAUD */

#else /* ! CONFIG_IDF_TARGET_ESP32S3 */
    configureGPIO();
  #ifdef FORCE_UART_BAUD
    Debug_printv("FORCE_UART_BAUD set to %u",FORCE_UART_BAUD);
    _drivewireBaud = FORCE_UART_BAUD;
  #else /* !FORCE_UART_BAUD */
    _drivewireBaud = readBaudSwitch();
  #endif /* FORCE_UART_BAUD */
#endif /* CONFIG_IDF_TARGET_ESP32S3 */
#else /* !ESP_PLATFORM */
    // FujiNet-PC specific
    _drivewireBaud = Config.get_serial_baud();
#endif /* !ESP_PLATFORM */

    if (Config.get_boip_enabled())
    {
        _becker.begin(Config.get_boip_host(), _drivewireBaud);
        _port = &_becker;
    }
    else
    {
        _serial.begin(ChannelConfig()
                      .baud(_drivewireBaud)
                      .deviceID(DW_UART_DEVICE)
                      .readTimeout(500)
#ifdef ESP_PLATFORM
                      .inverted(DW_UART_DEVICE == UART_NUM_2)
#endif /* ESP_PLATFORM */
                      );
        _port = &_serial;
    }

    _port->discardInput();
    Debug_printv("DRIVEWIRE MODE");

    // jeff hack to see if the S3 is getting serial data
    // Debug_println("now receiving data...");
    // uint8_t b[] = {' '};
    // while(1)
    // {
    //     while (_port->available())
    //     {
    //         _port->read(b,1);
    //         Debug_printf("%c\n",b[0]);
    //     }
    // }
    // end jeff hack

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
#endif               /* BUILD_COCO */
