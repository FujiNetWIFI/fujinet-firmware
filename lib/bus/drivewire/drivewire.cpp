#ifdef BUILD_COCO

#include "drivewire.h"

#include "../../include/debug.h"

#include "fuji.h"
#include "udpstream.h"
#include "modem.h"
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

static void IRAM_ATTR drivewire_isr_handler(void* arg)
{
    // Generic default interrupt handler
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(drivewire_evt_queue, &gpio_num, NULL);
}

static void drivewire_intr_task(void* arg)
{
    uint32_t gpio_num;

    while ( true ) 
    {
        if(xQueueReceive(drivewire_evt_queue, &gpio_num, portMAX_DELAY)) 
        {
            if ( gpio_num == PIN_CASS_MOTOR &&  gpio_get_level( (gpio_num_t)gpio_num) )
            {
                Debug_printv( "Cassette motor enalbed! Send boot loader!" );
                
                // auto cassette = theFuji.cassette();
                // FILE *fp = fopen("/dw3doscc1.bin", "r");
                // fseek(fp, 0L, SEEK_END);
                // auto sz = ftell(fp);
                // rewind(fp);
                // cassette->mount_cassette_file(fp, sz);
                // cassette->drivewire_enable_cassette();
            }
        }
    }
}

// Helper functions outside the class defintions

systemBus virtualDevice::get_bus() { return DRIVEWIRE; }

// Read and process a command frame from DRIVEWIRE
void systemBus::_drivewire_process_cmd()
{
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
}

// Setup DRIVEWIRE bus
void systemBus::setup()
{
    // Create a queue to handle parallel event from ISR
    drivewire_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Start task
    //xTaskCreate(drivewire_intr_task, "drivewire_intr_task", 2048, NULL, 10, NULL);
    xTaskCreatePinnedToCore(drivewire_intr_task, "drivewire_intr_task", 4096, NULL, 10, NULL, 0);

    // Setup interrupt for cassette motor pin
    gpio_config_t io_conf = {
        .pin_bit_mask = ( 1ULL << PIN_CASS_MOTOR ),    // bit mask of the pins that you want to set
        .mode = GPIO_MODE_INPUT,                      // set as input mode
        .pull_up_en = GPIO_PULLUP_DISABLE,            // disable pull-up mode
        .pull_down_en = GPIO_PULLDOWN_ENABLE,         // enable pull-down mode
        .intr_type = GPIO_INTR_POSEDGE                // interrupt on positive edge
    };

    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_isr_handler_add((gpio_num_t)PIN_CASS_MOTOR, drivewire_isr_handler, (void*) PIN_CASS_MOTOR);
}

// Add device to DRIVEWIRE bus
void systemBus::addDevice(virtualDevice *pDevice, int device_id)
{
}

// Removes device from the DRIVEWIRE bus.
// Note that the destructor is called on the device!
void systemBus::remDevice(virtualDevice *p)
{
    _daisyChain.remove(p);
}

// Should avoid using this as it requires counting through the list
int systemBus::numDevices()
{
    int i = 0;
    __BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    __END_IGNORE_UNUSEDVARS
}

void systemBus::changeDeviceId(virtualDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = device_id;
    }
}

virtualDevice *systemBus::deviceById(int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

// Give devices an opportunity to clean up before a reboot
void systemBus::shutdown()
{
    shuttingDown = true;

    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n",devicep->id());
        devicep->shutdown();
    }
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
#endif /* BUILD_COCO */