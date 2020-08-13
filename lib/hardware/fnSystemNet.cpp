/* NOTE: esp_idf 4.x replaces the tcpip_adapter library with esp_netif
*/
#include <string.h>
#include <lwip/netdb.h>
#include "esp_sntp.h"
#include <esp_system.h>
#include <tcpip_adapter.h>


#include "fnSystem.h"
#include "fnConfig.h"

std::string SystemManager::_net::get_hostname()
{
    std::string result;
    const char *hostname;
    esp_err_t e = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &hostname);
    if(e == ESP_OK)
        result += hostname;
    return result;
}

int SystemManager::_net::get_ip4_info(uint8_t ip4address[4], uint8_t ip4mask[4], uint8_t ip4gateway[4])
{
    tcpip_adapter_ip_info_t ip_info;
    esp_err_t e = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
    if(e == ESP_OK) 
    {
        ip4address[0] = ip4_addr1(&ip_info.ip);
        ip4address[1] = ip4_addr2(&ip_info.ip);
        ip4address[2] = ip4_addr3(&ip_info.ip);
        ip4address[3] = ip4_addr4(&ip_info.ip);

        ip4mask[0] = ip4_addr1(&ip_info.netmask);
        ip4mask[1] = ip4_addr2(&ip_info.netmask);
        ip4mask[2] = ip4_addr3(&ip_info.netmask);
        ip4mask[3] = ip4_addr4(&ip_info.netmask);

        ip4gateway[0] = ip4_addr1(&ip_info.gw);
        ip4gateway[1] = ip4_addr2(&ip_info.gw);
        ip4gateway[2] = ip4_addr3(&ip_info.gw);
        ip4gateway[3] = ip4_addr4(&ip_info.gw);
    }
    return e;
}
int SystemManager::_net::get_ip4_dns_info(uint8_t ip4dnsprimary[4])
{
    tcpip_adapter_dns_info_t dnsinfo;
    esp_err_t e = tcpip_adapter_get_dns_info(TCPIP_ADAPTER_IF_STA, TCPIP_ADAPTER_DNS_MAIN, &dnsinfo);
    if(e == ESP_OK)
    {
        ip4dnsprimary[0] = ip4_addr1(&dnsinfo.ip.u_addr.ip4);
        ip4dnsprimary[1] = ip4_addr2(&dnsinfo.ip.u_addr.ip4);
        ip4dnsprimary[2] = ip4_addr3(&dnsinfo.ip.u_addr.ip4);
        ip4dnsprimary[3] = ip4_addr4(&dnsinfo.ip.u_addr.ip4);
    }
    return e;
}

std::string SystemManager::_net::_get_ip4_address_str(_ip4_address_type iptype)
{
    std::string result;
    tcpip_adapter_ip_info_t ip_info;
    esp_err_t e = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
    if(e == ESP_OK)
    {
        switch(iptype)
        {
        case IP4_PRIMARY:
            result += ip4addr_ntoa(&ip_info.ip);
            break;
        case IP4_PRIMARYMASK:
            result += ip4addr_ntoa(&ip_info.netmask);
            break;
        case IP4_GATEWAY:
            result += ip4addr_ntoa(&ip_info.gw);
            break;
        }
    }
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
    std::string result;
    tcpip_adapter_dns_info_t dnsinfo;
    tcpip_adapter_dns_type_t t;
    switch(dnstype)
    {
    case IP4_DNS_PRIMARY:
        t = TCPIP_ADAPTER_DNS_MAIN;
        break;
    default:
        t = TCPIP_ADAPTER_DNS_MAX;
        break;
    }
    if (t < TCPIP_ADAPTER_DNS_MAX )
    {
        if (ESP_OK == tcpip_adapter_get_dns_info(TCPIP_ADAPTER_IF_STA, t, &dnsinfo) )
        {
            result += ip4addr_ntoa(&dnsinfo.ip.u_addr.ip4);
        }

    }

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
    Debug_printf("SNTP time sync event: %s\n", fnSystem.get_current_time_str());
}


void SystemManager::_net::stop_sntp_client()
{
    // TODO: Determine if we really need to stop this when our network connection is lost
    // sntp_stop();
}

void SystemManager::_net::start_sntp_client()
{
    // Don't do anything if we've already initialized
    if (_sntp_initialized == true)
        return;

    Debug_print("SNTP client start\n");

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
        Debug_print("No SNTP server defined - attempting DHCP setting\n");
        // This will only do something if SNTP_GET_SERVERS_FROM_DHCP is set in the LWIP library
        sntp_servermode_dhcp(1);
    }

    // Set a notification callback function
    sntp_set_time_sync_notification_cb(_sntp_time_sync_notification);

    sntp_init();
    _sntp_initialized = true;
}
