
#include "fnWiFi.h"

#include <esp_wifi.h>
#include <esp_event.h>
#include <mdns.h>

#include <cstring>

#include "../../include/debug.h"

#include "fuji.h"
#include "fnSystem.h"
#include "fnConfig.h"
#include "httpService.h"
#include "led.h"


// Global object to manage WiFi
WiFiManager fnWiFi;

WiFiManager::~WiFiManager()
{
    stop();
}

// Remove resources and shut down WiFi driver
void WiFiManager::stop()
{
    // Stop services
    if (_connected)
        handle_station_stop();

    // Un-register event handler
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, _wifi_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, _wifi_event_handler));

    // Remove event group
    // This is causing a reboot/crash randomly, so we're not going to delete it
    /*
    if (_wifi_event_group != nullptr)
        vEventGroupDelete(_wifi_event_group);
    */

    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_stop());
    //ESP_ERROR_CHECK(esp_wifi_deinit());

    if (_scan_records != nullptr)
        free(_scan_records);
    _scan_records = nullptr;
    _scan_record_count = 0;

    _started = false;
    _connected = false;
}

// Set up requried resources and start WiFi driver
int WiFiManager::start()
{
    // Initilize an event group
    if (_wifi_event_group == nullptr)
        _wifi_event_group = xEventGroupCreate();

    // Make sure our network interface is initialized
    ESP_ERROR_CHECK(esp_netif_init());

    // Assume we've already done these steps if _wifi_if has a value
    if (_wifi_if == nullptr)
    {
        // Create the default event loop, which is where the WiFi driver sends events
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        // Create a default WIFI station interface
        _wifi_if = esp_netif_create_default_wifi_sta();

        // Configure basic WiFi settings
        wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
        Debug_printf("WiFiManager::start() complete\n");
    }

    // TODO: Provide way to change WiFi region/country?
    // Default is to automatically set the value based on the AP the device is talking to

    // Register for events we care about
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, _wifi_event_handler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, _wifi_event_handler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, _wifi_event_handler, this));

    // Set WiFi mode to Station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_start());

    // Disable powersave for lower latency
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Set a hostname from our configuration
    esp_netif_set_hostname(_wifi_if, Config.get_general_devicename().c_str());

    _started = true;
    return 0;
}

// Attempts to connect using information in Config (if any)
int WiFiManager::connect()
{
    if (Config.have_wifi_info())
        return connect(Config.get_wifi_ssid().c_str(), Config.get_wifi_passphrase().c_str());
    else
        return -1;
}

int WiFiManager::connect(const char *ssid, const char *password)
{
    Debug_printf("WiFi connect attempt to SSID \"%s\"\n", ssid == nullptr ? "" : ssid);

    // Only set an SSID and password if given
    if (ssid != nullptr)
    {
        // Disconnect if we're connected to a different ssid
        if (_connected == true)
        {
            std::string current_ssid = get_current_ssid();
            if (current_ssid.compare(ssid) != 0)
            {
                Debug_printf("Disconnecting from current SSID \"%s\"\n", current_ssid.c_str());
                esp_wifi_disconnect();
                fnSystem.delay(500);
            }
        }

        // Set the new values
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config));

        strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

        // Debug_printf("WiFi config double-check: \"%s\", \"%s\"\n", (char *)wifi_config.sta.ssid, (char *)wifi_config.sta.password );

        wifi_config.sta.pmf_cfg.capable = true;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }

    // Now connect
    _reconnect_attempts = 0;
    esp_err_t e = esp_wifi_connect();
    Debug_printf("esp_wifi_connect returned %d\n", e);
    return e;
}

bool WiFiManager::connected()
{
    return _connected;
}

