#include "fnDNS.h"

#include "../../include/debug.h"

static const char *normalize_hostname(const char *hostname) {
    std::string normalized(hostname);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char c){ return std::tolower(c); });
    return normalized.c_str();
}

// Return a single IP4 address given a hostname
in_addr_t get_ip4_addr_by_name(const char *hostname)
{
    in_addr_t result = IPADDR_NONE;

    const char *normalized = normalize_hostname(hostname);
    Debug_printf("Resolving hostname \"%s\"\r\n", normalized);
    struct hostent *info = gethostbyname(normalized);

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
