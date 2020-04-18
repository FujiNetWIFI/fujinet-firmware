#include <cstring>
#include "esp_wifi.h"
//#include "esp_event.h"
#include "esp_log.h"

#include "fnWiFi.h"

// Global object to manage WiFi
WiFiManager fnWiFi;

int WiFiManager::start()
{
    /*
    esp_wifi_init()
    esp_wifi_start()
    */
    return 0;
}

void WiFiManager::stop()
{

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

std::string WiFiManager::get_ssid()
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

std::string WiFiManager::get_mac_str()
{
    std::string result;
    uint8_t mac[6];
    char macStr[18] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    result += macStr;
    return result;
}

int WiFiManager::get_bssid(uint8_t bssid[6])
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info( &apinfo );

    if(ESP_OK == e)
        memcpy(bssid, apinfo.bssid, 6);

    return e;
}    

std::string WiFiManager::get_bssid_str()
{
    wifi_ap_record_t apinfo;
    esp_err_t e = esp_wifi_sta_get_ap_info( &apinfo );

    std::string result;
    if(ESP_OK == e) 
    {
        char mac[18] = { 0 };
        sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", apinfo.bssid[0], 
            apinfo.bssid[1], apinfo.bssid[2], apinfo.bssid[3], apinfo.bssid[4], apinfo.bssid[5]);
        result += mac;
    }

    return result;
}
