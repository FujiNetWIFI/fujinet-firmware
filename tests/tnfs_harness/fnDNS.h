#ifndef TNFS_HARNESS_FNDNS_H
#define TNFS_HARNESS_FNDNS_H

// Test stand-in for lib/tcpip/fnDNS.h.

#include "compat_inet.h"

inline in_addr_t get_ip4_addr_by_name(const char *) { return IPADDR_NONE; }

#endif
