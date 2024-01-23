#include "TCPConnection.h"

#include <cstring>
#include <iostream>
#include <thread>

// clang-format off
#ifdef WIN32
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32.lib")
  #define CLOSE_SOCKET closesocket
  #define SOCKET_ERROR_CODE WSAGetLastError()
#else
  #include <sys/socket.h>
  #include <unistd.h>
  #include <errno.h>
  #define CLOSE_SOCKET close
  #define SOCKET_ERROR_CODE errno
  #define INVALID_SOCKET -1
  #define SOCKET_ERROR -1
#endif
// clang-format off

// #include "Log.h"
#include "SLIP.h"

void TCPConnection::close_connection()
{
  if (socket_ != 0)
  {
    CLOSE_SOCKET(socket_);
  }
  socket_ = 0;
}

void TCPConnection::send_data(const std::vector<uint8_t> &data)
{
  if (data.empty())
  {
    return;
  }

  const auto slip_data = SLIP::encode(data);
  send(socket_, reinterpret_cast<const char *>(slip_data.data()), slip_data.size(), 0);
}

void TCPConnection::create_read_channel()
{
  // Start a new thread to listen for incoming data
  reading_thread_ = std::thread([self = shared_from_this()]()
	{
		std::vector<uint8_t> complete_data;
		std::vector<uint8_t> buffer(1024);
		bool is_initialising = true;

		// Set a timeout on the socket
		timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		setsockopt(self->get_socket(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));

		while (self->is_connected() || is_initialising)
		{
			int valread = 0;
			do
			{
				if (is_initialising)
				{
					is_initialising = false;
					std::cout << "SmartPortOverSlip TCPConnection: connected" << std::endl;
					self->set_is_connected(true);
				}

				valread = recv(self->get_socket(), reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0);
				if (valread < 0)
				{
					// timeout is fine, just reloop.
					if (errno == EAGAIN || errno == EWOULDBLOCK || errno == 0)
					{
						continue;
					}
					// otherwise it was a genuine error.
					std::cerr << "Error in read thread for connection, errno: " << errno << " = " << strerror(errno) << std::endl;
					self->set_is_connected(false);
				}
				if (valread == 0)
				{
					// disconnected, this will end the loop and will be picked up higher up that we are no longer connected
					self->set_is_connected(false);
				}
				if (valread > 0)
				{
					complete_data.insert(complete_data.end(), buffer.begin(), buffer.begin() + valread);
				}
			} while (valread == 1024);

			if (!complete_data.empty())
			{
				std::vector<std::vector<uint8_t>> decoded_packets =
						SLIP::split_into_packets(complete_data.data(), complete_data.size());

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
		std::cout << "TCPConnection::create_read_channel - thread is EXITING" << std::endl;
	});
}
