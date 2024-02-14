#ifdef BUILD_APPLE
#ifdef SP_OVER_SLIP

#pragma once
#include <cstdbool>
#include <memory>
#include "Connection.h"

class connector
{
public:
	virtual ~connector() = default;
	virtual std::shared_ptr<Connection> create_connection() = 0;
};

#endif /* SP_OVER_SLIP */
#endif /* BUILD_APPLE */