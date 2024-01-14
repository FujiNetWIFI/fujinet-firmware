#ifdef SP_OVER_SLIP

#include "ReadBlockRequest.h"

#include "ReadBlockResponse.h"
#include "SmartPortCodes.h"

ReadBlockRequest::ReadBlockRequest(const uint8_t request_sequence_number,
                                   const uint8_t sp_unit)
    : Request(request_sequence_number, SP_READ_BLOCK, sp_unit),
      block_number_{} {}

std::vector<uint8_t> ReadBlockRequest::serialize() const {
  std::vector<uint8_t> request_data;
  request_data.push_back(this->get_request_sequence_number());
  request_data.push_back(this->get_command_number());
  request_data.push_back(this->get_sp_unit());
  request_data.insert(request_data.end(), block_number_.begin(),
                      block_number_.end());
  return request_data;
}

std::unique_ptr<Response>
ReadBlockRequest::deserialize(const std::vector<uint8_t> &data) const {
  if (data.size() != 514) {
    throw std::runtime_error(
        "Not enough data to deserialize ReadBlockResponse");
  }

  auto response = std::make_unique<ReadBlockResponse>(data[0], data[1]);
  if (response->get_status() == 0) {
    response->set_block_data(data.begin() + 2, data.end());
  }
  return response;
}

const std::array<uint8_t, 3> &ReadBlockRequest::get_block_number() const {
  return block_number_;
}

void ReadBlockRequest::set_block_number_from_ptr(const uint8_t *ptr,
                                                 const size_t offset) {
  std::copy_n(ptr + offset, block_number_.size(), block_number_.begin());
}

void ReadBlockRequest::create_command(uint8_t *cmd_data) const {
  init_command(cmd_data);
  std::copy(block_number_.begin(), block_number_.end(), cmd_data + 4);
}

std::unique_ptr<Response>
ReadBlockRequest::create_response(uint8_t source, uint8_t status,
                                  const uint8_t *data, uint16_t num) const {
  std::unique_ptr<ReadBlockResponse> response =
      std::make_unique<ReadBlockResponse>(get_request_sequence_number(),
                                          status);
  // Copy the return data if the status is OK
  if (status == 0) {
    std::vector<uint8_t> data_vector(data, data + num);
    response->set_block_data(data_vector.begin(), data_vector.end());
  }
  return response;
}

#endif // SP_OVER_SLIP
