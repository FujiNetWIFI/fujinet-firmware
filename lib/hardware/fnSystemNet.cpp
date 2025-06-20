
#ifdef ESP_PLATFORM
#include <esp_sntp.h>
#include <esp_netif.h>
// ESP_PLATFORM
#else
// !ESP_PLATFORM
// gethostname
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#endif
// !ESP_PLATFORM
#endif

#include <string>

#include "fnSystem.h"

#include "../../include/debug.h"

#include "fnConfig.h"
#include "fnWiFi.h"

std::string SystemManager::_net::get_hostname()
{
    std::string result;
#ifdef ESP_PLATFORM
    const char *hostname;
    esp_err_t e = esp_netif_get_hostname(fnWiFi.get_adapter_handle(), &hostname);
    if(e == ESP_OK)
        result += hostname;
#else
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        result += "unknown";
    else
        result += hostname;
#endif
    return result;
}

int SystemManager::_net::get_ip4_info(uint8_t ip4address[4], uint8_t ip4mask[4], uint8_t ip4gateway[4])
{
#ifdef ESP_PLATFORM
    esp_netif_ip_info_t ip_info;

    esp_err_t e = esp_netif_get_ip_info(fnWiFi.get_adapter_handle(), &ip_info);

    if(e == ESP_OK) 
    {
        ip4address[0] = esp_ip4_addr1(&ip_info.ip);
        ip4address[1] = esp_ip4_addr2(&ip_info.ip);
        ip4address[2] = esp_ip4_addr3(&ip_info.ip);
        ip4address[3] = esp_ip4_addr4(&ip_info.ip);

        ip4mask[0] = esp_ip4_addr1(&ip_info.netmask);
        ip4mask[1] = esp_ip4_addr2(&ip_info.netmask);
        ip4mask[2] = esp_ip4_addr3(&ip_info.netmask);
        ip4mask[3] = esp_ip4_addr4(&ip_info.netmask);

        ip4gateway[0] = esp_ip4_addr1(&ip_info.gw);
        ip4gateway[1] = esp_ip4_addr2(&ip_info.gw);
        ip4gateway[2] = esp_ip4_addr3(&ip_info.gw);
        ip4gateway[3] = esp_ip4_addr4(&ip_info.gw);
    }
#else
    int e = 0;
    // TODO obtain real IP from host OS?
    ip4address[0] = 127;
    ip4address[1] = 0;
    ip4address[2] = 0;
    ip4address[3] = 1;

    ip4mask[0] = 255;
    ip4mask[1] = 0;
    ip4mask[2] = 0;
    ip4mask[3] = 0;

    ip4gateway[0] = 0;
    ip4gateway[1] = 0;
    ip4gateway[2] = 0;
    ip4gateway[3] = 0;
#endif
    return e;
}

int SystemManager::_net::get_ip4_dns_info(uint8_t ip4dnsprimary[4])
{
#ifdef ESP_PLATFORM
    esp_netif_dns_info_t dnsinfo;
    esp_err_t e = esp_netif_get_dns_info(fnWiFi.get_adapter_handle(), ESP_NETIF_DNS_MAIN, &dnsinfo);
    if(e == ESP_OK)
    {
        ip4dnsprimary[0] = esp_ip4_addr1(&dnsinfo.ip.u_addr.ip4);
        ip4dnsprimary[1] = esp_ip4_addr2(&dnsinfo.ip.u_addr.ip4);
        ip4dnsprimary[2] = esp_ip4_addr3(&dnsinfo.ip.u_addr.ip4);
        ip4dnsprimary[3] = esp_ip4_addr4(&dnsinfo.ip.u_addr.ip4);
    }
#else
    int e = 0;
    // TODO obtain real DNS from host OS?
    ip4dnsprimary[0] = 1;
    ip4dnsprimary[1] = 1;
    ip4dnsprimary[2] = 1;
    ip4dnsprimary[3] = 1;
#endif
    return e;
}

