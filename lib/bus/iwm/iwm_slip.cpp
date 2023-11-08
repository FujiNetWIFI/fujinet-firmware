#ifndef ESP_PLATFORM
#ifdef BUILD_APPLE

#if SMARTPORT == SLIP

#include <string.h>

#include "iwm_slip.h"
#include "iwm.h"

uint8_t _phases;

sp_cmd_state_t sp_command_mode;

void iwm_slip::setup_gpio()
{
}

void iwm_slip::setup_spi()
{
}

void iwm_slip::spi_end()
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

int iwm_slip::iwm_send_packet_spi()
{
  return 0;
}

int iwm_slip::iwm_read_packet_spi(int n)
{
  return iwm_read_packet_spi(packet_buffer, n);
}

int iwm_slip::iwm_read_packet_spi(uint8_t* buffer, int n)
{
  return 0;
}

void iwm_slip::encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t* data, uint16_t num)
{
}

size_t iwm_slip::decode_data_packet(uint8_t* output_data)
{
  return decode_data_packet(packet_buffer, output_data);
}

size_t iwm_slip::decode_data_packet(uint8_t* input_data, uint8_t* output_data)
{
  return 0;
}

iwm_slip smartport;

#endif // SMARTPORT == SLIP

#endif // BUILD_APPLE
#endif // !ESP_PLATFORM
