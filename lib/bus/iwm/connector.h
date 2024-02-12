#ifdef BUILD_APPLE

#pragma once
#include <cstdbool>
#include <memory>
#include "Connection.h"

class connector
{
public:
	virtual ~connector() = default;
	virtual void close_connection(bool report_error) = 0;
	virtual std::shared_ptr<Connection> create_connection() = 0;
};

#endif /* BUILD_APPLE */