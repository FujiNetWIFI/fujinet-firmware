/* PlatformIO uses ESP-IDF v3.2 by default. Much of what's below will need changing
   with either v3.3 or v4.x
*/
#include <Arduino.h>
#include <WiFi.h>

#include <cstring>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "../../include/debug.h"
#include "../utils/utils.h"
#include "fnWiFi.h"

// Global object to manage WiFi
WiFiManager fnWiFi;

int WiFiManager::setup()
{
    /* JUST RELYING ON THE ARDUINO WIFI LIBRARY FOR NOW, AS THIS SEEMS TO CONFLICT
       AND DOCUMENTATION IS INCONSISTENT UNTIL THE ESP-IDF 4.X BRANCH
    
    // Initilize an event group
    if(_wifi_event_group == NULL)
        _wifi_event_group = xEventGroupCreate();

    // Make sure TCPIP is initialized
    Debug_println("WFM Setup");
    tcpip_adapter_init();

    Debug_println("WFM Event loop");
    //ESP_ERROR_CHECK(esp_event_loop_init(fnwifi_event_handler, this));
    esp_err_t e = esp_event_loop_init(fnwifi_event_handler, this);
    Debug_printf("e = %d\n", e);

    // Configure basic WiFi settings
    Debug_println("WFM Settings");
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    e = esp_wifi_init(&wifi_init_cfg);
    Debug_printf("e: %d\n", e);

    // Set WiFi mode to Station
    Debug_println("WFM Mode");
    //ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    e = esp_wifi_set_mode(WIFI_MODE_STA);
    Debug_printf("e: %d\n", e);

    */

    return 0;
}

int WiFiManager::connect(const char *ssid, const char *password)
{
    if(connected() == true)
    {
        WiFi.disconnect();
        delay(100);

        WiFi.mode(WIFI_STA);
        WiFi.enableSTA(true);
    }
    
    WiFi.begin(ssid, password);
    _started = true;
    return 0;
}

int WiFiManager::start(const char *ssid, const char *password)
{
    /* JUST RELYING ON THE ARDUINO WIFI LIBRARY FOR NOW, AS THIS SEEMS TO CONFLICT
       AND DOCUMENTATION IS INCONSISTENT UNTIL THE ESP-IDF 4.X BRANCH

    // Set logical WiFi options
    Debug_println("WFM Config");
    wifi_config_t wifi_config;
    // ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    esp_err_t e = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    Debug_printf("e: %d\n", e);

    // Start WiFi
    Debug_println("WFM Start");
    // ESP_ERROR_CHECK(esp_wifi_start());
    e = esp_wifi_start();
    Debug_printf("e: %d\n", e);
    */

    WiFi.begin(ssid, password);
    _started = true;
    return 0;
}

void WiFiManager::stop()
{
    //ESP_ERROR_CHECK(esp_wifi_stop());
    WiFi.disconnect();
    _started = false;
    _connected = false;
}

/*
This should be handled by an ESP-IDF connect/disconnect event handler,
but this is compatible with the Arduino WiFi setup we currently have
*/
bool WiFiManager::connected()
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info(&apinfo);

    return (e == ESP_OK);
}

/* Initiates a WiFi network scan and returns number of networks found
*/
uint8_t WiFiManager::scan_networks(uint8_t maxresults)
{
    int retries = 0;
    int result = 0;
    do
    {
        result = WiFi.scanNetworks();
#ifdef DEBUG
        Debug_printf("scanNetworks returned %d\n", result);
#endif
        // We're getting WIFI_SCAN_FAILED (-2) after attempting and failing to connect to a network
        // End any retry attempt if we got a non-negative value
        if (result >= 0)
            break;

    } while (++retries <= FNWIFI_RECONNECT_RETRIES);

    // Boundary check
    if (result < 0)
        result = 0;
    if (result > maxresults)
        result = maxresults;

    return result;
}

