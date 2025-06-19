#ifdef DEV_RELAY_SLIP

#include <iostream>
#include <ostream>
#include <sstream>

#include "Request.h"
#include "../../utils/utils.h"

#include "../commands/Close.h"
#include "../commands/Control.h"
#include "../commands/Format.h"
#include "../commands/Init.h"
#include "../commands/Open.h"
#include "../commands/Read.h"
#include "../commands/ReadBlock.h"
#include "../commands/Status.h"
#include "../commands/Write.h"
#include "../commands/WriteBlock.h"

Request::Request(const uint8_t request_sequence_number, const uint8_t command_number, const uint8_t param_count, const uint8_t device_id) : Command(request_sequence_number), command_number_(command_number), param_count_(param_count), device_id_(device_id) {}

uint8_t Request::get_command_number() const { return command_number_; }

uint8_t Request::get_param_count() const { return param_count_; }

uint8_t Request::get_device_id() const { return device_id_; }

// All Request subclasses when writing to the command data will first initialise it and set command value
// cmd_data is really a pointer to a iwm_decoded_cmd_t object. This all needs rewriting to be cleaner.
void Request::init_command(uint8_t* cmd_data) const {
	std::fill(cmd_data, cmd_data + 9, 0);
	cmd_data[0] = get_command_number();
	cmd_data[1] = get_param_count();
	cmd_data[2] = get_device_id();
}

std::unique_ptr<Request> Request::from_packet(const std::vector<uint8_t>& packet) {
	std::unique_ptr<Request> request;
  uint8_t command = packet[1];
  switch(command) {

  case CMD_STATUS: {
    request = std::make_unique<StatusRequest>(packet[0], packet[2], packet[3], packet[6], packet[7]);
    break;
  }

  case CMD_CONTROL: {
    // +2 for control list length bytes we need to skip
    std::vector<uint8_t> payload(packet.begin() + 11+2, packet.end());
    request = std::make_unique<ControlRequest>(packet[0], packet[2], packet[3], packet[6], packet[7], payload);
    break;
  }

  case CMD_READ_BLOCK: {
    auto readBlockRequest = std::make_unique<ReadBlockRequest>(packet[0], packet[2], packet[3]);
    readBlockRequest->set_block_number_from_ptr(packet.data(), 6);
    request = std::move(readBlockRequest);
    break;
  }

  case CMD_WRITE_BLOCK: {
    auto writeBlockRequest = std::make_unique<WriteBlockRequest>(packet[0], packet[2], packet[3]);
    writeBlockRequest->set_block_number_from_ptr(packet.data(), 6);
    writeBlockRequest->set_block_data_from_ptr(packet.data(), 11);
		request = std::move(writeBlockRequest);
    break;
  }

  case CMD_FORMAT: {
    request = std::make_unique<FormatRequest>(packet[0], packet[2], packet[3]);
    break;
  }

  case CMD_INIT: {
    request = std::make_unique<InitRequest>(packet[0], packet[2], packet[3]);
    break;
  }

  case CMD_OPEN: {
    request = std::make_unique<OpenRequest>(packet[0], packet[2], packet[3]);
    break;
  }

  case CMD_CLOSE: {
    request = std::make_unique<CloseRequest>(packet[0], packet[2], packet[3]);
    break;
  }

  case CMD_READ: {
    auto readRequest = std::make_unique<ReadRequest>(packet[0], packet[2], packet[3]);
    readRequest->set_byte_count_from_ptr(packet.data(), 6);
    readRequest->set_address_from_ptr(packet.data(), 8);
		request = std::move(readRequest);
    break;
  }

  case CMD_WRITE: {
    auto writeRequest = std::make_unique<WriteRequest>(packet[0], packet[2], packet[3]);
    writeRequest->set_byte_count_from_ptr(packet.data(), 6);
    writeRequest->set_address_from_ptr(packet.data(), 8);
    writeRequest->set_data_from_ptr(packet.data(), 11, packet.size() - 11);
    request = std::move(writeRequest);
		break;
  }

  default: {
    std::ostringstream oss;
    oss << "Unknown command: " << static_cast<int>(command) << "\n"
        << "Full packet dump:\n" << util_hexdump(packet.data(), packet.size());
    std::cerr << oss.str() << std::endl;
    return nullptr;
  }

  }
	return request;
}


#endif
