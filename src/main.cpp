#include "debug.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fnFsSD.h"
#include "fnFsSPIF.h"
#include "fnConfig.h"
#include "keys.h"
#include "led.h"
#include "bus.h"

#ifdef BUILD_ATARI
#include "sio/fuji.h"
#include "sio/modem.h"
#include "sio/apetime.h"
#include "sio/voice.h"
#include "sio/printerlist.h"
#include "sio/midimaze.h"
#include "sio/siocpm.h"
#include "samlib.h"
#endif /* BUILD_ATARI */

#ifdef BUILD_ADAM
#include "adamnet/keyboard.h"
#include "adamnet/fuji.h"
#include "adamnet/printer.h"
#include "adamnet/modem.h"
#include "adamnet/printerlist.h"
#include "adamnet/query_device.h"
#endif

#include "httpService.h"

#include <esp_system.h>
#include <nvs_flash.h>

#include <esp32/spiram.h>
#include <esp32/himem.h>

#ifdef BLUETOOTH_SUPPORT
#include "fnBluetooth.h"
#endif

// fnSystem is declared and defined in fnSystem.h/cpp
// fnBtManager is declared and defined in fnBluetooth.h/cpp
// fnLedManager is declared and defined in led.h/cpp
// fnKeyManager is declared and defined in keys.h/cpp
// fnHTTPD is declared and defineid in HttpService.h/cpp

// sioFuji theFuji; // moved to fuji.h/.cpp

#ifdef BUILD_ATARI
sioApeTime apeTime;
sioVoice sioV;
sioMIDIMaze sioMIDI;
// sioCassette sioC; // now part of sioFuji theFuji object
sioModem *sioR;
sioCPM sioZ;
#endif /* BUILD_ATARI */

#ifdef BUILD_ADAM

#define VIRTUAL_ADAM_DEVICES
//#define NO_VIRTUAL_KEYBOARD

adamModem *sioR;
adamKeyboard *sioK;
adamQueryDevice *sioQ;
bool exists = false;
#endif /* BUILD_ADAM */

void main_shutdown_handler()
{
    Debug_println("Shutdown handler called");
    // Give devices an opportunity to clean up before rebooting
#if defined( BUILD_ATARI )
    // SIO.shutdown();
#elif defined( BUILD_CBM )
    // IEC.shutdown();
#endif
}

// Initial setup
void main_setup()
{
#ifdef DEBUG
    fnUartDebug.begin(DEBUG_SPEED);
    unsigned long startms = fnSystem.millis();
    Debug_printf("\n\n--~--~--~--\nFujiNet %s Started @ %lu\n", fnSystem.get_fujinet_version(), startms);
    Debug_printf("Starting heap: %u\n", fnSystem.get_free_heap_size());
#ifdef ATARI
    Debug_printf("PsramSize %u\n", fnSystem.get_psram_size());
    Debug_printf("himem phys %u\n", esp_himem_get_phys_size());
    Debug_printf("himem free %u\n", esp_himem_get_free_size());
    Debug_printf("himem reserved %u\n", esp_himem_reserved_area_size());
#endif /* ATARI */
#endif
    // Install a reboot handler
    esp_register_shutdown_handler(main_shutdown_handler);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        Debug_println("Erasing flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    // Enable GPIO Interrupt Service Routine
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    fnSystem.check_hardware_ver();
    Debug_printf("Detected Hardware Version: %s\n", fnSystem.get_hardware_ver_str());

    fnKeyManager.setup();
    fnLedManager.setup();

    fnSPIFFS.start();
    fnSDFAT.start();

    // Load our stored configuration
    Config.load();

    if ( Config.get_bt_status() )
    {
#ifdef BLUETOOTH_SUPPORT
        // Start SIO2BT mode if we were in it last shutdown
        fnLedManager.set(eLed::LED_BT, true); // BT LED ON
        fnBtManager.start();
#endif
    }
    else
    {
        // Set up the WiFi adapter
        fnWiFi.start();
        // Go ahead and try reconnecting to WiFi
        fnWiFi.connect();
    }

#if defined( BUILD_ATARI )
    theFuji.setup(&SIO);
    SIO.addDevice(&theFuji, SIO_DEVICEID_FUJINET); // the FUJINET!

    SIO.addDevice(&apeTime, SIO_DEVICEID_APETIME); // APETime

    SIO.addDevice(&sioMIDI, SIO_DEVICEID_MIDI); // MIDIMaze

    // Create a new printer object, setting its output depending on whether we have SD or not
    FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fnSPIFFS;
    sioPrinter::printer_type ptype = Config.get_printer_type(0);
    if (ptype == sioPrinter::printer_type::PRINTER_INVALID)
        ptype = sioPrinter::printer_type::PRINTER_FILE_TRIM;

    Debug_printf("Creating a default printer using %s storage and type %d\n", ptrfs->typestring(), ptype);

    sioPrinter *ptr = new sioPrinter(ptrfs, ptype);
    fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));

    SIO.addDevice(ptr, SIO_DEVICEID_PRINTER + fnPrinters.get_port(0)); // P:

    sioR = new sioModem(ptrfs, Config.get_modem_sniffer_enabled()); // Config/User selected sniffer enable
    
    SIO.addDevice(sioR, SIO_DEVICEID_RS232); // R:

    SIO.addDevice(&sioV, SIO_DEVICEID_FN_VOICE); // P3:

    SIO.addDevice(&sioZ, SIO_DEVICEID_CPM); // (ATR8000 CPM)

    // Go setup SIO
    SIO.setup();

