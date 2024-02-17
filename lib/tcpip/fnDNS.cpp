#include "fnDNS.h"

#include "../../include/debug.h"


// Return a single IP4 address given a hostname
in_addr_t get_ip4_addr_by_name(const char *hostname)
{
    in_addr_t result = IPADDR_NONE;

    Debug_printf("Resolving hostname \"%s\"\r\n", hostname);
    struct hostent *info = gethostbyname(hostname);

    if(info == nullptr)
    {
        Debug_println("Name failed to resolve");
    }
    else
    {
        if(info->h_addr_list[0] != nullptr)
        {
            result = *((in_addr_t*)(info->h_addr_list[0]));
            Debug_printf("Resolved to address %s\r\n", compat_inet_ntoa(result));
        }
    }
    return result;
}