/* Initiates a WiFi network scan and returns number of networks found
*/
uint8_t WiFiManager::scan_networks(uint8_t maxresults)
{
    // Free any existing scan records
    if (_scan_records != nullptr)
        free(_scan_records);
    _scan_records = nullptr;
    _scan_record_count = 0;

    wifi_scan_config_t scan_conf;
    scan_conf.bssid = 0;
    scan_conf.ssid = 0;
    scan_conf.channel = 0;
    scan_conf.show_hidden = false;
    scan_conf.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_conf.scan_time.active.min = 100; // ms; 100 is what Arduino-ESP uses
    scan_conf.scan_time.active.max = 300; // ms; 300 is what Arduino-ESP uses

    bool temporary_disconnect = false;
    uint16_t result = 0;
    uint8_t final_count = 0;

    // If we're currently connected, disconnect to allow the scan to happen
    if (_connected == true)
    {
        temporary_disconnect = true;
        esp_wifi_disconnect();
    }

    _scan_in_progress = true;
    esp_err_t e = esp_wifi_scan_start(&scan_conf, true);
    if (e == ESP_OK)
    {
        e = esp_wifi_scan_get_ap_num(&result);
        if (e != ESP_OK)
        {
            Debug_printf("esp_wifi_scan_get_ap_num returned error %d\n", e);
        }
    }
    else
    {
        Debug_printf("esp_wifi_scan_start returned error %d\n", e);
    }

    if (e == ESP_OK)
    {
        Debug_printf("esp_wifi_scan returned %d results\n", result);

        // Boundary check
        if (result > maxresults)
            result = maxresults;

        // Allocate memory to store the results
        if (result > 0)
        {
            uint16_t numloaded = result;
            _scan_records = (wifi_ap_record_t *)malloc(result * sizeof(wifi_ap_record_t));

            e = esp_wifi_scan_get_ap_records(&numloaded, _scan_records);
            if (e != ESP_OK)
            {
                Debug_printf("esp_wifi_scan_get_ap_records returned error %d\n", e);
                if (_scan_records != nullptr)
                    free(_scan_records);
                _scan_records = nullptr;
                _scan_record_count = 0;

                final_count = 0;
            }
            else
            {
                _scan_record_count = remove_duplicate_scan_results(_scan_records, numloaded);
                final_count = _scan_record_count;
            }
        }
    }

    // Reconnect to WiFi if we disconnected ourselves
    if (temporary_disconnect == true)
        esp_wifi_connect();

    return final_count;
}

/* Remove duplicate entries in the scan results
*/
int WiFiManager::remove_duplicate_scan_results(wifi_ap_record_t scan_records[], uint16_t record_count)
{
    if (record_count <= 1)
        return record_count;

    int current_index = 0;
    while (current_index < record_count - 1)
    {
        char *current_ssid = (char *) &scan_records[current_index].ssid;
        int compare_index = current_index + 1;
        // Compare current SSID to others in array
        while (compare_index < record_count)
        {
            if (strcmp(current_ssid, (char *) &scan_records[compare_index].ssid) == 0)
            {
                // Keep the entry with better signal strength
                if(scan_records[compare_index].rssi > scan_records[current_index].rssi)
                    memcpy(&scan_records[current_index], &scan_records[compare_index], sizeof(wifi_ap_record_t));

                int move_index = compare_index + 1;
                // Move up all following records one position
                while (move_index < record_count)
                {
                    memcpy(&scan_records[move_index - 1], &scan_records[move_index], sizeof(wifi_ap_record_t));
                    move_index++;
                }
                memset(&scan_records[move_index - 1], 0, sizeof(wifi_ap_record_t));
                // We now have one record less
                record_count--;
            }
            else
                compare_index++;
        }
        current_index++;
    }
    return record_count;
}

int WiFiManager::get_scan_result(uint8_t index, char ssid[32], uint8_t *rssi, uint8_t *channel, char bssid[18], uint8_t *encryption)
{
    if (index > _scan_record_count)
        return -1;

    wifi_ap_record_t *ap = &_scan_records[index];

    if (ssid != nullptr)
        strlcpy(ssid, (const char *)ap->ssid, 32);

    if (bssid != nullptr)
        _mac_to_string(bssid, ap->bssid);

    if (rssi != nullptr)
        *rssi = ap->rssi;
    if (channel != nullptr)
        *channel = ap->primary;
    if (encryption != nullptr)
        *encryption = ap->authmode;

    return 0;
}

