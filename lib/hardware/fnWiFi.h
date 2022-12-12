#ifndef FNWIFI_H
#define FNWIFI_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_netif.h>

#include <string>


#define FNWIFI_RECONNECT_RETRIES 8
#define FNWIFI_SCAN_RESULTS_MAX 20
#define FNWIFI_BIT_CONNECTED BIT0

class WiFiManager
{
private:
    bool _started = false;
    bool _connected = false;
    std::string _ssid;
    std::string _password;

    esp_netif_t *_wifi_if = nullptr;

    wifi_ap_record_t * _scan_records = nullptr;
    uint16_t _scan_record_count = 0;
    bool _scan_in_progress = false;

    uint16_t _reconnect_attempts = 0;

    char *_mac_to_string(char dest[18], uint8_t mac[6]);

    static void _wifi_event_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data);
    EventGroupHandle_t _wifi_event_group;
    int remove_duplicate_scan_results(wifi_ap_record_t scan_records[], uint16_t record_count);

public:
    int retries;

    int start();
    void stop();

    ~WiFiManager();

    int connect(const char *ssid, const char *password);
    int connect();

    bool connected();
    esp_netif_t * get_adapter_handle() { return _wifi_if; };

    void handle_station_stop();

    void set_hostname(const char* hostname);

    std::string get_current_ssid();
    const char * get_current_detail_str();
    int get_current_bssid(uint8_t bssid[6]);
    std::string get_current_bssid_str();
    int get_mac(uint8_t mac[6]);
    std::string get_mac_str();
    uint8_t scan_networks(uint8_t maxresults = FNWIFI_SCAN_RESULTS_MAX);
    int get_scan_result(uint8_t index, char ssid[32], uint8_t *rssi = NULL,
                        uint8_t *channel = NULL, char bssid[18] = NULL, uint8_t *encryption = NULL);

    int32_t localIP();
};

extern WiFiManager fnWiFi;

#endif // FNWIFI_H
