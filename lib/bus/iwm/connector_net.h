#pragma once

#ifdef BUILD_APPLE
#ifdef DEV_RELAY_SLIP

#include <memory>
#include "connector.h"
#include "compat_inet.h"
#include "Connection.h"

class connector_net : public connector
{
public:
	virtual std::shared_ptr<Connection> create_connection() override;

private:
	int host_port = 0;
	in_addr_t host_ip = IPADDR_NONE;
};

#endif /* DEV_RELAY_SLIP */
#endif /* BUILD_APPLE */