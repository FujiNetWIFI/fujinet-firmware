#pragma once

#ifdef BUILD_APPLE
#ifdef DEV_RELAY_SLIP

#include <cstdint>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <functional>
#include "Connection.h"

#include "../../devrelay/types/Request.h"
#include "../../devrelay/types/Response.h"
#include "compat_inet.h"

#ifdef SLIP_PROTOCOL_NET
#include "connector_net.h"
#else
#include "connector_com.h"
#endif

#define COMMAND_LEN 11	   // Read Request / Write Request
#define PACKET_LEN 2 + 767 // Read Response

union cmdPacket_t
{
	struct
	{
		uint8_t seqno;
		uint8_t command;
		uint8_t params;
		uint8_t dest;
	};
	uint8_t data[COMMAND_LEN];
};

enum class iwm_packet_type_t
{
	status,
	data,
	ext_status,
	ext_data
};

enum class sp_cmd_state_t
{
	standby = 0,
	rxdata,
	command
};
extern sp_cmd_state_t sp_command_mode;

class iwm_slip
{
public:
	~iwm_slip();
	void setup_gpio();
	void setup_spi();

	void iwm_ack_set(){};
	void iwm_ack_clr(){};
	bool req_wait_for_falling_timeout(int t);
	bool req_wait_for_rising_timeout(int t);
	uint8_t iwm_phase_vector();

	int iwm_send_packet_spi();
	void spi_end();

	void encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t *data, uint16_t num);
	size_t decode_data_packet(uint8_t *output_data);
	size_t decode_data_packet(uint8_t *input_data, uint8_t *output_data);

	// void close_connection(int sock, bool report_error);
	// bool create_connection(in_addr_t host, int port);

#ifdef SLIP_PROTOCOL_NET
	connector_net connector;
#else
	connector_com connector;
#endif

	void wait_for_requests();
	void end_request_thread();

	uint8_t packet_buffer[PACKET_LEN];
	size_t packet_size;
	std::shared_ptr<Connection> connection_ = nullptr;
	std::thread request_thread_;
	std::atomic<bool> is_responding_{false};

	std::queue<std::vector<uint8_t>> request_queue_;
	std::mutex queue_mutex_;

	std::unique_ptr<Request> current_request;
	std::unique_ptr<Response> current_response;

	std::string ipt2str(iwm_packet_type_t packet_type)
	{
		switch (packet_type)
		{
		case iwm_packet_type_t::status:
			return "status";
		case iwm_packet_type_t::data:
			return "data";
		case iwm_packet_type_t::ext_status:
			return "ext_status";
		case iwm_packet_type_t::ext_data:
			return "ext_data";
		default:
			return "unknown";
		}
	}

private:
	void restart();
};

extern iwm_slip smartport;


#endif // DEV_RELAY_SLIP
#endif // BUILD_APPLE
