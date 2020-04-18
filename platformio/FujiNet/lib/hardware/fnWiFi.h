#ifndef FNWIFI_H
#define FNWIFI_H
#include <string>

class WiFiManager
{

public:
    bool connected();
    int start();
    void stop();
    std::string get_ssid();
    int get_bssid(uint8_t bssid[6]);
    std::string get_bssid_str();
    int get_mac(uint8_t mac[6]);
    std::string get_mac_str();
};

extern WiFiManager fnWiFi;

#endif // FNWIFI_H