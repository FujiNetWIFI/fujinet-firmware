#ifdef BUILD_COCO

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

#ifdef ESP_PLATFORM
static void drivewire_intr_task(void *arg)
{
    uint32_t gpio_num;
    int64_t d;

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

// Helper functions outside the class defintions

systemBus virtualDevice::get_bus() { return DRIVEWIRE; }

void systemBus::op_jeff()
{
    fnDwCom.print("FUJINET");
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
}

void systemBus::op_readex()
{
    drivewireDisk *d = nullptr;
    uint16_t c1 = 0, c2 = 0;

    drive_num = fnDwCom.read();

    lsn = fnDwCom.read() << 16;
    lsn |= fnDwCom.read() << 8;
    lsn |= fnDwCom.read();

    Debug_printf("OP_READ: DRIVE %3u - SECTOR %8lu\n", drive_num, lsn);

    if (theFuji.boot_config)
        d = theFuji.bootdisk();
    else
        d = &theFuji.get_disks(drive_num)->disk_dev;

    if (!d)
    {
        Debug_printv("Invalid drive #%3u", drive_num);
        fnDwCom.write(0xF6);
        fnDwCom.flush();
        fnDwCom.flush_input();
        
        return;
    }

    if (!d->device_active)
    {
        Debug_printv("Device not active.");
        fnDwCom.write(0xF6);
        fnDwCom.flush();
        fnDwCom.flush_input();
        return;
    }

    if (d->read(lsn, sector_data))
    {
        Debug_printf("Read error\n");
        fnDwCom.write(0xF4);
        fnDwCom.flush();
        fnDwCom.flush_input();
        return;
    }

    fnDwCom.write(sector_data, MEDIA_BLOCK_SIZE);

    c1 = (fnDwCom.read()) << 8;
    c1 |= fnDwCom.read();

    c2 = drivewire_checksum(sector_data, MEDIA_BLOCK_SIZE);

    if (c1 != c2)
        fnDwCom.write(243);
    else
        fnDwCom.write(0x00);
}

void systemBus::op_write()
{
    drivewireDisk *d = nullptr;
    uint16_t c1 = 0, c2 = 0;

    drive_num = fnDwCom.read();

    lsn = fnDwCom.read() << 16;
    lsn |= fnDwCom.read() << 8;
    lsn |= fnDwCom.read();

    size_t s = fnDwCom.readBytes(sector_data, MEDIA_BLOCK_SIZE);

    if (s != MEDIA_BLOCK_SIZE)
    {
        Debug_printv("Insufficient # of bytes for write, total recvd: %u", s);
        fnDwCom.flush_input();
        return;
    }

    // Todo handle checksum.
    c1 = fnDwCom.read();
    c1 |= fnDwCom.read() << 8;

    c2 = drivewire_checksum(sector_data, MEDIA_BLOCK_SIZE);

    // if (c1 != c2)
    // {
    //     Debug_printf("Checksum error\n");
    //     fnDwCom.write(243);
    //     return;
    // }

    Debug_printf("OP_WRITE DRIVE %3u - SECTOR %8lu\n", drive_num, lsn);

    d = &theFuji.get_disks(drive_num)->disk_dev;

    if (!d)
    {
        Debug_printv("Invalid drive #%3u", drive_num);
        fnDwCom.write(0xF6);
        return;
    }

    if (!d->device_active)
    {
        Debug_printv("Device not active.");
        fnDwCom.write(0xF6);
        return;
    }

    if (d->write(lsn, sector_data))
    {
        Debug_print("Write error\n");
        fnDwCom.write(0xF5);
        return;
    }

    fnDwCom.write(0x00); // success
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
    uint8_t device_id = (uint8_t)fnDwCom.read();

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

    while (fnDwCom.available())
        Debug_printf("%02x ", fnDwCom.read());

    fnDwCom.flush_input();
}

void systemBus::op_time()
{
    time_t tt = time(nullptr);
    struct tm *now = localtime(&tt);

    now->tm_mon++;

    Debug_printf("Returning %02d/%02d/%02d %02d:%02d:%02d\n", now->tm_year, now->tm_mon, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);

    fnDwCom.write(now->tm_year - 1900);
    fnDwCom.write(now->tm_mon);
    fnDwCom.write(now->tm_mday);
    fnDwCom.write(now->tm_hour);
    fnDwCom.write(now->tm_min);
    fnDwCom.write(now->tm_sec);
}

void systemBus::op_init()
{
    Debug_printv("OP_INIT");
}

void systemBus::op_dwinit()
{
    Debug_printv("OP_DWINIT - Sending feature byte 0x%02x", DWINIT_FEATURES);
    fnDwCom.write(DWINIT_FEATURES);
}

void systemBus::op_getstat()
{
    Debug_printv("OP_GETSTAT: 0x%02x", fnDwCom.read());
}

void systemBus::op_setstat()
{
    Debug_printv("OP_SETSTAT: 0x%02x", fnDwCom.read());
}

void systemBus::op_serread()
{
    // TODO: Temporary until modem and network are working
    fnDwCom.write(0x00);
    fnDwCom.write(0x00);
}

void systemBus::op_print()
{
    _printerdev->write(fnDwCom.read());
}

// Read and process a command frame from DRIVEWIRE
void systemBus::_drivewire_process_cmd()
{
    int c = fnDwCom.read();
    if (c < 0)
    {
        Debug_println("Failed to read cmd!");
        return;
    }

    fnLedManager.set(eLed::LED_BUS, true);

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
    case OP_DWINIT:
        op_dwinit();
        break;
    case OP_SERREAD:
        op_serread();
        break;
    case OP_PRINT:
        op_print();
        break;
    case OP_PRINTFLUSH:
        // Not needed.
        break;
        // case OP_GETSTAT:
        //     op_getstat();
        //     break;
        // case OP_SETSTAT:
        //     op_setstat();
        //     break;

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

    if (fnDwCom.available())
        _drivewire_process_cmd();

    fnDwCom.poll(1);

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
    _modemDev->get_uart()->set_baudrate(baud);
}

systemBus DRIVEWIRE; // Global DRIVEWIRE object
#endif               /* BUILD_COCO */