std::string WiFiManager::get_current_ssid()
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info(&apinfo);

    if (ESP_OK == e)
        return std::string((char *)apinfo.ssid);

    return std::string();
}

int WiFiManager::get_mac(uint8_t mac[6])
{
    esp_err_t e = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    return e;
}

char *WiFiManager::_mac_to_string(char dest[18], uint8_t mac[6])
{
    if (dest != NULL)
        sprintf(dest, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return dest;
}

std::string WiFiManager::get_mac_str()
{
    std::string result;
    uint8_t mac[6];
    char macStr[18] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    result += _mac_to_string(macStr, mac);
    return result;
}

const char *_wifi_country_string(wifi_country_t *cinfo)
{
    static char buff[100];

    snprintf(buff, sizeof(buff), "ccode=0x%02hx%02hx%02hx, firstchan=%hu, numchan=%hu, maxpwr=%hd, policy=%s",
             cinfo->cc[0], cinfo->cc[1], cinfo->cc[2], cinfo->schan, cinfo->nchan, cinfo->max_tx_power,
             cinfo->policy == WIFI_COUNTRY_POLICY_MANUAL ? "manual" : "auto");

    return buff;
}

const char *_wifi_cipher_string(wifi_cipher_type_t cipher)
{
    const char *cipherstrings[WIFI_CIPHER_TYPE_UNKNOWN] =
        {
            "WIFI_CIPHER_TYPE_NONE",
            "WIFI_CIPHER_TYPE_WEP40",
            "WIFI_CIPHER_TYPE_WEP104",
            "WIFI_CIPHER_TYPE_TKIP",
            "WIFI_CIPHER_TYPE_CCMP",
            "WIFI_CIPHER_TYPE_TKIP_CCMP",
            "WIFI_CIPHER_TYPE_AES_CMAC128"};

    if (cipher < WIFI_CIPHER_TYPE_UNKNOWN)
        return cipherstrings[cipher];
    else
        return "WIFI_CIPHER_TYPE_UNKNOWN";
}

const char *_wifi_auth_string(wifi_auth_mode_t mode)
{
    const char *modestrings[WIFI_AUTH_MAX] =
        {
            "WIFI_AUTH_OPEN",
            "WIFI_AUTH_WEP",
            "WIFI_AUTH_WPA_PSK",
            "WIFI_AUTH_WPA2_PSK",
            "WIFI_AUTH_WPA_WPA2_PSK",
            "WIFI_AUTH_WPA2_ENTERPRISE",
            "WIFI_AUTH_WPA3_PSK",
            "WIFI_AUTH_WPA2_WPA3_PSK"};

    if (mode < WIFI_AUTH_MAX)
        return modestrings[mode];
    else
        return "UNKNOWN MODE";
}

const char *WiFiManager::get_current_detail_str()
{
    static char buff[256];
    buff[0] = '\0';

    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info(&apinfo);

    if (ESP_OK == e)
    {
        const char *second = "20-none";
        if (apinfo.second == WIFI_SECOND_CHAN_ABOVE)
            second = "40-above";
        else if (apinfo.second == WIFI_SECOND_CHAN_BELOW)
            second = "40-below";

        snprintf(buff, sizeof(buff),
                 "chan=%hu, chan2=%s, rssi=%hd, auth=%s, paircipher=%s, groupcipher=%s, ant=%u "
                 "11b=%c, 11g=%c, 11n=%c, lowr=%c, wps=%c, (%s)",
                 apinfo.primary, second,
                 apinfo.rssi,
                 _wifi_auth_string(apinfo.authmode),
                 _wifi_cipher_string(apinfo.pairwise_cipher), _wifi_cipher_string(apinfo.group_cipher),
                 apinfo.ant,
                 apinfo.phy_11b ? 'y' : 'n', apinfo.phy_11g ? 'y' : 'n', apinfo.phy_11n ? 'y' : 'n',
                 apinfo.phy_lr ? 'y' : 'n',
                 apinfo.wps ? 'y' : 'n',
                 _wifi_country_string(&apinfo.country));
    }

    return buff;
}

int WiFiManager::get_current_bssid(uint8_t bssid[6])
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info(&apinfo);

    if (ESP_OK == e)
        memcpy(bssid, apinfo.bssid, 6);

    return e;
}

