
#include "fnWiFi.h"

#include <esp_wifi.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <esp_mac.h>
#endif
#include <esp_event.h>
#include <mdns.h>
#include <esp_crc.h>

#include <cstring>
#include <algorithm>
#include <vector>

#include "../../include/debug.h"

#include "fujiDevice.h"
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

    // Assume we've already done these steps if _wifi_sta has a value
    if (_wifi_sta == nullptr)
    {
        // Create the default event loop, which is where the WiFi driver sends events
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        // Create a default WIFI station interface
        _wifi_sta = esp_netif_create_default_wifi_sta();

        // Configure basic WiFi settings
        wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        Debug_printf("WiFiManager::start() complete\r\n");
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
    esp_netif_set_hostname(_wifi_sta, Config.get_general_devicename().c_str());

    _started = true;

    // // Go ahead and try connecting to WiFi
    // connect();

    return 0;
}

// Attempts to connect using information in Config (if any)
int WiFiManager::connect()
{
    if (Config.have_wifi_info()) {
        _all_stored_failed = false;
        _stored_index = 0;
        _trying_stored = false;

        // Check if main config works
        Debug_println("WiFiManager attempting to connect:");
        Debug_printf("ssid = %s\r\n", Config.get_wifi_ssid().c_str());
        // Debug_printf("pass = %s\r\n", Config.get_wifi_passphrase().c_str());

        return connect(Config.get_wifi_ssid().c_str(), Config.get_wifi_passphrase().c_str());
    }
    // else
    // {
    //     if ( strlen( WIFI_SSID ) )
    //     {
    //         Debug_printv("Connection failed.  Trying default WiFi Settings. [%s][%s]", WIFI_SSID, WIFI_PASSWORD);
    //         return connect( WIFI_SSID, WIFI_PASSWORD );
    //     }
    // }
    return -1;
}

int WiFiManager::connect(const char *ssid, const char *password)
{
    Debug_printf("WiFi connect attempt to SSID \"%s\"\r\n", ssid == nullptr ? "" : ssid);

    // Only set an SSID and password if given
    if (ssid != nullptr)
    {
        // Disconnect if we're connected to a different ssid
        if (_connected == true)
        {
            std::string current_ssid = get_current_ssid();
            if (current_ssid.compare(ssid) != 0)
            {
                Debug_printf("Disconnecting from current SSID \"%s\"\r\n", current_ssid.c_str());
                _disconnecting = true;
                esp_wifi_disconnect();
                // Must delay before changing our disconnecting flag, as the network tries to reconnect before completing the new connection
                // and uses the old settings still, and connecting you to the wrong (old) wifi details.
                fnSystem.delay(500);
                _disconnecting = false;
            }
        }

        // Set the new values
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config));

        strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

        // Debug_printf("WiFi config double-check: \"%s\", \"%s\"\r\n", (char *)wifi_config.sta.ssid, (char *)wifi_config.sta.password );

        wifi_config.sta.pmf_cfg.capable = true;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }

    // Now connect
    _reconnect_attempts = 0;
    esp_err_t e = esp_wifi_connect();
    Debug_printf("esp_wifi_connect returned %d\r\n", e);
    return e;
}

static EventGroupHandle_t wifi_event_group;

void WiFiManager::conn_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                Debug_println("WIFI_EVENT_STA_START received, connecting");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                Debug_println("WIFI_EVENT_STA_DISCONNECTED: settting WIFI_FAIL_BIT");
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                break;
            case WIFI_EVENT_STA_CONNECTED:
                Debug_printf("WIFI_EVENT_STA_CONNECTED received, ssid: %s, channel: %d\r\n", ((wifi_event_sta_connected_t*)event_data)->ssid, ((wifi_event_sta_connected_t*)event_data)->channel);
                xEventGroupSetBits(wifi_event_group, WIFI_NO_IP_YET_BIT);
                break;
            default:
                Debug_printf("Ignoring event_id: %lu\r\n", event_id);
                break;
        }
    }

    if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                // not sure this can happen in our scenario
                Debug_println("IP_EVENT_STA_GOT_IP received, setting WIFI_CONNECTED_BIT");
                xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
                break;
            default:
                Debug_printf("Ignoring event_id: %lu\r\n", event_id);
                break;
        }
    }

}

