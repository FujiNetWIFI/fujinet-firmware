#ifdef DEV_RELAY_SLIP

#include <sstream>

#include "Request.h"

#include "../commands/Close.h"
#include "../commands/Control.h"
#include "../commands/Format.h"
#include "../commands/Init.h"
#include "../commands/Open.h"
#include "../commands/Read.h"
#include "../commands/ReadBlock.h"
#include "../commands/Reset.h"
#include "../commands/Status.h"
#include "../commands/Write.h"
#include "../commands/WriteBlock.h"

Request::Request(const uint8_t request_sequence_number, const uint8_t command_number, const uint8_t device_id) : Command(request_sequence_number), command_number_(command_number), device_id_(device_id) {}

uint8_t Request::get_command_number() const { return command_number_; }

uint8_t Request::get_device_id() const { return device_id_; }

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

  case CMD_STATUS: {
    uint8_t network_unit = packet.size() > 4 ? packet[4] : 0;
    request = std::make_unique<StatusRequest>(packet[0], packet[2], packet[3], network_unit);
    break;
  }

  case CMD_CONTROL: {
    uint8_t network_unit = packet.size() > 4 ? packet[4] : 0;
    // +7 = 3 for "header", 1 for control code, 1 for network unit, 2 for length bytes we need to skip
    std::vector<uint8_t> payload(packet.begin() + 7, packet.end());
    request = std::make_unique<ControlRequest>(packet[0], packet[2], packet[3], network_unit, payload);
    break;
  }

  case CMD_READ_BLOCK: {
    auto readBlockRequest = std::make_unique<ReadBlockRequest>(packet[0], packet[2]);
    readBlockRequest->set_block_number_from_ptr(packet.data(), 3);
    request = std::move(readBlockRequest);
    break;
  }

  case CMD_WRITE_BLOCK: {
    auto writeBlockRequest = std::make_unique<WriteBlockRequest>(packet[0], packet[2]);
    writeBlockRequest->set_block_number_from_ptr(packet.data(), 3);
    writeBlockRequest->set_block_data_from_ptr(packet.data(), 6);
		request = std::move(writeBlockRequest);
    break;
  }

  case CMD_FORMAT: {
    request = std::make_unique<FormatRequest>(packet[0], packet[2]);
    break;
  }

  case CMD_INIT: {
    request = std::make_unique<InitRequest>(packet[0], packet[2]);
    break;
  }

  case CMD_OPEN: {
    request = std::make_unique<OpenRequest>(packet[0], packet[2]);
    break;
  }

  case CMD_CLOSE: {
    request = std::make_unique<CloseRequest>(packet[0], packet[2]);
    break;
  }

  case CMD_READ: {
    auto readRequest = std::make_unique<ReadRequest>(packet[0], packet[2]);
    readRequest->set_byte_count_from_ptr(packet.data(), 3);
    readRequest->set_address_from_ptr(packet.data(), 5);
		request = std::move(readRequest);
    break;
  }

  case CMD_WRITE: {
    auto writeRequest = std::make_unique<WriteRequest>(packet[0], packet[2]);
    writeRequest->set_byte_count_from_ptr(packet.data(), 3);
    writeRequest->set_address_from_ptr(packet.data(), 5);
    writeRequest->set_data_from_ptr(packet.data(), 8, packet.size() - 8);
    request = std::move(writeRequest);
		break;
  }

  case CMD_RESET: {
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


#endif
