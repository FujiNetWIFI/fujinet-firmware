#include <esp_system.h>
#include <nvs_flash.h>
#include <esp32/spiram.h>
#include <esp32/himem.h>

#include "debug.h"
#include "bus.h"
#include "device.h"
#include "keys.h"
#include "led.h"
#include "crypt.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fnFsSD.h"
#include "fnFsSPIFFS.h"

#include "httpService.h"

#ifdef BLUETOOTH_SUPPORT
#include "fnBluetooth.h"
#endif

// fnSystem is declared and defined in fnSystem.h/cpp
// fnBtManager is declared and defined in fnBluetooth.h/cpp
// fnLedManager is declared and defined in led.h/cpp
// fnKeyManager is declared and defined in keys.h/cpp
// fnHTTPD is declared and defineid in HttpService.h/cpp

// sioFuji theFuji; // moved to fuji.h/.cpp

void main_shutdown_handler()
{
    Debug_println("Shutdown handler called");
    // Give devices an opportunity to clean up before rebooting

    SYSTEM_BUS.shutdown();
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
#endif // ATARI
#endif // DEBUG

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

    // setup crypto key - must be done before loading the config
    crypto.setkey("FNK" + fnWiFi.get_mac_str());

    // Load our stored configuration
    Config.load();

    // WiFi/BT auto connect moved to app_main()

#ifdef BUILD_ATARI
    theFuji.setup(&SIO);
    SIO.addDevice(&theFuji, SIO_DEVICEID_FUJINET); // the FUJINET!

    SIO.addDevice(&apeTime, SIO_DEVICEID_APETIME); // APETime

    if (Config.get_apetime_enabled() == false)
        apeTime.device_active = false;
    else
        apeTime.device_active = true;

    SIO.addDevice(&udpDev, SIO_DEVICEID_MIDI); // UDP/MIDI device

    // Create a new printer object, setting its output depending on whether we have SD or not
    FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fnSPIFFS;
    sioPrinter::printer_type ptype = Config.get_printer_type(0);
    if (ptype == sioPrinter::printer_type::PRINTER_INVALID)
        ptype = sioPrinter::printer_type::PRINTER_FILE_TRIM;

    Debug_printf("Creating a default printer using %s storage and type %d\n", ptrfs->typestring(), ptype);

    sioPrinter *ptr = new sioPrinter(ptrfs, ptype);
    fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));

    SIO.addDevice(ptr, SIO_DEVICEID_PRINTER + fnPrinters.get_port(0)); // P:

    sioR = new modem(ptrfs, Config.get_modem_sniffer_enabled()); // Config/User selected sniffer enable
    sioR->set_uart(&fnUartSIO);

    SIO.addDevice(sioR, SIO_DEVICEID_RS232); // R:

    SIO.addDevice(&sioV, SIO_DEVICEID_FN_VOICE); // P3:

    SIO.addDevice(&sioZ, SIO_DEVICEID_CPM); // (ATR8000 CPM)

    // Go setup SIO
    SIO.setup();
#endif // BUILD_ATARI

#ifdef BUILD_CBM
    // Setup IEC Bus
    theFuji.setup(&IEC);
#endif // BUILD_CBM

#ifdef BUILD_LYNX
    theFuji.setup(&ComLynx);
    ComLynx.setup();
#endif

#ifdef BUILD_RS232
    theFuji.setup(&RS232);
    RS232.setup();
    RS232.addDevice(&theFuji,0x70);
#endif

#ifdef BUILD_ADAM
    theFuji.setup(&AdamNet);
    AdamNet.setup();
    fnSDFAT.create_path("/FujiNet");

    Debug_printf("Adding virtual printer\n");
    FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fnSPIFFS;
    adamPrinter::printer_type printer = Config.get_printer_type(0);
    adamPrinter *ptr = new adamPrinter(ptrfs, printer);
    fnPrinters.set_entry(0, ptr, printer, 0);
    AdamNet.addDevice(ptr, ADAMNET_DEVICE_ID_PRINTER);

    if (Config.get_printer_enabled())
        AdamNet.enableDevice(ADAMNET_DEVICE_ID_PRINTER);
    else
        AdamNet.disableDevice(ADAMNET_DEVICE_ID_PRINTER);

#ifdef VIRTUAL_ADAM_DEVICES
    Debug_printf("Physical Device Scanning...\n");
    sioQ = new adamQueryDevice();

#ifndef NO_VIRTUAL_KEYBOARD
    exists = sioQ->adamDeviceExists(ADAMNET_DEVICE_ID_KEYBOARD);
    if (!exists)
    {
        Debug_printf("Adding virtual keyboard\n");
        sioK = new adamKeyboard();
        AdamNet.addDevice(sioK, ADAMNET_DEVICE_ID_KEYBOARD);
    }
    else
        Debug_printf("Physical keyboard found\n");
#endif // NO_VIRTUAL_KEYBOARD

#endif // VIRTUAL_ADAM_DEVICES

#endif // BUILD_ADAM

#ifdef BUILD_APPLE
    iwmModem *sioR;
    FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fnSPIFFS;
    sioR = new iwmModem(ptrfs, Config.get_modem_sniffer_enabled());
    
    iwmPrinter::printer_type ptype = Config.get_printer_type(0);
    iwmPrinter *ptr = new iwmPrinter(ptrfs, ptype);
    fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));
    IWM.addDevice(ptr, iwm_fujinet_type_t::Printer);

    theFuji.setup(&IWM);
    IWM.setup(); // save device unit SP address somewhere and restore it after reboot?

#endif /* BUILD_APPLE */

#ifdef DEBUG
    unsigned long endms = fnSystem.millis();
    Debug_printf("Available heap: %u\nSetup complete @ %lu (%lums)\n", fnSystem.get_free_heap_size(), endms, endms - startms);
#endif // DEBUG
}

#ifdef BUILD_S100

// theFuji.setup(&s100Bus);
// SYSTEM_BUS.setup();

#endif /* BUILD_S100*/

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
#endif // BLUETOOTH_SUPPORT

            SYSTEM_BUS.service();

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
        // cppcheck-suppress "unusedFunction"
        // Call our setup routine
        main_setup();

// Create a new high-priority task to handle the main loop
// This is assigned to CPU1; the WiFi task ends up on CPU0
#define MAIN_STACKSIZE 32768
#define MAIN_PRIORITY 20
#define MAIN_CPUAFFINITY 1
        xTaskCreatePinnedToCore(fn_service_loop, "fnLoop",
                                MAIN_STACKSIZE, nullptr, MAIN_PRIORITY, nullptr, MAIN_CPUAFFINITY);

        // Now that our main service is running, try connecting to WiFi or BlueTooth
        if (Config.get_bt_status())
        {
#ifdef BLUETOOTH_SUPPORT
            // Start SIO2BT mode if we were in it last shutdown
            fnLedManager.set(eLed::LED_BT, true); // BT LED ON
            fnBtManager.start();
#endif
        }
        else if (Config.get_wifi_enabled())
        {
            // Set up the WiFi adapter if enabled in config
            fnWiFi.start();
            // Go ahead and try reconnecting to WiFi
            fnWiFi.connect();
        }

        // Sit here twiddling our thumbs
        while (true)
            vTaskDelay(9000 / portTICK_PERIOD_MS);
    }
}
