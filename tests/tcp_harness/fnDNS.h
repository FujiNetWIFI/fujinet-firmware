#ifndef TCP_HARNESS_FNDNS_H
#define TCP_HARNESS_FNDNS_H

// Test stand-in for lib/tcpip/fnDNS.h; the test defines the function.

#include "compat_inet.h"

in_addr_t get_ip4_addr_by_name(const char *hostname);

#endif // TCP_HARNESS_FNDNS_H