int WiFiManager::test_connect(const char *ssid, const char *password)
{
    stop();
    if (wifi_event_group == nullptr)
        wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    if (_wifi_sta == nullptr)
    {
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        _wifi_sta = esp_netif_create_default_wifi_ap();

        wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, conn_event_handler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, conn_event_handler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, conn_event_handler, this));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));

    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    // Debug_printf("wifi_config.sta.ssid: >%s<\r\n", wifi_config.sta.ssid);
    // Debug_printf("wifi_config.sta.pass: >%s<\r\n", wifi_config.sta.password);

    wifi_config.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    esp_netif_set_hostname(_wifi_sta, Config.get_general_devicename().c_str());

    esp_err_t result = block();

    // clean up
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, conn_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, conn_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, conn_event_handler));
    vEventGroupDelete(wifi_event_group);
    wifi_event_group = nullptr;

    return result;
}

esp_err_t WiFiManager::block()
{
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                WIFI_NO_IP_YET_BIT | WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                30000 / portTICK_PERIOD_MS);
                // portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    } else if (bits & WIFI_NO_IP_YET_BIT) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
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
    scan_conf.channel_bitmap.ghz_2_channels = 0xFFFF; // all channels
    scan_conf.channel_bitmap.ghz_5_channels = 0xFFFFFFFF; // all channels

    bool temporary_disconnect = false;
    uint16_t result = 0;
    uint8_t final_count = 0;

    // If we're currently connected, disconnect to allow the scan to happen
    if (_connected == true)
    {
        temporary_disconnect = true;
        _disconnecting = true;
        esp_wifi_disconnect();
        _disconnecting = false;
    }

    _scan_in_progress = true;
    esp_err_t e = esp_wifi_scan_start(&scan_conf, true);
    if (e == ESP_OK)
    {
        e = esp_wifi_scan_get_ap_num(&result);
        if (e != ESP_OK)
        {
            Debug_printf("esp_wifi_scan_get_ap_num returned error %d\r\n", e);
        }
    }
    else
    {
        Debug_printf("esp_wifi_scan_start returned error %d\r\n", e);
    }

    if (e == ESP_OK)
    {
        Debug_printf("esp_wifi_scan returned %d results\r\n", result);

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
                Debug_printf("esp_wifi_scan_get_ap_records returned error %d\r\n", e);
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

    while(_connected == false);

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
    Debug_printf("WiFiManager::set_hostname(%s)\r\n", hostname);
    esp_netif_set_hostname(_wifi_sta, hostname);
}

void WiFiManager::handle_station_stop()
{
    _connected = false;
    fnLedManager.set(eLed::LED_WIFI, false);
    fnHTTPD.stop();
    fnSystem.Net.stop_sntp_client();
}

void add_mdns_services()
{
    mdns_txt_item_t wdi[3] = {{"path","/dav"}, {"u","fujinet"}, {"p",""}};
    mdns_txt_item_t hti[3] = {{"u",""},{"p",""}, {"path","/"}};
    mdns_service_add(NULL,"_webdav","_tcp",80,wdi,3);
    mdns_service_add(NULL,"_http","_tcp",80,hti,3);
}

