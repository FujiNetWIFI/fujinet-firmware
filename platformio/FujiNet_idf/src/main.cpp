/*
MAJOR REV 3 UPDATE - June 12, 2020
After transitioning the Arduino-ESP components from the project to ESP-IDF equivalent
code, the source files have been copied into a new ESP-IDF project. Code massaging and
testing commences...
*/

#include "ssid.h" // Define WIFI_SSID and WIFI_PASS in include/ssid.h. File is ignored by GIT

#include "debug.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fnFsSD.h"
#include "fnFsSPIF.h"
#include "fnConfig.h"
#include "keys.h"
#include "led.h"
#include "sio.h"
#include "disk.h"
#include "fuji.h"
#include "modem.h"
#include "apetime.h"
#include "voice.h"
#include "httpService.h"
#include "printerlist.h"

#ifdef BOARD_HAS_PSRAM
#include <esp32/spiram.h>
#include <esp32/himem.h>
#endif

#ifdef BLUETOOTH_SUPPORT
#include "bluetooth.h"
#endif

// fnSystem is declared and defined in fnSystem.h/cpp
sioModem sioR;
sioFuji theFuji;
sioApeTime apeTime;
sioVoice sioV;
fnHttpService fnHTTPD;

#ifdef DEBUG_N
WiFiClient wifiDebugClient;
#endif

KeyManager keyMgr;
LedManager ledMgr;

#ifdef BLUETOOTH_SUPPORT
BluetoothManager btMgr;
#endif

TaskHandle_t _taskh_main_loop;

/*
* Initial setup
*/
void main_setup()
{
#ifdef DEBUG_S
#ifdef NO_GLOBAL_SERIAL
    fnUartDebug.begin(DEBUG_SPEED);
#else
    BUG_UART.begin(DEBUG_SPEED);
#endif
#endif

#ifdef DEBUG
    unsigned long startms = fnSystem.millis();
    Debug_printf("\n\n--~--~--~--\nFujiNet PlatformIO Started @ %lu\n", startms);
    Debug_printf("Starting heap: %u\n", fnSystem.get_free_heap_size());
#ifdef BOARD_HAS_PSRAM
    Debug_printf("PsramSize %u\n", fnSystem.get_psram_size());
    //Debug_printf("himem phys %u\n", esp_himem_get_phys_size());
    Debug_printf("himem free %u\n", esp_himem_get_free_size());
    Debug_printf("himem reserved %u\n", esp_himem_reserved_area_size());
#endif
#endif

    keyMgr.setup();
    ledMgr.setup();

    fnSPIFFS.start();
    fnSDFAT.start();

    // Load our stored configuration
    Config.load();

    // Set up the WiFi adapter
    fnWiFi.start();

    theFuji.setup(SIO);
    SIO.addDevice(&theFuji, SIO_DEVICEID_FUJINET); // the FUJINET!

    SIO.addDevice(&apeTime, SIO_DEVICEID_APETIME); // APETime

    SIO.addDevice(&sioR, SIO_DEVICEID_RS232); // R:

    // Create a new printer object, setting its output depending on whether we have SD or not
    FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fnSPIFFS;
    sioPrinter::printer_type ptype = Config.get_printer_type(0);
    if (ptype == sioPrinter::printer_type::PRINTER_INVALID)
        ptype = sioPrinter::printer_type::PRINTER_FILE_TRIM;

    Debug_printf("Creating a default printer using %s storage and type %d\n", ptrfs->typestring(), ptype);

    sioPrinter *ptr = new sioPrinter(ptrfs, ptype);
    fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));

    SIO.addDevice(ptr, SIO_DEVICEID_PRINTER + fnPrinters.get_port(0)); // P:

    SIO.addDevice(&sioV, SIO_DEVICEID_FN_VOICE); // P3:

    if (fnWiFi.connected())
    {
        Debug_printf("WiFi connected. Current IP address: %s\n", fnSystem.Net.get_ip4_address_str().c_str());
    }

    Debug_printf("%d devices registered\n", SIO.numDevices());

    // Go setup SIO
    SIO.setup();

    Debug_print("SIO Voltage: ");
    Debug_println(fnSystem.get_sio_voltage());

    void sio_flush();

