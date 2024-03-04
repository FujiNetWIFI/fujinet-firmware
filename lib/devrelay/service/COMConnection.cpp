#if defined(DEV_RELAY_SLIP) && defined(SLIP_PROTOCOL_COM)

#include "COMConnection.h"
#include <iostream>
#include "../slip/SLIP.h"

COMConnection::COMConnection(const std::string &port_name, struct sp_port *port, bool is_connected) : port_name_(port_name), port_(port) { set_is_connected(is_connected); }

COMConnection::~COMConnection() { close_connection(); }

void COMConnection::send_data(const std::vector<uint8_t> &data)
{
	if (!is_connected())
	{
		std::cerr << "COMConnection: Not connected\n";
		return;
	}

	const auto slip_data = SLIP::encode(data);
	sp_nonblocking_write(port_, slip_data.data(), slip_data.size());
}

void COMConnection::create_read_channel()
{
	reading_thread_ = std::thread([self = shared_from_this()]() {
		std::vector<uint8_t> buffer(1024);
		while (self->is_connected())
		{
			int bytes_read = sp_nonblocking_read(self->port_, buffer.data(), buffer.size());
			if (bytes_read > 0)
			{
				std::vector<std::vector<uint8_t>> decoded_packets = SLIP::split_into_packets(buffer.data(), buffer.size());
				if (!decoded_packets.empty())
				{
					for (const auto &packet : decoded_packets)
					{
						if (!packet.empty())
						{
							{
								std::lock_guard<std::mutex> lock(self->data_mutex_);
								self->data_map_[packet[0]] = packet;
							}
							self->data_cv_.notify_all();
						}
					}
				}
			}
		}
	});
}

void COMConnection::close_connection()
{
	set_is_connected(false);
	if (reading_thread_.joinable())
	{
		reading_thread_.join();
	}
	if (port_)
	{
		sp_close(port_);
		sp_free_port(port_);
		port_ = nullptr;
	}
}

#endif
