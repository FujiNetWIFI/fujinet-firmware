#ifndef FNWIFI_H
#define FNWIFI_H

#include <string>

#include "freertos/event_groups.h"
#include "esp_event.h"

#define FNWIFI_RECONNECT_RETRIES 5
#define FNWIFI_SCAN_RESULTS_MAX 20
#define FNWIFI_BIT_CONNECTED BIT0

class WiFiManager
{
private:
    bool _started = false;
    bool _connected = false;
    std::string _ssid;
    std::string _password;

    wifi_ap_record_t * _scan_records = nullptr;
    uint16_t _scan_record_count = 0;

    char *_mac_to_string(char dest[18], uint8_t mac[6]);

    static void _wifi_event_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data);
    EventGroupHandle_t _wifi_event_group;

public:
    int retries;

    int start();
    void stop();

    ~WiFiManager();

    int connect(const char *ssid, const char *password);

    bool connected();
    std::string get_current_ssid();
    int get_current_bssid(uint8_t bssid[6]);
    std::string get_current_bssid_str();
    int get_mac(uint8_t mac[6]);
    std::string get_mac_str();
    uint8_t scan_networks(uint8_t maxresults = FNWIFI_SCAN_RESULTS_MAX);
    int get_scan_result(uint8_t index, char ssid[32], uint8_t *rssi = NULL,
                        uint8_t *channel = NULL, char bssid[18] = NULL, uint8_t *encryption = NULL);
};

extern WiFiManager fnWiFi;

#endif // FNWIFI_H
