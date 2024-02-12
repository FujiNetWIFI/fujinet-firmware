#ifdef BUILD_APPLE

#pragma once

#include <memory>
#include "connector.h"
#include "compat_inet.h"
#include "Connection.h"

class connector_net : public connector
{
public:
	virtual void close_connection(bool report_error) override;
	virtual std::shared_ptr<Connection> create_connection() override;

private:
	int sock = 0;
	int host_port = 0;
	in_addr_t host_ip = IPADDR_NONE;

};

#endif /* BUILD_APPLE */