std::string SystemManager::_net::_get_ip4_address_str(_ip4_address_type iptype)
{
    std::string result;
#ifdef ESP_PLATFORM
    esp_netif_ip_info_t ip_info;
    esp_err_t e = esp_netif_get_ip_info(fnWiFi.get_adapter_handle(), &ip_info);

    ip4_addr_t ip4;
    if(e == ESP_OK)
    {
        switch(iptype)
        {
        case IP4_PRIMARY:
            ip4.addr = ip_info.ip.addr;
            result += ip4addr_ntoa(&ip4);
            break;
        case IP4_PRIMARYMASK:
            ip4.addr = ip_info.netmask.addr;
            result += ip4addr_ntoa(&ip4);
            break;
        case IP4_GATEWAY:
            ip4.addr = ip_info.gw.addr;
            result += ip4addr_ntoa(&ip4);
            break;
        }
    }
#else
    switch(iptype)
    {
    case IP4_PRIMARY:
        result += "127.0.0.1";
        break;
    case IP4_PRIMARYMASK:
        result += "255.0.0.0";
        break;
    case IP4_GATEWAY:
        result += "0.0.0.0";
        break;
    }
#endif
    return result;
}

std::string SystemManager::_net::get_ip4_address_str()
{
    return _get_ip4_address_str(IP4_PRIMARY);
}

std::string SystemManager::_net::get_ip4_mask_str()
{
    return _get_ip4_address_str(IP4_PRIMARYMASK);
}

std::string SystemManager::_net::get_ip4_gateway_str()
{
    return _get_ip4_address_str(IP4_GATEWAY);
}

std::string SystemManager::_net::_get_ip4_dns_str(_ip4_dns_type dnstype)
{
#ifdef ESP_PLATFORM
    std::string result;
    esp_netif_dns_info_t dnsinfo;
    esp_netif_dns_type_t t;

    switch(dnstype)
    {
    case IP4_DNS_PRIMARY:
        t = ESP_NETIF_DNS_MAIN;
        break;
    default:
        t =  ESP_NETIF_DNS_MAX;
        break;
    }

    if (t < ESP_NETIF_DNS_MAX)
    {
        if (ESP_OK == esp_netif_get_dns_info(fnWiFi.get_adapter_handle(), t, &dnsinfo) )
        {
            ip4_addr_t ip4;
            ip4.addr = dnsinfo.ip.u_addr.ip4.addr;
            result += ip4addr_ntoa(&ip4);
        }

    }
#else
    std::string result = "1.1.1.1";
#endif
    return result;
}

std::string SystemManager::_net::get_ip4_dns_str()
{
    return _get_ip4_dns_str(IP4_DNS_PRIMARY);
}

void SystemManager::_net::set_sntp_lastsync()
{
    _sntp_last_sync = fnSystem.millis();
}

// Static function for SNTP event notifications
void SystemManager::_net::_sntp_time_sync_notification(struct timeval *tv)
{
    fnSystem.Net.set_sntp_lastsync();
    Debug_printf("SNTP time sync event: %s\r\n", fnSystem.get_current_time_str());
}


void SystemManager::_net::stop_sntp_client()
{
    // TODO: Determine if we really need to stop this when our network connection is lost
    // sntp_stop();
}

void SystemManager::_net::start_sntp_client()
{
#ifdef ESP_PLATFORM
    // Don't do anything if we've already initialized
    if (_sntp_initialized == true)
        return;

    Debug_print("SNTP client start\r\n");

    // Update system timezone data
    fnSystem.update_timezone(Config.get_general_timezone().c_str());

    sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // Set a server if we have one defined, otherwise try DHCP
    const char * sntpserver = Config.get_network_sntpserver();
    // sntp_setservername does NOT copy the string passed, so it must be in a static buffer
    if (sntpserver != nullptr && sntpserver[0] != '\0')
        sntp_setservername(0, sntpserver); 
    else
    {
        Debug_print("No SNTP server defined - attempting DHCP setting\r\n");
        // This will only do something if SNTP_GET_SERVERS_FROM_DHCP is set in the LWIP library
        sntp_servermode_dhcp(1);
    }

    // Set a notification callback function
    sntp_set_time_sync_notification_cb(_sntp_time_sync_notification);

    sntp_init();
#endif
    _sntp_initialized = true;
}
