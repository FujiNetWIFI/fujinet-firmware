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

#include <freertos/queue.h>
#include <freertos/task.h>

#include "../../include/pinmap.h"
#include "../../include/debug.h"

static QueueHandle_t drivewire_evt_queue = NULL;

drivewireDload dload;

#define DEBOUNCE_THRESHOLD_US 50000ULL

static void IRAM_ATTR drivewire_isr_handler(void *arg)
{
    // Generic default interrupt handler
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(drivewire_evt_queue, &gpio_num, NULL);
}

// Calculate 8-bit checksum
inline uint16_t drivewire_checksum(uint8_t *buf, unsigned short len)
{
    uint16_t chk = 0;

    for (int i = 0; i < len; i++)
        chk += buf[i];

    return chk;
}

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

// Helper functions outside the class defintions

systemBus virtualDevice::get_bus() { return DRIVEWIRE; }

void systemBus::op_nop()
{
    Debug_printv("op_nop()");
}

void systemBus::op_reset()
{
    Debug_printv("op_reset()");
}

void systemBus::op_readex()
{
    drivewireDisk *d = nullptr;
    uint16_t c1 = 0,c2 = 0;

    drive_num = fnUartBUS.read();

    lsn = fnUartBUS.read() << 16;
    lsn |= fnUartBUS.read() << 8;
    lsn |= fnUartBUS.read();

    Debug_printv("OP_READEX: DRIVE %3u - SECTOR %8lu", drive_num, lsn);

    if (theFuji.boot_config)
        d = theFuji.bootdisk();
    else
        d = &theFuji.get_disks(drive_num)->disk_dev;

    if (!d)
    {
        Debug_printv("Invalid drive #%3u", drive_num);
        fnUartBUS.write(0xF6);
        fnUartBUS.flush();
        fnUartBUS.flush_input();
        return;
    }

    if (!d->device_active)
    {
        Debug_printv("Device not active.");
        fnUartBUS.write(0xF6);
        fnUartBUS.flush();
        fnUartBUS.flush_input();
        return;
    }

    if (d->read(lsn, sector_data))
    {
        Debug_printf("Read error\n");
        fnUartBUS.write(0xF4);
        fnUartBUS.flush();
        fnUartBUS.flush_input();
        return;
    }

    fnUartBUS.write(sector_data, MEDIA_BLOCK_SIZE);

    c1 = (fnUartBUS.read()) << 8;
    c1 |= fnUartBUS.read();

    c2 = drivewire_checksum(sector_data,MEDIA_BLOCK_SIZE);

    if (c1 != c2)
        fnUartBUS.write(243);
    else
        fnUartBUS.write(0x00);

}

void systemBus::op_write()
{
    drivewireDisk *d = nullptr;
    uint16_t c1 = 0, c2 = 0;

    drive_num = fnUartBUS.read();

    lsn = fnUartBUS.read() << 16;
    lsn |= fnUartBUS.read() << 8;
    lsn |= fnUartBUS.read();

    size_t s = fnUartBUS.readBytes(sector_data,MEDIA_BLOCK_SIZE);

    if (s != MEDIA_BLOCK_SIZE)
    {
        Debug_printv("Insufficient # of bytes for write, total recvd: %u",s);
        fnUartBUS.flush_input();
        return;
    }

    // Todo handle checksum.
    c1 = fnUartBUS.read();
    c1 |= fnUartBUS.read() << 8;

    c2 = drivewire_checksum(sector_data,MEDIA_BLOCK_SIZE);

    if (c1 != c2)
    {
        Debug_printf("Checksum error\n");
        fnUartBUS.write(243);
        return;
    }

    Debug_printv("OP_WRITE: DRIVE %3u - SECTOR %8lu", drive_num, lsn);

    d = &theFuji.get_disks(drive_num)->disk_dev;

    if (!d)
    {
        Debug_printv("Invalid drive #%3u", drive_num);
        fnUartBUS.write(0xF6);
        return;
    }

    if (!d->device_active)
    {
        Debug_printv("Device not active.");
        fnUartBUS.write(0xF6);
        return;
    }

    if (d->write(lsn,sector_data))
    {
        Debug_print("Write error\n");
        fnUartBUS.write(0xF5);
        return;
    }
}

void systemBus::op_fuji()
{
    Debug_printv("OP FUJI!");
    theFuji.process();
}

void systemBus::op_unhandled(uint8_t c)
{
    Debug_printv("Unhandled opcode: %02x",c);

    while (fnUartBUS.available())
        Debug_printf("%02x ",fnUartBUS.read());

    fnUartBUS.flush_input();
}

void systemBus::op_time()
{
    time_t tt = time(nullptr);
    struct tm * now = localtime(&tt);

    now->tm_mon++;

    Debug_printf("Returning %02d/%02d/%02d %02d:%02d:%02d\n", now->tm_year, now->tm_mon, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);

    fnUartBUS.write(now->tm_year-1900);
    fnUartBUS.write(now->tm_mon);
    fnUartBUS.write(now->tm_mday);
    fnUartBUS.write(now->tm_hour);
    fnUartBUS.write(now->tm_min);
    fnUartBUS.write(now->tm_sec);
}

void systemBus::op_init()
{
    Debug_printv("OP_INIT");
}

void systemBus::op_dwinit()
{
    Debug_printv("OP_DWINIT - Sending feature byte 0x%02x",DWINIT_FEATURES);
    fnUartBUS.write(DWINIT_FEATURES);
}

void systemBus::op_getstat()
{
    Debug_printv("OP_GETSTAT: 0x%02x",fnUartBUS.read());
}

void systemBus::op_setstat()
{
    Debug_printv("OP_SETSTAT: 0x%02x",fnUartBUS.read());
}

void systemBus::op_serread()
{
    // TODO: Temporary until modem and network are working
    fnUartBUS.write(0x00);
    fnUartBUS.write(0x00);
}

void systemBus::op_print()
{
    _printerdev->write(fnUartBUS.read());
}

// Read and process a command frame from DRIVEWIRE
void systemBus::_drivewire_process_cmd()
{
    uint8_t c = fnUartBUS.read();

    switch (c)
    {
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
    default:
        op_unhandled(c);
        break;
    }
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
    // Handle cassette play if MOTOR pin active.
    if (_cassetteDev)
    {
        if (motorActive)
        {
            _cassetteDev->play();
            return;
        }
    }

    if (fnUartBUS.available())
        _drivewire_process_cmd();

    // dload.dload_process();
}

// Setup DRIVEWIRE bus
void systemBus::setup()
{
    // Create a queue to handle parallel event from ISR
    drivewire_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Start task
    // xTaskCreate(drivewire_intr_task, "drivewire_intr_task", 2048, NULL, 10, NULL);
    xTaskCreatePinnedToCore(drivewire_intr_task, "drivewire_intr_task", 4096, this, 10, NULL, 0);

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

    // Start in DRIVEWIRE mode
    fnUartBUS.begin(57600);
    Debug_printv("DRIVEWIRE MODE");
}

// Give devices an opportunity to clean up before a reboot
void systemBus::shutdown()
{
    shuttingDown = true;

    // TODO: implement device shutdown for all sub-busses

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