int WiFiManager::get_scan_result(uint8_t index, char ssid[32], uint8_t *rssi, uint8_t *channel, char bssid[18], uint8_t *encryption)
{
    String s;
    if(ssid != NULL)
    {
        s = WiFi.SSID(index);
        strncpy(ssid, s.c_str(), 32);
    }
    if(bssid != NULL)
    {
        s = WiFi.BSSIDstr(index);
        strncpy(bssid, s.c_str(), 18);
    }
    if(rssi != NULL)
        *rssi = WiFi.RSSI(index);
    if(channel != NULL)
        *channel = WiFi.channel(index);
    if(encryption != NULL)
        *encryption = WiFi.encryptionType(index);
    
    return 0;
}

std::string WiFiManager::get_current_ssid()
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info( &apinfo );

    std::string result;
    if(ESP_OK == e) 
    {
        result += (char *)apinfo.ssid;
    }

    return result;
}

int WiFiManager::get_mac(uint8_t mac[6])
{
    esp_err_t e = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    return e;
}

char * WiFiManager::mac_to_string(char dest[18], uint8_t mac[6])
{
    if(dest != NULL)
        sprintf(dest, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return dest;
}

std::string WiFiManager::get_mac_str()
{
    std::string result;
    uint8_t mac[6];
    char macStr[18] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    result += mac_to_string(macStr, mac);
    return result;
}

int WiFiManager::get_current_bssid(uint8_t bssid[6])
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info( &apinfo );

    if(ESP_OK == e)
        memcpy(bssid, apinfo.bssid, 6);

    return e;
}    

std::string WiFiManager::get_current_bssid_str()
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info( &apinfo );

    std::string result;
    if(ESP_OK == e) 
    {
        char mac[18] = { 0 };
        result += mac_to_string(mac, apinfo.bssid);
    }

    return result;
}

esp_err_t WiFiManager::fnwifi_event_handler(void *ctx, system_event_t *event)
{
#ifdef DEBUG
        Debug_printf("fnwifi_event_handler event #%d\n", event->event_id);
#endif

    // Get a pointer to our fnWiFi object
    WiFiManager * pFnWiFi = (WiFiManager *)ctx;
    esp_err_t e;
    __IGNORE_UNUSED_VAR(e);
    switch(event->event_id) 
    {
    case SYSTEM_EVENT_STA_START:
#ifdef DEBUG
        Debug_println("fnwifi_event_handler SYSTEM_EVENT_STA_START");
#endif
        e = esp_wifi_connect();
#ifdef DEBUG
        Debug_printf("he = %d\n", e);
#endif
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
#ifdef DEBUG
        Debug_printf("fnwifi_event_handler SYSTEM_EVENT_STA_GOT_IP: %s\n", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
#endif
        //pFnWiFi->retries = 0;
        //xEventGroupSetBits(pFnWiFi->_wifi_event_group, FNWIFI_BIT_CONNECTED);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
#ifdef DEBUG
        Debug_println("fnwifi_event_handler SYSTEM_EVENT_STA_DISCONNECTED");
#endif
        {
            if (pFnWiFi->retries < FNWIFI_RECONNECT_RETRIES)
            {
                /*
                xEventGroupClearBits(pFnWiFi->_wifi_event_group, FNWIFI_BIT_CONNECTED);
                esp_wifi_connect();
                pFnWiFi->retries++;
#ifdef DEBUG
                Debug_printf("Attempting WiFi reconnect %d/%d", pFnWiFi->retries, FNWIFI_RECONNECT_RETRIES); 
#endif
*/
            }
#ifdef DEBUG
            Debug_println("Max WiFi reconnects exhausted");
#endif
            break;
        }
        break;
    default:
#ifdef DEBUG
        Debug_printf("fnwifi_event_handler UNHANDLED TYPE #%d\n", event->event_id);
#endif
        break;
    }
    return ESP_OK;
}
