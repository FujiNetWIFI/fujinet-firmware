#ifdef BUILD_COCO

#include "drivewire.h"

#include "../../include/debug.h"

#include "fuji.h"
#include "udpstream.h"
#include "modem.h"
#include "cassette.h"
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

static void IRAM_ATTR drivewire_isr_handler(void *arg)
{
    // Generic default interrupt handler
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(drivewire_evt_queue, &gpio_num, NULL);
}

static void drivewire_intr_task(void *arg)
{
    uint32_t gpio_num;
    systemBus *bus = (systemBus *)arg;

    while (true)
    {
        if (xQueueReceive(drivewire_evt_queue, &gpio_num, portMAX_DELAY))
        {
            if (gpio_num == PIN_CASS_MOTOR && gpio_get_level((gpio_num_t)gpio_num))
            {
                Debug_printv("Cassette motor enabled. Send boot loader!");
                bus->motorActive = true;
            }
            else
            {
                Debug_printv("Cassette motor off");
                bus->motorActive = false;
                bus->getCassette()->stop();
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
    uint16_t c=0;
    
    drive_num = fnUartBUS.read();

    lsn = fnUartBUS.read() << 16;
    lsn |= fnUartBUS.read() << 8;
    lsn |= fnUartBUS.read();

    Debug_printv("OP_READEX: DRIVE %3u - SECTOR %8lu",drive_num,lsn);

    d = &theFuji.get_disks(drive_num)->disk_dev;

    if (!d)
    {
        Debug_printv("Invalid drive #%3u",drive_num);
        return;
    }

    d->read(lsn,sector_data);

    fnUartBUS.write(sector_data,MEDIA_BLOCK_SIZE);

    fnUartBUS.read();
    fnUartBUS.read();

    fnUartBUS.write(0x00); // todo: proper err handling and cksum
    fnUartBUS.write(0x00); // todo: proper err handling and cksum
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
        else
        {
            _cassetteDev->stop();
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

    // Start in DLOAD mode
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