#pragma once

#if defined(DEV_RELAY_SLIP) && defined(SLIP_PROTOCOL_COM)

#include "Connection.h"
#include <libserialport.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <map>

class COMConnection : public Connection, public std::enable_shared_from_this<COMConnection>
{
public:
	explicit COMConnection(const std::string &port_name, struct sp_port *port, bool is_connected);
	virtual ~COMConnection();

	void send_data(const std::vector<uint8_t> &data) override;
	void create_read_channel() override;
	void close_connection() override;

	sp_port *get_port() const { return port_; }
	void set_port(sp_port *port)
	{
		if (port_ != nullptr)
		{
			sp_close(port_);
			sp_free_port(port_);
		}
		port_ = port;
	}

private:
	std::string port_name_;
	struct sp_port *port_;
};

#endif
