#ifndef _FN_DNS_
#define _FN_DNS_
#include <lwip/netdb.h>

in_addr_t get_ip4_addr_by_name(const char *hostname);

#endif // _FN_DNS_