#elif defined( BUILD_ADAM )

    theFuji.setup(&AdamNet);
    AdamNet.setup();

#ifdef VIRTUAL_ADAM_DEVICES
    Debug_printf("Physical Device Scanning...\n");
    sioQ = new adamQueryDevice();

#ifndef NO_VIRTUAL_KEYBOARD
    exists = sioQ->adamDeviceExists(ADAMNET_DEVICE_ID_KEYBOARD);
    if (! exists)
    {
        Debug_printf("Adding virtual keyboard\n");
        sioK = new adamKeyboard();
        AdamNet.addDevice(sioK,ADAMNET_DEVICE_ID_KEYBOARD);
    } else
        Debug_printf("Physical keyboard found\n");
#endif
    
    exists = sioQ->adamDeviceExists(ADAMNET_DEVICE_ID_PRINTER);
    if (! exists)
    {
        Debug_printf("Adding virtual printer\n");
        FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fnSPIFFS;
        adamPrinter::printer_type printer = adamPrinter::PRINTER_COLECO_ADAM;
        adamPrinter *ptr = new adamPrinter(ptrfs, printer);
        xTaskCreatePinnedToCore(printerTask,"foo",4096,ptr,10,NULL,1);
        fnPrinters.set_entry(0,ptr,printer,0);
        AdamNet.addDevice(ptr,ADAMNET_DEVICE_ID_PRINTER);
    } else
        Debug_printf("Physical printer found\n");

#endif


#elif defined( BUILD_CBM )

    // Setup IEC Bus
    theFuji.setup(&IEC);

#endif

#ifdef DEBUG
    unsigned long endms = fnSystem.millis();
    Debug_printf("Available heap: %u\nSetup complete @ %lu (%lums)\n", fnSystem.get_free_heap_size(), endms, endms - startms);
#endif
}


// Main high-priority service loop
void fn_service_loop(void *param)
{
    while (true)
    {
        // We don't have any delays in this loop, so IDLE threads will be starved
        // Shouldn't be a problem, but something to keep in mind...

        // Go service BT if it's active
    #ifdef BLUETOOTH_SUPPORT
        if (fnBtManager.isActive())
            fnBtManager.service();
        else
    #endif

    #if defined( BUILD_ATARI )
        SIO.service();
    #elif defined ( BUILD_ADAM )
        AdamNet.service();
    #elif defined( BUILD_CBM )
        IEC.service();
    #endif
        taskYIELD(); // Allow other tasks to run
    }
}

/*
* This is the start/entry point for an ESP-IDF program (must use "C" linkage)
*/
extern "C"
{    
    void app_main()
    {
        // Call our setup routine
        main_setup();

        // Create a new high-priority task to handle the main loop
        // This is assigned to CPU1; the WiFi task ends up on CPU0
        #define MAIN_STACKSIZE 16384
        #define MAIN_PRIORITY 10
        #define MAIN_CPUAFFINITY 1
        xTaskCreatePinnedToCore(fn_service_loop, "fnLoop",
            MAIN_STACKSIZE, nullptr, MAIN_PRIORITY, nullptr, MAIN_CPUAFFINITY);
        
        // Sit here twiddling our thumbs
        while (true)
            vTaskDelay(9000 / portTICK_PERIOD_MS);
    }
}
