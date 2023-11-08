#ifdef BUILD_APPLE
#ifndef IWM_SLIP_H
#define IWM_SLIP_H

#include <stdint.h>

#define BLOCK_PACKET_LEN    604 //606

#define PACKET_TYPE_CMD 0x80
#define PACKET_TYPE_STATUS 0x81
#define PACKET_TYPE_DATA 0x82

extern uint8_t _phases;

enum class iwm_packet_type_t
{
  cmd = PACKET_TYPE_CMD,
  status = PACKET_TYPE_STATUS,
  data = PACKET_TYPE_DATA,
  ext_cmd = PACKET_TYPE_CMD | 0x40,
  ext_status = PACKET_TYPE_STATUS | 0x40,
  ext_data = PACKET_TYPE_DATA | 0x40
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
  void spi_end();

  void iwm_ack_set() {};
  void iwm_ack_clr() {};
  bool req_wait_for_falling_timeout(int t);
  bool req_wait_for_rising_timeout(int t);
  uint8_t iwm_phase_vector() { return 0; };

  int iwm_send_packet_spi();
  int iwm_read_packet_spi(int n);
  int iwm_read_packet_spi(uint8_t *buffer, int n);
  
  void encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t *data, uint16_t num);
  size_t decode_data_packet(uint8_t* output_data);
  size_t decode_data_packet(uint8_t* input_data, uint8_t* output_data);

  uint8_t packet_buffer[BLOCK_PACKET_LEN]; //smartport packet buffer

  // For debug printing the checksum
  uint8_t calc_checksum;
  uint8_t pkt_checksum;

  // for tracking last checksum received for Liron bug
  uint8_t last_checksum;
};

extern iwm_slip smartport;

#endif // IWM_SLIP_H
#endif // BUILD_APPLE