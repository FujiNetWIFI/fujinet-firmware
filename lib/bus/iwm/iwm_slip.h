#ifdef BUILD_APPLE
#ifndef IWM_SLIP_H
#define IWM_SLIP_H

#include <stdint.h>

#define COMMAND_LEN 8 // Read Request / Write Request
#define PACKET_LEN  2 + 767 // Read Response

union cmdPacket_t
{
  struct
  {
    uint8_t seqno;
    uint8_t command;
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

  void setup_gpio();
  void setup_spi();

  void iwm_ack_set() {};
  void iwm_ack_clr() {};
  bool req_wait_for_falling_timeout(int t);
  bool req_wait_for_rising_timeout(int t);
  uint8_t iwm_phase_vector();

  int iwm_send_packet_spi();
  void spi_end();
  
  void encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t *data, uint16_t num);
  size_t decode_data_packet(uint8_t* output_data);
  size_t decode_data_packet(uint8_t* input_data, uint8_t* output_data);

  uint8_t packet_buffer[PACKET_LEN];
  size_t packet_size;
};

extern iwm_slip smartport;

#endif // IWM_SLIP_H
#endif // BUILD_APPLE