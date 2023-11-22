#ifdef BUILD_APPLE

#ifdef SP_OVER_SLIP

#include <string.h>

#include "iwm_slip.h"
#include "iwm.h"

#define PHASE_IDLE   0b0000
#define PHASE_ENABLE 0b1010
#define PHASE_RESET  0b0101

sp_cmd_state_t sp_command_mode;

void iwm_slip::setup_gpio()
{
}

void iwm_slip::setup_spi()
{
}

bool iwm_slip::req_wait_for_falling_timeout(int t)
{
  return false;
}

bool iwm_slip::req_wait_for_rising_timeout(int t)
{
  return false;
}

uint8_t iwm_slip::iwm_phase_vector()
{
  // Check for a new Request Packet on the transport layer
  //
  // if (no new packet)
  //   return PHASE_IDLE;
  //
  // Read the Request Packet "header" into command_packet
  // 
  // if (Data or Control List)
  //   Read the Data or Control List into packet_buffer
  //   Remember the packet_size
  //
  // return PHASE_ENABLE;
  return PHASE_IDLE;
}

int iwm_slip::iwm_send_packet_spi()
{
  // if (response is final)
  //   Write packet_buffer to the transport layer
  return 0;
}

void iwm_slip::spi_end()
{
}

void iwm_slip::encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t* data, uint16_t num)
{
  // if (packet_type == status)
  //   Transform data into a Reponse Packet "header" placed in packet_buffer
  //   Remember packet_size
  //   if (!(Data or Status List))
  //     Mark response as final
  //
  // else
  //   Copy num bytes from data to packet_buffer + packet_size
  //   Add num to packet_size
  //   Mark response as final 
}

size_t iwm_slip::decode_data_packet(uint8_t* output_data)
{
  // Copy packet_size bytes from packet_buffer to output_data
  // Return packet_size
  return 0;
}

size_t iwm_slip::decode_data_packet(uint8_t* input_data, uint8_t* output_data)
{
  // Transform from a cmdPacket_t into a iwm_decoded_cmd_t
  return 0;
}

iwm_slip smartport;

#endif // SP_OVER_SLIP

#endif // BUILD_APPLE
