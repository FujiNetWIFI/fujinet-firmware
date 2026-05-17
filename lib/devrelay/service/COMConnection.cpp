#if defined(DEV_RELAY_SLIP) && defined(SLIP_PROTOCOL_COM)

#include <cstring>
#include <iostream>
#include <thread>

#include "COMConnection.h"
#include "Log.h"
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
	sp_blocking_write(port_, slip_data.data(), slip_data.size(), 60 * 1000);
}

void COMConnection::create_read_channel()
{
	reading_thread_ = std::thread([self = shared_from_this()]() {
	    std::vector<uint8_t> complete_data;
		std::vector<uint8_t> buffer(1024);
		while (self->is_connected())
		{
			int bytes_read = 0;
			do
			{
				bytes_read = sp_nonblocking_read(self->port_, buffer.data(), buffer.size());
				if (bytes_read < 0)
				{
					LogFileOutput("Error in sp_nonblocking_read(): %d (%s)\n", sp_last_error_code(), sp_last_error_message());
					self->set_is_connected(false);
					sp_close(self->port_);
					sp_free_port(self->port_);
					self->port_ = nullptr;
				}
				if (bytes_read > 0)
				{
					// LogFileOutput("SmartPortOverSlip COMConnection, inserting data, bytes_read: %d\n", bytes_read);
					complete_data.insert(complete_data.end(), buffer.begin(), buffer.begin() + bytes_read);
				}
			} while (bytes_read > 0);

			if (!complete_data.empty())			
			{
				std::vector<std::vector<uint8_t>> decoded_packets = SLIP::split_into_packets(complete_data.data(), complete_data.size());
				// LogFileOutput("SmartPortOverSlip COMConnection, packets decoded: %d\n", decoded_packets.size());

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
				complete_data.clear();				
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
