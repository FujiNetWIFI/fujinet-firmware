#ifndef ESP_PLATFORM

#include <cstring>
#include "compat_string.h"

#include "../../include/debug.h"
#include "../utils/utils.h"
#include "fnDummyWiFi.h"
#include "fnSystem.h"
#include "../config/fnConfig.h"

#include "httpService.h"
#include "led.h"

// Global object to manage WiFi
DummyWiFiManager fnWiFi;

DummyWiFiManager::~DummyWiFiManager()
{
    stop();
}

// Remove resources and shut down WiFi driver
void DummyWiFiManager::stop()
{
    // Stop services
    if (_connected)
        handle_station_stop();
    _started = false;
}

// Set up requried resources and start WiFi driver
int DummyWiFiManager::start()
{

    Debug_println("DummyWiFiManager::start");
    _started = true;
    _connected = true;
    fnLedManager.set(eLed::LED_WIFI, true);
    // fnSystem.Net.start_sntp_client();
    fnHTTPD.start();

    return 0;
}

int DummyWiFiManager::test_connect(const char *ssid, const char *password)
{
    return 0;
}

// Attempts to connect using information in Config (if any)
int DummyWiFiManager::connect()
{
    if (Config.have_wifi_info())
        return connect(Config.get_wifi_ssid().c_str(), Config.get_wifi_passphrase().c_str());
    else
        return -1;
}

int DummyWiFiManager::connect(const char *ssid, const char *password)
{
    Debug_printf("DummyWiFi connect to SSID \"%s\"\n", ssid == nullptr ? "" : ssid);
    _connected = true;
    return 0; // ESP_OK
}

bool DummyWiFiManager::connected()
{
    return _connected;
}

/* Initiates a WiFi network scan and returns number of networks found
*/
uint8_t DummyWiFiManager::scan_networks(uint8_t maxresults)
{
    return 16;
}

int DummyWiFiManager::get_scan_result(uint8_t index, char ssid[32], uint8_t *rssi, uint8_t *channel, char bssid[18], uint8_t *encryption)
{
    /*
    if (index > 1)
        return -1;
    */

    if (ssid != nullptr) {
        memset(ssid, 0, 32);
        snprintf(ssid, 32, "Dummy Cafe %d", index);
    }

    if (bssid != nullptr)
        strlcpy(bssid, "D0:1C:ED:C0:FF:EE", 18);

    if (rssi != nullptr)
        *rssi = 255;
    if (channel != nullptr)
        *channel = 8;
    if (encryption != nullptr)
        *encryption = WIFI_AUTH_OPEN;

    return 0;
}

std::string DummyWiFiManager::get_current_ssid()
{
    return std::string("Dummy Cafe");
}

int DummyWiFiManager::get_mac(uint8_t mac[6])
{
    memcpy(mac, "\xD0\x1C\xED\xC0\xFF\xEE", 6);
    return 0; // ESP_OK
}

std::string DummyWiFiManager::get_mac_str()
{
    return std::string("D0:1C:ED:C0:FF:EE");
}

const char *_wifi_country_string()
{
    static char buff[100];

    snprintf(buff, sizeof(buff), "ccode=0x%02hx%02hx%02hx, firstchan=%hu, numchan=%hu, maxpwr=%hd, policy=%s",
             uint8_t(0), uint8_t(0), uint8_t(0), uint8_t(1), uint8_t(11), uint8_t(1),
             "auto");

    return buff;
}

const char *DummyWiFiManager::get_current_detail_str()
{
    static char buff[256];
    buff[0] = '\0';

    snprintf(buff, sizeof(buff),
                "chan=%hu, chan2=%s, rssi=%hd, auth=%s, paircipher=%s, groupcipher=%s, ant=%u "
                "11b=%c, 11g=%c, 11n=%c, lowr=%c, wps=%c, (%s)",
                uint8_t(8), "20-none",
                int8_t(255),
                "WIFI_AUTH_OPEN",
                "WIFI_CIPHER_TYPE_NONE", "WIFI_CIPHER_TYPE_NONE",
                0,
                'y', 'y', 'y',
                'y',
                'n',
                _wifi_country_string());

    return buff;
}

