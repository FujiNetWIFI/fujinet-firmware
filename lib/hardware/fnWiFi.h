#ifndef FNWIFI_H
#define FNWIFI_H

#ifndef ESP_PLATFORM
// dummy wifi module
#include "fnDummyWiFi.h"
#endif // !ESP_PLATFORM

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <string>
#include <vector>

#define FNWIFI_RECONNECT_RETRIES 4
#define FNWIFI_SCAN_RESULTS_MAX 20

#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1
#define WIFI_NO_IP_YET_BIT    BIT2

// using namespace std;

class WiFiManager
{
private:
     struct stored_wifi
    {
        char ssid[MAX_SSID_LEN+1];
        int index;
        // bool enabled;
    };

    bool _started = false;
    bool _connected = false;
    std::string _ssid;
    std::string _password;

    esp_netif_t *_wifi_sta = nullptr;

    wifi_ap_record_t * _scan_records = nullptr;
    uint16_t _scan_record_count = 0;
    bool _scan_in_progress = false;
    bool _disconnecting = false;

    uint16_t _reconnect_attempts = 0;

    char *_mac_to_string(char dest[18], uint8_t mac[6]);

    static void conn_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static esp_err_t block();

    static void _wifi_event_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data);
    EventGroupHandle_t _wifi_event_group;
    int remove_duplicate_scan_results(wifi_ap_record_t scan_records[], uint16_t record_count);

    bool _trying_stored = false;
    uint16_t _stored_index = 0;
    bool _all_stored_failed = false;
    uint16_t _common_index = 0;
    std::vector<stored_wifi> _matched_wifis;

public:
    std::vector<std::string> get_network_names();
    std::vector<stored_wifi> get_stored_wifis();
    std::vector<stored_wifi> match_stored_with_network_wifis(std::vector<std::string> network_names, std::vector<stored_wifi> stored_wifis);
    void store_wifi(std::string ssid, std::string password);

    int retries;

    int start();
    void stop();

    ~WiFiManager();

    int test_connect(const char *ssid, const char *password);

    int connect(const char *ssid, const char *password);
    int connect();

    bool connected();
    esp_netif_t * get_adapter_handle() { return _wifi_sta; };

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
    std::string get_network_name_by_crc8(uint8_t crc8);

    int32_t localIP();
};

extern WiFiManager fnWiFi;

#endif // ESP_PLATFORM
#endif // FNWIFI_H
