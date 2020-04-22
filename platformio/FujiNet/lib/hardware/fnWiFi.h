#ifndef FNWIFI_H
#define FNWIFI_H
#include <string>


#include "esp_event_loop.h"


#define FNWIFI_RECONNECT_RETRIES 5
#define FNWIFI_SCAN_RESULTS_MAX 20
#define FNWIFI_BIT_CONNECTED BIT0

class WiFiManager
{
private:
    bool _started;
    bool _connected;
    std::string _ssid;
    std::string _password;


public:
    EventGroupHandle_t _wifi_event_group;
    int retries;

    bool connected();
    int setup();
    int start(const char* ssid, const char *password);
    int connect(const char *ssid, const char *password);
    void stop();
    std::string get_ssid();
    int get_bssid(uint8_t bssid[6]);
    std::string get_bssid_str();
    int get_mac(uint8_t mac[6]);
    std::string get_mac_str();
    uint8_t scan_networks(uint8_t maxresults = FNWIFI_SCAN_RESULTS_MAX);
    int get_scan_result(uint8_t index, char ssid[32], uint8_t *rssi);

    static esp_err_t fnwifi_event_handler(void *ctx, system_event_t *event);
};

extern WiFiManager fnWiFi;

#endif // FNWIFI_H