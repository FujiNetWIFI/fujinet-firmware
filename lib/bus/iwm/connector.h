#ifdef BUILD_APPLE
#ifdef DEV_RELAY_SLIP

#pragma once
#include <memory>
#include "Connection.h"

class connector
{
public:
	virtual ~connector() = default;
	virtual std::shared_ptr<Connection> create_connection() = 0;
};

#endif /* DEV_RELAY_SLIP */
#endif /* BUILD_APPLE */