#ifdef DEBUG
    unsigned long endms = fnSystem.millis();
    Debug_printf("Available heap: %u\nSetup complete @ %lu (%lums)\n", fnSystem.get_free_heap_size(), endms, endms - startms);
#endif
}

/*
* Main activity loop
*/
void main_loop(void *param)
{
#ifdef DEBUG_N
    /* Connect to debug server if we aren't and WiFi is connected */
    if (!wifiDebugClient.connected() && WiFi.status() == WL_CONNECTED)
    {
        wifiDebugClient.connect(DEBUG_HOST, 6502);
        wifiDebugClient.println("FujiNet PlatformIO");
    }
#endif

    // Toggle the state of the WiFi LED based on the current WiFi status
    // Start the web server if it hasn't been started and WiFi is connected
    if (fnWiFi.connected())
    {
        ledMgr.set(eLed::LED_WIFI, true);
        if (!fnHTTPD.running())
            fnHTTPD.start();
    }
    else
    {
        ledMgr.set(eLed::LED_WIFI, false);
        if (fnHTTPD.running())
            fnHTTPD.stop();
    }

    // Check on the status of the OTHER_KEY and do something useful
    switch (keyMgr.getKeyStatus(eKey::OTHER_KEY))
    {
    case eKeyStatus::LONG_PRESSED:
        Debug_println("O_KEY: LONG PRESS");
        break;
    case eKeyStatus::SHORT_PRESSED:
        Debug_println("O_KEY: SHORT PRESS");
        break;
    default:
        break;
    }

    // Check on the status of the BOOT_KEY and do something useful
    switch (keyMgr.getKeyStatus(eKey::BOOT_KEY))
    {
    case eKeyStatus::LONG_PRESSED:
        Debug_println("B_KEY: LONG PRESS");

#ifdef BLUETOOTH_SUPPORT
        if (btMgr.isActive())
        {
            btMgr.stop();
#ifdef BOARD_HAS_PSRAM
            ledMgr.set(eLed::LED_BT, false);
#else
            ledMgr.set(eLed::LED_SIO, false);
#endif
        }
        else
        {
#ifdef BOARD_HAS_PSRAM
            ledMgr.set(eLed::LED_BT, true);
#else
            ledMgr.set(eLed::LED_SIO, true); // SIO LED always ON in Bluetooth mode
#endif
            btMgr.start();
        }
#endif //BLUETOOTH_SUPPORT
        break;
    case eKeyStatus::SHORT_PRESSED:
#ifdef DEBUG
        Debug_println("B_KEY: SHORT PRESS");
#ifdef BOARD_HAS_PSRAM
        ledMgr.blink(eLed::LED_BT); // blink to confirm a button press
#else
        ledMgr.blink(eLed::LED_SIO);         // blink to confirm a button press
#endif
#endif //DEBUG

// Either toggle BT baud rate or do a disk image rotation on B_KEY SHORT PRESS
#ifdef BLUETOOTH_SUPPORT
        if (btMgr.isActive())
        {
            btMgr.toggleBaudrate();
        }
        else
#endif
        {
            theFuji.image_rotate();
        }
        break;
    default:
        break;
    } // switch (keyMgr.getKeyStatus(eKey::BOOT_KEY))

    // Go service BT if it's active
#ifdef BLUETOOTH_SUPPORT
    if (btMgr.isActive())
    {
        btMgr.service();
    }
    else
#endif
    {
        SIO.service();
    }
}

/*
* This is the start/entry point for an ESP-IDF program (must use "C" linkage)
*/
extern "C"
{
    void app_main()
    {
        // Call our setup routing
        main_setup();

        // Start a new task to run the loop that was previously
        // the core of the Arduino-ESP project
    #define MAIN_LOOP_STACK_SIZE 4096
    #define MAIN_LOOP_PRIORITY 5    
        xTaskCreate(main_loop, "main_loop", MAIN_LOOP_STACK_SIZE, nullptr, MAIN_LOOP_PRIORITY, &_taskh_main_loop);

    }
}
