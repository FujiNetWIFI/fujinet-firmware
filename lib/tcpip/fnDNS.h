#ifndef _FN_DNS_
#define _FN_DNS_

#include "compat_inet.h"

#ifndef ESP_PLATFORM

/* borrowed from lwip/ip4_addr.h */
/** 255.255.255.255 */
#define IPADDR_NONE         ((uint32_t)0xffffffffUL)
/** 127.0.0.1 */
#define IPADDR_LOOPBACK     ((uint32_t)0x7f000001UL)
/** 0.0.0.0 */
#define IPADDR_ANY          ((uint32_t)0x00000000UL)
/** 255.255.255.255 */
#define IPADDR_BROADCAST    ((uint32_t)0xffffffffUL)

#endif // !ESP_PLATFORM

in_addr_t get_ip4_addr_by_name(const char *hostname);

#endif // _FN_DNS_