int DummyWiFiManager::get_current_bssid(uint8_t bssid[6])
{
    memcpy(bssid, "\xD0\x1C\xED\xC0\xFF\xEE", 6);
    return 0; // ESP_OK
}

std::string DummyWiFiManager::get_current_bssid_str()
{
    return std::string("D0:1C:ED:C0:FF:EE");
}

void DummyWiFiManager::set_hostname(const char *hostname)
{
    Debug_printf("DummyWiFiManager::set_hostname(%s) - skipped\n",hostname);
}

void DummyWiFiManager::handle_station_stop()
{
    _connected = false;
    fnLedManager.set(eLed::LED_WIFI, false);
    fnHTTPD.stop();
    // fnSystem.Net.stop_sntp_client();
}

void DummyWiFiManager::store_wifi(std::string ssid, std::string password)
{
    Debug_printf("DummyWiFiManager::store_wifi(%s, ****) - skipped\n",ssid.c_str());
}

// void DummyWiFiManager::_wifi_event_handler(void *arg, esp_event_base_t event_base,
//                                       int32_t event_id, void *event_data)
// {
//     // Get a pointer to our fnWiFi object
//     DummyWiFiManager *pFnWiFi = (DummyWiFiManager *)arg;

//     // IP_EVENT NOTIFICATIONS
//     if (event_base == IP_EVENT)
//     {
//         switch (event_id)
//         {
//         // Consider WiFi connected once we get an IP address
//         case IP_EVENT_STA_GOT_IP:
//             Debug_println("IP_EVENT_STA_GOT_IP");
//             Debug_printf("Obtained IP address: %s\n", fnSystem.Net.get_ip4_address_str().c_str());
//             pFnWiFi->_connected = true;
//             fnLedManager.set(eLed::LED_WIFI, true);
//             fnSystem.Net.start_sntp_client();
//             fnHTTPD.start();
//             break;
//         case IP_EVENT_STA_LOST_IP:
//             Debug_println("IP_EVENT_STA_LOST_IP");
//             break;
//         case IP_EVENT_ETH_GOT_IP:
//             Debug_println("IP_EVENT_ETH_GOT_IP");
//             break;
//         }
//     }
//     // WIFI_EVENT NOTIFICATIONS
//     else if (event_base == WIFI_EVENT)
//     {
//         switch (event_id)
//         {
//         case WIFI_EVENT_WIFI_READY:
//             Debug_println("WIFI_EVENT_WIFI_READY");
//             break;
//         case WIFI_EVENT_SCAN_DONE:
//             pFnWiFi->_scan_in_progress = false;
//             Debug_println("WIFI_EVENT_SCAN_DONE");
//             break;
//         case WIFI_EVENT_STA_START:
//             Debug_println("WIFI_EVENT_STA_START");
//             break;
//         case WIFI_EVENT_STA_STOP:
//             Debug_println("WIFI_EVENT_STA_STOP");
//             break;
//         case WIFI_EVENT_STA_CONNECTED:
//             Debug_println("WIFI_EVENT_STA_CONNECTED");
//             pFnWiFi->_reconnect_attempts = 0;
//             break;
//         // Set WiFi to disconnected
//         case WIFI_EVENT_STA_DISCONNECTED:
//             if (pFnWiFi->_connected == true)
//             {
//                 Debug_println("WIFI_EVENT_STA_DISCONNECTED");
//                 pFnWiFi->handle_station_stop();
//             }
//             // Try to reconnect
//             if (pFnWiFi->_scan_in_progress == false &&
//                 pFnWiFi->_reconnect_attempts < FNWIFI_RECONNECT_RETRIES && Config.have_wifi_info())
//             {
//                 pFnWiFi->_reconnect_attempts++;
//                 Debug_printf("WiFi reconnect attempt %u of %d\n", pFnWiFi->_reconnect_attempts, FNWIFI_RECONNECT_RETRIES);
//                 esp_wifi_connect();
//             }
//             break;
//         case WIFI_EVENT_STA_AUTHMODE_CHANGE:
//             Debug_println("WIFI_EVENT_STA_AUTHMODE_CHANGE");
//             break;
//         }
//     }
// }

#endif // !ESP_PLATFORM