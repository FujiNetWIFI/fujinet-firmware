#ifdef SP_OVER_SLIP

#include <sstream>

#include "Request.h"

#include "CloseRequest.h"
#include "CloseResponse.h"
#include "ControlRequest.h"
#include "ControlResponse.h"
#include "FormatRequest.h"
#include "FormatResponse.h"
#include "InitRequest.h"
#include "InitResponse.h"
#include "OpenRequest.h"
#include "OpenResponse.h"
#include "ReadBlockRequest.h"
#include "ReadBlockResponse.h"
#include "ReadRequest.h"
#include "ReadResponse.h"
#include "ResetRequest.h"
#include "ResetResponse.h"
#include "StatusRequest.h"
#include "StatusResponse.h"
#include "WriteBlockRequest.h"
#include "WriteBlockResponse.h"
#include "WriteRequest.h"
#include "WriteResponse.h"

Request::Request(const uint8_t request_sequence_number, const uint8_t command_number, const uint8_t sp_unit)
	: SmartPortPacket(request_sequence_number), command_number_(command_number), sp_unit_(sp_unit) {}

uint8_t Request::get_command_number() const { return command_number_; }

uint8_t Request::get_sp_unit() const { return sp_unit_; }

// All Request subclasses when writing to the command data will first initialise it and set command value
// cmd_data is really a pointer to a iwm_decoded_cmd_t object. This all needs rewriting to be cleaner.
void Request::init_command(uint8_t* cmd_data) const {
	std::fill(cmd_data, cmd_data + 9, 0);
	cmd_data[0] = get_command_number();
}

std::unique_ptr<Request> Request::from_packet(const std::vector<uint8_t>& packet) {
	std::unique_ptr<Request> request;
  uint8_t command = packet[1];
  switch(command) {

  case SP_STATUS: {
    request = std::make_unique<StatusRequest>(packet[0], packet[2], packet[3]);
    break;
  }

  case SP_CONTROL: {
    // +6 = 3 for "header", 1 for control code, 2 for length bytes we need to skip
    std::vector<uint8_t> payload(packet.begin() + 6, packet.end());
    request = std::make_unique<ControlRequest>(packet[0], packet[2], packet[3], payload);
    break;
  }

  case SP_READ_BLOCK: {
    auto readBlockRequest = std::make_unique<ReadBlockRequest>(packet[0], packet[2]);
    readBlockRequest->set_block_number_from_ptr(packet.data(), 3);
    request = std::move(readBlockRequest);
    break;
  }

  case SP_WRITE_BLOCK: {
    auto writeBlockRequest = std::make_unique<WriteBlockRequest>(packet[0], packet[2]);
    writeBlockRequest->set_block_number_from_ptr(packet.data(), 3);
    writeBlockRequest->set_block_data_from_ptr(packet.data(), 6);
		request = std::move(writeBlockRequest);
    break;
  }

  case SP_FORMAT: {
    request = std::make_unique<FormatRequest>(packet[0], packet[2]);
    break;
  }

  case SP_INIT: {
    request = std::make_unique<InitRequest>(packet[0], packet[2]);
    break;
  }

  case SP_OPEN: {
    request = std::make_unique<OpenRequest>(packet[0], packet[2]);
    break;
  }

  case SP_CLOSE: {
    request = std::make_unique<CloseRequest>(packet[0], packet[2]);
    break;
  }

  case SP_READ: {
    auto readRequest = std::make_unique<ReadRequest>(packet[0], packet[2]);
    readRequest->set_byte_count_from_ptr(packet.data(), 3);
    readRequest->set_address_from_ptr(packet.data(), 5);
		request = std::move(readRequest);
    break;
  }

  case SP_WRITE: {
    auto writeRequest = std::make_unique<WriteRequest>(packet[0], packet[2]);
    writeRequest->set_byte_count_from_ptr(packet.data(), 3);
    writeRequest->set_address_from_ptr(packet.data(), 5);
    writeRequest->set_data_from_ptr(packet.data(), 8, packet.size() - 8);
    request = std::move(writeRequest);
		break;
  }

  case SP_RESET: {
    request = std::make_unique<ResetRequest>(packet[0], packet[2]);
    break;
  }

  default: {
    std::ostringstream oss;
    oss << "Unknown command: %d" << command;
    throw std::runtime_error(oss.str());
    break;
  }

  }
	return request;
}

#endif // SP_OVER_SLIP