void WiFiManager::_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data)
{
    // Get a pointer to our fnWiFi object
    WiFiManager *pFnWiFi = (WiFiManager *)arg;
    int connection_attempts = FNWIFI_RECONNECT_RETRIES;

    // IP_EVENT NOTIFICATIONS
    if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        // Consider WiFi connected once we get an IP address
        case IP_EVENT_STA_GOT_IP:
            Debug_println("IP_EVENT_STA_GOT_IP");
            Debug_printf("Obtained IP address: %s\r\n", fnSystem.Net.get_ip4_address_str().c_str());
            pFnWiFi->_connected = true;
            fnLedManager.set(eLed::LED_WIFI, true);
            fnSystem.Net.start_sntp_client();
            fnHTTPD.start();
// #ifdef BUILD_APPLE
//             IWM.startup_hack();
// #endif
#ifdef BUILD_ATARI // temporary
            if (Config.get_general_config_enabled() == false)
                theFuji->fujicore_mount_all_success();
#endif /* BUILD_ATARI */
            mdns_init();
            mdns_hostname_set(Config.get_general_devicename().c_str());
            add_mdns_services();
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
            if (pFnWiFi->_trying_stored)
            {
                // we were trying stored values, and found a good connection
                int i = pFnWiFi->_matched_wifis.at(pFnWiFi->_stored_index).index;
                Debug_printf("Found stored entry to connect to. Shuffling everything above %d down 1\r\n", i);

                // copy the values that worked
                std::string working_ssid = Config.get_wifi_stored_ssid(i);
                std::string working_passphrase = Config.get_wifi_stored_passphrase(i);

                // shuffle the old stored wifi entries down 1 position until hit the found stored wifi
                for (int j = i - 1; j >= 0; j--)
                {
                    Config.store_wifi_stored_ssid(j + 1, Config.get_wifi_stored_ssid(j));
                    Config.store_wifi_stored_passphrase(j + 1, Config.get_wifi_stored_passphrase(j));
                    Config.store_wifi_stored_enabled(j + 1, true); // Always true as we're copying down the stack of stored configs
                }

                // move the old config into pos 0 so it's tried first next time.
                Config.store_wifi_stored_ssid(0, Config.get_wifi_ssid());
                Config.store_wifi_stored_passphrase(0, Config.get_wifi_passphrase());
                Config.store_wifi_stored_enabled(0, true);

                // store working values into current config
                Config.store_wifi_ssid(working_ssid.c_str(), working_ssid.length());
                Config.store_wifi_passphrase(working_passphrase.c_str(), working_passphrase.length());

                // save our changes
                Config.save();
            }
            break;
        // Set WiFi to disconnected
        case WIFI_EVENT_STA_DISCONNECTED:
            if (pFnWiFi->_connected == true)
            {
                Debug_println("WIFI_EVENT_STA_DISCONNECTED");
                pFnWiFi->handle_station_stop();
            }

            // if we are currently attempting to disconnect, don't attempt to reconnect
            if (pFnWiFi->_disconnecting) return;

            // Try to reconnect
            if (pFnWiFi->_scan_in_progress == false &&
                pFnWiFi->_reconnect_attempts < connection_attempts && Config.have_wifi_info())
            {
                pFnWiFi->_reconnect_attempts++;
                Debug_printf("WiFi reconnect attempt %u of %d\r\n", pFnWiFi->_reconnect_attempts, connection_attempts);
                esp_wifi_connect();
            }
            else if (pFnWiFi->_scan_in_progress == false &&
                pFnWiFi->_reconnect_attempts == connection_attempts && Config.have_wifi_info() && !pFnWiFi->_trying_stored)
            {
                // we have tried the current wifi config but it failed.
                // Start trying stored configs if there are any.
                // If we haven't yet scanned the network for bssids, do so, then match any with our current stored entries
                // as it's pointless trying to connect to anything not seen by network, as it clearly won't connect.
                // TODO: will this stop us connecting to hidden wifis? is that even possible?

                std::vector<std::string> network_names = pFnWiFi->get_network_names();
                std::vector<WiFiManager::stored_wifi> stored_wifis = pFnWiFi->get_stored_wifis();
                std::vector<stored_wifi> common_names = pFnWiFi->match_stored_with_network_wifis(network_names, stored_wifis);

                // copy the common names to our manager to iterate over
                std::copy(common_names.begin(), common_names.end(), std::back_inserter(pFnWiFi->_matched_wifis));

                // no entries in common between stored and seen networks
                if (common_names.empty()) return;

                pFnWiFi->_trying_stored = true;
                pFnWiFi->_reconnect_attempts = 0;

                Debug_printf("Trying wifi stored config 0, SSID: %s\r\n", common_names.at(0).ssid);
                pFnWiFi->connect(common_names.at(0).ssid, Config.get_wifi_stored_passphrase(common_names.at(0).index).c_str());
            }
            else if (pFnWiFi->_scan_in_progress == false &&
                pFnWiFi->_reconnect_attempts == connection_attempts && Config.have_wifi_info() && pFnWiFi->_trying_stored)
            {
                // Try next common if available
                pFnWiFi->_reconnect_attempts = 0;
                pFnWiFi->_stored_index++;
                int i = pFnWiFi->_stored_index;
                if (i < pFnWiFi->_matched_wifis.size())
                {
                    Debug_printf("Trying wifi stored config %d, SSID: %s\r\n", i, pFnWiFi->_matched_wifis.at(i).ssid);
                    pFnWiFi->connect(pFnWiFi->_matched_wifis.at(i).ssid, Config.get_wifi_stored_passphrase(pFnWiFi->_matched_wifis.at(i).index).c_str());
                }
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
    esp_netif_get_ip_info(get_adapter_handle(), &ip_info);
    return ip_info.ip.addr;
}

std::string WiFiManager::get_network_name_by_crc8(uint8_t crc8)
{
    std::vector<std::string> network_names = fnWiFi.get_network_names();
    for (std::string _network_name: network_names)
    {
        uint8_t c_crc8 = esp_crc8_le(0, (uint8_t *)_network_name.c_str(), _network_name.length());
        Debug_printf("[%03d] - %s\r\n", crc8, _network_name.c_str());
        if ( c_crc8 == crc8 )
        {
            return _network_name;
        }
    }
    return std::string();
}

std::vector<std::string> WiFiManager::get_network_names()
{
    struct
    {
        char ssid[MAX_SSID_LEN+1];
        uint8_t rssi;
    } detail;

    std::vector<std::string> network_names;
    if (_scan_record_count == 0) {
        // get the names of the networks in range, as we haven't done it yet
        scan_networks();
        _scan_in_progress = false;
    }
    for (int i = 0; i < _scan_record_count; i++) {
        get_scan_result(i, detail.ssid, &detail.rssi);
        network_names.push_back(detail.ssid);
    }

    return network_names;
}

std::vector<WiFiManager::stored_wifi> WiFiManager::get_stored_wifis()
{
    std::vector<WiFiManager::stored_wifi> stored_wifis;
    int i;
    for ( i = 0; i < MAX_WIFI_STORED; i++)
    {
        if(Config.get_wifi_stored_enabled(i))
        {
            WiFiManager::stored_wifi d;
            strcpy(d.ssid, Config.get_wifi_stored_ssid(i).c_str());
            d.index = i;
            stored_wifis.push_back(d);
        }
    }
    return stored_wifis;
}

std::vector<WiFiManager::stored_wifi> WiFiManager::match_stored_with_network_wifis(std::vector<std::string> network_names, std::vector<WiFiManager::stored_wifi> stored_wifis)
{
    Debug_printf("Found following networks:\r\n");
    for (std::string _network_name: network_names)
    {
        uint8_t id = esp_crc8_le(0, (uint8_t *)_network_name.c_str(), _network_name.length());
        Debug_printf("[%03d] - %s\r\n", id, _network_name.c_str());
    }

    Debug_printf("Found following stored networks:\r\n");
    for (stored_wifi d: stored_wifis)
    {
        uint8_t id = esp_crc8_le(0, (uint8_t *)d.ssid, strlen(d.ssid));
        Debug_printf("[%03d] - %s, index: %d\r\n", id, d.ssid, d.index);
    }

    std::vector<stored_wifi> common_names;
    // We are not using set_intersect() as it requires the lists to be sorted but we want to preserve the stored names order

    for (stored_wifi d: stored_wifis)
    {
        if (std::find(network_names.begin(), network_names.end(), d.ssid) != network_names.end())
        {
            common_names.push_back(d);
        }
    }

    Debug_printf("Common names:\r\n");
    for (stored_wifi d: common_names)
    {
        uint8_t id = esp_crc8_le(0, (uint8_t *)d.ssid, strlen(d.ssid));
        Debug_printf("[%03d] - %s, index: %d\r\n", id, d.ssid, d.index);
    }

    return common_names;
}

void WiFiManager::store_wifi(std::string ssid, std::string password)
{
    // 1. if this is a new SSID and not in the old stored, we should push the current one to the top of the stored configs, and everything else down.
    // 2. If this was already in the stored configs, push the stored one to the top, remove the new one from stored so it becomes current only.
    // 3. if this is same as current, then just save it again. User reconnected to current, nothing to change in stored. This is default if above don't happen

    int ssid_in_stored = -1;
    for (int i = 0; i < MAX_WIFI_STORED; i++)
    {
        if (Config.get_wifi_stored_ssid(i) == ssid)
        {
            ssid_in_stored = i;
            break;
        }
    }

    // case 1
    if (ssid_in_stored == -1 && Config.have_wifi_info() && Config.get_wifi_ssid() != ssid) {
        Debug_println("Case 1: Didn't find new ssid in stored, and it's new. Pushing everything down 1 and old current to 0");
        // Move enabled stored down one, last one will drop off
        for (int j = MAX_WIFI_STORED - 1; j > 0; j--)
        {
            bool enabled = Config.get_wifi_stored_enabled(j - 1);
            if (!enabled) continue;

            Config.store_wifi_stored_ssid(j, Config.get_wifi_stored_ssid(j - 1));
            Config.store_wifi_stored_passphrase(j, Config.get_wifi_stored_passphrase(j - 1));
            Config.store_wifi_stored_enabled(j, true); // already confirmed this is enabled
        }
        // push the current to the top of stored
        Config.store_wifi_stored_ssid(0, Config.get_wifi_ssid());
        Config.store_wifi_stored_passphrase(0, Config.get_wifi_passphrase());
        Config.store_wifi_stored_enabled(0, true);
    }

    // case 2
    if (ssid_in_stored != -1 && Config.have_wifi_info() && Config.get_wifi_ssid() != ssid) {
        Debug_printf("Case 2: Found new ssid in stored at %d, and it's not current (should never happen). Pushing everything down 1 and old current to 0\r\n", ssid_in_stored);
        // found the new SSID at ssid_in_stored, so move everything above it down one slot, and store the current at 0
        for (int j = ssid_in_stored; j > 0; j--)
        {
            Config.store_wifi_stored_ssid(j, Config.get_wifi_stored_ssid(j - 1));
            Config.store_wifi_stored_passphrase(j, Config.get_wifi_stored_passphrase(j - 1));
            Config.store_wifi_stored_enabled(j, true);
        }

        // push the current to the top of stored
        Config.store_wifi_stored_ssid(0, Config.get_wifi_ssid());
        Config.store_wifi_stored_passphrase(0, Config.get_wifi_passphrase());
        Config.store_wifi_stored_enabled(0, true);
    }

    // save the new SSID as current
    Config.store_wifi_ssid(ssid.c_str(), ssid.size());
    // Clear text here, it will be encrypted internally if enabled for encryption
    Config.store_wifi_passphrase(password.c_str(), password.size());

    Config.save();
}