std::string WiFiManager::get_current_bssid_str()
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info(&apinfo);

    if (ESP_OK == e)
    {
        char mac[18] = {0};
        return std::string(_mac_to_string(mac, apinfo.bssid));
    }

    return std::string();
}

void WiFiManager::set_hostname(const char *hostname)
{
    Debug_printf("WiFiManager::set_hostname(%s)\n", hostname);
    esp_netif_set_hostname(_wifi_if, hostname);
}

void WiFiManager::handle_station_stop()
{
    _connected = false;
    fnLedManager.set(eLed::LED_WIFI, false);
    fnHTTPD.stop();
    fnSystem.Net.stop_sntp_client();
}

void WiFiManager::_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data)
{
    // Get a pointer to our fnWiFi object
    WiFiManager *pFnWiFi = (WiFiManager *)arg;

    // IP_EVENT NOTIFICATIONS
    if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        // Consider WiFi connected once we get an IP address
        case IP_EVENT_STA_GOT_IP:
            Debug_println("IP_EVENT_STA_GOT_IP");
            Debug_printf("Obtained IP address: %s\n", fnSystem.Net.get_ip4_address_str().c_str());
            pFnWiFi->_connected = true;
            fnLedManager.set(eLed::LED_WIFI, true);
            fnSystem.Net.start_sntp_client();
            fnHTTPD.start();
// #ifdef BUILD_APPLE
//             IWM.startup_hack();
// #endif
#ifdef BUILD_ATARI // temporary
            if (Config.get_general_config_enabled() == false)
                theFuji.mount_all();
#endif /* BUILD_ATARI */
            mdns_init();
            mdns_hostname_set(Config.get_general_devicename().c_str());
            mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
            break;
        case IP_EVENT_STA_LOST_IP:
            Debug_println("IP_EVENT_STA_LOST_IP");
            break;
        case IP_EVENT_ETH_GOT_IP:
            Debug_println("IP_EVENT_ETH_GOT_IP");
            break;
        }
    }
    // WIFI_EVENT NOTIFICATIONS
    else if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_WIFI_READY:
            Debug_println("WIFI_EVENT_WIFI_READY");
            break;
        case WIFI_EVENT_SCAN_DONE:
            pFnWiFi->_scan_in_progress = false;
            Debug_println("WIFI_EVENT_SCAN_DONE");
            break;
        case WIFI_EVENT_STA_START:
            Debug_println("WIFI_EVENT_STA_START");
            break;
        case WIFI_EVENT_STA_STOP:
            mdns_free();
            Debug_println("WIFI_EVENT_STA_STOP");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            Debug_println("WIFI_EVENT_STA_CONNECTED");
            pFnWiFi->_reconnect_attempts = 0;
            break;
        // Set WiFi to disconnected
        case WIFI_EVENT_STA_DISCONNECTED:
            if (pFnWiFi->_connected == true)
            {
                Debug_println("WIFI_EVENT_STA_DISCONNECTED");
                pFnWiFi->handle_station_stop();
            }
            // Try to reconnect
            if (pFnWiFi->_scan_in_progress == false &&
                pFnWiFi->_reconnect_attempts < FNWIFI_RECONNECT_RETRIES && Config.have_wifi_info())
            {
                pFnWiFi->_reconnect_attempts++;
                Debug_printf("WiFi reconnect attempt %u of %d\n", pFnWiFi->_reconnect_attempts, FNWIFI_RECONNECT_RETRIES);
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_AUTHMODE_CHANGE:
            Debug_println("WIFI_EVENT_STA_AUTHMODE_CHANGE");
            break;
        }
    }
}

int32_t WiFiManager::localIP()
{
    std::string result;
    esp_netif_ip_info_t ip_info;
    esp_err_t e = esp_netif_get_ip_info(get_adapter_handle(), &ip_info);
    return ip_info.ip.addr;
}